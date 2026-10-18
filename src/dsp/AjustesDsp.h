#pragma once
// ---------------------------------------------------------------------------
//  AjustesDsp - ajustes do painel que os demoduladores leem a cada bloco
//  (AGC SLOW/MID/FAST/OFF, corte baixo do SSB/AM, tom do CW).
//  Atomics: a tela escreve, a thread do dongle le, sem trava.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cmath>

namespace masdr {

struct AjustesDsp {
    std::atomic<int>   agc{2};          // 0 OFF, 1 SLOW, 2 MID, 3 FAST
    std::atomic<int>   loCutHz{250};    // corte baixo do audio (SSB / AM)
    std::atomic<float> cwTomHz{700.f};  // CW PITCH

    static AjustesDsp& g() { static AjustesDsp a; return a; }

    // Coeficiente de liberacao do AGC por amostra, para a taxa fs.
    // MID = ~1 s (o valor fixo de antes), SLOW = 3 s, FAST = 0,15 s.
    static float liberacao(float fs, float lentoS = 3.0f, float medioS = 1.0f, float rapidoS = 0.15f) {
        const int m = g().agc.load();
        const float tau = m == 1 ? lentoS : m == 3 ? rapidoS : medioS;
        return 1.f - std::exp(-1.f / (tau * (fs > 1.f ? fs : 48000.f)));
    }
};

} // namespace masdr
