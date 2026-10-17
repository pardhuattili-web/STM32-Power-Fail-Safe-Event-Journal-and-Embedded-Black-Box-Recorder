# Test Plan

## Automated host tests

| Test | Expected result |
|---|---|
| CRC-32 standard vector | `123456789` -> `0xCBF43926` |
| Append two records and reopen | Records recover and sequence resumes |
| Read latest | Newest committed record is returned |
| Oversize/null payload | API rejects invalid arguments |
| Corrupt payload byte | CRC rejects the record |
| Power cut before payload completes | Record has no valid commit and is not returned |
| Partial payload programming | Torn record is rejected after reopen |

## Physical hardware acceptance checklist

- [ ] Record exact MCU, board, SPI flash part, datasheet revision and supply setup.
- [ ] Verify SPI mode, maximum clock, write-enable, WIP polling and page boundaries.
- [ ] Confirm that all program/erase calls obey part-specific alignment/size requirements.
- [ ] Trigger resets or controlled power removal after header, payload and commit-marker writes.
- [ ] Inject failures during erase/recycling after that feature is implemented.
- [ ] Reopen storage after each interruption and compare recovered sequence/payloads.
- [ ] Check repeated power cycling, empty/full media, bad records and sequence rollover.
- [ ] Measure append latency and estimate endurance from realistic event rate.
- [ ] Save raw logs and setup photos; don't label simulator tests as hardware tests.

## Current known limitations

- Host fault injection only; no physical power interruption has been verified.
- Current allocator is append-only and stops when space is exhausted.
- No erase-block recycling or redundant metadata yet.
- Native-struct record encoding should be changed to explicit endianness before long-term cross-platform use.
