from __future__ import annotations

import argparse
import asyncio
import gzip
import hashlib
import json
import logging
import math
import os
import re
import shutil
import threading
import time
import uuid
from contextlib import asynccontextmanager, contextmanager, suppress
from dataclasses import dataclass
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation
from functools import lru_cache
from http import HTTPStatus
from pathlib import Path
from typing import Annotated, Any
from urllib.parse import quote

import geopandas as gpd
import numpy as np
import pandas as pd
import pyogrio
import shapely
from shapely import union_all
from shapely.strtree import STRtree
import yaml
from fastapi import FastAPI, HTTPException, Query, Request
from fastapi import Path as ApiPath
from fastapi.responses import (
    FileResponse,
    JSONResponse,
    Response,
)
from fastapi.staticfiles import StaticFiles
from pyproj import CRS, Transformer
from starlette.middleware.gzip import GZipMiddleware

try:
    import fcntl
except ImportError:  # pragma: no cover - Windows
    fcntl = None


LOGGER = logging.getLogger("gis_viewer")


BASE_DIR = Path(__file__).resolve().parent
CONFIG_PATH = BASE_DIR / "config.yaml"

DATA_ROOT = BASE_DIR / "data"
INPUT_ROOT = DATA_ROOT / "input"
OUTPUT_ROOT = DATA_ROOT / "output"

CACHE_ROOT = DATA_ROOT / "cache"
CACHE_INPUT_ROOT = CACHE_ROOT / "input"
CACHE_OUTPUT_ROOT = CACHE_ROOT / "output"
CACHE_LOCK_ROOT = CACHE_ROOT / ".locks"

STATIC_ROOT = BASE_DIR / "static"
TEMPLATE_ROOT = BASE_DIR / "templates"


CACHE_FORMAT_VERSION = 5
CACHE_MANIFEST_NAME = "manifest.json"

CACHE_LOCK_TIMEOUT_SECONDS = 15 * 60
CACHE_LOCK_STALE_SECONDS = 2 * 60 * 60

WGS84 = CRS.from_epsg(4326)


SAFE_SEGMENT_PATTERN = re.compile(
    r"^[A-Za-z0-9][A-Za-z0-9_.-]*$"
)
SAFE_PARAMETER_PATTERN = re.compile(
    r"^[A-Za-z0-9_.+-]+$"
)


SOLUTION_FILES = {
    "antennas": "antenna_positions.gpkg",
    "edges": "union_edges.gpkg",
    "visibility": "union_visibility.gpkg",
    "covered": "covered_polygons_with_coverage.gpkg",
    "uncovered": "uncovered_polygons_with_coverage.gpkg",
}

# Solutions live in data/output/<algorithm>/visualizations/ in directories
# named <source-id>_par_<k>_<tau>[_<run metadata>].solution.visualization.
SOLUTION_VISUALIZATIONS_DIRECTORY = "visualizations"
SOLUTION_DIRECTORY_SUFFIX = ".solution.visualization"

# Part of a solution directory name after "<source-id>_par_".
SOLUTION_PARAMETERS_PATTERN = re.compile(
    r"^(?P<k>[^_]+)_"
    r"(?P<tau>[0-9]+(?:\.[0-9]+)?)"
    r"(?:_(?P<metadata>.*))?"
    + re.escape(SOLUTION_DIRECTORY_SUFFIX)
    + r"$"
)

# The solution catalog (all solution directories) is rescanned at most
# this often, so new solutions appear without a restart.
SOLUTION_CATALOG_MAX_AGE_SECONDS = 10.0

SOURCE_LAYER_NAME = "source"

SOLUTION_LAYER_NAMES = (
    "antennas",
    "covered",
    "uncovered",
    "edges",
    "visibility",
    "coverage_labels",
)

ALLOWED_SOLUTION_LAYERS = frozenset(SOLUTION_LAYER_NAMES)

# Layers whose features carry attributes the frontend needs.
LAYER_PROPERTY_COLUMNS: dict[str, tuple[str, ...]] = {
    "edges": ("status",),
    "coverage_labels": ("coverage", "status"),
}

# How oversized single features are cut into pieces during chunking.
#  - "lines": a line that alone exceeds the chunk limit is cut into parts.
#  - "polygon-outline": every polygon is emitted as fill-only polygon
#    pieces plus its boundary as stroke-only lines, so that cutting a
#    huge polygon never shows seams along the cuts.
LAYER_SPLIT_MODES = {
    "edges": "lines",
    "visibility": "polygon-outline",
}

# Chunks of these layers are listed first when they are equally close to
# the requested centre.
LAYER_PRIORITY = {
    "source": 0,
    "antennas": 0,
    "covered": 1,
    "uncovered": 1,
    "edges": 2,
    "coverage_labels": 3,
    "visibility": 4,
}

CHUNK_FORMAT = "kd-v1"
CHUNK_DIRECTORY_NAME = "chunks"
CHUNK_FILE_SUFFIX = ".geojson.gz"
CHUNK_GZIP_LEVEL = 6
CHUNK_ID_PATTERN = re.compile(r"^[01]{1,64}$")
COORDINATE_DECIMALS = 7

DEFAULT_CHUNK_MAX_BYTES = 512 * 1024
MINIMUM_CHUNK_MAX_BYTES = 4 * 1024

FEATURE_COLLECTION_PREFIX = b'{"type":"FeatureCollection","features":['
FEATURE_COLLECTION_SUFFIX = b"]}"
CHUNK_ENVELOPE_BYTES = (
    len(FEATURE_COLLECTION_PREFIX)
    + len(FEATURE_COLLECTION_SUFFIX)
)

# Serialised size of one coordinate pair, used to decide how finely an
# oversized geometry is cut before it is serialised.
ESTIMATED_BYTES_PER_COORDINATE = 26

# Fill pieces of a cut polygon overlap their neighbours by this many
# degrees. They are drawn opaquely in their own pane, so the overlap is
# invisible, while it prevents anti-aliasing seams along the cuts.
CLIP_FILL_OVERLAP_DEGREES = 5e-4

# Margin around a geometry's own extent for the outermost cut rectangle.
# Without it, line segments lying exactly on the extent would be dropped.
CLIP_OUTER_MARGIN_DEGREES = 1e-6


_LOCAL_CACHE_LOCKS: dict[str, threading.Lock] = {}
_LOCAL_CACHE_LOCKS_GUARD = threading.Lock()

# Set during server shutdown so that long cache builds stop early.
_SHUTDOWN_EVENT = threading.Event()


class CacheBuildCancelled(RuntimeError):
    """Raised inside a cache build when the server is shutting down."""


def _raise_if_shutting_down() -> None:
    if _SHUTDOWN_EVENT.is_set():
        raise CacheBuildCancelled(
            "Cache generation was cancelled because the "
            "server is shutting down."
        )


def _environment_flag(
    variable_name: str,
    default: bool,
) -> bool:
    raw_value = os.getenv(variable_name)

    if raw_value is None:
        return default

    return raw_value.strip().lower() not in {
        "0",
        "false",
        "no",
        "off",
    }


STARTUP_PRECOMPUTE_ENABLED = _environment_flag(
    "GIS_VIEWER_STARTUP_PRECOMPUTE",
    True,
)
STARTUP_PRECOMPUTE_FORCE = False


for cache_directory in (
    CACHE_INPUT_ROOT,
    CACHE_OUTPUT_ROOT,
    CACHE_LOCK_ROOT,
):
    cache_directory.mkdir(parents=True, exist_ok=True)


def _require_safe_segment(
    value: Any,
    description: str,
) -> str:
    value = str(value)

    if (
        value in {".", ".."}
        or not SAFE_SEGMENT_PATTERN.fullmatch(value)
    ):
        raise RuntimeError(
            f"Invalid {description} '{value}'. Only letters, numbers, "
            "'.', '_', and '-' are allowed."
        )

    return value


def _normalise_parameter_values(
    values: Any,
    description: str,
    allow_empty: bool = False,
) -> list[str]:
    if values is None and allow_empty:
        return []

    if not isinstance(values, list) or (not values and not allow_empty):
        raise RuntimeError(
            f"Configuration entry '{description}' must be a "
            "non-empty list."
        )

    result: list[str] = []
    seen: set[str] = set()

    for raw_value in values:
        value = str(raw_value)

        if not SAFE_PARAMETER_PATTERN.fullmatch(value):
            raise RuntimeError(
                f"Invalid value '{value}' in '{description}'. "
                "Parameter values may only contain letters, numbers, "
                "'.', '_', '+', and '-'."
            )

        if value in seen:
            raise RuntimeError(
                f"Duplicate value '{value}' in '{description}'."
            )

        seen.add(value)
        result.append(value)

    return result


def _source_parameter_values(
    source_parameters: dict[str, Any],
    name: str,
    source_id: str,
) -> list[str] | None:
    """Return a source's own k or tau values, or None if it has none."""
    values = source_parameters.get(name)

    if values is None:
        return None

    return _normalise_parameter_values(
        values,
        f"sources[{source_id}].parameters.{name}",
        allow_empty=True,
    )


SOURCE_FILE_SUFFIXES = {".gpkg", ".geojson", ".json"}


def _discover_sources(
    sources: dict[str, dict[str, Any]],
) -> None:
    """Add every source file in data/input that is not configured yet.

    The file name without extension becomes the source ID and label.
    """
    if not INPUT_ROOT.is_dir():
        return

    configured_files = {source["file"] for source in sources.values()}

    for path in sorted(INPUT_ROOT.iterdir()):
        if (
            not path.is_file()
            or path.suffix.lower() not in SOURCE_FILE_SUFFIXES
            or path.name in configured_files
        ):
            continue

        source_id = path.stem

        if (
            not SAFE_SEGMENT_PATTERN.fullmatch(source_id)
            or source_id in sources
        ):
            LOGGER.warning(
                "Not adding source file '%s': its name is not a valid, "
                "unused source ID.",
                path.name,
            )
            continue

        sources[source_id] = {
            "id": source_id,
            "label": source_id,
            "file": path.name,
            "layer": None,
            "k_values": None,
            "tau_values": None,
        }


def _discover_algorithms(
    algorithms: dict[str, dict[str, str]],
) -> None:
    """Add every algorithm directory in data/output not configured yet.

    An algorithm directory is a directory containing a "visualizations"
    directory. Its name becomes the algorithm ID and label.
    """
    if not OUTPUT_ROOT.is_dir():
        return

    configured_directories = {
        algorithm["directory"] for algorithm in algorithms.values()
    }

    for path in sorted(OUTPUT_ROOT.iterdir()):
        if (
            not path.is_dir()
            or path.name in configured_directories
            or not (path / SOLUTION_VISUALIZATIONS_DIRECTORY).is_dir()
        ):
            continue

        if (
            not SAFE_SEGMENT_PATTERN.fullmatch(path.name)
            or path.name in algorithms
        ):
            LOGGER.warning(
                "Not adding algorithm directory '%s': its name is not "
                "a valid, unused algorithm ID.",
                path.name,
            )
            continue

        algorithms[path.name] = {
            "id": path.name,
            "label": path.name,
            "short_label": path.name,
            "directory": path.name,
        }


