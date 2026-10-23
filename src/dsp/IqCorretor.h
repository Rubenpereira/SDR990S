#pragma once
// ---------------------------------------------------------------------------
//  IqCorretor - corrige o desequilibrio de amplitude e de fase entre os
//  canais I e Q do receptor (o "Corrigir IQ" da janela de Configuração).
//
//  O problema: o dongle tem dois conversores levemente diferentes - I pode
//  vir mais forte que Q (desiquilibrio de amplitude) e a separação entre
//  eles não fica em exatamente 90 graus (erro de fase). O resultado na tela
//  e o "sinal espelho": a estação em +f cria uma copia fraca em -f (e vice
//  versa), simétrica em torno do centro. E a assinatura classica de IQ
//  imperfeito nos SDR de conversão direta (RTL-SDR, RTL-TCP).
//
//  A correção: mede, por pacotes de ~0,13 s, a média e a matriz de co-
//  variancia 2x2 do IQ [  (ii, iq), (iq, qq)  ] e aplica a transformação
//  que "branqueia" os dois canais (Whitening por Cholesky, W = inv(L)):
//
//        L = chol(C);  y = k · L^-1 · (x - m);  k = sqrt((ii+qq)/2)
//
//  O k devolve a potencia original (o branqueamento não pode mudar o
//  volume). O estimador roda sempre (custa 5 somas por amostra), para a
//  correção ligar no ato; a MATRIX aplicada só move 6% por pacote, para
//  nunca dar salto audível. Sinal constante ou canal morto não refaz a
//  medicao (evita coeficientes gigantes).
// ---------------------------------------------------------------------------
#include <complex>
#include <cstddef>
#include <utility>
#include <cmath>

namespace masdr {

struct IqCorretor {
    // uma janela de ~0,13 s a 1,024 Msps (o coeficiente segue o hardware,
    // que não muda, então pode ser longo)
    static constexpr size_t kJanela = 131072;
    static constexpr double kEps = 1e-12;

    void reset()
    {
        n_ = 0;
        sI_ = sQ_ = sII_ = sQQ_ = sIQ_ = 0.0;
        // estado atual e alvo começam "sem correção" (a=c=1 é unitário)
        mI_ = mQ_ = 0.0; a_ = 1.0; b_ = 0.0; c_ = 1.0;
        alvoM_ = {0.0, 0.0}; alvoA_ = 1.0; alvoB_ = 0.0; alvoC_ = 1.0;
    }

    // mede o IQ cru (um pacote por quadro; roda sempre, correção ligada ou não)
    void medir(const std::complex<float>* v, size_t n)
    {
        if (!v || n == 0) return;
        double si = 0, sq = 0, sii = 0, sqq = 0, siq = 0;
        for (size_t i = 0; i < n; ++i) {
            const double re = v[i].real(), im = v[i].imag();
            si += re; sq += im; sii += re * re; sqq += im * im; siq += re * im;
        }
        sI_ += si; sQ_ += sq; sII_ += sii; sQQ_ += sqq; sIQ_ += siq;
        n_ += n;
        if (n_ >= kJanela) refaz();
    }

    // um passo de suavização por pacote: a matrix domanda até o alvo sem salto
    void pacote()
    {
        constexpr double f = 0.06;
        mI_ += (alvoM_.first - mI_) * f;
        mQ_ += (alvoM_.second - mQ_) * f;
        a_ += (alvoA_ - a_) * f;
        b_ += (alvoB_ - b_) * f;
        c_ += (alvoC_ - c_) * f;
    }

    // a correção de cada amostra (copia o domínio do IirDcBlock)
    inline std::complex<float> corrigir(const std::complex<float>& x) const noexcept
    {
        const double re = x.real(), im = x.imag();
        const double r = re - mI_, i = im - mQ_;
        return { float(a_ * r), float(b_ * r + c_ * i) };
    }

private:
    void refaz()
    {
        const double mI = sI_ / n_, mQ = sQ_ / n_;
        const double ii = sII_ / n_ - mI * mI;    // variancia de I (centrada)
        const double qq = sQQ_ / n_ - mQ * mQ;    // variancia de Q (centrada)
        const double iq = sIQ_ / n_ - mI * mQ;    // co-variancia
        n_ = 0;
        sI_ = sQ_ = sII_ = sQQ_ = sIQ_ = 0.0;

        // silêncio constante, canal morto ou falha além de qualquer
        // hardware real (±14 dB): não refaz, fica no coeficiente de antes
        if (ii < kEps || qq < kEps) return;
        const double razao = ii / qq;
        if (razao > 25.0 || razao < 1.0 / 25.0) return;

        const double L11 = std::sqrt(ii);
        const double L21 = iq / L11;
        const double d = qq - L21 * L21;
        if (d < kEps) return;
        const double L22 = std::sqrt(d);
        const double k = std::sqrt(0.5 * (ii + qq));

        alvoM_ = {mI, mQ};
        alvoA_ = k / L11;                        // y_i = a · (i - mI)
        alvoB_ = -L21 * k / (L11 * L22);         // y_q = b · (i - mI) + c · (q - mQ)
        alvoC_ = k / L22;
    }

    // somas da janela de medida
    size_t n_ = 0;
    double sI_ = 0, sQ_ = 0, sII_ = 0, sQQ_ = 0, sIQ_ = 0;
    // matrix aplicada (suavizada) e o alvo dela
    double mI_ = 0, mQ_ = 0, a_ = 1.0, b_ = 0.0, c_ = 1.0;
    std::pair<double, double> alvoM_ = {0.0, 0.0};
    double alvoA_ = 1.0, alvoB_ = 0.0, alvoC_ = 1.0;
};

} // namespace masdr
