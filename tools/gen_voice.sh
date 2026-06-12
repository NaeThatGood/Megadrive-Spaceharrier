#!/bin/sh
# Generate the original placeholder "Get ready!" voice sample (macOS only:
# uses the system speech synthesiser, so the result is copyright-clean).
# Output: res/audio/getready.wav (mono 16-bit, 16 kHz; rescomp converts it
# to the XGM2 driver format at build time).
#
# To use your own recording instead, just replace res/audio/getready.wav
# with any mono 16-bit PCM WAV file and rebuild.

set -e
cd "$(dirname "$0")/.."
mkdir -p res/audio

say -v Daniel -o /tmp/getready.aiff "Get ready!"
ffmpeg -y -loglevel error -i /tmp/getready.aiff -ar 16000 -ac 1 \
    -sample_fmt s16 res/audio/getready.wav
rm -f /tmp/getready.aiff

echo "wrote res/audio/getready.wav"
