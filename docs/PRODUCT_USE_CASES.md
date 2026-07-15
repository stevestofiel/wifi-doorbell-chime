<!-- SPDX-License-Identifier: MIT -->

# Product Use Cases

This document captures practical use cases, sensor ideas, and audience framing
for the smart chime system. The goal is to describe real problems the product
can solve without forcing every customer to understand the underlying smart-home
or sensor architecture.

## Core Framing

The same system can be described in two different ways depending on the
audience.

For smart-home users:

```text
A local audio notification endpoint for the smart home.
```

For everyday users:

```text
A customizable wireless alert system.
```

Both audiences use the same core product:

```text
event -> rule -> sound
```

## Audience Paths

### Smart-Home And Platform Users

These users may already use Home Assistant, Homey, UniFi Protect, MQTT,
webhooks, camera detections, automations, dashboards, or local network tools.

They care about:

- Local endpoints.
- Webhooks.
- Rules and event history.
- Custom sounds.
- Camera, person, vehicle, package, or line-crossing detections.
- Integrations without requiring a cloud service.
- Clear event fields such as `sensor`, `type`, `event`, `input`, and `eventId`.

Product language:

```text
Smart Chime Hub
Local Audio Notification Endpoint
Custom Sound Webhook Chime
Automation-Friendly Smart Chime
```

### Standalone Alert Users

These users may not have, want, or understand a smart-home platform. They care
about simple outcomes.

They care about:

- Mail arrived.
- Someone is in the driveway.
- The gate opened.
- Water is detected.
- The garage door opened.
- A doorbell button rings the chime.
- A shop, shed, or outbuilding has activity.

Product language:

```text
Wireless Alert System
Custom Chime Kit
Mailbox Alert Kit
Driveway Alert Kit
Water Leak Chime Kit
Gate Alert Kit
```

## Use Case Categories

### Arrival

Events that tell the user something or someone arrived.

- Front door button.
- Side door button.
- Driveway vehicle detected.
- Driveway person detected.
- Mailbox opened.
- Package box opened.
- Camera package/person detection.

Example sounds:

- Classic doorbell.
- Soft mail tone.
- Low driveway alert.
- Package arrival sound.

### Access

Events that tell the user something opened, moved, or changed state.

- Door contact sensor.
- Window contact sensor.
- Gate contact sensor.
- Garage side door.
- Shop or shed door.
- Package box lid.
- Pool gate.

Example sounds:

- Gate chime.
- Shop entry tone.
- Garage alert.
- Pool gate warning.

### Safety

Events that may need faster attention.

- Water leak sensor.
- Freezer or refrigerator door left open.
- Motion after hours.
- Help button.
- Basement water.
- Laundry area leak.
- Water heater leak.

Example sounds:

- Water leak alarm.
- Repeating freezer alert.
- Help button tone.
- Security alert.

Some safety events should become persistent alerts rather than one-time chimes.
For example:

- Freezer or refrigerator open should repeat until closed.
- Water leak should repeat until dry or cleared.
- Garage or gate left open should repeat until closed.
- Help button should repeat until acknowledged or cleared.

Future alert policies could support:

```text
play once
repeat until clear
escalate if unresolved
optional all-clear sound
```

### Property And Outbuildings

Events for larger properties, detached buildings, or outdoor areas.

- Driveway sensor.
- Gate open.
- Shed motion.
- Barn or shop door.
- Remote call button.
- Outbuilding motion.

These are strong candidates for future LoRa or range pod support when Wi-Fi is
unreliable.

### Automation And Camera Events

Events from software integrations rather than physical sensors.

- UniFi Protect person detected.
- UniFi Protect vehicle detected.
- UniFi Protect package detected.
- Home Assistant automation.
- Homey flow.
- MQTT event.
- Plain webhook.

These use cases make the chime useful even before the customer buys remote
sensors.

## Per-Sensor Sound Prominence

Every event can have its own sound, and some events should also have their own
relative loudness. A driveway motion sensor might be subtle, a mailbox alert
might be soft, and a doorbell button might remain prominent, all while the
chime's master volume stays under the user's control.

Future remote sensors could include a low-profile trim potentiometer that sends
event `gain` with the trigger. This should be a set-and-forget installer or
service adjustment for compact sensors, not a bulky everyday control. The chime
should treat it as a relative event hint and combine it with local rules and
master volume, rather than letting a sensor override the receiver's final
volume setting.

## Realistic Sensor Ideas

High-priority physical sensors:

- Wireless doorbell button.
- Button/touch sensor.
- Door/window contact.
- Mailbox open sensor.
- Gate open sensor.
- Water leak sensor.
- Radar/PIR motion sensor.
- Package box open sensor.

Later or specialized sensors:

- Driveway vehicle sensor.
- Freezer/fridge door-open sensor.
- Vibration sensor.
- Temperature alert sensor.
- Help/call button.
- Dog/back-door request button.
- Shop/customer entry sensor.

## Kit Concepts

### Wireless Doorbell Kit

- Wi-Fi chime.
- Wireless button/touch sensor.
- Default doorbell sound.
- Optional uploaded sounds.

### Mailbox Alert Kit

- Wi-Fi chime.
- Mailbox open or flag sensor.
- Soft mail arrival sound.

### Driveway Alert Kit

- Wi-Fi or future LoRa/range sensor.
- Radar, PIR, magnetometer, or other driveway-specific detector.
- Distinct driveway alert sound.

### Water Leak Chime Kit

- Wi-Fi chime.
- Water leak sensor.
- Urgent sound rule.

### Gate / Shop / Shed Alert Kit

- Wi-Fi or future LoRa/range sensor.
- Contact or motion sensor.
- Custom gate/shop/shed sound.

### Smart Home Chime Kit

- Wi-Fi chime.
- Documented HTTP endpoints.
- Example Home Assistant, Homey, UniFi Protect workflow, MQTT, and webhook
  patterns after integrations are validated.

## Product Strategy Notes

- Sell outcomes to everyday users, not protocols.
- Sell flexibility and local control to smart-home users.
- Keep the underlying event model shared across all paths.
- Avoid too many product variants by treating use cases as kits/configurations.
- Keep LoRa/range support as an expansion path for distance, not a separate
  product universe.
- Preserve the simple core promise: every event can have its own sound.
