#pragma once
// ---------------------------------------------------------------------------
//  Ui - a tela do RXSDR Nativo
//
//  Desenhada com Dear ImGui sobre Direct3D 9 (existe em todo Windows desde o
//  XP, inclusive no 7 do amigo e no 11). A aparencia copia a do RXSDR de
//  navegador: mesmas cores, S-meter de ponteiro, olho magico, frequencia em
//  digitos, espectro dourado e a cachoeira com a paleta Eclipse do
//  OpenWebRX+.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <d3d9.h>

#include "../app/Radio.h"
#include "../sat/Rastreador.h"

struct ImFont;
struct ImDrawList;
struct ImVec2;

namespace masdr {

struct Fontes {
    ImFont* normal = nullptr;
    ImFont* negrito = nullptr;
    ImFont* pequena = nullptr;
    ImFont* pequenaNeg = nullptr;      // rotulos dos gadgets (negrito)
    ImFont* digitos = nullptr;
    ImFont* digitosPeq = nullptr;
    ImFont* mono = nullptr;            // texto dos decodificadores
    ImFont* freqMouse = nullptr;       // frequencia ao lado do mouse (espectro/cachoeira)
    ImFont* painel = nullptr;          // SDR-990S: rotulos do painel (grande, desenhado em escala)
    ImFont* lcd = nullptr;             // SDR-990S: digitos dos mostradores
    ImFont* painelT[10] = {};          // a mesma fonte em varios tamanhos (texto nitido em qualquer escala)
    ImFont* lcdT[8] = {};
    ImFont* gabriola = nullptr;        // "SDR990S" dourado acima do Band Scope
};

class Ui {
public:
    Ui(Radio& radio, IDirect3DDevice9* dev, const Fontes& f, float escala);
    ~Ui();

    void quadro();            // monta a tela inteira (uma vez por quadro)
    void salvarEstado();      // grava sliders/sintonia no RXSDR.ini
    void prepararSaida() { devolverModo(); }   // fechando: o radio volta ao modo de antes do decoder
    bool querSair() const { return sair_; }

private:
    // --- partes da tela ---
    void barraTopo();
    void painelControles(float y, float h);
    void painelEsquerdo(float x, float y, float w, float h);
    void smeter(float x, float y, float w, float h);
    void mostradorFreq(float x, float y, float w, float h);
    void olhoMagico(float x, float y, float w, float h);
    void grupoSliders(float x, float y, float w, float h, int qual);
    void linhaModos(float y, float h);
    void espectro(float x, float y, float w, float h);
    void escala(float x, float y, float w, float h);
    void cachoeira(float x, float y, float w, float h);
    void rodape(float y, float h);
    void janelaConfig();
    void gravarAjustes();                                   // campos da janela -> Config
    bool trocarAparelho(const std::string& tipo, const std::string& serial);
    void janelaSobre();
    void janelaDecoders();
    void anexarDecTexto(const std::string& novo);
    void devolverModo();
    bool decModoGuardado_ = false;
    std::string decModoAntes_;
    int decBwAntes_ = 0;
    // HFDL / AIS / APRS mudam a frequencia (e o HFDL a taxa): ao fechar, volta
    uint64_t decFreqAntes_ = 0; unsigned decTaxaAntes_ = 0;
    bool decMudouFreq_ = false, decMudouTaxa_ = false;
    int decHfdlBanda_ = 5, decAprsCanal_ = 0, decDrmSel_ = -1;
    void sintonizarDecoder(int tipo);
    void tabelaAvioes(const char* id, const std::vector<AeronaveHfdl>& avs);
    void tabelaAdsb(const std::vector<AdsbCore::Aviao>& avs);
    // squelch automatico (clique no SQL)
    bool sqlMedindo_ = false;
    double sqlIni_ = 0, sqlUlt_ = 0;
    std::vector<float> sqlAmostras_;
    std::string sqlDica_;
    void ifDisplay(float x, float y, float w, float h);
    void criarTexturaIf();
    void recolorirIf();
    void linhaIf();
    void escolherDecoder(int tipo, bool sintonizarModo);

