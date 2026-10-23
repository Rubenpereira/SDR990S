#include "StanagCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace masdr {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFsBB = 4800.0;           // a banda base: 2 amostras por simbolo
constexpr int kSimbQuadro = 256;           // 80 de sync + 176 de dados/referencia
constexpr int kAmostrasQuadro = 2 * kSimbQuadro;
constexpr double kLimDif = 0.33;           // correlacao diferencial (0..1) para candidato
constexpr double kLimCoer = 0.50;          // correlacao coerente (0..1) do candidato
constexpr double kLimConf = 0.45;          // a confirmacao no quadro seguinte
// passos do LMS (normalizado). Escolhidos na bateria de canais (ruido,
// multipercurso de 1 a 3 ms com desvanecimento de 0,3 a 1 Hz): o passo
// maior segue o desvanecimento; nos simbolos conhecidos o passo e o dobro.
constexpr float kMuFF = 0.045f, kMuFB = kMuFF / 2, kMuTreino = 0.04f;
constexpr float kFatConhecido = 2.f;
constexpr double kPllA = 0.03, kPllB = 0.0004;
constexpr int kRuinsMax = 20;              // ~2 s de quadros ruins: perde a trava

// ITA2 (Baudot): 32 valores de 5 bits (o bit 0 e o primeiro a chegar).
// 27 = FIGS (figuras), 31 = LTRS (letras). Figuras do ITA2 internacional.
const char kIta2Ltr[32] = {
    0, 'E', '\n', 'A', ' ', 'S', 'I', 'U',
    '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
    'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q',
    'O', 'B', 'G', 0, 'M', 'X', 'V', 0
};
const char kIta2Fig[32] = {
    0, '3', '\n', '-', ' ', '\'', '8', '7',
    '\r', 0, '4', 0, ',', 0, ':', '(',
    '5', '+', ')', '2', 0, '6', '0', '1',
    '9', '?', 0, 0, '.', '/', '=', 0
};

inline int paridade(unsigned v)
{
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (int)(v & 1u);
}

} // namespace

// ---------------------------------------------------------------------------
StanagCore::StanagCore()
{
    // o sincronismo: PN de 31 bits, x^5+x^2+1, valor inicial 11010 (o
    // primeiro simbolo e o bit de baixo do valor inicial)
    int reg = 0x1A;
    for (int i = 0; i < 80; ++i) {
        const int b = reg & 1;
        syncS_[i] = b ? -1.f : 1.f;
        const int fb = (reg & 1) ^ ((reg >> 2) & 1);
        reg = (reg >> 1) | (fb << 4);
    }
    // o embaralhamento: x^9+x^4+1, todos os bits em 1 no inicio do quadro;
    // o simbolo sai do estado ATUAL e depois o registro anda 3 passos
    reg = 0x1FF;
    for (int j = 0; j < 176; ++j) {
        scr_[j] = (unsigned char)(reg & 7);
        scrC_[j] = std::polar(1.f, (float)(kPi / 4.0 * scr_[j]));
        for (int k = 0; k < 3; ++k) {
            const int fb = (reg & 1) ^ ((reg >> 4) & 1);
            reg = (reg >> 1) | (fb << 8);
        }
    }
    anel_.assign(kAnel, cpx());
    setModo(Params());
}

void StanagCore::setModo(const Params& p)
{
    std::lock_guard<std::mutex> lk(mtx_);
    p_ = p;
    if (p_.userRate != 75 && p_.userRate != 150 && p_.userRate != 300 && p_.userRate != 1200)
        p_.userRate = 600;
    fsIn_ = p_.sampleRate > 0 ? p_.sampleRate : 8000.0;
    ncoInc_ = std::polar(1.0, -2.0 * kPi * p_.centerHz / fsIn_);
    passoIn_ = 3.0 * fsIn_ / kFsBB;              // 5 para 8 kHz
    montarFiltro();
    reiniciarTudo();
}

