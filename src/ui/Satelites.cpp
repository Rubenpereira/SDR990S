// ---------------------------------------------------------------------------
//  SATELITES - decodificador que acompanha satelites (SDR990S)
//
//  Categoria "Radioamador": fonia (FM e SSB), SSTV e dados (AX.25 1200).
//  A tela mostra:
//    - a lista com a frequencia CENTRAL de descida (sem Doppler) e a proxima
//      passagem de cada um (hora local);
//    - o grafico polar em cruz (N/L/S/O) com o caminho no ceu: onde o
//      satelite ENTRA (verde) e onde SAI (vermelho), e o ponto mais alto;
//    - o mapa-mundi com a trilha no chao, a area de cobertura e a noite;
//    - as passagens das proximas 24 h do satelite escolhido.
//  "Sintonizar" poe o radio na frequencia/modo e liga o decodificador que o
//  satelite pede: SSTV (imagens), APRS/direwolf (dados) ou so o audio (fonia).
//  "Corrigir Doppler" (opcional) vai seguindo o desvio durante a passagem.
// ---------------------------------------------------------------------------
#include "Ui.h"

#include "../app/Config.h"
#include "Idioma.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>

namespace masdr {

namespace {
#include "MapaMundi.inc"

#define TEXID(t) ((ImTextureID)(intptr_t)(t))

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;
constexpr double kLuz = 299792.458;    // km/s
// Grupos da Celestrak: os satelites de radioamador e os de observacao da
// Terra (meteorologicos). Os dois vao para o mesmo arquivo satelites.tle.
// Os tres NOAA (POES) foram tirados do grupo "weather" quando foram
// desativados em 2025 - vao por numero de catalogo, um pedido por satelite.
const char* const kUrlTle[] = {
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle",
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=weather&FORMAT=tle",
    "https://celestrak.org/NORAD/elements/gp.php?CATNR=25338&FORMAT=tle",   // NOAA 15
    "https://celestrak.org/NORAD/elements/gp.php?CATNR=28654&FORMAT=tle",   // NOAA 18
    "https://celestrak.org/NORAD/elements/gp.php?CATNR=33591&FORMAT=tle",   // NOAA 19
};

// Os militares nao estao em nenhum grupo da Celestrak: cada um e pedido pelo
// numero de catalogo (so os que ainda estao em orbita).
std::vector<std::string> urlsTle()
{
    std::vector<std::string> u(std::begin(kUrlTle), std::end(kUrlTle));
    std::vector<int> ids;
    for (const auto& s : catalogoMilitares())
        if (s.ativo) ids.push_back(s.norad);
    // o ISS (o mesmo NORAD nas categorias Radioamador e ISS EVA) nao esta
    // em nenhum grupo da Celestrak: vem por numero de catalogo
    for (const auto& s : catalogoIssEva())
        if (s.ativo) ids.push_back(s.norad);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (int id : ids) {
        char b[128];
        std::snprintf(b, sizeof b, "https://celestrak.org/NORAD/elements/gp.php?CATNR=%d&FORMAT=tle", id);
        u.push_back(b);
    }
    return u;
}

constexpr ImU32 C_VERDE   = IM_COL32(0x20, 0xff, 0x70, 255);
constexpr ImU32 C_VERM    = IM_COL32(0xff, 0x50, 0x40, 255);
constexpr ImU32 C_AMARELO = IM_COL32(0xff, 0xc8, 0x20, 255);
constexpr ImU32 C_CIANO   = IM_COL32(0x30, 0xe0, 0xff, 255);
constexpr ImU32 C_GRADE   = IM_COL32(0x28, 0x4d, 0x33, 255);
constexpr ImU32 C_TXT     = IM_COL32(0xdd, 0xdd, 0xdd, 255);
constexpr ImU32 C_APAGADO = IM_COL32(0x88, 0x88, 0x88, 255);

std::string pastaExe()
{
    char p[MAX_PATH]{};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string d(p);
    const auto pos = d.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : d.substr(0, pos);
}

// relogio da tela (o mesmo do Ui.cpp: erroAte_ conta nele)
double relogio()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string arquivoTle() { return pastaExe() + "\\satelites.tle"; }

// hora da ultima gravacao do arquivo (UTC, s desde 1970); 0 = nao existe
double tempoArquivo(const std::string& cam)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(cam.c_str(), GetFileExInfoStandard, &fa)) return 0;
    const unsigned long long t = ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    return (double)(t - 116444736000000000ULL) / 1e7;
}

tm local(double t)
{
    time_t tt = (time_t)std::floor(t);
    tm lt{};
    localtime_s(&lt, &tt);
    return lt;
}