def _load_config() -> dict[str, Any]:
    if not CONFIG_PATH.is_file():
        raise RuntimeError(
            f"Missing configuration file: {CONFIG_PATH}"
        )

    with CONFIG_PATH.open(
        "r",
        encoding="utf-8",
    ) as config_file:
        raw_config = yaml.safe_load(config_file) or {}

    app_config = raw_config.get("app") or {}
    map_config = raw_config.get("map") or {}
    parameter_config = raw_config.get("parameters") or {}

    title = str(
        app_config.get("title", "GIS Cup 2026 Solution Viewer")
    )

    discovery_config = raw_config.get("discovery") or {}

    if not isinstance(discovery_config, dict):
        raise RuntimeError(
            "The configuration entry 'discovery' must be a mapping."
        )

    discovery = {
        "sources": bool(discovery_config.get("sources", True)),
        "solutions": bool(discovery_config.get("solutions", True)),
    }

    raw_sources = raw_config.get("sources") or []

    if not isinstance(raw_sources, list):
        raise RuntimeError(
            "The configuration entry 'sources' must be a list."
        )

    sources: dict[str, dict[str, Any]] = {}

    for raw_source in raw_sources:
        if (
            not isinstance(raw_source, dict)
            or "id" not in raw_source
        ):
            raise RuntimeError(
                "Every source must be an object containing an 'id'."
            )

        source_id = _require_safe_segment(
            raw_source["id"],
            "source ID",
        )

        if source_id in sources:
            raise RuntimeError(
                f"Duplicate source ID '{source_id}'."
            )

        file_name = str(
            raw_source.get(
                "file",
                f"{source_id}.geojson",
            )
        )

        file_path = Path(file_name)
        if (
            file_path.name != file_name
            or file_path.suffix.lower() not in SOURCE_FILE_SUFFIXES
        ):
            raise RuntimeError(
                f"The source file '{file_name}' must be a "
                "GeoPackage or GeoJSON filename without directory "
                "components."
            )

        layer = raw_source.get("layer")

        if layer is not None:
            layer = str(layer)

        source_parameters = raw_source.get("parameters") or {}

        if not isinstance(source_parameters, dict):
            raise RuntimeError(
                f"The entry 'parameters' of source '{source_id}' must "
                "be a mapping."
            )

        sources[source_id] = {
            "id": source_id,
            "label": str(
                raw_source.get("label", source_id)
            ),
            "file": file_name,
            "layer": layer,
            # None means that the global values apply; see below.
            "k_values": _source_parameter_values(
                source_parameters,
                "k_values",
                source_id,
            ),
            "tau_values": _source_parameter_values(
                source_parameters,
                "tau_values",
                source_id,
            ),
        }

    if discovery["sources"]:
        _discover_sources(sources)

    if not sources:
        raise RuntimeError(
            "No source is configured or found. Add a source to "
            "'sources', or enable 'discovery.sources' and place source "
            "files in data/input."
        )

    raw_algorithms = raw_config.get("algorithms") or []

    if not isinstance(raw_algorithms, list):
        raise RuntimeError(
            "The configuration entry 'algorithms' must be a list."
        )

    algorithms: dict[str, dict[str, str]] = {}

    for raw_algorithm in raw_algorithms:
        if (
            not isinstance(raw_algorithm, dict)
            or "id" not in raw_algorithm
        ):
            raise RuntimeError(
                "Every algorithm must be an object containing an "
                "'id'."
            )

        algorithm_id = _require_safe_segment(
            raw_algorithm["id"],
            "algorithm ID",
        )

        if algorithm_id in algorithms:
            raise RuntimeError(
                f"Duplicate algorithm ID '{algorithm_id}'."
            )

        directory = _require_safe_segment(
            raw_algorithm.get(
                "directory",
                algorithm_id,
            ),
            f"directory for algorithm '{algorithm_id}'",
        )

        label = str(
            raw_algorithm.get(
                "label",
                algorithm_id,
            )
        )

        algorithms[algorithm_id] = {
            "id": algorithm_id,
            "label": label,
            # Shown in the statistics table, where space is short.
            "short_label": str(
                raw_algorithm.get(
                    "short_label",
                    label,
                )
            ),
            "directory": directory,
        }

    if discovery["solutions"]:
        _discover_algorithms(algorithms)

    # With discovered solutions, k and tau come from the solution names;
    # configured values are then optional and only add choices. A source
    # with its own values uses them instead of the global ones.
    k_values = _normalise_parameter_values(
        parameter_config.get("k_values"),
        "parameters.k_values",
        allow_empty=True,
    )
    tau_values = _normalise_parameter_values(
        parameter_config.get("tau_values"),
        "parameters.tau_values",
        allow_empty=True,
    )

    for source in sources.values():
        if source["k_values"] is None:
            source["k_values"] = k_values

        if source["tau_values"] is None:
            source["tau_values"] = tau_values

        if not discovery["solutions"]:
            for name in ("k_values", "tau_values"):
                if not source[name]:
                    raise RuntimeError(
                        f"No {name} for source '{source['id']}'. Set "
                        f"'parameters.{name}' or the source's own "
                        f"'parameters.{name}', or enable "
                        "'discovery.solutions'."
                    )

    center = map_config.get(
        "center",
        [50.7374, 7.0982],
    )

    if (
        not isinstance(center, (list, tuple))
        or len(center) != 2
    ):
        raise RuntimeError(
            "'map.center' must contain latitude and longitude."
        )

    latitude = float(center[0])
    longitude = float(center[1])

    if (
        not math.isfinite(latitude)
        or not math.isfinite(longitude)
        or not -90 <= latitude <= 90
        or not -180 <= longitude <= 180
    ):
        raise RuntimeError(
            "'map.center' contains invalid coordinates."
        )

    map_zoom = int(
        map_config.get("zoom", 14)
    )
    map_max_zoom = int(
        map_config.get("max_zoom", 20)
    )
    tile_max_native_zoom = int(
        map_config.get(
            "tile_max_native_zoom",
            min(19, map_max_zoom),
        )
    )
    coverage_label_min_zoom = int(
        map_config.get(
            "coverage_label_min_zoom",
            17,
        )
    )
    background_opacity = float(
        map_config.get(
            "background_opacity",
            1.0,
        )
    )
    overlay_initial_blend = int(
        map_config.get(
            "overlay_initial_blend",
            50,
        )
    )
    initial_source_zoom_offset = int(
        map_config.get(
            "initial_source_zoom_offset",
            1,
        )
    )
    coverage_label_multiplier = float(
        map_config.get(
            "coverage_label_multiplier",
            100.0,
        )
    )
    coverage_label_decimals = int(
        map_config.get(
            "coverage_label_decimals",
            2,
        )
    )
    coverage_label_suffix = str(
        map_config.get(
            "coverage_label_suffix",
            "",
        )
    )

    if map_zoom < 0:
        raise RuntimeError(
            "'map.zoom' must not be negative."
        )

    if map_max_zoom < map_zoom:
        raise RuntimeError(
            "'map.max_zoom' must be at least 'map.zoom'."
        )

    if not 0 <= tile_max_native_zoom <= map_max_zoom:
        raise RuntimeError(
            "'map.tile_max_native_zoom' must be between "
            "0 and 'map.max_zoom'."
        )

    if not (
        0
        <= coverage_label_min_zoom
        <= map_max_zoom
    ):
        raise RuntimeError(
            "'map.coverage_label_min_zoom' must be between "
            "0 and 'map.max_zoom'."
        )

    if (
        not math.isfinite(background_opacity)
        or not 0.0 <= background_opacity <= 1.0
    ):
        raise RuntimeError(
            "'map.background_opacity' must be between 0 and 1."
        )

    if not 0 <= overlay_initial_blend <= 100:
        raise RuntimeError(
            "'map.overlay_initial_blend' must be between 0 and 100."
        )

    if not 0 <= initial_source_zoom_offset <= 4:
        raise RuntimeError(
            "'map.initial_source_zoom_offset' must be "
            "between 0 and 4."
        )
    
    if not math.isfinite(coverage_label_multiplier):
        raise RuntimeError(
            "'map.coverage_label_multiplier' must be finite."
        )

    if not 0 <= coverage_label_decimals <= 8:
        raise RuntimeError(
            "'map.coverage_label_decimals' must be between 0 and 8."
        )

    cache_config = raw_config.get("cache") or {}

    if not isinstance(cache_config, dict):
        raise RuntimeError(
            "The configuration entry 'cache' must be a mapping."
        )

    chunk_max_bytes = int(
        cache_config.get(
            "chunk_max_bytes",
            DEFAULT_CHUNK_MAX_BYTES,
        )
    )

    if chunk_max_bytes < MINIMUM_CHUNK_MAX_BYTES:
        raise RuntimeError(
            "'cache.chunk_max_bytes' must be at least "
            f"{MINIMUM_CHUNK_MAX_BYTES}."
        )

    raw_chunk_limits = (
        cache_config.get("chunk_max_bytes_by_layer") or {}
    )

    if not isinstance(raw_chunk_limits, dict):
        raise RuntimeError(
            "'cache.chunk_max_bytes_by_layer' must be a mapping "
            "from layer name to a byte limit."
        )

    known_chunk_layers = {
        SOURCE_LAYER_NAME,
        *SOLUTION_LAYER_NAMES,
    }
    chunk_max_bytes_by_layer: dict[str, int] = {}

    for raw_layer, raw_limit in raw_chunk_limits.items():
        layer_name = str(raw_layer)

        if layer_name not in known_chunk_layers:
            raise RuntimeError(
                f"Unknown layer '{layer_name}' in "
                "'cache.chunk_max_bytes_by_layer'. Known layers: "
                f"{', '.join(sorted(known_chunk_layers))}."
            )

        limit = int(raw_limit)

        if limit < MINIMUM_CHUNK_MAX_BYTES:
            raise RuntimeError(
                "Chunk limits in 'cache.chunk_max_bytes_by_layer' "
                f"must be at least {MINIMUM_CHUNK_MAX_BYTES}."
            )

        chunk_max_bytes_by_layer[layer_name] = limit

    return {
        "title": title,
        "sources": sources,
        "algorithms": algorithms,
        "discovery": discovery,
        "cache": {
            "chunk_max_bytes": chunk_max_bytes,
            "chunk_max_bytes_by_layer": chunk_max_bytes_by_layer,
        },
        "map": {
            "center": [latitude, longitude],
            "zoom": map_zoom,
            "max_zoom": map_max_zoom,
            "tile_max_native_zoom": tile_max_native_zoom,
            "tile_url": str(
                map_config.get(
                    "tile_url",
                    "https://tile.openstreetmap.org/"
                    "{z}/{x}/{y}.png",
                )
            ),
            "attribution": str(
                map_config.get(
                    "attribution",
                    '&copy; <a href="https://www.openstreetmap.org/'
                    'copyright">OpenStreetMap</a> contributors',
                )
            ),
            "coverage_label_min_zoom": (
                coverage_label_min_zoom
            ),
            "background_opacity": background_opacity,
            "overlay_initial_blend": overlay_initial_blend,
            "initial_source_zoom_offset": (
                initial_source_zoom_offset
            ),
            "coverage_label_multiplier": (
                coverage_label_multiplier
            ),
            "coverage_label_decimals": (
                coverage_label_decimals
            ),
            "coverage_label_suffix": coverage_label_suffix,
        },
    }


CONFIG = _load_config()


async def _run_startup_precomputation(
    application: FastAPI,
) -> None:
    try:
        LOGGER.info(
            "Starting background cache precomputation."
        )

        report = await asyncio.to_thread(
            precompute_all,
            STARTUP_PRECOMPUTE_FORCE,
        )

        application.state.precompute_report = report

        LOGGER.info(
            "Background cache precomputation %s: %s",
            "stopped" if report.get("stopped") else "completed",
            json.dumps(report, sort_keys=True),
        )
    except asyncio.CancelledError:
        raise
    except Exception:
        LOGGER.exception(
            "Background cache precomputation failed."
        )


@asynccontextmanager
async def _application_lifespan(
    application: FastAPI,
):
    application.state.precompute_report = None
    application.state.precompute_task = None

    task: asyncio.Task[None] | None = None

    if STARTUP_PRECOMPUTE_ENABLED:
        task = asyncio.create_task(
            _run_startup_precomputation(application),
            name="cache-precomputation",
        )
        application.state.precompute_task = task
    else:
        LOGGER.info(
            "Automatic startup cache precomputation is disabled."
        )

    try:
        yield
    finally:
        # The precomputation runs in a worker thread, which cannot be
        # cancelled. The event makes running cache builds stop at their
        # next checkpoint, so shutdown does not wait for a full build.
        _SHUTDOWN_EVENT.set()

        if task is not None and not task.done():
            task.cancel()

            with suppress(asyncio.CancelledError):
                await task


app = FastAPI(
    title=CONFIG["title"],
    description=(
        "Interactive display of building visibility solutions."
    ),
    lifespan=_application_lifespan,
)

app.add_middleware(
    GZipMiddleware,
    minimum_size=1000,
)

app.mount(
    "/static",
    StaticFiles(directory=STATIC_ROOT),
    name="static",
)


def _safe_join(
    root: Path,
    *parts: str,
) -> Path:
    resolved_root = root.resolve()
    candidate = resolved_root.joinpath(*parts).resolve()

    try:
        candidate.relative_to(resolved_root)
    except ValueError as exc:
        raise RuntimeError(
            "Resolved path escapes its configured data directory."
        ) from exc

    return candidate


def _get_source(
    source_id: str,
) -> dict[str, Any]:
    source = CONFIG["sources"].get(source_id)

    if source is None:
        raise HTTPException(
            status_code=404,
            detail=f"Unknown source '{source_id}'.",
        )

    return source


def _get_algorithm(
    algorithm_id: str,
) -> dict[str, str]:
    algorithm = CONFIG["algorithms"].get(
        algorithm_id
    )

    if algorithm is None:
        raise HTTPException(
            status_code=404,
            detail=f"Unknown algorithm '{algorithm_id}'.",
        )

    return algorithm


def _validate_parameter(
    raw_value: str,
    configured_values: list[str],
    description: str,
) -> str:
    if raw_value not in configured_values:
        raise HTTPException(
            status_code=400,
            detail=(
                f"Invalid {description} value '{raw_value}'. "
                "Allowed values are: "
                f"{', '.join(configured_values)}."
            ),
        )

    return raw_value


def _tau_directory_token(
    raw_tau: str,
) -> str:
    try:
        value = Decimal(raw_tau)
    except InvalidOperation as exc:
        raise HTTPException(
            status_code=400,
            detail=(
                f"Tau value '{raw_tau}' is not a valid number."
            ),
        ) from exc

    if (
        not value.is_finite()
        or not Decimal("0") <= value <= Decimal("1")
    ):
        raise HTTPException(
            status_code=400,
            detail=(
                f"Tau value '{raw_tau}' must be between 0 and 1."
            ),
        )

    if value == 0:
        value = Decimal("0")

    return format(value, ".6f")


def _normalised_decimal_label(
    value: Decimal,
) -> str:
    text = format(value, "f").rstrip("0").rstrip(".")
    return text or "0"


def _parameter_sort_key(
    value: str,
) -> tuple[int, Any]:
    try:
        return 0, Decimal(value)
    except InvalidOperation:
        return 1, value


def _normalised_tau(
    raw_tau: str,
) -> str:
    """Validate a tau value and return its normalised label ("0.32")."""
    return _normalised_decimal_label(
        Decimal(_tau_directory_token(raw_tau))
    )


def _visualizations_directory(
    algorithm_config: dict[str, str],
) -> Path:
    return _safe_join(
        OUTPUT_ROOT,
        algorithm_config["directory"],
        SOLUTION_VISUALIZATIONS_DIRECTORY,
    )


@dataclass(frozen=True)
class SolutionRun:
    """One solution directory, described by its name."""

    source_id: str
    algorithm_id: str
    k: str
    # Normalised, for example "0.32" for "0.320000" in the name.
    tau: str
    path: Path
    # Number of covered buildings, from "_val_<n>" in the name.
    value: int | None
    modified_ns: int


def _parse_solution_directory_name(
    directory_name: str,
    source_ids: Any,
) -> tuple[str, str, str, int | None] | None:
    """Return (source ID, k, tau, value) for a solution directory name.

    The name starts with the source ID followed by "_par_". Source IDs may
    themselves contain underscores, so the longest matching ID wins.
    """
    matching_ids = [
        source_id
        for source_id in source_ids
        if directory_name.startswith(f"{source_id}_par_")
    ]

    if not matching_ids:
        return None

    source_id = max(matching_ids, key=len)

    match = SOLUTION_PARAMETERS_PATTERN.fullmatch(
        directory_name[len(source_id) + len("_par_"):]
    )

    if match is None:
        return None

    try:
        tau_value = Decimal(match.group("tau"))
    except InvalidOperation:
        return None

    if (
        not tau_value.is_finite()
        or not Decimal("0")
        <= tau_value
        <= Decimal("1")
    ):
        return None

    values = re.findall(
        r"(?:^|_)val_([0-9]+)(?=_|$)",
        match.group("metadata") or "",
    )

    return (
        source_id,
        match.group("k"),
        _normalised_decimal_label(tau_value),
        int(values[-1]) if values else None,
    )


def _solution_tokens_from_directory(
    source_config: dict[str, Any],
    directory_name: str,
) -> tuple[str, str] | None:
    parsed = _parse_solution_directory_name(
        directory_name,
        (source_config["id"],),
    )

    if parsed is None:
        return None

    _, k_token, tau_token, _ = parsed

    return k_token, tau_token


SolutionKey = tuple[str, str, str, str]


def _scan_solution_runs() -> dict[SolutionKey, list[SolutionRun]]:
    """Find all solution directories of the configured algorithms.

    The result maps (source ID, algorithm ID, k, tau) to the runs found for
    that combination. Directories whose names do not follow the naming
    convention, or that belong to no configured source, are ignored.
    """
    catalog: dict[SolutionKey, list[SolutionRun]] = {}
    source_ids = tuple(CONFIG["sources"])

    for algorithm in CONFIG["algorithms"].values():
        try:
            directory = _visualizations_directory(algorithm)
        except RuntimeError:
            continue

        if not directory.is_dir():
            continue

        for entry in directory.iterdir():
            if not entry.name.endswith(SOLUTION_DIRECTORY_SUFFIX):
                continue

            parsed = _parse_solution_directory_name(
                entry.name,
                source_ids,
            )

            if parsed is None or not entry.is_dir():
                continue

            try:
                path = _safe_join(directory, entry.name)
                modified_ns = path.stat().st_mtime_ns
            except (RuntimeError, OSError):
                continue

            source_id, k_token, tau_token, value = parsed

            catalog.setdefault(
                (source_id, algorithm["id"], k_token, tau_token),
                [],
            ).append(
                SolutionRun(
                    source_id=source_id,
                    algorithm_id=algorithm["id"],
                    k=k_token,
                    tau=tau_token,
                    path=path,
                    value=value,
                    modified_ns=modified_ns,
                )
            )

    return catalog


