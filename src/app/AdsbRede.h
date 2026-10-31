#pragma once
// ============================================================================
//  AdsbRede - ADS-B de OUTRO receptor da rede (FlightAware/PiAware, dump1090,
//  readsb, FR24...): le o formato Beast (porta 30005, a mesma que os programas
//  que alimentam os sites usam) e entrega as mensagens a um AdsbCore - o mesmo
//  decodificador do dongle (CRC, CPR, prefixo, empresa, squawk). So le: nao
//  manda nada para o aparelho. O RADAR AEREO mostra estes avioes junto com os
//  do dongle, que fica livre para outro decodificador.
// ============================================================================
#include "../decoders/AdsbCore.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace masdr {

class AdsbRede {
public:
    ~AdsbRede() { parar(); }
    // Liga/desliga e troca o endereco. Pode ser chamado a cada quadro: so
    // reconecta quando algo muda.
    void manter(bool ligar, const std::string& host, int porta);
    AdsbCore& core() { return core_; }
    bool ligado() const { return vivo_.load(); }
    bool conectado() const { return conectado_.load(); }
    std::string situacao() const;
    std::string alvo() const;                  // "192.168.0.10:30005"

    // Beast: 0x1a + tipo ('1' Modo A/C, '2' Modo S curto, '3' Modo S longo) +
    // 6 bytes de relogio + 1 de sinal + a mensagem; 0x1a dentro vira 0x1a 0x1a.
    // Tira de 'buf' os quadros completos e chama 'msg' com cada Modo S.
    template <class F> static void lerBeast(std::vector<uint8_t>& buf, F&& msg);

private:
    void parar();
    void laco();
    void sit(const std::string& s);

    AdsbCore core_;
    std::thread th_;
    std::atomic<bool> vivo_{false}, conectado_{false};
    std::atomic<uintptr_t> sock_{~(uintptr_t)0};
    mutable std::mutex m_;
    std::string host_, sit_;
    int porta_ = 0;
};

template <class F> void AdsbRede::lerBeast(std::vector<uint8_t>& buf, F&& msg)
{
    size_t p = 0;
    uint8_t q[7 + 14];
    while (p < buf.size()) {
        if (buf[p] != 0x1a) { ++p; continue; }
        if (p + 1 >= buf.size()) break;                     // falta o tipo
        const uint8_t tipo = buf[p + 1];
        const int len = tipo == '1' ? 2 : tipo == '2' ? 7 : tipo == '3' ? 14 : 0;
        if (!len) { ++p; continue; }                         // nao e inicio de quadro
        const size_t total = 7 + (size_t)len;                // relogio + sinal + mensagem
        size_t j = p + 2, n = 0;
        bool incompleto = false, ruim = false;
        while (n < total) {
            if (j >= buf.size()) { incompleto = true; break; }
            const uint8_t b = buf[j];
            if (b == 0x1a) {
                if (j + 1 >= buf.size()) { incompleto = true; break; }
                if (buf[j + 1] != 0x1a) { ruim = true; break; }   // outro quadro comecou: este veio cortado
                q[n++] = 0x1a; j += 2;
            } else { q[n++] = b; ++j; }
        }
        if (incompleto) break;
        if (ruim) { p = j; continue; }
        if (tipo == '2' || tipo == '3') msg(q + 7, len * 8);
        p = j;
    }
    buf.erase(buf.begin(), buf.begin() + (std::ptrdiff_t)p);
}

} // namespace masdr
