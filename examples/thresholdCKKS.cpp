/*
  Example of threshold CKKS
 */

#include "openfhe.h"
#include "benchmark/benchmark.h"

using namespace lbcrypto;

void RunCKKS();

int main(int argc, char* argv[]) {

    std::cout << "\n=================RUNNING FOR CKKS=====================" << std::endl;

    RunCKKS();

    return 0;
}


void RunCKKS() {
    usint batchSize = 16;

    CCParams<CryptoContextCKKSRNS> parameters;  //holds configuration parameters
    parameters.SetMultiplicativeDepth(3);       //allowed depth of multiplication
    parameters.SetScalingModSize(50);           //scaling factor Delta
    parameters.SetBatchSize(batchSize);         //Number of plaintext numbers

    //cc: CryptoContext. Performs cryptographic operations. Important!
    //DCRT: Dual Chinese Remainder Theorem
    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    // enable features that you wish to use
    cc->Enable(PKE);            //Key-generation, encrypt, decrypt
    cc->Enable(KEYSWITCH);      //Key-switch
    cc->Enable(LEVELEDSHE);     //Somewhat Homomorphic Encryption, +, *
    cc->Enable(ADVANCEDSHE);    //Advanced stuff, rotations, inner products...
    cc->Enable(MULTIPARTY);     //Threshold CKKS

    ////////////////////////////////////////////////////////////
    // Set-up of parameters
    ////////////////////////////////////////////////////////////

    // Output the generated parameters
    std::cout << "p = " << cc->GetCryptoParameters()->GetPlaintextModulus() << std::endl;
    std::cout << "n = " << cc->GetCryptoParameters()->GetElementParams()->GetCyclotomicOrder() / 2 << std::endl;
    std::cout << "log2 q = " << std::log2(cc->GetCryptoParameters()->GetElementParams()->GetModulus().ConvertToDouble())
              << std::endl;

    // Initialize Key Containers
    KeyPair<DCRTPoly> kp1;
    KeyPair<DCRTPoly> kp2;

    KeyPair<DCRTPoly> kpMultiparty;

    ////////////////////////////////////////////////////////////
    // Perform Key Generation Operation
    ////////////////////////////////////////////////////////////

    std::cout << "Running key generation (used for source data)..." << std::endl;

    // Round 1 (party A)

    std::cout << "Round 1 (party A) started." << std::endl;

    kp1 = cc->KeyGen();     //secret key share, partial public key

    // Generate evalmult key part for A
    // Relinearization key, "more general switching key"??
    auto evalMultKey = cc->KeySwitchGen(kp1.secretKey, kp1.secretKey);

    // Generate evalsum key part for A
    // Rotation  key
    cc->EvalSumKeyGen(kp1.secretKey);

    // kp1.secretKey->GetKeyTag():
    // Every secret/public key pair has a unique identifier called a key tag.
    // cc->GetEvalSumKeyMap(keyTag):
    // Above EvalSum-keys were generated into a map. The functions retrieves the
    // value, which is also a map (std::map<usint, EvalKey<DCRTPoly).
    // std::make_shared:
    // Shared pointer. Declares ownership. Object gets deleted, when there is no
    // shared pointer anymore. Helps because multiple users access this map.
    auto evalSumKeys =
        std::make_shared<std::map<usint, EvalKey<DCRTPoly>>>(cc->GetEvalSumKeyMap(kp1.secretKey->GetKeyTag()));

    std::cout << "Round 1 of key generation completed." << std::endl;

    // Round 2 (party B)

    std::cout << "Round 2 (party B) started." << std::endl;

    std::cout << "Joint public key for (s_a + s_b) is generated..." << std::endl;
    
    // Calculates the second key-pair. First the secret key and partial public key are
    // created, then the joint public key is calculated. Public key of A not updated.
    kp2 = cc->MultipartyKeyGen(kp1.publicKey);

    //Note: Evaluation keys have the form (b,a). b + as -e = hidden message

    //compute relinearization key. evalMultKey2 = enc(P*s_b^2)
    auto evalMultKey2 = cc->MultiKeySwitchGen(kp2.secretKey, kp2.secretKey, evalMultKey);


    //add relinearization keys of A and B. evalMultAB = Enc(P(s_a + s_b))
    //P is special modulus
    std::cout << "Joint evaluation multiplication key for (s_a + s_b) is generated..." << std::endl;
        auto evalMultAB = cc->MultiAddEvalKeys(evalMultKey, evalMultKey2, kp2.publicKey->GetKeyTag());

    //calculate encryption of s_b * (s_a + s_b) = s_b^2 + s_b * s_a
    std::cout << "Joint evaluation multiplication key (s_a + s_b) is transformed "
                 "into s_b*(s_a + s_b)..."
              << std::endl; 
    auto evalMultBAB = cc->MultiMultEvalKey(kp2.secretKey, evalMultAB, kp2.publicKey->GetKeyTag());

    // Compute SumKey
    auto evalSumKeysB = cc->MultiEvalSumKeyGen(kp2.secretKey, evalSumKeys, kp2.publicKey->GetKeyTag());

    std::cout << "Joint evaluation summation key for (s_a + s_b) is generated..." << std::endl;
    auto evalSumKeysJoin = cc->MultiAddEvalSumKeys(evalSumKeys, evalSumKeysB, kp2.publicKey->GetKeyTag());

    cc->InsertEvalSumKey(evalSumKeysJoin);

    std::cout << "Round 2 of key generation completed." << std::endl;

    // Round 3 (party A)

    std::cout << "Round 3 (party A) started." << std::endl;

    //compute s_a * (s_a + s_b) = s_a^2 + s_a * s_b
    std::cout << "Joint key (s_a + s_b) is transformed into s_a*(s_a + s_b)..." << std::endl;
    auto evalMultAAB = cc->MultiMultEvalKey(kp1.secretKey, evalMultAB, kp2.publicKey->GetKeyTag());

    std::cout << "Computing the final evaluation multiplication key for (s_a + "
                 "s_b)*(s_a + s_b)..."
              << std::endl;
    auto evalMultFinal = cc->MultiAddEvalMultKeys(evalMultAAB, evalMultBAB, evalMultAB->GetKeyTag());

    cc->InsertEvalMultKey({evalMultFinal});

    std::cout << "Round 3 of key generation completed." << std::endl;

    ////////////////////////////////////////////////////////////
    // Encode source data
    ////////////////////////////////////////////////////////////
    std::vector<double> vectorOfInts1 = {1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0};
    std::vector<double> vectorOfInts2 = {1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0};
    std::vector<double> vectorOfInts3 = {2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0};

    Plaintext plaintext1 = cc->MakeCKKSPackedPlaintext(vectorOfInts1);
    Plaintext plaintext2 = cc->MakeCKKSPackedPlaintext(vectorOfInts2);
    Plaintext plaintext3 = cc->MakeCKKSPackedPlaintext(vectorOfInts3);

    ////////////////////////////////////////////////////////////
    // Encryption
    ////////////////////////////////////////////////////////////

    Ciphertext<DCRTPoly> ciphertext1;
    Ciphertext<DCRTPoly> ciphertext2;
    Ciphertext<DCRTPoly> ciphertext3;

    ciphertext1 = cc->Encrypt(kp2.publicKey, plaintext1);
    ciphertext2 = cc->Encrypt(kp2.publicKey, plaintext2);
    ciphertext3 = cc->Encrypt(kp2.publicKey, plaintext3);

    ////////////////////////////////////////////////////////////
    // EvalAdd Operation on Re-Encrypted Data
    ////////////////////////////////////////////////////////////

    Ciphertext<DCRTPoly> ciphertextAdd12;
    Ciphertext<DCRTPoly> ciphertextAdd123;

    ciphertextAdd12  = cc->EvalAdd(ciphertext1, ciphertext2);
    ciphertextAdd123 = cc->EvalAdd(ciphertextAdd12, ciphertext3);

    auto ciphertextMultTemp = cc->EvalMult(ciphertext1, ciphertext3);
    auto ciphertextMult     = cc->ModReduce(ciphertextMultTemp);
    auto ciphertextEvalSum  = cc->EvalSum(ciphertext3, batchSize);

    ////////////////////////////////////////////////////////////
    // Decryption after Accumulation Operation on Encrypted Data with Multiparty
    ////////////////////////////////////////////////////////////

    Plaintext plaintextAddNew1;
    Plaintext plaintextAddNew2;
    Plaintext plaintextAddNew3;

    DCRTPoly partialPlaintext1;
    DCRTPoly partialPlaintext2;
    DCRTPoly partialPlaintext3;

    Plaintext plaintextMultipartyNew;

    const std::shared_ptr<CryptoParametersBase<DCRTPoly>> cryptoParams = kp1.secretKey->GetCryptoParameters();
    const std::shared_ptr<typename DCRTPoly::Params> elementParams     = cryptoParams->GetElementParams();

    // distributed decryption

    auto ciphertextPartial1 = cc->MultipartyDecryptLead({ciphertextAdd123}, kp1.secretKey);

    auto ciphertextPartial2 = cc->MultipartyDecryptMain({ciphertextAdd123}, kp2.secretKey);

    std::vector<Ciphertext<DCRTPoly>> partialCiphertextVec;
    partialCiphertextVec.push_back(ciphertextPartial1[0]);
    partialCiphertextVec.push_back(ciphertextPartial2[0]);

    cc->MultipartyDecryptFusion(partialCiphertextVec, &plaintextMultipartyNew);

    std::cout << "\n Original Plaintext: \n" << std::endl;
    std::cout << plaintext1 << std::endl;
    std::cout << plaintext2 << std::endl;
    std::cout << plaintext3 << std::endl;

    plaintextMultipartyNew->SetLength(plaintext1->GetLength());

    std::cout << "\n Resulting Fused Plaintext: \n" << std::endl;
    std::cout << plaintextMultipartyNew << std::endl;

    std::cout << "\n";

    Plaintext plaintextMultipartyMult;

    ciphertextPartial1 = cc->MultipartyDecryptLead({ciphertextMult}, kp1.secretKey);

    ciphertextPartial2 = cc->MultipartyDecryptMain({ciphertextMult}, kp2.secretKey);

    std::vector<Ciphertext<DCRTPoly>> partialCiphertextVecMult;
    partialCiphertextVecMult.push_back(ciphertextPartial1[0]);
    partialCiphertextVecMult.push_back(ciphertextPartial2[0]);

    cc->MultipartyDecryptFusion(partialCiphertextVecMult, &plaintextMultipartyMult);

    plaintextMultipartyMult->SetLength(plaintext1->GetLength());

    std::cout << "\n Resulting Fused Plaintext after Multiplication of plaintexts 1 "
                 "and 3: \n"
              << std::endl;
    std::cout << plaintextMultipartyMult << std::endl;

    std::cout << "\n";

    Plaintext plaintextMultipartyEvalSum;

    ciphertextPartial1 = cc->MultipartyDecryptLead({ciphertextEvalSum}, kp1.secretKey);

    ciphertextPartial2 = cc->MultipartyDecryptMain({ciphertextEvalSum}, kp2.secretKey);

    std::vector<Ciphertext<DCRTPoly>> partialCiphertextVecEvalSum;
    partialCiphertextVecEvalSum.push_back(ciphertextPartial1[0]);
    partialCiphertextVecEvalSum.push_back(ciphertextPartial2[0]);

    cc->MultipartyDecryptFusion(partialCiphertextVecEvalSum, &plaintextMultipartyEvalSum);

    plaintextMultipartyEvalSum->SetLength(plaintext1->GetLength());

    std::cout << "\n Fused result after the Summation of ciphertext 3: "
                 "\n"
              << std::endl;
    std::cout << plaintextMultipartyEvalSum << std::endl;
}