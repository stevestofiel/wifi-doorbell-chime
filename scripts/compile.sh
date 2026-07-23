#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_DIR="${ROOT_DIR}/.arduino"

compile_chime() {
  arduino-cli compile \
    --build-path "${ARDUINO_DIR}/build/chime" \
    --fqbn esp32:esp32:esp32s3:PartitionScheme=no_ota \
    "${ROOT_DIR}"
}

compile_wifi_button_touch() {
  arduino-cli compile \
    --build-path "${ARDUINO_DIR}/build/wifi_button_touch" \
    --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc \
    "${ROOT_DIR}/sensors/wifi_button_touch"
}

compile_wifi_radar() {
  arduino-cli compile \
    --build-path "${ARDUINO_DIR}/build/wifi_radar" \
    --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc \
    "${ROOT_DIR}/sensors/wifi_radar"
}

usage() {
  cat <<'USAGE'
Usage: scripts/compile.sh <target>

Targets:
  chime
  wifi_button_touch
  wifi_radar
  all
USAGE
}

target="${1:-}"

mkdir -p "${ARDUINO_DIR}/build"

case "${target}" in
  chime)
    compile_chime
    ;;
  wifi_button_touch)
    compile_wifi_button_touch
    ;;
  wifi_radar)
    compile_wifi_radar
    ;;
  all)
    compile_chime
    compile_wifi_button_touch
    compile_wifi_radar
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
