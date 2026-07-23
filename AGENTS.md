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

## Workflow Control Words

Treat these user terms as explicit stopping points:

- `implement`: edit, compile, and test, but do not commit.
- `checkpoint`: create a focused local commit, but do not push.
- `publish`: push the current focused branch and open a draft pull request.
- `ready`: mark the current draft pull request ready for review.
- `merge`: merge the current pull request, switch to `main`, and fast-forward
  the local checkout to `origin/main`.
- `local only`: do not make GitHub, network-device, or other external writes.

Without one of these terms, implement and validate the requested change, then
stop at the safest natural checkpoint. Never infer `ready` or `merge` from
general approval such as “looks good.”

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
- For registered hardware, start with `scripts/device.sh list` and upload with
  `scripts/device.sh upload <device>`. The registry matches stable USB hardware
  IDs and must refuse to guess.
- Use `arduino-cli board list` directly only when diagnosing or registering new
  hardware.
- If multiple ESP32 boards are present, distinguish them by serial output,
  firmware strings, MAC address, or another concrete signal before uploading.
- Never guess the port when the chime and a sensor are both connected.
- Treat `scripts/device.sh trigger <device>` as an audible external action.
  Run it only when the user has requested or is actively expecting a bench
  trigger.

## Arduino Commands

- Compile into the workspace-local `.arduino/` build cache:
  - Chime/hub: `scripts/compile.sh chime`
  - Button/touch sensor: `scripts/compile.sh wifi_button_touch`
  - Radar sensor: `scripts/compile.sh wifi_radar`
  - Everything: `scripts/compile.sh all`
- Upload registered hardware by stable identity:
  `scripts/device.sh upload <device>`.
- Use the underlying `arduino-cli` commands only for unregistered hardware or
  diagnostics. Preserve the chime's
  `esp32:esp32:esp32s3:PartitionScheme=no_ota` profile.
