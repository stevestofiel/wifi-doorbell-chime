<!-- SPDX-License-Identifier: MIT -->

# Wi-Fi Sensor Radar Prototype

Reference sketch for an ESP32-S3 Super Mini-style board connected to an
RCWL-0516 microwave radar motion module. It reuses the shared Wi-Fi setup and
HTTP `/trigger` client used by the button/touch prototype.

## Prototype Goal

Use this as the first radar hardware test before designing a smaller board or
enclosure. The sketch sends this semantic chime event:

```text
/trigger?sensor=bench-radar&type=motion&event=detected&eventId=<sensor-generated-id>
```

The chime firmware already maps `motion.detected` to the `motion` sound key by
default, and the Manage UI Rules tab can override the sound per sensor.

## Wiring

RCWL-0516 module:

- `GND` to ESP32 `GND`
- `3V3` to ESP32 `3.3V`
- `OUT` to `GPIO4`
- Leave `VIN` disconnected for the first 3.3 V prototype
- Leave `CDS` disconnected unless adding a light-dependent inhibit circuit

Setup/reset button:

- One side to `GPIO3`
- Other side to `GND`

The photographed module exposes `CDS VIN OUT GND 3V3`. The `OUT` pin is treated
as an active-high 3.3 V logic signal. If a different radar module is used,
confirm the output voltage before connecting it to an ESP32 GPIO.

## Configure

On first boot, or when no chime URL has been saved, the sensor starts a captive
portal:

```text
SSID: ChimeSensor
Password: config123
```

Recommended first values:

- Sensor ID: `bench-radar`
- Sensor type: `motion`
- Event: `detected`

Hold the setup button while resetting or powering on the board for about 2
seconds to clear saved Wi-Fi and sensor settings.

## Test Notes

- Start with USB power so serial logs are visible.
- The radar board may hold `OUT` high for a few seconds after motion. The
  driver emits once per active period, and `TriggerClient` adds a 10 second
  cooldown.
- Mount orientation and nearby moving objects matter more than code at this
  stage. Test on the bench, then at the intended door location, before making
  a board layout.
- If the sensor is too chatty, increase `TRIGGER_COOLDOWN_MS` first. If it
  chatters electrically, increase `RADAR_SETTLE_MS`.
