#!/bin/bash

# ---- Run configuration ----

RUN_NAME="Competition-BaseRun"

EXEC="./build/GisCupBonn_FinalSeededCrossproduct"
PARAMS="competition_params.toml"
OUTPUT_DIR="data/output/final"

# Deadline for the runs. Will be converted to timelimit in minutes from now (should be same timezone as compute server).
DEADLINE="2026-08-16 16:00:00"

# Number of parallel threads. 
# The program will always run full crossproducts (of size 9 for the competition parameters).
# (THREADS - (THREADS mod 9)) / 9 crossproducts will be run, each with a different seed.
THREADS=27

# Algorithm configuration.
ALGORITHM="simann-ls-finish" 
SA_CONFIG="configs/simann_base_config.toml"
LS_CONFIG="configs/local_search_config.toml"

# ---- Print configuration ----

echo "========================================"
echo "Starting $RUN_NAME"
echo "PARAMS:        $PARAMS"
echo "DEADLINE:      $DEADLINE"
echo "OUTPUT_DIR:    $OUTPUT_DIR"
echo "THREADS:       $THREADS"
echo "ALGORITHM:     $ALGORITHM"
echo "SA_CONFIG:     $SA_CONFIG"
echo "LS CONFIG:     $LS_CONFIG"
echo "========================================"

# ---- Run ----

"$EXEC" \
    -f "$PARAMS" \
    -d "$DEADLINE" \
    -o "$OUTPUT_DIR" \
    -p "$THREADS" \
    -a "$ALGORITHM" \
    --sa-f "$SA_CONFIG"

echo "Finished $RUN_NAME."