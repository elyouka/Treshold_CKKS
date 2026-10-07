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
    { 1 << 16, 1 << 15,       54,       60,        15,         9,     1, {3, 3},      UNIFORM_TERNARY,         FLEXIBLEAUTO},
    { 1 << 16, 1 << 15,       50,       57,        11,         9,     2, {3, 3},      UNIFORM_TERNARY,         FLEXIBLEAUTO},
    { 1 << 16, 1 << 15,       50,       57,        16,        10,     2, {3, 3},      UNIFORM_TERNARY,         FLEXIBLEAUTO},
    { 1 << 16, 1 << 15,       52,       57,        10,         8,     2, {3, 3},      UNIFORM_TERNARY,          FIXEDMANUAL},
    { 1 << 16, 1 << 15,       52,       57,        16,         9,     2, {3, 3},      UNIFORM_TERNARY,          FIXEDMANUAL},
    { 1 << 17, 1 << 16,       59,       60,         0,         5,     1, {4, 4},       SPARSE_TERNARY,         FLEXIBLEAUTO},
    { 1 << 17, 1 << 16,       59,       60,         0,         5,     1, {4, 4},  SPARSE_ENCAPSULATED,         FLEXIBLEAUTO},
    { 1 << 16,  1 << 5,       59,       60,         0,         5,     1, {1, 1},       SPARSE_TERNARY,         FLEXIBLEAUTO},
    { 1 << 16,  1 << 5,       59,       60,         0,         5,     1, {1, 1},  SPARSE_ENCAPSULATED,         FLEXIBLEAUTO},
    { 1 << 17,  1 << 5,       59,       60,         0,         5,     1, {1, 1},       SPARSE_TERNARY,         FLEXIBLEAUTO},
    { 1 << 17,  1 << 5,       59,       60,         0,         5,     1, {1, 1},  SPARSE_ENCAPSULATED,         FLEXIBLEAUTO},
    { 1 << 17, 1 << 16,       59,       60,         0,        10,     1, {4, 4},  SPARSE_ENCAPSULATED,         FLEXIBLEAUTO},
    { 1 << 17,  1 << 5,       59,       60,         0,        10,     1, {1, 1},  SPARSE_ENCAPSULATED,         FLEXIBLEAUTO},
    { 1 << 17, 1 << 16,       59,       60,         0,        10,     2, {4, 4},  SPARSE_ENCAPSULATED,         FLEXIBLEAUTO},
    { 1 << 17,  1 << 5,       59,       60,         0,        10,     2, {1, 1},  SPARSE_ENCAPSULATED,         FLEXIBLEAUTO},
    { 1 << 16, 1 << 15,       55,       60,         3,          1,    1, {3, 3},      UNIFORM_TERNARY,         FLEXIBLEAUTO},  // GPU0
    { 1 << 16, 1 << 14,       50,       53,         7,         10,    1, {3, 3},       SPARSE_TERNARY,         FLEXIBLEAUTO},  // GPU1
    // TODO: enable following once STC Composite Scaling operational
    // { 1 << 17, 1 << 16,       78,       96,         0,        10,     2, {4, 4},       SPARSE_TERNARY, COMPOSITESCALINGAUTO},
};

[[maybe_unused]] static void BootConfigs(benchmark::internal::Benchmark* b) {
    for (uint32_t i = 0; i < boot_configs.size(); ++i)
        b->ArgName("Config")->Arg(i);
}

[[maybe_unused]] static CryptoContext<DCRTPoly> GenerateCKKSContext(uint32_t mdepth = 1) {

    auto t = boot_configs[0];

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
    uint32_t batchSize = 1 << 15;
    parameters.SetBatchSize(t.slots);

    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    return cc;
}


struct CKKSRelinSetup {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> keyPair;
    Ciphertext<DCRTPoly> ciphertextMul;

    CKKSRelinSetup(uint32_t n) {
        cc = GenerateCKKSContext(n);
        keyPair = cc->KeyGen();
        cc->EvalMultKeyGen(keyPair.secretKey);

        usint slots = cc->GetEncodingParams()->GetBatchSize();
        std::vector<std::complex<double>> vectorOfInts1(slots);
        for (usint i = 0; i < slots; i++) {
            vectorOfInts1[i] = 1.001 * i;
        }
        std::vector<std::complex<double>> vectorOfInts2(vectorOfInts1);

        auto plaintext1 = cc->MakeCKKSPackedPlaintext(vectorOfInts1);
        auto plaintext2 = cc->MakeCKKSPackedPlaintext(vectorOfInts2);

        auto ciphertext1 = cc->Encrypt(keyPair.publicKey, plaintext1);
        auto ciphertext2 = cc->Encrypt(keyPair.publicKey, plaintext2);

        ciphertextMul = cc->EvalMultNoRelin(ciphertext1, ciphertext2);
    }
};

