#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Building Docker container..."
docker build -t av1-encoder "$SCRIPT_DIR/docker"

echo "Running encoder pipeline in Docker..."
docker run --rm -it \
    -v "$SCRIPT_DIR:/workspace" \
    av1-encoder \
    bash /workspace/run.sh