_SOLUTION_CATALOG_LOCK = threading.Lock()
_solution_catalog_cache: tuple[
    float,
    dict[SolutionKey, list[SolutionRun]],
    str,
] | None = None


def _catalog_version(
    catalog: dict[SolutionKey, list[SolutionRun]],
) -> str:
    """A short hash that changes whenever a solution directory is added,
    removed, or renamed. The frontend compares it to decide whether its
    statistics are outdated."""
    digest = hashlib.sha256()

    for key in sorted(catalog):
        for run in sorted(catalog[key], key=lambda run: run.path.name):
            digest.update(f"{'|'.join(key)}|{run.path.name}\n".encode())

    return digest.hexdigest()[:16]


def _refreshed_solution_catalog() -> tuple[
    dict[SolutionKey, list[SolutionRun]],
    str,
]:
    """The solution directories and their version, rescanned at most every
    few seconds."""
    global _solution_catalog_cache

    with _SOLUTION_CATALOG_LOCK:
        now = time.monotonic()

        if (
            _solution_catalog_cache is None
            or now - _solution_catalog_cache[0]
            > SOLUTION_CATALOG_MAX_AGE_SECONDS
        ):
            catalog = _scan_solution_runs()
            _solution_catalog_cache = (
                now,
                catalog,
                _catalog_version(catalog),
            )

        return _solution_catalog_cache[1], _solution_catalog_cache[2]


def _solution_catalog() -> dict[SolutionKey, list[SolutionRun]]:
    return _refreshed_solution_catalog()[0]


def _solution_catalog_version() -> str:
    return _refreshed_solution_catalog()[1]


def _select_run(
    runs: list[SolutionRun],
) -> SolutionRun:
    """Choose the run to display among several for one combination.

    The best run wins: the one with the most covered buildings ("_val_" in
    its name). Runs without a value rank last; among equal values, the
    newest directory wins.
    """
    return max(
        runs,
        key=lambda run: (
            run.value is not None,
            run.value or 0,
            run.modified_ns,
            run.path.name,
        ),
    )


def _find_solution_runs(
    source_config: dict[str, Any],
    algorithm_config: dict[str, str],
    k_token: str,
    tau_token: str,
) -> list[SolutionRun]:
    """All runs of one combination; raises 404 if there are none."""
    runs = _solution_catalog().get(
        (
            source_config["id"],
            algorithm_config["id"],
            k_token,
            _normalised_tau(tau_token),
        )
    )

    if runs:
        return runs

    visualizations_directory = _visualizations_directory(
        algorithm_config
    )
    relative_path = _relative_path_label(visualizations_directory)

    if not visualizations_directory.is_dir():
        detail = f"Solution directory '{relative_path}' does not exist."
    else:
        expected_stem = (
            f"{source_config['id']}_par_"
            f"{k_token}_{_tau_directory_token(tau_token)}"
        )
        detail = (
            "No matching solution directory was found. "
            f"Expected '{expected_stem}_*"
            f"{SOLUTION_DIRECTORY_SUFFIX}' in '{relative_path}'."
        )

    raise HTTPException(status_code=404, detail=detail)


def _find_solution_directory(
    source_config: dict[str, Any],
    algorithm_config: dict[str, str],
    k_token: str,
    tau_token: str,
) -> Path:
    return _select_run(
        _find_solution_runs(
            source_config,
            algorithm_config,
            k_token,
            tau_token,
        )
    ).path


def _runs_summary(
    runs: list[SolutionRun],
) -> dict[str, Any]:
    """Describe the runs of one combination for the frontend."""
    selected = _select_run(runs)

    return {
        "count": len(runs),
        "selected": selected.path.name,
        "selected_value": selected.value,
        "values": sorted(
            (run.value for run in runs if run.value is not None),
            reverse=True,
        ),
    }


def _resolve_solution_request(
    source_id: str,
    algorithm_id: str,
    k: str,
    tau: str,
) -> tuple[
    dict[str, Any],
    dict[str, str],
    str,
    str,
    Path,
]:
    source = _get_source(source_id)
    algorithm = _get_algorithm(algorithm_id)

    if CONFIG["discovery"]["solutions"]:
        # Any k and tau may exist; the solution lookup decides.
        if not SAFE_PARAMETER_PATTERN.fullmatch(k):
            raise HTTPException(
                status_code=400,
                detail=f"Invalid k value '{k}'.",
            )

        k_token = k
        tau_token = _normalised_tau(tau)
    else:
        k_token = _validate_parameter(
            k,
            source["k_values"],
            "k",
        )
        tau_token = _validate_parameter(
            tau,
            source["tau_values"],
            "tau",
        )

    solution_directory = _find_solution_directory(
        source,
        algorithm,
        k_token,
        tau_token,
    )

    return (
        source,
        algorithm,
        k_token,
        tau_token,
        solution_directory,
    )


def _parse_bbox(
    raw_bbox: str | None,
) -> tuple[float, float, float, float] | None:
    if raw_bbox is None:
        return None

    parts = raw_bbox.split(",")

    if len(parts) != 4:
        raise HTTPException(
            status_code=400,
            detail=(
                "bbox must be "
                "minLon,minLat,maxLon,maxLat."
            ),
        )

    try:
        values = tuple(
            float(part)
            for part in parts
        )
    except ValueError as exc:
        raise HTTPException(
            status_code=400,
            detail=(
                "bbox must contain four numeric values."
            ),
        ) from exc

    if not all(
        math.isfinite(value)
        for value in values
    ):
        raise HTTPException(
            status_code=400,
            detail=(
                "bbox values must be finite numbers."
            ),
        )

    min_lon, min_lat, max_lon, max_lat = values

    if (
        min_lon >= max_lon
        or min_lat >= max_lat
        or min_lon < -180
        or max_lon > 180
        or min_lat < -90
        or max_lat > 90
    ):
        raise HTTPException(
            status_code=400,
            detail=(
                "Invalid EPSG:4326 bounding box."
            ),
        )

    return (
        min_lon,
        min_lat,
        max_lon,
        max_lat,
    )


def _read_geo_file(
    path: Path,
    display_name: str,
    layer: str | None = None,
) -> gpd.GeoDataFrame:
    if not path.is_file():
        raise HTTPException(
            status_code=404,
            detail=(
                f"Vector file '{display_name}' does not exist."
            ),
        )

    try:
        frame = pyogrio.read_dataframe(
            path,
            layer=layer,
            use_arrow=True,
        )
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Could not read '{display_name}': {exc}"
            ),
        ) from exc

    if frame.crs is None:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Vector file '{display_name}' has no "
                "coordinate reference system."
            ),
        )

    return frame


def _source_polygon_count(
    frame: gpd.GeoDataFrame,
) -> int:
    if frame.empty:
        raise HTTPException(
            status_code=500,
            detail=(
                "The selected source contains no polygons."
            ),
        )

    if (
        frame.geometry.isna().any()
        or frame.geometry.is_empty.any()
    ):
        raise HTTPException(
            status_code=500,
            detail=(
                "The selected source contains null or "
                "empty geometries."
            ),
        )

    geometry_types = set(
        frame.geometry.geom_type.dropna().unique()
    )
    invalid_types = geometry_types.difference(
        {"Polygon", "MultiPolygon"}
    )

    if invalid_types:
        invalid_text = ", ".join(
            sorted(invalid_types)
        )

        raise HTTPException(
            status_code=500,
            detail=(
                "The source must contain only Polygon or "
                "MultiPolygon geometries. "
                f"Found: {invalid_text}."
            ),
        )

    return len(frame)


@lru_cache(maxsize=1024)
def _cached_feature_count(
    path_string: str,
    modification_time_ns: int,
    file_size: int,
    layer: str | None,
) -> int:
    del modification_time_ns
    del file_size

    path = Path(path_string)

    try:
        info = pyogrio.read_info(
            path,
            layer=layer,
        )
        feature_count = info.get("features")

        if (
            feature_count is not None
            and int(feature_count) >= 0
        ):
            return int(feature_count)
    except Exception:
        pass

    read_options: dict[str, Any] = {
        "ignore_geometry": True,
    }

    if layer is not None:
        read_options["layer"] = layer

    return len(
        gpd.read_file(
            path,
            **read_options,
        )
    )


def _feature_count(
    path: Path,
    layer: str | None = None,
) -> int:
    if not path.is_file():
        raise HTTPException(
            status_code=404,
            detail=(
                f"Vector file '{path.name}' does not exist."
            ),
        )

    stat = path.stat()

    try:
        return _cached_feature_count(
            str(path.resolve()),
            stat.st_mtime_ns,
            stat.st_size,
            layer,
        )
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Could not count features in "
                f"'{path.name}': {exc}"
            ),
        ) from exc


def _relative_path_label(
    path: Path,
) -> str:
    try:
        return (
            path.resolve()
            .relative_to(BASE_DIR)
            .as_posix()
        )
    except ValueError:
        return path.name


def _file_signature(
    path: Path,
) -> dict[str, Any]:
    if not path.is_file():
        raise HTTPException(
            status_code=404,
            detail=(
                f"Required file '{path.name}' does not exist."
            ),
        )

    stat = path.stat()

    return {
        "path": _relative_path_label(path),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
    }


def _source_path(
    source_config: dict[str, Any],
) -> Path:
    return _safe_join(
        INPUT_ROOT,
        source_config["file"],
    )


def _source_cache_directory(
    source_config: dict[str, Any],
) -> Path:
    return _safe_join(
        CACHE_INPUT_ROOT,
        source_config["id"],
    )


def _solution_cache_directory(
    algorithm_config: dict[str, str],
    solution_directory: Path,
) -> Path:
    return _safe_join(
        CACHE_OUTPUT_ROOT,
        algorithm_config["directory"],
        solution_directory.name,
    )


def _chunk_max_bytes(
    layer_name: str,
) -> int:
    cache_config = CONFIG["cache"]

    return int(
        cache_config["chunk_max_bytes_by_layer"].get(
            layer_name,
            cache_config["chunk_max_bytes"],
        )
    )


def _chunk_settings(
    layer_names: tuple[str, ...],
) -> dict[str, Any]:
    return {
        "format": CHUNK_FORMAT,
        "coordinate_decimals": COORDINATE_DECIMALS,
        "max_bytes": {
            layer_name: _chunk_max_bytes(layer_name)
            for layer_name in layer_names
        },
    }


def _source_cache_fingerprint(
    source_config: dict[str, Any],
) -> dict[str, Any]:
    source_path = _source_path(source_config)

    return {
        "cache_format_version": CACHE_FORMAT_VERSION,
        "kind": "source",
        "source_id": source_config["id"],
        "source_layer": source_config.get("layer"),
        "chunking": _chunk_settings((SOURCE_LAYER_NAME,)),
        "source_file": _file_signature(source_path),
    }


def _solution_original_paths(
    solution_directory: Path,
) -> dict[str, Path]:
    paths = {
        layer_name: _safe_join(
            solution_directory,
            file_name,
        )
        for layer_name, file_name in SOLUTION_FILES.items()
    }

    missing_files = [
        path.name
        for path in paths.values()
        if not path.is_file()
    ]

    if missing_files:
        raise HTTPException(
            status_code=404,
            detail=(
                "The solution is incomplete. Missing files: "
                f"{', '.join(sorted(missing_files))}."
            ),
        )

    return paths


def _solution_cache_fingerprint(
    source_config: dict[str, Any],
    algorithm_config: dict[str, str],
    solution_directory: Path,
) -> dict[str, Any]:
    source_path = _source_path(source_config)
    original_paths = _solution_original_paths(
        solution_directory
    )

    return {
        "cache_format_version": CACHE_FORMAT_VERSION,
        "kind": "solution",
        "source_id": source_config["id"],
        "algorithm_id": algorithm_config["id"],
        "solution_directory": solution_directory.name,
        "chunking": _chunk_settings(SOLUTION_LAYER_NAMES),
        "edge_classification": "uncovered-boundary-v1",
        "label_method": (
            "representative-point-before-reprojection"
        ),
        "source_file": _file_signature(source_path),
        "solution_files": {
            layer_name: _file_signature(path)
            for layer_name, path in original_paths.items()
        },
    }


def _read_manifest_file(
    manifest_path: Path,
) -> dict[str, Any] | None:
    try:
        with manifest_path.open(
            "r",
            encoding="utf-8",
        ) as manifest_file:
            manifest = json.load(manifest_file)
    except (
        OSError,
        json.JSONDecodeError,
        TypeError,
        ValueError,
    ):
        return None

    if not isinstance(manifest, dict):
        return None

    return manifest


@lru_cache(maxsize=128)
def _manifest_for_signature(
    path_string: str,
    modification_time_ns: int,
    file_size: int,
) -> dict[str, Any] | None:
    del modification_time_ns
    del file_size

    manifest = _read_manifest_file(Path(path_string))

    if manifest is None:
        return None

    # Chunk requests only need to know whether an ID exists. The sets
    # are stored under a private key and never written back to disk.
    chunk_ids: dict[str, frozenset[str]] = {}
    chunk_index = manifest.get("chunks")

    if isinstance(chunk_index, dict):
        for layer_name, entries in chunk_index.items():
            if isinstance(entries, list):
                chunk_ids[str(layer_name)] = frozenset(
                    str(entry.get("id"))
                    for entry in entries
                    if isinstance(entry, dict)
                )

    manifest["_chunk_ids"] = chunk_ids

    return manifest


def _read_manifest(
    cache_directory: Path,
) -> dict[str, Any] | None:
    """Return a cache manifest, kept in memory until the file changes.

    The returned dictionary is shared between callers and must not be
    modified.
    """
    manifest_path = (
        cache_directory / CACHE_MANIFEST_NAME
    )

    try:
        stat = manifest_path.stat()
    except OSError:
        return None

    return _manifest_for_signature(
        str(manifest_path),
        stat.st_mtime_ns,
        stat.st_size,
    )


def _cache_is_current(
    cache_directory: Path,
    expected_kind: str,
    expected_fingerprint: dict[str, Any],
) -> bool:
    manifest = _read_manifest(cache_directory)

    if manifest is None:
        return False

    if (
        manifest.get("complete") is not True
        or manifest.get("kind") != expected_kind
        or manifest.get("cache_format_version")
        != CACHE_FORMAT_VERSION
        or manifest.get("fingerprint")
        != expected_fingerprint
        or not isinstance(manifest.get("version"), str)
        or not isinstance(manifest.get("chunks"), dict)
    ):
        return False

    return (
        cache_directory / CHUNK_DIRECTORY_NAME
    ).is_dir()


def _local_lock_for(
    cache_directory: Path,
) -> threading.Lock:
    lock_key = str(cache_directory.resolve())

    with _LOCAL_CACHE_LOCKS_GUARD:
        lock = _LOCAL_CACHE_LOCKS.get(lock_key)

        if lock is None:
            lock = threading.Lock()
            _LOCAL_CACHE_LOCKS[lock_key] = lock

        return lock


