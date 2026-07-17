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
- [x] Shared remote sensor setup/test/service button helper.
- [x] Distributed multi-chime network model documented.
- [x] Chime event log and indicator behavior.
- [x] Event IDs and duplicate suppression.
- [x] Local chime event log UI.
- [x] Sensor-to-sound rule backend.
- [x] Sensor-to-sound rule editor UI.
- [x] Embedded sound budget/spec documented.
- [x] Wi-Fi radar service button, gain trim, and RCWL motion bench validated.
- [x] Second chime node assembled on Rev A PCB and validated in enclosure with
  taller ESP32 socket/header clearance.
- [ ] Peer chime configuration.
- [ ] Peer log aggregation in the chime UI.
- [ ] MQTT event publishing.
- [ ] LoRa gateway prototype.
- [ ] LoRa sensor prototype.

## Next Session Plan

Focus: finish remote sensor physical validation, tune radar deployment, and
prepare remote sensor PCB decisions before ordering another chime PCB revision.

1. Upload the button/touch firmware and verify boot-hold setup reset.
2. Verify short-press sends a `doorbell.press` event.
3. Verify touch input behavior and false-trigger resistance at the intended
   mounting location.
4. Mount-test the RCWL radar and tune cooldown/settle timing if needed.
5. Confirm battery, enclosure, reset-button, and service-button access before
   calling either remote sensor hardware stable.
6. Sketch the remote sensor PCB requirements, then decide whether to batch those
   boards with a Rev B chime PCB order.

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

The preferred product direction is to avoid a separate LoRa-only sensor family
if possible. Instead, explore an optional external "range pod" that plugs into
a common remote sensor base. A sensor without the pod can use Wi-Fi; the same
sensor with the pod can use LoRa when Wi-Fi range is poor.

The first ordered LoRa candidate is a pair of 915 MHz UART AT-command modules
based on SX1262-class LoRa radio hardware. They are not intended for LoRaWAN or
Meshtastic in this project; the first goal is a private point-to-point bench
test that sends small semantic sensor-event packets.

## Core Architecture

Remote triggering should be modeled as a sensor event rather than as a transport
specific action.

```text
Sensor event:
  sensor id
  sensor type
  event type
  optional sound key
  optional event gain
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

`gain` is an event-level volume hint, not a replacement for the chime's master
volume. A remote sensor may provide it from firmware, setup configuration, or a
physical control such as a small rotary potentiometer. The receiving chime
should still apply its own master volume and local limits as the final authority
before playback.

Example:

```text
/trigger?sensor=driveway&type=motion&event=detected&gain=0.65
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

## Event Gain And Sensor Volume Controls

Remote sensors may eventually include a small set-and-forget gain control, such
as a board-mounted trim potentiometer, to tune how loudly that sensor's events
play relative to other events. This is especially useful when two sensors should
use different sound prominence without changing the receiver's master volume.
For compact remote sensors, this should be an installer/service adjustment
rather than a prominent user-facing knob.

Recommended behavior:

- Treat sensor-provided `gain` as a multiplier or hint for one event.
- Keep the chime's master volume as the final user safety control.
- Clamp the resolved event volume to the chime's supported playback range.
- Persist per-sensor rule volume on the chime separately from any physical
  sensor dial, so installed sensors can still be managed from the receiver UI.
- Consider per-sensor low/high calibration in the chime rule UI after the gain
  path is stable. This should scale or clamp the sensor's event `gain` locally
  on the chime, rather than requiring the chime to know raw ADC values from the
  sensor.
- Prefer the term `gain` for sensor-originated relative adjustment; reserve
  `volume` for absolute chime playback controls if both names are ever exposed.

Possible final volume model:

```text
final event volume = chime master volume * local rule gain * sensor event gain
```

Implementation notes:

- A Wi-Fi sensor can read a compact trim potentiometer on an ADC-capable pin
  and include the mapped value in `/trigger` as `gain=<value>`.
- LoRa sensors can carry the same field in the compact packet format, likely as
  a small integer that the gateway maps back into the shared `SensorEvent`.
- Use conservative defaults, such as `1.0` when no gain is provided.
- Consider a practical UI range such as 10-150% while keeping the wire protocol
  tolerant of a wider clamped range.
- If installers need finer control, expose optional per-sensor min/max or
  inverted-direction calibration on the chime. Keep sensor-side defaults simple
  enough that a trim pot works without calibration.
- Physical gain controls are a product feature, not a required part of the
  first remote sensor hardware milestone. Prefer low-profile internal trim
  controls unless a specific kit genuinely needs an external dial.

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

