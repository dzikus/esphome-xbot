# Changelog

## v1.3.0 (2026-11-24)

- The ESPHome floor is now 2026.11.0. ESPHome 2026.11 requires a `relation` on
  every entry under `esphome: devices:` and 2026.10 rejects that key, so one
  yaml cannot serve both; give each sub-device `relation: via`.
- Builds on ESPHome 2026.11, where `status_momentary_warning` with a name is
  deprecated.
- Code changes for the clang-tidy 23 findings.
- Dependency updates.

## v1.2.1 (2026-09-13)

- No change to the component.
- CI: tool pins read from the requirements file, per-job permissions, workflow
  audit and dependency review on pull requests, CodeQL and Scorecard.
- Dependency updates.

## v1.2.0 (2026-08-31)

- Fixes found by clang-tidy and the ESP32 warning gate; C++20 constructs where
  the language has them.
- Tests for the entity tables against the poll table and for the schema
  helpers and validators.
- CI: clang-tidy, a gate that fails on any component warning in the ESP32
  build, one set of gates for tags and pull requests.

## v1.1.0 (2026-08-16)

- `name_prefix` is opt-in. A node with several hubs gets a warning instead of
  a derived prefix.
- The oldest ESPHome release tested in CI is 2026.2.0.
- CI pins its pip installs.

## v1.0.0 (2026-08-13)

- First release.
