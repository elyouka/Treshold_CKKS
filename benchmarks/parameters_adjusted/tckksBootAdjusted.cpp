#define PROFILE

#include "openfhe.h"
#include "benchmark/benchmark.h"
#include "config_core.h"
#include "cryptocontext.h"
#include "gen-cryptocontext.h"
#include "scheme/ckksrns/ckksrns-fhe.h"
#include "scheme/ckksrns/ckksrns-utils.h"
#include "scheme/ckksrns/gen-cryptocontext-ckksrns.h"

#include <vector>

using namespace lbcrypto;

//This class contains a benchmark with parameters adjusted to the openfhe standard
//bootstrapping example


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
/*
 * A utility class defining a party that is involved in the collective bootstrapping protocol
 */
struct Party {
public:
    usint id;  // unique party identifier starting from 0

    std::vector<Ciphertext<DCRTPoly>> sharesPair;  // (h_{0,i}, h_{1,i}) = (masked decryption
                                                   // share, re-encryption share)
                                                   // we use a vector inseat of std::pair for Python API compatibility

    KeyPair<DCRTPoly> kpShard;  // key-pair shard (pk, sk_i)
};

// Demonstrate interactive multi-party bootstrapping for 3 parties
// We follow Protocol 5 in https://eprint.iacr.org/2020/304, "Multiparty
// Homomorphic Encryption from Ring-Learning-With-Errors"

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

