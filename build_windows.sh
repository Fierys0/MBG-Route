#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================================="
echo " Building MBG for Windows (x86_64-w64-mingw32)"
echo " Dynamic Library: fumbo.dll"
echo " Asset Package:   data.fpk"
echo " Executable:      MBG.exe"
echo "=========================================================="

mkdir -p build_win
cd build_win

cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-mingw64.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      ..

make -j$(nproc)

# Ensure data.fpk is generated using host pack_assets if wine wasn't used
if [ ! -f "bin/data.fpk" ] && [ -f "../build/bin/pack_assets" ]; then
    echo "Generating data.fpk using host packer..."
    ../build/bin/pack_assets ../assets bin/data.fpk
fi

echo ""
echo "=========================================================="
echo " Windows Build Completed Successfully!"
echo " Output files in build_win/bin/:"
echo "=========================================================="
ls -lh bin/