## Alert Policies

Some events are momentary and should play once, such as `doorbell.press`,
`mailbox.open`, or `motion.detected`. Other events are stateful and may need
attention until they clear, such as a freezer door left open, water leak,
garage door left open, gate left open, or help button.

Future rule behavior should support alert policies:

```text
play once
repeat until clear
escalate if unresolved
optional all-clear sound
```

Example stateful pairs:

```text
freezer.open  -> repeat alert until freezer.closed
leak.wet      -> repeat alert until leak.dry
garage.open   -> repeat alert until garage.closed
help.pressed  -> repeat alert until help.clear
```

This likely requires state tracking, timers, repeat limits, quiet-hour
exceptions, and UI controls. It should be implemented after the basic sensor
rules and event pipeline remain stable.

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
- A sensor has one owner chime. The owner chime enrolled/configured the sensor
  and is the sensor's normal HTTP target.
- Each chime decides locally whether to play, stay silent, log, indicate, or
  relay an event.
- Each chime stores its own recent event log.
- Event logs stay local-first. A future helper app or troubleshooting view may
  poll all chimes, and possibly awake/configurable sensors, to build a merged
  diagnostic view.
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

- Sensors send direct events to one owner chime.
- The owner chime supports simple per-sensor forwarding: all peers or none.
- Receiving peer chimes can play forwarded events using built-in defaults.
- Forwarded unknown sensors need more product/security thought before custom
  sound behavior is assumed.
- No dependency on a primary controller.

Later implementation:

- Chimes can maintain a peer list.
- A LoRa gateway chime or selected Wi-Fi chime can relay events to peers.
- Relayed events must include loop prevention, such as `relay=0`, a hop count,
  or a dedupe event ID.
- Sensor ownership transfer can move a sensor from one owner chime to another
  without factory-resetting the sensor.
- Owner-chime outage fallback needs a future design. Do not require fallback for
  the first peer-relay implementation.
- Group forwarding and named peer routing need more design before replacing the
  first all-or-none forwarding control.
- A peer chime should not create a full local override for a forwarded sensor in
  the first design. It may still enforce local safety limits, such as maximum
  playback gain/volume, quiet behavior, or disabled playback.

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

### Optional Range Pod Concept

The optional range pod concept keeps the base sensor enclosure small while
allowing field upgrades for long-range locations such as driveways, gates,
mailboxes, sheds, and outbuildings.

Possible range pod interface:

```text
VCC or 3V3
GND
UART TX
UART RX
AUX / ready / interrupt
M0 / mode
M1 / mode
RESET or SET, if required by the module
DETECT
```

Design rules:

- Do not finalize the expansion connector until the UART LoRa module pair is
  bench-tested.
- Prefer a private LoRa packet format that carries the same semantic event
  fields as HTTP `/trigger`.
- Keep the module mechanically separate from the base sensor enclosure if that
  avoids bloating small indoor sensors.
- Treat connector sealing, strain relief, keyed insertion, and antenna position
  as first-class hardware requirements for outdoor pods.

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
- Preserve room for per-sensor rule gain, even if the first UI only maps
  sensors to sounds.

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
- Optionally prototype ADC-based sensor event gain from a low-profile trim pot
  after the required setup/test/service button behavior is stable.

### Phase 7: LoRa Gateway Chime

- Bench-test the ordered UART LoRa module pair with simple point-to-point
  packets.
- Add compile-time LoRa gateway feature flag.
- Add LoRa radio hardware docs.
- Parse LoRa packets into `SensorEvent`.
- Add gateway status and recently heard sensor visibility.

### Phase 8: LoRa Sensor Firmware

- Add a minimal UART LoRa sensor sender after the module pair is bench-tested.
- Add compact packet protocol.
- Add low-power wake/send behavior.
- Add event counters and duplicate-aware retries.
- Add regional radio configuration guidance.

## Open Decisions

- Whether the first LoRa path uses UART AT-command modules or direct SPI radio
  modules after bench testing.
- Exact range pod connector, pins, sealing, and detect behavior.
- Whether LoRa is an external range pod, an internal option, or both.
- Whether Wi-Fi sensor firmware lives in this repo or a companion repo.
- Initial built-in default sound set and size budget.
- Whether peer discovery should remain manual or eventually use mDNS discovery.
- Whether LoRa packet authentication is included in the first LoRa release or a
  follow-up security phase.
- Exact gain ranges, clamping behavior, and UI language for sensor event gain
  versus chime master volume.
- Whether per-sensor gain calibration belongs in the first rule editor update
  or a later installer/service UI pass.
