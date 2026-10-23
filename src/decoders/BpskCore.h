#pragma once
// ---------------------------------------------------------------------------
//  BpskCore - decodificador BPSK/QPSK do audio do radio
//
//  Os modos PSK de radioamador (BPSK31/63/125/250, QPSK31/63/125 e os com
//  FEC "PSK-R"): portadora unica em 1800 Hz no audio (USB), modulada em
//  fase com envelope de cosseno levantado (o "buraco" do PSK31).
//
//  Camadas, na ordem do sinal:
//
//  1. Mezcla com NCO proprio para baseband + AFC de fase (Costas): o erro
//     de fase entre a decisao e o simbolo mais proximo corrige a frequencia
//     da portadora aos poucos.
//  2. Filtro casado (matched filter) e decimacao para 16 amostras do
//     envelope por simbolo.
//  3. Relogio de simbolo: o desenho do envelope num buffer de 16 posicoes;
//     a diferenca entre as duas metades arrasta o instante de decisao para
//     o ponto de mudanca de fase (onde o envelope e maximo).
//  4. Decisao diferencial: a fase ENTRE dois simbolos vizinhos e o dado
//     (BPSK: passo 0 grau = bit 1, passo de 180 = bit 0; QPSK: passos de
//     90 graus).
//  5. FEC (so nos modos com FEC): Viterbi K=5 (QPSK) ou K=7 + entrelacador
//     quadrado 2x2x40 (BPSK125F); dois viterbis atrasados de um bit um do
//     outro, com votacao no nivel de caractere.
//  6. Varicode: codigos variaveis de 1 a 12 bits por caractere (o varicode
//     do PSK31, G3PLX, nos modos sem FEC; o do MFSK16, ZL1BPU/IZ8BLY, nos
//     modos com FEC).
//
//  Convencoes da norma (conferidas nas publicacoes das especs):
//  * BPSK: bit 1 = sem mudanca de fase, bit 0 = passo de 180 graus;
//  * FEC: Viterbi apenas - nao ha Reed-Solomon no fluxo de dados (o RSID e
//    apenas um bipe curto de identificacao de modo fora do texto);
//  * BPSK125F acompanha o PSK125R da familia "R": 2x2x40;
//  * BPSK220F e BPSK10 ficam de fora: sem especificacao publica
//    interoperavel conhecida (ver CHANGELOG).
// ---------------------------------------------------------------------------

#include <complex>
#include <cstdint>
#include <mutex>
#include <string>

namespace masdr {

// ---------------------------------------------------------------------------
//  Tabela varicode: primeiro bit transmitido = bit mais significativo
// ---------------------------------------------------------------------------
struct TabelaVaricode {
    uint16_t codigo[256];      // bits do codigo como inteiro (ate 12 bits)
    int achar(unsigned v) const {          // -1 se nao existe
        for (int i = 0; i < 256; ++i) if ((unsigned)codigo[i] == v) return i;
        return -1;
    }
};

// tabelas geradas das normas publicadas (ver BpskCore.cpp)
extern const TabelaVaricode kVaricodePsk;    //  PSK31: G3PLX
extern const TabelaVaricode kVaricodeMfsk;   //  MFSK16: ZL1BPU/IZ8BLY

// ---------------------------------------------------------------------------
//  Codificador convolucional (para o gerador de teste fora do app):
//  memoria de K bits; saida = paridade(poly1) no bit 0 + paridade(poly2) no bit 1
// ---------------------------------------------------------------------------
class EncoderC {
public:
    EncoderC(int k, int poly1, int poly2)
        : k_(k), p1_(poly1), p2_(poly2) {
        mask_ = (1 << k) - 1;
        reset();
    }
    void reset() { shreg_ = 0; }
    int encode(int bit) {                       // 0..3: bit0 = poly1, bit1 = poly2
        shreg_ = (shreg_ << 1) | (bit ? 1 : 0);
        const int s = (int)(shreg_ & (unsigned)mask_);
        return paridade(p1_ & s) | (paridade(p2_ & s) << 1);
    }
    static int paridade(int x) { int p = 0; while (x) { p ^= (x & 1); x >>= 1; } return p; }
private:
    int k_, p1_, p2_, mask_;
    unsigned shreg_;
};

// ---------------------------------------------------------------------------
//  Decodificador Viterbi (K=5 ou K=7, aleatoriedade na ordem de bits publicada)
// ---------------------------------------------------------------------------
class ViterbiC {
public:
    ViterbiC(int k = 5, int poly1 = 0x17, int poly2 = 0x19);
    ~ViterbiC();
    void reset();
    // troca o codigo do viterbi (K e polinomios); realoca quando preciso
    void configura(int k, int poly1, int poly2);
    void setChunk(int c) { chunk_ = c; }
    // sym[0] = bit mole do poly1 (primeiro enviado), sym[1] = do poly2;
    // devolve o lote de chunk_ bits num byte (MSB = bit mais antigo) ou -1
    int decode(const unsigned char* sym, int* met);
private:
    ViterbiC(const ViterbiC&) = delete;
    ViterbiC& operator=(const ViterbiC&) = delete;
    void montaTabelas();