    // --- logica ---
    void novoQuadroFft();
    void atualizarSmeter(float dt);
    void atualizarSom();
    void sintonizar(uint64_t hz, bool arredondar);
    void centrarParaZoom(bool forcar);   // com zoom: estacao no meio, com o LO do dongle fora do canal
    void mudarModo(const std::string& m);
    void ligarDesligar();
    void lerPedidoTeste();          // teste.pedido ao lado do exe (teste remoto)
    void arcoVfo();                 // escala em meia-lua da tela SUB (gira com o VFO)
    void trocarMainSub();           // tecla M/S
    // roda do mouse em cima de um digito da frequencia (texto alinhado a direita em xDir)
    void rodaDigitos(ImFont* f, float tam, float xDir, float y, const std::string& s, int rx, uint64_t pesoUltimo);
    void mudarFreqRx(int rx, int64_t deltaHz);
    double arcoF_ = 0;              // frequencia que a escala mostra (segue o VFO suavemente)
    double arcoResto_ = 0;          // Hz arrastados que ainda nao deram um passo
    void autoCachoeira();                 // botao AUTO (igual a pagina)
    void seguirAuto();                    // AUTO aceso: acompanha o piso
    double ultSeguirAuto_ = 0;
    double wfAcum_ = 0;                  // linhas da cachoeira por desenhar (Speed)
    void padrao();                        // botao PADRAO: guarda a aparencia atual
    bool medirPiso(float& piso) const;    // percentil 25 do espectro visivel
    void recolorirCachoeira();
    void reguaMemorias(float x, float y, float w, float h);
    void janelaMemorias();
    void carregarMemorias();
    void salvarMemorias();
    void alternarGravacao();
    void sintonizarMemoria(size_t i);
    void criarTexturaCachoeira(int largura);
    void linhaCachoeira();
    void carregarImagens();
    bool interacaoEspectro(float x, float y, float w, float h, const char* id);
    void menuBandas();                                   // botao direito na cachoeira/espectro
    void irParaBanda(uint64_t hz, const char* modo);
    double hzParaX(double hz, float x, float w) const;
    double xParaHz(float px, float x, float w) const;
    std::string fmtFreq(uint64_t hz) const;
    bool botao(const char* rot, float x, float y, float w, float h, bool ativo,
               unsigned corAtiva = 0, bool borda = true);
    bool slider(const char* id, const char* rotulo, const char* valor, float* v,
                float mn, float mx, float x, float y, float w, float passoRoda, unsigned corRotulo = 0,
                unsigned corValor = 0, unsigned corTrilho = 0);

    Radio& r_;
    IDirect3DDevice9* dev_;
    Fontes f_;
    float s_;                       // escala (DPI)
    bool sair_ = false;

    // estado da tela (vai para o .ini)
    float vol_ = 50, sql_ = -90, nb_ = 0, nr_ = 0, tom_ = 50, zoom_ = 0;
    float wfRange_ = 55, wfBrilho_ = 96, wfSpeed_ = 2, ganhoRf_ = 0;
    // velocidade de cada cachoeira: wfSpeed_ = a de baixo, wfSpeedEsc_ = a do Band Scope.
    // As linhas chegam no ritmo da mais rapida; a mais lenta junta linhas ao desenhar.
    float wfSpeedEsc_ = 2, wfVelDesenho_ = 2;
    int linhasMostradas(float hPx, int hMax) const;
    bool  nrEsp_ = false; float nrEspForca_ = 40;
    bool  mudo_ = false;
    int   passo_ = 1000;
    bool  autoWf_ = true;

    // S-meter
    float smAlvo_ = 0.05f, smAtual_ = 0.05f, smPico_ = 0.05f;
    // S-meter no compasso do som: o alvo vai para uma fila e sai atrasado pelo
    // tempo que o audio leva na fila da placa - o ponteiro mexe junto com o que se ouve
    std::vector<std::pair<double, float>> smHist_;
    float smRetorno_ = 0.9f;        // segundos: quanto o ponteiro demora para voltar (CONFIGURACAO)
    double smPicoT_ = 0;
    float peakDb_ = -120, peakDbSql_ = -120;
    double hfPiso_ = 1e9;
    bool sqlAberto_ = true;
    float olho_ = 0;

    // FFT / espectro
    QuadroFft fft_;
    std::vector<float> suave_, picoHold_;
    double ultimoQuadroT_ = 0, ultimaLinhaT_ = 0;
    uint64_t centroMostrado_ = 0;
    uint32_t taxaMostrada_ = 1024000;

