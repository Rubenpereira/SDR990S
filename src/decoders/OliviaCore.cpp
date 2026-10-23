#include "OliviaCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace masdr {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 8000.0;
constexpr int kBloco = 64;            // simbolos por bloco (caracteres de 7 bits)
constexpr int kFases = 4;             // fases de tempo testadas por simbolo
constexpr uint64_t kChave = 0xE257E6D0291574ECULL;   // embaralhamento do OLIVIA
constexpr int kDeslocChave = 13;      // deslocamento da chave por caractere
constexpr float kPesoInteg = 0.35f;   // integracao da metrica (~3 blocos)
// limiar da metrica integrada (pico de Walsh / ruido, media dos caracteres).
// Ruido puro: o maximo entre as ~4300 hipoteses fica em ~3,3 (medido com
// 10 min de ruido branco nos modos 8/250, 16/500 e 32/1000).
constexpr float kLimiar = 4.2f;
constexpr float kLimiarAnt = 4.5f;    // o bloco de antes da trava (se ficou bom)
constexpr float kQminBloco = 3.3f;    // o proprio bloco tambem tem de sair do ruido (~2,6)
constexpr int kGrupo = 2 * kFases;    // fatias que formam um grupo (2 simbolos)

// Transformada de Walsh-Hadamard rapida (64 pontos, in-place), na mesma
// ordem de sinais da implementacao de referencia: (a,b) -> (b+a, b-a)
void fwht(float* d, int n)
{
    for (int step = 1; step < n; step *= 2)
        for (int i = 0; i < n; i += 2 * step)
            for (int j = i; j < i + step; ++j) {
                const float a = d[j], b = d[j + step];
                d[j] = b + a;
                d[j + step] = b - a;
            }
}

int degray(int n)
{
    int mask = n;
    while (mask != 0) { mask >>= 1; n ^= mask; }
    return n;
}

int potencia2(int v, int lo, int hi)
{
    v = std::clamp(v, lo, hi);
    int p = lo;
    while (p * 2 <= v) p *= 2;
    if (p * 2 <= hi && (v - p) > (p * 2 - v)) p *= 2;   // o mais perto
    return p;
}
} // namespace

OliviaCore::OliviaCore()
{
    montar();
}

void OliviaCore::setModo(int tons, int banda, double centro)
{
    std::lock_guard<std::mutex> lk(mtx_);
    tons_ = tons;
    banda_ = banda;
    centro_ = centro;
    montar();
}

void OliviaCore::limpar()
{
    std::lock_guard<std::mutex> lk(mtx_);
    zerar();
}

void OliviaCore::montar()
{
    // os formatos do OLIVIA: tons e banda sao potencias de 2 (banda = 125 * 2^k)
    tons_ = potencia2(tons_, 2, 128);
    banda_ = 125 * potencia2(std::max(1, banda_ / 125), 1, 16);
    // o simbolo tem de ter pelo menos 32 amostras (senao a FFT nao separa os tons)
    while (kFs * tons_ / banda_ < 32.0 && tons_ < 128) tons_ *= 2;
    spb_ = 0;
    while ((1 << spb_) < tons_) ++spb_;
    const double sep = (double)banda_ / (double)tons_;    // separacao = taxa de simbolos
    ns_ = (int)std::lround(kFs / sep);
    L_ = 2 * ns_;
    hop_ = ns_ / kFases;
    binHz_ = kFs / (double)L_;
    f0_ = centro_ - banda_ / 2.0 + sep / 2.0;
    b0_ = (int)std::lround(f0_ / binHz_);
    const double delta = f0_ - b0_ * binHz_;           // o pedaco de bin que sobra
    // a mistura tira esse pedaco: o primeiro tom cai em cima do bin b0
    ncoInc_ = std::polar(1.0, -2.0 * kPi * delta / kFs);
    // a busca de desvio: +-125 Hz (como o receptor de referencia), em bins
    margem_ = std::clamp((int)std::lround(125.0 / binHz_), 2, 16);
    margem_ = std::min(margem_, b0_ - 1);
    while (margem_ > 0 && b0_ + margem_ + 2 * (tons_ - 1) >= L_ / 2) --margem_;
    if (margem_ < 0) margem_ = 0;
    nOff_ = 2 * margem_ + 1;
    nHip_ = nOff_ * kFases;
    binIni_ = b0_ - margem_;
    nBins_ = 2 * margem_ + 2 * (tons_ - 1) + 1;

    // a FFT (radix 2) e a janela de Hann de 2 simbolos (o envelope 1-cos dos tons)
    janela_.resize(L_);
    for (int i = 0; i < L_; ++i)
        janela_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / L_));
    tw_.resize(L_ / 2);
    for (int i = 0; i < L_ / 2; ++i) tw_[i] = std::polar(1.0f, (float)(-2.0 * kPi * i / L_));
    rev_.resize(L_);
    int bits = 0;
    while ((1 << bits) < L_) ++bits;
    for (int i = 0; i < L_; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev_[i] = r;
    }
    fftBuf_.assign(L_, {});
    energia_.assign(nBins_, 0.f);
    grayInv_.resize(tons_);
    for (int k = 0; k < tons_; ++k) grayInv_[k] = degray(k);
    zerar();
}

