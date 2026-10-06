#define _USE_MATH_DEFINES

#include "benchmark/benchmark.h"
#include "math/hal/basicint.h"
#include "scheme/ckksrns/gen-cryptocontext-ckksrns.h"
#include "scheme/bfvrns/gen-cryptocontext-bfvrns.h"
#include "scheme/bgvrns/gen-cryptocontext-bgvrns.h"
#include "gen-cryptocontext.h"
#include "cryptocontext.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>

using namespace lbcrypto;

[[maybe_unused]] static void RingArgs(benchmark::internal::Benchmark* b) {
    for (uint32_t r : {1024, 4096, 8192, 16384, 32768, 65536})
        b->ArgName("ringdm")->Arg(r);
}

[[maybe_unused]] static void NativeNTT(benchmark::State& state) {
    uint32_t n = state.range(0);
    uint32_t m = n << 1;

    NativeInteger modulusQ(LastPrime<NativeInteger>(MAX_MODULUS_SIZE, m));
    NativeInteger rootOfUnity = RootOfUnity(m, modulusQ);

    DiscreteUniformGeneratorImpl<NativeVector> dug;
    NativeVector x = dug.GenerateVector(n, modulusQ);
    NativeVector X(n);

    ChineseRemainderTransformFTT<NativeVector> crtFTT;
    crtFTT.PreCompute(rootOfUnity, m, modulusQ);

    for (auto _ : state)
        crtFTT.ForwardTransformToBitReverse(x, rootOfUnity, m, &X);

    state.SetComplexityN(state.range(0));
}

BENCHMARK(NativeNTT)->Unit(benchmark::kMicrosecond)->Apply(RingArgs);          // ->Complexity(benchmark::oAuto);

BENCHMARK_MAIN();