std::string horaLocal(double t, bool seg = false)
{
    const tm lt = local(t);
    char b[16];
    if (seg) std::snprintf(b, sizeof b, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    else std::snprintf(b, sizeof b, "%02d:%02d", lt.tm_hour, lt.tm_min);
    return b;
}

std::string diaLocal(double t)
{
    const tm lt = local(t);
    char b[16];
    std::snprintf(b, sizeof b, "%02d/%02d", lt.tm_mday, lt.tm_mon + 1);
    return b;
}

bool mesmoDia(double a, double b)
{
    const tm x = local(a), y = local(b);
    return x.tm_yday == y.tm_yday && x.tm_year == y.tm_year;
}

std::string minSeg(double s)
{
    if (s < 0) s = 0;
    const int t = (int)std::lround(s);
    char b[24];
    if (t >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}

std::string emQuanto(double s)
{
    char b[32];
    if (s < 60) std::snprintf(b, sizeof b, T("em %.0f s"), std::max(0.0, s));
    else if (s < 3600) std::snprintf(b, sizeof b, T("em %d min"), (int)(s / 60));
    else std::snprintf(b, sizeof b, T("em %d h %02d min"), (int)(s / 3600), (int)(s / 60) % 60);
    return b;
}

const char* rumo(double az)
{
    static const char* const k[16] = {"N", "NNE", "NE", "ENE", "L", "ESE", "SE", "SSE",
                                      "S", "SSO", "SO", "OSO", "O", "ONO", "NO", "NNO"};
    const double a = std::fmod(std::fmod(az, 360.0) + 360.0 + 11.25, 360.0);
    return k[std::clamp((int)(a / 22.5), 0, 15)];
}

// Locator Maidenhead (GG87jb) -> centro do quadrado
bool locatorParaGraus(const char* in, double& lat, double& lon)
{
    std::string s;
    for (const char* p = in; *p; ++p)
        if (!std::isspace((unsigned char)*p)) s += (char)std::toupper((unsigned char)*p);
    if (s.size() != 4 && s.size() != 6) return false;
    if (s[0] < 'A' || s[0] > 'R' || s[1] < 'A' || s[1] > 'R' || !std::isdigit((unsigned char)s[2]) ||
        !std::isdigit((unsigned char)s[3]))
        return false;
    lon = (s[0] - 'A') * 20.0 - 180.0 + (s[2] - '0') * 2.0;
    lat = (s[1] - 'A') * 10.0 - 90.0 + (s[3] - '0') * 1.0;
    double wLon = 2.0, wLat = 1.0;
    if (s.size() == 6) {
        if (s[4] < 'A' || s[4] > 'X' || s[5] < 'A' || s[5] > 'X') return false;
        lon += (s[4] - 'A') * (2.0 / 24);
        lat += (s[5] - 'A') * (1.0 / 24);
        wLon = 2.0 / 24; wLat = 1.0 / 24;
    }
    lon += wLon / 2;
    lat += wLat / 2;
    return true;
}

std::string grausParaLocator(double lat, double lon)
{
    lon = std::clamp(lon + 180.0, 0.0, 359.9999);
    lat = std::clamp(lat + 90.0, 0.0, 179.9999);
    char b[8];
    b[0] = (char)('A' + (int)(lon / 20));
    b[1] = (char)('A' + (int)(lat / 10));
    b[2] = (char)('0' + (int)(std::fmod(lon, 20.0) / 2));
    b[3] = (char)('0' + (int)std::fmod(lat, 10.0));
    b[4] = (char)('a' + (int)(std::fmod(lon, 2.0) * 12));
    b[5] = (char)('a' + (int)(std::fmod(lat, 1.0) * 24));
    b[6] = 0;
    return b;
}

// ----------------------------- mapa ---------------------------------------
struct Proj {
    ImVec2 a; float w = 1, h = 1;
    ImVec2 operator()(double lat, double lon) const
    {
        return ImVec2(a.x + float((lon + 180.0) / 360.0 * w), a.y + float((90.0 - lat) / 180.0 * h));
    }
};

void linhasMapa(ImDrawList* dl, const Proj& pr, const short* pts, const int* ini, int n, ImU32 cor, float esp)
{
    for (int l = 0; l < n; ++l)
        for (int i = ini[l] + 1; i < ini[l + 1]; ++i) {
            const double lon0 = pts[2 * (i - 1)] * 0.01, lat0 = pts[2 * (i - 1) + 1] * 0.01;
            const double lon1 = pts[2 * i] * 0.01, lat1 = pts[2 * i + 1] * 0.01;
            if (std::fabs(lon1 - lon0) > 180) continue;
            dl->AddLine(pr(lat0, lon0), pr(lat1, lon1), cor, esp);
        }
}

// linha em (lat, lon) partida onde cruza a data (180 graus)
void caminho(ImDrawList* dl, const Proj& pr, const std::vector<std::pair<float, float>>& p, size_t i0, size_t i1,
             ImU32 cor, float esp)
{
    for (size_t i = std::max<size_t>(i0, 0) + 1; i <= i1 && i < p.size(); ++i) {
        if (std::fabs(p[i].second - p[i - 1].second) > 180) continue;
        dl->AddLine(pr(p[i - 1].first, p[i - 1].second), pr(p[i].first, p[i].second), cor, esp);
    }
}

// circulo de cobertura (de onde o satelite aparece acima do horizonte)
std::vector<std::pair<float, float>> pegada(double lat, double lon, double altKm)
{
    std::vector<std::pair<float, float>> out;
    const double re = 6378.137;
    const double lam = std::acos(re / (re + std::max(1.0, altKm)));
    const double f1 = lat * kRad, l1 = lon * kRad;
    for (int k = 0; k <= 90; ++k) {
        const double th = k * 4 * kRad;
        const double f2 = std::asin(std::sin(f1) * std::cos(lam) + std::cos(f1) * std::sin(lam) * std::cos(th));
        double l2 = l1 + std::atan2(std::sin(th) * std::sin(lam) * std::cos(f1), std::cos(lam) - std::sin(f1) * std::sin(f2));
        double lg = l2 / kRad;
        while (lg > 180) lg -= 360;
        while (lg < -180) lg += 360;
        out.emplace_back((float)(f2 / kRad), (float)lg);
    }
    return out;
}

// --------------------------- polar em cruz ---------------------------------
ImVec2 noCeu(ImVec2 c, float R, double az, double el)
{
    const float r = R * float((90.0 - std::clamp(el, 0.0, 90.0)) / 90.0);
    return ImVec2(c.x + r * (float)std::sin(az * kRad), c.y - r * (float)std::cos(az * kRad));
}

void textoAo(ImDrawList* dl, ImFont* f, float tam, ImVec2 p, ImU32 cor, const char* t, float ax, float ay)
{
    const ImVec2 ts = f->CalcTextSizeA(tam, FLT_MAX, 0, t);
    const ImVec2 q(p.x - ts.x * ax, p.y - ts.y * ay);
    dl->AddText(f, tam, ImVec2(q.x + 1, q.y + 1), IM_COL32(0, 0, 0, 200), t);
    dl->AddText(f, tam, q, cor, t);
}

void desenharPolar(ImDrawList* dl, ImVec2 c, float R, const Passagem* p, const PosSat* ag, float s, ImFont* fp, ImFont* fn)
{
    const float tp = ImGui::GetFontSize() * 0.85f;
    const float tn = ImGui::GetFontSize();
    dl->AddCircleFilled(c, R, IM_COL32(0x02, 0x0c, 0x06, 255), 128);
    // aneis de 30 em 30 graus de elevacao
    for (int el : {0, 30, 60}) {
        const float r = R * (90 - el) / 90.f;
        dl->AddCircle(c, r, el == 0 ? IM_COL32(0x30, 0xa0, 0x50, 255) : C_GRADE, 128, el == 0 ? 2.f * s : 1.f * s);
        if (el) {
            char b[8]; std::snprintf(b, sizeof b, "%d\xC2\xB0", el);
            textoAo(dl, fp, tp, ImVec2(c.x + 3 * s, c.y - r + 1 * s), C_APAGADO, b, 0, 0);
        }
    }
    // a CRUZ: norte-sul e leste-oeste
    const ImU32 cCruz = IM_COL32(0x40, 0xc0, 0x60, 220);
    dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), cCruz, 1.5f * s);
    dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y + R), cCruz, 1.5f * s);
    // marcas de 30 em 30 graus de azimute
    for (int a = 0; a < 360; a += 30) {
        const ImVec2 p0 = noCeu(c, R, a, 0), p1 = noCeu(c, R * 1.04f, a, 0);
        dl->AddLine(p0, p1, cCruz, 1.f * s);
    }
    const float m = 8 * s;
    textoAo(dl, fn, tn, ImVec2(c.x, c.y - R - m), C_TXT, "N", 0.5f, 1.f);
    textoAo(dl, fn, tn, ImVec2(c.x, c.y + R + m), C_TXT, "S", 0.5f, 0.f);
    textoAo(dl, fn, tn, ImVec2(c.x + R + m, c.y), C_TXT, "L", 0.f, 0.5f);
    textoAo(dl, fn, tn, ImVec2(c.x - R - m, c.y), C_TXT, "O", 1.f, 0.5f);

    if (p && p->curva.size() >= 2) {
        // caminho no ceu
        std::vector<ImVec2> pts;
        pts.reserve(p->curva.size());
        for (const auto& q : p->curva) pts.push_back(noCeu(c, R, q.first, q.second));
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(0xff, 0xa0, 0x20, 255), ImDrawFlags_None, 2.5f * s);
        // seta no meio, mostrando o sentido
        const size_t k = pts.size() / 2;
        const ImVec2 d = pts[std::min(k + 1, pts.size() - 1)] - pts[k - 1];
        const float n = std::sqrt(d.x * d.x + d.y * d.y);
        if (n > 0.5f) {
            const ImVec2 u(d.x / n, d.y / n), v(-u.y, u.x), ponta = pts[k] + u * (7 * s);
            dl->AddTriangleFilled(ponta, pts[k] - u * (5 * s) + v * (5 * s), pts[k] - u * (5 * s) - v * (5 * s),
                                  IM_COL32(0xff, 0xa0, 0x20, 255));
        }
        // ponto mais alto
        const ImVec2 pt = noCeu(c, R, p->azTca, p->elMax);
        dl->AddCircleFilled(pt, 3.5f * s, C_AMARELO);
        char b[24];
        std::snprintf(b, sizeof b, "%.0f\xC2\xB0", p->elMax);
        textoAo(dl, fp, tp, ImVec2(pt.x + 6 * s, pt.y), C_AMARELO, b, 0, 0.5f);
        // ENTRA (verde) e SAI (vermelho), com a hora, do lado de fora da cruz
        auto marca = [&](double az, double t, ImU32 cor, const char* rot, bool quadrado) {
            const ImVec2 q = noCeu(c, R, az, 0);
            if (quadrado) dl->AddRectFilled(q - ImVec2(5 * s, 5 * s), q + ImVec2(5 * s, 5 * s), cor);
            else dl->AddCircleFilled(q, 6 * s, cor);
            dl->AddCircle(q, 7 * s, IM_COL32(0, 0, 0, 255), 0, 1.5f * s);
            char t1[48];
            std::snprintf(t1, sizeof t1, "%s %s", rot, horaLocal(t).c_str());
            const float sx = (float)std::sin(az * kRad), sy = (float)-std::cos(az * kRad);
            const ImVec2 o = q + ImVec2(sx, sy) * (12 * s);
            textoAo(dl, fn, tn, o, cor, t1, sx < -0.3f ? 1.f : sx > 0.3f ? 0.f : 0.5f, sy < -0.3f ? 1.f : sy > 0.3f ? 0.f : 0.5f);
        };
        marca(p->azAos, p->aos, C_VERDE, T("ENTRA"), false);
        marca(p->azLos, p->los, C_VERM, T("SAI"), true);
    }
    // onde ele esta agora (se estiver acima do horizonte)
    if (ag && ag->ok && ag->el > 0) {
        const ImVec2 q = noCeu(c, R, ag->az, ag->el);
        dl->AddCircleFilled(q, 11 * s, IM_COL32(0x30, 0xe0, 0xff, 60));
        dl->AddCircleFilled(q, 5.5f * s, C_CIANO);
        dl->AddCircle(q, 6.5f * s, IM_COL32(0, 0, 0, 255), 0, 1.5f * s);
    }
}

} // namespace

