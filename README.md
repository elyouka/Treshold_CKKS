# Threshold CKKS Primitive Benchmarking

Micro-architectural profiling of core threshold CKKS primitives (NTT, Rescale, 
Keyswitch, Bootstrap) using OpenFHE and Google Benchmark

## Overview

This project isolates and benchmarks individual CKKS primitives to analyze 
their runtime and hardware performance characteristics. 

Primitives currently benchmarked:
- NTT (Number Theoretic Transform)
- Rescale
- Keyswitch (Relinearization)
- Bootstrap (threshold)

## Prerequisites

- Linux 
- CMake
- A C++ compiler (e.g. g++/clang) supporting C++17
- [OpenFHE](https://github.com/openfheorg/openfhe-development) — version/commit used: `<fill in>`
- Google Benchmark
- `libpfm` for hardware performance counters via `--benchmark_perf_counters`

## Build

\`\`\`bash
mkdir build && cd build
cmake ..
make
\`\`\`

## Usage

\`\`\`bash
./<your_benchmark_binary> --benchmark_perf_counters=<counters>
\`\`\`


## Known Issues

-The OpenFHE bootstrapping benchmark example might not run because of segment fault
due to insufficient memory on WSL2

## Status

Work in progress

## License

