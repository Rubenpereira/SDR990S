#pragma once
// ---------------------------------------------------------------------------
//  StanagCore - decodificador STANAG 4285 do audio do radio
//
//  O STANAG 4285 (NATO, "Characteristics of 1200/2400/3600 bps Single Tone
//  Modulators/Demodulators for HF Radio Links") e um modem de portadora unica
//  para HF, usado pelas frotas navais (Franca, Italia, Holanda, Noruega...).
//
//  Como funciona (conferido com a norma e com uma gravacao real de 1200L:
//  os bits decodificados, codificados de novo, batem com os recebidos):
//
//  * Portadora de 1800 Hz no audio (USB), PSK a 2400 Bd.
//  * Quadro de 256 simbolos (106,67 ms): 80 de SINCRONIZACAO em BPSK
//    (PN x^5+x^2+1, valor inicial 11010, 0 -> fase 0, 1 -> fase 180), e
//    176 de dados/referencia: 32 dados, 16 referencia, 32, 16, 32, 16, 32.
//  * Os 176 simbolos sao embaralhados (soma modulo 8 das fases) pela
//    sequencia de x^9+x^4+1 com todos os bits em 1 no inicio do quadro:
//    o simbolo j usa os 3 bits de baixo (4x2+2x1+x0) e DEPOIS o registro
//    anda 3 passos. A referencia e o simbolo 0 embaralhado (fase conhecida).
//  * Dados: 2-PSK (0 -> 0, 1 -> 180) nos 75..600 bps; 4-PSK no 1200 bps
//    (dibit "mais antigo, mais novo": 00->0, 01->90, 11->180, 10->270).
//  * FEC convolucional K=7, taxa 1/2 (T1 = 133, T2 = 171 em octal, com o bit
//    mais significativo = o bit que entra; T1 sai primeiro). 300/150/75 bps
//    repetem cada PAR (T1,T2) 2/4/8 vezes.
//  * Entrelacador convolucional de 32 filas: o bit q de cada ciclo de 32
//    entra na fila 9q mod 32, a fila r atrasa k*r ciclos e a saida le as
//    filas na ordem 0..31. k = 12 (longo, 75..600) / 24 (longo, 1200) /
//    1 (curto) / 2 (curto, 1200): 10,24 s ou 0,85 s.
//  * O texto: ITA2 (Baudot) 5N1, como nos receptores comerciais; 8N1 ASCII
//    e binario tambem.
//
//  O receptor: mistura para banda base, filtro e reamostragem para 4800 Hz
//  (2 amostras por simbolo); acha o sincronismo com a correlacao
//  diferencial (independe do desvio, ate +-400 Hz) e confirma no quadro
//  seguinte; um equalizador DFE (20 coeficientes T/2 + 10 de realimentacao,
//  LMS treinado no sync e na referencia, decisao direta nos dados) segue o
//  multipercurso, o desvio e o relogio; decisoes moles vao ao
//  desentrelacador e ao Viterbi. O trafego quase sempre e cifrado.
// ---------------------------------------------------------------------------
#include <complex>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace masdr {

class StanagCore {
public:
    struct Params {
        double sampleRate = 8000.0;
        double centerHz   = 1800.0;   // a frequencia da subportadora no audio
        int    userRate   = 600;       // 75, 150, 300, 600 ou 1200 bps
        bool   longIlv    = true;      // entrelacador longo (10,24 s) ou curto
        int    framing    = 0;         // 0 = 5N1 ITA2, 1 = 8N1 ASCII, 2 = binario
        bool   inverter   = false;     // gira a fase em 180 graus
    };

    struct Estado {
        bool   ok = false;             // sinal travado (sincronizacao achada)
        double nivelDb = -100.0;       // nivel do sinal
        double desvioHz = 0.0;         // desvio da portadora medido
        double faseGraus = 0.0;        // a fase da portadora
        double snrDb = 0.0;            // SNR medido nos simbolos conhecidos
        int    quadros = 0;            // quadros de dados processados
        int    bits = 0;               // bits de usuario decodificados
        std::string texto;             // o texto decodificado
    };

    StanagCore();

    void setModo(const Params& p);
    void alimentar(const float* amostras, size_t n);   // audio em 8 kHz
    Estado estado() const;
    std::string pegarTexto();        // o texto novo (para a tela)
    void limpar();

private:
    using cpx = std::complex<float>;
    static constexpr int kAnel = 8192;      // amostras de banda base (1,7 s a 4800 Hz)
    static constexpr int kNF = 20;          // coeficientes do equalizador (T/2): 10 simbolos
    static constexpr int kFFC = 4;          // o coeficiente do instante do simbolo
    static constexpr int kNB = 10;          // realimentacao (simbolos decididos)

