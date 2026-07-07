# WiFi Doorbell Chime

ESP32-S3 Wi-Fi chime that plays uploaded WAV/MP3 sounds from a local web UI and
simple HTTP playback URLs.

## Features

- Local Home and Manage pages over HTTP
- WAV/MP3 chime library stored in SPIFFS
- Stable sound-ID playback URLs for automations
- Captive portal provisioning with WiFiManager
- Editable device label for mDNS, such as `doorbell-front.local`
- Advanced LAN DNS suffix option for webhook-friendly names
- Wi-Fi reconnect watchdog for temporary network outages
- Optional LAN admin password for management actions

## Hardware

The current prototype uses an ESP32-S3 Super Mini, MAX98357A I2S amplifier,
small speaker, ElectroCookie prototype board, USB-C power, and a cylindrical
enclosure.

See [Hardware Diagrams](docs/HARDWARE.md) for component, wiring, and enclosure
layout diagrams.

## Build

Recommended Arduino settings:

- Board: `ESP32S3 Dev Module`
- Partition Scheme: `No OTA (2MB APP/2MB SPIFFS)`
- USB CDC On Boot: `Enabled`
- Erase All Flash Before Sketch Upload: `Disabled`

Required libraries:

- WiFiManager
- ESPAsyncWebServer
- AsyncTCP
- ArduinoJson
- ESP8266Audio

Compile with Arduino CLI:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=no_ota,CDCOnBoot=cdc .
```

Upload:

```sh
arduino-cli upload -p /dev/cu.usbmodem11401 --fqbn esp32:esp32:esp32s3:PartitionScheme=no_ota,CDCOnBoot=cdc .
```

## Provisioning

On first boot, or after Wi-Fi reset, the device starts a captive portal:

- SSID: `DoorbellChimeSetup`
- Password: `config123`

Development uploads preserve saved Wi-Fi, sounds, and settings when flash is not
erased.

The device advertises a DHCP hostname based on its label, such as
`doorbell-front`. The Manage page also includes an advanced LAN DNS option for
systems such as UniFi Protect that may not resolve mDNS `.local` names. LAN DNS
is off by default because your router or local DNS server must resolve that name
for it to work.

## HTTP Endpoints

- `GET /chime` plays the active chime
- `GET /play?key=<sound_id>` plays a specific sound
- `GET /list` returns uploaded sounds and stable sound IDs
- `GET /status` returns Wi-Fi, storage, active sound, gain, and security status
- `GET /upload` opens the Manage page

Playback sound IDs are stable identifiers, not secrets. The optional admin
password is intended as a local-network guard for management actions; it is not
a substitute for HTTPS or network isolation. See [Security Notes](SECURITY.md)
before exposing the device beyond a trusted LAN.

## License

This repository's original firmware, documentation, and diagrams are licensed
under the MIT License. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

Third-party Arduino libraries remain under their own licenses. Uploaded chime
sounds are user-provided content and are not covered by this repository's
license.
