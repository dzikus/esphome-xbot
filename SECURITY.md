# Security Policy

## Supported versions

Latest release only.

## Reporting a vulnerability

Use GitHub private vulnerability reporting:
https://github.com/dzikus/esphome-xbot/security/advisories/new

Do not open a public issue.

Include the component version or commit, the ESPHome version, the vehicle and
its advertised name, the configuration and the node log.

There is no response-time commitment.

## Scope

The code in this repository.

## Out of scope

- ESPHome, ESP-IDF and Home Assistant. Report to those projects.
- The vehicle's BLE protocol. It has no authentication or encryption and the
  vehicle does not pair or bond. Any BLE central in range can read its
  telemetry and write its settings, including the lock and the speed limits,
  without this component.
- Access control on the entities. The `number`, `select`, `switch` and `lock`
  platforms write to the vehicle without checking whether it is moving. Who can
  use them is set in Home Assistant. See [Safety](README.md#safety).
