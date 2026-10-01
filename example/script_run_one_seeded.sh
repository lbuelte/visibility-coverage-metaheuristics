#!/bin/bash

# ---- Run configuration ----

RUN_NAME="SA-LS-BaseRun"

EXEC="./build/GisCupBonn_RunOneSeededParallel"
INPUT_FILE="data/input/GIS-cup-competition-dataset.geojson"
OUTPUT_DIR="data/output/runone"

# Timelimit in minutes.
TIMELIMIT=600

# Number of parallel threads, each runs a different seed.
THREADS=4

# Problem parameters.
TAU="0.68"
K="49"

# Algorithm configuration.
ALGORITHM="simann-ls-finish" 
SA_CONFIG="configs/simann_base_config.toml"
LS_CONFIG="configs/local_search_config.toml"

# ---- Print configuration ----

echo "========================================"
echo "Starting $RUN_NAME"
echo "EXECUTABLE:    $EXEC"
echo "INPUT_FILE:    $INPUT_FILE"
echo "OUTPUT_DIR:    $OUTPUT_DIR"
echo "TIMELIMIT:     $TIMELIMIT"
echo "THREADS:       $THREADS"
echo "TAU:           $TAU"
echo "K:             $K"
echo "ALGORITHM:     $ALGORITHM"
echo "SA_CONFIG:     $SA_CONFIG"
echo "LS CONFIG:     $LS_CONFIG"
echo "========================================"

# ---- Run ----

"$EXEC" \
    -i "$INPUT_FILE" \
    -o "$OUTPUT_DIR" \
    -z "$TIMELIMIT" \
    -p "$THREADS" \
    -t "$TAU" \
    -k "$K" \
    -a "$ALGORITHM" \
    --sa-f "$SA_CONFIG" \
    --ls-f "$LS_CONFIG"

echo "Finished $RUN_NAME."