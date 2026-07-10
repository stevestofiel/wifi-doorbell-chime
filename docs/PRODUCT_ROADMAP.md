<!-- SPDX-License-Identifier: MIT -->

# Product Roadmap

This roadmap captures the planned expansion from a single Wi-Fi chime into a
small product family that supports remote sensors, multiple chime receivers, and
large-property LoRa coverage while preserving the current Wi-Fi chime as a
useful device.

## Implementation Status

- [x] Remote sensor and multi-chime roadmap documented.
- [x] Minimal chime `/trigger` endpoint.
- [x] Chime `_doorbell-chime._tcp` mDNS service advertisement.
- [x] Wi-Fi sensor button/touch prototype.
- [x] Sensor captive portal setup.
- [x] Sensor reset/setup gesture.
- [x] Sensor base firmware and driver refactor.
- [x] Wi-Fi radar sensor prototype scaffold.
- [x] Distributed multi-chime network model documented.
- [x] Chime event log and indicator behavior.
- [x] Event IDs and duplicate suppression.
- [x] Local chime event log UI.
- [x] Sensor-to-sound rule backend.
- [x] Sensor-to-sound rule editor UI.
- [ ] Peer chime configuration.
- [ ] Peer log aggregation in the chime UI.
- [ ] MQTT event publishing.
- [ ] LoRa gateway prototype.
- [ ] LoRa sensor prototype.

## Next Session Plan

Focus: add compact built-in default sound assets.

1. Pick the first tiny embedded sound for `doorbell`.
2. Add a resolver path for built-in audio keys that do not require SPIFFS.
3. Add mailbox, motion, and package defaults after the first asset works.
4. Keep user-uploaded rules higher priority than built-in defaults.
5. Smoke test fallback behavior with no custom rule configured.

## Product Variants

### Wi-Fi Chime

The current hardware design remains the lower-cost chime receiver.

- Plays uploaded WAV/MP3 sounds and compact built-in defaults.
- Receives local HTTP triggers over Wi-Fi.
- Can participate as a satellite chime in a multi-chime installation.
- Can receive forwarded events from a LoRa gateway chime.
- Does not require LoRa hardware to remain useful in larger systems.

### LoRa Gateway Chime

The higher-cost chime variant adds LoRa radio hardware while keeping the same
core chime behavior.

- Plays local sounds just like the Wi-Fi chime.
- Receives Wi-Fi HTTP triggers.
- Receives long-range LoRa sensor packets.
- Translates LoRa packets into the same internal sensor-event pipeline used by
  HTTP triggers.
- Can forward events to Wi-Fi-only chimes over the LAN.

### Wi-Fi Sensor

The first remote sensor target should use Wi-Fi because it can trigger the
existing chime firmware over HTTP with minimal system complexity.

- Best fit for normal houses, apartments, and outbuildings with reliable Wi-Fi.
- Sends HTTP trigger requests to one or more chimes.
- Can carry sensor identity, sensor type, event type, and optional sound target.

Wi-Fi sensor firmware should be shared across sensor products as much as
possible. Avoid maintaining separate complete firmware sketches for every
sensor type.

Common base firmware responsibilities:

- Wi-Fi setup and saved configuration.
- Captive portal setup and physical reset gesture.
- Chime target URL, token, sensor ID, sensor type, and event configuration.
- HTTP `/trigger` client behavior.
- Cooldown, logging, and basic health behavior.

Sensor-specific behavior should live in small driver modules:

- Button/touch driver.
- Reed switch driver.
- Motion driver.
- Ultrasonic distance driver.
- Tilt/mailbox driver.

Each driver should expose a small polling contract, such as "begin hardware"
and "return a semantic event when one occurs." The base firmware should not need
to know whether the event came from a pushbutton, reed switch, radar module, or
future sensor type.

### LoRa Sensor

LoRa sensors are for larger properties or locations where Wi-Fi is unreliable.

- Sends compact versioned packets to a LoRa gateway chime or dedicated gateway.
- Should use counters or event IDs for replay and duplicate protection.
- May send repeated packets and optionally listen for acknowledgements depending
  on power budget.

## Core Architecture

Remote triggering should be modeled as a sensor event rather than as a transport
specific action.

```text
Sensor event:
  sensor id
  sensor type
  event type
  optional sound key
  optional scope/group
  optional event id or counter

Transports:
  Wi-Fi HTTP
  LoRa packet
  future ESP-NOW or other local transport
```

The firmware should converge all trigger sources into a shared processing path:

```text
HTTP /trigger request
        |
        v
parse SensorEvent
        |
        v
processSensorEvent()
        |
        v
resolve sound and routing
        |
        v
play locally and/or relay to peers

LoRa packet receive
        |
        v
parse SensorEvent
        |
        v
same processing path
```