// ===========================================================================
//  Ui
// ===========================================================================
void Ui::iniciarRastreador()
{
    if (sat_) return;
    sat_ = std::make_unique<Rastreador>();
    auto& c = Config::instance();
    satQthDef_ = !c.str("sat_lat").empty();
    satLat_ = (float)c.dbl("sat_lat", -22.9068);     // sem QTH: Rio de Janeiro (aviso na tela)
    satLon_ = (float)c.dbl("sat_lon", -43.1729);
    satAlt_ = (float)c.dbl("sat_alt", 0);
    const std::string loc = c.str("sat_loc", grausParaLocator(satLat_, satLon_));
    std::snprintf(satLoc_, sizeof satLoc_, "%s", loc.c_str());
    satFiltro_ = std::clamp((int)c.num("sat_filtro", 0), 0, 3);
    satFiltroMet_ = std::clamp((int)c.num("sat_filtro_met", 0), 0, 2);
    satCat_ = std::clamp((int)c.num("sat_cat", 0), 0, 4);
    satElMin_ = std::clamp((int)c.num("sat_elmin", 10), 0, 45);
    satDoppler_ = c.flag("sat_doppler", false);
    sat_->definirQth(satLat_, satLon_, satAlt_);
    std::string erro;
    const std::string arq = arquivoTle();
    const bool ok = sat_->carregarArquivo(arq, erro);
    satTleArqT_ = tempoArquivo(arq);
    // sem arquivo, ou com mais de 1 dia: baixa um novo (em segundo plano)
    if (!ok || Rastreador::agoraUtc() - satTleArqT_ > 86400) sat_->baixar(urlsTle(), arq);
    satUltCalc_ = 0;
    satDlVisto_ = sat_->estadoDownload();
}

// Catalogo da categoria que esta na tela (radioamador, meteorologicos,
// militares ou ISS EVA)
const std::vector<SatInfo>& Ui::catSat() const
{
    if (satCat_ == 1) return catalogoMeteorologicos();
    if (satCat_ == 2) return catalogoMilitares();
    if (satCat_ == 3) return catalogoIssEva();
    if (satCat_ == 4) return catalogoNavesIss();
    return catalogoRadioamador();
}

// Liga o decodificador que o satelite sintonizado pede
void Ui::iniciarSubSat()
{
    auto& d = r_.decoders();
    const int sub = satSub_ == Decoders::SSTV || satSub_ == Decoders::APRS ? satSub_ : (int)Decoders::SATELITES;
    if (sub == Decoders::APRS) decAj_.aprsBaud = 1200;
    if (d.tipo() != (Decoders::Tipo)sub || !decRodando_) d.iniciar((Decoders::Tipo)sub, decAj_);
    decRodando_ = true;
}

