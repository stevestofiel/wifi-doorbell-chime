# Security Notes

This firmware is intended for trusted LAN use only. Do not expose the device
directly to the internet with port forwarding.

## LAN Access

The web server runs over HTTP on the local network. For best results, place the
device on an IoT VLAN or isolated Wi-Fi network and only allow trusted systems,
such as UniFi Protect, to reach it.

## Shared Token

The Manage page includes a shared token setting. When a token is configured,
management actions require that token:

- upload sounds
- set active sound
- delete sounds
- delete all files
- change gain
- change device label
- change security settings
- reset Wi-Fi

Playback URLs remain open by default for easier webhook integration. Enable
"Require token for playback URLs" on the Manage page if you want `/chime`,
`/play?key=...`, and numeric playback URLs to require the same token.

Example token-protected playback URL:

```text
http://<chime-ip>/play?key=<stable_key>&token=<shared_token>
```

## Development Uploads

Normal Arduino uploads preserve Wi-Fi and token settings because they do not
erase NVS/SPIFFS. Keep "Erase All Flash Before Sketch Upload" disabled unless
you intentionally want to clear local device state.
