#!/usr/bin/env bash
# After compiling kmeans and serial, run: bash run_tests.sh
# magic.txt: UCI MAGIC Gamma Telescope, https://doi.org/10.24432/C52C8B
# Original numeric features retained; g/h class labels omitted.
set -e
cd -- "$(dirname -- "$0")"

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
