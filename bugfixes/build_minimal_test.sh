#!/bin/bash
# Build the minimal Free DB test

set -e

echo "Building minimal Free DB test..."

gcc -g -O0 \
    -I./src \
    -I/usr/include/apr-1.0 \
    -DLINUX -D_REENTRANT -D_GNU_SOURCE \
    -DPAGE_SIZE=4096 \
    test_free_db_minimal.c \
    src/napr_db.c \
    src/napr_db_tree.c \
    src/napr_db_cursor.c \
    src/vendor/xxhash/xxhash.c \
    -L/usr/lib/x86_64-linux-gnu \
    -lapr-1 \
    -laprutil-1 \
    -o test_free_db_minimal

echo "Build successful! Run with: ./test_free_db_minimal"
