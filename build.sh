#!/bin/bash -ex

BUILD_DIR="./build/"
SRC_FOLDER="./sqlite-amalgamation-3470000/"

# Need to build everything if `build` doesnt exist
if ! ls $BUILD_DIR > /dev/null 2>&1; then
    echo "Building sqlite3..."
    mkdir -p -v $BUILD_DIR
    gcc -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -I$SRC_FOLDER -o $BUILD_DIR"sqlite3.o" -c $SRC_FOLDER"sqlite3.c"
# Need to rebuild sqlite if it doesn't exist
elif ! ls $BUILD_DIR"sqlite3.o" > /dev/null 2>&1; then
    echo "Rebuilding sqlite3..."
    gcc -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -I$SRC_FOLDER -o $BUILD_DIR"sqlite3.o" -c $SRC_FOLDER"sqlite3.c"
fi

# Building lore
gcc -Wall -Wextra -ggdb -static -I$SRC_FOLDER -o $BUILD_DIR"lore" lore.c $BUILD_DIR"sqlite3.o"

