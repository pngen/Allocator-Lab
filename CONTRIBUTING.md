# Contributing to Allocator Lab

Thanks for contributing. Allocator Lab is a measurement and experimentation laboratory; contributions that improve measurement rigor, reproducibility, or adversarial coverage are especially welcome.

## Build

Prerequisites: MSVC (Visual Studio Build Tools 2022 or newer), CMake >= 3.21, and an optional CUDA toolkit for the CUDA backends.

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`/W4` and `/WX` are enabled on MSVC. Zero warnings is a hard requirement: a new change must build cleanly in both Release and Debug.

## Tests

The test suite lives in `tests/` and is compiled into `allocator-lab-tests`. Run it directly or via `ctest`. A per-name filter is available through the `AL_TEST_FILTER` environment variable.

## Guidelines

- Keep the public API stable and strongly typed.
- Never fabricate success: an operation a backend cannot perform must return a structured `not_supported` / `no_backend` error.
- Report granted sizes consistently from `allocate` and `query`.
- Any new allocator must be thread-safe or explicitly reject concurrent use and document that.
- Add tests for new allocators: correctness, accounting-to-zero, capacity, alignment, and adversarial patterns.
- Do not add test timeouts.

## License

By contributing you agree that your contribution is licensed under the terms of the Apache License 2.0 included in this repository.
