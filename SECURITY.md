# Security Notes

This firmware is intended for trusted LAN use only. Do not expose the device
directly to the internet with port forwarding.

## LAN Access

The web server runs over HTTP on the local network. For best results, place the
device on an IoT VLAN or isolated Wi-Fi network and only allow trusted systems,
such as UniFi Protect, to reach it.

## Admin Password

The user-facing UI should describe this as an admin or management password, not
as a token. "Token" is still an accurate implementation detail, but "Admin
Password" gives end users a clearer mental model.

Recommended user-facing copy:

- Setting label: "Admin Password"
- Placeholder: "optional password"
- Button: "Save Password"
- Enabled status: "Admin password enabled"
- Disabled status: "No admin password set"
- Prompt: "Enter admin password for this chime"

If no admin password is configured, the Manage page should make the tradeoff
clear: anyone on the same Wi-Fi network can manage sounds and settings.

## Shared Token Internals

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

## TODO

- Rename the Manage page security UI from "Shared Token" to "Admin Password"
  while keeping the internal token mechanism.
- Add a first-run warning when no admin password is set, with clear actions to
  add a password or continue without one.
- Document recovery for forgotten admin passwords. Today, physical USB access
  can clear auth by compiling once with `CLEAR_AUTH_ON_BOOT=1`, then flashing
  the normal firmware again.
- Consider a physical recovery gesture, such as holding the device button during
  boot to clear only the admin password.