def _filesystem_lock_path(
    cache_directory: Path,
) -> Path:
    digest = hashlib.sha256(
        str(cache_directory.resolve()).encode("utf-8")
    ).hexdigest()[:32]

    return CACHE_LOCK_ROOT / f"{digest}.lock"


@contextmanager
def _cache_build_lock(
    cache_directory: Path,
):
    """Serialise builds of one cache across threads and processes.

    On POSIX systems an advisory ``flock`` is used. The kernel releases it
    when the owning process dies, so a crash never leaves a stale lock
    behind. Other platforms fall back to an exclusively created lock file.
    """
    local_lock = _local_lock_for(cache_directory)

    with local_lock:
        lock_path = _filesystem_lock_path(
            cache_directory
        )

        if fcntl is not None:
            file_lock = _flock_file_lock(
                lock_path,
                cache_directory,
            )
        else:
            file_lock = _exclusive_file_lock(
                lock_path,
                cache_directory,
            )

        with file_lock:
            yield


def _log_lock_wait(
    lock_path: Path,
    cache_directory: Path,
) -> None:
    LOGGER.warning(
        "Waiting for cache lock '%s' for '%s'. "
        "Another process or request may be "
        "building this cache.",
        lock_path.name,
        cache_directory.name,
    )


@contextmanager
def _flock_file_lock(
    lock_path: Path,
    cache_directory: Path,
):
    file_descriptor = os.open(
        lock_path,
        os.O_CREAT | os.O_RDWR,
        0o644,
    )

    try:
        deadline = (
            time.monotonic()
            + CACHE_LOCK_TIMEOUT_SECONDS
        )
        last_wait_log = 0.0

        while True:
            try:
                fcntl.flock(
                    file_descriptor,
                    fcntl.LOCK_EX | fcntl.LOCK_NB,
                )
                break
            except BlockingIOError:
                pass

            now = time.monotonic()

            if now - last_wait_log >= 10:
                _log_lock_wait(
                    lock_path,
                    cache_directory,
                )
                last_wait_log = now

            if now >= deadline:
                raise RuntimeError(
                    "Timed out while waiting for cache "
                    f"lock '{lock_path.name}'."
                )

            _raise_if_shutting_down()
            time.sleep(0.2)

        try:
            yield
        finally:
            fcntl.flock(
                file_descriptor,
                fcntl.LOCK_UN,
            )
    finally:
        os.close(file_descriptor)


@contextmanager
def _exclusive_file_lock(
    lock_path: Path,
    cache_directory: Path,
):
    deadline = (
        time.monotonic()
        + CACHE_LOCK_TIMEOUT_SECONDS
    )
    acquired = False
    last_wait_log = 0.0

    while not acquired:
        try:
            file_descriptor = os.open(
                lock_path,
                os.O_CREAT
                | os.O_EXCL
                | os.O_WRONLY,
                0o644,
            )

            lock_payload = json.dumps(
                {
                    "pid": os.getpid(),
                    "created_at": time.time(),
                    "cache_directory": str(
                        cache_directory
                    ),
                }
            ).encode("utf-8")

            os.write(
                file_descriptor,
                lock_payload,
            )
            os.close(file_descriptor)

            acquired = True
        except FileExistsError:
            now = time.monotonic()

            if now - last_wait_log >= 10:
                _log_lock_wait(
                    lock_path,
                    cache_directory,
                )
                last_wait_log = now

            try:
                age = (
                    time.time()
                    - lock_path.stat().st_mtime
                )
            except FileNotFoundError:
                continue

            if age > CACHE_LOCK_STALE_SECONDS:
                with suppress(FileNotFoundError):
                    lock_path.unlink()
                continue

            if time.monotonic() >= deadline:
                raise RuntimeError(
                    "Timed out while waiting for cache "
                    f"lock '{lock_path.name}'."
                )

            _raise_if_shutting_down()
            time.sleep(0.2)

    try:
        yield
    finally:
        with suppress(FileNotFoundError):
            lock_path.unlink()


def _remove_path(
    path: Path,
) -> None:
    if not path.exists() and not path.is_symlink():
        return

    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink()


def _replace_cache_directory(
    temporary_directory: Path,
    target_directory: Path,
) -> None:
    target_directory.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    backup_directory: Path | None = None

    if target_directory.exists():
        backup_directory = _build_directory_path(
            target_directory,
            "old",
        )

        os.replace(
            target_directory,
            backup_directory,
        )

    try:
        os.replace(
            temporary_directory,
            target_directory,
        )
    except Exception:
        if (
            backup_directory is not None
            and backup_directory.exists()
            and not target_directory.exists()
        ):
            os.replace(
                backup_directory,
                target_directory,
            )

        raise
    finally:
        if (
            backup_directory is not None
            and backup_directory.exists()
        ):
            _remove_path(backup_directory)


def _build_directory_path(
    target_directory: Path,
    kind: str,
) -> Path:
    return (
        target_directory.parent
        / f".cache-{kind}-{target_directory.name}--"
        f"{uuid.uuid4().hex}"
    )


def _remove_orphaned_build_directories(
    target_directory: Path,
) -> None:
    """Remove leftovers of interrupted builds of one cache.

    Must be called while holding the cache's build lock: no other build
    of the same cache can then be running, so every temporary or backup
    directory of this cache was left behind by a terminated process.
    """
    pattern = re.compile(
        r"^\.cache-(?:tmp|old)-"
        + re.escape(target_directory.name)
        + r"--[0-9a-f]{32}$"
    )

    for entry in target_directory.parent.iterdir():
        if pattern.fullmatch(entry.name):
            LOGGER.info(
                "Removing '%s' left behind by an interrupted "
                "cache build.",
                entry.name,
            )
            _remove_path(entry)


def _new_temporary_cache_directory(
    target_directory: Path,
) -> Path:
    target_directory.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    _remove_orphaned_build_directories(
        target_directory
    )

    temporary_directory = _build_directory_path(
        target_directory,
        "tmp",
    )

    temporary_directory.mkdir(
        parents=False,
        exist_ok=False,
    )

    return temporary_directory


def _to_wgs84(
    frame: gpd.GeoDataFrame,
) -> gpd.GeoDataFrame:
    if frame.crs is None:
        raise HTTPException(
            status_code=500,
            detail=(
                "Cannot cache a layer without a coordinate "
                "reference system."
            ),
        )

    try:
        source_crs = CRS.from_user_input(frame.crs)

        if source_crs.equals(WGS84):
            transformed = frame.copy()
            transformed.set_crs(
                WGS84,
                allow_override=True,
                inplace=True,
            )
            return transformed

        return frame.to_crs(WGS84)
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                "Could not transform a layer to EPSG:4326: "
                f"{exc}"
            ),
        ) from exc


def _coverage_label_frame(
    frame: gpd.GeoDataFrame,
    status: str,
    display_name: str,
) -> gpd.GeoDataFrame:
    if "coverage" not in frame.columns:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Layer '{display_name}' has no "
                "'coverage' attribute."
            ),
        )

    valid_geometry = (
        frame.geometry.notna()
        & ~frame.geometry.is_empty
    )
    valid_frame = frame.loc[valid_geometry]

    try:
        representative_points = (
            valid_frame.geometry
            .representative_point()
            .reset_index(drop=True)
        )
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Could not create labels for "
                f"'{display_name}': {exc}"
            ),
        ) from exc

    label_frame = gpd.GeoDataFrame(
        {
            "coverage": (
                valid_frame["coverage"]
                .reset_index(drop=True)
            ),
            "status": [status] * len(valid_frame),
        },
        geometry=representative_points,
        crs=frame.crs,
    )

    return _to_wgs84(label_frame)


def _combine_label_frames(
    label_frames: list[gpd.GeoDataFrame],
) -> gpd.GeoDataFrame:
    if not label_frames:
        return gpd.GeoDataFrame(
            {
                "coverage": pd.Series(dtype="float64"),
                "status": pd.Series(dtype="object"),
            },
            geometry=gpd.GeoSeries([], crs=WGS84),
            crs=WGS84,
        )

    combined = pd.concat(
        label_frames,
        ignore_index=True,
    )

    return gpd.GeoDataFrame(
        combined,
        geometry="geometry",
        crs=WGS84,
    )


def _json_property_value(
    value: Any,
) -> Any:
    """Convert one attribute value to a JSON-compatible Python value."""
    if value is None:
        return None

    if isinstance(value, (bool, np.bool_)):
        return bool(value)

    if isinstance(value, (int, np.integer)):
        return int(value)

    if isinstance(value, (float, np.floating)):
        number = float(value)
        return number if math.isfinite(number) else None

    if isinstance(value, str):
        return value

    with suppress(TypeError, ValueError):
        if pd.isna(value):
            return None

    return str(value)


def _flatten_geometry(
    geometry: Any,
) -> list[Any]:
    """Return the non-empty simple parts of a geometry, in order."""
    result: list[Any] = []
    pending = [geometry]

    while pending:
        current = pending.pop()

        if current is None or current.is_empty:
            continue

        if current.geom_type in {
            "MultiPolygon",
            "MultiLineString",
            "MultiPoint",
            "GeometryCollection",
        }:
            pending.extend(
                reversed(
                    shapely.get_parts(current).tolist()
                )
            )
            continue

        result.append(current)

    return result


def _polygonal_geometry(
    geometry: Any,
) -> Any:
    parts = [
        part
        for part in _flatten_geometry(geometry)
        if part.geom_type == "Polygon"
    ]

    if not parts:
        return None

    if len(parts) == 1:
        return parts[0]

    return shapely.MultiPolygon(parts)


def _linear_geometry(
    geometry: Any,
) -> Any:
    parts: list[Any] = []

    for part in _flatten_geometry(geometry):
        if part.geom_type == "LinearRing":
            part = shapely.LineString(part.coords)

        if part.geom_type == "LineString":
            parts.append(part)

    if not parts:
        return None

    if len(parts) == 1:
        return parts[0]

    return shapely.MultiLineString(parts)


def _coordinate_cut(
    values: np.ndarray,
    low: float,
    high: float,
) -> float | None:
    """Return a cut near the median of values strictly inside (low, high).

    The cut lies between two neighbouring distinct values, so no vertex
    ever lies exactly on it.
    """
    inside = np.unique(
        values[(values > low) & (values < high)]
    )

    if len(inside) < 2:
        return None

    middle = len(inside) // 2
    lower = float(inside[middle - 1])
    upper = float(inside[middle])
    cut = (lower + upper) / 2

    if not lower < cut < upper:
        return None

    return cut


def _cut_rectangles(
    geometry: Any,
    max_coordinates: int,
) -> list[tuple[float, float, float, float]]:
    """Cut a geometry's extent into rectangles with few vertices each.

    The extent is bisected recursively along its longer side. Cuts never
    pass through a vertex, so no segment runs along a cut. That matters
    because clip_by_rect drops line segments lying exactly on the border
    of the clipping rectangle.
    """
    min_x, min_y, max_x, max_y = geometry.bounds
    margin = CLIP_OUTER_MARGIN_DEGREES

    root = (
        min_x - margin,
        min_y - margin,
        max_x + margin,
        max_y + margin,
    )

    rectangles: list[tuple[float, float, float, float]] = []
    pending = [(root, geometry)]

    while pending:
        rectangle, part = pending.pop()
        coordinates = shapely.get_coordinates(part)

        if len(coordinates) == 0:
            continue

        if len(coordinates) <= max_coordinates:
            rectangles.append(rectangle)
            continue

        left, bottom, right, top = rectangle
        latitude = math.radians((bottom + top) / 2)
        width = (right - left) * max(math.cos(latitude), 1e-6)
        height = top - bottom
        axes = (0, 1) if width >= height else (1, 0)

        cut: float | None = None
        cut_axis = 0

        for cut_axis in axes:
            cut = _coordinate_cut(
                coordinates[:, cut_axis],
                rectangle[cut_axis],
                rectangle[cut_axis + 2],
            )

            if cut is not None:
                break

        if cut is None:
            rectangles.append(rectangle)
            continue

        if cut_axis == 0:
            halves = (
                (left, bottom, cut, top),
                (cut, bottom, right, top),
            )
        else:
            halves = (
                (left, bottom, right, cut),
                (left, cut, right, top),
            )

        for half in reversed(halves):
            pending.append(
                (
                    half,
                    shapely.clip_by_rect(part, *half),
                )
            )

    return rectangles


def _split_layer_geometries(
    geometries: np.ndarray,
    properties: dict[str, list[Any]],
    split_mode: str,
    max_bytes: int,
) -> tuple[list[Any], dict[str, list[Any]], int]:
    """Apply a layer's split mode; see LAYER_SPLIT_MODES.

    Geometries are cut before coordinates are rounded, so neighbouring
    pieces round their shared cut points identically.
    """
    # A cut polygon produces a fill piece and an outline piece of roughly
    # the same size, so each piece may use about half of a chunk.
    max_coordinates = max(
        64,
        max_bytes // (2 * ESTIMATED_BYTES_PER_COORDINATE),
    )
    coordinate_counts = shapely.get_num_coordinates(geometries)
    overlap = CLIP_FILL_OVERLAP_DEGREES

    result_geometries: list[Any] = []
    result_properties: dict[str, list[Any]] = {
        name: [] for name in properties
    }
    split_count = 0

    def emit(geometry: Any, index: int) -> None:
        if geometry is None or geometry.is_empty:
            return

        result_geometries.append(geometry)

        for name, values in properties.items():
            result_properties[name].append(values[index])

    for index, geometry in enumerate(geometries):
        geometry_type = geometry.geom_type
        oversized = (
            coordinate_counts[index] > max_coordinates
        )

        if (
            split_mode == "polygon-outline"
            and geometry_type in {"Polygon", "MultiPolygon"}
        ):
            boundary = _linear_geometry(geometry.boundary)

            if not oversized:
                emit(geometry, index)
                emit(boundary, index)
                continue

            split_count += 1

            for left, bottom, right, top in _cut_rectangles(
                geometry,
                max_coordinates,
            ):
                emit(
                    _polygonal_geometry(
                        shapely.clip_by_rect(
                            geometry,
                            left - overlap,
                            bottom - overlap,
                            right + overlap,
                            top + overlap,
                        )
                    ),
                    index,
                )

                if boundary is not None:
                    emit(
                        _linear_geometry(
                            shapely.clip_by_rect(
                                boundary,
                                left,
                                bottom,
                                right,
                                top,
                            )
                        ),
                        index,
                    )

            continue

        if oversized and geometry_type in {
            "LineString",
            "MultiLineString",
            "LinearRing",
        }:
            split_count += 1

            for rectangle in _cut_rectangles(
                geometry,
                max_coordinates,
            ):
                emit(
                    _linear_geometry(
                        shapely.clip_by_rect(
                            geometry,
                            *rectangle,
                        )
                    ),
                    index,
                )

            continue

        emit(geometry, index)

    return (
        result_geometries,
        result_properties,
        split_count,
    )


