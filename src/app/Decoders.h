#pragma once
// ---------------------------------------------------------------------------
//  Decoders - os decodificadores digitais do RXSDR Nativo
//
//  Usa os mesmos nucleos em C++ do RXSDR principal (pasta decoders: CwCore,
//  RttyCore, SitorBCore, DscCore, AleCore, AnaliseCore). Nenhum depende de
//  programa externo: tudo roda dentro do RXSDR.exe.
//
//  O audio do demodulador (48 kHz, antes do volume e do NR) chega pela
//  thread do dongle em empurrar(); ele so e copiado para uma fila. Uma
//  thread propria reduz para 8 kHz (filtro passa-baixas + decimacao) e
//  alimenta o nucleo escolhido - assim um decodificador pesado nunca
//  engasga o som. A tela busca o texto novo com pegarTexto().
// ---------------------------------------------------------------------------
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Dsd.h"
#include "Externos.h"
#include "Tetra.h"
#include "../decoders/Ft8Core.h"
#include "../decoders/SstvCore.h"
#include "../decoders/WefaxCore.h"
#include "../decoders/OliviaCore.h"
#include "../decoders/StanagCore.h"
#include "../decoders/BpskCore.h"
#include "../decoders/AdsbCore.h"
#include "../decoders/AdsbEmpresas.h"
#include "AdsbInternet.h"

namespace masdr {

class CwCore;
class RttyCore;
class SitorBCore;
class DscCore;
class AleCore;
class AnaliseCore;
class PactorCore;

class Decoders {
public:
    enum Tipo { NENHUM = 0, CW, RTTY, SITORB, DSC, ALE, DMR, TETRA, HFDL, AIS, APRS, ACARS, VDL2, ANALISE, DRM, SSTV, WEFAX, PACTOR, FT8, FT4, SATELITES, OLIVIA, STANAG, BPSK, ADSB, N_TIPOS };

    struct Ajustes {
        float rttyBaud = 45.45f, rttyShift = 170.f;
        bool  rttyInverter = false;
        float sitorShift = 170.f;
        bool  sitorInverter = false;
        bool  dscInverter = false;
        float cwTom = 0.f;            // 0 = medir sozinho
        int   dmrModo = 1;            // indice em Dsd::nomeModo (1 = DMR)
        bool  dmrInverter = false;
        int   dmrTaxaVoz = 16150;
        bool  tetraInverter = false;  // espectro invertido (IQ conjugado)
        std::vector<double> hfdlCanais;   // kHz (a banda escolhida)
        double   hfdlCentroHz = 0;        // centro REAL do dongle ao iniciar
        uint32_t hfdlTaxa = 0;
        int   aprsBaud = 1200;            // 1200 VHF (FM) ou 300 HF (USB)
        std::vector<double> acarsCanais;  // Hz
        double   acarsCentroHz = 0;
        uint32_t acarsTaxa = 0;
        std::vector<double> vdl2Canais;   // Hz
        double   vdl2CentroHz = 0;
        uint32_t vdl2Taxa = 0;
        bool  drmInverter = false;        // espectro invertido (I/Q trocados)
        int   stanagVel = 600;            // 75, 150, 300, 600 ou 1200 bps (Anexo A)
        bool  stanagLongo = true;         // o entrelacador longo (j=12/24) ou curto
        int   bpskModo = 0;               // indice em BpskCore::nomeModo (BPSK31...)
        bool  adsbInternet = false;       // ADSB: buscar prefixo/modelo no hexdb.io (desligado: so com o usuario ligando)
    };

    Decoders();
    ~Decoders();

    void iniciar(Tipo t, const Ajustes& a);   // troca/reinicia o decodificador
    void parar();
    Tipo tipo() const { return tipo_.load(); }

    // Chamado pela thread do dongle com o audio demodulado
    void empurrar(const int16_t* pcm, size_t n, uint32_t sps);

