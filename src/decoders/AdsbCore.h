#pragma once
// ---------------------------------------------------------------------------
//  AdsbCore - decodificador ADS-B / Modo S (1090 MHz) a partir do IQ cru
//
//  O que chega: o IQ do dongle (2,0 Msps de preferencia; qualquer taxa a
//  partir de 1,8 Msps serve - o demodulador trabalha com tempo fracionario). O que sai: a lista de avioes
//  (endereco ICAO, voo, altitude, velocidade, rumo, squawk, posicao) e umas
//  linhas de texto para a janela.
//
//  Como funciona (ICAO Anexo 10 Vol. IV, DO-260; "The 1090 MHz Riddle"):
//  * Modulacao PPM a 1 Mbit/s: cada bit e um pulso de 0,5 us na primeira
//    metade (bit 1) ou na segunda (bit 0).
//  * Preambulo de 8 us: pulsos em 0; 1,0; 3,5 e 4,5 us. Os dados comecam em
//    8 us: 56 ou 112 bits. O inicio e achado com fracao de amostra (o melhor
//    contraste pulso/vazio) e cada meio-chip e a integral das amostras nele;
//    se o CRC falha, troca os bits menos firmes (so para aviao ja conhecido).
//  * Paridade CRC-24 (gerador 0xFFF409) nos ultimos 24 bits. No DF11/17/18
//    ela da zero; no DF0/4/5/16/20/21 ela vem somada ao endereco do aviao
//    (endereco/paridade): o endereco so vale se o aviao ja foi visto num
//    DF11/17 - assim o ruido nao inventa aviao. DF17 com 1 bit errado e
//    corrigido pela sindrome (so de aviao ja conhecido).
//  * DF17/18 (ADS-B): TC 1-4 identificacao (o voo), TC 9-18 e 20-22 posicao
//    no ar (CPR par/impar: decodificacao global com os dois quadros em ate
//    10 s, depois local), TC 19 velocidade, TC 28 squawk de emergencia,
//    TC 5-8 no chao. DF4/20 altitude, DF5/21 squawk.
//  * O prefixo (matricula) nao vem no ADS-B: e calculado do endereco para os
//    EUA (N-number) e, nos outros paises, consultado na internet pela camada
//    de cima (definirInfo).
// ---------------------------------------------------------------------------
#include <climits>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace masdr {

class AdsbCore {
public:
    static constexpr int kSemValor = INT_MIN;

    struct Aviao {
        uint32_t icao = 0;
        std::string hex;            // "E49406"
        std::string voo;            // indicativo (callsign), sem espacos
        std::string prefixo;        // matricula (calculada ou da internet)
        std::string tipo;           // modelo (codigo ICAO, da internet)
        std::string operador;       // dono / operador (da internet)
        std::string operadorIcao;   // codigo ICAO do operador no cadastro (GLO, TAM...)
        int categoria = 0;          // categoria do ADS-B: tc*8 + ca (0 = nao veio)
        std::string empresa;        // companhia / Militar / Particular... (AdsbEmpresas)
        std::string classe;         // comercial, carga, executiva, militar, governo, taxi, particular
        std::string pais;           // pelo bloco do endereco ICAO
        int altitude = kSemValor;   // pes
        double velocidade = -1;     // nos
        double rumo = -1;           // graus
        int vertical = kSemValor;   // pes por minuto
        int squawk = -1;
        double lat = 91, lon = 181; // graus (91/181 = sem posicao)
        bool noChao = false;
        int msgs = 0;
        double idade = 0;           // segundos desde a ultima mensagem
        bool ativo = true;          // ouvido no ultimo minuto (os outros ficam na lista como registro)
    };

    struct Estado {
        uint32_t taxa = 0;          // amostras/s que estao chegando
        bool taxaOk = false;        // >= 1,8 Msps
        long long mensagens = 0;    // mensagens aceitas
        long long corrigidas = 0;   // com 1 bit corrigido
        int avioes = 0;             // avioes na lista
        int comPosicao = 0;
        double msgsPorS = 0;
        double nivelDb = -100;      // nivel medio dos pulsos (dBFS)
    };

    AdsbCore();

    void limpar();
    void limparLista();                         // so a tabela (botao Limpar)
    void alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps);
    std::vector<Aviao> avioes() const;          // os mais recentes primeiro
    Estado estado() const;
    std::string pegarTexto();                   // linhas novas para a janela

    // A camada de cima consulta o prefixo/modelo na internet e devolve aqui.
    void definirInfo(uint32_t icao, const std::string& prefixo, const std::string& tipo,
                     const std::string& operador, const std::string& operadorIcao = std::string());
    // Os enderecos novos ainda nao consultados (cada um aparece uma vez so).
    std::vector<uint32_t> pedirConsultas();

    // Uma mensagem ja em bytes (56 ou 112 bits). Usado pelo demodulador e
    // pelos testes. Devolve true se a mensagem foi aceita.
    bool mensagem(uint8_t* msg, int nbits, bool podeCorrigir, bool jaCorrigida = false);
    void avancarRelogio(double s) { relogio_ += s; }   // (testes)

    static uint32_t sindrome(const uint8_t* msg, int nbits);
    static std::string prefixoCalculado(uint32_t icao);   // EUA: N-number
    static const char* paisDoIcao(uint32_t icao);
    static int altitudeAc13(int ac);                      // codigo de altitude (pes) ou kSemValor
    static int squawkId13(int id);

private:
    struct Interno {
        Aviao a;
        double visto = 0, criado = 0;
        bool anunciado = false, consultado = false;
        // CPR
        int cprLat[2] = {0, 0}, cprLon[2] = {0, 0};
        double cprT[2] = {-1e9, -1e9};
        double posT = -1e9;
        double candLat = 91, candLon = 181, candT = -1e9;   // posicao a confirmar
        int saltos = 0;
    };
    Interno* pegar(uint32_t icao, bool criar);
    bool conhecido(uint32_t icao) const;
    void decodificarEs(Interno& v, const uint8_t* me);
    void posicaoAr(Interno& v, int impar, int latCpr, int lonCpr);
    void alertaSquawk(Interno& v, int sq);
    void linha(const std::string& s);
    void demodular();
    std::string horaUtc() const;

    mutable std::mutex mtx_;
    std::map<uint32_t, Interno> avioes_;
    std::set<uint32_t> fila_;                       // a consultar
    std::unordered_map<uint32_t, int> umBit112_;    // sindrome -> bit
    std::string texto_;
    double relogio_ = 0;                            // segundos de amostras recebidas
    Estado est_;
    double contMsgs_ = 0, janelaMsgs_ = 0;

    // a entrada
    std::vector<float> mag_;                        // magnitudes (com a sobra do bloco anterior)
    std::vector<double> soma_;                      // soma acumulada (integrais rapidas)
    std::complex<float> dc_{0, 0};
    uint32_t sps_ = 0;
    double ruido_ = 0, sinal_ = 0;
};

} // namespace masdr
