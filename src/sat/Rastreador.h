#pragma once
// ---------------------------------------------------------------------------
//  Rastreador - previsao de passagens de satelites (decodificador SATELITES)
//
//  Os elementos orbitais (TLE) vem da Celestrak e ficam guardados em
//  satelites.tle ao lado do SDR990S.exe (funciona sem internet com o ultimo
//  arquivo baixado). A posicao sai do SGP4/SDP4 da libsgp4 (Daniel Warner,
//  Apache 2.0, third_party/sgp4).
//
//  Tempo: segundos UTC desde 1970 (double), igual ao resto do programa.
//  Angulos: graus. Distancias: km.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace masdr {

// --- catalogo (os dados ficam em SatCatalogo.cpp) ---
enum class TipoSat { FONIA_FM = 0, FONIA_SSB, SSTV, DADOS, APT, LRIT, VOZ };

// Categorias da lista de satelites. Radioamador: os satelites de ham com
// fonia/SSTV/dados. Meteorologicos: os de observacao da Terra (APT, LRIT).
// Militares: os de defesa com voz (ou imagem) NAO criptografada.
// ISS EVA: os canais da ISS usados nos caminhos espaciais (voz da Orlan).
// Naves da ISS: as telemetrias dos ferros Soyuz/Progress e do segmento
// russo (166 MHz, BITS e Regul).
enum class Categoria { RADIOAMADOR = 0, METEOROLOGICOS, MILITARES, ISS_EVA, NAVES_ISS };

struct SatInfo {
    const char* nome;     // nome na tela
    int norad;            // numero NORAD (procura no TLE)
    TipoSat tipo;
    uint64_t desce;       // downlink CENTRAL (Hz), sem Doppler
    uint64_t sobe;        // uplink (Hz), 0 = so escuta
    const char* modo;     // modo do radio: "NFM", "USB", "WFM"
    int banda;            // largura do filtro (Hz)
    const char* nota;     // tom, modo SSTV, faixa do transponder...
    bool ativo = true;    // false = desligado (nao ha sinal para receber)
};
const std::vector<SatInfo>& catalogoRadioamador();
const std::vector<SatInfo>& catalogoMeteorologicos();
const std::vector<SatInfo>& catalogoMilitares();
const std::vector<SatInfo>& catalogoIssEva();
const std::vector<SatInfo>& catalogoNavesIss();
const std::vector<SatInfo>& catalogo(Categoria c);
const char* nomeTipoSat(TipoSat t);
const char* nomeCategoria(Categoria c);
bool categoriaMeteo(TipoSat t);        // APT/LRIT: so o audio do radio

// --- resultados ---
struct PosSat {
    bool ok = false;
    double az = 0, el = 0;        // graus (az de 0 = norte, horario)
    double distKm = 0;
    double velRad = 0;            // km/s, positivo = afastando
    double lat = 0, lon = 0, altKm = 0;   // ponto abaixo do satelite
};

struct Passagem {
    double aos = 0, tca = 0, los = 0;     // entra, mais alto, sai (UTC)
    double elMax = 0, azAos = 0, azTca = 0, azLos = 0;
    std::vector<std::pair<float, float>> curva;   // (az, el) de AOS a LOS
};

class Rastreador {
public:
    Rastreador();
    ~Rastreador();

    static double agoraUtc();

    // QTH (graus, metros)
    void definirQth(double lat, double lon, double altM);

    // TLE: le o arquivo (formato de 3 linhas da Celestrak)
    bool carregarArquivo(const std::string& caminho, std::string& erro, bool mesclar = false);
    size_t nTle() const;
    bool temTle(int norad) const;
    double epocaTle(int norad) const;           // UTC da epoca (0 = nao tem)

    // download em segundo plano (grava em caminho so se vier um TLE valido).
    // Varias URLs: os grupos da Celestrak sao salvos todos no mesmo arquivo.
    void baixar(const std::vector<std::string>& urls, const std::string& caminho);
    int estadoDownload() const { return dl_.load(); }   // 0 nada, 1 baixando, 2 ok, -1 erro
    std::string msgDownload() const;

    // calculo
    PosSat posicao(int norad, double t) const;
    bool ehGeo(int norad) const;         // geostacionario (inclinacao < 5 graus)
    std::vector<Passagem> passagens(int norad, double t0, double horas, double elMin, int maxN = 100) const;
    // pontos (lat, lon) do caminho no chao entre t0 e t1
    std::vector<std::pair<float, float>> trilha(int norad, double t0, double t1, double passo) const;
    // ponto abaixo do sol (para a sombra da noite no mapa)
    static void posicaoSol(double t, double& lat, double& lon);

    // proximas passagens de uma lista de satelites, calculadas numa thread
    // propria (a tela nao trava): pedir() dispara; proxima() le o resultado
    void pedirProximas(const std::vector<int>& norads, double elMin);
    bool proxima(int norad, Passagem& p) const;   // false = nenhuma em 48 h (ou sem TLE)
    bool calculando() const { return calc_.load(); }

private:
    struct Item;
    std::map<int, std::shared_ptr<Item>> sats_;
    mutable std::mutex m_;
    double lat_ = -22.9, lon_ = -43.2, altKm_ = 0;

    std::atomic<int> dl_{0};
    std::string dlMsg_;
    mutable std::mutex mdl_;
    std::thread dlTh_;

    std::atomic<bool> calc_{false};
    std::thread calcTh_;
    std::map<int, Passagem> prox_;
    mutable std::mutex mp_;

    std::shared_ptr<Item> item(int norad) const;
    void qth(double& lat, double& lon, double& altKm) const;
};

} // namespace masdr
