#!/bin/sh
# Installs MultiPaw Compressor for your user only (no admin rights needed).
# The VST3 goes to ~/.vst3, the folder most Linux hosts scan. Set VST3_DIR to use another folder.
set -e
here=$(cd "$(dirname "$0")" && pwd)
dest="${VST3_DIR:-$HOME/.vst3}"
if [ ! -d "$here/MultiPaw Compressor.vst3" ]; then
  echo "MultiPaw Compressor.vst3 was not found next to this script." >&2
  exit 1
fi
mkdir -p "$dest"
rm -rf "$dest/MultiPaw Compressor.vst3"
cp -R "$here/MultiPaw Compressor.vst3" "$dest/"
echo "Installed: $dest/MultiPaw Compressor.vst3"
echo "Rescan plugins in your host. To remove it later, run uninstall.sh."
