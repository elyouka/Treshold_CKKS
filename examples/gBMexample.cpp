#include <benchmark/benchmark.h>
#include "openfhe.h"

using namespace lbcrypto;

// ============================================================
// Fixture: sets up crypto context + keys once per benchmark run.
// NOTE: with --benchmark_repetitions > 1, SetUp/TearDown rerun
// for every repetition (incl. bootstrap keygen) — expected, but
// slow. Reduce repetitions if that becomes annoying.
// ============================================================
class CKKSFixture : public benchmark::Fixture {
public:
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kp;
    Ciphertext<DCRTPoly> ctBase;

    // Bootstrap-specific context (needs much larger depth budget,
    // so it's kept separate from the plain rescale/keyswitch context
    // to keep those benchmarks representative of a "normal" level).
    CryptoContext<DCRTPoly> ccBoot;
    KeyPair<DCRTPoly> kpBoot;
    Ciphertext<DCRTPoly> ctBoot;

    void SetUp(const ::benchmark::State& state) override {
        // ---- Plain context for NTT / Rescale / KeySwitch ----
        CCParams<CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(5);
        parameters.SetScalingModSize(50);
        parameters.SetBatchSize(8);

        cc = GenCryptoContext(parameters);
        cc->Enable(PKE);
        cc->Enable(KEYSWITCH);
        cc->Enable(LEVELEDSHE);

        kp = cc->KeyGen();
        cc->EvalMultKeyGen(kp.secretKey);

        std::vector<double> x = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8};
        auto pt = cc->MakeCKKSPackedPlaintext(x);
        ctBase = cc->Encrypt(kp.publicKey, pt);

        // ---- Separate context for Bootstrap ----
        // Only build it for the Bootstrap benchmark to avoid paying
        // this (expensive) setup cost for every other benchmark too.
        if (state.name().find("Bootstrap") != std::string::npos) {
            uint32_t levelBudget    = 4;   // levels for approx eval of the modular reduction step
            uint32_t bootstrapDepth = 3;   // extra depth consumed by bootstrap internals (approx.)
            usint depth = levelBudget * 2 + bootstrapDepth + 1;  // rough rule of thumb, tune per params

            CCParams<CryptoContextCKKSRNS> bootParams;
            bootParams.SetMultiplicativeDepth(depth);
            bootParams.SetScalingModSize(50);
            usint ringDim = ccBoot->GetRingDimension();
            usint numSlots = ringDim / 2;          // full packing
            bootParams.SetBatchSize(numSlots);

            ccBoot = GenCryptoContext(bootParams);
            ccBoot->Enable(PKE);
            ccBoot->Enable(KEYSWITCH);
            ccBoot->Enable(LEVELEDSHE);
            ccBoot->Enable(ADVANCEDSHE);
            ccBoot->Enable(FHE);

            std::vector<uint32_t> levelBudgetVec = {levelBudget, levelBudget};
            ccBoot->EvalBootstrapSetup(levelBudgetVec);

            kpBoot = ccBoot->KeyGen();
            ccBoot->EvalMultKeyGen(kpBoot.secretKey);
            ccBoot->EvalBootstrapKeyGen(kpBoot.secretKey, numSlots);

            auto ptBoot = ccBoot->MakeCKKSPackedPlaintext(x);
            ctBoot = ccBoot->Encrypt(kpBoot.publicKey, ptBoot);
        }
    }

    void TearDown(const ::benchmark::State&) override {
        cc.reset();
        ccBoot.reset();
    }
};

// ============================================================
// Rescale (ModReduce)
// Pre-generate a pool of fresh depth-2 ciphertexts *before* the
// timed loop so no in-loop Pause/ResumeTiming is needed.
// ============================================================
BENCHMARK_F(CKKSFixture, Rescale)(benchmark::State& state) {
    std::vector<Ciphertext<DCRTPoly>> pool;
    pool.reserve(state.max_iterations);
    for (int64_t i = 0; i < state.max_iterations; i++) {
        pool.push_back(cc->EvalMultNoRelin(ctBase, ctBase));
    }

    size_t i = 0;
    for (auto _ : state) {
        auto ctRescaled = cc->ModReduce(pool[i++]);
        benchmark::DoNotOptimize(ctRescaled);
    }
}

// ============================================================
// KeySwitch (Relinearize)
// Same pooling trick: relinearize consumes/mutates state, so
// each iteration needs its own fresh degree-2 ciphertext.
// ============================================================
BENCHMARK_F(CKKSFixture, KeySwitch)(benchmark::State& state) {
    std::vector<Ciphertext<DCRTPoly>> pool;
    pool.reserve(state.max_iterations);
    for (int64_t i = 0; i < state.max_iterations; i++) {
        pool.push_back(cc->EvalMultNoRelin(ctBase, ctBase));
    }

    size_t i = 0;
    for (auto _ : state) {
        auto ctRelin = cc->Relinearize(pool[i++]);
        benchmark::DoNotOptimize(ctRelin);
    }
}

// ============================================================
// NTT (via DCRTPoly::SwitchFormat)
// Each iteration copies the pristine poly first (outside timing
// would be ideal, but the copy itself is cheap relative to NTT;
// keeping it simple here — feel free to pool this too if you
// want to strip the copy cost out of the measurement).
// ============================================================
BENCHMARK_F(CKKSFixture, NTT)(benchmark::State& state) {
    const auto& elements = ctBase->GetElements();
    const DCRTPoly& polyRef = elements[0];

    std::vector<DCRTPoly> pool;
    pool.reserve(state.max_iterations);
    for (int64_t i = 0; i < state.max_iterations; i++) {
        pool.push_back(polyRef);  // fresh copy, same format as polyRef each time
    }

    size_t i = 0;
    for (auto _ : state) {
        pool[i++].SwitchFormat();  // triggers NTT/iNTT across all RNS limbs
    }
    benchmark::ClobberMemory();
}

// ============================================================
// Bootstrap
// Uses the separate ccBoot context built in SetUp with a much
// larger depth budget. Only one iteration's worth of state is
// needed per pool entry since EvalBootstrap doesn't mutate ctBoot.
// ============================================================
BENCHMARK_F(CKKSFixture, Bootstrap)(benchmark::State& state) {
    for (auto _ : state) {
        auto ctResult = ccBoot->EvalBootstrap(ctBoot);
        benchmark::DoNotOptimize(ctResult);
    }
}

BENCHMARK_MAIN();
