#pragma once
// ---------------------------------------------------------------------------
//  AdsbInternet - o prefixo (matricula), o modelo e o operador de cada aviao
//
//  O ADS-B so traz o endereco ICAO de 24 bits; a matricula (PR-XMA, PP-...)
//  vem de uma consulta ao hexdb.io (base aberta, sem chave):
//      https://hexdb.io/api/v1/aircraft/E49406
//  Uma consulta por vez, no maximo uma por segundo, cada endereco uma vez so.
//  As respostas ficam guardadas em adsb_prefixos.txt ao lado do exe, entao o
//  mesmo aviao nao e consultado de novo nos dias seguintes.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <thread>

namespace masdr {

class AdsbCore;

class ConsultaHexdb {
public:
    ~ConsultaHexdb() { parar(); }
    void iniciar(AdsbCore* core);
    void parar();
    std::string situacao() const;           // para a linha de estado

private:
    struct Info { std::string prefixo, tipo, operador, operadorIcao; };
    void laco();
    bool buscar(uint32_t icao, Info& info, bool& achou);
    void carregar();
    void guardar(uint32_t icao, const Info& i, bool achou);

    AdsbCore* core_ = nullptr;
    std::thread th_;
    std::atomic<bool> vivo_{false};
    std::map<uint32_t, Info> cache_;        // achados
    std::map<uint32_t, bool> semCadastro_;  // o hexdb nao conhece
    std::deque<uint32_t> fila_;
    std::string arquivo_;
    mutable std::mutex m_;
    std::string situacao_;
    int consultas_ = 0;
};

// O caminho contrario: do PREFIXO para o endereco ICAO, o modelo e o operador
// (o ACARS / VDL2 / HFDL trazem a matricula, nao o endereco). hexdb.io:
//     https://hexdb.io/reg-hex?reg=PR-GXW       -> E48FB7
//     https://hexdb.io/api/v1/aircraft/E48FB7   -> modelo, dono, operador
// Cada prefixo e consultado uma vez e fica em acars_prefixos.txt ao lado do exe;
// os avioes que o ADSB ja consultou (adsb_prefixos.txt) nem vao a internet.
class ConsultaPrefixo {
public:
    struct Info { std::string hex, tipo, operador, operadorIcao; };
    ~ConsultaPrefixo();
    void pedir(const std::string& prefixo);              // entra na fila (uma vez so)
    bool info(const std::string& prefixo, Info& i);      // ja sabe (achou no hexdb)?
private:
    struct Estado;                                       // fica vivo com a thread (fechar no meio de uma consulta)
    std::shared_ptr<Estado> e_;
    void comecar();
};

// Posicao pela INTERNET para os avioes que o radio OUVIU mas sem posicao (ACARS/VDL2/HFDL
// sem relatorio de posicao, Modo S sem ADS-B). adsb.lol - rede aberta de receptores ADS-B,
// API gratuita (sem chave por enquanto), por endereco ICAO, prefixo ou voo:
//     https://api.adsb.lol/v2/hex/a71980   /v2/reg/N557AV   /v2/callsign/AVA8582
// No maximo uma consulta a cada 2 s; cada aviao de novo a cada 60 s, enquanto for pedido.
// Recusou (4xx): espera 30 min; sem resposta: 2 min.
class ConsultaPosicao {
public:
    struct Pos {
        bool achou = false;                  // o adsb.lol tem a posicao deste aviao
        std::string hex, voo, prefixo, tipo;
        double lat = 91, lon = 181, gs = -1, trk = -1;
        int alt = -2147483647 - 1, vs = -2147483647 - 1;
        bool noChao = false;
        double quando = 0;                   // relogio (steady_clock, s) da posicao
        std::string fonte;                   // "adsb.lol" ou "ADS-B Exchange"
    };
    ~ConsultaPosicao();
    // "hex:A71980", "reg:N557AV", "cs:AVA8582" ou "pt:-7.15,-34.94,AZU248" (o aviao perto deste
    // ponto cujo voo comeca com AZU248 - para completar o voo cortado do HFDL)
    void pedir(const std::string& chave);
    bool pegar(const std::string& chave, Pos& p);
    std::string situacao();
    // chave da API do ADS-B Exchange no RapidAPI (assinatura PAGA do proprio usuario);
    // vazia = so o adsb.lol. Com chave: primeiro o ADS-B Exchange, depois o adsb.lol.
    void chaveAdsbx(const std::string& chave);
private:
    struct Estado;
    std::shared_ptr<Estado> e_;
    void comecar();
};

} // namespace masdr