void StanagCore::montarFiltro()
{
    // passa-baixa a 3*fs (24 kHz): corte em 1900 Hz (o sinal ocupa +-1,4 kHz
    // e pode estar ate 400 Hz fora), janela de Blackman, 3 fases de 24
    nTapFase_ = 24;
    const int n = 3 * nTapFase_;
    const double fsUp = 3.0 * fsIn_;
    const double fc = 1900.0 / fsUp;
    h_.assign(n, 0.f);
    double soma = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i - (n - 1) / 2.0;
        double v = std::fabs(t) < 1e-9 ? 2 * fc : std::sin(2 * kPi * fc * t) / (kPi * t);
        v *= 0.42 - 0.5 * std::cos(2 * kPi * i / (n - 1)) + 0.08 * std::cos(4 * kPi * i / (n - 1));
        h_[i] = (float)v;
        soma += v;
    }
    for (auto& v : h_) v = (float)(v * 3.0 / soma);    // ganho 3 (a interpolacao)
    hist_.assign(nTapFase_, cpx());
    histPos_ = 0;
}

void StanagCore::reiniciarTudo()
{
    nco_ = {1, 0};
    ncoRen_ = 0;
    std::fill(hist_.begin(), hist_.end(), cpx());
    histPos_ = 0;
    nIn_ = 0;
    posUp_ = 0;
    std::fill(anel_.begin(), anel_.end(), cpx());
    nBB_ = 0;
    nivel_ = 0;
    candidato_ = false;
    janela_ = false;
    trava_ = false;
    snr_ = 0;
    contQuadro_ = 0;
    texto_.clear();
    pendente_.clear();
    bitsUsuario_ = 0;
    montarDesentrelacador();
}

void StanagCore::limpar()
{
    std::lock_guard<std::mutex> lk(mtx_);
    texto_.clear();
    pendente_.clear();
    mold_ = 0;
    nDados_ = 0;
    acum_ = 0;
    bitsUsuario_ = 0;
    contQuadro_ = 0;
}

// ---------------------------------------------------------------------------
//  a entrada: mistura, filtra e reamostra para 4800 Hz
// ---------------------------------------------------------------------------
void StanagCore::alimentar(const float* amostras, size_t n)
{
    if (!amostras || !n) return;
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t i = 0; i < n; ++i) {
        const std::complex<double> v = (double)amostras[i] * nco_;
        nco_ *= ncoInc_;
        if (++ncoRen_ >= 2048) { nco_ /= std::abs(nco_); ncoRen_ = 0; }
        hist_[histPos_] = cpx((float)v.real(), (float)v.imag());
        const int novo = histPos_;
        histPos_ = (histPos_ + 1) % nTapFase_;
        ++nIn_;
        // as saidas que ja podem ser calculadas (posicao em 1/3 de amostra)
        while ((double)posUp_ <= 3.0 * (double)(nIn_ - 1)) {
            const long long u = posUp_;
            const long long base = u / 3;               // a amostra de entrada
            const int fase = (int)(u % 3);
            const int atras = (int)((nIn_ - 1) - base);  // 0 = a mais nova
            cpx acc;
            int idx = novo - atras;
            if (idx < 0) idx += nTapFase_;
            for (int k = 0; k < nTapFase_; ++k) {
                acc += hist_[idx] * h_[fase + 3 * k];
                if (--idx < 0) idx = nTapFase_ - 1;
            }
            amostraBB(acc);
            posUp_ = (long long)std::llround((double)posUp_ + passoIn_);
        }
    }
}

void StanagCore::amostraBB(cpx v)
{
    anel_[(size_t)(nBB_ & (kAnel - 1))] = v;
    ++nBB_;
    nivel_ = nivel_ * 0.999 + std::norm(v) * 0.001;
    if (!trava_) {
        buscar();
    } else if (nBB_ > mSync_ + kAmostrasQuadro + (kNF - kFFC) + 4) {
        processarQuadro();
    }
}

// ---------------------------------------------------------------------------
//  a aquisicao
// ---------------------------------------------------------------------------
// correlacao diferencial do sync que comeca em m0 (0..1). O desvio de
// frequencia vira uma fase constante na soma: arg(soma) = 2*pi*df/2400.
double StanagCore::corrDif(long long m0, cpx& soma) const
{
    cpx s;
    double mag = 0;
    cpx ant = z(m0);
    for (int k = 1; k < 80; ++k) {
        const cpx a = z(m0 + 2 * k);
        const cpx d = a * std::conj(ant);
        ant = a;
        s += d * (syncS_[k] * syncS_[k - 1]);
        mag += std::abs(d);
    }
    soma = s;
    return mag > 1e-20 ? std::abs(s) / mag : 0.0;
}

