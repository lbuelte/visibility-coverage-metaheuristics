# GIS Cup 2026 Solution Viewer

A FastAPI and Leaflet web application for interactively exploring building
visibility and coverage solutions produced for the [GIS Cup 2026 challenge](https://sigspatial2026.sigspatial.org/giscup.html).

The viewer supports:

- Source-building outlines.
- Antenna positions.
- Covered and uncovered building polygons.
- Visible building edges.
- Optional visibility geometries.
- Coverage-percentage labels at high zoom levels.
- Overlayed visual comparison of two solutions.
- A statistics table for the available algorithms and parameter combinations.
- Clicking statistics cells to select a primary solution.
- Loading of large layers in fixed spatial chunks, nearest to the map centre
  first, with background preloading of the whole instance.
- Automatically generated, precompressed chunk caches.

## Requirements

- Python 3.11 or newer.
- A working GeoPandas/Pyogrio installation.
- Input and solution files with a defined coordinate reference system (see [below](#solution-directory-layout-and-naming-convention)).
- Write access to `data/cache`.
- Internet access for the default OpenStreetMap tiles. Leaflet (the library for rendering) is bundled below `static/vendor/leaflet` (BSD 2-Clause, see `static/vendor/leaflet/LICENSE`).

The Python wheels available for common operating systems normally include the
required native geospatial libraries. If installation fails, ensure that the
GDAL, GEOS, and PROJ libraries required by GeoPandas and Pyogrio are available
for your platform. They are needed to read the GeoPackage and GeoJSON files,
to process their geometries, and to transform all cached map data to
`EPSG:4326` before it is sent to Leaflet.

## Installation

First, create and activate a virtual environment: 

### Linux and macOS

```bash
python3 -m venv .venv
source .venv/bin/activate
```

### Windows PowerShell

```powershell
py -3.11 -m venv .venv
.venv\Scripts\Activate.ps1
```

Second, install the dependencies:

```bash
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

## Quick start

1. Place source datasets (geojson or geopackage) below `data/input`.
2. Place generated solution directories below `data/output`. 

   These have to consist of five GeoPackage files and follow a specific format and naming scheme, as computed with [our solver and visualization computation tools](https://github.com/lbuelte/visibility-coverage-metaheuristics/tree/main) (see [below](#solution-directory-layout-and-naming-convention)). 
   
   It is possible to link the `data/` folder of this viewer to the `../data/` folder of our solver, leading to all solver output directly being available in the viewer: 

   ```bash
   mv data data_old
   ln -sr ../data data
   ```

   Alternatively, download the published competition instance and our
   solutions from Zenodo ([doi:10.5281/zenodo.23055364](https://doi.org/10.5281/zenodo.23055364),
   CC BY 4.0) into `data/`:

   ```bash
   python scripts/download_zenodo_data.py
   ```

3. Optionally configure the sources, algorithms, parameters, and map in `config.yaml`. By default, all available sources and solutions are auto-detected without any changes to the config necessary.

4. Optionally precompute all caches before starting the server (this might take a few minutes):

   ```bash
   python app.py --precompute-only
   ```

5. Start the application (locally):

   ```bash
   python app.py --host 127.0.0.1 --port 8000
   ```

6. Open <http://127.0.0.1:8000>.

By default, the web server launches cache precomputation as a background task.
It builds the caches of every source and of every solution the viewer can
display (every algorithm, $k$, and $\tau$ combination, configured or
discovered). For a combination with several runs, only the run that the
viewer displays (the best one, with the modification time as tie breaker) is
built.
The application remains available while that task is running. A request for a
source or solution that has not yet been processed also generates the required
cache on demand (also relevant when resources are added while the application is already running).

See below for the full [Command Line Interface](#command-line-interface) documentation. E.g. for access from another machine, bind to all interfaces:

```bash
python app.py --host 0.0.0.0 --port 8000
```

Do not expose this application directly to the public Internet! Use a reverse proxy with HTTPS for a production deployment (see below for [Deployment with Docker](#deployment-with-docker)).

## Project structure

```text
.
├── app.py
├── config.yaml
├── data
│   ├── cache
│   │   ├── input
│   │   └── output
│   ├── input
│   └── output
├── deploy
│   ├── .env.example
│   ├── certbot
│   │   └── certbot-loop.sh
│   ├── compose.yaml
│   └── nginx
│       ├── 40-tls-certificates.sh
│       ├── default.conf.template
│       └── Dockerfile
├── Dockerfile
├── README.md
├── requirements.txt
├── scripts
│   ├── download_zenodo_data.py
│   └── vendor_leaflet.py
├── static
│   ├── app.js
│   ├── apple-touch-icon.png
│   ├── chunks.js
│   ├── favicon.ico
│   ├── favicon.svg
│   ├── styles.css
│   └── vendor
│       └── leaflet
└── templates
    └── index.html
```

The contents of `data/cache` are generated automatically and should not be
committed to version control; only its `.gitkeep` is. `scripts/vendor_leaflet.py` downloads the bundled Leaflet files again if they need to be replaced.

`static/favicon.ico` and `static/apple-touch-icon.png` are generated from
`static/favicon.svg` (with `rsvg-convert` and ImageMagick). After changing the
SVG, regenerate them and raise the `?v=` of the icon links in
`templates/index.html`:

```bash
cd static
rsvg-convert -w 16 -h 16 favicon.svg -o /tmp/favicon-16.png
rsvg-convert -w 32 -h 32 favicon.svg -o /tmp/favicon-32.png
magick /tmp/favicon-16.png /tmp/favicon-32.png favicon.ico

# iOS rounds the corners itself and needs an opaque, square icon.
sed 's/rx="14"/rx="0"/g' favicon.svg |
    rsvg-convert -w 180 -h 180 |
    magick - -background "#003f70" -alpha remove -alpha off apple-touch-icon.png
```

## Command-line interface

Run:

```bash
python app.py --help
```

The available options are:

| Option | Description |
|---|---|
| `--host ADDRESS` | Address on which Uvicorn listens; default: `127.0.0.1`. |
| `--port PORT` | TCP port on which Uvicorn listens; default: `8000`. |
| `--log-level LEVEL` | Uvicorn log level. |
| `--no-precompute` | Disable the automatic background precomputation pass. |
| `--precompute-only` | Generate caches and exit without starting the server. |
| `--force-precompute` | Rebuild caches even if their manifests are current. |

Stopping the server (for example with Ctrl+C) also stops a running cache build
at its next checkpoint; the incomplete build is discarded.

Examples:

```bash
# Prepare all missing or stale caches and exit.
python app.py --precompute-only

# Rebuild every discovered cache and exit.
python app.py --precompute-only --force-precompute

# Start without scanning all sources and solutions in the background.
# Missing caches are still generated when requested.
python app.py --no-precompute --host 0.0.0.0 --port 8000

# Start and force the background task to rebuild all caches.
python app.py --force-precompute
```

### Starting with the Uvicorn command

The application can also be started through Uvicorn directly:

```bash
uvicorn app:app --host 127.0.0.1 --port 8000
```

When using the Uvicorn command, control startup precomputation through the
`GIS_VIEWER_STARTUP_PRECOMPUTE` environment variable:

```bash
GIS_VIEWER_STARTUP_PRECOMPUTE=0 \
uvicorn app:app --host 0.0.0.0 --port 8000
```

Accepted false values are `0`, `false`, `no`, and `off`, without regard to
case. If the variable is absent, automatic startup precomputation is enabled.

For a multi-worker production process, it is preferable to precompute once and
then disable the background scan in the workers:

```bash
python app.py --precompute-only

GIS_VIEWER_STARTUP_PRECOMPUTE=0 \
uvicorn app:app --host 0.0.0.0 --port 8000 --workers 2
```

Cross-process cache locks protect individual cache directories, but avoiding a
duplicate startup scan still reduces unnecessary work.

## Deployment with Docker

This section is only needed for a public server; for local use, see
[Quick start](#quick-start). `deploy/compose.yaml` runs the viewer behind
nginx with HTTPS. Certificates are obtained from Let's Encrypt and renewed
automatically. The viewer still uses `config.yaml` and `data/` from the
repository root.

| Service | Purpose |
|---|---|
| `app` | The viewer, built from `Dockerfile`; only reachable through nginx. |
| `nginx` | HTTPS termination on ports 80 and 443 (`deploy/nginx`). |
| `certbot` | Obtains and renews the certificate (`deploy/certbot`). |

Requirements on the server:

- Docker with the Compose plugin.
- A DNS record for the host name (for example `giscup26.mydomain.tld`)
  pointing to the server.
- Ports 80 and 443 reachable from the Internet. Let's Encrypt validates the
  domain on port 80.

Setup:

1. Clone the repository on the server, configure `config.yaml` and copy
   content into `data/input` and `data/output`. An existing cache can
   be copied into `data/cache` as well and is reused. The user in
   `APP_UID`/`APP_GID` (default 1000) must be able to write to `data/cache`;
   this is the case if that user cloned the repository.
2. Change to the `deploy` directory. All `docker compose` commands below are
   run from there.

   ```bash
   cd deploy
   ```

3. Create the settings file and adjust it:

   ```bash
   cp .env.example .env
   ```

   Don't forget to fill in your own domain and e-mail address.

4. Start everything:

   ```bash
   docker compose up -d --build
   ```

On the first start, nginx uses a temporary self-signed certificate, and
browsers show a warning. Within about a minute of Let's Encrypt issuing the
certificate, nginx switches to it automatically. Renewals are checked twice a
day and picked up the same way. Follow the progress with:

```bash
docker compose logs -f certbot nginx
```

The viewer builds missing caches in the background after starting; see
`docker compose logs -f app`. New solutions appear without a restart (see
[Discovery settings](#discovery-settings)). Restart the viewer after changing
`config.yaml` or adding source files or algorithm directories:

```bash
docker compose restart app
```

nginx limits the number of simultaneous requests and the request rate per IP
address and answers with `429 Too Many Requests` beyond them. If many users
share one address and see such errors, raise the limits at the top of
`deploy/nginx/default.conf.template` and rebuild with
`docker compose up -d --build nginx`.

### Testing with the Let's Encrypt staging environment

Set `LETSENCRYPT_STAGING=1` in `deploy/.env` for a first test. Staging certificates
are not trusted by browsers, but failed attempts do not count against the
production rate limits. To switch to a real certificate afterwards, delete the
staging certificate, set `LETSENCRYPT_STAGING=0`, and recreate the certbot
service:

```bash
docker compose run --rm --entrypoint certbot certbot \
    delete --cert-name giscup26.mydomain.tld
docker compose up -d certbot
```

## Configuration

The application reads `config.yaml` when the Python module starts. Restart the
server after changing the configuration.

A representative configuration is:

```yaml
app:
  title: "GIS Cup 2026 Solution Viewer"

map:
  center: [33.98, -117.375]
  zoom: 14
  max_zoom: 20

  tile_url: "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
  tile_max_native_zoom: 19

  coverage_label_min_zoom: 18
  background_opacity: 0.6
  overlay_initial_blend: 50
  initial_source_zoom_offset: 1

  coverage_label_multiplier: 100
  coverage_label_decimals: 1
  coverage_label_suffix: "%"

cache:
  chunk_max_bytes: 524288
  chunk_max_bytes_by_layer:
    coverage_labels: 65536

discovery:
  sources: true
  solutions: true

parameters:
  k_values:
    - "9"
    - "49"
    - "484"

  tau_values:
    - "0.32"
    - "0.49"
    - "0.68"

sources:
  - id: "GIS-cup-competition-dataset"
    label: "GIS Cup Competition 2026"
    file: "GIS-cup-competition-dataset.geojson"

algorithms:
  - id: "GISCupBonn_eSA"
    label: "Extended Simulated Annealing (Bonn)"
    short_label: "eSA (Bonn)"
    directory: "GISCupBonn_eSA"
```

### Application settings

- `app.title` is displayed in the page header and used as the browser title.

### Map settings

- `map.center` contains the initial Leaflet latitude and longitude.
- `map.zoom` is the initial zoom level.
- `map.max_zoom` is the maximum permitted tile-map zoom.
- `map.tile_url` is the Leaflet tile URL template.
- `map.attribution` is the attribution displayed by Leaflet.
- `map.tile_max_native_zoom` is the maximum zoom which is supported natively by
  the tile source provided.
- `map.coverage_label_min_zoom` is the zoom level from which coverage labels
  are shown while "Visibility" is on.
- `map.background_opacity` is a value from `0` to `1`.
- `map.overlay_initial_blend` is a value from `0` to `100`, where `0` shows
  only the primary solution and `100` shows only the comparison solution when entering comparison mode.
- `map.initial_source_zoom_offset` applies additional zoom levels after the
  complete source extent has been fitted. The default value `1` starts one
  level closer; use `0` to show the exact complete fitted extent.
- `map.coverage_label_multiplier` is applied before coverage values are
  displayed. 
- `map.coverage_label_decimals` must be between `0` and `8`.
- `map.coverage_label_suffix` is appended to formatted coverage values.

For coverage values stored between `0` and `1`, use:

```yaml
coverage_label_multiplier: 100
coverage_label_suffix: "%"
```

For coverage values already stored between `0` and `100`, use:

```yaml
coverage_label_multiplier: 1
coverage_label_suffix: "%"
```

### Cache settings

Every map layer is split into spatial chunks during preprocessing; see
[Cache design](#cache-design).

- `cache.chunk_max_bytes` is the maximum size of one chunk in bytes of
  uncompressed GeoJSON. The default is `524288` (512 KiB). The minimum is
  `4096`. Only a single feature that is larger than the limit on its own
  produces a larger chunk.
- `cache.chunk_max_bytes_by_layer` optionally overrides the limit for
  individual layers: `source`, `antennas`, `covered`, `uncovered`, `edges`,
  `visibility`, and `coverage_labels`.

Smaller chunks give finer-grained loading at the cost of more requests.
Coverage labels are displayed as one marker each, so a smaller limit for
`coverage_labels` keeps the number of simultaneously loaded markers low.

Changing these settings rebuilds the affected caches.

### Discovery settings

Discovery lets the viewer find sources and solutions in the data directories
instead of requiring every one of them in `config.yaml`. Both options are on
by default:

```yaml
discovery:
  sources: true
  solutions: true
```

- `discovery.sources: true` also offers every source file in `data/input`
  (`.gpkg`, `.geojson`, `.json`) that is not listed under `sources`. The file
  name without its extension becomes the source ID and label, the first layer
  of the file is used, and the global `parameters` apply. With it, `sources`
  may be empty.
- `discovery.solutions: true` also offers every directory in `data/output`
  that contains a `visualizations` directory as an algorithm, with its name as
  ID and label. Every $k$ and $\tau$ found in the solution names can then be
  selected; configured `parameters` are optional and only add choices. For
  each source, only the algorithms with at least one solution for it are
  offered, both in the selectors and in the statistics table.

Files and directories whose names are not valid IDs (letters, numbers, `.`,
`_`, and `-`), or whose ID is already taken by a configured entry, are skipped
with a warning in the log.

With an option set to `false`, only the configured sources or algorithms are
offered; without `discovery.solutions`, only solutions with configured $k$ and
$\tau$ values can be displayed.

New source files and algorithm directories appear after a restart; new
solutions appear within about ten seconds, and the statistics panel in an open
browser updates the next time the map loads data or a selector is opened.

### Parameter settings

`parameters.k_values` and `parameters.tau_values` define the values that may be
requested through the API and selected in the browser. With
`discovery.solutions` enabled, both lists are optional: every $k$ and $\tau$
found in solution names can then be selected as well.

In the browser, $k$ and $\tau$ choices without a solution for the selected
source and algorithm are greyed out. When a new algorithm or $k$ leaves the
current choice without a solution, the nearest available value is selected.

Values should be quoted so that their textual representation remains stable:

```yaml
parameters:
  k_values: ["9", "49", "484"]
  tau_values: ["0.32", "0.49", "0.68"]
```

A source can set its own `parameters.k_values` and `parameters.tau_values`,
which replace the global lists for that source; a list it does not set is
taken from the global `parameters`. Without `discovery.solutions`, every
source must end up with at least one $k$ and one $\tau$ value, so the global
lists may be omitted if every source sets its own. See
[Source settings](#source-settings) for an example.

Solution directory names usually write tau with six decimal places. Values are
compared numerically, so the configured value `0.32` matches the directory
token `0.320000` (and also `0.32`):

```text
0.32 -> 0.320000
0.49 -> 0.490000
0.68 -> 0.680000
```

### Source settings

Each source supports the following fields:

| Field | Required | Description |
|---|---:|---|
| `id` | Yes | Stable source identifier used in URLs and solution names. |
| `label` | No | Human-readable name displayed in the browser. |
| `file` | No | Filename below `data/input`; defaults to `<id>.geojson`. |
| `layer` | No | Layer to read from a multi-layer source file. |
| `parameters` | No | Own `k_values` and/or `tau_values` for this source; see [Parameter settings](#parameter-settings). |

Source IDs may contain letters, numbers, `.`, `_`, and `-`. They may not
contain directory separators.

Supported source filename extensions are:

- `.gpkg`
- `.geojson`
- `.json`

A source must:

- Have a defined coordinate reference system.
- Contain at least one feature.
- Contain only `Polygon` or `MultiPolygon` geometries.
- Contain no null or empty geometries.

If a GeoPackage contains several layers, set the optional `layer` field.

Example:

```yaml
sources:
  - id: "city-centre"
    label: "City Centre"
    file: "regional-data.gpkg"
    layer: "buildings"

    # Optional; replaces the global k values for this source. The tau
    # values still come from the global "parameters".
    parameters:
      k_values: ["16", "100"]
```

A source found through `discovery.sources` has no entry of its own, so the
global `parameters` are its configured values. With `discovery.solutions`
enabled, the $k$ and $\tau$ values found in its solution names are offered as
well, as for every other source.

### Algorithm settings

Each algorithm supports:

| Field | Required | Description |
|---|---:|---|
| `id` | Yes | Stable identifier used by the API and statistics table. |
| `label` | No | Human-readable browser label; defaults to the algorithm ID. |
| `short_label` | No | Shorter name for the statistics table, which shows the full `label` on hover; defaults to `label`. |
| `directory` | No | Directory below `data/output`; defaults to the algorithm ID. |

Algorithm IDs and directory names may contain letters, numbers, `.`, `_`, and
`-`.

Example:

```yaml
algorithms:
  - id: "GISCupBonn_eSA"
    label: "Extended Simulated Annealing (Bonn)"
    short_label: "eSA (Bonn)"
    directory: "GISCupBonn_eSA"
```

## Input-data layout

Source files are stored immediately below `data/input`:

```text
data/input/
└── GIS-cup-competition-dataset.geojson
```

For a source listed under `sources`, the filename must match its `file`
setting in `config.yaml`. With `discovery.sources`, every other supported file
in `data/input` becomes a source as well.

## Solution directory layout and naming convention

Solutions are expected under:

```text
data/output/<algorithm-directory>/visualizations/
```

Here, `<algorithm-directory>` is the algorithm's `directory`, or its `id` when
no separate directory is configured. The solutions of all sources share this
directory; the source each solution belongs to is part of its name.

A solution directory must use one of these forms:

```text
<source-id>_par_<k>_<tau>.solution.visualization
```

or:

```text
<source-id>_par_<k>_<tau>_<run-metadata>.solution.visualization
```

`<tau>` is a number between 0 and 1, usually written with six decimal places
(`0.320000`); other notations of the same value (`0.32`) work as well.

For example:

```text
GIS-cup-competition-dataset_par_49_0.320000.solution.visualization
```

and:

```text
GIS-cup-competition-dataset_par_49_0.320000_id_p15_t08_15_20_49_57_447_r_6_val_4054.solution.visualization
```

The name must start with the ID of a source (configured or discovered)
followed by `_par_`; other directories are ignored. New solution directories
are picked up within about ten seconds, without a restart.

Without `discovery.solutions`, the `k` token must match a `k_values` entry
configured for the source (its own or the global one), and the tau token must
have the value of such a `tau_values` entry.

If several matching run directories exist for the same source, algorithm,
$k$, and $\tau$, the application selects the best run: the one with the most
covered buildings, given by `_val_<n>` in the directory name. Runs without a
`_val_` value rank last; among runs with the same value, the most recently
modified directory wins.

The statistics table marks parameter combinations for which several runs were
found and reports the selected run. When such a solution is displayed, the
status panel shows a warning. Only the selected run is cached and displayed.

### Required solution files

Every complete solution directory must contain these five files:

| Filename | Expected content |
|---|---|
| `antenna_positions.gpkg` | Antenna point geometries. |
| `union_edges.gpkg` | Visible-edge line geometries. |
| `union_visibility.gpkg` | Visibility geometries. |
| `covered_polygons_with_coverage.gpkg` | Covered building polygons. |
| `uncovered_polygons_with_coverage.gpkg` | Uncovered building polygons. |

The two coverage files must contain a `coverage` attribute. The backend uses
this value when it precomputes representative label points.

Every solution GeoPackage must:

- Have a defined coordinate reference system.
- Contain the intended data in its default or only layer.
- Be readable by Pyogrio/GDAL.
- Use geometry types appropriate for the layer.

A complete example is:

```text
data/output/
└── bonn-sa/
    └── visualizations/
        └── GIS-cup-competition-dataset_par_49_0.320000_run-001.solution.visualization/
            ├── antenna_positions.gpkg
            ├── covered_polygons_with_coverage.gpkg
            ├── uncovered_polygons_with_coverage.gpkg
            ├── union_edges.gpkg
            └── union_visibility.gpkg
```

## Cache design

The application creates these directories automatically:

```text
data/cache/
├── input/
├── output/
└── .locks/
```

### Chunks

During preprocessing, every layer is transformed to `EPSG:4326`, coordinates
are rounded to seven decimal places (about one centimetre), and the features
are grouped into fixed spatial chunks:

- The features are split recursively along the longer side of their extent,
  at the position where half of the data lies on each side, until every chunk
  is no larger than the configured limit. Chunks therefore follow the data
  density: they are small in dense areas and large in sparse ones.
- Every chunk is a gzip-compressed GeoJSON `FeatureCollection` stored as
  `chunks/<layer>/<chunk-id>.geojson.gz`. Every feature belongs to exactly one
  chunk.
- A single line that is larger than the limit is cut into several lines.
- The visibility layer is stored as fill-only polygons plus their outlines as
  separate lines. A visibility polygon that is larger than the limit is cut
  into pieces; because the outline is drawn separately, the cuts are
  invisible on the map.

Only the attributes the map needs are kept: edges keep `status`, and coverage
labels keep `coverage` and `status`.

### Source caches

A source cache is stored under:

```text
data/cache/input/<source-id>/
├── manifest.json
└── chunks/
    └── source/
```

### Solution caches

A solution cache is stored under:

```text
data/cache/output/
└── <algorithm-directory>/
    └── <complete-solution-directory-name>/
        ├── manifest.json
        └── chunks/
            ├── antennas/
            ├── covered/
            ├── uncovered/
            ├── edges/
            ├── visibility/
            └── coverage_labels/
```

The `coverage_labels` layer is generated during preprocessing. Label points
are created with `representative_point()` in the source coordinate system
before being transformed to `EPSG:4326`, which keeps them inside their
polygons.

Edges are classified during preprocessing: edge parts that run along the
boundary of an uncovered building get `status: "uncovered"`, all others
`status: "covered"`.

### Manifest and cache validation

Every cache contains a `manifest.json` file. It records:

- The cache-format version.
- The fingerprint of the inputs: original file paths, sizes, and modification
  timestamps, and the chunk settings.
- The chunk index: for every layer and chunk its ID, bounding box, feature
  count, and size.
- A content version, a hash over all chunk contents. It is part of every
  chunk URL.
- Generation time, feature counts, and solution metadata.

A cache is rebuilt when:

- It does not exist.
- Its manifest is missing or invalid.
- The cache-format version changes.
- An original file's size or modification time changes.
- The chunk settings change.
- `--force-precompute` is used.

Cache generation writes to a temporary directory, which then replaces the
previous cache. Thread locks and file locks prevent simultaneous requests or
processes from generating the same cache concurrently. On Linux and macOS the
file lock is released automatically when a process ends, even after a crash.

Do not edit files below `data/cache` manually.

### Clearing the cache

Stop the server and remove the contents of `data/cache`, but keep the
directory and its `.gitkeep`. (If the directory is missing and a Docker setup is used, Docker recreates it
as root, and the application can no longer write to it.)

```bash
find data/cache -mindepth 1 ! -name .gitkeep -delete
```

On Windows PowerShell:

```powershell
Get-ChildItem data\cache -Force -Exclude .gitkeep |
    Remove-Item -Recurse -Force
```

The cache directories and files will be recreated automatically.

## Performance behavior

The browser loads map data in the server's precomputed chunks:

1. For the current map view (padded by a quarter of its size), the browser
   requests the chunk index of each displayed dataset: the source, the
   primary solution, and the comparison solution when it is open.
2. The server lists the chunks that intersect the view, nearest to the map
   centre first.
3. The browser fetches these chunks in that order, four at a time, and draws
   each one as soon as it arrives. Chunks of the comparison solution follow
   those of the primary solution.
4. Once the view is complete, the browser preloads all remaining chunks of
   the displayed layers in the background, again nearest to the map centre
   first. Coverage labels and visibility are only preloaded while they are
   shown. Preloaded chunks are drawn as soon as the map reaches them, without
   a network request.

Further details:

- Chunk URLs contain the content version and never change their content, so
  the browser caches them permanently. A reload or a return to a previously
  viewed solution is served from the browser cache.
- Chunks are shared between the primary and the comparison solution: showing
  the same solution in both roles downloads it only once.
- Background requests never delay the current view: they use a single
  connection and only start while no request for the current view is waiting.
- Up to about 150 MB of chunk data are kept in memory; the least recently used
  chunks are dropped first and reloaded from the browser cache when needed.
- Chunks far outside the current view are removed from the map, which keeps
  panning and zooming fast. Chunks near the view stay on the map, so small pans
  do not reload anything.
- Map layers are built in small steps between animation frames, so the map
  stays responsive while data arrives.
- Already displayed data stays visible while further chunks load. Closing the
  comparison keeps its data, so reopening it is fast.

For best production performance:

1. Run `python app.py --precompute-only` during deployment.
2. Persist `data/cache` between (routine) application restarts.
3. Store source, output, and cache data on fast local storage.
4. Ensure the application user has write access to `data/cache`.
5. Put an HTTPS reverse proxy in front of Uvicorn.

The first request for an uncached solution still waits until its whole cache has
been generated, which mainly consists of the edge classification. Other requests
and already displayed map content remain usable meanwhile.

## HTTP API

Interactive documentation is available at:

- `/docs`
- `/redoc`

Both describe every parameter and show example requests and responses for the
competition instance with the solutions published on Zenodo (see
[Quick start](#quick-start)). In `/docs`, "Try it out" fills in the example
values; the chunk URLs, however, contain cache versions that differ from
installation to installation, so take them from a chunk index.

### `GET /api/health`

Reports whether the service is running and whether startup cache
precomputation is active.

### `GET /api/config`

Returns the public application, source, algorithm, and map configuration.
Each source carries the $k$ and $\tau$ values configured for it.

### `GET /api/source/{source_id}/extent`

Returns the source extent in `EPSG:4326` and its feature count.

### `GET /api/source/{source_id}/statistics`

Returns:

- The total source polygon count.
- Discovered parameter combinations.
- Covered-polygon counts.
- Available algorithms.
- Incomplete-solution information.
- The number of matching runs.
- `catalog_version`, the version of the solution directories (see below).

### `GET /api/solutions/version`

Returns `catalog_version`, a short hash that changes whenever a solution
directory is added, removed, or renamed. The statistics and all chunk indexes
include it as well. When the browser sees a version different from that of its
statistics, it reloads the statistics panel and the $k$/$\tau$ choices in the
background. It checks after every chunk index request and when a selector is
opened.

### `GET /api/source/{source_id}/chunks`

Returns the chunk index of the source buildings.

Optional query parameters:

```text
bbox=minLon,minLat,maxLon,maxLat
center=lon,lat
```

Only chunks intersecting `bbox` are listed; without `bbox`, all chunks are
listed. Chunks are sorted by their distance from `center` (default: the centre
of `bbox`), nearest first.

The response has this form:

```json
{
  "version": "a91d6294122168ea3d9c",
  "chunk_url_prefix": "/api/chunk/source/GIS-cup-competition-dataset/a91d6294122168ea3d9c",
  "chunks": [
    {
      "layer": "source",
      "id": "010010",
      "bbox": [-117.392287, 33.9682141, -117.3689974, 33.9837582],
      "count": 1571,
      "bytes": 417482
    }
  ],
  "catalog_version": "0be9d2918afaaa23"
}
```

The URL of a chunk is `<chunk_url_prefix>/<layer>/<id>`. `bytes` is the
uncompressed size.

### `GET /api/solution/chunks`

Returns the chunk index of a solution, in the same form.

Required query parameters:

- `source`
- `algorithm`
- `k`
- `tau`

Optional query parameters:

- `layers`: comma-separated layer names; default: all layers.
- `bbox` and `center`, as above.

Allowed layer values are:

- `antennas`
- `covered`
- `uncovered`
- `edges`
- `visibility`
- `coverage_labels`

When several chunks are equally close to the centre, they are listed in this
order: antennas, covered and uncovered buildings, edges, coverage labels,
visibility.

Example:

```text
/api/solution/chunks?source=GIS-cup-competition-dataset&algorithm=GISCupBonn_eSA&k=9&tau=0.32&layers=covered,uncovered&bbox=-117.38,33.97,-117.37,33.98&center=-117.375,33.975
```

### `GET /api/chunk/...`

Returns one chunk as a GeoJSON `FeatureCollection`. Chunk URLs are taken from a
chunk index and should not be constructed by hand.

The chunk is sent gzip-compressed when the client accepts it, and may be cached
permanently. A chunk of an outdated version returns `404`; request a fresh
chunk index in that case.

## Browser behavior

The top toolbar selects the source and primary solution. Comparison mode adds a
second independently selectable algorithm, $k$, and $\tau$ combination.

The status box at the top right shows the loading state, warnings, and errors.
Longer messages can be scrolled inside it, and clicking the box copies the
message.

The source buildings are drawn while no solution is selected. Once a solution
is selected, its covered and uncovered buildings replace them.

Visible building edges are always drawn. Edges on uncovered buildings are
highlighted in the uncovered colour and drawn thinner than edges on covered
buildings. Edge lines become thinner when zoomed out and bolder when zoomed in.
The "Visibility" button additionally shows the visibility areas and, at high
zoom levels, the coverage labels.

The solution-fade slider changes the relative opacity of primary and
comparison layers: its left end shows only the primary solution, its right end
only the comparison solution. Comparison edges are displayed as solid lines.

An antenna that both solutions place at the same position is shown as one
marker, split into the comparison colour on the left and the primary colour on
the right. The border between the colours moves with the fade slider's thumb,
so at either end of the slider the marker shows only one solution's colour.

Coverage labels are shown at their representative points while "Visibility" is
on and the zoom level reaches `map.coverage_label_min_zoom`.
In comparison mode, primary labels are shifted upward and comparison labels
downward so that labels at the same point remain readable. The offset applies
at every slider position, so the labels do not move when the slider does.

The statistics table highlights:

- The primary solution in blue.
- The comparison solution in violet.
- A solution used for both roles with a split blue/violet background.

Click an available count in the statistics table to set that algorithm,
$k$, and $\tau$ combination as the primary solution. Right-click it to set it
as the comparison solution; right-clicking the current comparison solution again
turns comparison mode off.

## Troubleshooting

### No matching solution directory was found

Check all of the following:

- The source ID at the beginning of the directory name is correct.
- The solution is in `data/output/<algorithm-directory>/visualizations/`.
- Without `discovery.solutions`: the `k` value exactly matches a configured
  value, and tau has the value of a configured one.
- The directory ends in `.solution.visualization`.

The error message shows the expected name with tau in six decimal places, for
example `0.320000` for tau `0.32`; other notations of the same value match as
well.

### A solution is reported as incomplete

Verify that all five required GeoPackage files exist and use the exact expected
filenames.

### A file has no coordinate reference system

Assign the correct CRS to the original source or solution file. The application
cannot safely transform a layer when its source CRS is unknown.

### Coverage labels cannot be generated

Verify that both of these files contain a `coverage` column:

```text
covered_polygons_with_coverage.gpkg
uncovered_polygons_with_coverage.gpkg
```

### Cache generation fails with a permissions error

Ensure the application process can create, replace, and delete files below:

```text
data/cache
```

With Docker, the directory must belong to `APP_UID`/`APP_GID` (see
`deploy/.env`). If it was created by Docker, it belongs to root; fix this
once on the host:

```bash
sudo chown -R 1000:1000 data/cache   # or the values of APP_UID:APP_GID
```

### Cache generation appears locked

Cache locks are stored below:

```text
data/cache/.locks
```

On Linux and macOS, a lock is released automatically when the process holding
it ends, so the lock files can stay in place.

On Windows, a lock file left behind by a terminated process is considered stale
after two hours and removed automatically. If no application process is still
running, it may also be removed manually.

### The basemap does not load

The default setup loads map tiles from OpenStreetMap. Verify network access or
configure another `map.tile_url`. The map data itself does not depend on the
basemap.

### Changes to data are not visible

Caches normally invalidate from source-file size and modification timestamps.
If files were replaced while preserving both values, force a rebuild:

```bash
python app.py --precompute-only --force-precompute
```

A full reset can be performed by clearing `data/cache`; see
[Clearing the cache](#clearing-the-cache).

## Development checks

Compile-check the Python source:

```bash
python -m compileall app.py
```

Start the development server with debug logging:

```bash
python app.py --log-level debug
```

Check the health endpoint:

```bash
curl http://127.0.0.1:8000/api/health
```

Check the public configuration:

```bash
curl http://127.0.0.1:8000/api/config
```
