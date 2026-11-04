#pragma once
// ============================================================================
//  SelcallCore - SELCAL (Selective Calling) da aviacao, ICAO Anexo 10 Vol. III
//
//  A estacao de terra (HF em USB, ou VHF em AM) chama UM aviao: dois pulsos de
//  1,0 s (+-0,25), separados por 0,2 s (+-0,1), cada pulso com DOIS tons
//  juntos. O codigo tem 4 letras, escrito "AB-CD" (A+B no 1o pulso, C+D no 2o).
//  O aviao com aquele codigo toca a campainha na cabine.
//
//  Tons: os 16 classicos (A..S sem I, N, O: 312,6 .. 1479,1 Hz) e os 16 do
//  SELCAL 32 (T..Z e 1..9, a meio caminho entre os classicos), da Emenda 91
//  ao Anexo 10 (Tabela 3-1). Os tons sobem ~10,9% a cada letra classica.
//
//  Cadeia: audio 8 kHz -> FFT de 2048 (256 ms) a cada 32 ms -> os picos da
//  faixa 270-1650 Hz acima do ruido -> cada TOM e seguido sozinho (trilha),
//  aguentando sumicos de ate 0,35 s: no HF os dois tons desvanecem cada um
//  na sua hora (desvanecimento seletivo, visto na gravacao real de 8906 kHz,
//  03/11/2026). Trilha de 0,4-1,8 s = um tom; dois tons que comecam e acabam
//  juntos (e nenhum terceiro junto - voz tem varios) = um pulso; dois pulsos
//  seguidos = um codigo. O desvio de sintonia do SSB (ate +-60 Hz) e achado
//  no codigo inteiro: os 4 tons tem de cair na tabela com o MESMO desvio.
// ============================================================================
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace masdr {

class SelcallCore {
public:
    struct Chamada {
        std::string codigo;     // "AB-CD"
        std::string hora;       // hh:mm:ss UTC
        double desvioHz = 0;    // desvio de sintonia medido
        double nivelDb = 0;     // tons acima do ruido
        bool selcal32 = false;  // usa algum tom novo (T..Z, 1..9)
        int vezes = 1;          // quantas vezes este codigo chegou
    };
    struct Estado {
        bool tons = false;      // um par de tons esta no ar agora
        double f1 = 0, f2 = 0;  // o par (Hz)
        int chamadas = 0;
        std::string ultimo;     // ultimo codigo
        std::string ultimaHora;
    };

    explicit SelcallCore(double taxa = 8000.0);
    void limpar();
    void alimentar(const float* x, size_t n);
    std::string pegarTexto();                       // linhas novas para a caixa de texto
    Estado estado() const { return est_; }
    const std::vector<Chamada>& chamadas() const { return lista_; }   // mais recente primeiro

    // ---- usado pelo teste ----
    static const char* letras();                     // "ABCDEFGHJKLMPQRSTUVWXYZ123456789"
    static double freqTom(char letra);               // Hz (0 = nao existe)
    // os 4 tons medidos -> codigo (vazio se nao fecha com a tabela)
    static std::string decodificar(const double f[4], double& desvio, bool& sel32);

private:
    struct Trilha {            // um tom sendo seguido quadro a quadro
        double fSoma = 0, pSoma = 0;   // media da frequencia ponderada pelo snr (log)
        double t0 = 0, t1 = 0;         // primeiro e ultimo quadro com o tom
        double snrMax = 0;
        int hits = 0;
        bool usadoAgora = false;
        std::vector<std::pair<float, float>> marcas;   // (instante, snr) de cada quadro com o tom
        double f() const { return pSoma > 0 ? fSoma / pSoma : 0; }
    };
    struct Tom { double f, t0, t1, snr; };           // trilha que acabou com cara de tom de SELCAL (snr = o maior)
    struct Pulso { double f1, f2, t0, t1, nivel; };
    void quadro();
    void fecharTrilha(const Trilha& tr, double agora);
    void novoPulso(const Pulso& p);
    std::string horaUtc() const;

    double taxa_;
    std::vector<float> buf_;                         // audio ainda nao usado (o inicio do proximo quadro em ini_)
    long long base_ = 0;                             // numero da amostra buf_[0] desde o inicio
    size_t ini_ = 0;
    std::vector<float> janela_;
    std::vector<double> re_, im_;

    std::vector<Trilha> trilhas_;                    // tons no ar (ou que sumiram ha pouco)
    std::vector<Tom> tons_;                          // tons que acabaram e ainda nao tem par
    std::vector<Tom> recentes_;                      // todos os tons dos ultimos segundos (com par ou nao): voz tem varios
    bool temAnterior_ = false;
    Pulso anterior_{};

    std::vector<Chamada> lista_;
    std::string texto_;
    Estado est_;
};

} // namespace masdr
