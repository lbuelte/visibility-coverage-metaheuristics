"""Download source and solution data from Zenodo into the data folder.

The default record is "GIS Cup 2026 Solution Visualizations for
'Engineering Metaheuristics for a Visibility Coverage Problem'"
(https://doi.org/10.5281/zenodo.23055363, CC BY 4.0). Its archive contains

    <archive>/input/<source>.geojson
    <archive>/output/<algorithm>/visualizations/<solution>.solution.visualization/
    <archive>/LICENSE.md

The script places these files in the layout the viewer expects:

    data/input/<source>.geojson
    data/output/<algorithm>/visualizations/<solution>.solution.visualization/
    data/input/zenodo-<record>-LICENSE.md

Existing files and solution directories are kept unless --force is given.
Only the Python standard library is used.

Examples:

    python scripts/download_zenodo_data.py
    python scripts/download_zenodo_data.py --rename-algorithm GISCupBonn_eSA=bonn-esa
    python scripts/download_zenodo_data.py --archive downloaded.zip
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath


BASE_DIR = Path(__file__).resolve().parents[1]

DEFAULT_RECORD = "23055363"
ZENODO_API = "https://zenodo.org/api/records"

VISUALIZATIONS_DIRECTORY = "visualizations"

USER_AGENT = "giscup-viewer-data-download/1.0"


def fetch_record(record: str) -> dict:
    """Return the record metadata. A concept record ID (as in the DOI)
    resolves to the latest version."""
    request = urllib.request.Request(
        f"{ZENODO_API}/{record}",
        headers={
            "Accept": "application/json",
            "User-Agent": USER_AGENT,
        },
    )

    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(url: str, destination: Path, expected_size: int) -> None:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": USER_AGENT},
    )

    received = 0
    last_report = -1

    with (
        urllib.request.urlopen(request, timeout=60) as response,
        destination.open("wb") as output,
    ):
        while chunk := response.read(1024 * 1024):
            output.write(chunk)
            received += len(chunk)

            if expected_size:
                percent = received * 100 // expected_size

                if percent // 10 != last_report:
                    last_report = percent // 10
                    print(
                        f"  {percent:3d}% "
                        f"({received / 1e6:.1f} of "
                        f"{expected_size / 1e6:.1f} MB)",
                        flush=True,
                    )


def verify_checksum(path: Path, checksum: str | None) -> None:
    """Check a Zenodo checksum such as "md5:ef56..."."""
    if not checksum or ":" not in checksum:
        print("  No checksum available; skipping verification.")
        return

    algorithm, expected = checksum.split(":", 1)
    digest = hashlib.new(algorithm)

    with path.open("rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)

    if digest.hexdigest() != expected:
        raise SystemExit(
            f"Checksum mismatch for {path.name}: expected {checksum}, "
            f"got {algorithm}:{digest.hexdigest()}."
        )

    print(f"  Checksum verified ({algorithm}).")


def target_path(
    member: PurePosixPath,
    record_id: str,
    algorithm_names: dict[str, str],
) -> tuple[str, PurePosixPath] | None:
    """Map an archive path to (kind, path relative to the data folder).

    kind is "input", "solution", or "license"; None means the member is
    not used. Paths with ".." or absolute paths are rejected by the
    caller before this is called.
    """
    parts = member.parts

    # Skip the archive's top-level folder, if there is one.
    for index, part in enumerate(parts):
        if part in {"input", "output"}:
            parts = parts[index:]
            break
    else:
        if parts and parts[-1] == "LICENSE.md":
            return (
                "license",
                PurePosixPath("input", f"zenodo-{record_id}-LICENSE.md"),
            )

        return None

    if parts[0] == "input" and len(parts) == 2:
        return "input", PurePosixPath(*parts)

    if (
        parts[0] == "output"
        and len(parts) == 5
        and parts[2] == VISUALIZATIONS_DIRECTORY
    ):
        algorithm = algorithm_names.get(parts[1], parts[1])

        return "solution", PurePosixPath(
            "output",
            algorithm,
            VISUALIZATIONS_DIRECTORY,
            parts[3],
            parts[4],
        )

    return None


def safe_member_path(name: str) -> PurePosixPath | None:
    path = PurePosixPath(name)

    if path.is_absolute() or ".." in path.parts or not path.parts:
        return None

    return path


def unpack(
    archive: Path,
    data_dir: Path,
    record_id: str,
    algorithm_names: dict[str, str],
    force: bool,
) -> None:
    """Extract the archive into the data folder.

    Everything is extracted into a staging directory first and then moved
    into place, one source file or solution directory at a time, so an
    interrupted run never leaves a half-written solution behind.
    """
    data_dir.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".download-", dir=data_dir))

    # Top-level units that are moved into place: a source file, a license
    # file, or a solution directory.
    units: dict[PurePosixPath, str] = {}

    try:
        with zipfile.ZipFile(archive) as zip_file:
            for info in zip_file.infolist():
                if info.is_dir():
                    continue

                member = safe_member_path(info.filename)

                if member is None:
                    print(f"  Skipping unsafe path: {info.filename}")
                    continue

                mapped = target_path(member, record_id, algorithm_names)

                if mapped is None:
                    print(f"  Skipping unused file: {info.filename}")
                    continue

                kind, relative = mapped
                unit = (
                    relative.parent if kind == "solution" else relative
                )
                units[unit] = kind

                destination = staging.joinpath(*relative.parts)
                destination.parent.mkdir(parents=True, exist_ok=True)

                with (
                    zip_file.open(info) as source,
                    destination.open("wb") as output,
                ):
                    shutil.copyfileobj(source, output, 1024 * 1024)

        added = skipped = replaced = 0

        for unit, kind in sorted(units.items()):
            staged = staging.joinpath(*unit.parts)
            final = data_dir.joinpath(*unit.parts)

            if final.exists():
                if not force:
                    print(f"  Exists, kept: {unit}")
                    skipped += 1
                    continue

                if final.is_dir():
                    shutil.rmtree(final)
                else:
                    final.unlink()

                replaced += 1
            else:
                added += 1

            final.parent.mkdir(parents=True, exist_ok=True)
            staged.replace(final)
            print(f"  {kind:8s} {unit}")

        print(
            f"\nAdded {added}, replaced {replaced}, kept {skipped} "
            f"existing (use --force to replace)."
        )
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def parse_renames(values: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}

    for value in values:
        old, separator, new = value.partition("=")

        if not separator or not old or not new or "/" in new:
            raise SystemExit(
                f"Invalid --rename-algorithm '{value}'; expected OLD=NEW."
            )

        result[old] = new

    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Download source and solution data from Zenodo into the "
            "viewer's data folder."
        )
    )
    parser.add_argument(
        "--record",
        default=DEFAULT_RECORD,
        help=(
            "Zenodo record or concept record ID "
            f"(default: {DEFAULT_RECORD}, the latest version of "
            "doi:10.5281/zenodo.23055363)."
        ),
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=BASE_DIR / "data",
        help="Target data folder (default: data/ in the repository).",
    )
    parser.add_argument(
        "--rename-algorithm",
        action="append",
        default=[],
        metavar="OLD=NEW",
        help=(
            "Store an algorithm directory under another name, for "
            "example GISCupBonn_eSA=bonn-esa. May be repeated."
        ),
    )
    parser.add_argument(
        "--archive",
        type=Path,
        help="Use an already downloaded archive instead of downloading.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace existing source files and solution directories.",
    )
    options = parser.parse_args()

    algorithm_names = parse_renames(options.rename_algorithm)
    data_dir = options.data_dir.resolve()

    if options.archive:
        print(f"Using archive {options.archive}")
        unpack(
            options.archive,
            data_dir,
            options.record,
            algorithm_names,
            options.force,
        )
        return 0

    print(f"Reading Zenodo record {options.record} ...")
    record = fetch_record(options.record)
    record_id = str(record.get("id", options.record))
    title = record.get("metadata", {}).get("title", "")
    print(f"  {title}\n  Version record {record_id}, doi:{record.get('doi')}")

    archives = [
        file
        for file in record.get("files", [])
        if file.get("key", "").lower().endswith(".zip")
    ]

    if not archives:
        raise SystemExit("The record contains no zip archive.")

    with tempfile.TemporaryDirectory() as temporary:
        for file in archives:
            path = Path(temporary) / file["key"]
            print(f"\nDownloading {file['key']} ...")
            download(file["links"]["self"], path, int(file.get("size", 0)))
            verify_checksum(path, file.get("checksum"))
            print(f"Unpacking into {data_dir} ...")
            unpack(path, data_dir, record_id, algorithm_names, options.force)

    return 0


if __name__ == "__main__":
    sys.exit(main())