void OliviaCore::zerar()
{
    anel_.assign(L_, {});
    anelPos_ = 0;
    contHop_ = 0;
    nAmostras_ = 0;
    nco_ = {1.0, 0.0};
    ncoRenorm_ = 0;
    nFatias_ = 0;
    moles_.assign((size_t)nHip_ * kBloco * spb_, 0.f);
    tomDur_.assign((size_t)nHip_ * kBloco, 0);
    nSimb_.assign(nHip_, 0);
    integ_.assign((size_t)nHip_ * kBloco, 0.f);
    qAnt_.assign((size_t)nHip_ * kBloco, 0.f);
    charsAnt_.assign((size_t)nHip_ * kBloco * spb_, 0);
    ultimaSaida_ = -1000000;
    ultimaTrava_ = -1000000;
    pend_ = Pendente();
    melhorInteg_ = 0;
    melhorHip_ = 0;
    nivel_ = 0;
    est_ = Estado();
    est_.texto = "esperando audio";
    pendente_.clear();
    recente_.clear();
    ultimoChar_ = 0;
}

void OliviaCore::alimentar(const float* amostras, size_t n)
{
    if (!amostras || !n) return;
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t i = 0; i < n; ++i) {
        const std::complex<double> v = (double)amostras[i] * nco_;
        nco_ *= ncoInc_;
        if (++ncoRenorm_ >= 1024) { nco_ /= std::abs(nco_); ncoRenorm_ = 0; }
        anel_[anelPos_] = std::complex<float>((float)v.real(), (float)v.imag());
        if (++anelPos_ >= (size_t)L_) anelPos_ = 0;
        ++nAmostras_;
        if (++contHop_ >= hop_) {
            contHop_ = 0;
            if (nAmostras_ >= L_) fatia();
        }
    }
}