def _serialise_features(
    geometries: np.ndarray,
    properties: dict[str, list[Any]],
) -> tuple[list[bytes], np.ndarray]:
    """Return GeoJSON Feature bytes and bounds of the rounded geometries."""
    rounded = shapely.transform(
        geometries,
        lambda coordinates: np.round(
            coordinates,
            COORDINATE_DECIMALS,
        ),
    )
    geometry_texts = shapely.to_geojson(rounded)
    bounds = shapely.bounds(rounded)

    property_names = list(properties)
    features: list[bytes] = []

    if not property_names:
        for geometry_text in geometry_texts:
            features.append(
                (
                    '{"type":"Feature","properties":{},'
                    '"geometry":' + geometry_text + "}"
                ).encode("utf-8")
            )

        return features, bounds

    property_values = [
        properties[name] for name in property_names
    ]

    for index, geometry_text in enumerate(geometry_texts):
        property_text = json.dumps(
            {
                name: values[index]
                for name, values in zip(
                    property_names,
                    property_values,
                )
            },
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
        )

        features.append(
            (
                '{"type":"Feature","properties":'
                + property_text
                + ',"geometry":'
                + geometry_text
                + "}"
            ).encode("utf-8")
        )

    return features, bounds


def _kd_partition(
    sizes: np.ndarray,
    anchors: np.ndarray,
    max_bytes: int,
) -> list[tuple[str, np.ndarray]]:
    """Group features into spatially compact chunks of limited size.

    The feature set is split recursively along the longer side of its
    anchor extent, at the position where half of the bytes lie on each
    side. Splitting by rank guarantees progress even for identical
    anchors. A chunk ID is the path in the split tree ("0", "01", ...),
    which is deterministic for identical input.
    """
    result: list[tuple[str, np.ndarray]] = []
    pending = [("0", np.arange(len(sizes)))]

    while pending:
        chunk_id, indices = pending.pop()

        # Every feature is followed by a comma, except the last one.
        chunk_bytes = (
            int(sizes[indices].sum())
            + len(indices)
            - 1
            + CHUNK_ENVELOPE_BYTES
        )

        if chunk_bytes <= max_bytes or len(indices) <= 1:
            result.append((chunk_id, indices))
            continue

        points = anchors[indices]
        min_x, min_y = points.min(axis=0)
        max_x, max_y = points.max(axis=0)
        latitude = math.radians((min_y + max_y) / 2)
        width = (max_x - min_x) * max(math.cos(latitude), 1e-6)
        height = max_y - min_y
        axis = 0 if width >= height else 1

        ordered = indices[
            np.argsort(points[:, axis], kind="stable")
        ]
        cumulative = np.cumsum(sizes[ordered] + 1)
        split = int(
            np.searchsorted(
                cumulative,
                cumulative[-1] / 2,
                side="left",
            )
        ) + 1
        split = min(max(split, 1), len(ordered) - 1)

        pending.append((chunk_id + "1", ordered[split:]))
        pending.append((chunk_id + "0", ordered[:split]))

    result.sort(key=lambda item: item[0])

    return result


def _write_chunked_layer(
    cache_directory: Path,
    layer_name: str,
    frame: gpd.GeoDataFrame,
) -> list[dict[str, Any]]:
    """Split one EPSG:4326 layer into spatial chunks and write them.

    Every chunk is a gzip-compressed GeoJSON FeatureCollection below
    ``chunks/<layer>/``. The returned entries form the layer's part of the
    chunk index stored in the manifest.
    """
    started = time.perf_counter()
    max_bytes = _chunk_max_bytes(layer_name)
    property_columns = LAYER_PROPERTY_COLUMNS.get(
        layer_name,
        (),
    )

    for column in property_columns:
        if column not in frame.columns:
            raise HTTPException(
                status_code=500,
                detail=(
                    f"Layer '{layer_name}' is missing the "
                    f"required '{column}' attribute."
                ),
            )

    geometries = frame.geometry.to_numpy()
    keep = ~(
        shapely.is_missing(geometries)
        | shapely.is_empty(geometries)
    )
    geometries = geometries[keep]

    properties: dict[str, list[Any]] = {
        column: [
            _json_property_value(value)
            for value in frame[column].to_numpy()[keep]
        ]
        for column in property_columns
    }

    split_mode = LAYER_SPLIT_MODES.get(layer_name)
    split_count = 0

    if split_mode is not None and len(geometries) > 0:
        (
            split_geometries,
            properties,
            split_count,
        ) = _split_layer_geometries(
            geometries,
            properties,
            split_mode,
            max_bytes,
        )

        geometries = np.empty(
            len(split_geometries),
            dtype=object,
        )
        geometries[:] = split_geometries

    layer_directory = (
        cache_directory
        / CHUNK_DIRECTORY_NAME
        / layer_name
    )
    layer_directory.mkdir(
        parents=True,
        exist_ok=True,
    )

    if len(geometries) == 0:
        LOGGER.info(
            "Layer '%s' is empty; no chunks were written.",
            layer_name,
        )
        return []

    _raise_if_shutting_down()

    features, bounds = _serialise_features(
        geometries,
        properties,
    )

    finite = np.isfinite(bounds).all(axis=1)

    if not finite.all():
        features = [
            feature
            for feature, is_finite in zip(features, finite)
            if is_finite
        ]
        bounds = bounds[finite]

    if not features:
        return []

    sizes = np.fromiter(
        (len(feature) for feature in features),
        dtype=np.int64,
        count=len(features),
    )
    anchors = np.column_stack(
        (
            (bounds[:, 0] + bounds[:, 2]) / 2,
            (bounds[:, 1] + bounds[:, 3]) / 2,
        )
    )

    entries: list[dict[str, Any]] = []
    oversized_chunks = 0
    total_bytes = 0
    total_compressed_bytes = 0

    for chunk_id, indices in _kd_partition(
        sizes,
        anchors,
        max_bytes,
    ):
        _raise_if_shutting_down()

        payload = b"".join(
            (
                FEATURE_COLLECTION_PREFIX,
                b",".join(
                    features[int(index)]
                    for index in indices
                ),
                FEATURE_COLLECTION_SUFFIX,
            )
        )
        compressed = gzip.compress(
            payload,
            compresslevel=CHUNK_GZIP_LEVEL,
            mtime=0,
        )

        (
            layer_directory
            / f"{chunk_id}{CHUNK_FILE_SUFFIX}"
        ).write_bytes(compressed)

        if len(payload) > max_bytes:
            oversized_chunks += 1

        chunk_bounds = bounds[indices]
        total_bytes += len(payload)
        total_compressed_bytes += len(compressed)

        entries.append(
            {
                "id": chunk_id,
                "bbox": [
                    float(chunk_bounds[:, 0].min()),
                    float(chunk_bounds[:, 1].min()),
                    float(chunk_bounds[:, 2].max()),
                    float(chunk_bounds[:, 3].max()),
                ],
                "count": int(len(indices)),
                "bytes": len(payload),
                "gz_bytes": len(compressed),
                "sha": hashlib.sha256(payload).hexdigest()[:16],
            }
        )

    if oversized_chunks:
        LOGGER.warning(
            "%s chunk(s) of layer '%s' exceed the limit of %s "
            "bytes because a single feature is larger than "
            "the limit.",
            oversized_chunks,
            layer_name,
            f"{max_bytes:,}",
        )

    LOGGER.info(
        "Wrote %s chunks for '%s': %s features%s, "
        "%.1f MB (%.1f MB compressed) in %.1f seconds.",
        len(entries),
        layer_name,
        f"{len(features):,}",
        (
            f" ({split_count} oversized feature(s) cut)"
            if split_count
            else ""
        ),
        total_bytes / 1e6,
        total_compressed_bytes / 1e6,
        time.perf_counter() - started,
    )

    return entries


def _chunk_index_version(
    chunk_index: dict[str, list[dict[str, Any]]],
) -> str:
    """Return a content hash that identifies one set of chunk files."""
    digest = hashlib.sha256()

    for layer_name in sorted(chunk_index):
        for entry in chunk_index[layer_name]:
            digest.update(
                f"{layer_name}/{entry['id']}/{entry['sha']}\n"
                .encode("utf-8")
            )

    return digest.hexdigest()[:20]


