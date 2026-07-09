<!-- SPDX-License-Identifier: MIT -->

# Wi-Fi Sensor Button Prototype

Reference sketch for an ESP32 Super Mini-style board used as a remote chime
sensor. The first bench board was labeled like a C3 variant, but uploaded as an
ESP32-S3.

## Wiring

Button:

- One side to `GPIO3`
- Other side to `GND`

Touch module:

- `G` to `GND`
- `V` to `3.3V`
- `S` to `GPIO4`

Do not power the touch module from `5V` unless its signal output is confirmed
safe for ESP32 3.3 V GPIO.

The touch input is intentionally less immediate than the physical button. It
must stay active briefly before triggering, which helps avoid false rings from
hovering, long jumper wires, or breadboard capacitance.

## Configure

On first boot, or when no chime URL has been saved, the sensor starts a captive
portal:

```text
SSID: ChimeSensor
Password: config123
```

Join that Wi-Fi network from a phone or laptop, then fill in:

- Wi-Fi network and password
- Chime URL, usually the chime base URL
- Playback token, only if playback auth is enabled
- Sensor ID
- Sensor type
- Event

For first tests, use the chime IP address. These are both accepted:

```text
192.168.1.42
http://192.168.1.42
```

If a full endpoint such as `/trigger` or `/chime` is pasted, the sketch trims it
back to the chime base URL before saving.

The sketch calls `/trigger` with a semantic event:

```text
/trigger?sensor=bench-button&type=doorbell&event=press
```

It falls back to `/chime` when testing against older chime firmware that does
not have `/trigger` yet.

## Reset Setup

To clear saved Wi-Fi and sensor settings, hold the physical button while
resetting or powering on the board. Keep holding for about 2 seconds, then
release. The sensor will start the `ChimeSensor` setup portal again.
