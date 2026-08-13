# esphome-xbot

ESPHome external component for electric scooters built around an XBOT / LebiTEC
controller (Guangzhou Lebi Robotics). It is an OEM part, so it turns up unbadged
inside scooters sold under other brands, and on its own as an aftermarket
replacement for 10-inch scooters.

The brand on the deck does not decide whether this works. The name the vehicle
advertises over BLE does, and some of those names are turned away. See
[Which vehicles](#which-vehicles) before buying hardware for this.

It connects over BLE, reads telemetry and settings, and exposes both to Home
Assistant. It also writes settings back. Read [Safety](#safety) before enabling
the write platforms.

The link is held open rather than reopened each time, and the poll interval
decides how often the registers are re-read over it. That keeps a parked vehicle
awake and reachable, and it means the vendor app cannot connect until the
Bluetooth switch releases the link.

That also suppresses the vehicle's own idle shutdown. On the vehicle this was
tried on, a held link kept it awake well past the five minutes it claims to
power off after. Switch the link off if it is going to sit for a while.

## Status

Verified on a single 36 V vehicle.

The transport is picked from the discovered GATT services. Five profiles are
implemented; one has been seen on hardware. The protocol variant comes from the
name the vehicle advertises, and it decides whether frames go out obfuscated or
in the clear.

Only one variant is decoded. A vehicle whose name belongs to another one is
identified and logged, then dropped without a frame being sent.

A name matching no pattern at all is not in that category. It falls back to the
decoded variant and is then treated exactly like a verified vehicle, including
being written to if the write platforms are configured. Read
[Safety](#safety) before pointing this at a scooter it has never seen.

Register decoding is fixed. It was read off one controller, and nothing checks
that another puts the same values in the same registers.

Not covered: firmware update, battery management data (this controller reports
none), gyro calibration, the light colour mode. Trip time and motor temperature
are read but this controller never fills them in, so both entities are off
unless asked for.

## Which vehicles

Read the name your scooter advertises with any BLE scanner. This component also
prints it on every connection, next to the variant it resolved to.

Patterns below are matched as substrings anywhere in the name, case-sensitive,
in the order given, and the first hit decides. A scooter advertising `M0Clean`
is turned away even though the name contains `M0`, and one advertising `TK2-S`
is accepted even though it contains `TK`.

`<space>` marks a pattern that begins with a literal space, which is part of
what has to match.

The table describes what this component does with a name, not what those
vehicles were measured to speak. Only the one in [Status](#status) has been
tried. The patterns and the variants behind them were read out of the vendor
app, so a row saying `TK` is turned away means this component will refuse it,
on the strength of that reading alone.

| Name contains | Result |
|---|---|
| `X0Robot` | works, frames sent in the clear |
| `PRO-II`, `TK2-S`, `TK2_Y`, `M1 MAX`, `X2`, `CURTIS`, `MSEnery`, `smart` | works |
| `TK` | turned away (variant 7) |
| `XRIDER`, `NXRIDE` | works |
| `M0Clean` | turned away (variant 6) |
| `XBOT_AGV` | turned away (variant 1) |
| `M6` | turned away (variant 0) |
| `MiniRobot` | turned away (variant 1) |
| `GoKart` | turned away (variant 4) |
| `N3MTenbot`, `<space>MiniPro`, `<space>Ninebot`, `Ninebot` | turned away (variant 1) |
| `miniPLUS_` | turned away (variant 1) |
| `M5Robot` | turned away (variant 1) |
| `M1`, or `X` as the first or second character | 7 characters or more: turned away (variant 1). 6 or fewer: works |
| `A6Robot` | turned away (variant 2) |
| `M0`, `SFSO` | works |
| `MIScooter` | works, frames sent in the clear |
| `W1` | turned away (variant 5) |
| `Plus` | turned away (variant 1) |
| anything else | works |

Turned away means the component logs the name, the variant it resolved to, and
the address and opcode that variant would have needed, then drops the link. It
never subscribes and never sends a protocol frame, so nothing reaches the
controller except the GATT read that fetched the name. The component also marks
itself failed, which is what is still visible once the log has scrolled; Poll
Now and the Bluetooth switch both clear it.

It then stops trying altogether. The name will not change between polls, so
reconnecting every interval would only keep waking the vehicle and holding a
connection slot. Pressing Poll Now, or switching the Bluetooth switch off and
on again, asks for another look. Use it after a firmware update renamed the
vehicle, or when a different one took over that `ble_client`. With neither
platform configured there is no way back short of restarting the node.

Brand names are no guide here. `MiniRobot`, which the paperwork shipped with
some of these controllers names as the app to pair with, resolves to a variant
this component does not decode. Other brands carrying the same controller have
not been checked in either direction.

Stopping applies to the protocol variant only. A device whose GATT services
match none of the five transport profiles, which is also what pointing
`ble_client` at the wrong address looks like, is retried every interval for as
long as the node runs. Each attempt connects to whatever is at that address and
holds a connection slot for up to thirty seconds. The log says `no known
transport profile on this device` once per attempt.

A vehicle whose name cannot be read at all, because it exposes no GATT device
name or the read fails, proceeds with the decoded variant, and writing is held
back until a frame decodes. That is not the same as a name that read and matched
no pattern, which is written to straight away.

Once a name has been read successfully, the variant it resolved to is kept for
as long as the node runs. A later connection whose name read fails then carries
on with the answer that worked instead of falling back. That matters for
`X0Robot` and `MIScooter`, which speak the decoded variant but want their frames
in the clear, and for them the fallback would be wrong.

If the vehicle answers with bytes that will not assemble into frames, the log
says which header they carried, raw and under the key in use, instead of
dropping them without a word. A vehicle that answers nothing at all cannot be
told apart from one out of range. The cycle line reports `notifies=0, frames=0`
and that is all there is to go on.

## Why this exists

No Home Assistant or ESPHome integration for these controllers existed.
Checked against the HACS default store, the ESPHome component index,
Gadgetbridge, GitHub repo and code search, PyPI and npm. The adjacent projects
all target different protocols: Xiaomi M365, Ninebot/Segway, Silence, NIU, or
generic smart-BMS chips (JBD, JK, Daly, ANT).

## Installation

```yaml
external_components:
  - source: github://dzikus/esphome-xbot
    components: [xbot]
```

Pin a release instead of tracking the default branch, using the newest tag from
the releases page:

```yaml
external_components:
  - source: github://dzikus/esphome-xbot@v0.0.0   # a real tag from Releases
    components: [xbot]
```

See [example.yaml](example.yaml) for a complete node.

## Configuration

| Option | Type | Default | Meaning |
|---|---|---|---|
| `id` | id | generated | Name it if a platform has to point at it. |
| `ble_client_id` | id | generated | The `ble_client` entry for the vehicle. Needed once there is more than one. |
| `update_interval` | time | `300s` | How often the registers are re-read over the held link. Minimum 60 s. |
| `profile` | enum | `auto` | `auto`, `nus`, `ae00`, `ffe0`, `fff0-f3f7`, `fff0-f2f1`. Pin only if auto-detection picks wrong. |
| `probe_repeat` | int 1-5 | `2` | How many times each register block is asked for per sweep. A lost request is indistinguishable from a register the vehicle does not have, and nothing reports the loss, so the default asks twice. On the link this was measured on, one ask answered 19.8 blocks out of 20 and halved the sweep; set `1` if your link is good. |
| `obfuscation_key` | int 0-255 | taken from the vehicle name | The byte every frame is xored with. The name decides it, and this overrides that decision. Set it only when the log says `nothing parses` and names a key that did: `framing found in the clear` means `0`. Guessing here produces a vehicle that answers nothing, which looks the same as one that is switched off. |
| `name_prefix` | string, max 48 chars | derived when more than one hub is configured, empty otherwise | Prepended to every default entity name on this hub, so two vehicles do not both call a sensor `Battery Level`. Derived from the hub id with a leading `xbot_` stripped, so `xbot_scooter_b` gives `Scooter B Battery Level`. Set it explicitly to choose the wording, or to `""` to opt out. Names you write yourself are never touched. |

Every entity platform takes `xbot_id` to say which vehicle it belongs to, and
`device_id` to place its entities on a sub-device.

Several vehicles can share one node, each with its own `ble_client`; two hubs
pointing at the same one are rejected at validation. The component is
`MULTI_CONF`, and stored state is keyed per vehicle.

With more than one vehicle, give each platform block its own `device_id`
pointing at an entry under `esphome:` `devices:`. Every vehicle gets the same
default entity names, so without that they collide and validation fails with a
duplicate-name error for each one.

The names themselves also need separating, for a reason that is easy to miss.
An entity's api key is a hash of its name alone, with no device in it, so two
vehicles carrying an entity of the same name share a key.

Whether that collides depends on how the state is consumed, and only the native
api has been used here. The prefix removes the question rather than answering
it.

So once more than one hub is configured, each one's default entity names are
prefixed, taking the prefix from the hub id with a leading `xbot_` stripped:

```
hub_a -> "Hub A Battery Level"        xbot_scooter_b -> "Scooter B Battery Level"
```

Set `name_prefix` on each hub to choose the wording:

```yaml
xbot:
  - id: hub_a
    ble_client_id: ble_a
    name_prefix: "Scooter A"
  - id: hub_b
    ble_client_id: ble_b
    name_prefix: "Scooter B"
```

Adding a second vehicle renames the first one's entities, because the prefix
only appears once there is something to tell apart. Home Assistant then sees new
entity ids, and history, dashboards and automations built on the old ones stop
following. Put `name_prefix: ""` on the vehicle that was there first to keep its
names as they are.

Set it to `""` to keep the bare names, which is safe if you only use the native
API. A single-vehicle node is never prefixed. It cannot collide with itself, and
prefixing it would rename entities that are already in use.
Names you write out yourself are never touched either way.

Each vehicle still needs its own `device_id` so the duplicate-name check passes:

```yaml
esphome:
  devices:
    - id: dev_a
      name: Scooter A
    - id: dev_b
      name: Scooter B

sensor:
  - platform: xbot
    xbot_id: hub_a
    device_id: dev_a
  - platform: xbot
    xbot_id: hub_b
    device_id: dev_b
```

## Entities

Every platform creates its full set from a bare stanza. Names and units below
are the defaults for a node with one vehicle; with more than one they carry the
prefix described above. To change one, write its yaml key with the options under
it; to leave one out, set it to false:

```yaml
sensor:
  - platform: xbot
    speed:
      name: How fast          # renamed
    warning_code: false       # not created at all
    battery_level: true       # created with its defaults, said out loud
```

The yaml keys are the display names lowercased with underscores, so `Total
Distance` is `total_distance` and `Sport Speed Limit` is `sport_speed_limit`.
Three do not follow from the table: `connected` for BLE Connected, `voltage` for
Battery Voltage and `current` for Battery Current.

| Platform | Entities |
|---|---|
| `sensor` | Total Distance (km), Operating Time (s), Battery Level (%), Battery Voltage (V), Battery Current (A), Temperature, Trip Distance (km), Speed (km/h), Power (W), Error Code, Warning Code, Wheel Factor (hidden), and off unless asked for: Trip Time (s), Motor Temperature |
| `binary_sensor` | BLE Connected |
| `text_sensor` | Controller Version |
| `switch` | Bluetooth, Light, Zero Start, Cruise Control |
| `number` | Sport Speed Limit, Drive Speed Limit, Eco Speed Limit, Cruise Speed |
| `select` | Speed Unit, Riding Mode |
| `button` | Poll Now |
| `lock` | Lock |

The Bluetooth switch releases the link. The vehicle accepts one connection at a
time, so the vendor app cannot reach it while this node is connected.

The four `number` entities take `min_value`, `max_value` and `step`. The
defaults are one vehicle's per-mode ranges, so set your own if your controller
allows something else:

```yaml
number:
  - platform: xbot
    sport_speed_limit:
      max_value: 25        # a 25 km/h vehicle
    cruise_speed:
      step: 0.5
```

A limit reported from outside those bounds is dropped rather than shown, with
the register and the reading in the log. It is not a value the slider could have
produced, so it means a wrong scale or a wrong register, or bounds that are
wrong for your controller.

Sensor readings are bounded the same way, against what the quantity can
physically be. A cumulative counter that takes one wrong reading keeps it in
long-term statistics, and a persisted one writes it to flash. They were set
against one vehicle, so a controller reporting a wider range legitimately would
have those readings dropped. Every drop is logged.

A bare stanza does not create Trip Time or Motor Temperature. Neither says
anything on the vehicle this was tried on, in two different ways. Trip Time's
register holds the not-available marker whether the vehicle is moving or parked,
so the entity stays unknown for good. Motor Temperature's reads a flat zero
while the controller's own probe, in the same frame, reads 34 degrees, which is
what a motor with no sensor on it looks like.

Both are kept because those registers are where the vendor app reads them from,
and another controller may fill them in. Turn either on and watch whether it
moves:

```yaml
sensor:
  - platform: xbot
    trip_time: true
    motor_temperature: true
```

Total Distance, Operating Time, Trip Time, Trip Distance, Battery Level, Wheel
Factor and every setting survive a reboot and a vehicle that is out of range.
Battery Voltage, Battery Current, Temperature, Motor Temperature, Speed, Power,
Error Code, Warning Code and Controller Version do not, because a stale
instantaneous reading passed off as fresh is worse than none. Flash is written
once the vehicle has been unreachable for ten minutes, not on every disconnect.

## Safety

A scooter is a vehicle, and this component writes to it.

The `number`, `select`, `switch` and `lock` platforms change how it behaves on
the road: the three riding-mode speed limits, the cruise speed, the riding mode,
zero start, cruise control, the lights, and the lock itself. Setting the Sport
limit to the top of its default range makes the vehicle do 35 km/h. Turning the
lock off releases it. Neither asks for confirmation, and an automation can do
either.

Nothing in this component asks whether the vehicle is moving. No write path
reads the speed sensor, so the lock, the riding mode and the speed limits can
all be changed while somebody is riding. Speed is read as a sensor and no write
path consults it. What a controller does with a lock it receives at speed has
not been tested here and is not known. Anything that can reach these entities
can do this: an automation, a scene, a dashboard press, or a voice assistant
acting on "lock the scooter". If that matters to you, keep the writing platforms
out of your configuration, or restrict who can reach them in Home Assistant.

The registers written are fixed, not negotiated. They were read off one vehicle,
and nothing checks that another controller keeps its settings in the same
places. On a vehicle that resolves to the decoded variant but arranges its
registers differently, a write lands wherever that number means something else.

The speed limit sliders default to the ranges that vehicle reported; see
[Entities](#entities) for the `min_value` and `max_value` keys. Widening one has
only ever been tested downward, so what a controller does with a limit above its
own ceiling is unknown. Raise them against a vehicle you are willing to test on.

The protocol has no authentication and no encryption in either direction, and
the vehicle does not pair or bond. Anything within radio range can read its
telemetry and write its settings, the lock and the speed limits included,
without this component. The trailing checksum is the complement of a byte sum
over the frame: it catches a corrupted frame, not a crafted one. This component
identifies the vehicle by its address alone; the protocol offers nothing else to
go on.

Some writes are held back, and one case that sounds as if it should be is not. A
vehicle whose name resolves to a variant this component cannot decode is dropped
before anything is sent. A vehicle whose name could not be read at all is only
written to once a frame has decoded. But a name that matched no pattern falls
back to the decoded variant and is written to on that alone, with nothing having
been decoded first, which is the case covered in [Which vehicles](#which-vehicles).
A speed limit waits for the wheel factor its scale depends on. A write into a
full radio buffer is refused. A read is held instead, and goes out
anyway if the radio stays busy.

Writes are sent without waiting for an acknowledgement, so an entity first shows
what was asked for and not what arrived. Two seconds after a write the component
re-reads the block that register lives in, and the reply reaches the entity by
the same path a sweep uses, so a write that never landed corrects itself. Until
that reply, including for the lock, the displayed state is the request and
nothing has confirmed it.

If you want telemetry only, leave `number`, `select` and `lock` out. `button` is
safe to keep, since its one entity asks for a fresh re-read and writes no
setting. It does also retry a vehicle that was turned away for an unsupported
variant.

The `switch` platform is the awkward one. Three of its four entities write to
the vehicle, but the fourth is the Bluetooth switch, and that is the only way to
hand the link back to the vendor app. Keep the platform and drop the three:

```yaml
switch:
  - platform: xbot
    light: false
    zero_start: false
    cruise_control: false
```

Leaving the whole platform out is also fine if you never need the vendor app.
The radio then runs permanently enabled, and short of a reboot nothing can
release the link for anything else to connect.

The controller also has a firmware update path for six separate boards,
including the Bluetooth module this component talks through. None of it is
implemented here and none of it is planned.

## Development

```bash
(cd tests && pio test -e native)      # host unit tests, no hardware needed
esphome config .intellisense.yaml     # config validation against a local checkout
```

A devcontainer is included. It installs esphome, platformio and pre-commit, and
generates C++ IntelliSense settings from a real build on every start.

## License

GPL-3.0. This component builds on GPL-3.0 ESPHome components and inherits that
license. See [LICENSE](LICENSE).