// a banda base num instante fracionario (interpolacao cubica de 4 pontos)
StanagCore::cpx StanagCore::zi(double t) const
{
    const double fl = std::floor(t);
    const long long i = (long long)fl;
    const float f = (float)(t - fl);
    const cpx p0 = z(i - 1), p1 = z(i), p2 = z(i + 1), p3 = z(i + 2);
    const cpx a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const cpx b = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
    const cpx c = -0.5f * p0 + 0.5f * p2;
    return ((a * f + b) * f + c) * f + p1;
}

// |correlacao| do sync num instante fracionario (para seguir o relogio)
double StanagCore::corrSyncFrac(double t0, double df) const
{
    const cpx passo = std::polar(1.f, (float)(-2.0 * kPi * df * 2.0 / kFsBB));
    cpx rot(1.f, 0.f), s;
    for (int k = 0; k < 80; ++k) {
        s += zi(t0 + 2.0 * k) * rot * syncS_[k];
        rot *= passo;
    }
    return std::abs(s);
}

// correlacao coerente do sync em m0 com o desvio df (0..1)
double StanagCore::corrCoerente(long long m0, double df, cpx& c) const
{
    const cpx passo = std::polar(1.f, (float)(-2.0 * kPi * df * 2.0 / kFsBB));
    cpx rot(1.f, 0.f), s;
    double e = 0;
    for (int k = 0; k < 80; ++k) {
        const cpx a = z(m0 + 2 * k);
        s += a * rot * syncS_[k];
        e += std::norm(a);
        rot *= passo;
    }
    c = s;
    return e > 1e-20 ? std::abs(s) / std::sqrt(e * 80.0) : 0.0;
}

void StanagCore::buscar()
{
    const long long ultimo = nBB_ - 1;
    if (candidato_) {
        // a confirmacao: o sync do quadro seguinte, 512 amostras depois
        const long long alvo = candM0_ + kAmostrasQuadro;
        if (ultimo < alvo + 2 * 79 + 2) return;
        double melhor = 0, melhorDf = candDf_;
        long long melhorM = alvo;
        for (long long m = alvo - 2; m <= alvo + 2; ++m)
            for (int d = -3; d <= 3; ++d) {
                cpx c;
                const double v = corrCoerente(m, candDf_ + d * 1.0, c);
                if (v > melhor) { melhor = v; melhorM = m; melhorDf = candDf_ + d * 1.0; }
            }
        candidato_ = false;
        if (melhor >= kLimConf) travar(melhorM, melhorDf);
        return;
    }
    const long long m0 = ultimo - 2 * 79;
    if (m0 < 4) return;
    if (janela_) {
        // ja ha um pico: o PN do sync tem periodo de 31 simbolos, entao 31 e
        // 62 simbolos (62 e 124 amostras) antes e depois do sync certo a
        // correlacao tambem passa do limiar (49 ou 18 de 80 simbolos batem;
        // com silencio antes do sinal, mais ainda). A janela segue mais 140
        // amostras e fica com o maior.
        const double base = janDf_;
        for (int d = -2; d <= 2; ++d) {
            cpx c;
            const double v = corrCoerente(m0, base + d * 1.0, c);
            if (v > janVal_) { janVal_ = v; janM0_ = m0; janDf_ = base + d * 1.0; }
        }
        if (m0 - janIni_ < 140) return;
        janela_ = false;
        candidato_ = true;
        candM0_ = janM0_;
        candDf_ = janDf_;
        candVal_ = janVal_;
        return;
    }
    cpx soma;
    const double v = corrDif(m0, soma);
    if (v < kLimDif) return;
    // o desvio sai da fase da soma; o refino coerente acerta o desvio
    // (o coerente nao aceita mais que ~8 Hz de erro)
    const double df0 = std::arg(soma) * 2400.0 / (2.0 * kPi);
    double melhor = 0, melhorDf = df0;
    for (int d = -8; d <= 8; d += 2) {
        cpx c;
        const double c0 = corrCoerente(m0, df0 + d, c);
        if (c0 > melhor) { melhor = c0; melhorDf = df0 + d; }
    }
    {
        const double base = melhorDf;
        for (int d = -3; d <= 3; ++d) {
            cpx c;
            const double c0 = corrCoerente(m0, base + d * 0.5, c);
            if (c0 > melhor) { melhor = c0; melhorDf = base + d * 0.5; }
        }
    }
    if (melhor < kLimCoer) return;
    janela_ = true;
    janIni_ = m0;
    janM0_ = m0;
    janDf_ = melhorDf;
    janVal_ = melhor;
}