    std::string pegarTexto();     // texto novo desde a ultima chamada
    std::string estado();         // linha curta de situacao
    bool travado();               // sincronizado agora (para o LED)
    float progressoAnalise();     // 0..1 (analise de sinal)
    // FT8/FT4: as mensagens novas (a tela monta a tabela) e onde esta o periodo
    std::vector<Ft8Core::Mensagem> pegarFt8();
    float progressoFt8();         // 0..1 dentro do periodo de 15 s (FT4: 7,5 s)

    static const char* nome(Tipo t);

    // DMR: a voz decodificada toma o lugar do audio do radio
    // (DMR e TETRA)
    bool substituiAudio() const {
        const Tipo t = tipo_.load();
        return (t == DMR && dsd_.rodando()) || (t == TETRA && tetra_.rodando()) || (t == DRM && drm_.rodando() && drm_.audioBom());   // DRM: so com audio bom; antes, o som do radio
    }
    // ADSB, ACARS e VDL2 decodificam o IQ: o som do radio (estalos e chiado
    // do AM) nao serve para nada, entao fica mudo enquanto um deles roda
    bool silenciaAudio() const {
        const Tipo t = tipo_.load();
        return t == ADSB || t == ACARS || t == VDL2;
    }
    void puxarVoz(int16_t* out, size_t n, uint32_t sps) {
        if (tipo_.load() == TETRA) { tetra_.puxarVoz(out, n, sps); return; }
        if (tipo_.load() == DRM) { drm_.puxarAudio(out, n, sps); return; }
        dsd_.setTaxaSaida(sps); dsd_.puxarVoz(out, n);
    }
    // TETRA trabalha com o IQ (o demodulador de FM nao serve para pi/4-DQPSK)
    void empurrarIQ(const std::complex<float>* iq, size_t n, uint32_t sps) {
        const Tipo t = tipo_.load();
        if (t == TETRA) tetra_.alimentarIQ(iq, n, sps);
        else if (t == AIS) ais_.alimentarIQ(iq, n, sps);
        else if (t == DRM) drm_.alimentarIQ(iq, n, sps);
    }
    // HFDL: o IQ CRU, centrado no centro do dongle (a banda inteira de uma vez)
    void empurrarIQCru(const std::complex<float>* iq, size_t n, uint32_t sps, uint64_t centro) {
        const Tipo t = tipo_.load();
        if (t == HFDL) hfdl_.alimentarIQCru(iq, n, sps, centro);
        else if (t == ACARS) acars_.alimentarIQCru(iq, n, sps, centro);
        else if (t == VDL2) vdl2_.alimentarIQCru(iq, n, sps, centro);
        else if (t == ADSB) {
            // ADS-B: o IQ inteiro (1090 MHz no centro, 2,4 Msps)
            adsb_.alimentarIQ(iq, n, sps);
            const std::string s = adsb_.pegarTexto();
            if (!s.empty()) escrever(s);
        }
    }
    Hfdl& hfdl() { return hfdl_; }
    Ais& ais() { return ais_; }
    Aprs& aprs() { return aprs_; }
    Acars& acars() { return acars_; }
    Vdl2& vdl2() { return vdl2_; }
    AdsbCore& adsb() { return adsb_; }
    std::string adsbInternet() const { return hexdb_.situacao(); }
    Drm& drm() { return drm_; }
    SstvCore& sstv() { return sstv_; }
    WefaxCore& wefax() { return wefax_; }
    OliviaCore& olivia() { return olivia_; }
    StanagCore& stanag() { return stanag_; }
    BpskCore& bpsk() { return bpsk_; }
    // WEFAX: igual ao SSTV, o PNG e salvo aqui (pasta WEFAX); a tela pega para o historico
    struct ImagemWefax { std::vector<uint8_t> cinza; int w = 0, h = 0, lpm = 120, ioc = 576; std::string arquivo, rotulo; };
    bool pegarImagemWefax(ImagemWefax& im);
    std::atomic<bool> wefaxSalvar{true};
    // SSTV: a imagem terminada e salva AQUI (thread dos decodificadores), nao
    // na tela - com a tela bloqueada o RXSDR nao desenha, mas continua
    // recebendo e salvando a noite toda. A tela so pega para o historico.
    struct ImagemSstv { std::vector<uint32_t> argb; int w = 0, h = 0, modo = 0; bool porVis = false; std::string arquivo, rotulo; };
    bool pegarImagemSstv(ImagemSstv& im);
    std::atomic<bool> sstvSalvar{true};
    std::atomic<uint64_t> sstvVfoHz{0};      // so para o nome/rotulo
    Dsd& dsd() { return dsd_; }
    Tetra& tetra() { return tetra_; }

private:
    void laco();
    void alimentar(const float* x, size_t n);   // ja em 8 kHz, com mutex do nucleo
    void escrever(const std::string& s);
    void colherSstv();
    void colherWefax();
    std::deque<ImagemWefax> wefaxProntas_;
    std::mutex sstvMutex_;
    std::deque<ImagemSstv> sstvProntas_;

