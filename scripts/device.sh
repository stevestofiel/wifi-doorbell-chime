#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICES_FILE="${ROOT_DIR}/devices.json"

usage() {
  cat <<'USAGE'
Usage: scripts/device.sh <command> [device]

Commands:
  list              List detected ESP32 USB devices and known labels.
  port <device>     Print the current serial port for a known device.
  upload <device>   Compile and upload firmware to a known device.

Known devices are configured in devices.json by stable Espressif USB
hardware_id, not by changing /dev/cu.usbmodem* port names.
USAGE
}

python_device() {
  python3 - "$@" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
devices_file = Path(sys.argv[2])
command = sys.argv[3]
device_name = sys.argv[4] if len(sys.argv) > 4 else ""

with devices_file.open("r", encoding="utf-8") as f:
    registry = json.load(f).get("devices", {})

board_json = subprocess.check_output(
    ["arduino-cli", "board", "list", "--format", "json"],
    cwd=root,
    text=True,
)
detected = json.loads(board_json).get("detected_ports", [])

known_by_id = {
    info.get("hardware_id", "").upper(): name
    for name, info in registry.items()
}

esp_ports = []
for item in detected:
    port = item.get("port", {})
    props = port.get("properties", {})
    hardware_id = (
        port.get("hardware_id")
        or props.get("serialNumber")
        or ""
    ).upper()
    address = port.get("address", "")
    if not address.startswith("/dev/cu.usbmodem"):
        continue
    esp_ports.append({
        "address": address,
        "hardware_id": hardware_id,
        "label": known_by_id.get(hardware_id, ""),
    })

def find_device(name):
    if name not in registry:
        known = ", ".join(sorted(registry)) or "(none)"
        raise SystemExit(f"Unknown device '{name}'. Known devices: {known}")
    expected = registry[name].get("hardware_id", "").upper()
    matches = [p for p in esp_ports if p["hardware_id"] == expected]
    if not matches:
        print(f"Device '{name}' is not connected.")
        print(f"Expected hardware_id: {expected}")
        if esp_ports:
            print("Detected ESP32 devices:")
            for p in esp_ports:
                label = f" ({p['label']})" if p["label"] else ""
                print(f"  {p['address']}  {p['hardware_id']}{label}")
        raise SystemExit(1)
    if len(matches) > 1:
        raise SystemExit(f"Multiple ports matched '{name}', refusing to guess.")
    return registry[name], matches[0]

if command == "list":
    if not esp_ports:
        print("No ESP32 USB devices detected.")
    for p in esp_ports:
        label = p["label"] or "unknown"
        print(f"{p['address']}\t{p['hardware_id']}\t{label}")
elif command == "port":
    _, match = find_device(device_name)
    print(match["address"])
elif command == "upload":
    info, match = find_device(device_name)
    compile_target = info.get("role", device_name)
    if compile_target == "wifi_radar":
        build_dir = root / ".arduino" / "build" / "wifi_radar"
    elif compile_target == "wifi_button_touch":
        build_dir = root / ".arduino" / "build" / "wifi_button_touch"
    elif compile_target == "chime":
        build_dir = root / ".arduino" / "build" / "chime"
    else:
        raise SystemExit(f"Device '{device_name}' has unsupported role '{info.get('role')}'.")

    subprocess.check_call([
        "arduino-cli",
        "compile",
        "--clean",
        "--build-path",
        str(build_dir),
        "--fqbn",
        info["fqbn"],
        "--upload",
        "-p",
        match["address"],
        str(root / info["sketch"]),
    ], cwd=root)
else:
    raise SystemExit(f"Unknown command '{command}'")
PY
}

command="${1:-}"
device="${2:-}"

case "${command}" in
  list)
    python_device "${ROOT_DIR}" "${DEVICES_FILE}" list
    ;;
  port|upload)
    if [[ -z "${device}" ]]; then
      usage >&2
      exit 2
    fi
    python_device "${ROOT_DIR}" "${DEVICES_FILE}" "${command}" "${device}"
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
