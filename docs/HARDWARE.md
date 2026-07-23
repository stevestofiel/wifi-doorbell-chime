<!-- SPDX-License-Identifier: MIT -->

# Hardware Diagrams

These diagrams are based on the current prototype photos and the firmware pin
assignments. They are documentation drawings, not CAD. Verify exact solder
connections and power rails against the physical build before reproducing.

## Component Overview

![Component overview](hardware-overview.svg)

Visible prototype parts:

- ESP32-S3 Super Mini development board
- MAX98357A I2S mono amplifier module
- Small 8 ohm / 3 W speaker module
- ElectroCookie prototype board
- Short USB-C cable and USB power adapter
- Cylindrical enclosure with front speaker grille and internal rear USB access

## Wiring

![Wiring diagram](wiring-diagram.svg)

Treat this as the primary technical diagram.

Firmware pin assignments:

- I2S bit clock: `GPIO4`
- I2S word select / LRCLK: `GPIO5`
- I2S data out: `GPIO6`
- Manual play button: `GPIO13` to ground, using `INPUT_PULLUP`
- Onboard indicator LED: `GPIO48`

The MAX98357A speaker output is bridged. Connect the speaker only to the amp
module speaker terminals, not to ground.

## Enclosure Layout

![Enclosure layout](enclosure-layout.svg)

The enclosure photos show a round front grille, internal speaker mounted near
the front opening, a prototype board behind it, and a USB-C cable routed toward
the rear/top opening for power and programming.

## Rev A Chime PCB Validation

The first custom chime PCB is electrically functional when the ESP32-S3 Super
Mini is raised above the carrier board with taller socket/header clearance.

Observed results:

- The second chime node booted, played the startup sound, joined Wi-Fi, accepted
  a sound upload, played the uploaded sound, and ran in the enclosure from a wall
  outlet.
- The MAX98357A audio path, speaker wiring, ESP32 socket wiring, and SPIFFS
  storage partition all behaved correctly during bench testing.
- Low-profile ESP32 socket/header placement caused the setup access point to be
  difficult or impossible to see even though firmware was running.
- Removing a large physical section of PCB/copper near the ESP32 did not fix the
  Wi-Fi issue by itself.
- Taller ESP32 socket/header clearance restored strong Wi-Fi signal in the
  assembled unit and did not create enclosure fit or clearance problems.

For future PCB revisions or assembly instructions:

- Specify the taller ESP32 socket/header stack that was validated in the
  enclosure.
- Keep the ESP32 antenna region free of copper, metal, and nearby hardware where
  practical, but treat module height/clearance as the proven Rev A fix.
- Keep useful ground copper near the MAX98357A audio section unless testing
  shows a reason to remove it.
- If using 8-pin sockets because the ninth ESP32 pins are unused, document the
  orientation clearly. A full footprint remains preferable for mechanical keying
  and assembly confidence.
- Re-test Wi-Fi signal, captive portal visibility, sound upload, playback, and
  enclosure fit before ordering another PCB revision.

## Notes For Next Revision

- Add a labeled internal photo after final soldering.
- Confirm whether amplifier power is taken from ESP32 5 V/VBUS or 3.3 V.
- Add a strain-relief detail for the short USB-C cable.
- Add a physical admin-password recovery gesture if the button placement allows
  a reliable boot-time hold.
- Batch any Rev B chime PCB order with the remote sensor PCB order if the chime
  board is still functioning well with the taller ESP32 socket/header.
