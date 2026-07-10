<!-- SPDX-License-Identifier: MIT -->

# Radar Sensor Prototype Plan

This note captures the first hardware path for a Wi-Fi radar motion sensor using
the current ESP32-S3 Super Mini-style sensor board and the photographed
RCWL-0516 microwave radar module.

## Current Module

The photographed radar board is labeled `RCWL-0516` and exposes:

```text
CDS VIN OUT GND 3V3
```

For the first prototype, use the `3V3`, `GND`, and `OUT` pins only. Leave `VIN`
and `CDS` disconnected until the basic motion event path is working.

## Breadboard Wiring

- RCWL `3V3` to ESP32 `3.3V`
- RCWL `GND` to ESP32 `GND`
- RCWL `OUT` to ESP32 `GPIO4`
- Setup/reset button between ESP32 `GPIO3` and `GND`
- Power the ESP32 over USB for initial serial logging

The firmware treats `OUT` as an active-high 3.3 V signal and sends:

```text
sensor=bench-radar&type=motion&event=detected
```

## Prototype Checkpoints

1. Upload `examples/wifi_sensor_radar/wifi_sensor_radar.ino`.
2. Configure the chime URL through the `ChimeSensor` setup portal.
3. Watch serial logs for `Input change: radar=1`.
4. Confirm the chime event log shows `bench-radar motion: detected`.
5. Add or adjust a Manage UI rule for `motion.detected`.
6. Move the module to the intended mounting location and retest for false
   triggers before designing the next board.

## Hardware Questions For The Next Board

- Power source: USB, wall-powered 5 V with local regulation, or battery.
- Placement: radar modules can detect through some plastics and walls, so test
  in the intended enclosure and near the intended door before committing.
- Inhibit behavior: the `CDS` pin can be explored later if light-dependent
  suppression is useful, but it is not needed for the first motion prototype.
- Reset access: keep a physical setup button reachable, or define a reliable
  boot-time gesture.
- Serviceability: include labels for `3V3`, `GND`, `OUT`, and setup/reset on
  the prototype board.

## Firmware Tuning

The radar sketch starts with:

- `TRIGGER_COOLDOWN_MS = 10000`
- `RADAR_SETTLE_MS = 80`

Increase cooldown if the sensor is too chatty in real placement. Increase
settle time only if the `OUT` signal chatters electrically.