[[maybe_unused]] static void TCKKSCollectiveBoot(benchmark::State& state) {

    auto t = boot_configs[3];

    CCParams<CryptoContextCKKSRNS> parameters;
    // A. Specify main parameters
    /*  A1) Secret key distribution
	* The secret key distribution for CKKS should either be SPARSE_TERNARY or UNIFORM_TERNARY.
	* The SPARSE_TERNARY distribution was used in the original CKKS paper,
	* but in this example, we use UNIFORM_TERNARY because this is included in the homomorphic
	* encryption standard.
	*/
    SecretKeyDist secretKeyDist = UNIFORM_TERNARY;
    parameters.SetSecurityLevel(HEStd_128_classic);
    parameters.SetRingDim(t.ringDim);
    parameters.SetScalingModSize(t.dcrtBits);
    parameters.SetFirstModSize(t.firstMod);
    parameters.SetNumLargeDigits(t.numDigits);
    parameters.SetSecretKeyDist(secretKeyDist);
    parameters.SetScalingTechnique(t.stech);
    parameters.SetKeySwitchTechnique(KeySwitchTechnique::HYBRID);
    uint32_t multiplicativeDepth = 28;
    parameters.SetMultiplicativeDepth(multiplicativeDepth);
    uint32_t batchSize = 1 << 15;
    parameters.SetBatchSize(batchSize);
    auto compressionLevel = CompressionLevel::SLACK;
    parameters.SetInteractiveBootCompressionLevel(compressionLevel);


       /*  A4) Multiplicative depth.
    * The multiplicative depth determines the computational capability of the instantiated scheme. It should be set
    * according the following formula:
    * multDepth >= desired_depth + interactive_bootstrapping_depth
    * where,
    *   The desired_depth is the depth of the computation, as chosen by the user.
    *   The interactive_bootstrapping_depth is either 3 or 4, depending on the ciphertext compression mode: COMPACT vs SLACK (see below)
    * Example 1, if you want to perform a computation of depth 24, you can set multDepth to 10, use 6 levels
    * for computation and 4 for interactive bootstrapping. You will need to bootstrap 3 times.
    */
    /*  Protocol-specific parameters (SLACK or COMPACT)
    * SLACK (default) uses larger masks, which makes it more secure theoretically. However, it is also slightly less efficient.
    * COMPACT uses smaller masks, which makes it more efficient. However, it is relatively less secure theoretically.
    * Both options can be used for practical security.
    * The following table summarizes the differences between SLACK and COMPACT:
    * Parameter	        SLACK	                                        COMPACT
    * Mask size	        Larger	                                        Smaller
    * Security	        More secure	                                    Less secure
    * Efficiency	    Less efficient	                                More efficient
    * Recommended use	For applications where security is paramount	For applications where efficiency is paramount
    */


    CryptoContext<DCRTPoly> cryptoContext = GenCryptoContext(parameters);

    cryptoContext->Enable(PKE);
    cryptoContext->Enable(KEYSWITCH);
    cryptoContext->Enable(LEVELEDSHE);
    cryptoContext->Enable(ADVANCEDSHE);
    cryptoContext->Enable(MULTIPARTY);

    //usint ringDim = cryptoContext->GetRingDimension();
    usint ringDim = t.ringDim;
    // This is the maximum number of slots that can be used for full packing.
    usint maxNumSlots = ringDim / 2;
    std::cout << "TCKKS scheme is using ring dimension " << ringDim << std::endl;
    std::cout << "TCKKS scheme number of slots         " << batchSize << std::endl;
    std::cout << "TCKKS scheme max number of slots     " << maxNumSlots << std::endl;
    std::cout << "TCKKS example with Scaling Technique " << FIXEDMANUAL << std::endl;

    const usint numParties = 3;  // n: number of parties involved in the interactive protocol

    std::cout << "\n===========================IntMPBoot protocol parameters===========================\n";
    std::cout << "number of parties: " << numParties << "\n";
    std::cout << "===============================================================\n";

    std::vector<Party> parties(numParties);

    // Joint public key
    KeyPair<DCRTPoly> kpMultiparty;

    ////////////////////////////////////////////////////////////
    // Perform Key Generation Operation
    ////////////////////////////////////////////////////////////

    std::cout << "Running key generation (used for source data)..." << std::endl;

    // Initialization - Assuming numParties (n) of parties
    // P0 is the leading party
    for (usint i = 0; i < numParties; i++) {
        parties[i].id = i;
        std::cout << "Party " << parties[i].id << " started.\n";
        if (0 == i)
            parties[i].kpShard = cryptoContext->KeyGen();
        else
            parties[i].kpShard = cryptoContext->MultipartyKeyGen(parties[0].kpShard.publicKey);
        std::cout << "Party " << i << " key generation completed.\n";
    }
    std::cout << "Joint public key for (s_0 + s_1 + ... + s_n) is generated..." << std::endl;

    // Assert everything is good
    for (usint i = 0; i < numParties; i++) {
        if (!parties[i].kpShard.good()) {
            std::cout << "Key generation failed for party " << i << "!" << std::endl;
            exit(1);
        }
    }

    // Generate the collective public key
    std::vector<PrivateKey<DCRTPoly>> secretKeys;
    for (usint i = 0; i < numParties; i++) {
        secretKeys.push_back(parties[i].kpShard.secretKey);
    }
    kpMultiparty = cryptoContext->MultipartyKeyGen(secretKeys);  // This is the same core key generation operation.

    // Prepare input vector
    std::vector<std::complex<double>> msg1({-0.9, -0.8, 0.2, 0.4});
    Plaintext ptxt1 = cryptoContext->MakeCKKSPackedPlaintext(msg1);

    // Encryption
    Ciphertext<DCRTPoly> inCtxt = cryptoContext->Encrypt(kpMultiparty.publicKey, ptxt1);
    DCRTPoly ptxtpoly           = ptxt1->GetElement<DCRTPoly>();

    std::cout << "Compressing ctxt to the smallest possible number of towers!\n";
    
    // INTERACTIVE BOOTSTRAPPING STARTS
    
    while (state.KeepRunning()) {
    state.PauseTiming();
    auto ct = inCtxt->Clone();
    state.ResumeTiming();

    ct = cryptoContext->IntMPBootAdjustScale(ct);
    
    
    //std::cout << "\n============================ INTERACTIVE BOOTSTRAPPING STARTS ============================\n";

    // Leading party (P0) generates a Common Random Poly (a) at max coefficient modulus (QNumPrime).
    // a is sampled at random uniformly from R_{Q}
    Ciphertext<DCRTPoly> a = cryptoContext->IntMPBootRandomElementGen(parties[0].kpShard.publicKey);
    //std::cout << "Common Random Poly (a) has been generated with coefficient modulus Q\n";

    // Each party generates its own shares: maskedDecryptionShare and reEncryptionShare
    std::vector<std::vector<Ciphertext<DCRTPoly>>> sharesPairVec;

    // Make a copy of input ciphertext and remove the first element (c0), we only need
    // c1 for IntMPBootDecrypt
    auto c1 = inCtxt->Clone();
    c1->GetElements().erase(c1->GetElements().begin());
    for (usint i = 0; i < numParties; i++) {
        //std::cout << "Party " << i << " started its part in the Collective Bootstrapping Protocol\n";
        parties[i].sharesPair = cryptoContext->IntMPBootDecrypt(parties[i].kpShard.secretKey, c1, a);
        sharesPairVec.push_back(parties[i].sharesPair);
    }

    // P0 finalizes the protocol by aggregating the shares and reEncrypting the results
    auto aggregatedSharesPair = cryptoContext->IntMPBootAdd(sharesPairVec);
    // Make sure you provide the non-striped ciphertext (inCtxt) in IntMPBootEncrypt
    auto outCtxt = cryptoContext->IntMPBootEncrypt(parties[0].kpShard.publicKey, aggregatedSharesPair, a, inCtxt);
    benchmark::DoNotOptimize(outCtxt);    
    }
    // INTERACTIVE BOOTSTRAPPING ENDS
    std::cout << "\n============================ INTERACTIVE BOOTSTRAPPING ENDED ============================\n";
}

BENCHMARK(TCKKSCollectiveBoot)->Unit(benchmark::kSecond);

BENCHMARK_MAIN();