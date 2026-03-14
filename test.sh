#!/bin/sh
set -e

BUILD_DIR="build_test"

cmake -B "$BUILD_DIR" -DBUILD_TESTS=ON
make -C "$BUILD_DIR" campello_gpu_universal_tests
ctest --test-dir "$BUILD_DIR" --output-on-failure
