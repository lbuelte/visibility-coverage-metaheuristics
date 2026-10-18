from __future__ import annotations

import argparse
import shutil
import urllib.request
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parents[1]
TARGET_ROOT = (
    BASE_DIR
    / "static"
    / "vendor"
    / "leaflet"
)

LEAFLET_VERSION = "1.9.4"
BASE_URL = (
    f"https://unpkg.com/leaflet@"
    f"{LEAFLET_VERSION}/dist"
)

FILES = {
    "leaflet.js": f"{BASE_URL}/leaflet.js",
    "leaflet.js.map": (
        f"{BASE_URL}/leaflet.js.map"
    ),
    "leaflet.css": f"{BASE_URL}/leaflet.css",
    "images/layers.png": (
        f"{BASE_URL}/images/layers.png"
    ),
    "images/layers-2x.png": (
        f"{BASE_URL}/images/layers-2x.png"
    ),
    "images/marker-icon.png": (
        f"{BASE_URL}/images/marker-icon.png"
    ),
    "images/marker-icon-2x.png": (
        f"{BASE_URL}/images/marker-icon-2x.png"
    ),
    "images/marker-shadow.png": (
        f"{BASE_URL}/images/marker-shadow.png"
    ),
}


def download_file(
    url: str,
    destination: Path,
) -> None:
    destination.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    temporary = destination.with_suffix(
        destination.suffix + ".tmp"
    )

    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": (
                "GIS-Cup-Solution-Viewer/"
                f"{LEAFLET_VERSION}"
            )
        },
    )

    with (
        urllib.request.urlopen(
            request,
            timeout=60,
        ) as response,
        temporary.open("wb") as output,
    ):
        shutil.copyfileobj(response, output)

    temporary.replace(destination)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Download Leaflet into the application's "
            "static asset directory."
        )
    )

    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace existing vendored files.",
    )

    options = parser.parse_args()

    for relative_name, url in FILES.items():
        destination = TARGET_ROOT / relative_name

        if (
            destination.is_file()
            and destination.stat().st_size > 0
            and not options.force
        ):
            print(f"Reusing {relative_name}")
            continue

        print(f"Downloading {relative_name}")
        download_file(url, destination)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())