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

/*
        Setup
*/

struct boot_config {
    uint32_t ringDim;
    uint32_t slots;
    uint32_t dcrtBits;
    uint32_t firstMod;
    uint32_t numDigits;
    uint32_t lvlsAfter;
    uint32_t iters;
    std::vector<uint32_t> lvlb;
    SecretKeyDist skdst;
    ScalingTechnique stech;
};

[[maybe_unused]] std::vector<boot_config> boot_configs = {
    // ringDm,   slots, dcrtBits, firstMod, numDigits, lvlsAfter, iters,   lvlb,                skdst,                stech
    { 1 << 14, 1 << 13,       52,       57,        16,         9,     2, {3, 3},      UNIFORM_TERNARY,          FIXEDMANUAL},
    { 1 << 15, 1 << 14,       52,       57,        16,         9,     2, {3, 3},      UNIFORM_TERNARY,          FIXEDMANUAL},
    { 1 << 16, 1 << 15,       52,       57,        16,         9,     2, {3, 3},      UNIFORM_TERNARY,          FIXEDMANUAL},
};

[[maybe_unused]] static void BootConfigs(benchmark::internal::Benchmark* b) {
    for (uint32_t i = 0; i < boot_configs.size(); ++i)
        b->ArgName("Config")->Arg(i);
}

[[maybe_unused]] static CryptoContext<DCRTPoly> GenerateCKKSContext(uint32_t n) {

    auto t = boot_configs[n];

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(t.ringDim);
    parameters.SetScalingModSize(t.dcrtBits);
    parameters.SetFirstModSize(t.firstMod);
    parameters.SetNumLargeDigits(t.numDigits);
    parameters.SetSecretKeyDist(t.skdst);
    parameters.SetScalingTechnique(t.stech);
    parameters.SetKeySwitchTechnique(HYBRID);
    uint32_t depth = t.lvlsAfter + FHECKKSRNS::GetBootstrapDepth(t.lvlb, t.skdst) + (t.iters - 1);
    parameters.SetMultiplicativeDepth(depth);
    parameters.SetBatchSize(t.slots);

    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    return cc;
}

[[maybe_unused]] static CryptoContext<DCRTPoly> GenerateTCKKSContext(uint32_t n) {

    auto t = boot_configs[n];

    CCParams<CryptoContextCKKSRNS> parameters;
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(t.ringDim);
    parameters.SetScalingModSize(t.dcrtBits);
    parameters.SetFirstModSize(t.firstMod);
    parameters.SetNumLargeDigits(t.numDigits);
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetScalingTechnique(t.stech);
    parameters.SetKeySwitchTechnique(KeySwitchTechnique::HYBRID);
    uint32_t depth = t.lvlsAfter + FHECKKSRNS::GetBootstrapDepth(t.lvlb, t.skdst) + (t.iters - 1);
    parameters.SetMultiplicativeDepth(depth);
    parameters.SetBatchSize(t.slots);
    auto compressionLevel = CompressionLevel::COMPACT;
    parameters.SetInteractiveBootCompressionLevel(compressionLevel);

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);            //Key-generation, encrypt, decrypt
    cc->Enable(KEYSWITCH);      //Key-switch
    cc->Enable(LEVELEDSHE);     //Somewhat Homomorphic Encryption, +, *
    cc->Enable(ADVANCEDSHE);    //Advanced stuff, rotations, inner products...
    cc->Enable(MULTIPARTY);     //Threshold CKKS

    return cc;
}

/*
        Key-generation setups (context only; key gen happens inside the benchmark loop)
*/

struct CKKSKeyGenSetup {
    CryptoContext<DCRTPoly> cc;

    CKKSKeyGenSetup(uint32_t n) {
        cc = GenerateCKKSContext(n);
    }
};

struct TCKKSKeyGenSetup {
    CryptoContext<DCRTPoly> cc;

    TCKKSKeyGenSetup(uint32_t n) {
        cc = GenerateTCKKSContext(n);
    }
};

/*
        Setup cache
*/