def _write_manifest(
    cache_directory: Path,
    manifest: dict[str, Any],
) -> None:
    manifest_path = (
        cache_directory / CACHE_MANIFEST_NAME
    )

    with manifest_path.open(
        "w",
        encoding="utf-8",
    ) as manifest_file:
        json.dump(
            manifest,
            manifest_file,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        manifest_file.write("\n")


def _build_source_cache(
    temporary_directory: Path,
    source_config: dict[str, Any],
    fingerprint: dict[str, Any],
) -> None:
    source_path = _source_path(source_config)

    frame = _read_geo_file(
        source_path,
        source_config["file"],
        source_config.get("layer"),
    )

    polygon_count = _source_polygon_count(frame)

    chunk_index = {
        SOURCE_LAYER_NAME: _write_chunked_layer(
            temporary_directory,
            SOURCE_LAYER_NAME,
            _to_wgs84(frame),
        ),
    }

    manifest = {
        "cache_format_version": CACHE_FORMAT_VERSION,
        "kind": "source",
        "complete": True,
        "generated_at": (
            datetime.now(timezone.utc).isoformat()
        ),
        "fingerprint": fingerprint,
        "source": source_config["id"],
        "feature_count": polygon_count,
        "crs": "EPSG:4326",
        "version": _chunk_index_version(chunk_index),
        "chunks": chunk_index,
    }

    _write_manifest(
        temporary_directory,
        manifest,
    )


def _build_solution_cache(
    temporary_directory: Path,
    source_config: dict[str, Any],
    algorithm_config: dict[str, str],
    solution_directory: Path,
    fingerprint: dict[str, Any],
) -> None:
    original_paths = _solution_original_paths(
        solution_directory
    )

    solution_tokens = _solution_tokens_from_directory(
        source_config,
        solution_directory.name,
    )

    if solution_tokens is None:
        raise RuntimeError(
            "The solution directory does not follow the "
            "configured naming convention."
        )

    directory_k, directory_tau = solution_tokens

    build_started = time.perf_counter()

    LOGGER.info(
        "Starting solution cache: %s",
        solution_directory.name,
    )

    uncovered_for_edges = _read_geo_file(
        original_paths["uncovered"],
        original_paths["uncovered"].name,
    )

    feature_counts: dict[str, int] = {}
    label_frames: list[gpd.GeoDataFrame] = []
    chunk_index: dict[str, list[dict[str, Any]]] = {}

    build_order = (
        "antennas",
        "covered",
        "uncovered",
        "edges",
        "visibility",
    )

    for layer_name in build_order:
        _raise_if_shutting_down()

        original_path = original_paths[layer_name]
        layer_started = time.perf_counter()

        LOGGER.info(
            "[%s] Processing layer '%s'.",
            solution_directory.name,
            layer_name,
        )

        if layer_name == "uncovered":
            frame = uncovered_for_edges
        else:
            frame = _read_geo_file(
                original_path,
                original_path.name,
            )

        if layer_name == "edges":
            frame = _classify_edges_by_uncovered_buildings(
                frame,
                uncovered_for_edges,
            )

        feature_counts[layer_name] = len(frame)

        if layer_name in {"covered", "uncovered"}:
            label_frames.append(
                _coverage_label_frame(
                    frame,
                    layer_name,
                    original_path.name,
                )
            )

        chunk_index[layer_name] = _write_chunked_layer(
            temporary_directory,
            layer_name,
            _to_wgs84(frame),
        )

        LOGGER.info(
            "[%s] Finished layer '%s' in %.1f seconds.",
            solution_directory.name,
            layer_name,
            time.perf_counter() - layer_started,
        )

        del frame

    _raise_if_shutting_down()

    labels = _combine_label_frames(label_frames)
    del label_frames

    chunk_index["coverage_labels"] = _write_chunked_layer(
        temporary_directory,
        "coverage_labels",
        labels,
    )

    feature_counts["coverage_labels"] = len(labels)

    source_path = _source_path(source_config)
    polygon_count = _feature_count(
        source_path,
        source_config.get("layer"),
    )

    metadata = {
        "source": source_config["id"],
        "algorithm": algorithm_config["id"],
        "k": directory_k,
        "tau": directory_tau,
        "polygon_count": polygon_count,
        "covered_polygon_count": feature_counts[
            "covered"
        ],
        "uncovered_polygon_count": feature_counts[
            "uncovered"
        ],
        "edge_classification": "uncovered-boundary-v1",
        "solution_directory": _relative_path_label(
            solution_directory
        ),
    }

    manifest = {
        "cache_format_version": CACHE_FORMAT_VERSION,
        "kind": "solution",
        "complete": True,
        "generated_at": (
            datetime.now(timezone.utc).isoformat()
        ),
        "fingerprint": fingerprint,
        "metadata": metadata,
        "feature_counts": feature_counts,
        "crs": "EPSG:4326",
        "version": _chunk_index_version(chunk_index),
        "chunks": chunk_index,
    }

    _write_manifest(
        temporary_directory,
        manifest,
    )

    LOGGER.info(
        "Finished solution cache '%s' in %.1f seconds.",
        solution_directory.name,
        time.perf_counter() - build_started,
    )


def _classify_edges_by_uncovered_buildings(
    edges: gpd.GeoDataFrame,
    uncovered: gpd.GeoDataFrame,
) -> gpd.GeoDataFrame:
    if edges.crs is None or uncovered.crs is None:
        raise HTTPException(
            status_code=500,
            detail=(
                "Edges and uncovered buildings must have "
                "defined coordinate reference systems."
            ),
        )

    if edges.empty:
        return gpd.GeoDataFrame(
            {
                "status": pd.Series(dtype="object"),
            },
            geometry=gpd.GeoSeries([], crs=edges.crs),
            crs=edges.crs,
        )

    edge_crs = CRS.from_user_input(edges.crs)
    uncovered_crs = CRS.from_user_input(uncovered.crs)

    if not edge_crs.equals(uncovered_crs):
        LOGGER.info(
            "Reprojecting uncovered buildings for edge "
            "classification."
        )

        uncovered = uncovered.to_crs(edge_crs)

    valid_edges = edges.loc[
        edges.geometry.notna()
        & ~edges.geometry.is_empty
    ].copy()

    # A union_edges file may contain one very large MultiLineString.
    # Exploding it is essential: otherwise one spatial-index query can
    # still select every building boundary in the instance.
    valid_edges = valid_edges.explode(
        index_parts=False,
        ignore_index=True,
    )

    valid_edges = valid_edges.loc[
        valid_edges.geometry.geom_type.isin(
            {
                "LineString",
                "MultiLineString",
                "LinearRing",
            }
        )
    ].reset_index(drop=True)

    if valid_edges.empty:
        return gpd.GeoDataFrame(
            {
                "status": pd.Series(dtype="object"),
            },
            geometry=gpd.GeoSeries([], crs=edges.crs),
            crs=edges.crs,
        )

    valid_uncovered = uncovered.loc[
        uncovered.geometry.notna()
        & ~uncovered.geometry.is_empty
    ].copy()

    valid_uncovered = valid_uncovered.explode(
        index_parts=False,
        ignore_index=True,
    )

    valid_uncovered = valid_uncovered.loc[
        valid_uncovered.geometry.geom_type.isin(
            {"Polygon", "MultiPolygon"}
        )
    ].reset_index(drop=True)

    if valid_uncovered.empty:
        return gpd.GeoDataFrame(
            {
                "status": ["covered"] * len(valid_edges),
            },
            geometry=valid_edges.geometry.reset_index(
                drop=True
            ),
            crs=edges.crs,
        )

    boundary_geometries = [
        boundary
        for boundary in (
            valid_uncovered.geometry.boundary.tolist()
        )
        if (
            boundary is not None
            and not boundary.is_empty
        )
    ]

    if not boundary_geometries:
        return gpd.GeoDataFrame(
            {
                "status": ["covered"] * len(valid_edges),
            },
            geometry=valid_edges.geometry.reset_index(
                drop=True
            ),
            crs=edges.crs,
        )

    bounds = valid_edges.total_bounds

    extent = max(
        float(bounds[2] - bounds[0]),
        float(bounds[3] - bounds[1]),
        0.0,
    )

    tolerance = max(
        extent * 1e-9,
        1e-8 if edge_crs.is_geographic else 0.001,
    )

    boundary_tree = STRtree(boundary_geometries)

    result_geometries: list[Any] = []
    result_statuses: list[str] = []

    topology_failures = 0
    edge_count = len(valid_edges)
    progress_step = max(1000, edge_count // 20)

    LOGGER.info(
        "Classifying %s edge parts against %s uncovered "
        "building boundaries with tolerance %g.",
        f"{edge_count:,}",
        f"{len(boundary_geometries):,}",
        tolerance,
    )

    def append_linear_parts(
        geometry: Any,
        status: str,
    ) -> None:
        if geometry is None or geometry.is_empty:
            return

        geometry_type = geometry.geom_type

        if geometry_type in {
            "LineString",
            "LinearRing",
        }:
            result_geometries.append(geometry)
            result_statuses.append(status)
            return

        if geometry_type in {
            "MultiLineString",
            "GeometryCollection",
        }:
            for part in geometry.geoms:
                append_linear_parts(part, status)

    for position, edge_geometry in enumerate(
        valid_edges.geometry,
        start=1,
    ):
        if position % 1000 == 0:
            _raise_if_shutting_down()

        candidate_indices = boundary_tree.query(
            edge_geometry,
            predicate="dwithin",
            distance=tolerance,
        )

        if len(candidate_indices) == 0:
            append_linear_parts(
                edge_geometry,
                "covered",
            )
        else:
            candidate_boundaries = [
                boundary_geometries[int(index)]
                for index in candidate_indices
            ]

            try:
                local_boundary = union_all(
                    candidate_boundaries
                )

                local_boundary_zone = (
                    local_boundary.buffer(tolerance)
                )

                uncovered_part = edge_geometry.intersection(
                    local_boundary_zone
                )

                covered_part = edge_geometry.difference(
                    local_boundary_zone
                )

                append_linear_parts(
                    uncovered_part,
                    "uncovered",
                )

                append_linear_parts(
                    covered_part,
                    "covered",
                )
            except Exception:
                # A topology problem in one feature should not make
                # the whole solution impossible to cache. Since the
                # spatial index found an uncovered boundary nearby,
                # conservatively classify this edge part as uncovered.
                topology_failures += 1

                append_linear_parts(
                    edge_geometry,
                    "uncovered",
                )

        if (
            position % progress_step == 0
            or position == edge_count
        ):
            LOGGER.info(
                "Edge classification progress: %s/%s "
                "(%.1f%%).",
                f"{position:,}",
                f"{edge_count:,}",
                position * 100 / edge_count,
            )

    if topology_failures:
        LOGGER.warning(
            "Edge classification encountered %s local "
            "topology failures; those edge parts were "
            "classified as uncovered.",
            f"{topology_failures:,}",
        )

    result = gpd.GeoDataFrame(
        {
            "status": result_statuses,
        },
        geometry=result_geometries,
        crs=edges.crs,
    )

    LOGGER.info(
        "Edge classification produced %s output parts.",
        f"{len(result):,}",
    )

    return result


def _ensure_source_cache(
    source_config: dict[str, Any],
    force: bool = False,
) -> tuple[Path, dict[str, Any], bool]:
    cache_directory = _source_cache_directory(
        source_config
    )
    fingerprint = _source_cache_fingerprint(
        source_config
    )

    if (
        not force
        and _cache_is_current(
            cache_directory,
            "source",
            fingerprint,
        )
    ):
        manifest = _read_manifest(cache_directory)

        if manifest is not None:
            return cache_directory, manifest, False

    with _cache_build_lock(cache_directory):
        fingerprint = _source_cache_fingerprint(
            source_config
        )

        if (
            not force
            and _cache_is_current(
                cache_directory,
                "source",
                fingerprint,
            )
        ):
            manifest = _read_manifest(cache_directory)

            if manifest is not None:
                return cache_directory, manifest, False

        temporary_directory = (
            _new_temporary_cache_directory(
                cache_directory
            )
        )

        try:
            _build_source_cache(
                temporary_directory,
                source_config,
                fingerprint,
            )

            _replace_cache_directory(
                temporary_directory,
                cache_directory,
            )
        except HTTPException:
            _remove_path(temporary_directory)
            raise
        except Exception as exc:
            _remove_path(temporary_directory)

            raise HTTPException(
                status_code=500,
                detail=(
                    f"Could not create the cache for source "
                    f"'{source_config['id']}': {exc}"
                ),
            ) from exc

        manifest = _read_manifest(cache_directory)

        if manifest is None:
            raise HTTPException(
                status_code=500,
                detail=(
                    "The source cache was generated without "
                    "a valid manifest."
                ),
            )

        return cache_directory, manifest, True


def _ensure_solution_cache(
    source_config: dict[str, Any],
    algorithm_config: dict[str, str],
    solution_directory: Path,
    force: bool = False,
) -> tuple[Path, dict[str, Any], bool]:
    cache_directory = _solution_cache_directory(
        algorithm_config,
        solution_directory,
    )
    fingerprint = _solution_cache_fingerprint(
        source_config,
        algorithm_config,
        solution_directory,
    )

    if (
        not force
        and _cache_is_current(
            cache_directory,
            "solution",
            fingerprint,
        )
    ):
        manifest = _read_manifest(cache_directory)

        if manifest is not None:
            return cache_directory, manifest, False

    with _cache_build_lock(cache_directory):
        fingerprint = _solution_cache_fingerprint(
            source_config,
            algorithm_config,
            solution_directory,
        )

        if (
            not force
            and _cache_is_current(
                cache_directory,
                "solution",
                fingerprint,
            )
        ):
            manifest = _read_manifest(cache_directory)

            if manifest is not None:
                return cache_directory, manifest, False

        temporary_directory = (
            _new_temporary_cache_directory(
                cache_directory
            )
        )

        try:
            _build_solution_cache(
                temporary_directory,
                source_config,
                algorithm_config,
                solution_directory,
                fingerprint,
            )

            _replace_cache_directory(
                temporary_directory,
                cache_directory,
            )
        except HTTPException:
            _remove_path(temporary_directory)
            raise
        except Exception as exc:
            _remove_path(temporary_directory)

            raise HTTPException(
                status_code=500,
                detail=(
                    "Could not create the cache for solution "
                    f"'{solution_directory.name}': {exc}"
                ),
            ) from exc

        manifest = _read_manifest(cache_directory)

        if manifest is None:
            raise HTTPException(
                status_code=500,
                detail=(
                    "The solution cache was generated without "
                    "a valid manifest."
                ),
            )

        return cache_directory, manifest, True


def _accepted_encodings(
    request: Request,
) -> dict[str, float]:
    result: dict[str, float] = {}

    for item in request.headers.get(
        "accept-encoding",
        "",
    ).split(","):
        item = item.strip()

        if not item:
            continue

        parts = [
            part.strip()
            for part in item.split(";")
        ]
        encoding = parts[0].lower()
        quality = 1.0

        for parameter in parts[1:]:
            if not parameter.lower().startswith("q="):
                continue

            try:
                quality = float(
                    parameter.split("=", 1)[1]
                )
            except ValueError:
                quality = 0.0

        result[encoding] = quality

    return result


def _request_accepts_gzip(
    request: Request,
) -> bool:
    encodings = _accepted_encodings(request)

    if "gzip" in encodings:
        return encodings["gzip"] > 0

    return encodings.get("*", 0) > 0


def _parse_center(
    raw_center: str | None,
) -> tuple[float, float] | None:
    if raw_center is None:
        return None

    parts = raw_center.split(",")

    if len(parts) != 2:
        raise HTTPException(
            status_code=400,
            detail="center must be lon,lat.",
        )

    try:
        longitude, latitude = (
            float(part)
            for part in parts
        )
    except ValueError as exc:
        raise HTTPException(
            status_code=400,
            detail=(
                "center must contain two numeric values."
            ),
        ) from exc

    if (
        not math.isfinite(longitude)
        or not math.isfinite(latitude)
        or not -180 <= longitude <= 180
        or not -90 <= latitude <= 90
    ):
        raise HTTPException(
            status_code=400,
            detail="Invalid EPSG:4326 centre.",
        )

    return longitude, latitude


def _parse_layer_list(
    raw_layers: str | None,
) -> tuple[str, ...]:
    if raw_layers is None or not raw_layers.strip():
        return SOLUTION_LAYER_NAMES

    result: list[str] = []

    for raw_layer in raw_layers.split(","):
        layer_name = raw_layer.strip()

        if not layer_name:
            continue

        if layer_name not in ALLOWED_SOLUTION_LAYERS:
            raise HTTPException(
                status_code=400,
                detail=(
                    f"Unknown layer '{layer_name}'. "
                    "Allowed values are: "
                    + ", ".join(SOLUTION_LAYER_NAMES)
                    + "."
                ),
            )

        if layer_name not in result:
            result.append(layer_name)

    return tuple(result)


def _distance_to_bbox(
    bbox: list[float],
    longitude: float,
    latitude: float,
    longitude_scale: float,
) -> float:
    """Approximate distance in degrees; zero inside the bbox."""
    delta_x = max(
        bbox[0] - longitude,
        0.0,
        longitude - bbox[2],
    ) * longitude_scale
    delta_y = max(
        bbox[1] - latitude,
        0.0,
        latitude - bbox[3],
    )

    return math.hypot(delta_x, delta_y)


def _chunk_index_response(
    manifest: dict[str, Any],
    layer_names: tuple[str, ...],
    bbox: tuple[float, float, float, float] | None,
    center: tuple[float, float] | None,
    chunk_url_prefix: str,
    cache_status: str,
    extra: dict[str, Any] | None = None,
) -> JSONResponse:
    """List the chunks intersecting bbox, nearest to center first.

    A chunk's URL is ``<chunk_url_prefix>/<layer>/<id>``.
    """
    chunk_index = manifest.get("chunks") or {}
    selected: list[tuple[str, dict[str, Any]]] = []

    for layer_name in layer_names:
        for entry in chunk_index.get(layer_name, []):
            chunk_bbox = entry["bbox"]

            if bbox is not None and (
                chunk_bbox[2] < bbox[0]
                or chunk_bbox[0] > bbox[2]
                or chunk_bbox[3] < bbox[1]
                or chunk_bbox[1] > bbox[3]
            ):
                continue

            selected.append((layer_name, entry))

    if center is None and bbox is not None:
        center = (
            (bbox[0] + bbox[2]) / 2,
            (bbox[1] + bbox[3]) / 2,
        )

    if center is not None:
        longitude, latitude = center
        longitude_scale = math.cos(
            math.radians(latitude)
        )

        def sort_key(
            item: tuple[str, dict[str, Any]],
        ) -> tuple[float, int, float, str, str]:
            layer_name, entry = item
            chunk_bbox = entry["bbox"]

            centre_distance = math.hypot(
                (
                    (chunk_bbox[0] + chunk_bbox[2]) / 2
                    - longitude
                ) * longitude_scale,
                (chunk_bbox[1] + chunk_bbox[3]) / 2
                - latitude,
            )

            return (
                _distance_to_bbox(
                    chunk_bbox,
                    longitude,
                    latitude,
                    longitude_scale,
                ),
                LAYER_PRIORITY.get(layer_name, 99),
                centre_distance,
                layer_name,
                entry["id"],
            )

        selected.sort(key=sort_key)
    else:
        selected.sort(
            key=lambda item: (
                LAYER_PRIORITY.get(item[0], 99),
                item[0],
                item[1]["id"],
            )
        )

    return JSONResponse(
        {
            "version": manifest.get("version"),
            "chunk_url_prefix": chunk_url_prefix,
            "chunks": [
                {
                    "layer": layer_name,
                    "id": entry["id"],
                    "bbox": entry["bbox"],
                    "count": entry["count"],
                    "bytes": entry["bytes"],
                }
                for layer_name, entry in selected
            ],
            **(extra or {}),
        },
        headers={
            "Cache-Control": "no-cache",
            "X-Data-Cache": cache_status,
        },
    )


def _chunk_url_prefix(
    *segments: str,
) -> str:
    return "/api/chunk/" + "/".join(
        quote(segment, safe="")
        for segment in segments
    )


def _chunk_file_response(
    request: Request,
    cache_directory: Path,
    version: str,
    layer_name: str,
    chunk_id: str,
) -> Response:
    """Serve one precompressed chunk without touching any cache build.

    Chunk URLs contain the content version, so a response never changes
    and may be cached forever. A request for an outdated version is
    answered with 404; the client then requests a fresh chunk index.
    """
    manifest = _read_manifest(cache_directory)

    known_ids = (
        manifest.get("_chunk_ids", {}).get(layer_name)
        if manifest is not None
        and manifest.get("version") == version
        else None
    )

    if (
        known_ids is None
        or chunk_id not in known_ids
        or not CHUNK_ID_PATTERN.fullmatch(chunk_id)
    ):
        raise HTTPException(
            status_code=404,
            detail="Unknown or outdated chunk.",
        )

    path = (
        cache_directory
        / CHUNK_DIRECTORY_NAME
        / layer_name
        / f"{chunk_id}{CHUNK_FILE_SUFFIX}"
    )

    if not path.is_file():
        raise HTTPException(
            status_code=404,
            detail="Unknown or outdated chunk.",
        )

    headers = {
        "Cache-Control": (
            "public, max-age=31536000, immutable"
        ),
        "Vary": "Accept-Encoding",
    }

    if _request_accepts_gzip(request):
        headers["Content-Encoding"] = "gzip"

        return FileResponse(
            path,
            media_type="application/geo+json",
            headers=headers,
        )

    return Response(
        content=gzip.decompress(path.read_bytes()),
        media_type="application/geo+json",
        headers=headers,
    )


def _exception_text(
    error: Exception,
) -> str:
    if isinstance(error, HTTPException):
        return str(error.detail)

    return str(error)


def precompute_all(
    force: bool = False,
) -> dict[str, Any]:
    """Build every cache the viewer can request.

    For each source, algorithm, k, and tau with solutions, only the run
    that the API selects (the best one) is built; the other runs of the
    same combination can never be displayed. Without discovered solutions,
    combinations with unconfigured k or tau values are skipped.
    """
    report: dict[str, Any] = {
        "force": force,
        "stopped": False,
        "source_caches": {
            "built": 0,
            "reused": 0,
        },
        "solution_caches": {
            "built": 0,
            "reused": 0,
            "skipped": 0,
        },
        "failures": [],
    }

    def stop_requested() -> bool:
        if _SHUTDOWN_EVENT.is_set():
            report["stopped"] = True
            return True

        return False

    for source in CONFIG["sources"].values():
        if stop_requested():
            return report

        try:
            _, _, was_built = _ensure_source_cache(
                source,
                force=force,
            )

            result_key = (
                "built"
                if was_built
                else "reused"
            )
            report["source_caches"][result_key] += 1

            LOGGER.info(
                "%s source cache for '%s'.",
                (
                    "Built"
                    if was_built
                    else "Reused"
                ),
                source["id"],
            )
        except Exception as exc:
            if stop_requested():
                return report

            LOGGER.exception(
                "Could not precompute source '%s'.",
                source["id"],
            )

            report["failures"].append(
                {
                    "kind": "source",
                    "source": source["id"],
                    "error": _exception_text(exc),
                }
            )

    for (
        source_id,
        algorithm_id,
        k_value,
        tau_value,
    ), runs in sorted(_scan_solution_runs().items()):
        if stop_requested():
            return report

        source = CONFIG["sources"][source_id]

        if not _parameters_requestable(source, k_value, tau_value):
            report["solution_caches"]["skipped"] += 1
            continue

        algorithm = CONFIG["algorithms"][algorithm_id]
        solution_directory = _select_run(runs).path

        try:
            _, _, was_built = _ensure_solution_cache(
                source,
                algorithm,
                solution_directory,
                force=force,
            )

            result_key = "built" if was_built else "reused"
            report["solution_caches"][result_key] += 1

            LOGGER.info(
                "%s solution cache for '%s'.",
                "Built" if was_built else "Reused",
                solution_directory.name,
            )
        except Exception as exc:
            if stop_requested():
                return report

            LOGGER.exception(
                "Could not precompute solution '%s'.",
                solution_directory.name,
            )

            report["failures"].append(
                {
                    "kind": "solution",
                    "source": source_id,
                    "algorithm": algorithm_id,
                    "directory": solution_directory.name,
                    "error": _exception_text(exc),
                }
            )

    return report


def _parameters_requestable(
    source: dict[str, Any],
    k_value: str,
    tau_value: str,
) -> bool:
    """Whether the viewer can request a solution with these parameters.

    With discovered solutions, every k and tau can be requested. Otherwise
    only the values configured for the source can.
    """
    if CONFIG["discovery"]["solutions"]:
        return True

    configured_taus = set()

    for configured_tau in source["tau_values"]:
        with suppress(HTTPException):
            configured_taus.add(_normalised_tau(configured_tau))

    return (
        k_value in source["k_values"]
        and tau_value in configured_taus
    )


# Examples for the interactive API documentation (/docs and /redoc). They
# use the competition instance and the solutions published on Zenodo (see
# scripts/download_zenodo_data.py). Version hashes depend on the cache
# contents, so the example chunk URLs only work where they were taken from.

EXAMPLE_SOURCE_ID = "GIS-cup-competition-dataset"
EXAMPLE_ALGORITHM_ID = "GISCupBonn_eSA"
EXAMPLE_SOLUTION_DIRECTORY = (
    "GIS-cup-competition-dataset_par_9_0.320000_id_p0_t08_15_18_32_09_020"
    "_r_60_val_1403.solution.visualization"
)
EXAMPLE_CATALOG_VERSION = "0be9d2918afaaa23"
EXAMPLE_SOURCE_VERSION = "a91d6294122168ea3d9c"
EXAMPLE_SOLUTION_VERSION = "ce5fd68bb40537258633"


def _parameter_example(
    value: str,
) -> dict[str, dict[str, str]]:
    """openapi_examples entry; Swagger UI fills in the value."""
    return {
        "competition": {
            "summary": "Competition instance",
            "value": value,
        },
    }


def _documented_responses(
    example: Any,
    *errors: tuple[int, str],
    media_type: str = "application/json",
) -> dict[int | str, dict[str, Any]]:
    """OpenAPI responses with an example for success and each error."""
    responses: dict[int | str, dict[str, Any]] = {
        200: {
            "content": {
                media_type: {
                    "example": example,
                },
            },
        },
    }

    for status_code, detail in errors:
        responses[status_code] = {
            "description": HTTPStatus(status_code).phrase,
            "content": {
                "application/json": {
                    "example": {"detail": detail},
                },
            },
        }

    return responses


SourceIdPath = Annotated[
    str,
    ApiPath(
        description="Source ID, as listed by /api/config.",
        openapi_examples=_parameter_example(EXAMPLE_SOURCE_ID),
    ),
]

BboxQuery = Annotated[
    str | None,
    Query(
        description=(
            "Only list chunks intersecting this box, as "
            "minLon,minLat,maxLon,maxLat in EPSG:4326. Default: all "
            "chunks."
        ),
        openapi_examples=_parameter_example(
            "-117.38,33.97,-117.37,33.98"
        ),
    ),
]

CenterQuery = Annotated[
    str | None,
    Query(
        description=(
            "Sort chunks by their distance from this point, as lon,lat "
            "in EPSG:4326, nearest first. Default: the centre of bbox."
        ),
        openapi_examples=_parameter_example("-117.375,33.975"),
    ),
]

UNKNOWN_SOURCE_ERROR = (404, "Unknown source 'example'.")
UNKNOWN_CHUNK_ERROR = (404, "Unknown or outdated chunk.")
INVALID_BBOX_ERROR = (400, "bbox must be minLon,minLat,maxLon,maxLat.")

EXAMPLE_PRECOMPUTE_REPORT = {
    "force": False,
    "stopped": False,
    "source_caches": {"built": 0, "reused": 1},
    "solution_caches": {"built": 1, "reused": 8, "skipped": 0},
    "failures": [],
}

EXAMPLE_CONFIG = {
    "title": "GIS Cup 2026 Solution Viewer",
    "sources": [
        {
            "id": EXAMPLE_SOURCE_ID,
            "label": "GIS Cup Competition 2026",
            "k_values": ["9", "49", "484"],
            "tau_values": ["0.32", "0.49", "0.68"],
        },
    ],
    "algorithms": [
        {
            "id": EXAMPLE_ALGORITHM_ID,
            "label": "Extended Simulated Annealing (Bonn)",
            "short_label": "eSA (Bonn)",
        },
    ],
    "discovery": {"sources": True, "solutions": True},
    "map": {
        "center": [50.7374, 7.0982],
        "zoom": 14,
        "max_zoom": 20,
        "tile_max_native_zoom": 19,
        "tile_url": "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
        "attribution": (
            '&copy; <a href="https://www.openstreetmap.org/copyright">'
            "OpenStreetMap</a> contributors"
        ),
        "coverage_label_min_zoom": 19,
        "background_opacity": 0.4,
        "overlay_initial_blend": 50,
        "initial_source_zoom_offset": 1,
        "coverage_label_multiplier": 100.0,
        "coverage_label_decimals": 1,
        "coverage_label_suffix": "%",
    },
}

EXAMPLE_EXTENT = {
    "source": EXAMPLE_SOURCE_ID,
    "source_label": "GIS Cup Competition 2026",
    "bbox": [-117.4331640, 33.9325315, -117.3179099, 34.0300286],
    "feature_count": 50000,
}

EXAMPLE_STATISTICS = {
    "source": EXAMPLE_SOURCE_ID,
    "source_label": "GIS Cup Competition 2026",
    "total_polygons": 50000,
    "metric": "covered_polygons",
    "algorithms": [
        {
            "id": EXAMPLE_ALGORITHM_ID,
            "label": "Extended Simulated Annealing (Bonn)",
            "short_label": "eSA (Bonn)",
        },
    ],
    "rows": [
        {
            "k": "9",
            "tau": "0.32",
            "cells": {
                EXAMPLE_ALGORITHM_ID: {
                    "status": "available",
                    "count": 1403,
                    "runs": 1,
                    "directory": EXAMPLE_SOLUTION_DIRECTORY,
                    "missing": [],
                },
            },
        },
    ],
    "catalog_version": EXAMPLE_CATALOG_VERSION,
}

EXAMPLE_SOURCE_CHUNK_INDEX = {
    "version": EXAMPLE_SOURCE_VERSION,
    "chunk_url_prefix": (
        f"/api/chunk/source/{EXAMPLE_SOURCE_ID}/{EXAMPLE_SOURCE_VERSION}"
    ),
    "chunks": [
        {
            "layer": "source",
            "id": "010010",
            "bbox": [-117.392287, 33.9682141, -117.3689974, 33.9837582],
            "count": 1571,
            "bytes": 417482,
        },
    ],
    "catalog_version": EXAMPLE_CATALOG_VERSION,
}

EXAMPLE_SOLUTION_CHUNK_INDEX = {
    "version": EXAMPLE_SOLUTION_VERSION,
    "chunk_url_prefix": (
        f"/api/chunk/solution/{EXAMPLE_SOURCE_ID}/{EXAMPLE_ALGORITHM_ID}/"
        f"{EXAMPLE_SOLUTION_DIRECTORY}/{EXAMPLE_SOLUTION_VERSION}"
    ),
    "chunks": [
        {
            "layer": "antennas",
            "id": "0",
            "bbox": [-117.4180853, 33.9692873, -117.3365184, 34.0251388],
            "count": 9,
            "bytes": 964,
        },
        {
            "layer": "covered",
            "id": "0",
            "bbox": [-117.4323874, 33.9582456, -117.3272129, 34.0288908],
            "count": 1403,
            "bytes": 360598,
        },
    ],
    "runs": {
        "count": 1,
        "selected": EXAMPLE_SOLUTION_DIRECTORY,
        "selected_value": 1403,
        "values": [1403],
    },
    "catalog_version": EXAMPLE_CATALOG_VERSION,
}

EXAMPLE_SOURCE_CHUNK = {
    "type": "FeatureCollection",
    "features": [
        {
            "type": "Feature",
            "properties": {},
            "geometry": {
                "type": "Polygon",
                "coordinates": [[
                    [-117.384086, 33.968358],
                    [-117.3840548, 33.9684034],
                    [-117.3839332, 33.968346],
                    [-117.3839643, 33.9683006],
                    [-117.384086, 33.968358],
                ]],
            },
        },
    ],
}

EXAMPLE_SOLUTION_CHUNK = {
    "type": "FeatureCollection",
    "features": [
        {
            "type": "Feature",
            "properties": {},
            "geometry": {
                "type": "Point",
                "coordinates": [-117.3863021, 34.0080715],
            },
        },
    ],
}


@app.get(
    "/",
    include_in_schema=False,
)
def index() -> FileResponse:
    return FileResponse(
        TEMPLATE_ROOT / "index.html",
        media_type="text/html",
    )


@app.get(
    "/api/health",
    responses=_documented_responses(
        {
            "status": "ok",
            "startup_precompute_enabled": True,
            "startup_precompute_running": False,
            "startup_precompute_report": EXAMPLE_PRECOMPUTE_REPORT,
        }
    ),
)
def get_health(
    request: Request,
) -> dict[str, Any]:
    """Whether the service runs, and the state of the startup cache build.

    The report is null until the startup precomputation has finished.
    """
    task = getattr(
        request.app.state,
        "precompute_task",
        None,
    )
    report = getattr(
        request.app.state,
        "precompute_report",
        None,
    )

    return {
        "status": "ok",
        "startup_precompute_enabled": (
            STARTUP_PRECOMPUTE_ENABLED
        ),
        "startup_precompute_running": (
            task is not None
            and not task.done()
        ),
        "startup_precompute_report": report,
    }


@app.get(
    "/api/config",
    responses=_documented_responses(EXAMPLE_CONFIG),
)
def get_public_config() -> dict[str, Any]:
    """The public configuration: sources (each with its configured k and
    tau values), algorithms, discovery settings, and map settings."""
    return {
        "title": CONFIG["title"],
        "sources": [
            {
                "id": source["id"],
                "label": source["label"],
                "k_values": source["k_values"],
                "tau_values": source["tau_values"],
            }
            for source in CONFIG["sources"].values()
        ],
        "algorithms": [
            {
                "id": algorithm["id"],
                "label": algorithm["label"],
                "short_label": algorithm["short_label"],
            }
            for algorithm in CONFIG[
                "algorithms"
            ].values()
        ],
        "discovery": CONFIG["discovery"],
        "map": CONFIG["map"],
    }


@app.get(
    "/api/source/{source_id}/chunks",
    responses=_documented_responses(
        EXAMPLE_SOURCE_CHUNK_INDEX,
        INVALID_BBOX_ERROR,
        UNKNOWN_SOURCE_ERROR,
    ),
)
def get_source_chunk_index(
    source_id: SourceIdPath,
    bbox: BboxQuery = None,
    center: CenterQuery = None,
) -> JSONResponse:
    """The chunk index of the source buildings.

    A chunk's URL is `<chunk_url_prefix>/<layer>/<id>`; `bytes` is its
    uncompressed size. The source cache is built first if necessary.
    """
    source = _get_source(source_id)
    parsed_bbox = _parse_bbox(bbox)
    parsed_center = _parse_center(center)

    (
        _,
        manifest,
        was_built,
    ) = _ensure_source_cache(source)

    return _chunk_index_response(
        manifest,
        (SOURCE_LAYER_NAME,),
        parsed_bbox,
        parsed_center,
        _chunk_url_prefix(
            "source",
            source["id"],
            manifest["version"],
        ),
        "generated" if was_built else "hit",
        extra={"catalog_version": _solution_catalog_version()},
    )


def _source_extent_wgs84(
    source: dict[str, Any],
) -> tuple[float, float, float, float]:
    source_path = _source_path(source)

    if not source_path.is_file():
        raise HTTPException(
            status_code=404,
            detail=(
                f"Source file '{source['file']}' does not exist."
            ),
        )

    try:
        info = pyogrio.read_info(
            source_path,
            layer=source.get("layer"),
        )

        source_crs_value = info.get("crs")
        raw_bounds = info.get("total_bounds")

        if source_crs_value is None:
            raise ValueError(
                "The source has no coordinate reference system."
            )

        if raw_bounds is None or len(raw_bounds) != 4:
            raise ValueError(
                "The source driver did not return an extent."
            )

        source_crs = CRS.from_user_input(
            source_crs_value
        )

        transformer = Transformer.from_crs(
            source_crs,
            WGS84,
            always_xy=True,
        )

        transformed = transformer.transform_bounds(
            *[
                float(value)
                for value in raw_bounds
            ],
            densify_pts=21,
        )
    except Exception as exc:
        raise HTTPException(
            status_code=500,
            detail=(
                f"Could not determine the source extent: {exc}"
            ),
        ) from exc

    min_lon, min_lat, max_lon, max_lat = transformed

    min_lon = max(-180.0, min_lon)
    min_lat = max(-90.0, min_lat)
    max_lon = min(180.0, max_lon)
    max_lat = min(90.0, max_lat)

    epsilon = 1e-7

    if min_lon == max_lon:
        min_lon -= epsilon
        max_lon += epsilon

    if min_lat == max_lat:
        min_lat -= epsilon
        max_lat += epsilon

    if (
        not all(
            math.isfinite(value)
            for value in (
                min_lon,
                min_lat,
                max_lon,
                max_lat,
            )
        )
        or min_lon >= max_lon
        or min_lat >= max_lat
    ):
        raise HTTPException(
            status_code=500,
            detail="The source has an invalid extent.",
        )

    return min_lon, min_lat, max_lon, max_lat

@app.get(
    "/api/source/{source_id}/extent",
    responses=_documented_responses(
        EXAMPLE_EXTENT,
        UNKNOWN_SOURCE_ERROR,
    ),
)
def get_source_extent(
    source_id: SourceIdPath,
) -> JSONResponse:
    """The source extent as minLon, minLat, maxLon, maxLat in EPSG:4326,
    and its feature count."""
    source = _get_source(source_id)
    bbox = _source_extent_wgs84(source)

    source_path = _source_path(source)

    response = {
        "source": source_id,
        "source_label": source["label"],
        "bbox": list(bbox),
        "feature_count": _feature_count(
            source_path,
            source.get("layer"),
        ),
    }

    return JSONResponse(
        response,
        headers={
            "Cache-Control": "public, max-age=300",
        },
    )


@app.get(
    "/api/source/{source_id}/statistics",
    responses=_documented_responses(
        EXAMPLE_STATISTICS,
        UNKNOWN_SOURCE_ERROR,
    ),
)
def get_source_statistics(
    source_id: SourceIdPath,
) -> JSONResponse:
    """The solutions of a source: one row per k and tau, with the
    covered-polygon count of each algorithm's best run.

    A cell's status is "available" or "incomplete"; for incomplete
    solutions, "missing" lists the missing files.
    """
    source = _get_source(source_id)
    source_path = _source_path(source)

    total_polygons = _feature_count(
        source_path,
        source.get("layer"),
    )

    discovered: dict[
        tuple[str, str],
        dict[str, list[SolutionRun]],
    ] = {}

    catalog, catalog_version = _refreshed_solution_catalog()

    for (
        run_source_id,
        algorithm_id,
        k_value,
        tau_value,
    ), runs in catalog.items():
        if (
            run_source_id == source_id
            and _parameters_requestable(source, k_value, tau_value)
        ):
            discovered.setdefault(
                (k_value, tau_value),
                {},
            )[algorithm_id] = runs

    algorithms_with_solutions = {
        algorithm_id
        for runs_by_algorithm in discovered.values()
        for algorithm_id in runs_by_algorithm
    }

    rows: list[dict[str, Any]] = []

    sorted_keys = sorted(
        discovered,
        key=lambda item: (
            _parameter_sort_key(item[0]),
            _parameter_sort_key(item[1]),
        ),
    )

    for k_value, tau_value in sorted_keys:
        cells: dict[str, Any] = {}

        for algorithm_id, candidates in discovered[
            (k_value, tau_value)
        ].items():
            selected = _select_run(candidates).path

            solution_paths = {
                layer_name: (
                    selected / file_name
                )
                for layer_name, file_name
                in SOLUTION_FILES.items()
            }

            missing_files = [
                path.name
                for path in solution_paths.values()
                if not path.is_file()
            ]

            covered_file = solution_paths["covered"]

            if missing_files:
                cells[algorithm_id] = {
                    "status": "incomplete",
                    "count": (
                        _feature_count(covered_file)
                        if covered_file.is_file()
                        else None
                    ),
                    "runs": len(candidates),
                    "directory": selected.name,
                    "missing": sorted(missing_files),
                }
                continue

            cells[algorithm_id] = {
                "status": "available",
                "count": _feature_count(
                    covered_file
                ),
                "runs": len(candidates),
                "directory": selected.name,
                "missing": [],
            }

        rows.append(
            {
                "k": k_value,
                "tau": tau_value,
                "cells": cells,
            }
        )

    response = {
        "source": source_id,
        "source_label": source["label"],
        "total_polygons": total_polygons,
        "metric": "covered_polygons",
        # With discovered solutions, only algorithms with at least one
        # solution for this source are offered.
        "algorithms": [
            {
                "id": algorithm["id"],
                "label": algorithm["label"],
                "short_label": algorithm["short_label"],
            }
            for algorithm in CONFIG[
                "algorithms"
            ].values()
            if not CONFIG["discovery"]["solutions"]
            or algorithm["id"] in algorithms_with_solutions
        ],
        "rows": rows,
        "catalog_version": catalog_version,
    }

    # Solutions can be added at any time; the browser must not reuse an
    # older response.
    return JSONResponse(
        response,
        headers={
            "Cache-Control": "no-cache",
        },
    )


@app.get(
    "/api/solutions/version",
    responses=_documented_responses(
        {"catalog_version": EXAMPLE_CATALOG_VERSION}
    ),
)
def get_solution_catalog_version() -> JSONResponse:
    """A short hash that changes whenever a solution directory is added,
    removed, or renamed."""
    return JSONResponse(
        {"catalog_version": _solution_catalog_version()},
        headers={"Cache-Control": "no-cache"},
    )


@app.get(
    "/api/solution/chunks",
    responses=_documented_responses(
        EXAMPLE_SOLUTION_CHUNK_INDEX,
        (
            400,
            "Unknown layer 'example'. Allowed values are: "
            + ", ".join(SOLUTION_LAYER_NAMES)
            + ".",
        ),
        (
            404,
            "No matching solution directory was found. Expected "
            f"'{EXAMPLE_SOURCE_ID}_par_9_0.500000_*"
            f"{SOLUTION_DIRECTORY_SUFFIX}' in "
            f"'data/output/{EXAMPLE_ALGORITHM_ID}/visualizations'.",
        ),
    ),
)
def get_solution_chunk_index(
    source: Annotated[
        str,
        Query(
            description="Source ID, as listed by /api/config.",
            openapi_examples=_parameter_example(EXAMPLE_SOURCE_ID),
        ),
    ],
    algorithm: Annotated[
        str,
        Query(
            description="Algorithm ID, as listed by /api/config.",
            openapi_examples=_parameter_example(EXAMPLE_ALGORITHM_ID),
        ),
    ],
    k: Annotated[
        str,
        Query(
            description="Value of k, as in the solution directory name.",
            openapi_examples=_parameter_example("9"),
        ),
    ],
    tau: Annotated[
        str,
        Query(
            description=(
                "Value of tau; 0.32 stands for the token 0.320000 in "
                "the solution directory name."
            ),
            openapi_examples=_parameter_example("0.32"),
        ),
    ],
    layers: Annotated[
        str | None,
        Query(
            description=(
                "Comma-separated layers to list: "
                + ", ".join(SOLUTION_LAYER_NAMES)
                + ". Default: all layers."
            ),
            openapi_examples=_parameter_example("antennas,covered"),
        ),
    ] = None,
    bbox: BboxQuery = None,
    center: CenterQuery = None,
) -> JSONResponse:
    """The chunk index of a solution, in the same form as the source chunk
    index.

    If several runs exist for the combination, the best one is shown;
    "runs" describes all of them. The solution cache is built first if
    necessary, which can take several minutes.
    """
    layer_names = _parse_layer_list(layers)
    parsed_bbox = _parse_bbox(bbox)
    parsed_center = _parse_center(center)

    (
        source_config,
        algorithm_config,
        k_token,
        tau_token,
        solution_directory,
    ) = _resolve_solution_request(
        source,
        algorithm,
        k,
        tau,
    )

    (
        _,
        manifest,
        was_built,
    ) = _ensure_solution_cache(
        source_config,
        algorithm_config,
        solution_directory,
    )

    runs = _find_solution_runs(
        source_config,
        algorithm_config,
        k_token,
        tau_token,
    )

    return _chunk_index_response(
        manifest,
        layer_names,
        parsed_bbox,
        parsed_center,
        _chunk_url_prefix(
            "solution",
            source_config["id"],
            algorithm_config["id"],
            solution_directory.name,
            manifest["version"],
        ),
        "generated" if was_built else "hit",
        extra={
            "runs": _runs_summary(runs),
            "catalog_version": _solution_catalog_version(),
        },
    )


# Chunk routes are asynchronous on purpose: they only read the manifest
# and stream a small file, and must not compete for worker threads with
# requests that may be waiting for a cache build.
@app.get(
    "/api/chunk/source/{source_id}/{version}/{layer}/{chunk_id}",
    response_class=Response,
    responses=_documented_responses(
        EXAMPLE_SOURCE_CHUNK,
        UNKNOWN_CHUNK_ERROR,
        media_type="application/geo+json",
    ),
)
async def get_source_chunk(
    request: Request,
    source_id: SourceIdPath,
    version: Annotated[
        str,
        ApiPath(
            description="Cache version, from the chunk index.",
            openapi_examples=_parameter_example(EXAMPLE_SOURCE_VERSION),
        ),
    ],
    layer: Annotated[
        str,
        ApiPath(
            description="Always \"source\".",
            openapi_examples=_parameter_example(SOURCE_LAYER_NAME),
        ),
    ],
    chunk_id: Annotated[
        str,
        ApiPath(
            description="Chunk ID, from the chunk index.",
            openapi_examples=_parameter_example("010010"),
        ),
    ],
) -> Response:
    """One chunk of the source buildings as a GeoJSON FeatureCollection.

    Take the URL from the chunk index. The chunk is gzip-compressed if the
    client accepts it and may be cached permanently; an outdated version
    returns 404.
    """
    source = _get_source(source_id)

    return _chunk_file_response(
        request,
        _source_cache_directory(source),
        version,
        layer,
        chunk_id,
    )


@app.get(
    "/api/chunk/solution/{source_id}/{algorithm_id}/"
    "{solution_directory}/{version}/{layer}/{chunk_id}",
    response_class=Response,
    responses=_documented_responses(
        EXAMPLE_SOLUTION_CHUNK,
        UNKNOWN_CHUNK_ERROR,
        media_type="application/geo+json",
    ),
)
async def get_solution_chunk(
    request: Request,
    source_id: SourceIdPath,
    algorithm_id: Annotated[
        str,
        ApiPath(
            description="Algorithm ID, as listed by /api/config.",
            openapi_examples=_parameter_example(EXAMPLE_ALGORITHM_ID),
        ),
    ],
    solution_directory: Annotated[
        str,
        ApiPath(
            description="Solution directory, from the chunk index.",
            openapi_examples=_parameter_example(
                EXAMPLE_SOLUTION_DIRECTORY
            ),
        ),
    ],
    version: Annotated[
        str,
        ApiPath(
            description="Cache version, from the chunk index.",
            openapi_examples=_parameter_example(EXAMPLE_SOLUTION_VERSION),
        ),
    ],
    layer: Annotated[
        str,
        ApiPath(
            description=(
                "Solution layer: " + ", ".join(SOLUTION_LAYER_NAMES) + "."
            ),
            openapi_examples=_parameter_example("antennas"),
        ),
    ],
    chunk_id: Annotated[
        str,
        ApiPath(
            description="Chunk ID, from the chunk index.",
            openapi_examples=_parameter_example("0"),
        ),
    ],
) -> Response:
    """One chunk of a solution layer as a GeoJSON FeatureCollection.

    Take the URL from the chunk index; caching and errors work as for
    source chunks.
    """
    source = _get_source(source_id)
    algorithm = _get_algorithm(algorithm_id)

    if (
        _solution_tokens_from_directory(
            source,
            solution_directory,
        )
        is None
    ):
        raise HTTPException(
            status_code=404,
            detail="Unknown solution directory.",
        )

    try:
        cache_directory = _solution_cache_directory(
            algorithm,
            Path(solution_directory),
        )
    except RuntimeError as exc:
        raise HTTPException(
            status_code=404,
            detail="Unknown solution directory.",
        ) from exc

    return _chunk_file_response(
        request,
        cache_directory,
        version,
        layer,
        chunk_id,
    )


def _build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run the GIS Cup Solution Viewer or "
            "precompute its geospatial cache."
        )
    )

    parser.add_argument(
        "--host",
        default="127.0.0.1",
        help=(
            "Address on which the web server listens "
            "(default: 127.0.0.1)."
        ),
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8000,
        help=(
            "TCP port on which the web server listens "
            "(default: 8000)."
        ),
    )
    parser.add_argument(
        "--log-level",
        choices=(
            "critical",
            "error",
            "warning",
            "info",
            "debug",
            "trace",
        ),
        default="info",
        help="Uvicorn log level (default: info).",
    )
    parser.add_argument(
        "--no-precompute",
        action="store_true",
        help=(
            "Disable automatic background cache "
            "precomputation when the server starts."
        ),
    )
    parser.add_argument(
        "--precompute-only",
        action="store_true",
        help=(
            "Generate all missing caches and exit "
            "without starting the web server."
        ),
    )
    parser.add_argument(
        "--force-precompute",
        action="store_true",
        help=(
            "Rebuild caches even when their manifests "
            "indicate that they are current."
        ),
    )

    return parser