This keeps `/trigger` as the HTTP representation of the sensor protocol while
allowing LoRa to reuse the same behavior without pretending to be HTTP on the
air.

## MVP Track

The first usable product path should stay intentionally simple:

1. Chimes expose `/trigger` and advertise a chime-specific mDNS service.
2. Wi-Fi sensors use their own captive portal for Wi-Fi and target setup.
3. Sensors send semantic events rather than sound-file assumptions.
4. Each chime resolves those events locally.
5. Multiple chimes are supported first by sensor fan-out, then by peer relay.

This avoids early BLE provisioning complexity while leaving room for a smoother
pairing flow later.

## HTTP Trigger Endpoint

Add a new endpoint for sensor events:

```text
GET /trigger?sensor=front-door&type=doorbell&event=press
```

Optional parameters:

```text
sound=<sound_id>
scope=local|all|group:<name>
gain=<0.0-3.0>
event=<event_type>
token=<lan_admin_password>
relay=0|1
eventId=<dedupe_id>
```

Existing endpoints should remain:

- `/chime` for simple active-chime playback.
- `/play?key=<sound_id>` for direct sound playback.

The new endpoint should be additive so existing automations continue working.

## Chime Discovery

Each chime should keep its human-friendly mDNS hostname, such as:

```text
doorbell-front.local
```

It should also advertise a custom service for discovery:

```text
_doorbell-chime._tcp
```

This allows future sensor captive portals and peer-chime setup screens to scan
for chimes on the LAN and present choices such as Front, Upstairs, Shop, or
Basement. Manual URL entry should remain available because mDNS discovery can be
unreliable across some routers, VLANs, and IoT networks.

## Sound Resolution

Each chime should decide what sound to play for a sensor event. This allows the
same sensor to produce different behavior on different chimes.

Resolution order:

1. Explicit sound key from the trigger, if present and valid.
2. Per-sensor override configured on this chime.
3. Sensor-type default built into firmware.
4. Current active chime sound.
5. Silent/disabled if configured for this sensor on this chime.

Example cases:

- Front door plays a full chime upstairs.
- Front door plays a shorter sound in the shop.
- Mailbox sensor plays only in the house.
- Shop motion sensor is silent in bedrooms.

## Sensor Configuration

Store sensor rules in SPIFFS, likely in `/sensors.json`.

Example shape:

```json
{
  "sensors": [
    {
      "id": "front-door",
      "label": "Front Door",
      "type": "doorbell",
      "soundKey": "k123abc",
      "enabled": true
    },
    {
      "id": "mailbox",
      "label": "Mailbox",
      "type": "mailbox",
      "soundKey": "",
      "enabled": true
    }
  ]
}
```

The firmware should include compact built-in defaults for known sensor types.
These are best stored in program memory, similar to the existing boot sound, so
new devices have sensible behavior before user audio is uploaded.

Initial sensor types:

- `doorbell.press`
- `gate.open`
- `mailbox.open`
- `motion.detected`

## Multi-Chime Behavior

Multi-chime support should make older Wi-Fi-only chimes more useful, not
obsolete.

## Multi-Chime Network Model

The product should be distributed-first. A central server, Docker service, or
home-automation hub may be useful later, but should not be required for the core
system.

Consensus model:

- Each chime is standalone and useful by itself.
- Multiple chimes form a peer group.
- Sensors send semantic events rather than sound-file assumptions.
- Each chime decides locally whether to play, stay silent, log, indicate, or
  relay an event.
- Each chime stores its own recent event log.
- Any chime UI may fetch peer logs and merge them into a whole-property view.
- Event IDs should prevent duplicate playback, relay loops, and duplicate log
  rows.
- MQTT, Home Assistant, Docker dashboards, and similar systems are optional
  integrations, not required infrastructure.
- A LoRa gateway chime is a peer with extra radio capability, not a mandatory
  parent controller.

In short:

```text
Distributed chime network first; optional hub integrations later.
```

Routing scopes:

- `local`: play only on the receiving chime.
- `all`: play locally and forward to configured peers.
- `group:<name>`: play locally or forward only to a named group.
- `silent`: receive the event but do not play.

Recommended first implementation:

- Sensors can fan out directly to multiple chime URLs.
- Each chime applies its own local sound rules.
- No dependency on a primary controller.

Later implementation:

- Chimes can maintain a peer list.
- A LoRa gateway chime or selected Wi-Fi chime can relay events to peers.
- Relayed events must include loop prevention, such as `relay=0`, a hop count,
  or a dedupe event ID.