void Ui::sintonizarSat(int idx)
{
    const auto& cat = catSat();
    if (idx < 0 || idx >= (int)cat.size()) return;
    iniciarRastreador();
    const SatInfo& s = cat[idx];
    mudarModo(s.modo);
    r_.setBanda(s.banda);
    satSint_ = idx;
    satBaseHz_ = (double)s.desce;
    uint64_t f = s.desce;
    const PosSat p = sat_->posicao(s.norad, Rastreador::agoraUtc());
    if (satDoppler_ && p.ok) f = (uint64_t)std::llround(s.desce * (1.0 - p.velRad / kLuz));
    sintonizar(f, false);
    satUltVfo_ = r_.vfo();
    decMudouFreq_ = true;
    const int antes = satSub_;
    satSub_ = s.tipo == TipoSat::SSTV ? (int)Decoders::SSTV : s.tipo == TipoSat::DADOS ? (int)Decoders::APRS : (int)Decoders::SATELITES;
    if (satSub_ != antes || !decRodando_ || r_.decoders().tipo() != (Decoders::Tipo)satSub_) iniciarSubSat();
    char b[256];
    std::snprintf(b, sizeof b, T("[SAT] %s  %.3f MHz %s  -  %s\n"), s.nome, s.desce / 1e6, s.modo,
                  s.tipo == TipoSat::SSTV ? T("decodificador SSTV ligado")
                  : s.tipo == TipoSat::DADOS ? T("direwolf (AX.25 1200) ligado")
                  : s.tipo == TipoSat::APT ? T("APT: a imagem sai no audio do radio (WXtoIMG)")
                  : s.tipo == TipoSat::LRIT ? T("LRPT digital: o IQ bruto e para o SatDump")
                                           : T("fonia: ouça o áudio do rádio"));
    anexarDecTexto(b);
    if (!s.ativo)
        anexarDecTexto(std::string(T("Atenção: este satélite está DESATIVADO - ")) + s.nota + "\n");
}

// Segue o Doppler do satelite sintonizado. Se a pessoa mexer na sintonia
// (procurando uma estacao dentro do transponder SSB), o novo ponto vira a
// referencia e continua corrigido dali.
void Ui::seguirDoppler(double agora)
{
    const auto& cat = catSat();
    if (!satDoppler_ || !sat_ || satSint_ < 0 || satSint_ >= (int)cat.size()) return;
    if (agora - satUltDop_ < 0.5) return;
    satUltDop_ = agora;
    const SatInfo& s = cat[satSint_];
    const PosSat p = sat_->posicao(s.norad, agora);
    if (!p.ok) return;
    const double fator = 1.0 - p.velRad / kLuz;
    const uint64_t vfo = r_.vfo();
    if (satUltVfo_ && std::llabs((long long)vfo - (long long)satUltVfo_) > 5) satBaseHz_ = vfo / fator;
    if (p.el < -3) { satUltVfo_ = vfo; return; }
    const uint64_t alvo = (uint64_t)std::llround(satBaseHz_ * fator);
    // APT/LRIT: passos de 50 Hz (o sinal e largo, nao precisa de tanto)
    const long long lim = s.tipo == TipoSat::FONIA_SSB ? 25 : (categoriaMeteo(s.tipo) ? 50 : 250);
    if (std::llabs((long long)alvo - (long long)vfo) >= lim) sintonizar(alvo, false);
    satUltVfo_ = r_.vfo();
}

// resumo mostrado na barra minimizada; tambem mantem o Doppler vivo
// (o painel nao roda enquanto a janela esta minimizada)
void Ui::atualizarSatMin()
{
    iniciarRastreador();
    const double agora = Rastreador::agoraUtc();
    seguirDoppler(agora);
    const auto& cat = catSat();
    if (satSel_ < 0 || satSel_ >= (int)cat.size()) { satInfoMin_.clear(); return; }
    const SatInfo& s = cat[satSel_];
    Passagem p;
    const bool tle = sat_->temTle(s.norad);
    const bool tem = tle && sat_->proxima(s.norad, p) && p.los > agora;
    satInfoMin_ = s.nome;
    if (!tle) satInfoMin_ += T(" | sem elementos orbitais (TLE)");
    else if (!tem) satInfoMin_ += T(" | sem passagem em breve");
    else if (p.aos <= agora) satInfoMin_ += " | NO AR até " + horaLocal(p.los);
    else {
        satInfoMin_ += " | ENTRA " + horaLocal(p.aos) + " (" + emQuanto(p.aos - agora) + ")";
        satInfoMin_ += " | SAI " + horaLocal(p.los);
    }
}

