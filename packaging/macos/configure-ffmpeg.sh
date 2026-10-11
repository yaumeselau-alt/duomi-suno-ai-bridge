#!/bin/bash
set -euo pipefail
if [[ "$(uname -s)" != Darwin ]]; then echo "Run this on macOS." >&2; exit 1; fi
decoder="${1:-}"
if [[ "$decoder" != /* || ! -f "$decoder" || ! -x "$decoder" ]]; then
  echo "Usage: bash packaging/macos/configure-ffmpeg.sh /absolute/path/to/ffmpeg" >&2
  exit 1
fi
# JUCE userApplicationDataDirectory is ~/Library on macOS.
config="$HOME/Library/DuomiSunoAIBridge"
mkdir -p "$config"
printf '%s\n' "$decoder" > "$config/ffmpeg-path.txt"
echo "FFmpeg path saved. Select the stem again in the plugin."
