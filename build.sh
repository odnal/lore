#!/bin/bash -e

BUILD_DIR="./build/"
SRC_FOLDER="./sqlite-amalgamation-3470000/"

OUTPUT_PATH=$BUILD_DIR"sqlite3.o"
INPUT_PATH=$SRC_FOLDER"sqlite3.c"

# Need to build everything if `build` doesnt exist
if ! ls $BUILD_DIR > /dev/null 2>&1; then
    echo "Building sqlite3..."
    mkdir -p -v $BUILD_DIR
    gcc -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -I$SRC_FOLDER -o $OUTPUT_PATH -c $INPUT_PATH
# Need to rebuild sqlite if it doesn't exist
elif ! ls $BUILD_DIR"sqlite3.o" > /dev/null 2>&1; then
    echo "Rebuilding sqlite3..."
    gcc -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -I$SRC_FOLDER -o $OUTPUT_PATH -c $INPUT_PATH
fi

# Building lore
gcc -Wall -Wextra -ggdb -I$SRC_FOLDER -o lore lore.c