void Ui::painelSatelites()
{
    iniciarRastreador();
    auto& cfg = Config::instance();
    const auto& cat = catSat();
    const double agora = Rastreador::agoraUtc();
    ImFont* fp = f_.pequena ? f_.pequena : ImGui::GetFont();
    ImFont* fn = f_.negrito ? f_.negrito : ImGui::GetFont();

    // TLE novo chegou: refaz as contas
    const int dl = sat_->estadoDownload();
    if (dl != satDlVisto_) {
        if (dl == 2) { satUltCalc_ = 0; satPassesDe_ = -1; satTleArqT_ = tempoArquivo(arquivoTle()); }
        satDlVisto_ = dl;
    }
    if (agora - satUltCalc_ > 60 && !sat_->calculando()) {
        std::vector<int> ns;
        for (const auto& s : cat) ns.push_back(s.norad);
        sat_->pedirProximas(ns, satElMin_);
        satUltCalc_ = agora;
    }
    seguirDoppler(agora);

    // ---- linha 1: categoria, tipo, elevacao minima ----
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T("Categoria"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170 * s_);
    if (ImGui::BeginCombo("##satcat", T(nomeCategoria((Categoria)satCat_)))) {
        for (int c = 0; c < 5; ++c) {
            if (ImGui::Selectable(T(nomeCategoria((Categoria)c)), satCat_ == c)) {
                satCat_ = c; cfg.set("sat_cat", c);
                satSel_ = -1; satSint_ = -1; satPassesDe_ = -1; satUltCalc_ = 0;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 16 * s_);
    // filtro por tipo: cada categoria tem os seus botoes
    static const char* const kFilHam[] = {"Todos", "Fonia", "SSTV", "Dados"};
    static const char* const kFilMet[] = {"Todos", "APT", "LRIT"};
    static const char* const kFilMil[] = {"Todos", "Fonia"};
    static const char* const kFilIss[] = {"Todos"};
    // a ISS EVA tem poucos canais (so voz e telemetria): so o filtro "Todos"
    const char* const* kFil = satCat_ == 1 ? kFilMet : (satCat_ == 2 ? kFilMil : (satCat_ >= 3 ? kFilIss : kFilHam));
    const int nFil = satCat_ == 1 ? 3 : (satCat_ == 2 ? 2 : (satCat_ >= 3 ? 1 : 4));
    int& filtro = satCat_ == 1 ? satFiltroMet_ : satFiltro_;
    for (int i = 0; i < nFil; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(T(kFil[i]), filtro == i)) {
            filtro = i;
            cfg.set(satCat_ == 1 ? "sat_filtro_met" : "sat_filtro", i);
        }
    }
    ImGui::SameLine(0, 16 * s_);
    ImGui::TextUnformatted(T("Elevação mínima"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70 * s_);
    char elTxt[16];
    std::snprintf(elTxt, sizeof elTxt, "%d\xC2\xB0", satElMin_);
    if (ImGui::BeginCombo("##satel", elTxt)) {
        for (int v : {0, 5, 10, 15, 20, 30, 45}) {
            char t[16];
            std::snprintf(t, sizeof t, "%d\xC2\xB0", v);
            if (ImGui::Selectable(t, v == satElMin_)) {
                satElMin_ = v; cfg.set("sat_elmin", v);
                satUltCalc_ = 0; satPassesDe_ = -1;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Só conta passagens que sobem pelo menos isto acima do horizonte"));
    ImGui::SameLine(0, 16 * s_);
    if (ImGui::Button(T("Minimizar"))) {
        // a janela inteira vira a barra fina do rodape (o radio continua visivel)
        decMin_ = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Vira a barra fina no rodapé da janela (clique em Satélites ▲ para voltar)"));

    // ---- linha 2: QTH e TLE ----
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("QTH");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(78 * s_);
    bool aplicar = false;
    if (ImGui::InputText("##satloc", satLoc_, sizeof satLoc_, ImGuiInputTextFlags_EnterReturnsTrue) ||
        ImGui::IsItemDeactivatedAfterEdit()) {
        double la, lo;
        if (locatorParaGraus(satLoc_, la, lo)) { satLat_ = (float)la; satLon_ = (float)lo; aplicar = true; }
        else { erro_ = T("Locator inválido (use 4 ou 6 caracteres, ex.: GG87jb)"); erroAte_ = relogio() + 5; }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(T("Locator (grid) do seu QTH, ex.: GG87jb - Enter aplica.\nOu digite latitude e longitude ao lado."));
    ImGui::SameLine();
    ImGui::TextUnformatted("Lat"); ImGui::SameLine();
    ImGui::SetNextItemWidth(80 * s_);
    ImGui::InputFloat("##satlat", &satLat_, 0, 0, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit()) aplicar = true;
    ImGui::SameLine();
    ImGui::TextUnformatted("Lon"); ImGui::SameLine();
    ImGui::SetNextItemWidth(80 * s_);
    ImGui::InputFloat("##satlon", &satLon_, 0, 0, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit()) aplicar = true;
    ImGui::SameLine();
    ImGui::TextUnformatted(T("Alt.")); ImGui::SameLine();
    ImGui::SetNextItemWidth(62 * s_);
    ImGui::InputFloat("##satalt", &satAlt_, 0, 0, "%.0f m");
    if (ImGui::IsItemDeactivatedAfterEdit()) aplicar = true;
    if (aplicar) {
        satLat_ = std::clamp(satLat_, -90.f, 90.f);
        satLon_ = std::clamp(satLon_, -180.f, 180.f);
        satAlt_ = std::clamp(satAlt_, -500.f, 9000.f);
        sat_->definirQth(satLat_, satLon_, satAlt_);
        cfg.set("sat_lat", (double)satLat_);
        cfg.set("sat_lon", (double)satLon_);
        cfg.set("sat_alt", (double)satAlt_);
        // o locator acompanha a lat/lon (se veio do locator, continua o mesmo)
        double la, lo;
        if (!locatorParaGraus(satLoc_, la, lo) || std::fabs(la - satLat_) > 0.05 || std::fabs(lo - satLon_) > 0.1)
            std::snprintf(satLoc_, sizeof satLoc_, "%s", grausParaLocator(satLat_, satLon_).c_str());
        cfg.set("sat_loc", std::string(satLoc_));
        satQthDef_ = true;
        satUltCalc_ = 0; satPassesDe_ = -1;
    }
    ImGui::SameLine(0, 16 * s_);
    if (dl == 1) ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "%s", T("baixando TLE..."));
    else {
        const double idade = satTleArqT_ > 0 ? (agora - satTleArqT_) / 3600.0 : -1;
        char b[96];
        if (idade < 0) std::snprintf(b, sizeof b, T("TLE: %d satélites"), (int)sat_->nTle());
        else if (idade < 48) std::snprintf(b, sizeof b, T("TLE: %d satélites (há %.0f h)"), (int)sat_->nTle(), idade);
        else std::snprintf(b, sizeof b, T("TLE: %d satélites (há %.0f dias)"), (int)sat_->nTle(), idade / 24);
        if (dl == -1 || sat_->nTle() == 0 || idade > 24 * 7) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", b);
        else ImGui::TextUnformatted(b);
    }
    if (ImGui::IsItemHovered()) {
        const std::string m = sat_->msgDownload();
        ImGui::SetTooltip("%s%s%s", T("Elementos orbitais da Celestrak (grupos amateur e weather), em satelites.tle ao lado do programa.\n"
                                      "Baixa sozinho quando tem mais de 1 dia."),
                          m.empty() ? "" : "\n\n", m.c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(dl == 1);
    if (ImGui::Button(T("Atualizar TLE"))) sat_->baixar(urlsTle(), arquivoTle());
    ImGui::EndDisabled();
    if (!satQthDef_) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "%s",
                           T("Digite o seu locator (ou lat/lon) em QTH: as passagens e o lado de entrada dependem de onde você está. (agora: Rio de Janeiro)"));
    }

    // ---- lista filtrada, ordenada pela proxima passagem ----
    struct Linha { int i; Passagem p; bool tem, tle, noAr; };
    std::vector<Linha> ls;
    const int filtroLista = satCat_ == 1 ? satFiltroMet_ : satFiltro_;
    for (int i = 0; i < (int)cat.size(); ++i) {
        const SatInfo& s = cat[i];
        bool passa;
        if (filtroLista == 0) passa = true;
        else if (satCat_ == 1) passa = (filtroLista == 1 ? s.tipo == TipoSat::APT : s.tipo == TipoSat::LRIT);
        else if (satCat_ == 2) passa = (s.tipo == TipoSat::VOZ);
        else if (satCat_ >= 3) passa = true;   // ISS EVA e Naves da ISS: so tem "Todos"
        else passa = (filtroLista == 1 && (s.tipo == TipoSat::FONIA_FM || s.tipo == TipoSat::FONIA_SSB)) ||
                     (filtroLista == 2 && s.tipo == TipoSat::SSTV) || (filtroLista == 3 && s.tipo == TipoSat::DADOS);
        if (!passa) continue;
        Linha l{i, {}, false, sat_->temTle(s.norad), false};
        l.tem = l.tle && sat_->proxima(s.norad, l.p) && l.p.los > agora;
        l.noAr = l.tem && l.p.aos <= agora;
        ls.push_back(l);
    }
    std::stable_sort(ls.begin(), ls.end(), [](const Linha& a, const Linha& b) {
        if (a.tem != b.tem) return a.tem;
        if (a.tem) return a.p.aos < b.p.aos;
        return a.tle > b.tle;
    });
    if ((satSel_ < 0 || satSel_ >= (int)cat.size()) && !ls.empty()) satSel_ = ls.front().i;

    const ImVec2 av = ImGui::GetContentRegionAvail();
    const float gap = 8 * s_;
    const float wR = std::clamp(av.x * 0.37f, 300 * s_, 470 * s_);
    const float wL = std::max(200 * s_, av.x - wR - gap);
    const float hTop = std::max(230 * s_, av.y * 0.55f);
    const float hBot = std::max(150 * s_, av.y - hTop - gap);

    // ================= lista de satelites (alto, esquerda) =================
    if (ImGui::BeginChild("##satlista", ImVec2(wL, hTop), ImGuiChildFlags_None)) {
        if (ImGui::BeginTable("##sats", 7, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(T("Satélite"), ImGuiTableColumnFlags_WidthStretch, 2.3f);
            ImGui::TableSetupColumn(T("Tipo"), ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn(T("Desce (MHz)"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(T("Modo"), ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn(T("Próxima"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(T("Máx"), ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn(T("Situação"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableHeadersRow();
            for (const Linha& l : ls) {
                const SatInfo& s = cat[l.i];
                ImGui::PushID(l.i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImVec4 cor(0.87f, 0.87f, 0.87f, 1);
                if (!l.tle) cor = ImVec4(0.5f, 0.5f, 0.5f, 1);
                else if (!s.ativo) cor = ImVec4(0.62f, 0.45f, 0.45f, 1);   // desligado
                else if (l.noAr) cor = ImVec4(0.2f, 1.f, 0.45f, 1);         // NO AR: verde
                else if (l.tem && l.p.aos - agora < 900) cor = ImVec4(0.2f, 1.f, 0.45f, 1);   // faltando 15 min: verde
                else if (l.tem && l.p.aos - agora < 3600) cor = ImVec4(1.f, 0.8f, 0.2f, 1);  // faltando 1 h: amarelo
                ImGui::PushStyleColor(ImGuiCol_Text, cor);
                std::string rot = s.nome;
                if (!s.ativo) rot = "\xE2\x9C\x97 " + rot;                // X = desligado
                if (l.i == satSint_) rot = "\xE2\x97\x8F " + rot;       // bolinha = sintonizado
                if (ImGui::Selectable(rot.c_str(), satSel_ == l.i,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    satSel_ = l.i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) sintonizarSat(l.i);
                }
                if (ImGui::IsItemHovered()) {
                    char sobe[48] = "";
                    if (s.sobe) std::snprintf(sobe, sizeof sobe, T("\nSubida: %.3f MHz"), s.sobe / 1e6);
                    ImGui::SetTooltip(T("%s\nDescida: %.3f MHz %s (central, sem Doppler)%s\n%s\nNORAD %d\n\nDuplo clique sintoniza"),
                                      s.nome, s.desce / 1e6, s.modo, sobe, s.nota, s.norad);
                }
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(T(nomeTipoSat(s.tipo)));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", s.desce / 1e6);
                ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(s.modo);
                ImGui::TableSetColumnIndex(4);
                if (l.tem) {
                    if (mesmoDia(l.p.aos, agora)) ImGui::TextUnformatted(horaLocal(l.p.aos).c_str());
                    else ImGui::Text("%s %s", diaLocal(l.p.aos).c_str(), horaLocal(l.p.aos).c_str());
                } else if (l.tle && sat_->ehGeo(s.norad)) ImGui::TextUnformatted(T("GEO"));
                else ImGui::TextUnformatted(l.tle ? "-" : T("sem TLE"));
                ImGui::TableSetColumnIndex(5);
                if (l.tem) ImGui::Text("%.0f\xC2\xB0", l.p.elMax); else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(6);
                if (!s.ativo) ImGui::TextUnformatted(T("DESATIVADO"));
                else if (l.noAr) ImGui::Text(T("NO AR - sai em %s"), minSeg(l.p.los - agora).c_str());
                else if (l.tem) ImGui::TextUnformatted(emQuanto(l.p.aos - agora).c_str());
                else if (l.tle && sat_->calculando()) ImGui::TextUnformatted(T("calculando..."));
                else if (l.tle && sat_->ehGeo(s.norad)) ImGui::TextUnformatted(T("GEO"));
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0, gap);

    // ================= polar em cruz (alto, direita) =================
    const SatInfo* sel = satSel_ >= 0 && satSel_ < (int)cat.size() ? &cat[satSel_] : nullptr;
    if (sel && (satPassesDe_ != satSel_ || agora - satPassesT_ > 60)) {
        const double aosAntes = satPassSel_ >= 0 && satPassSel_ < (int)satPasses_.size() && satPassesDe_ == satSel_
                                    ? satPasses_[satPassSel_].aos : -1;
        satPasses_ = sat_->passagens(sel->norad, agora, 24, satElMin_, 40);
        satPassSel_ = 0;
        for (int k = 0; k < (int)satPasses_.size(); ++k)
            if (std::fabs(satPasses_[k].aos - aosAntes) < 90) satPassSel_ = k;
        satPassesDe_ = satSel_;
        satPassesT_ = agora;
    }
    const Passagem* pp = satPassSel_ >= 0 && satPassSel_ < (int)satPasses_.size() ? &satPasses_[satPassSel_] : nullptr;
    const PosSat pa = sel ? sat_->posicao(sel->norad, agora) : PosSat();
    if (ImGui::BeginChild("##satpolar", ImVec2(wR, hTop), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
        if (!sel) ImGui::TextDisabled("%s", T("Escolha um satélite na lista"));
        else {
            ImGui::PushFont(fn);
            ImGui::TextColored(ImVec4(1.f, 0.78f, 0.2f, 1), "%s", sel->nome);
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("%.3f MHz %s", sel->desce / 1e6, sel->modo);
            const float lh = ImGui::GetTextLineHeightWithSpacing();
            const ImVec2 a0 = ImGui::GetContentRegionAvail();
            const float lado = std::max(80 * s_, std::min(a0.x, a0.y - 4.4f * lh));
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float marg = 28 * s_;
            const float R = std::max(30 * s_, lado * 0.5f - marg);
            const ImVec2 c(p0.x + a0.x * 0.5f, p0.y + lado * 0.5f);
            desenharPolar(ImGui::GetWindowDrawList(), c, R, pp, &pa, s_, fp, fn);
            ImGui::Dummy(ImVec2(a0.x, lado));
            // a passagem mostrada
            if (pp) {
                ImGui::TextColored(ImVec4(0.2f, 1.f, 0.45f, 1), T("Entra %s  Az %.0f\xC2\xB0 %s"), horaLocal(pp->aos, true).c_str(),
                                   pp->azAos, rumo(pp->azAos));
                ImGui::SameLine(0, 12 * s_);
                ImGui::TextColored(ImVec4(1.f, 0.35f, 0.3f, 1), T("Sai %s  Az %.0f\xC2\xB0 %s"), horaLocal(pp->los, true).c_str(),
                                   pp->azLos, rumo(pp->azLos));
                const bool noAr = pp->aos <= agora && pp->los > agora;
                ImGui::Text(T("Máx %.0f\xC2\xB0 às %s  -  %s  -  %s"), pp->elMax, horaLocal(pp->tca).c_str(),
                            minSeg(pp->los - pp->aos).c_str(),
                            noAr ? (std::string(T("NO AR, sai em ")) + minSeg(pp->los - agora)).c_str()
                                 : pp->aos > agora ? emQuanto(pp->aos - agora).c_str() : T("já passou"));
            } else {
                ImGui::TextDisabled(sat_->temTle(sel->norad) ? T("Nenhuma passagem acima da elevação mínima nas próximas 24 h")
                                                             : T("Sem TLE para este satélite - clique em Atualizar TLE"));
            }
            if (pa.ok) {
                const double dop = -(double)sel->desce * pa.velRad / kLuz;
                ImGui::Text(T("Agora: Az %.0f\xC2\xB0  El %.1f\xC2\xB0  %.0f km  Doppler %+.1f kHz"), pa.az, pa.el, pa.distKm, dop / 1000);
            }
            if (ImGui::Button(T("Sintonizar"))) sintonizarSat(satSel_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(T("Põe o rádio na descida (%.3f MHz %s, filtro %.1f kHz)\ne liga o decodificador que ele pede"),
                                  sel->desce / 1e6, sel->modo, sel->banda / 1000.0);
            ImGui::SameLine();
            if (ImGui::Checkbox(T("Corrigir Doppler"), &satDoppler_)) {
                cfg.set("sat_doppler", satDoppler_);
                satUltVfo_ = r_.vfo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(T("Desligado: o rádio fica na frequência central (sem Doppler).\n"
                                  "Ligado: a sintonia acompanha o Doppler durante a passagem\n"
                                  "(em SSB pode girar o dial: o ponto escolhido continua corrigido)."));
            if (satSint_ >= 0 && satSint_ < (int)cat.size()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f, 1.f, 0.45f, 1), "\xE2\x97\x8F %s", cat[satSint_].nome);
            }
        }
    }
    ImGui::EndChild();

    // ================= mapa (baixo, esquerda) =================
    if (ImGui::BeginChild("##satmapa", ImVec2(wL, hBot), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 a0 = ImGui::GetContentRegionAvail();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        float w = a0.x, h = w * 0.5f;
        if (h > a0.y) { h = a0.y; w = h * 2; }
        Proj pr;
        pr.a = ImVec2(p0.x + (a0.x - w) * 0.5f, p0.y);
        pr.w = w; pr.h = h;
        const ImVec2 b0 = pr.a, b1(pr.a.x + w, pr.a.y + h);
        d->PushClipRect(b0, b1, true);
        d->AddRectFilled(b0, b1, IM_COL32(0x04, 0x10, 0x20, 255));
        for (int lo = -150; lo <= 150; lo += 30) d->AddLine(pr(90, lo), pr(-90, lo), IM_COL32(0x14, 0x28, 0x3a, 255));
        for (int la = -60; la <= 60; la += 30) d->AddLine(pr(la, -180), pr(la, 180), la ? IM_COL32(0x14, 0x28, 0x3a, 255) : IM_COL32(0x22, 0x3c, 0x52, 255));
        // noite
        double solLat, solLon;
        Rastreador::posicaoSol(agora, solLat, solLon);
        double dec = solLat;
        if (std::fabs(dec) < 0.2) dec = dec < 0 ? -0.2 : 0.2;
        const float passoX = std::max(2.f, 3 * s_);
        for (float x = 0; x < w; x += passoX) {
            const double lon = (x + passoX * 0.5) / w * 360.0 - 180.0;
            const double lt = std::atan(-std::cos((lon - solLon) * kRad) / std::tan(dec * kRad)) / kRad;
            const float yt = pr.a.y + float((90.0 - lt) / 180.0 * h);
            if (dec > 0) d->AddRectFilled(ImVec2(pr.a.x + x, yt), ImVec2(pr.a.x + x + passoX, b1.y), IM_COL32(0, 0, 0, 105));
            else d->AddRectFilled(ImVec2(pr.a.x + x, b0.y), ImVec2(pr.a.x + x + passoX, yt), IM_COL32(0, 0, 0, 105));
        }
        linhasMapa(d, pr, kFronteira_pts, kFronteira_ini, kFronteira_n, IM_COL32(0x2a, 0x5a, 0x40, 255), 1.f);
        linhasMapa(d, pr, kCosta_pts, kCosta_ini, kCosta_n, IM_COL32(0x50, 0xc0, 0x70, 255), 1.2f * s_);
        // sol
        d->AddCircleFilled(pr(solLat, solLon), 5 * s_, IM_COL32(0xff, 0xe0, 0x40, 200));
        // outros satelites da lista (pontinhos)
        std::set<int> vistos;
        for (const Linha& l : ls) {
            const int n = cat[l.i].norad;
            if (!l.tle || (sel && n == sel->norad) || !vistos.insert(n).second) continue;
            const PosSat q = sat_->posicao(n, agora);
            if (!q.ok) continue;
            const ImVec2 pt = pr(q.lat, q.lon);
            d->AddCircleFilled(pt, 2.5f * s_, l.noAr ? IM_COL32(0x40, 0xff, 0x80, 220) : IM_COL32(0xb0, 0xb0, 0xb0, 170));
        }
        if (sel && pa.ok) {
            // trilha: 25 min para tras, 95 min para frente (de 20 em 20 s)
            if (satTrilhaDe_ != satSel_ || agora - satTrilhaT_ > 10) {
                satTrilhaT0_ = agora - 25 * 60;
                satTrilha_ = sat_->trilha(sel->norad, satTrilhaT0_, agora + 95 * 60, 20);
                satTrilhaDe_ = satSel_;
                satTrilhaT_ = agora;
            }
            const size_t iAgora = (size_t)std::clamp((agora - satTrilhaT0_) / 20.0, 0.0, (double)satTrilha_.size());
            caminho(d, pr, satTrilha_, 0, iAgora, IM_COL32(0xff, 0xa0, 0x20, 110), 1.5f * s_);
            caminho(d, pr, satTrilha_, iAgora, satTrilha_.size(), IM_COL32(0xff, 0xc8, 0x20, 230), 1.8f * s_);
            // trecho em que ele passa pelo QTH (verde e grosso)
            for (const Passagem& ps : satPasses_) {
                const double i0 = (ps.aos - satTrilhaT0_) / 20.0, i1 = (ps.los - satTrilhaT0_) / 20.0;
                if (i1 < 0 || i0 >= satTrilha_.size()) continue;
                caminho(d, pr, satTrilha_, (size_t)std::max(0.0, std::floor(i0)), (size_t)std::ceil(i1), C_VERDE, 3.f * s_);
            }
            // cobertura e o satelite
            const auto peg = pegada(pa.lat, pa.lon, pa.altKm);
            caminho(d, pr, peg, 0, peg.size(), IM_COL32(0x30, 0xe0, 0xff, 170), 1.3f * s_);
            const ImVec2 ps = pr(pa.lat, pa.lon);
            d->AddCircleFilled(ps, 6 * s_, C_CIANO);
            d->AddCircle(ps, 7 * s_, IM_COL32(0, 0, 0, 255), 0, 1.5f * s_);
            textoAo(d, fn, ImGui::GetFontSize(), ImVec2(ps.x + 9 * s_, ps.y - 2 * s_), C_CIANO, sel->nome, 0, 1);
        }
        // QTH
        const ImVec2 q = pr(satLat_, satLon_);
        d->AddLine(ImVec2(q.x - 6 * s_, q.y), ImVec2(q.x + 6 * s_, q.y), C_VERM, 2 * s_);
        d->AddLine(ImVec2(q.x, q.y - 6 * s_), ImVec2(q.x, q.y + 6 * s_), C_VERM, 2 * s_);
        textoAo(d, fp, ImGui::GetFontSize() * 0.85f, ImVec2(q.x + 7 * s_, q.y + 3 * s_), C_VERM, "QTH", 0, 0);
        d->PopClipRect();
        d->AddRect(b0, b1, C_GRADE);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton("##mapa", ImVec2(w, h));
        if (ImGui::IsItemHovered()) {
            const ImVec2 m = ImGui::GetMousePos();
            const double lo = (m.x - b0.x) / w * 360.0 - 180.0, la = 90.0 - (m.y - b0.y) / h * 180.0;
            ImGui::SetTooltip("%.2f  %.2f  (%s)", la, lo, grausParaLocator(la, lo).c_str());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0, gap);

    // ================= passagens + imagem/texto (baixo, direita) =================
    if (ImGui::BeginChild("##satbaixo", ImVec2(wR, hBot), ImGuiChildFlags_None)) {
        const float lh = ImGui::GetTextLineHeightWithSpacing();
        const float hTab = std::min(hBot * 0.45f, lh * 7.5f);
        if (ImGui::BeginTable("##passes", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                                 ImGuiTableFlags_SizingStretchProp, ImVec2(0, hTab))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(T("Dia"), ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn(T("Entra"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(T("Máx"), ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn(T("Sai"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(T("Dur."), ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableHeadersRow();
            for (int k = 0; k < (int)satPasses_.size(); ++k) {
                const Passagem& ps = satPasses_[k];
                ImGui::PushID(k);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool boa = ps.elMax >= 30;
                if (boa) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.f, 0.5f, 1));
                if (ImGui::Selectable(diaLocal(ps.aos).c_str(), k == satPassSel_, ImGuiSelectableFlags_SpanAllColumns))
                    satPassSel_ = k;
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s %s", horaLocal(ps.aos).c_str(), rumo(ps.azAos));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.0f\xC2\xB0 %s", ps.elMax, horaLocal(ps.tca).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s %s", horaLocal(ps.los).c_str(), rumo(ps.azLos));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(minSeg(ps.los - ps.aos).c_str());
                if (boa) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("%s", T("Horários locais. Verde = passagem alta (30\xC2\xB0 ou mais)."));
        // SSTV: a imagem chegando; o resto: o texto do decodificador
        auto& dec = r_.decoders();
        if (dec.tipo() == Decoders::SSTV) {
            auto& sv = dec.sstv();
            const SstvCore::Status stt = sv.status();
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 a0 = ImGui::GetContentRegionAvail();
            const float hb = lh;
            const int iw = sstvW_ > 0 ? sstvW_ : 320, ih = sstvH_ > 0 ? sstvH_ : 256;
            const float esc = std::max(0.1f, std::min(a0.x / iw, (a0.y - hb - 4 * s_) / ih));
            const ImVec2 tam(iw * esc, ih * esc);
            const ImVec2 a(p0.x + (a0.x - tam.x) * 0.5f, p0.y), b(a.x + tam.x, a.y + tam.y);
            d->AddRectFilled(a, b, IM_COL32(0x08, 0x08, 0x08, 255));
            if (sstvTex_ && sstvW_ > 0) d->AddImage(TEXID(sstvTex_), a, b);
            else textoAo(d, ImGui::GetFont(), ImGui::GetFontSize(), (a + b) * 0.5f, C_APAGADO, T("esperando uma imagem SSTV..."), 0.5f, 0.5f);
            d->AddRect(a, b, C_GRADE);
            const float frac = stt.linhas > 0 ? float(stt.linha) / stt.linhas : 0.f;
            const ImVec2 ba(a.x, b.y + 4 * s_), bb(b.x, b.y + 4 * s_ + hb * 0.8f);
            d->AddRectFilled(ba, bb, IM_COL32(0x10, 0x1e, 0x14, 255));
            d->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * frac, bb.y), IM_COL32(0x00, 0xb0, 0x50, 255));
            char pr[128];
            if (stt.modo >= 0) std::snprintf(pr, sizeof pr, T("%s - linha %d de %d"), SstvCore::nomeModo(stt.modo), stt.linha, stt.linhas);
            else std::snprintf(pr, sizeof pr, "%s", T("SSTV: esperando o VIS"));
            textoAo(d, fp, ImGui::GetFontSize() * 0.85f, ImVec2(ba.x + 4 * s_, (ba.y + bb.y) * 0.5f), C_TXT, pr, 0, 0.5f);
            ImGui::Dummy(ImVec2(a0.x, tam.y + hb + 6 * s_));
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0x02, 0x06, 0x03, 255));
            if (ImGui::BeginChild("##sattxt", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
                ImGui::PushFont(f_.mono ? f_.mono : ImGui::GetFont());
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xb0, 0xf0, 0x20, 255));
                ImGui::PushTextWrapPos(0.0f);       // a caixa e estreita: quebra as linhas
                ImGui::TextUnformatted(decTexto_.c_str(), decTexto_.c_str() + decTexto_.size());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (decTexto_.size() != satTxtTam_) {
                    satTxtTam_ = decTexto_.size();
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
}

} // namespace masdr
