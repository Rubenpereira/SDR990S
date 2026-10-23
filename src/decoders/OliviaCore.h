#pragma once
// ---------------------------------------------------------------------------
//  OliviaCore - decodificador OLIVIA (MFSK + FEC de Walsh) do audio do radio
//
//  O OLIVIA (Pawel Jalocha, 2003) e um modo de radio digital de HF feito para
//  sobreviver a propagacao ruim: decodifica com o sinal 10 dB abaixo do ruido.
//
//  Como funciona (especificacao do Jalocha / "The Draft Specification For The
//  Olivia HF Transmission System", conferida com a implementacao de referencia):
//
//  * Camada 1 (MFSK): N tons (2..64) separados de BW/N Hz; a taxa de simbolos
//    e igual a separacao (BW/N baud: 31,25 Bd no 8/250, 16/500 e 32/1000).
//    O primeiro tom fica em centro - BW/2 + sep/2, o conjunto e centrado no
//    "centro" (1500 Hz no padrao). O numero do tom e o simbolo em codigo Gray.
//    Cada tom dura DOIS periodos de simbolo com envelope cosseno (1-cos):
//    os simbolos vizinhos se sobrepoem pela metade. Por isso o receptor usa
//    janela de Hann de 2 simbolos (o filtro casado) e a FFT de 2 simbolos
//    (resolucao de meia separacao: os tons caem de 2 em 2 bins).
//  * Camada 2 (FEC Walsh): 64 simbolos formam um bloco de bps caracteres
//    (bps = bits por simbolo). O caractere c (7 bits) vira a funcao de Walsh
//    de indice c&63 (invertida se c >= 64); o vetor de 64 e embaralhado com a
//    sequencia 0xE257E6D0291574EC (deslocada 13 bits por caractere) e o bit
//    do instante t do caractere i vai para o bit (i+t) mod bps do simbolo t.
//    O receptor monta o vetor com decisoes MOLES (probabilidade de cada bit
//    a partir das energias dos tons), desembaralha, faz a transformada de
//    Walsh-Hadamard e o pico da o caractere.
//  * Sincronismo: o receptor nao sabe onde o bloco comeca, nem o instante do
//    simbolo, nem o desvio de frequencia. Ele decodifica TODAS as hipoteses
//    (desvio em passos de meia separacao dentro de +-125 Hz, 4 fases de
//    tempo por simbolo, 64 alinhamentos de bloco), mede a qualidade de cada
//    bloco (pico de Walsh / ruido) e integra por alguns blocos. So o melhor
//    candidato, acima do limiar, vira texto - em ruido puro nao sai lixo.
//
//  O texto decodificado sai para a tela do programa.
// ---------------------------------------------------------------------------
#include <complex>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace masdr {

class OliviaCore {
public:
    struct Estado {
        bool   ok = false;        // ha sinal OLIVIA (perto do limiar ou acima)
        int    nTons = 0;         // quantos tons diferentes aparecem
        double tomBaixo = 0;      // Hz (o tom mais baixo do conjunto achado)
        double tomAlto = 0;       // Hz
        double nivelDb = -100;    // nivel do tom mais forte (dB, 0 = escala cheia)
        bool   travado = false;   // travado em um sinal (texto decodificando)
        int    blocos = 0;        // blocos decodificados
        double qualidade = 0;     // a metrica de sincronismo (limiar ~ 4)
        double desvioHz = 0;      // desvio do conjunto de tons achado
        std::string texto;        // o texto decodificado (o ultimo pedaco) ou o estado
    };

    OliviaCore();

    // Formato: tons (4, 8, 16, 32...) e largura de banda (125, 250, 500, 1000, 2000 Hz).
    // centro: onde fica o meio do conjunto de tons no audio (1500 Hz = padrao).
    void setModo(int tons, int banda, double centro = 1500.0);
    void alimentar(const float* amostras, size_t n);   // audio em 8 kHz
    Estado estado() const;
    std::string pegarTexto();    // o texto novo (para a tela)
    void limpar();

private:
    void montar();               // tamanhos e tabelas do modo atual
    void zerar();                // limpa o estado de decodificacao
    void fatia();                // uma FFT (a cada 1/4 de simbolo)
    double decodificaBloco(int s, uint8_t* chars);   // os ultimos 64 simbolos da hipotese s
    void saida(const uint8_t* chars);

    mutable std::mutex mtx_;
    // o modo
    int tons_ = 8, banda_ = 250, spb_ = 3;
    double centro_ = 1500.0;
    int ns_ = 256;               // amostras por simbolo (8 kHz)
    int L_ = 512;                // tamanho da FFT (2 simbolos)
    int hop_ = 64;               // amostras entre FFTs (ns/4)
    double binHz_ = 15.625;      // resolucao da FFT
    double f0_ = 1390.625;       // o primeiro tom (Hz) sem desvio
    int b0_ = 89;                // o bin do primeiro tom
    int margem_ = 8;             // a busca de desvio (bins para cada lado)
    int nOff_ = 17;              // hipoteses de desvio
    int nHip_ = 68;              // hipoteses (desvio x fase de tempo)

    // a entrada
    std::vector<std::complex<float>> anel_;   // as ultimas L amostras (misturadas)
    size_t anelPos_ = 0;
    int contHop_ = 0;
    long long nAmostras_ = 0;
    std::complex<double> nco_{1.0, 0.0}, ncoInc_{1.0, 0.0};
    int ncoRenorm_ = 0;

    // a FFT
    std::vector<float> janela_;
    std::vector<std::complex<float>> fftBuf_, tw_;
    std::vector<int> rev_;
    std::vector<float> energia_;              // energia dos bins usados
    int binIni_ = 0, nBins_ = 0;

    // as hipoteses
    long long nFatias_ = 0;
    std::vector<float> moles_;     // [hip][64][spb] decisoes moles (anel)
    std::vector<uint8_t> tomDur_;  // [hip][64] o tom decidido (para contar os tons)
    std::vector<long long> nSimb_; // [hip] simbolos recebidos
    std::vector<float> integ_;     // [hip][64] a metrica integrada por alinhamento
    std::vector<float> qAnt_;      // [hip][64] a qualidade do bloco anterior (mesmo alinhamento)
    std::vector<uint8_t> charsAnt_;// [hip][64][spb] o bloco anterior
    std::vector<int> grayInv_;     // tom -> simbolo
    struct Pendente {                    // o melhor bloco do grupo que esta terminando
        bool ativo = false;
        long long t = 0;
        float integ = 0, q = 0, qAnt = 0;
        uint8_t chars[8] = {}, charsAnt[8] = {};
    } pend_;
    long long ultimaSaida_ = -1000000;   // a fatia da ultima saida de texto
    long long ultimaTrava_ = -1000000;   // a fatia em que estava travado
    float melhorInteg_ = 0;
    int melhorHip_ = 0;

    double nivel_ = 0;
    Estado est_;
    std::string pendente_;        // texto novo para a tela
    std::string recente_;         // os ultimos caracteres (para o estado)
    char ultimoChar_ = 0;
};

} // namespace masdr