// uma FFT de 2 simbolos; ela da o simbolo de uma das 4 fases de tempo
void OliviaCore::fatia()
{
    // as amostras em ordem (a mais antiga primeiro), com a janela, em ordem
    // de bits invertida para a FFT
    for (int i = 0; i < L_; ++i) {
        size_t a = anelPos_ + (size_t)i;
        if (a >= (size_t)L_) a -= (size_t)L_;
        fftBuf_[rev_[i]] = anel_[a] * janela_[i];
    }
    for (int len = 2; len <= L_; len <<= 1) {
        const int meio = len >> 1, passo = L_ / len;
        for (int i = 0; i < L_; i += len)
            for (int j = 0; j < meio; ++j) {
                const std::complex<float> t = fftBuf_[i + j + meio] * tw_[j * passo];
                fftBuf_[i + j + meio] = fftBuf_[i + j] - t;
                fftBuf_[i + j] += t;
            }
    }
    for (int b = 0; b < nBins_; ++b) energia_[b] = std::norm(fftBuf_[binIni_ + b]);

    const int fase = (int)(nFatias_ % kFases);
    ++nFatias_;

    // cada desvio desta fase de tempo recebe um simbolo novo
    float qNovo[33];
    int aNovo[33];
    uint8_t chNovo[33][8];
    for (int oi = 0; oi < nOff_; ++oi) {
        const int s = fase * nOff_ + oi;
        const float* e = &energia_[oi];
        // a probabilidade de cada tom ~ energia^2 (como o receptor de
        // referencia): o tom forte manda, os fracos quase nao pesam
        double tot = 0, maior = -1;
        int tom = 0;
        double p[128];
        for (int k = 0; k < tons_; ++k) {
            const double en = e[2 * k];
            p[k] = en * en;
            tot += p[k];
            if (en > maior) { maior = en; tom = k; }
        }
        const long long ns = nSimb_[s];
        const int slot = (int)(ns % kBloco);
        float* m = &moles_[((size_t)s * kBloco + slot) * spb_];
        for (int b = 0; b < spb_; ++b) {
            double v = 0;
            for (int k = 0; k < tons_; ++k) v += ((grayInv_[k] >> b) & 1) ? -p[k] : p[k];
            m[b] = tot > 0 ? (float)(v / tot) : 0.f;
        }
        tomDur_[(size_t)s * kBloco + slot] = (uint8_t)tom;
        nSimb_[s] = ns + 1;
        qNovo[oi] = -1;
        aNovo[oi] = (int)((ns + 1) % kBloco);
        if (ns + 1 >= kBloco) {
            const float q = (float)decodificaBloco(s, chNovo[oi]);
            qNovo[oi] = q;
            float& I = integ_[(size_t)s * kBloco + aNovo[oi]];
            I += (q - I) * kPesoInteg;
        }
    }

    // o melhor candidato entre todas as hipoteses e alinhamentos (para o estado)
    float best = 0;
    int bestIdx = 0;
    const size_t nI = integ_.size();
    for (size_t i = 0; i < nI; ++i)
        if (integ_[i] > best) { best = integ_[i]; bestIdx = (int)i; }
    melhorInteg_ = best;
    melhorHip_ = bestIdx / kBloco;
    if (best >= kLimiar) ultimaTrava_ = nFatias_;

    // O texto: as hipoteses vizinhas (1/4 de simbolo, meia separacao) veem o
    // MESMO bloco, uma ou duas fatias de diferenca. Entao os blocos que
    // terminam perto um do outro formam um grupo e so o melhor do grupo
    // (a metrica integrada mais alta) vira texto, quando o grupo fecha.
    for (int oi = 0; oi < nOff_; ++oi) {
        if (qNovo[oi] < 0) continue;
        const int s = fase * nOff_ + oi;
        const size_t ia = (size_t)s * kBloco + aNovo[oi];
        const float I = integ_[ia];
        if (I < kLimiar) continue;
        if (!pend_.ativo || I > pend_.integ) {
            if (!pend_.ativo) pend_.t = nFatias_;
            pend_.ativo = true;
            pend_.integ = I;
            pend_.q = qNovo[oi];
            std::memcpy(pend_.chars, chNovo[oi], spb_);
            pend_.qAnt = qAnt_[ia];
            std::memcpy(pend_.charsAnt, &charsAnt_[ia * spb_], spb_);
        }
    }
    if (pend_.ativo && nFatias_ - pend_.t >= kGrupo) {
        pend_.ativo = false;
        // e tem de ser o candidato forte: com um sinal forte, os alinhamentos
        // errados (o bloco "pela metade") tambem passam do limiar, mas ficam
        // muito abaixo do certo
        // (o proprio bloco tambem: depois do fim da transmissao a metrica
        // integrada ainda fica alta por uns blocos, mas o bloco e ruido)
        if (pend_.q >= std::max(kQminBloco, 0.2f * pend_.integ) && pend_.integ >= 0.5f * best &&
            pend_.t - ultimaSaida_ >= (long long)(kBloco * 3 / 4) * kFases) {
            // o bloco anterior deste mesmo candidato, se ficou de fora e era bom
            if (pend_.t - ultimaSaida_ > (long long)(kBloco * 3 / 2) * kFases && pend_.qAnt >= kLimiarAnt)
                saida(pend_.charsAnt);
            saida(pend_.chars);
            ultimaSaida_ = pend_.t;
            est_.blocos++;
        }
    }
    // guarda o bloco deste alinhamento (para a trava "olhar para tras")
    for (int oi = 0; oi < nOff_; ++oi) {
        if (qNovo[oi] < 0) continue;
        const size_t ia = (size_t)(fase * nOff_ + oi) * kBloco + aNovo[oi];
        qAnt_[ia] = qNovo[oi];
        std::memcpy(&charsAnt_[ia * spb_], chNovo[oi], spb_);
    }

    // o estado (para a tela)
    const int oiB = melhorHip_ % nOff_;
    double emax = 0;
    for (int k = 0; k < tons_; ++k) emax = std::max(emax, (double)energia_[oiB + 2 * k]);
    // um tom de amplitude a da |X| = a*L/4 com a janela de Hann
    const double amp = 4.0 * std::sqrt(emax) / L_;
    nivel_ = nivel_ * 0.9 + amp * 0.1;
    est_.nivelDb = nivel_ > 1e-9 ? 20.0 * std::log10(nivel_) : -100.0;
    est_.qualidade = best;
    est_.travado = (nFatias_ - ultimaTrava_) < (long long)2 * kBloco * kFases;
    est_.ok = est_.travado || best >= 0.85f * kLimiar;
    const double sep = (double)banda_ / (double)tons_;
    est_.desvioHz = (oiB - margem_) * binHz_;
    est_.tomBaixo = f0_ + est_.desvioHz;
    est_.tomAlto = est_.tomBaixo + (tons_ - 1) * sep;
    {
        uint64_t vistos[2] = {0, 0};
        const uint8_t* t = &tomDur_[(size_t)melhorHip_ * kBloco];
        for (int i = 0; i < kBloco; ++i) vistos[t[i] >> 6] |= 1ULL << (t[i] & 63);
        int c = 0;
        for (int w = 0; w < 2; ++w) for (uint64_t v = vistos[w]; v; v &= v - 1) ++c;
        est_.nTons = c;
    }
    if (!est_.travado && est_.blocos == 0) {
        char b[96];
        std::snprintf(b, sizeof b, "procurando sinal OLIVIA %d/%d (qualidade %.1f, limiar %.1f)",
                      tons_, banda_, best, kLimiar);
        est_.texto = b;
    } else {
        est_.texto = recente_;
    }
}

