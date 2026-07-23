<!-- SPDX-License-Identifier: MIT -->

# NoTiFi Product Naming

This document is the terminology source of truth for the product family. It
separates brand identity, hardware capability, sensing technology, user-defined
deployment, and event semantics so a physical product is not permanently named
for only one possible use.

This is a product-language decision, not a protocol migration. Existing API
routes, discovery identifiers, saved keys, filenames, and internal symbols
remain compatible unless a separate migration explicitly changes them.

## Core Product Identity

The primary device is:

```text
NoTiFi huB
```

Its functional description is:

```text
Notification Hub
```

Use each form for a different purpose:

- `NoTiFi huB` is the product/model identity and visual brand.
- `Notification Hub` explains what the product is.
- `hub` and `hubs` are the ordinary nouns used in UI instructions and prose.

Example device presentation:

```text
NoTiFi huB
Front · Notification Hub
```

The notification hub receives semantic events, applies local rules, plays
sounds, records recent activity, and may forward directly received sensor
events to peer hubs. It remains useful as a standalone device and does not
require a central controller or cloud service.

## Brand Styling

`NoTiFi` retains its mixed-case brand spelling. Individual product names use a
terminal-cap treatment in the visual mark:

```text
NoTiFi huB
NoTiFi buttoN
NoTiFi motioN
NoTiFi contacT
```

The final capital receives the product accent color in logos, packaging, and
product headers. Wi-Fi-only and LoRa-capable `NoTiFi huB` variants use
distinctly different `huB` accent colors so the communication tier is visible
at a glance. Exact hub colors and the sensor-family color system remain to be
selected.

Color reinforces the model identity but must not be the only differentiator.
Packaging, enclosure markings, accessible UI text, and device metadata should
also identify Wi-Fi-only and LoRa-capable variants explicitly.

Do not force brand capitalization into ordinary grammar:

- Write `hub` and `hubs`, not `huB` and `huBs`, when referring generically to
  devices.
- Write `sensor` and `sensors` normally.
- Avoid pluralizing a product mark. Prefer `NoTiFi huB devices` or simply
  `hubs`.
- Use a plain accessible name such as `NoTiFi Hub, Notification Hub` for screen
  readers, browser tabs and history, bookmarks, logs, or device metadata.

## Separate Model From Deployment

A sensor identity has distinct layers:

| Layer | Purpose | Example |
|---|---|---|
| Brand/model | Stable product-family identity | `NoTiFi motioN` |
| Hardware capability | What the device can observe | Motion or presence |
| Sensing technology | How the current variant works | Radar |
| User label | Where or why this unit is installed | Shop Driveway |
| Semantic type | Event category sent to the hub | `motion` |
| Semantic event | What happened | `detected` |

The product model should normally describe a reusable capability. The user
label and configuration describe the deployment.

For example, identical motion hardware could be configured as:

```text
Model: NoTiFi motioN
Technology: Radar
Label: Shop Driveway
Type: motion
Event: detected
```

or:

```text
Model: NoTiFi motioN
Technology: Radar
Label: Basement Stairs
Type: presence
Event: detected
```

This keeps the product useful when customers find applications that were not
anticipated by its original designer.

## Candidate Sensor Families

`NoTiFi huB` is established by this decision, and capacitive touch is a distinct
`NoTiFi toucH` product direction rather than part of `NoTiFi buttoN`. Other
sensor model names remain working candidates until hardware scope, industrial
design, and product positioning are settled.

| Candidate | General capability | Possible technologies or inputs |
|---|---|---|
| `NoTiFi buttoN` | Deliberate physical activation | Momentary button |
| `NoTiFi motioN` | Motion or presence detection | Radar, PIR |
| `NoTiFi contacT` | Open/closed state | Reed switch, Hall sensor |
| `NoTiFi rangE` | Distance or proximity | Ultrasonic, time-of-flight |
| `NoTiFi tilT` | Orientation or movement | Tilt switch, accelerometer |
| `NoTiFi toucH` | Touch activation | Capacitive touch |

Prefer `NoTiFi motioN` over a technology-specific model such as
`NoTiFi radaR` when the customer is buying the capability rather than the
underlying sensing method. Technology can identify a variant:

```text
NoTiFi motioN
Radar variant
```

Technology-specific naming remains appropriate when the technology materially
changes installation, performance, regulation, or customer expectations.

## Common Sensor Controls

Every NoTiFi sensor product requires two physically reachable base controls:

1. A setup/test/service push button.
2. A user-adjustable event-gain potentiometer.

