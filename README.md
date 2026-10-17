# STM32 Power-Fail-Safe Event Journal & Embedded Black-Box Recorder

[![CI](https://github.com/pardhuattili-web/STM32-Power-Fail-Safe-Event-Journal-and-Embedded-Black-Box-Recorder/actions/workflows/ci.yml/badge.svg)](https://github.com/pardhuattili-web/STM32-Power-Fail-Safe-Event-Journal-and-Embedded-Black-Box-Recorder/actions/workflows/ci.yml)
![C11](https://img.shields.io/badge/language-C11-blue)
![Platform](https://img.shields.io/badge/platform-host%20%2B%20STM32%20port-lightgrey)
![Validation](https://img.shields.io/badge/validation-host%20fault%20injection-orange)

A portable Embedded C event journal designed to preserve critical event records across interrupted flash writes. The core uses an append-only record format, CRC-32, a commit marker written last, sequence numbers, and boot-time recovery. A host-side NOR flash simulator models 1-to-0 programming and injects operation failures or partial programming to test recovery without a physical board.

> **Validation status:** host simulation and software tests are the current milestone. No physical flash device or real power-cut behavior has been validated. The allocator is append-only and currently does not reclaim erase blocks when full.

## Features

- Portable flash-device interface with read/program/erase callbacks.
- Versioned event records with sequence numbers, timestamps, payload length, and CRC-32.
- Commit marker programmed after header and payload so torn writes are not treated as committed.
- Recovery scan after simulated reboot.
- NOR simulator enforces 1-to-0 programming and supports power-cut injection.
- Host unit tests for valid records, CRC corruption, invalid input, and interrupted writes.
- CMake, CTest, warnings-as-errors, and GitHub Actions CI.

## Architecture

```text
Application event
      |
Event journal: serialize + CRC
      |
Write header -> write payload -> write commit marker last
      |
Flash interface (read / program / erase)
      |
SPI NOR driver on STM32 OR simulated NOR for host tests
      |
Boot recovery scan -> newest valid committed record
```

## Repository structure

```text
include/event_journal.h        Portable API and device contract
src/event_journal.c            Record creation, CRC, validation, recovery
simulator/fake_nor_flash.h     Simulator API and fault controls
simulator/fake_nor_flash.c     NOR semantics and partial-write injection
tests/test_event_journal.c     Automated tests
app/main.c                     Host demonstration
docs/architecture.md           Record format and recovery model
docs/test-plan.md              Fault matrix and hardware checklist
docs/benchmarks.md             Validation record template
.github/workflows/ci.yml       Build and test workflow
```

## Build and run

Requirements: CMake 3.16+, GCC or Clang with C11 support.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/journal_demo
```

The default build uses `-Wall -Wextra -Wpedantic -Werror`.

## Event record and commit protocol

Each record contains a magic value, format version, event type, payload length, sequence number, timestamp, CRC-32, payload (up to 32 bytes), and one-byte commit marker. The CRC covers the header fields preceding the CRC field and the payload. The marker is written last, and recovery accepts a record only when the structure, marker, and CRC validate.

This prototype writes the header as a native C struct. Before using long-lived data or supporting different compilers/architectures, replace it with explicit field-by-field serialization and a documented byte order. Real flash drivers must also obey their chip's page-program, alignment, write-enable, busy-polling, and erase requirements.

## Fault injection

The simulated NOR backend allows tests to interrupt programming after a chosen number of operations or after a partial number of bytes. Unit tests demonstrate that incomplete records are rejected after reopening the journal. These are deterministic host simulations, not physical power-failure measurements.

## Known limitations and roadmap

- [x] Portable journal core and CRC-32
- [x] Commit-marker record protocol
- [x] Boot-time scan and recovery
- [x] NOR flash host simulator and fault injection
- [x] CMake, CTest and GitHub Actions
- [ ] Explicit portable on-flash serialization and endianness
- [ ] Erase-block recycling / circular-log policy
- [ ] Redundant metadata and sequence-wrap policy
- [ ] STM32-specific SPI NOR driver
- [ ] Hardware reset/power-cut tests and logic-analyzer evidence
- [ ] Append latency and endurance measurements

## Intended use and safety

This is an educational embedded-storage prototype, not a certified automotive recorder. It must not be used as the sole recorder for safety-critical events. Software alone cannot guarantee completion of a flash write after supply voltage collapses; real systems need an appropriate power design and must validate brownout behavior.

## License

MIT. See [LICENSE](LICENSE).
