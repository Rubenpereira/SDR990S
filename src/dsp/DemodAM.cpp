#include "DemodAM.h"
#include "AjustesDsp.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace masdr {

void DemodAM::process(const std::complex<float>* iq, size_t n, uint32_t sampleRate)
{
    if (!iq || n == 0 || !onAudio_) return;

    // Pre-decimação para reduzir taxas de amostragem altas para ~250 kHz
    uint32_t factor = 1;
    if (sampleRate >= 2000000) factor = 8;
    else if (sampleRate >= 1000000) factor = 4;
    else if (sampleRate >= 500000) factor = 2;

    const size_t step = audioDecimStep(sampleRate / factor);

    std::vector<std::complex<float>> decIQ;
    const uint32_t rate = processInput(iq, n, sampleRate, factor, step, decIQ);
    if (decIQ.empty()) return;

    const std::complex<float>* pIq = decIQ.data();
    const size_t pN = decIQ.size();

    const uint32_t actualAudioRate = rate / step;

    // Pre-filtro IIR LPF (RF) e Pós-filtros de Áudio
    // A frequência de corte de RF é metade da largura de banda (BW) selecionada na UI multiplicada por 1.35 (folga).
    // Isso evita a atenuação precoce da borda e elimina o tom abafado/fanhoso do AM.
    float wantFc = (bwHz_ > 0) ? (static_cast<float>(bwHz_) / 2.0f) * 1.35f : 6750.0f;
    wantFc = std::clamp(wantFc, 3000.0f, 15000.0f); // limites de segurança expandidos para AM claro

    const float wantFs = static_cast<float>(rate);
    const float audioFs = static_cast<float>(actualAudioRate);

    const int loPainel = std::clamp(AjustesDsp::g().loCutHz.load(), 0, 1500);
    if (lpfFs_ != wantFs || audioFs_ != audioFs || lpfFc_ != wantFc || lastLo_ != loPainel) {
        lpf_.setCutoff(wantFc, wantFs);
        lpf_.reset();

        // O pós-filtro de áudio também é configurado com folga de 30% (wantFc * 1.30f)
        const float audioLpfFc = std::min(18000.0f, wantFc * 1.30f);
        audioLpf_.setCutoff(audioLpfFc, audioFs);
        audioLpf_.reset();
        
        audioHpf_.setCutoff(std::max(60.0f, loPainel * 0.5f), audioFs);   // LO-CUT do painel (250 = 125 Hz)
        lastLo_ = loPainel;
        audioHpf_.reset();

        lpfFs_ = wantFs;
        audioFs_ = audioFs;
        lpfFc_ = wantFc;
    }

    std::vector<int16_t> pcm;
    pcm.reserve(pN / step + 1);
    const int agcModo = AjustesDsp::g().agc.load();
    const float agcLib = AjustesDsp::liberacao(audioFs);

    for (size_t i = 0; i + step <= pN; i += step) {

        // Detector de envelope: media do modulo sobre o bloco decimado.
        // Usar a media em vez de amostrar 1 ponto elimina alias de audio
        // e suaviza o envelope -> menos estalo.
        float envSum = 0.0f;
        for (size_t j = 0; j < step; ++j) {
            const std::complex<float> s = lpf_.process(pIq[i + j]);
            envSum += std::sqrt(s.real() * s.real() + s.imag() * s.imag());
        }
        float env = envSum / static_cast<float>(step);

        // Remove portadora AM (DC do envelope).
        // Constante 0.002 @ 48 kHz -> fc ~ 15 Hz: mantem baix frequencias de audio.
        envDc_ += 0.002f * (env - envDc_);
        float audio = env - envDc_;

        // Proteção de auto-recuperação contra nan/inf causados por transientes de chaveamento de RF
        if (std::isnan(audio) || std::isinf(audio)) {
            lpf_.reset();
            audioLpf_.reset();
            audioHpf_.reset();
            agc_ = 0.05f;
            envDc_ = 0.0f;
            return;
        }

        // AGC com ataque rapido / liberacao lenta:
        //   Ataque  ~5 ms @ 48 kHz  : alpha ~ 0.004  -> reage rapido a picos
        //   Liberacao ~1 s @ 48 kHz : alpha ~ 0.00002 -> nao "bombeia" entre palavras
        // Resultado: sem estalo em sinais fortes, sem colapso de nivel em pausas.
        const float absA = std::abs(audio);
        if (agcModo == 0) {
            agc_ = 0.04f;                       // AGC OFF
        } else if (absA > agc_) {
            agc_ += 0.004f   * (absA - agc_);   // ataque rapido
        } else {
            agc_ += agcLib * (absA - agc_);     // liberacao (SLOW/MID/FAST)
        }
        audio *= 0.75f / std::max(0.01f, agc_);

        // Pos-filtro de audio: LPF 5 kHz remove chiado HF; HPF 80 Hz remove DC residual.
        // Ordem: LPF primeiro, depois HPF (evita acumulo de DC no integrador do HPF).
        audio = audioHpf_.process(audioLpf_.process(audio));

        // Limitador linear simples (sem tanh que deforma o envelope de amplitude AM)
        audio = std::clamp(audio, -1.0f, 1.0f);
        pcm.push_back(static_cast<int16_t>(audio * 30000.0f));
    }

    if (!pcm.empty()) onAudio_(pcm, actualAudioRate);
}

} // namespace masdr