[[maybe_unused]] static CryptoContext<DCRTPoly> GenerateTCKKSContext(uint32_t n) {
    //usint batchSize = 16;

    auto t = boot_configs[n];

    CCParams<CryptoContextCKKSRNS> parameters;
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    parameters.SetSecurityLevel(HEStd_128_classic);
    parameters.SetRingDim(t.ringDim);
    parameters.SetScalingModSize(t.dcrtBits);
    parameters.SetFirstModSize(t.firstMod);
    parameters.SetNumLargeDigits(t.numDigits);
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetScalingTechnique(t.stech);
    parameters.SetKeySwitchTechnique(KeySwitchTechnique::HYBRID);
    uint32_t depth = t.lvlsAfter + FHECKKSRNS::GetBootstrapDepth(t.lvlb, t.skdst) + (t.iters - 1);
    parameters.SetMultiplicativeDepth(depth);
    uint32_t batchSize = 1 << 15;
    parameters.SetBatchSize(batchSize);
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

struct TCKKSRelinSetup {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> kpMultiparty;
    Ciphertext<DCRTPoly> ciphertextMul;

    TCKKSRelinSetup(uint32_t n) {
    cc = GenerateTCKKSContext(n);
        
    KeyPair<DCRTPoly> kp1;
    KeyPair<DCRTPoly> kp2;

    kp1 = cc->KeyGen();     
    auto evalMultKey = cc->KeySwitchGen(kp1.secretKey, kp1.secretKey);
    cc->EvalSumKeyGen(kp1.secretKey);
    auto evalSumKeys =
        std::make_shared<std::map<usint, EvalKey<DCRTPoly>>>(cc->GetEvalSumKeyMap(kp1.secretKey->GetKeyTag()));
    // Round 2 (party B)
    kp2 = cc->MultipartyKeyGen(kp1.publicKey);

    auto evalMultKey2 = cc->MultiKeySwitchGen(kp2.secretKey, kp2.secretKey, evalMultKey);
    auto evalMultAB = cc->MultiAddEvalKeys(evalMultKey, evalMultKey2, kp2.publicKey->GetKeyTag());
    auto evalMultBAB = cc->MultiMultEvalKey(kp2.secretKey, evalMultAB, kp2.publicKey->GetKeyTag());
    // Compute SumKey
    auto evalSumKeysB = cc->MultiEvalSumKeyGen(kp2.secretKey, evalSumKeys, kp2.publicKey->GetKeyTag());
    //std::cout << "Joint evaluation summation key for (s_a + s_b) is generated..." << std::endl;
    auto evalSumKeysJoin = cc->MultiAddEvalSumKeys(evalSumKeys, evalSumKeysB, kp2.publicKey->GetKeyTag());
    cc->InsertEvalSumKey(evalSumKeysJoin);
    // Round 3 (party A)
    auto evalMultAAB = cc->MultiMultEvalKey(kp1.secretKey, evalMultAB, kp2.publicKey->GetKeyTag());
    auto evalMultFinal = cc->MultiAddEvalMultKeys(evalMultAAB, evalMultBAB, evalMultAB->GetKeyTag());

    cc->InsertEvalMultKey({evalMultFinal});

    kpMultiparty = kp2;

    usint slots = cc->GetEncodingParams()->GetBatchSize();
    std::vector<std::complex<double>> vectorOfInts1(slots);
    for (usint i = 0; i < slots; i++) {
        vectorOfInts1[i] = 1.001 * i;
    }
    std::vector<std::complex<double>> vectorOfInts2(vectorOfInts1);

    auto plaintext1 = cc->MakeCKKSPackedPlaintext(vectorOfInts1);
    auto plaintext2 = cc->MakeCKKSPackedPlaintext(vectorOfInts2);

    auto ciphertext1 = cc->Encrypt(kpMultiparty.publicKey, plaintext1);
    auto ciphertext2 = cc->Encrypt(kpMultiparty.publicKey, plaintext2);

    ciphertextMul = cc->EvalMultNoRelin(ciphertext1, ciphertext2);
    }
};

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

void CKKSrns_Relin(benchmark::State& state) {
    uint32_t n = state.range(0);
    auto& setup = GetSetup<CKKSRelinSetup>(n);   // initialized once
    while (state.KeepRunning()) {
        auto ciphertext3 = setup.cc->Relinearize(setup.ciphertextMul);
        benchmark::DoNotOptimize(ciphertext3);
    }
}

BENCHMARK(CKKSrns_Relin)->Unit(benchmark::kMillisecond)->Apply(BootConfigs);

void TCKKS_Relin(benchmark::State& state) {
    uint32_t n = state.range(0);
    auto& setup = GetSetup<TCKKSRelinSetup>(n);   // initialized once
    while (state.KeepRunning()) {
        auto ciphertext3 = setup.cc->Relinearize(setup.ciphertextMul);
        benchmark::DoNotOptimize(ciphertext3);
    }
}

BENCHMARK(TCKKS_Relin)->Unit(benchmark::kMillisecond)->Apply(BootConfigs);


BENCHMARK_MAIN();