def main(
    arguments: list[str] | None = None,
) -> int:
    global STARTUP_PRECOMPUTE_ENABLED
    global STARTUP_PRECOMPUTE_FORCE

    parser = _build_argument_parser()
    options = parser.parse_args(arguments)

    if options.port < 1 or options.port > 65535:
        parser.error(
            "--port must be between 1 and 65535."
        )

    if (
        options.no_precompute
        and options.force_precompute
        and not options.precompute_only
    ):
        parser.error(
            "--no-precompute and --force-precompute "
            "cannot be used together when starting "
            "the server."
        )

    logging.basicConfig(
        level=getattr(
            logging,
            options.log_level.upper(),
            logging.INFO,
        ),
        format=(
            "%(asctime)s %(levelname)s "
            "%(name)s: %(message)s"
        ),
    )

    if options.precompute_only:
        report = precompute_all(
            force=options.force_precompute
        )

        print(
            json.dumps(
                report,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
        )

        return 1 if report["failures"] else 0

    STARTUP_PRECOMPUTE_ENABLED = (
        not options.no_precompute
    )
    STARTUP_PRECOMPUTE_FORCE = (
        options.force_precompute
    )

    import uvicorn

    uvicorn.run(
        app,
        host=options.host,
        port=options.port,
        log_level=options.log_level,
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())