template <typename Setup>
static Setup& GetSetup(uint32_t configIndex) {
    static std::map<uint32_t, std::unique_ptr<Setup>> cache;
    static uint32_t lastKey = std::numeric_limits<uint32_t>::max();
    if (lastKey != configIndex) {
        cache.clear();  // free all previous setups
        lastKey = configIndex;
    }
    auto& slot = cache[configIndex];
    if (!slot) slot = std::make_unique<Setup>(configIndex);
    return *slot;
}


/*
        Key-generation benchmarks
*/

void CKKSrns_KeyGen(benchmark::State& state) {
    uint32_t n = state.range(0);
    auto& setup = GetSetup<CKKSKeyGenSetup>(n);
    state.counters["ring-dim"] = std::log2(boot_configs[n].ringDim);

    while (state.KeepRunning()) {
        state.PauseTiming();
        setup.cc->ClearEvalMultKeys();
        setup.cc->ClearEvalSumKeys();
        state.ResumeTiming();

        auto kp = setup.cc->KeyGen();
        setup.cc->EvalMultKeyGen(kp.secretKey);
        setup.cc->EvalSumKeyGen(kp.secretKey);
        benchmark::DoNotOptimize(kp);
    }
}

BENCHMARK(CKKSrns_KeyGen)->Unit(benchmark::kMillisecond)->Apply(BootConfigs);

void TCKKS_KeyGen(benchmark::State& state) {
    uint32_t n = state.range(0);
    auto& setup = GetSetup<TCKKSKeyGenSetup>(n);
    state.counters["ring-dim"] = std::log2(boot_configs[n].ringDim);

    while (state.KeepRunning()) {
        // Avoid unbounded growth of the eval-key maps across iterations.
        state.PauseTiming();
        setup.cc->ClearEvalMultKeys();
        setup.cc->ClearEvalSumKeys();
        state.ResumeTiming();

        // ---- Round 1 (party A) ----
        auto kp1         = setup.cc->KeyGen();
        auto evalMultKey = setup.cc->KeySwitchGen(kp1.secretKey, kp1.secretKey);

        setup.cc->EvalSumKeyGen(kp1.secretKey);
        auto evalSumKeys = std::make_shared<std::map<usint, EvalKey<DCRTPoly>>>(
            setup.cc->GetEvalSumKeyMap(kp1.secretKey->GetKeyTag()));

        // ---- Round 2 (party B) ----
        auto kp2          = setup.cc->MultipartyKeyGen(kp1.publicKey);
        auto evalMultKey2 = setup.cc->MultiKeySwitchGen(kp2.secretKey, kp2.secretKey, evalMultKey);
        auto evalMultAB   = setup.cc->MultiAddEvalKeys(evalMultKey, evalMultKey2,
                                                       kp2.publicKey->GetKeyTag());
        auto evalMultBAB  = setup.cc->MultiMultEvalKey(kp2.secretKey, evalMultAB,
                                                       kp2.publicKey->GetKeyTag());

        auto evalSumKeysB    = setup.cc->MultiEvalSumKeyGen(kp2.secretKey, evalSumKeys,
                                                            kp2.publicKey->GetKeyTag());
        auto evalSumKeysJoin = setup.cc->MultiAddEvalSumKeys(evalSumKeys, evalSumKeysB,
                                                             kp2.publicKey->GetKeyTag());
        setup.cc->InsertEvalSumKey(evalSumKeysJoin);

        // ---- Round 3 (party A) ----
        auto evalMultAAB   = setup.cc->MultiMultEvalKey(kp1.secretKey, evalMultAB,
                                                        kp2.publicKey->GetKeyTag());
        auto evalMultFinal = setup.cc->MultiAddEvalMultKeys(evalMultAAB, evalMultBAB,
                                                            evalMultAB->GetKeyTag());
        setup.cc->InsertEvalMultKey({evalMultFinal});

        benchmark::DoNotOptimize(kp2);
        benchmark::DoNotOptimize(evalMultFinal);
    }
}

BENCHMARK(TCKKS_KeyGen)->Unit(benchmark::kMillisecond)->Apply(BootConfigs);

BENCHMARK_MAIN();