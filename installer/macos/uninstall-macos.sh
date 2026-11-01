#!/bin/sh
# Removes the MultiPaw Compressor plugin installed by the package.
set -e
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/MultiPaw Compressor.vst3"
sudo pkgutil --forget com.studio87.multipawcompressor.vst3 >/dev/null 2>&1 || true
echo "Removed the plugin."