// confere, com o equalizador travado, se o sync nao esta 31 simbolos fora
// (o PN se repete a cada 31: a correlacao ali e ~0,6 da certa)
void StanagCore::conferirPosicao()
{
    const double df = omega_ * kFsBB / (2.0 * kPi);
    cpx c;
    const double v0 = corrCoerente(mSync_, df, c);
    double vMelhor = v0;
    int lado = 0;
    for (int s = -1; s <= 1; s += 2) {
        double v = 0;
        for (int d = -1; d <= 1; ++d) v = std::max(v, corrCoerente(mSync_ + s * 62 + d, df, c));
        if (v > vMelhor * 1.3) { vMelhor = v; lado = s; }
    }
    if (lado == 0) { errPos_ = 0; return; }
    if (lado != ladoPos_) { ladoPos_ = lado; errPos_ = 0; }
    if (++errPos_ >= 3) {
        // re-trava no lugar certo
        long long melhorM = mSync_ + lado * 62;
        double vm = 0;
        for (int d = -2; d <= 2; ++d) {
            const double v = corrCoerente(mSync_ + lado * 62 + d, df, c);
            if (v > vm) { vm = v; melhorM = mSync_ + lado * 62 + d; }
        }
        errPos_ = 0;
        if (vm < kLimConf) return;          // ruido: quem decide e a contagem de quadros ruins
        const int ruins = quadrosRuins_;
        const double taxa = taxaT_;
        travar(melhorM, df);
        quadrosRuins_ = ruins;
        taxaT_ = taxa;
    }
}

void StanagCore::travar(long long m0, double df)
{
    cpx c;
    corrCoerente(m0, df, c);
    trava_ = true;
    mSync_ = m0;
    frac_ = 0.0;
    taxaT_ = 0.0;
    omega_ = 2.0 * kPi * df / kFsBB;
    fase_ = std::arg(c);
    // o equalizador comeca com um coeficiente so, com o ganho e a fase do sync
    const double amp = std::max(1e-9, std::abs(c) / 80.0);
    for (auto& w : w_) w = cpx();
    w_[kFFC] = cpx((float)(1.0 / amp), 0.f);
    for (auto& b : b_) b = cpx();
    for (auto& d : dAnt_) d = cpx();
    primeiro_ = true;
    quadrosRuins_ = 0;
    errPos_ = 0;
    ladoPos_ = 0;
    snr_ = 0;
    contQuadro_ = 0;
    montarDesentrelacador();
}

void StanagCore::destravar()
{
    trava_ = false;
    candidato_ = false;
    janela_ = false;
}

