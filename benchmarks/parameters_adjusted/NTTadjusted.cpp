#include "openfhe.h"
#include <benchmark/benchmark.h>

using namespace lbcrypto;

[[maybe_unused]] static void RingArgs(benchmark::internal::Benchmark* b) {
    for (uint32_t r : {1024, 4096, 8192, 16384, 32768, 65536})
        b->ArgName("ringdm")->Arg(r);
}

static void CiphertextNTT(benchmark::State& state) {

    uint32_t n = state.range(0);
    uint32_t m = n >> 1;
    CCParams<CryptoContextCKKSRNS> params;
    params.SetMultiplicativeDepth(20);
    params.SetScalingModSize(52);
    params.SetFirstModSize(57);
    params.SetRingDim(n);
    params.SetBatchSize(m);
    params.SetSecurityLevel(HEStd_NotSet);

    CryptoContext<DCRTPoly> cc = GenCryptoContext(params);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);

    auto keys = cc->KeyGen();
    usint slots = cc->GetEncodingParams()->GetBatchSize();
    std::vector<std::complex<double>> vectorOfInts1(slots);
    for (usint i = 0; i < slots; i++) {
        vectorOfInts1[i] = 1.001 * i;
    }
    std::vector<std::complex<double>> vectorOfInts2(vectorOfInts1);
    auto pt = cc->MakeCKKSPackedPlaintext(vectorOfInts1);
    auto ct = cc->Encrypt(keys.publicKey, pt);

    // sanity check: number of towers per polynomial
    state.counters["towers"] = ct->GetElements()[0].GetNumOfElements();
    state.counters["ring dimension"] = std::log2(n);

    for (auto _ : state) {
        for (auto& poly : ct->GetElements())
            poly.SetFormat(Format::COEFFICIENT);   // INTT on every tower
        for (auto& poly : ct->GetElements())
            poly.SetFormat(Format::EVALUATION);    // NTT on every tower
        benchmark::DoNotOptimize(ct);
    }
}
BENCHMARK(CiphertextNTT)->Unit(benchmark::kMillisecond)->Apply(RingArgs);

BENCHMARK_MAIN();


/*

inline void SetFormat(const Format format, uint32_t thread_limit = 0) {
        if (this->GetFormat() != format) {
            this->SwitchFormat(thread_limit);
        }
    }

void DCRTPolyImpl<VecType>::SwitchFormat(uint32_t thread_limit) {
    m_format = (m_format == Format::COEFFICIENT) ? Format::EVALUATION : Format::COEFFICIENT;

    const uint32_t size = m_vectors.size();
    if (ParallelControls::InParallelRegion()) {
        for (uint32_t i = 0; i < size; ++i)
            m_vectors[i].SwitchFormat();
        return;
    }
    [[maybe_unused]] const uint32_t limit = thread_limit ? thread_limit : size;
#pragma omp parallel for num_threads(OpenFHEParallelControls.GetThreadLimit(limit))
    for (uint32_t i = 0; i < size; ++i)
        m_vectors[i].SwitchFormat();
}


void PolyImpl<VecType>::SwitchFormat(uint32_t thread_limit) {
    const auto& co{m_params->GetCyclotomicOrder()};
    const auto& rd{m_params->GetRingDimension()};
    const auto& ru{m_params->GetRootOfUnity()};

    if (rd != (co >> 1)) {
        PolyImpl<VecType>::ArbitrarySwitchFormat();
        return;
    }

    if (!m_values)
        OPENFHE_THROW("Poly switch format to empty values");

    if (m_format != Format::COEFFICIENT) {
        m_format = Format::COEFFICIENT;
        ChineseRemainderTransformFTT<VecType>().InverseTransformFromBitReverseInPlace(ru, co, &(*m_values));
        return;
    }
    m_format = Format::EVALUATION;
    ChineseRemainderTransformFTT<VecType>().ForwardTransformToBitReverseInPlace(ru, co, &(*m_values));
}

*/