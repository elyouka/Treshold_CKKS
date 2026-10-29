# Threshold CKKS Primitive Benchmarking

Micro-architectural profiling of core threshold CKKS primitives (NTT, Rescale, 
Keyswitch, Bootstrap) using OpenFHE and Google Benchmark

## Overview

This project isolates and benchmarks individual CKKS primitives to analyze 
their runtime and hardware performance characteristics. 

Primitives currently benchmarked:
- NTT (Number Theoretic Transform)
- Rescale (standard CKKS + threshold CKKS)
- Relinearization (standard CKKS + threshold CKKS)
- Bootstraping
- threshold CKKS bootstrapping
- decryption (standard CKKS + threshold CKKS)
- key generation (standard CKKS + threshold CKKS)

## Folder structure and location of important files

-Benchmark implementations are located under benchmarks/parameters_adjusted, with 
standard CKKS bootstrapping being an exeption (examples/bootstrapping2.cpp)
-plots are located in the folder plots
-plotting scripts in python are located in folder plotting scripts
-results are located in the folder json files

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


## Status

Completed

## License

