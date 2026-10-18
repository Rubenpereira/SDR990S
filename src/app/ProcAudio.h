#pragma once
// ---------------------------------------------------------------------------
//  ProcAudio - os redutores de ruido da pagina, agora dentro do programa
//  (so na versao Windows 7, que toca o audio direto na placa - ver SomNativo).
//
//  E a MESMA conta que a pagina faz no navegador, traduzida linha a linha do
//  index.html, na mesma ordem:
//
//     silenciar (rampa de 8 ms: MUTE, squelch, troca de frequencia)
//       -> NB  (slider "Noise Blanker")
//       -> NR ESPECTRAL (botao + FORCA 0-100: log-MMSE com piso por
//                        estatistica minima, STFT de 4096 a 48 kHz)
//       -> Redutor de Ruido (slider: ganho de Wiener em blocos de 64)
//       -> Tom (slider grave/agudo: prateleiras em 350 Hz e 2500 Hz)
//
//  O volume e a saturacao suave ficam no SomNativo, na saida.
//  Os ajustes chegam da tela por POST /api/audio.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace masdr {

struct AjusteAudio {
    bool  mudo = true;        // MUTE, squelch fechado, troca de frequencia
    float volume = 0.f;       // 0..8 (mesma curva da pagina: (v/100)^1,5 x 8)
    float nb = 0.f;           // 0..1  slider Noise Blanker
    bool  nrEsp = false;      // botao NR ESPECTRAL
    int   nrEspForca = 40;    // 0..100
    float nr = 0.f;           // 0..1  slider Redutor de Ruido
    int   tom = 50;           // 0..100 (50 = plano)
    // ---- painel SDR-990S ----
    bool  notch = false;      // NCH: notch manual
    float notchHz = 1000.f;
    bool  notchLargo = false; //   SEL: estreito / largo
    bool  bef = false;        // BEF: filtro eliminador de faixa (largo, fundo)
    float befHz = 1500.f;
    bool  aNotch = false;     // A.NCH: notch automatico (segue a portadora)
    bool  bc = false;         // BC: cancelador de batimentos (varios tons)
    bool  apf = false;        // APF: filtro de pico de audio (CW)
    float apfHz = 700.f;
    bool  proc = false;       // PROC: compressor de audio (sinais fracos)
    float procNivel = 0.5f;   // 0..1
    bool igual(const AjusteAudio& o) const {
        return mudo == o.mudo && volume == o.volume && nb == o.nb && nrEsp == o.nrEsp &&
               nrEspForca == o.nrEspForca && nr == o.nr && tom == o.tom && notch == o.notch &&
               notchHz == o.notchHz && notchLargo == o.notchLargo && bef == o.bef && befHz == o.befHz &&
               aNotch == o.aNotch && bc == o.bc && apf == o.apf && apfHz == o.apfHz &&
               proc == o.proc && procNivel == o.procNivel;
    }
};

class NrEspectral;   // interno (ProcAudio.cpp)

class ProcAudio {
public:
    ProcAudio();
    ~ProcAudio();

    void ajustar(const AjusteAudio& a);
    AjusteAudio ajuste() const;

    // Processa no lugar. Taxa fixa de 48 kHz (o SomNativo ja reamostrou).
    void processar(float* x, size_t n);

    static constexpr double kTaxa = 48000.0;

private:
    mutable std::mutex m_;
    AjusteAudio aj_;

    // silenciar
    float rampa_ = 0.f;

    // NB
    float nbEnv_ = 0.f;

    // NR espectral
    std::unique_ptr<NrEspectral> esp_;
    bool espLigadoAntes_ = false;

    // Redutor de Ruido (slider)
    double nrPiso_ = 0.0;
    double nrUltimoGanho_ = 1.0;
    void aplicarNR(float* x, size_t n, double nivel);

    // Tom: duas biquads (prateleira grave 350 Hz, aguda 2500 Hz)
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float passa(float x) {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return float(y);
        }
    };
    Biquad grave_, agudo_;
    // painel: notch, BEF, APF
    Biquad notch_, bef1_, bef2_, apf_;
    float notchF_ = -1, befF_ = -1, apfF_ = -1; bool notchL_ = false;
    static void projetarNotch(Biquad& b, double f, double q);
    static void projetarPassaFaixa(Biquad& b, double f, double q);
    // A.NCH / BC: preditor NLMS (o que e periodico - tom - e previsto e tirado)
    struct Lms {
        std::vector<float> w, h;   // pesos e historico
        size_t pos = 0;
        void preparar(size_t L) { if (w.size() != L) { w.assign(L, 0.f); h.assign(L * 2, 0.f); pos = 0; } }
        float passo(float atual, float atrasada, float mu);
    };
    Lms anotch_, bc_;
    std::vector<float> atrasoA_, atrasoB_; size_t posA_ = 0, posB_ = 0;
    // PROC
    float procEnv_ = 0.f, procG_ = 1.f;
    int tomAtual_ = -1;
    void calcularTom(int tom);
};

} // namespace masdr