// ---------------------------------------------------------------------------
//  um quadro (256 simbolos) pelo equalizador
// ---------------------------------------------------------------------------
void StanagCore::processarQuadro()
{
    if (!primeiro_) {
        conferirPosicao();
        if (!trava_) return;
    }
    const bool qpsk = p_.userRate >= 1200;
    double mseConh = 0;
    int nConh = 0;
    double ruido = 0.05;              // variancia do erro (para as decisoes moles)
    double ruidoAcc = 0;
    int ruidoN = 0;

    // o primeiro quadro: treina o equalizador no sync antes (3 passadas)
    const int passadas = primeiro_ ? 4 : 1;
    double thetaIni = fase_, omegaIni = omega_;
    for (int ps = 0; ps < passadas; ++ps) {
        const bool treino = ps < passadas - 1;
        const int nSimb = treino ? 80 : kSimbQuadro;
        double theta = thetaIni;
        omega_ = omegaIni;
        for (int k = 0; k < nSimb; ++k) {
            const long long mk = mSync_ + 2LL * k;
            // as amostras do equalizador, ja sem a portadora
            cpx x[kNF];
            const cpx passo = std::polar(1.f, (float)(-omega_));
            cpx rot = std::polar(1.f, (float)(-(theta - omega_ * kFFC)));
            double px = 1e-6;
            for (int j = 0; j < kNF; ++j) {
                x[j] = zi((double)(mk + j - kFFC) + frac_) * rot;
                rot *= passo;
                px += std::norm(x[j]);
            }
            cpx y;
            for (int j = 0; j < kNF; ++j) y += w_[j] * x[j];
            for (int i = 0; i < kNB; ++i) y -= b_[i] * dAnt_[i];

            // o simbolo de referencia (conhecido) ou a decisao
            cpx d;
            bool conhecido = true;
            int jd = -1;
            if (k < 80) {
                d = cpx(syncS_[k], 0.f);
            } else {
                const int j = k - 80;
                if ((j % 48) >= 32) {
                    d = scrC_[j];
                } else {
                    conhecido = false;
                    jd = j;
                    const cpx yd = y * std::conj(scrC_[j]);
                    cpx dd;
                    if (qpsk) {
                        if (std::fabs(yd.real()) >= std::fabs(yd.imag())) dd = cpx(yd.real() >= 0 ? 1.f : -1.f, 0.f);
                        else dd = cpx(0.f, yd.imag() >= 0 ? 1.f : -1.f);
                    } else {
                        dd = cpx(yd.real() >= 0 ? 1.f : -1.f, 0.f);
                    }
                    d = dd * scrC_[j];
                }
            }
            const cpx e = d - y;
            const float mu = treino ? kMuTreino : (conhecido ? kFatConhecido * kMuFF : kMuFF);
            const float g = mu / (float)px;
            for (int j = 0; j < kNF; ++j) w_[j] += g * e * std::conj(x[j]);
            const float gb = (treino ? 2.f * kMuFB : kMuFB) / (float)kNB;
            for (int i = 0; i < kNB; ++i) b_[i] -= gb * e * std::conj(dAnt_[i]);
            for (int i = kNB - 1; i > 0; --i) dAnt_[i] = dAnt_[i - 1];
            dAnt_[0] = d;

            // o PLL (segunda ordem) com o erro de fase da saida
            double err = 0;
            if (std::abs(y) > 0.2f) err = std::arg(y * std::conj(d));
            theta += 2.0 * omega_ + kPllA * err;
            omega_ += kPllB * err;

            if (treino) continue;
            if (conhecido) {
                mseConh += std::norm(e);
                ++nConh;
                ruidoAcc += std::norm(e);
                ++ruidoN;
                if (ruidoN >= 8) ruido = std::max(0.01, ruidoAcc / ruidoN);
            } else {
                // decisoes moles (positivo = bit 1), pesadas pelo ruido do quadro
                const float esc = (float)(1.0 / std::max(0.02, ruido));
                const cpx yd = y * std::conj(scrC_[jd]);
                const float inv = p_.inverter ? -1.f : 1.f;
                if (qpsk) {
                    const cpx r = yd * std::polar(1.f, (float)(-kPi / 4.0));
                    bitCodificado(-r.real() * esc * inv);   // o bit mais antigo
                    bitCodificado(r.imag() * esc * inv);    // o mais novo
                } else {
                    bitCodificado(-yd.real() * esc * inv);
                }
            }
        }
        if (!treino) fase_ = theta;
    }

    // a qualidade do quadro (pelos simbolos conhecidos: sync e referencia)
    const double mse = nConh ? mseConh / nConh : 1.0;
    const double snrQ = 10.0 * std::log10(1.0 / std::max(1e-4, mse));
    snr_ = primeiro_ ? snrQ : snr_ * 0.8 + snrQ * 0.2;
    bool divergiu = !std::isfinite(mse);
    for (const auto& w : w_) if (!std::isfinite(w.real()) || !std::isfinite(w.imag())) divergiu = true;
    // quadro ruim (sinal sumiu ou desvaneceu): o PLL nao aprende com ruido
    if (mse > 0.8) { ++quadrosRuins_; omega_ = omegaIni; }
    else quadrosRuins_ = std::max(0, quadrosRuins_ - 1);

    // o relogio: o transmissor e o nosso audio nao tem exatamente a mesma
    // taxa (ate centenas de ppm). O laco de tempo (proporcional + integral)
    // segue o pico da correlacao do sync, adiantado/atrasado meia amostra.
    {
        const double df = omega_ * kFsBB / (2.0 * kPi);
        const double t0 = (double)mSync_ + frac_;
        const double cedo = corrSyncFrac(t0 - 0.5, df), tarde = corrSyncFrac(t0 + 0.5, df);
        const double err = (cedo + tarde) > 1e-12 ? (tarde - cedo) / (tarde + cedo) : 0.0;
        if (!primeiro_) {
            taxaT_ += 0.04 * err;
            taxaT_ = std::clamp(taxaT_, -0.3, 0.3);    // ate ~1400 ppm
        }
        frac_ += 0.5 * err + taxaT_;
    }
    while (frac_ >= 1.0) { frac_ -= 1.0; mSync_ += 1; fase_ += omega_; }
    while (frac_ < 0.0) { frac_ += 1.0; mSync_ -= 1; fase_ -= omega_; }
    fase_ = std::remainder(fase_, 2.0 * kPi);
    mSync_ += kAmostrasQuadro;
    primeiro_ = false;
    ++contQuadro_;
    if (divergiu || quadrosRuins_ >= kRuinsMax) destravar();
}

