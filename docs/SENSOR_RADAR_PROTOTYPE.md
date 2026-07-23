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
- Service button between ESP32 `GPIO3` and `GND`
- Optional 10K trim pot with outer pins to `3.3V`/`GND` and wiper to `GPIO2`
- Power the ESP32 over USB for initial serial logging

The firmware treats `OUT` as an active-high 3.3 V signal and sends:

```text
sensor=bench-radar&type=motion&event=detected
```

## Mini Breadboard Layout

Use a mini breadboard as a temporary carrier, not a final mechanical design.
Keep the current touch prototype intact as the known-good control.

Recommended layout:

- Place the ESP32-S3 Super Mini straddling the breadboard center gap with USB-C
  facing outward for easy upload and serial access.
- Put the service button near one edge so it can be pressed without touching
  jumper wires.
- Put the RCWL-0516 at the opposite edge with its antenna area facing away from
  the ESP32 and jumper-wire bundle.
- Run one short ground jumper from ESP32 `GND` to the radar `GND`.
- Run one short power jumper from ESP32 `3.3V` to radar `3V3`.
- Run one signal jumper from radar `OUT` to ESP32 `GPIO4`.
- Run the service button from ESP32 `GPIO3` to `GND`.
- Leave radar `VIN` and `CDS` unconnected.

Text layout:

```text
USB-C edge
+-------------------------------+
| ESP32-S3 Super Mini           |
|   GPIO3 -- service button - GND
|   GPIO4 <---- RCWL OUT        |
|   GPIO2 <---- trim pot wiper  |
|   3V3  ----> RCWL 3V3         |
|   GND  ----> RCWL GND         |
|                               |
|                   RCWL antenna|
+-------------------------------+
```

For the first bench test, do not add battery hardware. USB power plus serial
logs will make wiring and firmware problems much easier to separate.

## Prototype Checkpoints

1. Upload `sensors/wifi_radar/wifi_radar.ino`.
2. Configure the chime URL through the `ChimeSensor` setup portal.
3. Short-press the service button and confirm a test event is sent.
4. Watch serial logs for `Trigger gain: <value>`.
5. Watch serial logs for `Input change: radar=1`.
6. Confirm the chime event log shows `bench-radar motion: detected`.
7. Add or adjust a Manage UI rule for `motion.detected`.
8. Move the module to the intended mounting location and retest for false
   triggers before designing the next board.

## Hardware Questions For The Next Board

- Power source: USB, wall-powered 5 V with local regulation, or battery.
- Placement: radar modules can detect through some plastics and walls, so test
  in the intended enclosure and near the intended door before committing.
- Inhibit behavior: the `CDS` pin can be explored later if light-dependent
  suppression is useful, but it is not needed for the first motion prototype.
- Reset access: keep the physical service button reachable.
- Serviceability: include labels for `3V3`, `GND`, `OUT`, and setup/reset on
  the prototype board.

## Battery Options

The radar sensor should be battery-capable eventually, but the RCWL-0516 is not
ideal for a tiny long-life battery product because it is an always-on motion
sensor. Treat battery tests as a separate power milestone after the USB bench
prototype works.

Options to consider:

| Option | Pros | Cons | Best use |
| --- | --- | --- | --- |
| USB power bank | Easy, rechargeable, no regulator design | Bulky, not final-product shape | Early placement tests |
| 1-cell LiPo plus 3.3 V regulator | Common, rechargeable, compact | Needs charging/protection/regulator board | Rechargeable prototype |
| 18650 Li-ion plus regulator | More capacity | Larger enclosure, still needs protection/charging | Longer runtime experiments |
| 3x AA or AAA to 3.3 V regulator | Easy to source, replaceable | Bigger, voltage varies as cells drain | Simple user-replaceable prototype |
| 2x AA with boost regulator | Good availability | Boost converter idle current matters | Low-power sensor design later |

First recommendation:

1. Validate radar behavior on USB.
2. Try a USB power bank for a real doorway placement test.
3. Measure or estimate current before choosing LiPo vs AA/AAA.
4. For a production-style sensor, consider a lower-power radar/PIR module or a
   sleep/wake design before committing to RCWL-0516 on batteries.

Battery design notes:

- The ESP32 Wi-Fi transmit burst is power-hungry, so deep sleep matters for
  button/reed/mailbox sensors.
- RCWL-style radar usually wants to stay powered to detect motion, which limits
  battery life.
- Any onboard LED that blinks or stays on should be disabled or physically
  removed for battery builds.
- The required service button should use a wake-capable GPIO in future sleep
  firmware.

## Firmware Tuning

The radar sketch starts with:

- `TRIGGER_COOLDOWN_MS = 10000`
- `RADAR_SETTLE_MS = 80`

Increase cooldown if the sensor is too chatty in real placement. Increase
settle time only if the `OUT` signal chatters electrically.