    // cachoeira
    IDirect3DTexture9* wfTex_ = nullptr;
    int wfW_ = 0, wfH_ = 1200, wfPos_ = 0, wfLinhas_ = 0;
    std::vector<uint32_t> lut_;
    float lutRef_ = 1e9, lutRange_ = 1e9;
    std::vector<int8_t> wfDb_;          // dB de cada ponto (para recolorir tudo)
    uint64_t dadosCentro_ = 0; uint32_t dadosTaxa_ = 0;   // centro/taxa do ultimo quadro
    bool recolorir_ = false;
    double ultRecolor_ = 0;
    float wfAcimaPiso_ = 45, wfRangeCal_ = 53;
    std::string padraoMsg_;
    double padraoMsgAte_ = 0;

    // imagens
    IDirect3DTexture9* texSmeter_ = nullptr;
    IDirect3DTexture9* texOlho_ = nullptr;

    // arrastar
    bool arrastando_ = false;
    bool arrastandoBw_ = false;
    int  bordaBw_ = 0;                   // -1 esquerda, +1 direita
    float arrastoX0_ = 0, arrastoW_ = 1;
    uint64_t arrastoCentro0_ = 0, arrastoVfo0_ = 0;
    double centroVisual_ = 0;            // centro do que se desenha (arrasto)
    double alvoCentro_ = 0, alvoAte_ = 0;
    double ultTuneArrasto_ = 0;
    double muteAte_ = 0;

    // janelas
    bool cfgAberta_ = false, sobreAberta_ = false;
    std::string erro_;
    double erroAte_ = 0;
    char freqTxt_[32] = {0};
    bool editandoFreq_ = false;

    // config em edicao
    std::vector<DispositivoInfo> lista_;
    int cfgTipo_ = 0, cfgDisp_ = 0, cfgTaxa_ = 0, cfgQ_ = 2, cfgFft_ = 3;
    char cfgHost_[64] = "127.0.0.1"; int cfgPorta_ = 1234;
    bool cfgAgc_ = false, cfgBias_ = false, cfgIq_ = false; int cfgPpm_ = 0; float cfgGanho_ = 49.6f;
    int cfgLna_ = 9, cfgIfGain_ = 40; bool cfgIfAgc_ = false;
    int cfgS9Hf_ = -94, cfgS9Vhf_ = -93;
    // ajustes ao vivo (valem na hora, sem Aplicar)
    std::string cfgAssin_;          // assinatura do que ja foi aplicado
    double cfgUltAplic_ = 0;
    std::string cfgMsg_; bool cfgMsgErro_ = false;   // mensagem dentro da janela
    // teste do rtl_tcp em segundo plano (a tela nao congela)
    struct TesteTcp { std::atomic<int> estado{0}; std::string erro, info; };   // 0 testando, 1 ok, -1 falhou
    std::shared_ptr<TesteTcp> tcpTeste_;
    std::string tcpTesteAlvo_;

    // memorias (bookmarks.json ao lado do exe - mesmo formato do OpenWebRX)
    struct Memoria {
        std::string nome, modo, desc, modOriginal, alvo, pais;
        uint64_t f = 0;
        int bc = -1;                                   // -1 sem marca, 0 nao, 1 broadcast
        std::vector<std::pair<int, int>> hor;          // horarios UTC (hhmm-hhmm)
    };
    int memFiltroTipo_ = 0;         // 0 todas, 1 utilitarias, 2 bc no ar, 3 bc no ar Americas, 4 bc todas
    bool ehBroadcast(const Memoria& m) const;
    bool passaFiltro(const Memoria& m) const;
    void menuMemorias();
    bool espSobre_ = false;         // mouse em cima do espectro (esconde a marca na cachoeira)
    float espFrac_ = 0.32f;         // altura do espectro (fracao) - arrastavel pela regua
    std::vector<Memoria> mem_;
    bool memVisivel_ = true, memAberta_ = false, memSujo_ = false;
    char memFiltro_[64] = {0};
    char memNome_[96] = {0};
    int memSel_ = -1;

    // decodificadores
    bool decAberta_ = false;
    int  decTipo_ = 0;
    Decoders::Ajustes decAj_;
    std::string decTexto_;
    bool decRodando_ = false, decSelTudo_ = false;
    int  decSelPend_ = 0, decSelFeito_ = 0;
    int  decCol_ = 0, decLinhas_ = 0;
    int decCanalRtty_ = -1, decCanalSitor_ = -1, decCanalDsc_ = -1, decCanalAle_ = -1, decCanalPactor_ = -1;
    int decCanalOlivia_ = -1;
    int decCanalStanag_ = -1;
    int decCanalBpsk_ = -1;
    bool oliviaEncolher_ = false;    // a janela encolhe ao escolher o OLIVIA

