<!-- SPDX-License-Identifier: MIT -->

# AI Project Directives

This repo is an active Wi-Fi doorbell chime and remote sensor product project.
Treat both firmware and hardware documentation as product source, not examples.

## Work Style

- Inspect the repo and roadmap before making project changes.
- Work in small, focused branches and commits.
- Keep changes scoped to the current hardware or firmware milestone.
- Keep `docs/PRODUCT_ROADMAP.md` current whenever priorities, status, or next
  steps change.
- Compile touched Arduino sketches when possible before committing.
- Perform firmware uploads when requested; do not hand upload steps back to the
  user unless blocked by hardware access.
- Do not leave required physical validation implicit. Prompt the user to perform
  bench tests, wiring checks, upload tests, serial-log checks, and enclosure
  tests that Codex cannot perform.

## Branch And Commit Discipline

- Use `codex/` branch names by default.
- Prefer one commit per coherent iteration, such as one firmware refactor, one
  hardware doc update, or one sensor prototype.
- Merge or prepare for merge when a branch reaches a stable checkpoint and the
  working tree is clean.
- Avoid piling unrelated roadmap, UI, firmware, and hardware changes into one
  commit.

## Product Direction

- The root Arduino sketch is the Wi-Fi chime receiver firmware.
- `sensors/` contains first-class remote sensor firmware.
- `sensors/common/` contains shared remote sensor behavior.
- Remote sensors send semantic events to `/trigger` using `sensor`, `type`,
  `event`, and `eventId`.
- Chimes resolve those events locally through rules and defaults.

## Remote Sensor Hardware Assumptions

- Every remote sensor should include a required setup/test/service button.
- Every remote sensor should include a physically reachable user event-gain
  potentiometer.
- Default service button wiring: `GPIO3` to momentary button to `GND`, using
  `INPUT_PULLUP`.
- Boot hold should clear saved Wi-Fi/sensor settings and start the captive
  portal.
- Short press while running should send a test event.
- Keep the shared gain potentiometer's meaning consistent across sensor
  products. Use a service-button teach/calibration gesture or a separate
  product-specific trim for light, tilt, range, or similar thresholds.
- Battery-powered sensors should keep this button physically reachable and
  eventually use a wake-capable pin for sleep designs.

## Physical Testing Prompts

When a change affects hardware behavior, explicitly ask the user to verify:

- Pin wiring and power rails before upload.
- Serial logs for startup, setup gesture, input changes, and HTTP status.
- Chime event log entries after sensor triggers.
- False-trigger behavior at the intended mounting location.
- Battery, enclosure, reset-button, service-button, and gain-control access
  before calling hardware stable.
- Product-specific threshold calibration and repeatability where applicable.

## Upload Discipline

- Assume the chime and one or more sensor boards may be connected at the same
  time.
- Before uploading, identify the intended board and port. Start with
  `arduino-cli board list`.
- If multiple ESP32 boards are present, distinguish them by serial output,
  firmware strings, MAC address, or another concrete signal before uploading.
- Never guess the port when the chime and a sensor are both connected.

## Arduino Commands

- Compile chime firmware with its 2 MB app partition:
  `arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=no_ota .`
- Upload chime firmware with the same partition profile:
  `arduino-cli upload -p <chime-port> --fqbn esp32:esp32:esp32s3:PartitionScheme=no_ota .`
- Compile Wi-Fi button/touch sensor firmware:
  `arduino-cli compile --fqbn esp32:esp32:esp32s3 sensors/wifi_button_touch`
- Upload Wi-Fi button/touch sensor firmware:
  `arduino-cli upload -p <sensor-port> --fqbn esp32:esp32:esp32s3 sensors/wifi_button_touch`
- Compile Wi-Fi radar sensor firmware:
  `arduino-cli compile --fqbn esp32:esp32:esp32s3 sensors/wifi_radar`
- Upload Wi-Fi radar sensor firmware:
  `arduino-cli upload -p <sensor-port> --fqbn esp32:esp32:esp32s3 sensors/wifi_radar`
