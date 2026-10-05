#!/usr/bin/env bash
# After compiling kmeans and serial, run: bash run_tests.sh
# Grain-size sweep: bash run_tests.sh x [grain sizes...]
# magic.txt: UCI MAGIC Gamma Telescope, https://doi.org/10.24432/C52C8B
# Original numeric features retained; g/h class labels omitted.
set -euo pipefail
cd -- "$(dirname -- "$0")"

if [[ ${1:-} == x ]]; then
    shift
    if [[ ! -x ./kmeans ]]; then
        echo "Compile kmeans before running grain-size tests." >&2
        exit 1
    fi
    if [[ $# == 0 ]]; then
        set -- 64 128 256 512 1024 2048 4096
    fi
    for grain in "$@"; do
        if [[ ! $grain =~ ^[1-9][0-9]*$ || ${#grain} -gt 10 || $grain -gt 2147483647 ]]; then
            echo "Grain sizes must be positive integers up to 2147483647." >&2
            exit 1
        fi
    done
    mkdir -p results/grain
    for trial in 1 2 3 4 5; do
        for grain in "$@"; do
            echo "MAGIC: 8 threads, grain $grain, trial $trial of 5"
            ./kmeans 8 x "$grain" < datasets/magic.txt |
                grep -E '^(Break in iteration|TOTAL EXECUTION TIME|TIME PHASE)' \
                > "results/grain/magic-t8-g$grain-$trial.txt"
        done
    done
    echo "Finished. Timings are in results/grain/. Repeating a test replaces its timings."
    exit 0
fi
if [[ $# != 0 ]]; then
    echo "Usage: bash run_tests.sh [x grain_sizes...]" >&2
    exit 1
fi

if [[ ! -x ./serial || ! -x ./kmeans ]]; then
    echo "Compile serial and kmeans in pa1/part2 before running this script." >&2
    exit 1
fi

mkdir -p results
for dataset in dataset1 dataset2 magic; do
    for trial in 1 2 3 4 5; do
        echo "$dataset: trial $trial of 5"
        ./serial < "datasets/$dataset.txt" \
            > "results/$dataset-serial-$trial.txt"

        for threads in 1 2 4 8 16; do
            ./kmeans "$threads" < "datasets/$dataset.txt" \
                > "results/$dataset-t$threads-$trial.txt"
        done
    done
done
echo "Finished. Outputs are in part2/results/. Rerunning replaces these outputs."