The shared controls should behave consistently across sensor families:

- Holding the service button during boot clears saved Wi-Fi and sensor settings
  and starts setup mode.
- A short service-button press while running sends a test event.
- The gain potentiometer controls the relative sound level requested for that
  sensor's events; the receiving hub retains final master-volume and safety
  limits.
- Enclosures must preserve practical access to both controls.

Do not silently redefine the common gain potentiometer as a live detection
threshold on selected products. Light, tilt, range, and similar sensors should
prefer a clear service-button "teach" or calibration gesture that stores the
current physical condition as the threshold. A separate product-specific trim
is appropriate when continuous independent threshold adjustment is genuinely
required.

This preserves one meaning for the shared gain control while allowing
sensor-specific calibration behavior. Exact teach gestures, feedback, and any
need for a second trim remain hardware and UX decisions.

## Use Cases And Kits

Deployment language belongs in user labels, setup profiles, examples, and kits.
It should not unnecessarily constrain the reusable sensor model.

Examples:

- `NoTiFi Driveway Kit`: huB plus motioN
- `NoTiFi Mailbox Kit`: huB plus contacT or tilT
- `NoTiFi Entry Kit`: huB plus buttoN and/or contacT
- `NoTiFi Property Kit`: huB plus a mix of sensors

Kit names may be use-case specific because the kit deliberately packages and
explains one outcome. The included sensor retains its capability-based model
identity and can be repurposed by the customer.

## UI Terminology

User-facing copy should distinguish the device from the sound it plays.

| Meaning | Preferred UI language |
|---|---|
| Product identity | `NoTiFi huB` |
| Device category | Notification Hub |
| Generic device reference | hub |
| Another device on the network | peer hub |
| Discover network devices | Scan for Hubs |
| Sensor destination | Hub URL |
| Uploaded audio asset | sound or sound file |
| Current audio selection | Active Sound |
| Start audio playback | Play Sound |
| Add audio | Upload Sound |
| Audible notification behavior | chime |

Examples of eventual UI changes:

- `Doorbell Chime` becomes `NoTiFi huB`.
- `Manage Chimes` becomes `Manage Hub`.
- `Peer Chimes` becomes `Peer Hubs`.
- `Scan for Chimes` becomes `Scan for Hubs`.
- `Upload Chime` becomes `Upload Sound`.
- `No chime loaded` becomes `No sound selected`.
- `Chime URL` becomes `Hub URL`.
- `Forward sensor triggers to peers` becomes
  `Forward direct sensor events to peer hubs`.

The word `chime` remains valid when it describes an audible alert, a
customer-provided chime sound, or a legacy technical interface.

## Compatibility Boundaries

Do not rename the following as part of a UI-copy pass:

- `/chime`
- `_doorbell-chime._tcp`
- existing `doorbell-<label>.local` hostnames
- saved preference and JSON keys
- existing sound filenames such as `/chime.wav`
- internal C++ function, type, and variable names
- repository and sketch paths

These names are already part of working firmware, configured sensors,
bookmarks, scripts, or saved state. They may gain documented aliases in a
future compatibility migration, but existing forms should continue working.

UI text can adopt the new terminology without changing those interfaces. When
technical documentation mentions a legacy identifier, describe it explicitly,
for example:

```text
The hub keeps the legacy `/chime` playback endpoint for compatibility.
```

## Future Device Metadata

Future persistent sensor records may need to distinguish:

```text
product model
hardware capability
sensing technology or variant
stable device identifier
user label
semantic sensor type
semantic event
owner hub
```

Do not overload `sensor`, `type`, or `event` with marketing model names. Those
fields describe the semantic event pipeline and should remain useful across
different hardware generations.

## Migration Order

Apply the terminology in deliberate stages:

1. Review and approve this naming architecture.
2. Update firmware UI copy without changing protocols or saved state.
3. Update setup instructions and sensor portal labels.
4. Update remaining use-case, marketing, hardware, and README prose.
5. Consider optional protocol aliases only if they provide concrete user value.
6. Perform separate trademark, domain, and marketplace research before treating
   candidate names as commercially cleared.

## Open Naming Questions

- Which candidate sensor model names should become final?
- Which exact accent colors distinguish Wi-Fi and LoRa-capable hubs?
- What color system, if any, should distinguish sensor families?
- How should model variants appear on packaging and in device metadata?
- Should the browser title use only `NoTiFi huB` or include
  `Notification Hub` for accessibility and browser-tab clarity?
