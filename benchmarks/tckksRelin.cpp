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

KeyPair<DCRTPoly> kpMultiparty;


[[maybe_unused]] static CryptoContext<DCRTPoly> GenerateCKKSContext(uint32_t mdepth = 1) {
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetScalingModSize(48);
    parameters.SetBatchSize(8);
    parameters.SetScalingTechnique(FIXEDMANUAL);
    parameters.SetMultiplicativeDepth(mdepth);
    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    return cc;
}

[[maybe_unused]] static CryptoContext<DCRTPoly> GenerateTCKKSContext(uint32_t mdepth = 1) {
    //usint batchSize = 16;
    CCParams<CryptoContextCKKSRNS> parameters;  //holds configuration parameters
    parameters.SetScalingModSize(48);
    parameters.SetBatchSize(8);
    parameters.SetScalingTechnique(FIXEDMANUAL);
    parameters.SetMultiplicativeDepth(mdepth);
   
    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);            //Key-generation, encrypt, decrypt
    cc->Enable(KEYSWITCH);      //Key-switch
    cc->Enable(LEVELEDSHE);     //Somewhat Homomorphic Encryption, +, *
    cc->Enable(ADVANCEDSHE);    //Advanced stuff, rotations, inner products...
    cc->Enable(MULTIPARTY);     //Threshold CKKS

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
    return cc;
}


void CKKSrns_Relin(benchmark::State& state) {
    CryptoContext<DCRTPoly> cc = GenerateCKKSContext();

    KeyPair<DCRTPoly> keyPair = cc->KeyGen();
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

    auto ciphertextMul = cc->EvalMultNoRelin(ciphertext1, ciphertext2);

    while (state.KeepRunning()) {
        auto ciphertext3 = cc->Relinearize(ciphertextMul);
    }
}

BENCHMARK(CKKSrns_Relin)->Unit(benchmark::kMicrosecond);

void TCKKS_Relin(benchmark::State& state) {
    CryptoContext<DCRTPoly> cc = GenerateTCKKSContext();

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

    auto ciphertextMul = cc->EvalMultNoRelin(ciphertext1, ciphertext2);

    while (state.KeepRunning()) {
        auto ciphertext3 = cc->Relinearize(ciphertextMul);
    }
}

BENCHMARK(TCKKS_Relin)->Unit(benchmark::kMicrosecond);


BENCHMARK_MAIN();