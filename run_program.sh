#!/bin/bash

# check if the directory argument is provided
if [ $# -ne 1 ]; then
    echo "Usage: ./run_program.sh <directory>"
    exit 1
fi

# assign the first argument as the directory
directory="$1"

# check if the directory exists
if [ ! -d "$directory" ]; then
    echo "Error: $directory does not exist."
    exit 1
fi

# prepare a list of all .ppm files in the directory
files=("$directory"/*.ppm)

# check if any .ppm files were found
if [ ${#files[@]} -eq 0 ]; then
    echo "No .ppm files found in the directory."
    exit 1
fi

gcc -o edge_detector edge_detector.c
./edge_detector "${files[@]}"