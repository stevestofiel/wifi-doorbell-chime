<!-- SPDX-License-Identifier: MIT -->

# Embedded Sound Spec

Embedded sounds are compact fallback assets stored in firmware program memory.
They should make a new chime useful before custom audio is uploaded, but they
must not consume space needed for roadmap features.

## Current Flash Budget

Measured with:

```text
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=no_ota .
```

Current chime firmware size:

```text
Used app space: about 1.41 MB
Max app space: 2.00 MB
Free app space: about 686 KB
```

The existing startup MP3 is about 17 KB of audio data.

## Budget Rules

- Target each embedded sound at 5-20 KB.
- Treat 30 KB as the hard maximum for one sound.
- Keep the first built-in sound set at or below 60 KB total.
- Keep at least 400-500 KB of app flash free for future firmware work.
- Do not add long music, voice prompts, or high-fidelity audio as built-ins.

## Format

- Store built-ins as MP3 byte arrays in PROGMEM.
- Prefer very short mono sounds.
- Use low bitrate settings that still sound acceptable on the small speaker.
- Keep source audio files outside firmware unless they are intentionally added
  as documentation assets.

## Initial Built-In Keys

Planned keys:

```text
doorbell
motion
mailbox
package
```

Initial event defaults:

```text
doorbell.press -> doorbell
motion.detected -> motion
mailbox.open / mailbox.flag-raised -> mailbox
package.detected -> package
```

## Resolution Order

Sensor events should resolve sounds in this order:

1. Explicit valid sound key from the trigger.
2. Custom chime rule configured in SPIFFS.
3. Built-in default sound key for the sensor type/event.
4. Current active uploaded chime.

Uploaded sounds and user rules must remain higher priority than built-ins.

## Implementation Notes

- Add a small built-in sound registry rather than special-casing each key.
- Keep file-backed sounds and PROGMEM-backed sounds behind a shared playback
  interface where practical.
- Add one built-in sound first, verify playback, then add the rest.
- Show built-in defaults in the Manage UI only after playback resolution works.