// Decodifica os ultimos 64 simbolos da hipotese s -> spb caracteres.
// Devolve a qualidade: pico de Walsh / desvio do resto (media dos caracteres).
double OliviaCore::decodificaBloco(int s, uint8_t* chars)
{
    const long long ns = nSimb_[s];
    const float* base = &moles_[(size_t)s * kBloco * spb_];
    double qSoma = 0;
    float w[kBloco];
    for (int i = 0; i < spb_; ++i) {
        int bitChave = (i * kDeslocChave) & (kBloco - 1);
        for (int t = 0; t < kBloco; ++t) {
            const int slot = (int)((ns - kBloco + t) % kBloco);
            float v = base[slot * spb_ + (i + t) % spb_];
            if ((kChave >> bitChave) & 1ULL) v = -v;
            w[t] = v;
            bitChave = (bitChave + 1) & (kBloco - 1);
        }
        fwht(w, kBloco);
        int c = 0;
        float pico = 0, soma2 = 0;
        for (int j = 0; j < kBloco; ++j) {
            soma2 += w[j] * w[j];
            if (std::fabs(w[j]) > std::fabs(pico)) { pico = w[j]; c = j; }
        }
        const float ruido = (soma2 - pico * pico) / (kBloco - 1);
        qSoma += std::fabs(pico) / std::sqrt(ruido + 1e-12f);
        chars[i] = (uint8_t)(pico < 0 ? c + kBloco : c);
    }
    return qSoma / spb_;
}

// os caracteres de um bloco para a tela (NUL = ocioso, nao aparece)
void OliviaCore::saida(const uint8_t* chars)
{
    for (int i = 0; i < spb_; ++i) {
        const char c = (char)chars[i];
        if (c == 0) continue;
        if (c == '\r') { pendente_ += '\n'; recente_ += '\n'; }
        else if (c == '\n') { if (ultimoChar_ != '\r') { pendente_ += '\n'; recente_ += '\n'; } }
        else if (c == 8) { if (!recente_.empty()) recente_.pop_back(); }
        else if ((unsigned char)c >= 32 && (unsigned char)c < 127) { pendente_ += c; recente_ += c; }
        ultimoChar_ = c;
    }
    if (recente_.size() > 80) recente_.erase(0, recente_.size() - 80);
}

OliviaCore::Estado OliviaCore::estado() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return est_;
}

std::string OliviaCore::pegarTexto()
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::string s;
    s.swap(pendente_);
    return s;
}

} // namespace masdr
