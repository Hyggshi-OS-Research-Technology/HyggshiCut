#!/usr/bin/env bash
set -e

# Change to repository root
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=========================================================="
echo "  HyggshiCut - Building .deb inside Docker container"
echo "=========================================================="

mkdir -p "$DIR/dist"

# Build builder image
docker build -t hyggshicut:builder --target builder .

# Run container to package .deb and output into ./dist/
docker run --rm \
    -v "$DIR/dist:/dist_out" \
    hyggshicut:builder \
    sh -c "dpkg-buildpackage -us -uc -b -j\$(nproc) && cp ../hyggshicut_*.deb /dist_out/ && ls -lh /dist_out/"

echo ""
 echo "Build completed successfully! Packages generated in: $DIR/dist/"