    // FT8 / FT4
    void painelFt8();                 // bandas, relogio UTC, andamento do periodo
    static uint64_t freqFt8NaBanda(uint64_t vfo, bool ft4);
    void tabelaFt8(bool chegou);      // a lista (no lugar da caixa de texto)
    std::vector<Ft8Core::Mensagem> ft8Msgs_;
    bool ft8SoCq_ = false;
    char ft8Busca_[24] = {0};

    // SATELITES (Satelites.cpp): rastreio, polar em cruz, mapa e sintonia
    void painelSatelites();
    void atualizarSatMin();           // o resumo da barra minimizada
    void iniciarRastreador();
    void iniciarSubSat();             // liga SSTV / APRS / so audio, conforme o satelite
    void sintonizarSat(int idx);
    void seguirDoppler(double agora);
    const std::vector<SatInfo>& catSat() const;   // catalogo da categoria escolhida
    std::unique_ptr<Rastreador> sat_;
    int satFiltro_ = 0, satSel_ = -1, satSint_ = -1, satPassSel_ = 0, satElMin_ = 10, satDlVisto_ = 0;
    int satCat_ = 0;                  // 0 = radioamador, 1 = meteorologicos
    int satFiltroMet_ = 0;            // filtro da categoria de meteorologicos
    int satSub_ = 0;                  // Decoders::Tipo que o satelite sintonizado usa
    bool satDoppler_ = false, satQthDef_ = false;
    bool decMin_ = false;             // a janela de decodificadores minimizada (a barra do rodape)
    std::string satInfoMin_;          // o resumo da barra minimizada (satelite e os horarios)
    float satLat_ = -22.9f, satLon_ = -43.2f, satAlt_ = 0;
    char satLoc_[12] = {0};
    double satBaseHz_ = 0, satUltDop_ = 0, satUltCalc_ = 0, satPassesT_ = 0, satTleArqT_ = 0;
    uint64_t satUltVfo_ = 0;
    std::vector<Passagem> satPasses_; int satPassesDe_ = -1;
    std::vector<std::pair<float, float>> satTrilha_; double satTrilhaT_ = 0, satTrilhaT0_ = 0; int satTrilhaDe_ = -1;
    size_t satTxtTam_ = 0;

    // SSTV
    void painelSstv();
    void atualizarSstv();             // textura da imagem atual; terminadas -> PNG + historico
    IDirect3DTexture9* sstvTex_ = nullptr;
    int sstvTexW_ = 0, sstvTexH_ = 0, sstvW_ = 0, sstvH_ = 0;
    uint64_t sstvVer_ = 0;
    std::vector<uint32_t> sstvBuf_;
    struct SstvFeita { IDirect3DTexture9* tex = nullptr; int w = 0, h = 0; std::string arquivo, rotulo; };
    std::vector<SstvFeita> sstvHist_;
    int sstvModoManual_ = 2, sstvFreqSel_ = -1;
    bool sstvSalvar_ = true, sstvAumentar_ = false;
    std::vector<float> sstvEsp_;

    // WEFAX
    void painelWefax();
    void atualizarWefax();            // textura da imagem (meia resolucao) + historico
    IDirect3DTexture9* wfxTex_ = nullptr;
    int wfxTexW_ = 0, wfxTexH_ = 0, wfxW_ = 0, wfxH_ = 0;
    uint64_t wfxVer_ = 0;
    std::vector<uint32_t> wfxBuf_;
    std::vector<SstvFeita> wfxHist_;
    int decCanalWefax_ = -1, wfxLpm_ = 120, wfxIoc_ = 576;
    bool wfxAlinhar_ = false, wfxSalvar_ = true;

    // IF DISPLAY
    bool ifOn_ = false;
    int  ifTam_ = 1;                    // 0 estreita, 1 media, 2 larga
    IDirect3DTexture9* ifTex_ = nullptr;
    int  ifW_ = 512, ifH_ = 600, ifPos_ = 0;
    std::vector<int8_t> ifDb_;

    // saidas de som (placas)
    std::vector<std::string> saidas_;
    int cfgSaida_ = 0;

    // CPU
    double cpuT_ = 0; unsigned long long cpuProc_ = 0, cpuWall_ = 0; float cpu_ = 0;

    // ultimo ajuste mandado ao som
    AjusteAudio ajEnviado_;
    bool ajPrimeiro_ = true;

