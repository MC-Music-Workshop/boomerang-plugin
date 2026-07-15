#!/bin/bash
# Boomerang Plugin Build Script
# Builds universal binary (Intel + Apple Silicon) for macOS 11.0+

set -e  # Exit on error

echo "Cleaning build directory..."
rm -rf build

echo "Configuring CMake..."
cmake -S . -B build \
  -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13

echo "Building with 8 parallel jobs..."
cmake --build build -j8 2>&1 | grep -v "Git hash"

echo ""
echo "Build complete!"

# Show git hash at the end
GIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
echo "Git hash: ${GIT_HASH}"

echo "Artifacts location: build/Boomerang_artefacts/"
echo ""
echo "Verifying architectures:"
lipo -info build/Boomerang_artefacts/Standalone/Turrama.app/Contents/MacOS/Turrama
lipo -info build/Boomerang_artefacts/VST3/Turrama.vst3/Contents/MacOS/Turrama
lipo -info build/Boomerang_artefacts/AU/Turrama.component/Contents/MacOS/Turrama
