#!/bin/bash
#
# Helper script to run cppcheck locally before CI/CD catches issues
# This matches the CI/CD configuration for consistency
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "Running cppcheck analysis..."
echo "Project root: $PROJECT_ROOT"
echo ""

# Check if podman is available
if command -v podman &> /dev/null; then
    CONTAINER_CMD="podman"
elif command -v docker &> /dev/null; then
    CONTAINER_CMD="docker"
else
    echo "Error: Neither podman nor docker found. Please install one of them."
    exit 1
fi

echo "Using container command: $CONTAINER_CMD"
echo ""

# Run cppcheck with the same parameters as CI/CD
$CONTAINER_CMD run --rm \
    -v "$(pwd)":/workspace \
    -w /workspace \
    docker.io/facthunder/cppcheck \
    cppcheck \
        --enable=all \
        --language=c \
        --std=c11 \
        --platform=unix64 \
        --inconclusive \
        --suppressions-list=.cppcheck-suppressions \
        -i third-party \
        .

echo ""
echo "cppcheck analysis complete!"
