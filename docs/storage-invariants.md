# Storage Invariants and Failure Semantics

1. A record is only eligible for recovery when magic, version, payload bound, commit marker, and CRC all validate.
2. Header and payload are programmed before the commit marker.
3. A failed append must not advance the in-memory append pointer or sequence.
4. The recovery scan must not program over a region that may already contain partially programmed bytes.
5. Programming may clear bits from 1 to 0 only; erase is required before 0 to 1 transitions.
6. Append-only storage returns full rather than silently deleting older records.
7. Real flash page constraints and erase-block recycling must be implemented in the hardware/storage layer before production use.

The host simulator validates selected software failure paths. It cannot model every physical failure mode, internal flash-controller behavior, brownout ramp, or undocumented device constraint.
