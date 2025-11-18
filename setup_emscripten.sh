#!/bin/bash
# Install Emscripten for KMA WASM build

set -e

echo "=== Installing Emscripten SDK ==="
echo ""

# Check if emsdk already exists
if [ -d "emsdk" ]; then
    echo "emsdk directory already exists. Updating..."
    cd emsdk
    git pull
else
    echo "Cloning Emscripten SDK..."
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk
fi

echo ""
echo "Installing latest Emscripten..."
./emsdk install latest

echo ""
echo "Activating Emscripten..."
./emsdk activate latest

echo ""
echo "=== Installation Complete! ==="
echo ""
echo "To use Emscripten, run this command in your terminal:"
echo ""
echo "    source $(pwd)/emsdk_env.sh"
echo ""
echo "Then you can build KMA with:"
echo "    cd kma"
echo "    ./build_wasm.sh"
echo ""
