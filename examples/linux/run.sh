#!/usr/bin/env bash
# Usage:
#   ./run.sh              — normal build + run
#   ./run.sh --validation — enable Vulkan validation layers
#                           (requires: sudo apt install vulkan-validationlayers)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/../.."
BUILD_DIR="$PROJECT_ROOT/build"

VALIDATION_FLAG="OFF"
for arg in "$@"; do
    case "$arg" in
        --validation|-v) VALIDATION_FLAG="ON" ;;
    esac
done

cmake -B "$BUILD_DIR" "$PROJECT_ROOT" \
    -DBUILD_EXAMPLES=ON \
    -DCAMPELLO_GPU_VALIDATION="$VALIDATION_FLAG"
make -C "$BUILD_DIR" campello_linux_example

exec "$BUILD_DIR/examples/linux/campello_linux_example"