    void reiniciarTudo();
    void montarFiltro();
    void amostraBB(cpx v);                  // uma amostra nova a 4800 Hz
    cpx  z(long long m) const { return anel_[(size_t)(m & (kAnel - 1))]; }
    cpx  zi(double t) const;                // interpolada (cubica)
    double corrSyncFrac(double t0, double df) const;
    // a aquisicao
    double corrDif(long long m0, cpx& soma) const;
    double corrCoerente(long long m0, double df, cpx& c) const;
    void buscar();
    void travar(long long m0, double df);
    void conferirPosicao();
    void destravar();
    // o demodulador
    void processarQuadro();
    // a camada de dados
    void montarDesentrelacador();
    void bitCodificado(float v);            // valor mole (positivo = 1, 0 = apagado)
    void viterbi(float a, float b);
    void bitUsuario(int b);
    void caractere(int b);

    mutable std::mutex mtx_;
    Params p_;

    // a entrada: misturador, filtro e reamostragem 8000 -> 4800 Hz (3/5)
    double fsIn_ = 8000.0;
    std::complex<double> nco_{1, 0}, ncoInc_{1, 0};
    int ncoRen_ = 0;
    std::vector<float> h_;                  // o filtro (a 24 kHz), 3 fases
    int nTapFase_ = 0;
    std::vector<cpx> hist_;                 // as ultimas entradas misturadas
    int histPos_ = 0;
    long long nIn_ = 0;                     // amostras de entrada
    long long posUp_ = 0;                   // a proxima saida (em 1/3 de amostra)
    double passoIn_ = 5.0;                  // passo da saida em 1/3 de amostra de entrada

    // a banda base (4800 Hz)
    std::vector<cpx> anel_;
    long long nBB_ = 0;                     // amostras de banda base recebidas
    double nivel_ = 0.0;

    // a aquisicao
    float syncS_[80];                       // +1 / -1
    unsigned char scr_[176];                // o embaralhamento (0..7)
    cpx scrC_[176];                         // exp(j*pi/4*scr)
    bool candidato_ = false;
    long long candM0_ = 0;
    double candDf_ = 0.0;
    double candVal_ = 0.0;
    bool janela_ = false;                   // achou um pico: procura o maior por 70 amostras
    long long janIni_ = 0, janM0_ = 0;
    double janDf_ = 0.0, janVal_ = 0.0;
    int errPos_ = 0, ladoPos_ = 0;          // o sync parece 31 simbolos fora

    // travado
    bool trava_ = false;
    long long mSync_ = 0;                   // o sync do proximo quadro (amostra)
    double omega_ = 0.0;                    // desvio total (rad/amostra a 4800)
    double fase_ = 0.0;                     // a fase da portadora no mSync_
    cpx w_[kNF];
    cpx b_[kNB];
    cpx dAnt_[kNB];
    double frac_ = 0.0;                     // o instante fracionario do quadro (amostras)
    double taxaT_ = 0.0;                    // a deriva do relogio (amostras por quadro)
    bool primeiro_ = true;
    int quadrosRuins_ = 0;
    double snr_ = 0.0;
    int contQuadro_ = 0;

    // o desentrelacador (as 32 filas; a fila r atrasa k*(31-r) ciclos)
    int kIlv_ = 12;
    std::vector<float> fila_[32];
    int filaPos_[32] = {};
    float ciclo_[32] = {};
    int cicloPos_ = 0;
    long long ciclos_ = 0;
    int rep_ = 1;
    int repPos_ = 0;
    float acc1_ = 0.f, acc2_ = 0.f;

    // o Viterbi (K=7)
    float met_[64];
    std::vector<uint64_t> dec_;             // as decisoes (1 bit por estado)
    long long passos_ = 0;
    static constexpr int kProf = 96;        // profundidade do traceback

    // a moldura do texto
    int    mold_ = 0;
    int    nDados_ = 0;
    int    acum_ = 0;
    bool   figuras_ = false;
    uint32_t somReg_ = 0;
    bool   inMsg_ = false;
    std::string texto_;
    std::string pendente_;
    int    bitsUsuario_ = 0;
};

} // namespace masdr
