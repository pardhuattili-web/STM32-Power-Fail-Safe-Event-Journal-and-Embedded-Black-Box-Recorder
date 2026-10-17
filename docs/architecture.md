# Architecture and Storage Design

## Data path

```text
Application event
    |
event_journal API
    |  serialize header + payload + CRC
    |  program header/payload
    |  program commit marker last
    v
Flash-device abstraction (read / program / erase)
    |
SPI NOR flash on STM32 target OR host fault-injection simulator
```

The core has no STM32 HAL dependency and performs no heap allocation. A target driver can provide the three flash callbacks.

## Record format

The current version stores a fixed-size C header followed by up to 32 payload bytes and a one-byte commit marker. The header includes magic, format version, event type, payload length, sequence, timestamp and CRC-32. The CRC covers the header bytes before the CRC field plus the payload. The marker is programmed last; a record is valid only when marker and CRC both validate.

**Portability note:** this first version writes the header as a native C struct. For interoperability across compiler/architecture/ABI changes, serialize fields explicitly in a specified byte order before using the format for long-lived data.

## Programming model

NOR flash generally permits changing programmed bits from 1 to 0, but not 0 to 1 without erasing an erase block. The simulator models this constraint and supports injected operation failures and partial programming. Real flash data sheets may impose additional page-program size, alignment, timing, and page-boundary constraints; the hardware driver must enforce them.

## Recovery

Open scans the flash for structurally valid records, CRC-valid records and commit markers. An incomplete/corrupt region is rejected; the scanner advances to an erase-block boundary because it must not overwrite potentially programmed bytes. Sequence numbers resume from the newest valid record found.

The implementation is an educational baseline, not a complete circular-log allocator. It does not yet reclaim erase blocks, maintain redundant metadata, or guarantee preservation through failures during erase/recycling. Those are explicit follow-up milestones.

## STM32 integration plan

1. Select the exact MCU board and external NOR flash part.
2. Implement a driver around the flash callbacks using SPI.
3. Enforce datasheet-specific write-enable, busy polling, page-program boundaries, and erase alignment.
4. Consider a brownout/power-fail signal and hold-up energy only if supported by the system's power design; software alone cannot finish a write after supply collapse.
5. Test resets at every write stage on hardware and preserve raw evidence.
6. Measure write latency and erase endurance under the expected event rate.

This project is not a certified black-box recorder and must not be treated as a safety-critical event logger.