    std::atomic<Tipo> tipo_{NENHUM};
    Ajustes aj_;

    // fila de audio (thread do dongle -> thread dos decodificadores)
    std::mutex filaMutex_;
    std::condition_variable filaCv_;
    std::deque<std::vector<int16_t>> fila_;
    uint32_t filaSps_ = 48000;
    size_t filaAmostras_ = 0;
    bool sair_ = false;
    std::thread th_;

    // reducao para 8 kHz
    std::vector<float> fir_, hist_;
    size_t histPos_ = 0;
    uint32_t spsAtual_ = 0;
    int fator_ = 6;
    size_t fase_ = 0;
    double posLin_ = 0; float ultLin_ = 0; bool temUlt_ = false;
    std::vector<float> bloco8k_;
    double dmrPos_ = 0; float dmrUlt_ = 0, dmrDc_ = 0; bool dmrTemUlt_ = false;   // reamostragem para o dsd-fme

    // nucleos (so o escolhido existe)
    std::mutex coreMutex_;
    Dsd dsd_;
    Tetra tetra_;
    Hfdl hfdl_;
    Ais ais_;
    Aprs aprs_;
    Acars acars_;
    Vdl2 vdl2_;
    Drm drm_;
    SstvCore sstv_;      // SSTV: imagens (o audio vai direto, na taxa do radio)
    WefaxCore wefax_;    // WEFAX: fax meteorologico (idem)
    OliviaCore olivia_;  // OLIVIA: decodifica o texto (MFSK + FEC de Walsh)
    StanagCore stanag_;  // STANAG 4285: 75..1200 bps, portadora unica em 1800 Hz
    BpskCore bpsk_;      // PSK31/63/125/250, QPSK e os FEC "R" (BPSK63F/125F)
    AdsbCore adsb_;      // ADS-B / Modo S (1090 MHz), direto do IQ
    ConsultaHexdb hexdb_; // ADSB: prefixo e modelo de cada aviao (hexdb.io)
    std::unique_ptr<CwCore> cw_;
    std::unique_ptr<RttyCore> rtty_;
    std::unique_ptr<SitorBCore> sitor_;
    std::unique_ptr<DscCore> dsc_;
    std::unique_ptr<AleCore> ale_;
    std::unique_ptr<AnaliseCore> analise_;
    std::unique_ptr<PactorCore> pactor_;
    std::unique_ptr<Ft8Core> ft8_;      // FT8 e FT4
    std::vector<Ft8Core::Mensagem> ft8Novas_;   // (textoMutex_)
    double pactorUltimo_ = -1e9;     // ultimo texto (para o cabecalho com a hora)
    double pactorRelogio_ = 0;      // segundos de audio ja passados
    bool analiseFeita_ = false;

    // texto de saida
    std::mutex textoMutex_;
    std::string texto_;
    char ultimoChar_ = '\n';      // ultimo caractere escrito (para o aviso comecar em linha nova)
};

} // namespace masdr
