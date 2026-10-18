# syntax=docker/dockerfile:1

# GIS CUP 2026 Solution Viewer
#
# The data directories are not part of the image; mount them at runtime:
#   /app/data/input    source datasets (read-only is sufficient)
#   /app/data/output   solution directories (read-only is sufficient)
#   /app/data/cache    generated caches (must be writable)
# Optionally mount a different /app/config.yaml.

FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

WORKDIR /app

# The geospatial packages ship wheels that include GDAL, GEOS, and PROJ,
# so no system libraries are needed.
COPY requirements.txt .
RUN pip install --requirement requirements.txt

COPY app.py config.yaml ./
COPY static ./static
COPY templates ./templates

# Run as an unprivileged user. The cache directory must be writable by
# this user; with bind mounts, see APP_UID/APP_GID in deploy/compose.yaml.
RUN useradd --uid 1000 --create-home viewer \
    && mkdir -p data/input data/output data/cache \
    && chown -R viewer:viewer data/cache

USER viewer

# Build missing or outdated caches in the background after startup.
ENV GIS_VIEWER_STARTUP_PRECOMPUTE=1

EXPOSE 8000

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD ["python", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8000/api/health', timeout=4)"]

# One worker: cache builds are serialised by locks anyway, and the startup
# precomputation should run only once.
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8000"]
