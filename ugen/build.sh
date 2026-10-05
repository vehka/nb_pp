#!/bin/sh
# Build the PlaitsPalette UGen and, with "install", copy it where scsynth
# looks for plugins. Restart norns (or SuperCollider) afterwards.
#
#   ugen/build.sh            build, then run the host test
#   ugen/build.sh install    build and install
#
# SC_PATH=<supercollider source tree> if the plugin headers are not installed.
set -e
cd "$(dirname "$0")"

git -C .. submodule update --init ugen/eurorack
git -C eurorack submodule update --init stmlib

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${SC_PATH:+-DSC_PATH="$SC_PATH"}
cmake --build build -j"$(nproc)"
./build/pp_host_test | tail -n 3

if [ "$1" = "install" ]; then
  dest="$HOME/.local/share/SuperCollider/Extensions/nb_pp"
  mkdir -p "$dest"
  cp build/PlaitsPalette.so "$dest/"
  echo "installed $dest/PlaitsPalette.so"
fi