    // =====================================================================
    //  Painel SDR-990S (Painel.cpp) - o radio desenhado, botao por botao
    // =====================================================================
    void abrirConfig();
    void painel();
    void chassi();
    void telaCheia();
    void escopoTela(float sx, float sy, float sw, float sh, float& frac, const char* id, bool comIf);
    void areaBaixo(float x, float y, float w, float h);
    float painelFrac_ = 0.66f, fracBaixo_ = 0.35f;
    bool baixoAtivo_ = false;
    void colunaEsquerda();
    void telaPrincipal();
    void medidorAnalogico(float x, float y, float w, float h);
    void escopo(float x, float y, float w, float h);
    void teclasTela();
    void teclasF();
    void controlesBaixo();
    void colunaModos();
    void areaCentral();
    void telaSub();
    void blocoBandas();
    void blocoMeio();
    void colunaMain();
    void colunaSub();
    void popupsPainel();
    // peças desenhadas
    int  tecla(const char* id, const char* txt, float x, float y, float w, float h, int led = 0,
               const char* acima = nullptr, const char* dica = nullptr, unsigned corTxt = 0, bool fkey = false);
    bool giro(const char* id, float cx, float cy, float r, const char* rotE, const char* rotD,
              float tIn, float tOut, float& dIn, float& dOut, const char* dica, bool duplo = true);
    int  encoder(const char* id, float cx, float cy, float r, float& ang, const char* dica, bool* clique = nullptr);
    int  dial(const char* id, float cx, float cy, float r, float& ang, bool travado, const char* dica);
    // logica do painel
    struct Receptor {
        uint64_t f = 7100000; std::string modo = "LSB";
        int fil = 0;                         // FIL A / B / C
        float af = 50, rf = 100, sql = -90;  // AF 0..100, RF 0..100 (100 = ganho maximo), SQL dB
        bool nb1 = false, nb2 = false, nr1 = false, nr2 = false;
        bool nch = false, nchLargo = false, bef = false, anch = false, bc = false, apf = false, mute = false;
        float notch = 0.45f;                 // posicao do NOTCH (0..1 -> 150..3500 Hz)
        int agc = 2; bool agcOff = false;    // 1 SLOW, 2 MID, 3 FAST
        int rit = 0; bool ritOn = false;
        bool memModo = false; int memCanal = 0; bool lock = false; bool fine = false;
    };
    Receptor rx_[2];
    int ativo_ = 0, txVfo_ = 0;
    float filHi_[5][3]{}, filLo_[5][3]{};
    int grupoAplicado_ = -1, bwAplicado_ = -1;
    float sqlAplicado_ = -999.f, rfAplicado_ = -1.f;
    int att_ = 0, pamp_ = 0, medidorModo_ = 0, dim_ = 0, sDisp_ = 0, scopeAtt_ = 0, scp_ = 0;
    bool pausa_ = false, modoCentro_ = false, tracking_ = false, proc_ = false, sendMudo_ = false;
    bool tfSet_ = false, tfAtivoAntes_ = false;
    float nbNivel_ = 40, nb2Nivel_ = 0.5f, nrNivel_ = 40, procNivel_ = 0.5f, cwPitch_ = 700, sqlMargem_ = 2, sqlHang_ = 0.3f;
    bool sqlAuto_[2] = {false, false};   // squelch do receptor veio do automatico (VOX aceso)
    double sqlUltAberto_ = 0, sleepAte_ = 0;
    int paleta_ = 0, rxeq_ = 0;
    float angDialA_ = 0, angDialB_ = 0, angRit_ = 0, angMulti_ = 0;
    bool entrando_ = false; std::string entrada_;
    struct Rapida { uint64_t f; std::string modo; };
    std::vector<Rapida> qm_; int qmIdx_ = -1;
    std::vector<Rapida> bandaMem_;
    std::string ultimaGravacao_;
    std::string avisoPainel_; double avisoAte_ = 0;
    std::vector<float> congelado_;
    int grupoModo(const std::string& m) const;
    void sincronizarAtivo();
    void aplicarReceptor();
    void aplicarFiltro();
    void ativar(int i);
    void mudarModoPainel(const std::string& m);
    void sintonizarRx(int i, int64_t delta);
    void irBanda(int b);
    void digito(int d);
    void confirmarEntrada();
    void cwTune();
    void falarFrequencia(int i);
    void tocarUltimaGravacao();
    void aviso(const std::string& t, double seg = 2.0);
    void carregarPainel();
    void salvarPainel();
    std::string fmtFreqLcd(uint64_t hz) const;
};

} // namespace masdr
