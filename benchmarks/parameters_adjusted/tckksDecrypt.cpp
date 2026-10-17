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


static void CheckDecryption(const Plaintext& got,
                            const std::vector<std::complex<double>>& want,
                            const char* tag)
{
    auto v = got->GetCKKSPackedValue();
    if (v.size() < want.size())
        throw std::runtime_error(std::string(tag) + ": plaintext shorter than expected");

    double maxErr = 0.0;
    for (size_t i = 0; i < want.size(); ++i)
        maxErr = std::max(maxErr, std::abs(v[i] - want[i]));

    if (maxErr > 1e-6) {
        throw std::runtime_error(std::string(tag) +
            ": max |err| = " + std::to_string(maxErr));
    }
}

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

/*[[maybe_unused]] std::vector<boot_config> boot_configs = {
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
};*/

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
    return cc;
}


struct CKKSRescaleSetup {
    CryptoContext<DCRTPoly> cc;
    KeyPair<DCRTPoly> keyPair;
    Ciphertext<DCRTPoly> ciphertext1;

    CKKSRescaleSetup(uint32_t n) {
        cc = GenerateCKKSContext(n);
        keyPair = cc->KeyGen();
        usint slots = cc->GetEncodingParams()->GetBatchSize();
        std::vector<std::complex<double>> vectorOfInts1(slots);
        for (usint i = 0; i < slots; i++) {
            vectorOfInts1[i] = 1.001 * i;
        }
        std::vector<std::complex<double>> vectorOfInts2(vectorOfInts1);

        auto plaintext1 = cc->MakeCKKSPackedPlaintext(vectorOfInts1);
        auto plaintext2 = cc->MakeCKKSPackedPlaintext(vectorOfInts2);

        ciphertext1 = cc->Encrypt(keyPair.publicKey, plaintext1);
        //auto ciphertext2 = cc->Encrypt(keyPair.publicKey, plaintext2);

        //ciphertextMul = cc->EvalMult(ciphertext1, ciphertext2);

        //check if decryption works correctly
        Plaintext check;
        cc->Decrypt(keyPair.secretKey, ciphertext1, &check);
        check->SetLength(slots);
        CheckDecryption(check, vectorOfInts1, "CKKS single-party");
    }
};

[[maybe_unused]] static CryptoContext<DCRTPoly> GenerateTCKKSContext(uint32_t n) {
    //usint batchSize = 16;

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
    uint32_t batchSize = 1 << 15;
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

struct TCKKSRescaleSetup {
    CryptoContext<DCRTPoly> cc;
    //KeyPair<DCRTPoly> kpMultiparty;
    KeyPair<DCRTPoly> kp1;              // party A's share
    KeyPair<DCRTPoly> kp2;
    Ciphertext<DCRTPoly> ciphertext1;

    TCKKSRescaleSetup(uint32_t n) {
    cc = GenerateTCKKSContext(n);

    kp1 = cc->KeyGen();     
    kp2 = cc->MultipartyKeyGen(kp1.publicKey);

    usint slots = cc->GetEncodingParams()->GetBatchSize();
    std::vector<std::complex<double>> vectorOfInts1(slots);
    for (usint i = 0; i < slots; i++) {
        vectorOfInts1[i] = 1.001 * i;
    }
    std::vector<std::complex<double>> vectorOfInts2(vectorOfInts1);

    auto plaintext1 = cc->MakeCKKSPackedPlaintext(vectorOfInts1);
    auto plaintext2 = cc->MakeCKKSPackedPlaintext(vectorOfInts2);

    ciphertext1 = cc->Encrypt(kp2.publicKey, plaintext1);


    //check if decryption works correctly

    auto partial1 = cc->MultipartyDecryptLead({ciphertext1}, kp1.secretKey);
        auto partial2 = cc->MultipartyDecryptMain({ciphertext1}, kp2.secretKey);

        std::vector<Ciphertext<DCRTPoly>> partials;
        partials.push_back(partial1[0]);
        partials.push_back(partial2[0]);

        Plaintext check;
        cc->MultipartyDecryptFusion(partials, &check);
        check->SetLength(slots);

        CheckDecryption(check, vectorOfInts1, "TCKKS multiparty");
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


void CKKSrns_Decrypt(benchmark::State& state) {
    uint32_t n = state.range(0);
    auto& setup = GetSetup<CKKSRescaleSetup>(n);
    state.counters["ring-dim"]  = std::log2(boot_configs[n].ringDim);

    Plaintext plaintextDec1;
    while (state.KeepRunning()) {
        setup.cc->Decrypt(setup.keyPair.secretKey, setup.ciphertext1, &plaintextDec1);
        benchmark::DoNotOptimize(plaintextDec1);
        }
}

BENCHMARK(CKKSrns_Decrypt)->Unit(benchmark::kMillisecond)->Apply(BootConfigs);

void TCKKS_Decrypt(benchmark::State& state) {
    uint32_t n = state.range(0);
    auto& setup = GetSetup<TCKKSRescaleSetup>(n);
    state.counters["ring-dim"]  = std::log2(boot_configs[n].ringDim);

    Plaintext plaintextDec;
    while (state.KeepRunning()) {
        auto partial1 = setup.cc->MultipartyDecryptLead(
            {setup.ciphertext1}, setup.kp1.secretKey);

        // Step 2: Party B partial decryption
        auto partial2 = setup.cc->MultipartyDecryptMain(
            {setup.ciphertext1}, setup.kp2.secretKey);

        // Step 3: Fusion
        std::vector<Ciphertext<DCRTPoly>> partials;
        partials.push_back(partial1[0]);
        partials.push_back(partial2[0]);
        setup.cc->MultipartyDecryptFusion(partials, &plaintextDec);

        benchmark::DoNotOptimize(plaintextDec);
    }
}


BENCHMARK(TCKKS_Decrypt)->Unit(benchmark::kMillisecond)->Apply(BootConfigs);



BENCHMARK_MAIN();