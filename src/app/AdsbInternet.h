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

} // namespace masdr
