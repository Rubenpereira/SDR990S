#pragma once
// ---------------------------------------------------------------------------
//  Ft8Core - decodificador FT8 / FT4 (os modos do WSJT-X) nativo
//
//  A parte pesada (sincronismo pelas matrizes de Costas, LDPC, CRC e a
//  desmontagem das mensagens de 77 bits) e o ft8_lib (MIT, Karlis Goba),
//  em third_party/ft8_lib. Aqui fica o resto:
//
//   - o relogio: FT8 anda em periodos de 15 s (FT4: 7,5 s) contados no
//     relogio UTC. Cada amostra ganha uma hora (hora do PC na chegada do
//     bloco, corrigida devagar para nao tremer); a amostra 0 do buffer e o
//     inicio do periodo. Entrou no meio de um periodo: espera o proximo.
//   - a analise espectral (STFT com janela de Hann, 2x no tempo e 2x na
//     frequencia), que monta a "cachoeira" que o ft8_lib procura.
//   - a decodificacao numa thread propria: leva algumas centenas de ms e
//     nao pode segurar o audio nem a tela.
//   - o SNR em 2500 Hz (a mesma escala do WSJT-X) e o DT.
//
//  O audio chega na taxa de trabalho dos decodificadores (8 kHz): a banda do
//  FT8 vai de 200 a 3000 Hz, cabe com folga.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace masdr {

class Ft8Core {
public:
    struct Mensagem {
        int hora = 0;          // HHMMSS (UTC) do inicio do periodo
        int snr = 0;           // dB em 2500 Hz, igual ao WSJT-X (aproximado)
        float dt = 0;          // s, em relacao ao inicio nominal (0,5 s depois do periodo)
        float hz = 0;          // tom mais baixo, no audio (Hz)
        std::string texto;     // "CQ PU1ABC GG87", "PY2XX PU1ABC -12" ...
    };

    Ft8Core(double taxa, bool ft4);
    ~Ft8Core();
    Ft8Core(const Ft8Core&) = delete;
    Ft8Core& operator=(const Ft8Core&) = delete;

    // Audio (-1..1) na taxa de trabalho. utcFim = hora UTC (segundos desde
    // 1970, com fracao) da ULTIMA amostra do bloco.
    void feed(const float* x, size_t n, double utcFim);

    // Para teste: decodifica um periodo ja alinhado (amostra 0 = inicio do
    // periodo), na hora, sem thread.
    std::vector<Mensagem> decodificarAgora(const float* x, size_t n, int hora);

    std::vector<Mensagem> pegar();      // mensagens novas desde a ultima chamada

    bool   ft4() const;
    double periodo() const;             // 15 ou 7,5 s
    double segundoNoPeriodo() const;    // onde esta o audio agora dentro do periodo
    bool   esperandoInicio() const;     // entrou no meio do periodo: aguarda o proximo
    bool   decodificando() const;
    int    noUltimoPeriodo() const;     // mensagens no ultimo periodo decodificado
    int    total() const;
    double ultimoNivelDb() const;       // nivel medio do audio no ultimo periodo (dBFS)

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace masdr
