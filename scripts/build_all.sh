#!/bin/bash

echo "Starting build process for Edge-Cloud Migration System..."

# Build Edge
echo "Building Edge components..."
cd edge && make && cd ..

# Build Cloud
echo "Building Cloud components..."
cd cloud && make && cd ..

# Build Kernel Module
echo "Building Kernel module (requires Linux headers)..."
if [ "$(uname)" == "Linux" ]; then
    cd kernel && make && cd ..
else
    echo "Warning: Not on Linux. Skipping kernel module build."
fi

echo "Build complete."
