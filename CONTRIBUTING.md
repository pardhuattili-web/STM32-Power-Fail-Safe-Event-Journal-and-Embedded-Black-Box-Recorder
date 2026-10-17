# Contributing

## Local validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/journal_demo
```

Keep the portable journal core free of STM32 HAL dependencies. Add regression tests for new storage behaviors and fault cases. Record actual compiler output and hardware measurements; never present simulator results as physical power-cut tests.

## Hardware and format changes

Document the flash part, page-program size, erase geometry, SPI configuration, CRC rules, record layout, and recovery behavior. Before long-term storage is used across firmware versions or MCU/compiler variants, use explicit field serialization and a documented byte order.
