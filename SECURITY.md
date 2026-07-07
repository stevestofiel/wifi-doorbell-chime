<!-- SPDX-License-Identifier: MIT -->

# Security Notes

This firmware is intended for trusted LAN use only. Do not expose the device
directly to the internet with port forwarding.

## LAN Access

The web server runs over HTTP on the local network. For best results, place the
device on an IoT VLAN or isolated Wi-Fi network and only allow trusted systems,
such as UniFi Protect, to reach it.

Because traffic is plain HTTP, the admin password is a practical LAN guard, not
strong cryptographic protection. A device or person able to observe local
traffic can capture it. Do not reuse an important password here.

Playback sound IDs in `/play?key=...` URLs are stable identifiers for sounds,
not secrets.

## Admin Password

The Manage page describes the security control as a LAN admin password.
Internally, the firmware stores and checks a shared token, but the user-facing
language is intended to be familiar to non-technical users.

If no admin password is configured, the Manage page warns that anyone on the
same Wi-Fi network can manage sounds and settings. This is convenient for
development, but a password is recommended on shared or less trusted networks.

## Shared Token Internals

The Manage page includes a shared-token setting behind the LAN admin password
UI. When a token is configured, management actions require that token:

- upload sounds
- set active sound
- delete sounds
- delete all files
- change gain
- change device label
- change security settings
- reset Wi-Fi

Playback URLs remain open by default for easier webhook integration. Enable
"Require admin password for playback URLs" on the Manage page if you want
`/chime`, `/play?key=...`, and numeric playback URLs to require the same
internal token.

Example token-protected playback URL:

```text
http://<chime-ip>/play?key=<sound_id>&token=<lan_admin_password>
```

## Development Uploads

Normal Arduino uploads preserve Wi-Fi and password settings because they do not
erase NVS/SPIFFS. Keep "Erase All Flash Before Sketch Upload" disabled unless
you intentionally want to clear local device state.

## Recovery

Forgotten admin passwords can be cleared with physical USB access by compiling
once with `CLEAR_AUTH_ON_BOOT=1`, flashing that recovery build, then flashing
the normal firmware again.

Future improvement: consider a physical recovery gesture, such as holding the
device button during boot to clear only the admin password.
