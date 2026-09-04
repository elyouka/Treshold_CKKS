#include "benchmark/benchmark.h"
#include <vector>
#include <numeric>

// A trivial function to benchmark: summing a vector
static void BM_VectorSum(benchmark::State& state) {
    // --- setup, not timed ---
    std::vector<int> v(1000, 1);

    // --- timed region ---
    for (auto _ : state) {
        int sum = std::accumulate(v.begin(), v.end(), 0);
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_VectorSum);

BENCHMARK_MAIN();