// ---------------------------------------------------------------------------
//  o desentrelacador (recepcao): o bit r de cada ciclo de 32 entra na fila
//  r, que atrasa k*(31-r) ciclos; a saida do ciclo e lida na ordem (9q) mod 32.
//  As filas comecam com zero = "apagado" (o Viterbi nao usa).
// ---------------------------------------------------------------------------
void StanagCore::montarDesentrelacador()
{
    if (p_.userRate >= 1200) kIlv_ = p_.longIlv ? 24 : 2;
    else kIlv_ = p_.longIlv ? 12 : 1;
    for (int r = 0; r < 32; ++r) {
        fila_[r].assign((size_t)kIlv_ * (31 - r), 0.f);
        filaPos_[r] = 0;
    }
    cicloPos_ = 0;
    ciclos_ = 0;
    rep_ = p_.userRate == 300 ? 2 : p_.userRate == 150 ? 4 : p_.userRate == 75 ? 8 : 1;
    repPos_ = 0;
    acc1_ = acc2_ = 0.f;
    for (auto& m : met_) m = 0.f;
    dec_.assign(2 * kProf, 0);
    passos_ = 0;
    mold_ = 0;
    nDados_ = 0;
    acum_ = 0;
    figuras_ = false;
    somReg_ = 0;
    inMsg_ = false;
}

void StanagCore::bitCodificado(float v)
{
    ciclo_[cicloPos_] = v;
    if (++cicloPos_ < 32) return;
    cicloPos_ = 0;
    float saida[32];
    for (int r = 0; r < 32; ++r) {
        std::vector<float>& f = fila_[r];
        if (f.empty()) { saida[r] = ciclo_[r]; continue; }
        saida[r] = f[filaPos_[r]];
        f[filaPos_[r]] = ciclo_[r];
        if (++filaPos_[r] >= (int)f.size()) filaPos_[r] = 0;
    }
    ++ciclos_;
    // so depois que ~78% das filas tem dados (o resto vai como apagado)
    if (ciclos_ < 24LL * kIlv_) return;
    for (int q = 0; q < 32; ++q) {
        const float c = saida[(9 * q) & 31];
        // os pares (T1,T2) repetidos rep_ vezes (300/150/75 bps) sao somados
        if ((repPos_ & 1) == 0) acc1_ += c; else acc2_ += c;
        if (++repPos_ >= 2 * rep_) {
            viterbi(acc1_, acc2_);
            repPos_ = 0;
            acc1_ = acc2_ = 0.f;
        }
    }
}

// ---------------------------------------------------------------------------
//  o Viterbi (K=7, T1 = 133 / T2 = 171 octal). O estado guarda as 6 entradas
//  anteriores (bit 0 = a mais recente). T1 usa os atrasos 0,2,3,5,6 e T2 os
//  atrasos 0,1,2,3,6.
// ---------------------------------------------------------------------------
void StanagCore::viterbi(float a, float b)
{
    float nova[64];
    uint64_t dec = 0;
    for (int ns = 0; ns < 64; ++ns) {
        const int u = ns & 1;
        float melhor = -1e30f;
        int esc = 0;
        for (int x = 0; x < 2; ++x) {
            const int ps = (ns >> 1) | (x << 5);
            const int t1 = u ^ paridade((unsigned)ps & 0x36u);
            const int t2 = u ^ paridade((unsigned)ps & 0x27u);
            const float m = met_[ps] + (t1 ? a : -a) + (t2 ? b : -b);
            if (m > melhor) { melhor = m; esc = x; }
        }
        nova[ns] = melhor;
        if (esc) dec |= 1ULL << ns;
    }
    float maior = nova[0];
    int melhorEst = 0;
    for (int i = 1; i < 64; ++i) if (nova[i] > maior) { maior = nova[i]; melhorEst = i; }
    for (int i = 0; i < 64; ++i) met_[i] = nova[i] - maior;
    const int n = (int)dec_.size();
    dec_[(size_t)(passos_ % n)] = dec;
    ++passos_;
    if (passos_ < kProf) return;
    // o traceback: kProf passos para tras a partir do melhor estado
    int s = melhorEst;
    long long t = passos_ - 1;
    for (int i = 0; i < kProf - 1; ++i, --t) {
        const int x = (int)((dec_[(size_t)(t % n)] >> s) & 1ULL);
        s = (s >> 1) | (x << 5);
    }
    bitUsuario(s & 1);
}