    static constexpr int kCamadas = 256;
    int k_ = 0, p1_ = 0, p2_ = 0;
    int chunk_ = 8;
    int saida_[1 << 8];
    int* met_[kCamadas];
    int* hist_[kCamadas];
    int ptr_ = 0;
    short metTab_[2][256];
};

// ---------------------------------------------------------------------------
//  Entrelacador quadrado deslizante (2x2, profundidade; so para o modo F125)
// ---------------------------------------------------------------------------
class EntrelacadorC {
public:
    enum { FWD = 0, REV = 1 };
    EntrelacadorC(int size, int depth, int dir)
        : size_(size), depth_(depth), dir_(dir),
          len_(size * size * depth), tabela_(new unsigned char[size * size * depth]) { flush(); }
    ~EntrelacadorC() { delete[] tabela_; }
    void flush() {   // RX enche de 128 (bit mole neutro)
        for (int i = 0; i < len_; ++i) tabela_[i] = (dir_ == REV) ? 128 : 0;
    }
    void symbols(unsigned char* syms) {
        for (int k = 0; k < depth_; ++k) {
            for (int i = 0; i < size_; ++i)
                for (int j = 0; j < size_ - 1; ++j)
                    tab(k, i, j) = tab(k, i, j + 1);
            for (int i = 0; i < size_; ++i)
                tab(k, i, size_ - 1) = syms[i];
            for (int i = 0; i < size_; ++i)
                syms[i] = (dir_ == FWD) ? tab(k, i, size_ - i - 1) : tab(k, i, i);
        }
    }
private:
    unsigned char& tab(int k, int i, int j) { return tabela_[size_ * size_ * k + size_ * i + j]; }
    int size_, depth_, dir_, len_;
    unsigned char* tabela_;
};

// ---------------------------------------------------------------------------
//  Nucleo BPSK/QPSK
// ---------------------------------------------------------------------------
class BpskCore {
public:
    struct Estado {
        bool   ok = false;        // portadora com dados detectada
        double desvioHz = 0;      // correcao AFC aplicada ate agora
        double nivelDb = 0;       // nivel medido do sinal
        double metrica = 0;       // qualidade da constelacao 0..100
        long   simbolos = 0;
        long   caracteres = 0;
    };

    BpskCore();

    void setModo(int indice);
    void alimentar(const float* x, size_t n);   // 8 kHz real, -1..1
    Estado estado() const;
    std::string pegarTexto();
    void limpar();

    static int nModos();
    static const char* nomeModo(int i);

private:
    struct ModoP {
        int L;               // amostras por simbolo @ 8 kHz
        int dcdbits;         // paciencia do AFC (varreduras)
        int filtro;          // 0 = pskcore (cosseno levantado), 1 = sinc
        bool qpsk;           // constelacao de 4 fases
        bool fec;            // Viterbi K=7 (modos com FEC)
        int k, poly1, poly2;
        bool inter;          // entrelacador 2x2x40 (so BPSK125F)
        bool varMfsk;        // varicode do MFSK16 (modos com FEC)
    };

    static const ModoP kModos[9];

    void amostraSimbolo(const std::complex<double>& z);
    void ajustaAfc(const std::complex<double>& z2);
    void rxBit(int bit);
    void rxBit2(int bit);
    void rxPskr(unsigned char soft);
    void rxQpsk(int bits);
    void dcdPasso(int bits);
    void chegouCaractere(int c);
    void chegouCaractere2(int c);
    void reiniciarRx();
    double decMedia(double media, double val, int peso) const {
        return (val - media) / peso + media;
    }

    mutable std::mutex mtx_;
    int modo_ = 0;
    const ModoP* m_ = &kModos[0];

    double fs_ = 8000.0;
    double centro_ = 1800.0;
    double freq_ = 1800.0;           // NCO corrente
    double faseNco_ = 0;

    // filtros
    int M_ = 16;
    double f1_[65];                  // fir1 (decimado por M_)
    double f2_[65];                  // fir2 (nao decimado)
    int nf1_ = 65, nf2_ = 65;
    std::complex<double> buf1_[65 * 16]; unsigned b1i_ = 0; int b1f_ = 0;
    std::complex<double> buf2_[65]; unsigned b2i_ = 0;

    // relogio de simbolo
    double syncbuf_[16] = {};
    double bitclk_ = 0;

    // decisoes / AFC / DCD
    std::complex<double> simboloAnt_ = { 1.0, 0.0 };
    double nivelMedio_ = 0;
    double qRe_ = 0, qIm_ = 0;
    double metrica_ = 0, afcMetrica_ = 0;
    bool dcd_ = false;
    unsigned dcdshreg_ = 0;
    int dcdOff_ = 0;

    // varicode
    unsigned shreg_ = 0, shreg2_ = 0;        // PSK (fluxo principal / atrasado)
    unsigned shregM_ = 0, shreg2M_ = 0;      // MFSK idem
    const TabelaVaricode* tab0_ = &kVaricodePsk;

    // FEC
    ViterbiC vit0_, vit1_;
    bool vitPasso_ = false;
    unsigned char parAnt_ = 128;          // simbolo anterior (pares de bits)
    EntrelacadorC* ent0_ = nullptr;
    EntrelacadorC* ent1_ = nullptr;
    double fecMet0_ = 0, fecMet1_ = 0;

    long simbolos_ = 0, caracteres_ = 0;
    std::string pendente_;
};

} // namespace masdr