## Peer Chime Configuration

Store peer chime configuration separately from sound files, likely in
`/peers.json`.

Example shape:

```json
{
  "peers": [
    {
      "id": "upstairs",
      "label": "Upstairs",
      "url": "http://doorbell-upstairs.local",
      "group": "house",
      "enabled": true
    },
    {
      "id": "shop",
      "label": "Shop",
      "url": "http://doorbell-shop.local",
      "group": "outbuildings",
      "enabled": true
    }
  ]
}
```

Peer relay should use the same `/trigger` endpoint so the receiving chime can
apply its own rules.

## LoRa Gateway Considerations

LoRa should be a gateway capability, not a separate chime ecosystem.

```text
LoRa sensor
    -> LoRa gateway chime
    -> local playback
    -> optional HTTP relay to Wi-Fi-only chimes
```

LoRa-specific requirements:

- Versioned packet format.
- Regional radio configuration, such as 915 MHz for US builds.
- Sensor ID and type in every event packet.
- Event counter or event ID for replay and duplicate protection.
- Duplicate suppression window on the gateway.
- Optional ACK strategy for sensors that can afford receive time.
- Clear behavior when multiple gateways hear the same sensor.

The LoRa gateway should translate packets into the shared `SensorEvent`
structure and then call the same processing path as `/trigger`.

## Security Model

The current project is designed for trusted LAN use. Remote sensor support
should preserve that model and avoid implying internet-safe security.

Wi-Fi HTTP sensors:

- Use the existing playback-auth token when enabled.
- Support token in query string for simple microcontroller clients.
- Continue documenting that plain HTTP is a LAN guard, not strong encryption.

LoRa sensors:

- Do not rely on sensor IDs as secrets.
- Include event counters or IDs to reduce replay and duplicate triggers.
- Consider a lightweight shared key or packet signature after the basic protocol
  is stable.

## Manage UI Roadmap

The Manage page can grow in stages.

Early UI:

- Show copyable `/trigger` URLs.
- Show mDNS and LAN DNS hostnames.
- Show known sound IDs.

Sensor UI:

- List configured sensors.
- Edit sensor label, type, enabled state, and sound mapping.
- Choose default, uploaded sound, built-in sound, or silent.
- Show recently seen sensors.

Multi-chime UI:

- List peer chimes.
- Add peer URL manually.
- Assign peers to groups.
- Test a peer chime.
- Choose whether this chime relays events.

LoRa UI:

- Show gateway radio status.
- Show recently heard LoRa sensors.
- Show duplicate/invalid packet counts.
- Configure regional radio settings if needed.

## Implementation Phases

### Phase 1: Roadmap And Protocol Shape

- Capture this roadmap.
- Keep firmware behavior unchanged.
- Use this document as the reference for future implementation branches.

### Phase 2: HTTP Trigger Foundation

- Add `SensorEvent` structure.
- Add `/trigger`.
- Route `/trigger` through shared event-processing code.
- Keep `/chime` and `/play` backward compatible.
- Add minimal serial logging for trigger source and resolved sound.

### Phase 3: Sensor Sound Rules

- Add `/sensors.json`.
- Add sensor-id and sensor-type sound resolution.
- Add compact built-in default sounds.
- Add JSON endpoints for reading and updating sensor rules.

### Phase 4: Multi-Chime Relay

- Add peer configuration.
- Add `scope=local|all|group:<name>`.
- Relay events to peers over HTTP.
- Add duplicate and relay-loop prevention.

### Phase 5: Manage UI

- Add sensor rules UI.
- Add peer chime UI.
- Add trigger URL helper UI.
- Add recent-event visibility.

### Phase 6: Wi-Fi Sensor Firmware

- Add a reference Wi-Fi sensor sketch or separate firmware package.
- Support sensor ID, type, event, target URLs, and optional token.
- Support fan-out to multiple chimes.

### Phase 7: LoRa Gateway Chime

- Add compile-time LoRa gateway feature flag.
- Add LoRa radio hardware docs.
- Parse LoRa packets into `SensorEvent`.
- Add gateway status and recently heard sensor visibility.

### Phase 8: LoRa Sensor Firmware

- Add compact packet protocol.
- Add low-power wake/send behavior.
- Add event counters and duplicate-aware retries.
- Add regional radio configuration guidance.

## Open Decisions

- Exact LoRa module and pins for the gateway hardware.
- Whether Wi-Fi sensor firmware lives in this repo or a companion repo.
- Initial built-in default sound set and size budget.
- Whether peer discovery should remain manual or eventually use mDNS discovery.
- Whether LoRa packet authentication is included in the first LoRa release or a
  follow-up security phase.