// ---------------------------------------------------------------------------
//  os bits de usuario (depois do Viterbi)
// ---------------------------------------------------------------------------
void StanagCore::bitUsuario(int b)
{
    ++bitsUsuario_;
    somReg_ = (somReg_ << 1) | (uint32_t)(b & 1);
    if (somReg_ == 0x4B65A5B2u) inMsg_ = false;          // EOM
    else if (!inMsg_ && somReg_ == 0x03873C3Cu) inMsg_ = true;   // SOM
    // em ITA2 o texto flui sempre (os receptores comerciais nao esperam o
    // SOM para abrir a moldura 5N1); nos outros, so depois do SOM
    if (!inMsg_ && p_.framing != 0) return;
    if (p_.framing == 0) { caractere(b); return; }
    if (p_.framing == 1) {
        // 8N1: start(0), 8 dados (LSB primeiro), stop(1)
        switch (mold_) {
        case 0: if (b == 0) { mold_ = 1; acum_ = 0; nDados_ = 0; } break;
        case 1:
            acum_ |= (b & 1) << nDados_;
            if (++nDados_ >= 8) mold_ = 2;
            break;
        case 2:
            if (b) {
                const char c = (char)(acum_ & 0xFF);
                if ((c >= 32 && c < 127) || c == '\n') { texto_.push_back(c); pendente_.push_back(c); }
                else if (c == '\r') { texto_.push_back('\n'); pendente_.push_back('\n'); }
            }
            mold_ = 0;
            break;
        }
        return;
    }
    // binario: um byte (hexadecimal) a cada 8 bits
    if (mold_ == 0) { acum_ = 0; nDados_ = 0; mold_ = 1; }
    acum_ |= (b & 1) << nDados_;
    if (++nDados_ >= 8) {
        char buf[4];
        std::snprintf(buf, sizeof buf, "%02X", (unsigned)(acum_ & 0xFF));
        texto_ += buf;
        pendente_ += buf;
        mold_ = 0;
    }
}

// a moldura 5N1 do ITA2: start (0), 5 bits (o primeiro e o de baixo), stop (1)
void StanagCore::caractere(int b)
{
    switch (mold_) {
    case 0:
        if (b == 0) { mold_ = 1; nDados_ = 0; acum_ = 0; }
        break;
    case 1:
        acum_ |= (b & 1) << nDados_;
        if (++nDados_ >= 5) mold_ = 2;
        break;
    case 2:
        if (b) {
            const int v = acum_ & 0x1F;
            char c = 0;
            if (v == 31) figuras_ = false;
            else if (v == 27) figuras_ = true;
            else c = figuras_ ? kIta2Fig[v] : kIta2Ltr[v];
            if (c == '\r') c = '\n';
            else if (c == '\n' && !texto_.empty() && texto_.back() == '\n') c = 0;
            if (c) {
                texto_.push_back(c);
                pendente_.push_back(c);
                if (texto_.size() > 20000) texto_.erase(0, 10000);
            }
        }
        mold_ = 0;
        break;
    }
}

// ---------------------------------------------------------------------------
StanagCore::Estado StanagCore::estado() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    Estado e;
    e.ok = trava_;
    e.nivelDb = 10.0 * std::log10(std::max(1e-12, nivel_));
    e.desvioHz = omega_ * kFsBB / (2.0 * kPi);
    e.faseGraus = fase_ * 180.0 / kPi;
    e.snrDb = snr_;
    e.quadros = contQuadro_;
    e.bits = bitsUsuario_;
    e.texto = texto_;
    return e;
}

std::string StanagCore::pegarTexto()
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::string s;
    s.swap(pendente_);
    return s;
}

} // namespace masdr
