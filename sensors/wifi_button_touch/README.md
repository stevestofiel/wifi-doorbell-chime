<!-- SPDX-License-Identifier: MIT -->

# Wi-Fi Button/Touch Sensor Firmware

Firmware for an ESP32 Super Mini-style board used as a remote chime
sensor. The first bench board was labeled like a C3 variant, but uploaded as an
ESP32-S3.

## Structure

This sensor firmware is split so future sensor types can reuse the same base behavior:

- `../common/SensorConfig.h`: Wi-Fi setup, captive portal fields, saved sensor config,
  and setup-reset behavior.
- `../common/SensorButton.h`: required setup/test/service button behavior.
- `../common/TriggerClient.h`: Wi-Fi reconnect and HTTP `/trigger` calls.
- `ButtonTouchDriver.h`: capacitive touch input handling.
- `wifi_button_touch.ino`: wires the config manager, trigger client, and
  selected driver together.

Additional sensors, such as reed switches, radar/motion modules, or ultrasonic
distance modules, should add a small driver with the same pattern rather than
copying the whole sketch.

## Wiring

Service button:

- One side to `GPIO3`
- Other side to `GND`

Touch module:

- `G` to `GND`
- `V` to `3.3V`
- `S` to `GPIO4`

Do not power the touch module from `5V` unless its signal output is confirmed
safe for ESP32 3.3 V GPIO.

The current bench touch module is treated as active-low: its `S` output reads
low when the module's touch indicator LED is on. It also behaves like a
latched/toggle touch module, so the firmware emits a touch event on each stable
state change. This makes both "LED turns on" and "LED turns off" touches send
the configured sensor event.

Optional event-gain trim potentiometer:

- One outer pin to `3.3V`
- Other outer pin to `GND`
- Center/wiper pin to `GPIO2`

The firmware reads the trim pot only when a service-button or touch event is
sent. It maps the ADC reading to a conservative `gain` hint and includes it in
the `/trigger` request.

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

The sketch calls `/trigger` with a semantic event when the service button is
pressed or the touch input is held:

```text
/trigger?sensor=bench-button&type=doorbell&event=press&eventId=<sensor-generated-id>&gain=<trim-value>
```

The `eventId` is generated from the sensor MAC address, a local counter, and
uptime so the chime can ignore duplicate retries. It falls back to `/chime`
when testing against older chime firmware that does not have `/trigger` yet.

## Service Button

- Short press while running sends the configured sensor event.
- Hold while resetting or powering on the board for about 2 seconds to clear
  saved Wi-Fi and sensor settings. The sensor will start the `ChimeSensor`
  setup portal again.
