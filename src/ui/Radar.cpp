// ---------------------------------------------------------------------------
//  RADAR AEREO - tela de radar com os avioes que o dongle esta ouvindo
//  (SDR990S)
//
//  Nao e um decodificador novo: usa os que ja existem. O ADSB (1090 MHz) da
//  posicao, altitude, velocidade e rumo varias vezes por segundo; o ACARS,
//  o VDL2 e o HFDL so de vez em quando (relatorio de posicao). O radar le as
//  listas desses decodificadores e desenha como um radar de verdade (PPI):
//    - a agulha gira 360 graus; quando passa por um aviao que esta sendo
//      ouvido, ele "acende" na posicao daquele instante e vai apagando
//      (persistencia do fosforo). Se o aviao continua sendo ouvido, acende de
//      novo na volta seguinte; se parou de ser ouvido, nao volta mais;
//    - etiqueta de cada alvo (voo / prefixo, nivel de voo, velocidade,
//      modelo e empresa), rastro das ultimas posicoes e vetor de 1 minuto;
//    - mapa (costa e fronteiras do Natural Earth), aeroportos (OurAirports),
//      aneis de distancia em volta da estacao e a escala de 0 a 359 graus;
//    - lista dos alvos por distancia, ficha do escolhido (links do
//      FlightAware / ADS-B Exchange / Google Maps) e o registro de eventos.
//  O centro (o QTH de quem usa) e perguntado na primeira vez: locator
//  (GG87, GG87jb) ou coordenadas (-22.8349,-42.3599). Fica no SDR990S.ini.
//  Enquanto o radar esta aberto a janela do programa ocupa o monitor inteiro,
//  sem bordas (main.cpp, g_telaCheia).
// ---------------------------------------------------------------------------
#include "Ui.h"

#include "../app/Config.h"
#include "Idioma.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace masdr {

extern int g_telaCheia;   // main.cpp: 1 = a janela do programa ocupa o monitor inteiro (sem bordas)

namespace {
#include "MapaRadar.inc"     // Natural Earth 1:50m: costa, fronteiras e divisas de estados
#include "MapaMundi.inc"     // Natural Earth 1:110m: para o alcance de continente / planeta (mais leve)
#include "Aeroportos.inc"    // OurAirports: aeroportos grandes e medios

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;
constexpr double kTerra = 6371.0088;      // raio medio da Terra (km)
constexpr double kNm = 1.852;             // km por milha nautica
constexpr double kMemoriaExt = 900;       // ACARS/VDL2/HFDL: a posicao vale 15 min (eles mandam pouco)
constexpr float  kAlcMin = 10, kAlcMax = 20000;   // 20 mil km = o outro lado do planeta (HFDL)
constexpr float  kAlcPadrao = 450;                // o radar sempre abre neste alcance (e o RESET volta para ele)
constexpr float  kPassoRoda = 0.82f;              // cada clique da roda: 18% mais perto / mais longe

constexpr ImU32 C_FOSFORO = IM_COL32(0x40, 0xff, 0x9a, 255);
constexpr ImU32 C_GRADE   = IM_COL32(0x22, 0x80, 0x58, 150);
constexpr ImU32 C_RAIO    = IM_COL32(0x1a, 0x5c, 0x40, 110);
constexpr ImU32 C_ESCALA  = IM_COL32(0x48, 0xe8, 0xd0, 255);
constexpr ImU32 C_ESCALA2 = IM_COL32(0x2a, 0x9a, 0x8c, 255);
constexpr ImU32 C_TEXTO   = IM_COL32(0xd8, 0xf0, 0xe4, 255);
constexpr ImU32 C_APAGADO = IM_COL32(0x80, 0x98, 0x8c, 255);
constexpr ImU32 C_AMBAR   = IM_COL32(0xff, 0xb4, 0x28, 255);
constexpr ImU32 C_VERM    = IM_COL32(0xff, 0x40, 0x40, 255);
constexpr ImU32 C_COSTA   = IM_COL32(0x3a, 0xb0, 0x90, 170);
constexpr ImU32 C_FRONT   = IM_COL32(0x3a, 0x88, 0xb0, 140);
constexpr ImU32 C_ESTADO  = IM_COL32(0x34, 0x6c, 0x7c, 105);
constexpr ImU32 C_AERO    = IM_COL32(0x70, 0xa8, 0xd8, 200);
constexpr ImU32 C_TERRA   = IM_COL32(0xff, 0x4f, 0xb4, 255);   // estacao de terra do HFDL (transmissao)
constexpr ImU32 C_NET     = IM_COL32(0x9c, 0xf0, 0xff, 255);   // posicao que veio da INTERNET (adsb.lol), nao do receptor

double relogio()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

ImU32 comAlfa(ImU32 c, float a)
{
    const int al = (int)std::lround(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.f, 1.f));
    return (c & ~IM_COL32_A_MASK) | ((ImU32)al << IM_COL32_A_SHIFT);
}

ImU32 misturar(ImU32 a, ImU32 b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    auto canal = [&](int sh) {
        const float x = (float)((a >> sh) & 0xFF), y = (float)((b >> sh) & 0xFF);
        return (ImU32)std::lround(x + (y - x) * t) << sh;
    };
    return canal(IM_COL32_R_SHIFT) | canal(IM_COL32_G_SHIFT) | canal(IM_COL32_B_SHIFT) | canal(IM_COL32_A_SHIFT);
}

// ----------------------------- geografia -----------------------------------
double difLon(double a, double b) { return std::fmod(a - b + 540.0, 360.0) - 180.0; }

double distKm(double la1, double lo1, double la2, double lo2)
{
    const double p1 = la1 * kRad, p2 = la2 * kRad, dp = (la2 - la1) * kRad, dl = difLon(lo2, lo1) * kRad;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * kTerra * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1 - a)));
}

double azimute(double la1, double lo1, double la2, double lo2)
{
    const double p1 = la1 * kRad, p2 = la2 * kRad, dl = difLon(lo2, lo1) * kRad;
    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    return std::fmod(std::atan2(y, x) / kRad + 360.0, 360.0);
}

// ponto a dKm de (la, lo) no rumo az
void destino(double la, double lo, double az, double dKm, double& la2, double& lo2)
{
    const double d = dKm / kTerra, p1 = la * kRad, l1 = lo * kRad, th = az * kRad;
    const double p2 = std::asin(std::sin(p1) * std::cos(d) + std::cos(p1) * std::sin(d) * std::cos(th));
    const double l2 = l1 + std::atan2(std::sin(th) * std::sin(d) * std::cos(p1), std::cos(d) - std::sin(p1) * std::sin(p2));
    la2 = p2 / kRad;
    lo2 = std::fmod(l2 / kRad + 540.0, 360.0) - 180.0;
}

// Locator Maidenhead (GG87, GG87jb, GG87jb55) -> centro do quadrado
bool locatorParaGraus(const std::string& in, double& lat, double& lon)
{
    std::string s;
    for (char c : in)
        if (!std::isspace((unsigned char)c)) s += (char)std::toupper((unsigned char)c);
    if (s.size() != 4 && s.size() != 6 && s.size() != 8) return false;
    if (s[0] < 'A' || s[0] > 'R' || s[1] < 'A' || s[1] > 'R' || !std::isdigit((unsigned char)s[2]) ||
        !std::isdigit((unsigned char)s[3]))
        return false;
    lon = (s[0] - 'A') * 20.0 - 180.0 + (s[2] - '0') * 2.0;
    lat = (s[1] - 'A') * 10.0 - 90.0 + (s[3] - '0') * 1.0;
    double wLon = 2.0, wLat = 1.0;
    if (s.size() >= 6) {
        if (s[4] < 'A' || s[4] > 'X' || s[5] < 'A' || s[5] > 'X') return false;
        lon += (s[4] - 'A') * (2.0 / 24);
        lat += (s[5] - 'A') * (1.0 / 24);
        wLon = 2.0 / 24; wLat = 1.0 / 24;
    }
    if (s.size() == 8) {
        if (!std::isdigit((unsigned char)s[6]) || !std::isdigit((unsigned char)s[7])) return false;
        lon += (s[6] - '0') * (wLon / 10);
        lat += (s[7] - '0') * (wLat / 10);
        wLon /= 10; wLat /= 10;
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

std::string aparar(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// O centro digitado: locator (GG87 / GG87jb / GG87jb55) ou coordenadas em
// graus decimais com ponto ("-22.8349,-42.3599", "-22.8349; -42.3599" ou
// "-22.8349 -42.3599").
bool lerCentro(const std::string& in, double& lat, double& lon, std::string& erro)
{
    std::string s = aparar(in);
    erro.clear();
    if (s.empty()) { erro = T("Digite o locator ou as coordenadas."); return false; }
    bool soAlnum = true;
    for (char c : s) if (!std::isalnum((unsigned char)c)) soAlnum = false;
    if (soAlnum && std::isalpha((unsigned char)s[0])) {
        if (locatorParaGraus(s, lat, lon)) return true;
        erro = T("Locator inválido (exemplos: GG87, GG87jb).");
        return false;
    }
    for (char& c : s) if (c == ';') c = ',';
    std::string a, b;
    const size_t v = s.find(',');
    if (v != std::string::npos) {
        a = s.substr(0, v); b = s.substr(v + 1);
        if (b.find(',') != std::string::npos) {
            erro = T("Use ponto nos decimais e vírgula entre os dois números: -22.8349,-42.3599");
            return false;
        }
    } else {
        const size_t e = s.find(' ');
        if (e == std::string::npos) { erro = T("Faltou a longitude: -22.8349,-42.3599"); return false; }
        a = s.substr(0, e); b = s.substr(e + 1);
    }
    a = aparar(a); b = aparar(b);
    char* f1 = nullptr; char* f2 = nullptr;
    lat = std::strtod(a.c_str(), &f1);
    lon = std::strtod(b.c_str(), &f2);
    if (a.empty() || b.empty() || *f1 || *f2) {
        erro = T("Não entendi os números. Exemplo: -22.8349,-42.3599");
        return false;
    }
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        erro = T("Latitude vai de -90 a 90 e longitude de -180 a 180.");
        return false;
    }
    return true;
}

// ----------------------------- projecao ------------------------------------
// Azimutal equidistante em volta do centro da vista: a distancia e o rumo a
// partir do centro sao os verdadeiros (como o radar ve).
struct Proj {
    double lat0 = 0, lon0 = 0, sla0 = 0, cla0 = 1;
    ImVec2 c;
    float R = 1;
    double alc = 100;     // km do centro ate a borda
    double esc = 1;       // pixels por radiano de Terra
    void ajustar(double la, double lo, ImVec2 centro, float raio, double alcance)
    {
        lat0 = la; lon0 = lo; sla0 = std::sin(la * kRad); cla0 = std::cos(la * kRad);
        c = centro; R = raio; alc = alcance; esc = kTerra / alc * R;
    }
    ImVec2 xy(double lat, double lon) const
    {
        const double la = lat * kRad, dl = difLon(lon, lon0) * kRad;
        const double sla = std::sin(la), cla = std::cos(la), cdl = std::cos(dl);
        const double cc = std::clamp(sla0 * sla + cla0 * cla * cdl, -1.0, 1.0);
        const double ang = std::acos(cc);
        const double k = ang < 1e-9 ? 1.0 : ang / std::sin(ang);
        const double x = k * cla * std::sin(dl), y = k * (cla0 * sla - sla0 * cla * cdl);
        return ImVec2(c.x + (float)(x * esc), c.y - (float)(y * esc));
    }
    void inv(ImVec2 p, double& lat, double& lon) const
    {
        const double x = (p.x - c.x) / esc, y = -(p.y - c.y) / esc;
        const double rho = std::min(std::hypot(x, y), kPi);
        if (rho < 1e-12) { lat = lat0; lon = lon0; return; }
        const double sc = std::sin(rho), cc = std::cos(rho);
        lat = std::asin(std::clamp(cc * sla0 + y * sc * cla0 / rho, -1.0, 1.0)) / kRad;
        lon = lon0 + std::atan2(x * sc, rho * cla0 * cc - y * sla0 * sc) / kRad;
        lon = std::fmod(lon + 540.0, 360.0) - 180.0;
    }
};

// recorta o segmento a-b dentro do circulo (c, R)
bool recortar(ImVec2 a, ImVec2 b, ImVec2 c, float R, ImVec2& a2, ImVec2& b2)
{
    const ImVec2 d = b - a, f = a - c;
    const float A = d.x * d.x + d.y * d.y;
    const float B = 2 * (f.x * d.x + f.y * d.y);
    const float Cq = f.x * f.x + f.y * f.y - R * R;
    if (A < 1e-6f) { if (Cq <= 0) { a2 = a; b2 = b; return true; } return false; }
    const float disc = B * B - 4 * A * Cq;
    if (disc <= 0) return false;
    const float sq = std::sqrt(disc);
    const float t1 = std::max(0.f, (-B - sq) / (2 * A)), t2 = std::min(1.f, (-B + sq) / (2 * A));
    if (t1 >= t2) return false;
    a2 = a + d * t1; b2 = a + d * t2;
    return true;
}

// distancia (pixels) de p ate a borda do circulo (c, R) andando no rumo az; < 0 = nao chega
float ateBorda(ImVec2 p, double az, ImVec2 c, float R)
{
    const ImVec2 u((float)std::sin(az * kRad), (float)-std::cos(az * kRad)), f = p - c;
    const float b = f.x * u.x + f.y * u.y, cq = f.x * f.x + f.y * f.y - R * R;
    const float disc = b * b - cq;
    if (disc < 0) return -1;
    return -b + std::sqrt(disc);
}

// ----------------------------- desenho -------------------------------------
void triCor(ImDrawList* dl, ImVec2 a, ImU32 ca, ImVec2 b, ImU32 cb, ImVec2 c, ImU32 cc)
{
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(3, 3);
    const ImDrawIdx i0 = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(a, uv, ca);
    dl->PrimWriteVtx(b, uv, cb);
    dl->PrimWriteVtx(c, uv, cc);
    dl->PrimWriteIdx(i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + 1)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 2));
}

// disco com degrade do centro para a borda (o "vidro" do radar)
void discoDegrade(ImDrawList* dl, ImVec2 c, float r, ImU32 cCentro, ImU32 cBorda, int n = 120)
{
    for (int i = 0; i < n; ++i) {
        const float a1 = i * 2 * (float)kPi / n, a2 = (i + 1) * 2 * (float)kPi / n;
        triCor(dl, c, cCentro, ImVec2(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r), cBorda,
               ImVec2(c.x + std::cos(a2) * r, c.y + std::sin(a2) * r), cBorda);
    }
}

void textoSombra(ImDrawList* dl, ImFont* f, float tam, ImVec2 p, ImU32 cor, const char* t, float ax = 0, float ay = 0)
{
    const ImVec2 ts = f->CalcTextSizeA(tam, FLT_MAX, 0, t);
    const ImVec2 q(std::floor(p.x - ts.x * ax), std::floor(p.y - ts.y * ay));
    dl->AddText(f, tam, ImVec2(q.x + 1, q.y + 1), IM_COL32(0, 0, 0, (int)(((cor >> IM_COL32_A_SHIFT) & 0xFF) * 0.8f)), t);
    dl->AddText(f, tam, q, cor, t);
}

// texto centrado em p e girado de ang (radianos, horario na tela)
void textoGirado(ImDrawList* dl, ImFont* f, float tam, ImVec2 p, float ang, ImU32 cor, const char* t)
{
    const ImVec2 ts = f->CalcTextSizeA(tam, FLT_MAX, 0, t);
    const int v0 = dl->VtxBuffer.Size;
    dl->AddText(f, tam, ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), cor, t);
    const float cs = std::cos(ang), sn = std::sin(ang);
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& q = dl->VtxBuffer[i].pos;
        const float dx = q.x - p.x, dy = q.y - p.y;
        q = ImVec2(p.x + dx * cs - dy * sn, p.y + dx * sn + dy * cs);
    }
}

// silhueta de aviao (nariz para o rumo), tamanho = meia envergadura
void aviao(ImDrawList* dl, ImVec2 p, float tam, double rumo, ImU32 cor)
{
    const float cs = (float)std::cos(rumo * kRad), sn = (float)std::sin(rumo * kRad);
    auto q = [&](float x, float y) { return ImVec2(p.x + (x * cs - y * sn) * tam, p.y + (x * sn + y * cs) * tam); };
    const ImVec2 fus[] = {q(0, -1.05f), q(0.13f, -0.8f), q(0.13f, 0.85f), q(0, 0.95f), q(-0.13f, 0.85f), q(-0.13f, -0.8f)};
    dl->AddConvexPolyFilled(fus, 6, cor);
    const ImVec2 asa[] = {q(-0.12f, -0.3f), q(0.12f, -0.3f), q(1.0f, 0.18f), q(1.0f, 0.3f), q(-1.0f, 0.3f), q(-1.0f, 0.18f)};
    dl->AddConvexPolyFilled(asa, 6, cor);
    const ImVec2 cauda[] = {q(-0.1f, 0.6f), q(0.1f, 0.6f), q(0.42f, 0.88f), q(0.42f, 0.98f), q(-0.42f, 0.98f), q(-0.42f, 0.88f)};
    dl->AddConvexPolyFilled(cauda, 6, cor);
}

// so o contorno do aviao (posicao que veio da internet, nao do receptor)
void aviaoContorno(ImDrawList* dl, ImVec2 p, float tam, double rumo, ImU32 cor, float esp)
{
    const float cs = (float)std::cos(rumo * kRad), sn = (float)std::sin(rumo * kRad);
    auto q = [&](float x, float y) { return ImVec2(p.x + (x * cs - y * sn) * tam, p.y + (x * sn + y * cs) * tam); };
    const ImVec2 c[] = {q(0, -1.05f), q(0.13f, -0.8f), q(0.13f, -0.3f), q(1.0f, 0.18f), q(1.0f, 0.3f), q(0.13f, 0.3f),
                        q(0.13f, 0.72f), q(0.42f, 0.88f), q(0.42f, 0.98f), q(0, 0.95f), q(-0.42f, 0.98f), q(-0.42f, 0.88f),
                        q(-0.13f, 0.72f), q(-0.13f, 0.3f), q(-1.0f, 0.3f), q(-1.0f, 0.18f), q(-0.13f, -0.3f), q(-0.13f, -0.8f)};
    dl->AddPolyline(c, 18, cor, ImDrawFlags_Closed, esp);
}

ImU32 corAltitude(int ft)
{
    struct P { float ft; ImU32 c; };
    static const P k[] = {{0, IM_COL32(0xff, 0x8c, 0x28, 255)},     {5000, IM_COL32(0xff, 0xdc, 0x28, 255)},
                          {15000, IM_COL32(0x78, 0xff, 0x50, 255)},  {25000, IM_COL32(0x28, 0xe6, 0xe6, 255)},
                          {35000, IM_COL32(0x5a, 0x96, 0xff, 255)},  {45000, IM_COL32(0xe6, 0x6e, 0xff, 255)}};
    if (ft <= 0) return k[0].c;
    for (int i = 1; i < 6; ++i)
        if (ft < k[i].ft) return misturar(k[i - 1].c, k[i].c, (ft - k[i - 1].ft) / (k[i].ft - k[i - 1].ft));
    return k[5].c;
}

struct EventoRadar { std::string hora, texto; ImU32 cor; };

std::string horaAgora(bool utc)
{
    SYSTEMTIME st;
    if (utc) GetSystemTime(&st); else GetLocalTime(&st);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return b;
}

void abrirUrl(const std::string& url) { ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::string pastaPrograma()
{
    char p[MAX_PATH]{};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string d(p);
    const auto pos = d.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : d.substr(0, pos);
}

std::string haQuanto(double s)
{
    char b[32];
    if (s < 120) std::snprintf(b, sizeof b, "%.0f s", std::max(0.0, s));
    else if (s < 7200) std::snprintf(b, sizeof b, "%.0f min", s / 60);
    else std::snprintf(b, sizeof b, "%.1f h", s / 3600);
    return b;
}

// torre de antena (estacao de terra): p = o meio da torre
void torre(ImDrawList* dl, ImVec2 p, float s, ImU32 c)
{
    const float h = 16 * s, w = 6.5f * s;
    const ImVec2 topo = p + ImVec2(0, -h * 0.55f), pe1 = p + ImVec2(-w, h * 0.45f), pe2 = p + ImVec2(w, h * 0.45f);
    const ImVec2 m1 = p + ImVec2(-w * 0.52f, h * 0.02f), m2 = p + ImVec2(w * 0.52f, h * 0.02f);
    dl->AddLine(pe1, topo, c, 1.8f * s);
    dl->AddLine(pe2, topo, c, 1.8f * s);
    dl->AddLine(m1, m2, c, 1.3f * s);
    dl->AddLine(pe1, m2, c, 1.f * s);
    dl->AddLine(pe2, m1, c, 1.f * s);
    dl->AddCircleFilled(topo, 2.6f * s, c, 10);
    for (float r : {6.f, 10.f}) {
        dl->PathArcTo(topo, r * s, -0.6f, 0.6f, 8);
        dl->PathStroke(c, 0, 1.3f * s);
        dl->PathArcTo(topo, r * s, (float)kPi - 0.6f, (float)kPi + 0.6f, 8);
        dl->PathStroke(c, 0, 1.3f * s);
    }
}

} // namespace

// ===========================================================================
//  Estado do radar
// ===========================================================================
struct Ui::RadarEstado {
    struct Alvo {
        std::string chave, fonte;                 // fonte: "ADS-B", "ACARS", "VDL2", "HFDL"
        std::string voo, prefixo, hex, tipo, empresa, classe, pais, operador;
        std::string vooMsg;                       // o voo como veio na mensagem (G31017), quando virou ICAO (GLO1017)
        bool vooCortado = false;                  // HFDL: so 6 caracteres do voo (AZU2482 -> AZU248)
        std::string vooNet;                       // o voo completo veio da internet (adsb.lol / ADS-B Exchange)
        int alt = AdsbCore::kSemValor, vs = AdsbCore::kSemValor, squawk = -1, msgs = 0, categoria = 0;
        double vel = -1, rumo = -1;
        bool noChao = false;
        bool net = false;                         // posicao pela INTERNET (adsb.lol): o radio ouviu, mas sem posicao
        std::string netFonte;                     // "adsb.lol" ou "ADS-B Exchange"
        double lat = 91, lon = 181;               // ultima posicao ouvida
        double idade = 1e9, tLido = 0;            // s sem mensagem quando foi lido / relogio da leitura
        bool visto = false;                       // estava na leitura mais recente
        bool vivo = false, jaPintado = false;
        double tPint = -1e9, pLat = 91, pLon = 181;      // a ultima "pintura" da agulha
        std::deque<std::pair<double, double>> rastro;    // pinturas anteriores (lat, lon)
        double eLat = 91, eLon = 181;             // posicao estimada agora
        double dist = 0, az = 0;                  // da estacao (estimada agora)
        int sqAnt = -1;
        double idadeAgora(double t) const { return idade + (t - tLido); }
        double memoria(int memAdsb) const { return net ? 300.0 : fonte == "ADS-B" ? (double)memAdsb : kMemoriaExt; }
        std::string nome() const { return !voo.empty() ? voo + (vooCortado ? "?" : "") : !prefixo.empty() ? prefixo : !hex.empty() ? hex : chave.substr(1); }
    };

    bool carregado = false;
    bool temCentro = false;
    double qLat = -22.9068, qLon = -43.1729;      // a estacao (centro configurado)
    std::string centroTxt;
    double vLat = -22.9068, vLon = -43.1729;      // o centro da vista (arrastar / RESET)
    bool seguir = false;
    float alcance = kAlcPadrao, periodo = 5, persist = 4;
    int memoria = 30;
    bool mapa = true, aeroportos = true, aneis = true, etiquetas = true, fixa = false, rastros = true, vetor = true;
    bool fora = true, cheia = true, paineis = true;
    int cor = 0, simbolo = 0;

    double ang = 0, ultQuadro = 0, ultLeitura = 0;
    std::map<std::string, Alvo> alvos;
    std::vector<AdsbCore::Aviao> semPos;          // ouvidos agora sem posicao (Modo S)
    ConsultaPrefixo consulta;                     // ACARS/VDL2/HFDL: prefixo -> codigo ICAO, modelo, operador
    ConsultaPosicao posNet;                       // posicao pela internet (adsb.lol) de quem foi ouvido sem posicao
    std::map<std::string, double> candDesde;      // chave da consulta -> desde quando esta sem posicao
    bool netPos = false;                          // consulta na internet: so se o usuario ligar
    char chaveX[128] = {};                        // chave do ADS-B Exchange (RapidAPI, assinatura paga do usuario)
    // estacoes de terra do HFDL: a tabela do dumphfdl (posicao de cada uma) e as que transmitiram
    struct TerraTab { int id = -1; double lat = 91, lon = 181; std::string nome, freqs; };
    std::vector<TerraTab> gsTab;
    bool gsLida = false;
    struct Terra {
        std::string nome, freqs;
        int id = -1, msgs = 0;
        double lat = 91, lon = 181, freqKHz = 0, snr = -99, visto = 0, pulso = -1e9, dist = 0, az = 0;
    };
    std::map<std::string, Terra> gs;              // chave "G" + nome
    std::string sel;
    std::deque<EventoRadar> eventos;
    double recKm = 0; std::string recNome;
    std::vector<float> histMsgs;                  // mensagens/s do ADSB, um ponto por segundo (2 min)
    double ultHist = 0;

    bool pedirCentro = false, fecharCentro = false;
    char entrada[64] = {0};
    std::string erroEntrada;
    bool arrastou = false;
    double arrLat = 0, arrLon = 0;
    ImVec2 arrMouse;
    double menuLat = 91, menuLon = 181;
};

namespace {
using Radar = Ui::RadarEstado;

void evento(Radar& R, const std::string& txt, ImU32 cor)
{
    R.eventos.push_front({horaAgora(false), txt, cor});
    while (R.eventos.size() > 300) R.eventos.pop_back();
}

void carregarCfg(Radar& R)
{
    auto& c = Config::instance();
    R.temCentro = !c.str("radar_lat").empty() && !c.str("radar_lon").empty();
    if (R.temCentro) { R.qLat = c.dbl("radar_lat", R.qLat); R.qLon = c.dbl("radar_lon", R.qLon); }
    R.centroTxt = c.str("radar_centro");
    R.alcance = kAlcPadrao;           // sempre abre em 450 km (o ultimo alcance nao fica guardado)
    R.periodo = std::clamp((float)c.dbl("radar_volta", 5), 2.f, 12.f);
    R.persist = std::clamp((float)c.dbl("radar_persist", 4), 1.f, 30.f);
    R.memoria = (int)std::clamp(c.num("radar_memoria", 30), 10LL, 180LL);
    R.mapa = c.flag("radar_mapa", true);
    R.aeroportos = c.flag("radar_aero", true);
    R.aneis = c.flag("radar_aneis", true);
    R.etiquetas = c.flag("radar_etiquetas", true);
    R.fixa = c.flag("radar_fixa", false);
    R.rastros = c.flag("radar_rastro", true);
    R.vetor = c.flag("radar_vetor", true);
    R.fora = c.flag("radar_fora", true);
    R.cheia = c.flag("radar_cheia", true);
    R.paineis = c.flag("radar_paineis", true);
    R.cor = (int)std::clamp(c.num("radar_cor", 0), 0LL, 1LL);
    R.simbolo = (int)std::clamp(c.num("radar_simbolo", 0), 0LL, 1LL);
    R.netPos = c.flag("radar_netpos", false);
    std::snprintf(R.chaveX, sizeof R.chaveX, "%s", c.str("radar_adsbx_chave").c_str());
    R.vLat = R.qLat; R.vLon = R.qLon;
    R.carregado = true;
}

void salvarCfg(const Radar& R)
{
    auto& c = Config::instance();
    if (R.temCentro) {
        char b[32];
        std::snprintf(b, sizeof b, "%.6f", R.qLat); c.set("radar_lat", std::string(b));
        std::snprintf(b, sizeof b, "%.6f", R.qLon); c.set("radar_lon", std::string(b));
        c.set("radar_centro", R.centroTxt);
    }
    c.set("radar_volta", (double)R.periodo);
    c.set("radar_persist", (double)R.persist);
    c.set("radar_memoria", R.memoria);
    c.set("radar_mapa", R.mapa);
    c.set("radar_aero", R.aeroportos);
    c.set("radar_aneis", R.aneis);
    c.set("radar_etiquetas", R.etiquetas);
    c.set("radar_fixa", R.fixa);
    c.set("radar_rastro", R.rastros);
    c.set("radar_vetor", R.vetor);
    c.set("radar_fora", R.fora);
    c.set("radar_cheia", R.cheia);
    c.set("radar_paineis", R.paineis);
    c.set("radar_cor", R.cor);
    c.set("radar_simbolo", R.simbolo);
    c.set("radar_netpos", R.netPos);
    c.set("radar_adsbx_chave", std::string(R.chaveX));
}

bool definirCentro(Radar& R, const std::string& txt, std::string& erro)
{
    double la = 0, lo = 0;
    if (!lerCentro(txt, la, lo, erro)) return false;
    R.qLat = la; R.qLon = lo; R.temCentro = true; R.centroTxt = aparar(txt);
    R.vLat = la; R.vLon = lo; R.seguir = false;
    R.recKm = 0; R.recNome.clear();
    for (auto& kv : R.alvos) kv.second.rastro.clear();
    char b[160];
    std::snprintf(b, sizeof b, T("Centro do radar: %.5f, %.5f (%s)"), la, lo, grausParaLocator(la, lo).c_str());
    evento(R, b, C_ESCALA);
    salvarCfg(R);
    Config::instance().salvar();
    return true;
}

// Le as listas dos decodificadores (4 vezes por segundo). O ADSB manda a
// posicao varias vezes por segundo; ACARS/VDL2/HFDL so de vez em quando.
void lerAlvos(Radar& R, Decoders& d, double t, bool internet)
{
    for (auto& kv : R.alvos) kv.second.visto = false;
    R.semPos.clear();
    for (const auto& a : d.adsb().avioes()) {
        if (a.lat > 90) {
            if (a.idade <= R.memoria) R.semPos.push_back(a);
            continue;
        }
        Radar::Alvo& v = R.alvos["A" + a.hex];
        if (v.chave.empty()) v.chave = "A" + a.hex;
        v.fonte = "ADS-B"; v.voo = a.voo; v.prefixo = a.prefixo; v.hex = a.hex; v.tipo = a.tipo;
        v.empresa = a.empresa; v.classe = a.classe; v.pais = a.pais; v.operador = a.operador; v.categoria = a.categoria;
        v.alt = a.altitude; v.vs = a.vertical; v.squawk = a.squawk; v.msgs = a.msgs;
        v.vel = a.velocidade; v.rumo = a.rumo; v.noChao = a.noChao;
        v.lat = a.lat; v.lon = a.lon; v.idade = a.idade; v.tLido = t; v.visto = true;
    }
    auto deFora = [&](const std::vector<AeronaveHfdl>& lst, const char* fonte) {
        for (const auto& a : lst) {
            if (a.lat > 90) continue;
            // a matricula vem na mensagem; o endereco ICAO, o modelo e o operador vem
            // do hexdb.io pelo prefixo (consultado uma vez e guardado)
            ConsultaPrefixo::Info cad;
            const bool temCad = !a.prefixo.empty() && R.consulta.info(a.prefixo, cad);
            if (!temCad && internet && !a.prefixo.empty()) R.consulta.pedir(a.prefixo);
            const std::string hex = !a.hex.empty() ? a.hex : cad.hex;
            if (!hex.empty()) {
                const auto it = R.alvos.find("A" + hex);
                if (it != R.alvos.end() && it->second.visto) continue;   // o ADS-B ja mostra este
            }
            const std::string id = !a.hex.empty() ? a.hex : !a.voo.empty() ? a.voo : a.prefixo;
            if (id.empty()) continue;
            Radar::Alvo& v = R.alvos["H" + id];
            if (v.chave.empty()) v.chave = "H" + id;
            // o voo vem com o codigo IATA da companhia (G31017): vira o ICAO (GLO1017)
            const std::string vi = vooParaIcao(a.voo, a.prefixo);
            v.fonte = fonte; v.voo = vi.empty() ? a.voo : vi; v.vooMsg = vi.empty() ? std::string() : a.voo;
            v.prefixo = a.prefixo; v.hex = hex; v.msgs = a.msgs; v.alt = a.altPes;
            v.tipo = cad.tipo; v.operador = cad.operador;
            v.vooCortado = a.vooCortado; v.vooNet.clear();
            // HFDL com o voo cortado (AZU248 de AZU2482): procura na internet o aviao perto desta
            // posicao cujo voo comeca assim - dali vem o voo completo (e o ICAO, se faltar)
            if (a.vooCortado && R.netPos && !v.voo.empty()) {
                char k[96];
                std::snprintf(k, sizeof k, "pt:%.2f,%.2f,%s", a.lat, a.lon, v.voo.c_str());
                R.posNet.chaveAdsbx(R.chaveX);
                R.posNet.pedir(k);
                ConsultaPosicao::Pos p;
                if (R.posNet.pegar(k, p) && p.voo.size() >= v.voo.size() && p.voo.compare(0, v.voo.size(), v.voo) == 0) {
                    // achou: o voo completo (AZU2482) - ou o mesmo voo, que entao ja estava completo (AZU248)
                    if (v.vooMsg.empty() && p.voo != v.voo) v.vooMsg = v.voo;
                    v.voo = p.voo; v.vooCortado = false; v.vooNet = p.fonte;
                    if (v.hex.empty()) v.hex = p.hex;
                    if (v.prefixo.empty()) v.prefixo = p.prefixo;
                    if (v.tipo.empty()) v.tipo = p.tipo;
                }
            }
            const uint32_t i24 = v.hex.size() == 6 ? (uint32_t)std::strtoul(v.hex.c_str(), nullptr, 16) : 0;
            const ClasseAviao cl = classificarAviao(v.voo, v.prefixo, i24, cad.operadorIcao, cad.operador, 0);
            v.empresa = cl.empresa; v.classe = cl.classe;
            v.pais = i24 ? AdsbCore::paisDoIcao(i24) : "";
            v.lat = a.lat; v.lon = a.lon; v.tLido = t; v.visto = true;
            v.idade = std::max(0.0, t - (a.posVisto > 0 ? a.posVisto : a.visto));   // a idade da POSICAO
        }
    };
    deFora(d.acars().aeronaves(), "ACARS");
    deFora(d.vdl2().aeronaves(), "VDL2");
    deFora(d.hfdl().aeronaves(), "HFDL");
    // os que sairam das listas (botao Limpar dos decodificadores) saem do radar
    for (auto it = R.alvos.begin(); it != R.alvos.end();) {
        if (!it->second.visto && it->first[0] != 'N') {   // ('N' = posicao da internet: o lerInternet cuida)
            if (R.sel == it->first) R.sel.clear();
            it = R.alvos.erase(it);
        } else ++it;
    }
}

// a tabela de estacoes do dumphfdl (decoders\\hfdl_systable.conf): numero, posicao, frequencias, nome
void lerTabelaGs(Radar& R)
{
    R.gsLida = true;
    std::ifstream f(pastaPrograma() + "\\decoders\\hfdl_systable.conf");
    Radar::TerraTab cur;
    auto fecha = [&] { if (cur.id >= 0 && !cur.nome.empty() && cur.lat <= 90) R.gsTab.push_back(cur); cur = Radar::TerraTab(); };
    auto valor = [](const std::string& l) { const auto e = l.find('='); return e == std::string::npos ? std::string() : l.substr(e + 1); };
    for (std::string l; std::getline(f, l);) {
        size_t i = 0;
        while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) ++i;
        const std::string x = l.substr(i);
        if (x.rfind("id", 0) == 0 && x.find('=') != std::string::npos) { fecha(); cur.id = std::atoi(valor(x).c_str()); }
        else if (x.rfind("lat", 0) == 0) cur.lat = std::atof(valor(x).c_str());
        else if (x.rfind("lon", 0) == 0) cur.lon = std::atof(valor(x).c_str());
        else if (x.rfind("frequencies", 0) == 0) {
            const auto a = x.find('('), b = x.find(')');
            if (a != std::string::npos && b != std::string::npos && b > a) cur.freqs = aparar(x.substr(a + 1, b - a - 1));
        } else if (x.rfind("name", 0) == 0) {
            const auto a = x.find('"'), b = x.rfind('"');
            if (a != std::string::npos && b > a) cur.nome = x.substr(a + 1, b - a - 1);
        }
    }
    fecha();
}

void lerEstacoes(Radar& R, Decoders& d, double t)
{
    if (!R.gsLida) lerTabelaGs(R);
    const auto lista = d.hfdl().estacoes();
    if (lista.empty()) {                       // Limpar do HFDL (ou ainda nada)
        if (!R.sel.empty() && R.sel[0] == 'G') R.sel.clear();
        R.gs.clear();
        return;
    }
    for (const auto& e : lista) {
        Radar::Terra& g = R.gs["G" + e.nome];
        const bool nova = g.nome.empty();
        if (nova) {
            g.nome = e.nome;
            for (const auto& x : R.gsTab)
                if (x.nome == e.nome) { g.id = x.id; g.lat = x.lat; g.lon = x.lon; g.freqs = x.freqs; }
        }
        if (e.msgs != g.msgs) { g.pulso = t; g.msgs = e.msgs; }     // transmissao nova: as ondas
        g.freqKHz = e.freqKHz; g.snr = e.snrDb; g.visto = e.visto;
        if (g.lat <= 90) { g.dist = distKm(R.qLat, R.qLon, g.lat, g.lon); g.az = azimute(R.qLat, R.qLon, g.lat, g.lon); }
        if (nova) {
            char b[200];
            std::snprintf(b, sizeof b, T("Estação de terra HFDL: %s (%.1f kHz) a %.0f km, %03.0f°"), g.nome.c_str(), g.freqKHz, g.dist, g.az);
            evento(R, b, C_TERRA);
        }
    }
}

// Posicao pela INTERNET (adsb.lol) de quem o radio OUVIU sem posicao: ACARS/VDL2/HFDL sem
// relatorio de posicao e Modo S sem ADS-B. Vira um alvo "N..." marcado (contorno, cor, [NET]).
void lerInternet(Radar& R, Decoders& d, double t)
{
    for (auto& kv : R.alvos) if (kv.first[0] == 'N') kv.second.visto = false;
    if (R.netPos) {
        R.posNet.chaveAdsbx(R.chaveX);
        struct Cand { std::string chave, fonte, voo, prefixo, hex; int msgs; double espera = 0; };
        std::vector<Cand> cands;
        // ADS-B: so depois de 30 s sem posicao (no comeco todo aviao fica uns segundos so com Modo S)
        for (const auto& a : R.semPos) cands.push_back({"hex:" + a.hex, "ADS-B", a.voo, a.prefixo, a.hex, a.msgs, 30});
        auto deFora = [&](const std::vector<AeronaveHfdl>& lst, const char* fonte) {
            for (const auto& a : lst) {
                if (a.lat <= 90 && t - a.posVisto < kMemoriaExt) continue;   // o radio tem a posicao
                if (t - a.visto > kMemoriaExt) continue;                     // nao e ouvido ha 15 min
                std::string hex = a.hex;
                ConsultaPrefixo::Info cad;
                if (hex.empty() && !a.prefixo.empty() && R.consulta.info(a.prefixo, cad)) hex = cad.hex;
                const std::string vi = vooParaIcao(a.voo, a.prefixo);
                const std::string cs = !vi.empty() ? vi : a.voo;
                const std::string ch = !hex.empty() ? "hex:" + hex : !a.prefixo.empty() ? "reg:" + a.prefixo : !cs.empty() ? "cs:" + cs : "";
                if (!ch.empty()) cands.push_back({ch, fonte, cs, a.prefixo, hex, a.msgs});
            }
        };
        deFora(d.acars().aeronaves(), "ACARS");
        deFora(d.vdl2().aeronaves(), "VDL2");
        deFora(d.hfdl().aeronaves(), "HFDL");
        std::map<std::string, double> desde;
        for (const auto& c : cands) {
            const auto ia = R.candDesde.find(c.chave);
            const double d0 = desde.emplace(c.chave, ia != R.candDesde.end() ? ia->second : t).first->second;
            if (t - d0 < c.espera) continue;
            R.posNet.pedir(c.chave);
            ConsultaPosicao::Pos p;
            if (!R.posNet.pegar(c.chave, p) || !p.achou || t - p.quando > 300) continue;
            const std::string hex = !p.hex.empty() ? p.hex : c.hex;
            if (!hex.empty()) {                      // o radio ja mostra este? entao nao repete
                const auto ia = R.alvos.find("A" + hex);
                if (ia != R.alvos.end() && ia->second.visto) continue;
                bool dup = false;
                for (const auto& kv : R.alvos) if (kv.first[0] == 'H' && kv.second.visto && kv.second.hex == hex) dup = true;
                if (dup) continue;
            }
            Radar::Alvo& v = R.alvos["N" + c.chave];
            if (v.chave.empty()) v.chave = "N" + c.chave;
            v.net = true; v.fonte = c.fonte; v.msgs = c.msgs; v.netFonte = p.fonte;
            v.voo = !p.voo.empty() ? p.voo : c.voo;
            v.prefixo = !c.prefixo.empty() ? c.prefixo : p.prefixo;
            v.hex = hex; v.tipo = p.tipo;
            v.alt = p.alt; v.vs = p.vs; v.vel = p.gs; v.rumo = p.trk; v.noChao = p.noChao; v.squawk = -1;
            const uint32_t i24 = hex.size() == 6 ? (uint32_t)std::strtoul(hex.c_str(), nullptr, 16) : 0;
            const ClasseAviao cl = classificarAviao(v.voo, v.prefixo, i24, "", "", 0);
            v.empresa = cl.empresa; v.classe = cl.classe;
            v.pais = i24 ? AdsbCore::paisDoIcao(i24) : "";
            v.lat = p.lat; v.lon = p.lon; v.tLido = t; v.idade = std::max(0.0, t - p.quando); v.visto = true;
        }
        R.candDesde.swap(desde);
    } else R.candDesde.clear();
    for (auto it = R.alvos.begin(); it != R.alvos.end();) {
        if (it->first[0] == 'N' && !it->second.visto) {
            if (R.sel == it->first) R.sel.clear();
            it = R.alvos.erase(it);
        } else ++it;
    }
}

std::string textoAlt(const Radar::Alvo& v)
{
    char b[32];
    if (v.noChao) return T("SOLO");
    if (v.alt == AdsbCore::kSemValor) return "-";
    if (v.alt >= 10000) std::snprintf(b, sizeof b, "FL%03d", (v.alt + 50) / 100);
    else std::snprintf(b, sizeof b, "%d ft", v.alt);
    return b;
}

bool emergencia(int sq) { return sq == 7500 || sq == 7600 || sq == 7700; }

const char* nomeEmergencia(int sq)
{
    return sq == 7500 ? T("SEQUESTRO") : sq == 7600 ? T("FALHA DE RÁDIO") : sq == 7700 ? T("EMERGÊNCIA") : "";
}

ImU32 corClasse(const Radar::Alvo& v)
{
    if (v.net) return C_NET;                                                  // posicao da internet
    if (v.fonte != "ADS-B") return IM_COL32(0xd8, 0x90, 0xff, 255);         // posicao do ACARS / VDL2 / HFDL
    if (v.classe == "militar") return IM_COL32(0xff, 0x9e, 0x40, 255);
    if (v.classe == "governo") return IM_COL32(0x74, 0xcc, 0xff, 255);
    if (v.classe == "particular") return IM_COL32(0xc8, 0xc8, 0xc8, 255);
    if (v.classe == "carga") return IM_COL32(0xf2, 0xd8, 0x70, 255);
    if (v.classe == "executiva" || v.classe == "taxi") return IM_COL32(0xc0, 0xf2, 0xa6, 255);
    return C_FOSFORO;                                                           // linha aerea / sem cadastro
}

const char* nomeFonte(int tipo)
{
    switch (tipo) {
    case Decoders::ADSB:  return "ADS-B 1090 MHz";
    case Decoders::ACARS: return "ACARS 131,550 / 131,825 MHz";
    case Decoders::VDL2:  return "VDL2 136,975 MHz";
    case Decoders::HFDL:  return "HFDL (HF)";
    default:              return "-";
    }
}

} // namespace

// ===========================================================================
//  Ui: abrir / fechar / pedidos
// ===========================================================================
static bool ehAviacao(int t)
{
    return t == Decoders::ADSB || t == Decoders::ACARS || t == Decoders::VDL2 || t == Decoders::HFDL;
}

void Ui::abrirRadar()
{
    if (!radar_) radar_ = std::make_shared<RadarEstado>();
    Radar& R = *radar_;
    if (!R.carregado) carregarCfg(R);
    radarAberto_ = true;
    R.vLat = R.qLat; R.vLon = R.qLon; R.seguir = false;
    R.alcance = kAlcPadrao;
    R.ultQuadro = 0; R.ultLeitura = 0;
    if (!R.temCentro) {
        // primeira vez: pergunta o centro (sugere o QTH dos satelites, se houver)
        R.pedirCentro = true;
        const std::string loc = Config::instance().str("sat_loc");
        std::snprintf(R.entrada, sizeof R.entrada, "%s", Config::instance().str("sat_lat").empty() ? "" : loc.c_str());
    }
    g_telaCheia = R.cheia ? 1 : 0;
    // o radar precisa de um decodificador de aviacao: sem nenhum, liga o ADS-B
    if (!(decRodando_ && ehAviacao(decTipo_))) {
        if (!r_.ligado()) ligarDesligar();
        decAberta_ = true;
        escolherDecoder(Decoders::ADSB, true);
        evento(R, T("ADS-B (1090 MHz) ligado para o radar"), C_ESCALA);
    } else {
        char b[96];
        std::snprintf(b, sizeof b, T("Radar aberto com o %s"), nomeFonte(decTipo_));
        evento(R, b, C_ESCALA);
    }
}

void Ui::fecharRadar(bool pararDecoder)
{
    radarAberto_ = false;
    g_telaCheia = 0;
    salvarRadar();
    Config::instance().salvar();
    if (pararDecoder) {
        decAberta_ = false;
        escolherDecoder(Decoders::NENHUM, false);    // o radio volta como estava
    }
}

void Ui::salvarRadar()
{
    if (radar_ && radar_->carregado) salvarCfg(*radar_);
}

// o radar cobre a tela inteira: a janela dos decodificadores nao e desenhada,
// mas o texto deles continua chegando (fica na caixa e no painel do radar)
void Ui::radarDrenarTexto()
{
    const std::string novo = r_.decoders().pegarTexto();
    if (!novo.empty()) anexarDecTexto(novo);
}

// botao nas janelas do ADSB, ACARS, VDL2 e HFDL
void Ui::botaoRadar()
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.36f, 0.24f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.50f, 0.33f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.62f, 0.40f, 1));
    if (ImGui::Button(T("RADAR AÉREO"))) abrirRadar();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", T("Abre a tela de radar (tela cheia) com os aviões que este decodificador está ouvindo:\n"
                                  "agulha girando, posição, altitude, velocidade e rumo de cada um."));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("tela de radar com os aviões ouvidos"));
}

// teste.pedido: "radar", "radar_voltar", "radar_fechar", "radar_centro GG87jb",
// "radar_alcance 150", "radar_sel GLO1234", "radar_cheia 0/1"
void Ui::radarPedido(const std::string& l)
{
    if (l == "radar") { abrirRadar(); return; }
    if (!radar_) return;
    Radar& R = *radar_;
    if (l == "radar_voltar") fecharRadar(false);
    else if (l == "radar_fechar") fecharRadar(true);
    else if (l.rfind("radar_centro ", 0) == 0) {
        std::string erro;
        if (definirCentro(R, l.substr(13), erro)) { R.pedirCentro = false; R.fecharCentro = true; }
    } else if (l.rfind("radar_alcance ", 0) == 0) R.alcance = std::clamp((float)std::atof(l.c_str() + 14), kAlcMin, kAlcMax);
    else if (l.rfind("radar_sel ", 0) == 0) {
        const std::string n = l.substr(10);
        for (auto& kv : R.alvos) if (kv.second.nome() == n || kv.second.hex == n) R.sel = kv.first;
        if (R.gs.count("G" + n)) R.sel = "G" + n;     // estacao de terra do HFDL (pelo nome)
    } else if (l.rfind("radar_cheia ", 0) == 0) { R.cheia = l.substr(12) == "1"; if (radarAberto_) g_telaCheia = R.cheia ? 1 : 0; }
}

// ===========================================================================
//  A tela do radar (desenhada dentro da janela principal, no lugar do painel)
// ===========================================================================
void Ui::telaRadar()
{
    if (!radar_) { radarAberto_ = false; return; }
    Radar& R = *radar_;
    auto& d = r_.decoders();
    ImGuiIO& io = ImGui::GetIO();
    const float s = s_;
    const double t = relogio();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // fonte nitida do tamanho pedido (as do painel existem em 10 tamanhos)
    auto fonte = [&](float px) -> ImFont* {
        static const float tp[10] = {8, 9, 10, 11.5f, 13, 15, 18, 21, 25, 30};
        for (int i = 0; i < 10; ++i)
            if (tp[i] * s >= px * 0.97f && f_.painelT[i]) return f_.painelT[i];
        return f_.painelT[9] ? f_.painelT[9] : f_.normal;
    };
    const bool en = emIngles();
    int acao = 0;              // 1 = voltar, 2 = fechar (para o decodificador)
    bool ligarAdsb = false;

    // ---------------- tempo, leitura dos decodificadores e a agulha ----------------
    double dt = R.ultQuadro > 0 ? t - R.ultQuadro : 0.0;
    R.ultQuadro = t;
    dt = std::clamp(dt, 0.0, 0.5);
    if (t - R.ultLeitura >= 0.25) { lerAlvos(R, d, t, decAj_.adsbInternet); lerEstacoes(R, d, t); lerInternet(R, d, t); R.ultLeitura = t; }
    const bool adsbRodando = decRodando_ && decTipo_ == Decoders::ADSB;
    if (t - R.ultHist >= 1.0) {
        R.ultHist = t;
        R.histMsgs.push_back(adsbRodando ? (float)d.adsb().estado().msgsPorS : 0.f);
        if (R.histMsgs.size() > 120) R.histMsgs.erase(R.histMsgs.begin());
    }
    const double a0 = R.ang, varrido = dt / R.periodo * 360.0;
    R.ang = std::fmod(R.ang + varrido, 360.0);
    int nVivos = 0;
    for (auto& kv : R.alvos) {
        auto& v = kv.second;
        const bool vivo = v.idadeAgora(t) <= v.memoria(R.memoria);
        if (v.vivo && !vivo && v.jaPintado) {
            char b[160];
            std::snprintf(b, sizeof b, T("Perdido: %s (último a %.0f km, %03.0f°)"), v.nome().c_str(), v.dist, v.az);
            evento(R, b, C_APAGADO);
        }
        v.vivo = vivo;
        if (!vivo) continue;
        ++nVivos;
        // posicao agora: a ultima ouvida + o que andou desde entao (no maximo 20 s)
        double la = v.lat, lo = v.lon;
        if ((v.fonte == "ADS-B" || v.net) && v.vel > 0 && v.rumo >= 0 && !v.noChao)
            destino(la, lo, v.rumo, v.vel * kNm * std::min(v.idadeAgora(t), v.net ? 120.0 : 20.0) / 3600.0, la, lo);
        v.eLat = la; v.eLon = lo;
        v.dist = distKm(R.qLat, R.qLon, la, lo);
        v.az = azimute(R.qLat, R.qLon, la, lo);
        // a agulha passou por ele neste quadro? Entao acende (pinta) aqui
        const double e = std::fmod(v.az - a0 + 720.0, 360.0);
        if (varrido > 0 && e > 0 && e <= varrido) {
            if (v.jaPintado && v.pLat <= 90) {
                v.rastro.push_front({v.pLat, v.pLon});
                while (v.rastro.size() > 12) v.rastro.pop_back();
            }
            v.pLat = la; v.pLon = lo; v.tPint = t;
            if (!v.jaPintado) {
                v.jaPintado = true;
                char b[200];
                std::snprintf(b, sizeof b, T("Novo: %s  %.0f km  %03.0f°  %s  (%s)"), v.nome().c_str(), v.dist, v.az,
                              textoAlt(v).c_str(), (v.net ? v.fonte + T(" + posição da internet") : v.fonte).c_str());
                evento(R, b, IM_COL32(0x70, 0xff, 0xa8, 255));
            }
            if (!v.net && v.dist > R.recKm && v.dist < 20100) { R.recKm = v.dist; R.recNome = v.nome(); }
        }
        if (emergencia(v.squawk) && v.squawk != v.sqAnt) {
            char b[160];
            std::snprintf(b, sizeof b, T("SQUAWK %04d (%s): %s a %.0f km, %03.0f°"), v.squawk, nomeEmergencia(v.squawk),
                          v.nome().c_str(), v.dist, v.az);
            evento(R, b, C_VERM);
        }
        v.sqAnt = v.squawk;
    }
    if (R.seguir) {
        const auto it = R.alvos.find(R.sel);
        if (it != R.alvos.end() && it->second.vivo) { R.vLat = it->second.eLat; R.vLon = it->second.eLon; }
        else R.seguir = false;
    }

    // ---------------- disposicao ----------------
    const float hb = 46 * s;
    const bool lados = R.paineis && W > 900 * s;
    const float wl = lados ? std::clamp(W * 0.19f, 250 * s, 320 * s) : 0.f;
    const float wr = lados ? std::clamp(W * 0.25f, 290 * s, 430 * s) : 0.f;
    const ImVec2 caixa0(wl, hb), caixa1(W - wr, H);
    const float boxW = caixa1.x - caixa0.x, boxH = caixa1.y - caixa0.y;
    const float aro = 42 * s;                                   // a escala de graus em volta
    const float Rr = std::max(80 * s, std::min(boxW, boxH) * 0.5f - aro - 4 * s);
    const ImVec2 C(caixa0.x + boxW * 0.5f, caixa0.y + boxH * 0.5f);
    Proj P;
    P.ajustar(R.vLat, R.vLon, C, Rr, R.alcance);
    const ImVec2 E = P.xy(R.qLat, R.qLon);                      // a estacao na tela
    const bool estDentro = (E.x - C.x) * (E.x - C.x) + (E.y - C.y) * (E.y - C.y) < Rr * Rr;
    auto dentro = [&](ImVec2 p, float folga = 0) { return (p.x - C.x) * (p.x - C.x) + (p.y - C.y) * (p.y - C.y) <= (Rr - folga) * (Rr - folga); };

    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0x03, 0x07, 0x05, 255));

    // ---------------- aro, vidro ----------------
    dl->AddCircleFilled(C, Rr + aro, IM_COL32(0x05, 0x13, 0x0e, 255), 180);
    dl->AddCircle(C, Rr + aro, C_ESCALA2, 180, 1.5f * s);
    dl->AddCircle(C, Rr + aro - 3 * s, IM_COL32(0x1a, 0x50, 0x44, 255), 180, 1.f * s);
    discoDegrade(dl, C, Rr, IM_COL32(0x07, 0x2c, 0x1b, 255), IM_COL32(0x01, 0x09, 0x05, 255));

    // ---------------- mapa ----------------
    const double janLat = R.alcance / 111.0 * 1.6 + 0.6;
    const double janLon = std::min(180.0, janLat / std::max(0.05, std::cos(R.vLat * kRad)));
    auto perto = [&](double la, double lo) { return std::fabs(la - R.vLat) < janLat && std::fabs(difLon(lo, R.vLon)) < janLon; };
    auto linhas = [&](const short* pts, const int* ini, int n, ImU32 cor, float esp) {
        for (int l = 0; l < n; ++l) {
            bool temAnt = false; ImVec2 ant; double loAnt = 0;
            for (int i = ini[l]; i < ini[l + 1]; ++i) {
                const double lo = pts[2 * i] * 0.01, la = pts[2 * i + 1] * 0.01;
                if (!perto(la, lo)) { temAnt = false; continue; }
                const ImVec2 p = P.xy(la, lo);
                ImVec2 a, b;
                // (perto do outro lado do planeta a projecao estica os segmentos: esses ficam de fora)
                if (temAnt && std::fabs(lo - loAnt) < 180 &&
                    (p.x - ant.x) * (p.x - ant.x) + (p.y - ant.y) * (p.y - ant.y) < Rr * Rr * 0.09f && recortar(ant, p, C, Rr, a, b))
                    dl->AddLine(a, b, cor, esp);
                ant = p; loAnt = lo; temAnt = true;
            }
        }
    };
    if (R.mapa) {
        if (R.alcance <= 2500) {         // de perto: o 1:50m, com as divisas dos estados
            linhas(kEstados50_pts, kEstados50_ini, kEstados50_n, C_ESTADO, 1.f * s);
            linhas(kFronteira50_pts, kFronteira50_ini, kFronteira50_n, C_FRONT, 1.2f * s);
            linhas(kCosta50_pts, kCosta50_ini, kCosta50_n, C_COSTA, 1.4f * s);
        } else {                         // continente / planeta: o 1:110m basta (e e bem mais leve)
            linhas(kFronteira_pts, kFronteira_ini, kFronteira_n, C_FRONT, 1.f * s);
            linhas(kCosta_pts, kCosta_ini, kCosta_n, C_COSTA, 1.3f * s);
        }
    }
    // aeroportos (OurAirports): os grandes sempre com o codigo; os medios so de perto
    std::vector<std::pair<int, ImVec2>> aeroTela;
    if (R.aeroportos && R.alcance <= 6000) {
        ImFont* fa = fonte(10.5f * s);
        for (int i = 0; i < kNAeroportos; ++i) {
            const AeroportoInfo& a = kAeroportos[i];
            if (!a.grande && R.alcance > 1500) continue;     // de longe so os grandes
            if (!perto(a.lat, a.lon)) continue;
            const ImVec2 p = P.xy(a.lat, a.lon);
            if (!dentro(p, 4 * s)) continue;
            aeroTela.push_back({i, p});
            if (a.grande) {
                dl->AddRect(p - ImVec2(3.5f * s, 3.5f * s), p + ImVec2(3.5f * s, 3.5f * s), C_AERO, 0, 0, 1.3f * s);
                dl->AddLine(p - ImVec2(5.5f * s, 0), p + ImVec2(5.5f * s, 0), C_AERO, 1.f * s);
            } else {
                dl->AddCircle(p, 2.6f * s, comAlfa(C_AERO, 0.8f), 10, 1.f * s);
            }
            if (a.grande ? R.alcance <= 1500 : R.alcance <= 200)
                dl->AddText(fa, 10.5f * s, p + ImVec2(5 * s, 1 * s), comAlfa(C_AERO, a.grande ? 0.95f : 0.7f), a.id);
        }
    }

    // ---------------- aneis de distancia e raios (em volta da estacao) ----------------
    double passo = 5000;
    for (double cand : {2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 2500.0, 5000.0})
        if (R.alcance / cand <= 6.0) { passo = cand; break; }
    if (R.aneis) {
        ImFont* fr = fonte(11 * s);
        const double maxKm = R.alcance + distKm(R.vLat, R.vLon, R.qLat, R.qLon);
        for (int k = 1; k <= 60 && k * passo <= maxKm; ++k) {
            const float rp = (float)(k * passo / R.alcance) * Rr;
            const int N = 160;
            for (int i = 0; i < N; ++i) {
                const float a1 = i * 2 * (float)kPi / N, a2 = (i + 1) * 2 * (float)kPi / N;
                ImVec2 a, b;
                if (recortar(E + ImVec2(std::sin(a1), -std::cos(a1)) * rp, E + ImVec2(std::sin(a2), -std::cos(a2)) * rp, C, Rr, a, b))
                    dl->AddLine(a, b, C_GRADE, 1.f * s);
            }
            const ImVec2 pr = E + ImVec2(std::sin(135 * (float)kRad), -std::cos(135 * (float)kRad)) * rp;
            if (dentro(pr, 10 * s)) {
                char b[24]; std::snprintf(b, sizeof b, "%.0f km", k * passo);
                textoSombra(dl, fr, 11 * s, pr + ImVec2(3 * s, 1 * s), comAlfa(C_ESCALA, 0.75f), b);
            }
        }
        if (estDentro)
            for (int az = 0; az < 360; az += 30) {
                const float l = ateBorda(E, az, C, Rr);
                if (l > 0) dl->AddLine(E, E + ImVec2((float)std::sin(az * kRad), (float)-std::cos(az * kRad)) * l, C_RAIO, 1.f * s);
            }
    }

    // ---------------- a agulha (com o rastro de brilho atras dela) ----------------
    if (estDentro) {
        const int N = 60;
        const float larg = 55.f;                                  // graus de brilho atras da agulha
        auto u = [](double az) { return ImVec2((float)std::sin(az * kRad), (float)-std::cos(az * kRad)); };
        for (int k = 0; k < N; ++k) {
            const double az1 = R.ang - k * larg / N, az2 = R.ang - (k + 1) * larg / N;
            const float f1 = std::pow(1.f - (float)k / N, 2.4f) * 0.42f, f2 = std::pow(1.f - (float)(k + 1) / N, 2.4f) * 0.42f;
            const float l1 = ateBorda(E, az1, C, Rr), l2 = ateBorda(E, az2, C, Rr);
            if (l1 <= 0 || l2 <= 0) continue;
            triCor(dl, E, comAlfa(C_FOSFORO, f1 * 0.6f), E + u(az1) * l1, comAlfa(C_FOSFORO, f1), E + u(az2) * l2, comAlfa(C_FOSFORO, f2));
        }
        const float l = ateBorda(E, R.ang, C, Rr);
        if (l > 0) {
            dl->AddLine(E, E + u(R.ang) * l, comAlfa(C_FOSFORO, 0.25f), 6.f * s);
            dl->AddLine(E, E + u(R.ang) * l, IM_COL32(0xc8, 0xff, 0xe0, 255), 2.f * s);
        }
    }

    // ---------------- os alvos ----------------
    struct Etiqueta { std::vector<std::string> l; ImU32 cor; float a; ImVec2 p; bool sel, emerg; };
    std::vector<Etiqueta> etiquetas;
    std::vector<std::pair<std::string, ImVec2>> pontos;       // para o mouse
    int nTela = 0, nFora = 0;
    const bool pisca = std::fmod(t, 0.8) < 0.45;
    std::vector<Radar::Alvo*> ordem;
    for (auto& kv : R.alvos) ordem.push_back(&kv.second);
    std::stable_sort(ordem.begin(), ordem.end(), [&](const Radar::Alvo* x, const Radar::Alvo* y) {
        return (x->chave == R.sel) < (y->chave == R.sel);      // o escolhido por cima
    });
    ImFont* fEtq = fonte(12 * s);
    for (Radar::Alvo* pv : ordem) {
        Radar::Alvo& v = *pv;
        if (v.pLat > 90) continue;
        const bool sel = v.chave == R.sel;
        float aBlip = (float)(1.0 - (t - v.tPint) / R.persist);          // fosforo apagando
        if (sel && v.vivo) aBlip = std::max(aBlip, 0.55f);
        float aEtq = std::min(1.f, aBlip * 1.6f);
        if (R.fixa && v.vivo) aEtq = std::max(aEtq, 0.9f);
        if (aBlip <= 0.01f && aEtq <= 0.01f) continue;                    // apagou: some ate a agulha passar de novo
        const bool emerg = emergencia(v.squawk);
        ImU32 cor = R.cor == 1 && v.fonte == "ADS-B" && (v.alt != AdsbCore::kSemValor || v.noChao) ? corAltitude(v.noChao ? 0 : v.alt)
                                                                                                    : corClasse(v);
        if (emerg) cor = pisca ? C_VERM : IM_COL32(0xff, 0xd0, 0xd0, 255);
        const ImVec2 p = P.xy(v.pLat, v.pLon);
        if (!dentro(p)) {
            // fora do alcance: uma seta na borda, no rumo dele
            if (!R.fora || aBlip <= 0.01f || !v.vivo) continue;
            ++nFora;
            ImVec2 dir = p - C;
            const float n = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (n < 1) continue;
            dir = dir / n;
            const ImVec2 q = C + dir * (Rr - 5 * s), nn(-dir.y, dir.x);
            dl->AddTriangleFilled(q + dir * (4 * s), q - dir * (6 * s) + nn * (5 * s), q - dir * (6 * s) - nn * (5 * s), comAlfa(cor, aBlip * 0.9f));
            char b[64];
            std::snprintf(b, sizeof b, "%s %.0f", v.nome().c_str(), v.dist);
            const ImVec2 ts = fEtq->CalcTextSizeA(11 * s, FLT_MAX, 0, b);
            const ImVec2 qt = C + dir * (Rr - 18 * s) - ImVec2(ts.x * (0.5f - dir.x * 0.5f), ts.y * (0.5f - dir.y * 0.5f));
            textoSombra(dl, fEtq, 11 * s, qt, comAlfa(cor, aBlip * 0.85f), b);
            pontos.push_back({v.chave, q});
            continue;
        }
        if (v.vivo) ++nTela;
        // rastro: as posicoes das voltas anteriores
        if (R.rastros) {
            int i = 0;
            for (const auto& rp : v.rastro) {
                const ImVec2 q = P.xy(rp.first, rp.second);
                const float a = 0.55f * (1.f - i / 12.f) * (v.vivo ? 1.f : std::max(0.f, aBlip));
                if (dentro(q) && a > 0.01f) dl->AddCircleFilled(q, 1.9f * s, comAlfa(cor, a), 8);
                ++i;
            }
        }
        // vetor: onde vai estar daqui a 1 minuto
        if (R.vetor && v.vel > 0 && v.rumo >= 0 && !v.noChao && aBlip > 0.01f) {
            double la, lo;
            destino(v.pLat, v.pLon, v.rumo, v.vel * kNm / 60.0, la, lo);
            ImVec2 a, b;
            if (recortar(p, P.xy(la, lo), C, Rr, a, b)) dl->AddLine(a, b, comAlfa(cor, 0.75f * aBlip), 1.3f * s);
        }
        if (aBlip > 0.01f) {
            dl->AddCircleFilled(p, 11 * s, comAlfa(cor, 0.13f * aBlip), 20);           // o brilho do eco
            if (v.net) {
                // posicao da INTERNET: so o contorno (nao foi o seu receptor)
                if (v.rumo >= 0) aviaoContorno(dl, p, 9.f * s, v.rumo, comAlfa(cor, aBlip), 1.5f * s);
                else dl->AddCircle(p, 5 * s, comAlfa(cor, aBlip), 12, 1.6f * s);
            } else if (R.simbolo == 0 && v.rumo >= 0) aviao(dl, p, 8.5f * s, v.rumo, comAlfa(cor, aBlip));
            else if (v.fonte != "ADS-B") {
                const ImVec2 q[] = {p + ImVec2(0, -5 * s), p + ImVec2(5 * s, 0), p + ImVec2(0, 5 * s), p + ImVec2(-5 * s, 0)};
                dl->AddConvexPolyFilled(q, 4, comAlfa(cor, aBlip));
            } else {
                dl->AddCircleFilled(p, 3.8f * s, comAlfa(cor, aBlip), 12);
            }
            if (emerg) dl->AddCircle(p, 15 * s, comAlfa(C_VERM, pisca ? 1.f : 0.4f), 24, 2.f * s);
        }
        if (sel) {
            const float k = 13 * s, m = 5 * s;
            const ImU32 cs = IM_COL32(0xff, 0xff, 0xff, 230);
            for (int sx : {-1, 1})
                for (int sy : {-1, 1}) {
                    const ImVec2 q = p + ImVec2(sx * k, sy * k);
                    dl->AddLine(q, q - ImVec2(sx * m, 0), cs, 1.6f * s);
                    dl->AddLine(q, q - ImVec2(0, sy * m), cs, 1.6f * s);
                }
        }
        pontos.push_back({v.chave, p});
        if ((R.etiquetas || sel || emerg) && aEtq > 0.01f) {
            Etiqueta e;
            std::string l1 = v.nome();
            if (v.net) l1 += "  [NET]";
            else if (v.fonte != "ADS-B") l1 += "  [" + v.fonte + "]";
            e.l.push_back(l1);
            std::string l2 = textoAlt(v);
            if (v.vs != AdsbCore::kSemValor && std::abs(v.vs) >= 300) l2 += v.vs > 0 ? " \xE2\x96\xB2" : " \xE2\x96\xBC";
            if (v.vel >= 0) { char b[24]; std::snprintf(b, sizeof b, "  %.0f kt", v.vel); l2 += b; }
            if (v.fonte == "ADS-B" || v.net) e.l.push_back(l2);
            else {
                // ACARS / VDL2 / HFDL: a posicao veio numa mensagem e pode ter alguns minutos
                char b[64];
                const double ida = v.idadeAgora(t);
                if (ida < 120) std::snprintf(b, sizeof b, T("posição há %.0f s"), ida);
                else std::snprintf(b, sizeof b, T("posição há %.0f min"), ida / 60);
                std::string l = v.alt != AdsbCore::kSemValor ? textoAlt(v) + "  " : std::string();
                if (!v.prefixo.empty() && v.prefixo != v.nome()) l += v.prefixo + "  ";
                e.l.push_back(l + b);
            }
            std::string l3 = v.tipo;
            if (!v.empresa.empty()) l3 += (l3.empty() ? "" : "  ") + v.empresa;
            if (!l3.empty()) e.l.push_back(l3);
            if (v.net) {
                char b[96];
                std::snprintf(b, sizeof b, T("internet (%s) há %s \xC2\xB7 ouvido no %s"), v.netFonte.c_str(), haQuanto(v.idadeAgora(t)).c_str(), v.fonte.c_str());
                e.l.push_back(b);
            }
            if (emerg) { char b[48]; std::snprintf(b, sizeof b, "SQ %04d %s", v.squawk, nomeEmergencia(v.squawk)); e.l.push_back(b); }
            e.cor = cor; e.a = aEtq; e.p = p; e.sel = sel; e.emerg = emerg;
            etiquetas.push_back(std::move(e));
        }
    }
    // ---------------- estacoes de terra do HFDL (quem transmitiu: "Src GS") ----------------
    // Ficam acesas enquanto estao sendo ouvidas (a estacao nao anda): a cada transmissao
    // recebida saem ondas da torre; 30 min sem ouvir, somem. Fora do alcance: na borda.
    for (auto& kv : R.gs) {
        const Radar::Terra& g = kv.second;
        if (g.lat > 90) continue;
        const double idade = t - g.visto;
        if (idade > 1800) continue;
        const float a = idade < 600 ? 1.f : (float)std::max(0.35, 1.0 - (idade - 600) / 1200.0);
        const bool sel = R.sel == kv.first;
        ImVec2 p = P.xy(g.lat, g.lon);
        if (!dentro(p, 8 * s)) {
            ImVec2 dir = p - C;
            const float n = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (n < 1) continue;
            dir = dir / n;
            p = C + dir * (Rr - 16 * s);
        }
        const float fp = (float)(t - g.pulso);
        if (fp >= 0 && fp < 4.f)
            for (int k = 0; k < 3; ++k) {
                const float f = std::fmod(fp + k * 0.45f, 1.35f) / 1.35f;
                dl->AddCircle(p, (8 + f * 34) * s, comAlfa(C_TERRA, (1 - f) * 0.85f * (1 - fp / 4)), 32, 1.6f * s);
            }
        dl->AddCircleFilled(p, 13 * s, comAlfa(C_TERRA, 0.10f * a), 20);
        torre(dl, p, s, comAlfa(C_TERRA, a));
        if (sel) {
            const float k = 15 * s, m = 5 * s;
            for (int sx : {-1, 1})
                for (int sy : {-1, 1}) {
                    const ImVec2 q = p + ImVec2(sx * k, sy * k);
                    dl->AddLine(q, q - ImVec2(sx * m, 0), IM_COL32(255, 255, 255, 230), 1.6f * s);
                    dl->AddLine(q, q - ImVec2(0, sy * m), IM_COL32(255, 255, 255, 230), 1.6f * s);
                }
        }
        pontos.push_back({kv.first, p});
        if (R.etiquetas || sel) {
            Etiqueta e;
            e.l.push_back("HFDL  " + g.nome);
            char b[96];
            std::snprintf(b, sizeof b, "%.1f kHz   SNR %.1f dB", g.freqKHz, g.snr);
            e.l.push_back(b);
            std::snprintf(b, sizeof b, T("%.0f km  %03.0f°  tx há %s"), g.dist, g.az, haQuanto(idade).c_str());
            e.l.push_back(b);
            e.cor = C_TERRA; e.a = a; e.p = p; e.sel = sel; e.emerg = false;
            etiquetas.push_back(std::move(e));
        }
    }

    // etiquetas: tenta 4 cantos e fica com o primeiro que nao bate em outra
    {
        std::vector<ImVec4> usados;
        const float lh = 12 * s + 1;
        for (const auto& e : etiquetas) {
            float w = 0;
            for (const auto& l : e.l) w = std::max(w, fEtq->CalcTextSizeA(12 * s, FLT_MAX, 0, l.c_str()).x);
            w += 8 * s;
            const float h = e.l.size() * lh + 4 * s;
            const ImVec2 cand[] = {e.p + ImVec2(14 * s, -h - 8 * s), e.p + ImVec2(14 * s, 8 * s),
                                   e.p + ImVec2(-w - 14 * s, -h - 8 * s), e.p + ImVec2(-w - 14 * s, 8 * s)};
            int esc = 0;
            for (int i = 0; i < 4; ++i) {
                bool bate = false;
                for (const auto& u : usados)
                    if (cand[i].x < u.z && cand[i].x + w > u.x && cand[i].y < u.w && cand[i].y + h > u.y) { bate = true; break; }
                if (!bate) { esc = i; break; }
            }
            const ImVec2 q = cand[esc];
            usados.push_back(ImVec4(q.x, q.y, q.x + w, q.y + h));
            const ImVec2 canto(esc < 2 ? q.x : q.x + w, esc % 2 == 0 ? q.y + h : q.y);
            dl->AddLine(e.p, canto, comAlfa(e.cor, 0.6f * e.a), 1.f * s);
            dl->AddRectFilled(q, q + ImVec2(w, h), IM_COL32(0, 0x10, 0x08, (int)(150 * e.a)), 3 * s);
            if (e.sel || e.emerg) dl->AddRect(q, q + ImVec2(w, h), comAlfa(e.emerg ? C_VERM : IM_COL32(0xff, 0xff, 0xff, 255), 0.8f * e.a), 3 * s, 0, 1.2f * s);
            for (size_t i = 0; i < e.l.size(); ++i)
                dl->AddText(fEtq, 12 * s, q + ImVec2(4 * s, 2 * s + i * lh), comAlfa(i == 0 ? e.cor : misturar(e.cor, C_TEXTO, 0.45f), e.a), e.l[i].c_str());
        }
    }

    // ---------------- a estacao ----------------
    if (dentro(E)) {
        const float k = 6 * s;
        const ImVec2 q[] = {E + ImVec2(0, -k), E + ImVec2(k, 0), E + ImVec2(0, k), E + ImVec2(-k, 0)};
        dl->AddConvexPolyFilled(q, 4, C_AMBAR);
        dl->AddCircle(E, 10 * s, comAlfa(C_AMBAR, 0.7f), 20, 1.2f * s);
        textoSombra(dl, fonte(11 * s), 11 * s, E + ImVec2(0, 12 * s), C_AMBAR, grausParaLocator(R.qLat, R.qLon).c_str(), 0.5f, 0);
    }

    // ---------------- a escala de 0 a 359 graus ----------------
    {
        ImFont* fg = fonte(13 * s);
        ImFont* fc = fonte(18 * s);
        for (int g = 0; g < 360; ++g) {
            const ImVec2 u((float)std::sin(g * kRad), (float)-std::cos(g * kRad));
            const bool dez = g % 10 == 0, cinco = g % 5 == 0;
            const float len = dez ? 11 * s : cinco ? 7 * s : 3.5f * s;
            dl->AddLine(C + u * (Rr + 1), C + u * (Rr + 1 + len), dez ? C_ESCALA : C_ESCALA2, dez ? 1.6f * s : 1.f * s);
            if (cinco) dl->AddLine(C + u * Rr, C + u * (Rr - (dez ? 6 : 3) * s), comAlfa(C_ESCALA2, 0.8f), 1.f * s);
            if (dez) {
                char b[8]; std::snprintf(b, sizeof b, "%03d", g);
                textoGirado(dl, fg, 13 * s, C + u * (Rr + 26 * s), (float)(g * kRad), C_ESCALA, b);
            }
        }
        dl->AddCircle(C, Rr, C_ESCALA, 180, 2.f * s);
        const char* card[4] = {"N", en ? "E" : "L", "S", en ? "W" : "O"};
        for (int i = 0; i < 4; ++i) {
            const ImVec2 u((float)std::sin(i * 90 * kRad), (float)-std::cos(i * 90 * kRad));
            textoSombra(dl, fc, 18 * s, C + u * (Rr - 20 * s), comAlfa(C_TEXTO, 0.85f), card[i], 0.5f, 0.5f);
        }
    }

    // ---------------- textos sobre o radar ----------------
    {
        ImFont* fi = fonte(13 * s);
        const ImVec2 q = caixa0 + ImVec2(12 * s, 10 * s);
        char b[128];
        std::snprintf(b, sizeof b, T("ALCANCE  %.0f km  (%.0f nm)"), R.alcance, R.alcance / kNm);
        textoSombra(dl, fi, 13 * s, q, C_ESCALA, b);
        std::snprintf(b, sizeof b, T("AGULHA  %.1f s/volta  (%.0f RPM)"), R.periodo, 60.0 / R.periodo);
        textoSombra(dl, fi, 13 * s, q + ImVec2(0, 17 * s), C_ESCALA2, b);
        if (R.aneis) {
            std::snprintf(b, sizeof b, T("ANÉIS  %.0f km"), passo);
            textoSombra(dl, fi, 13 * s, q + ImVec2(0, 34 * s), C_ESCALA2, b);
        }
        std::snprintf(b, sizeof b, T("NO AR %d   NA TELA %d"), nVivos, nTela);
        textoSombra(dl, fi, 13 * s, ImVec2(caixa1.x - 12 * s, q.y), C_ESCALA, b, 1.f, 0);
        if (nFora || !R.semPos.empty()) {
            // "FORA n" (avioes alem do alcance - as setas na borda) pisca em ambar, negrito, para chamar a atencao
            const float yL = q.y + 17 * s;
            float xD = caixa1.x - 12 * s;
            if (!R.semPos.empty()) {
                std::snprintf(b, sizeof b, T("SEM POSIÇÃO %d"), (int)R.semPos.size());
                textoSombra(dl, fi, 13 * s, ImVec2(xD, yL), C_ESCALA2, b, 1.f, 0);
                xD -= fi->CalcTextSizeA(13 * s, FLT_MAX, 0, b).x + 16 * s;
            }
            if (nFora) {
                // no canto de baixo, a direita: no alto a frase ficava em cima da escala de graus
                std::snprintf(b, sizeof b, T("ACERTE O ZOOM DO RADAR: %d"), nFora);
                textoSombra(dl, fonte(16 * s), 16 * s, ImVec2(caixa1.x - 12 * s, caixa1.y - 10 * s),
                            pisca ? C_AMBAR : comAlfa(C_AMBAR, 0.22f), b, 1.f, 1.f);
            }
        }
        if (R.seguir) textoSombra(dl, fi, 13 * s, ImVec2(caixa1.x - 12 * s, q.y + 34 * s), C_AMBAR, T("SEGUINDO O ALVO"), 1.f, 0);
        if (!(decRodando_ && ehAviacao(decTipo_)))
            textoSombra(dl, fonte(18 * s), 18 * s, ImVec2(C.x, C.y + Rr * 0.45f), pisca ? C_AMBAR : comAlfa(C_AMBAR, 0.6f),
                        T("DECODIFICADOR PARADO - ligue o ADS-B no painel da esquerda"), 0.5f, 0.5f);
        else if (R.alvos.empty() && R.semPos.empty())
            textoSombra(dl, fonte(15 * s), 15 * s, ImVec2(C.x, C.y + Rr * 0.45f), comAlfa(C_ESCALA, 0.8f),
                        T("aguardando aviões..."), 0.5f, 0.5f);
    }

    // ---------------- mouse: alcance, arrastar, escolher ----------------
    const float lado = Rr + aro;
    ImGui::SetCursorScreenPos(C - ImVec2(lado, lado));
    ImGui::InvisibleButton("##radarArea", ImVec2(2 * lado, 2 * lado));
    const bool hov = ImGui::IsItemHovered();
    const ImVec2 m = io.MousePos;
    const bool mDentro = hov && dentro(m);
    std::string alvoMouse;
    {
        float melhor = 14 * s * 14 * s;
        for (const auto& pp : pontos) {
            const float d2 = (pp.second.x - m.x) * (pp.second.x - m.x) + (pp.second.y - m.y) * (pp.second.y - m.y);
            if (d2 < melhor) { melhor = d2; alvoMouse = pp.first; }
        }
    }
    int aeroMouse = -1;
    if (alvoMouse.empty()) {
        float melhor = 8 * s * 8 * s;
        for (const auto& pa : aeroTela) {
            const float d2 = (pa.second.x - m.x) * (pa.second.x - m.x) + (pa.second.y - m.y) * (pa.second.y - m.y);
            if (d2 < melhor) { melhor = d2; aeroMouse = pa.first; }
        }
    }
    if (hov && io.MouseWheel != 0.f)
        R.alcance = std::clamp(R.alcance * (float)std::pow(kPassoRoda, io.MouseWheel), kAlcMin, kAlcMax);
    if (ImGui::IsItemActivated()) { R.arrastou = false; R.arrLat = R.vLat; R.arrLon = R.vLon; R.arrMouse = m; }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4 * s)) {
        R.arrastou = true; R.seguir = false;
        Proj P0;
        P0.ajustar(R.arrLat, R.arrLon, C, Rr, R.alcance);
        P0.inv(C - (m - R.arrMouse), R.vLat, R.vLon);
    }
    if (ImGui::IsItemDeactivated() && !R.arrastou) R.sel = alvoMouse;          // clique no vazio desmarca
    if (mDentro && alvoMouse.empty() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { P.inv(m, R.vLat, R.vLon); R.seguir = false; }
    if (ImGui::IsItemActive() && R.arrastou) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if (!alvoMouse.empty()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (mDentro && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        P.inv(m, R.menuLat, R.menuLon);
        if (!alvoMouse.empty()) R.sel = alvoMouse;
        ImGui::OpenPopup("##radarmenu");
    }
    if (ImGui::BeginPopup("##radarmenu")) {
        char b[96];
        std::snprintf(b, sizeof b, "%.5f, %.5f  (%s)", R.menuLat, R.menuLon, grausParaLocator(R.menuLat, R.menuLon).c_str());
        ImGui::TextDisabled("%s", b);
        ImGui::Separator();
        if (ImGui::MenuItem(T("Centralizar aqui"))) { R.vLat = R.menuLat; R.vLon = R.menuLon; R.seguir = false; }
        if (ImGui::MenuItem(T("RESET - voltar para a estação"))) { R.vLat = R.qLat; R.vLon = R.qLon; R.seguir = false; R.alcance = kAlcPadrao; }
        if (!R.sel.empty() && R.alvos.count(R.sel)) {
            if (ImGui::MenuItem(R.seguir ? T("Parar de seguir o alvo") : T("Seguir o alvo escolhido"))) R.seguir = !R.seguir;
        }
        if (ImGui::MenuItem(T("Copiar as coordenadas"))) ImGui::SetClipboardText(b);
        ImGui::Separator();
        if (ImGui::MenuItem(T("Usar este ponto como centro do radar"))) {
            std::snprintf(b, sizeof b, "%.6f,%.6f", R.menuLat, R.menuLon);
            std::string erro;
            definirCentro(R, b, erro);
        }
        ImGui::EndPopup();
    }
    // dica do alvo / aeroporto sob o mouse
    const auto igm = R.gs.find(alvoMouse);
    if (hov && igm != R.gs.end() && !ImGui::IsPopupOpen("##radarmenu")) {
        const auto& g = igm->second;
        ImGui::BeginTooltip();
        ImGui::PushFont(f_.negrito);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_TERRA), "HFDL  %s", g.nome.c_str());
        ImGui::PopFont();
        ImGui::Text(T("Estação de terra nº %d - transmissão recebida"), g.id);
        ImGui::Text("%.1f kHz   SNR %.1f dB", g.freqKHz, g.snr);
        ImGui::Text(T("Distância: %.0f km (%.0f nm)   Azimute: %03.0f°"), g.dist, g.dist / kNm, g.az);
        ImGui::Text(T("Última transmissão há %s"), haQuanto(t - g.visto).c_str());
        ImGui::EndTooltip();
    } else if (hov && !alvoMouse.empty() && !ImGui::IsPopupOpen("##radarmenu")) {
        const auto it = R.alvos.find(alvoMouse);
        if (it != R.alvos.end()) {
            const auto& v = it->second;
            ImGui::BeginTooltip();
            ImGui::PushFont(f_.negrito);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(corClasse(v)), "%s", v.nome().c_str());
            ImGui::PopFont();
            if (!v.prefixo.empty() && v.prefixo != v.nome()) ImGui::Text(T("Prefixo: %s"), v.prefixo.c_str());
            if (!v.tipo.empty() || !v.empresa.empty()) ImGui::Text("%s  %s", v.tipo.c_str(), v.empresa.c_str());
            ImGui::Text(T("Altitude: %s   Velocidade: %s"), textoAlt(v).c_str(), v.vel >= 0 ? (std::to_string((int)std::lround(v.vel)) + " kt").c_str() : "-");
            ImGui::Text(T("Distância: %.1f km (%.1f nm)   Azimute: %03.0f°"), v.dist, v.dist / kNm, v.az);
            ImGui::Text(T("Fonte: %s   visto há %.0f s"), (v.net ? v.fonte + T(" + posição da internet") : v.fonte).c_str(), v.idadeAgora(t));
            ImGui::TextDisabled("%s", T("clique: ficha completa  ·  botão direito: seguir"));
            ImGui::EndTooltip();
        }
    } else if (hov && aeroMouse >= 0) {
        const AeroportoInfo& a = kAeroportos[aeroMouse];
        ImGui::SetTooltip(T("%s - %s\n%s\n%.1f km da estação, azimute %03.0f°"), a.id, a.nome, a.cidade,
                          distKm(R.qLat, R.qLon, a.lat, a.lon), azimute(R.qLat, R.qLon, a.lat, a.lon));
    }
    // leitura do cursor (embaixo do radar)
    if (mDentro) {
        double la, lo;
        P.inv(m, la, lo);
        const double dk = distKm(R.qLat, R.qLon, la, lo);
        char b[160];
        std::snprintf(b, sizeof b, T("CURSOR  %03.0f°  %.1f km (%.1f nm)   %.4f, %.4f   %s"), azimute(R.qLat, R.qLon, la, lo), dk,
                      dk / kNm, la, lo, grausParaLocator(la, lo).c_str());
        textoSombra(dl, fonte(13 * s), 13 * s, ImVec2(caixa0.x + 12 * s, caixa1.y - 10 * s), C_ESCALA, b, 0, 1.f);
    }

    // ---------------- barra do topo ----------------
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, hb), IM_COL32(0x08, 0x22, 0x18, 255), IM_COL32(0x08, 0x22, 0x18, 255),
                                IM_COL32(0x03, 0x0c, 0x08, 255), IM_COL32(0x03, 0x0c, 0x08, 255));
    dl->AddLine(ImVec2(0, hb - 1), ImVec2(W, hb - 1), C_ESCALA2, 1.5f * s);
    {
        ImFont* ft = fonte(25 * s);
        textoSombra(dl, ft, 25 * s, ImVec2(W * 0.5f, hb * 0.5f - 6 * s), C_FOSFORO, T("RADAR AÉREO"), 0.5f, 0.5f);
        char b[96];
        std::snprintf(b, sizeof b, "UTC %s   %s %s", horaAgora(true).c_str(), T("local"), horaAgora(false).c_str());
        textoSombra(dl, fonte(11.5f * s), 11.5f * s, ImVec2(W * 0.5f, hb - 9 * s), C_ESCALA2, b, 0.5f, 0.5f);
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.20f, 0.14f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.07f, 0.34f, 0.24f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.46f, 0.32f, 1));
    ImGui::PushFont(f_.negrito);
    const float bh = 30 * s, by = (hb - bh) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(10 * s, by));
    if (ImGui::Button(T("<  VOLTAR"), ImVec2(118 * s, bh))) acao = 1;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Volta para o programa (ESC). O decodificador continua ouvindo."));
    ImGui::SameLine();
    {
        const bool aviacao = decRodando_ && ehAviacao(decTipo_);
        const ImVec2 q = ImGui::GetCursorScreenPos() + ImVec2(8 * s, bh * 0.5f);
        const bool recebe = aviacao && (decTipo_ != Decoders::ADSB || d.adsb().estado().msgsPorS > 0.05);
        dl->AddCircleFilled(q, 5 * s, aviacao ? (recebe ? C_FOSFORO : C_AMBAR) : C_VERM, 12);
        char b[96];
        std::snprintf(b, sizeof b, "%s", aviacao ? nomeFonte(decTipo_) : T("sem decodificador de aviação"));
        textoSombra(dl, f_.negrito, f_.negrito->FontSize, q + ImVec2(10 * s, 0), aviacao ? C_TEXTO : C_AMBAR, b, 0, 0.5f);
    }
    float bx = W - 10 * s;
    auto botaoDir = [&](const char* rot, float w) {
        bx -= w;
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        bx -= 6 * s;
        return ImGui::Button(rot, ImVec2(w, bh));
    };
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.08f, 0.06f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.12f, 0.08f, 1));
    if (botaoDir(T("X  FECHAR"), 110 * s)) acao = 2;
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Fecha o radar E para o decodificador (o rádio volta como estava)."));
    if (botaoDir(R.cheia ? T("JANELA") : T("TELA CHEIA"), 112 * s)) { R.cheia = !R.cheia; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Alterna entre a tela cheia do monitor e a janela normal (F11)."));
    if (botaoDir(R.paineis ? T("SÓ O RADAR") : T("PAINÉIS"), 112 * s)) R.paineis = !R.paineis;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Esconde / mostra os painéis dos lados."));
    if (botaoDir(T("CENTRO"), 92 * s)) {
        R.pedirCentro = true;
        std::snprintf(R.entrada, sizeof R.entrada, "%s", R.centroTxt.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Trocar o centro do radar (locator ou coordenadas da sua estação)."));
    if (botaoDir(T("RESET"), 84 * s)) { R.vLat = R.qLat; R.vLon = R.qLon; R.seguir = false; R.alcance = kAlcPadrao; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Volta a vista para o centro configurado (a sua estação) e o alcance para 450 km."));
    ImGui::PopFont();
    ImGui::PopStyleColor(3);

    auto titulo = [&](const char* txt) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x48, 0xe8, 0xd0, 255));
        ImGui::PushFont(f_.negrito);
        ImGui::SeparatorText(txt);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    };
    auto bolinha = [&](ImU32 cor) {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        const float lh = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(q + ImVec2(5 * s, lh * 0.5f), 4.5f * s, cor, 12);
        ImGui::Dummy(ImVec2(12 * s, lh));
        ImGui::SameLine();
    };

    // ---------------- painel da esquerda: estacao, recepcao, vista ----------------
    if (lados) {
        ImGui::SetCursorScreenPos(ImVec2(0, hb));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0x04, 0x0e, 0x09, 255));
        if (ImGui::BeginChild("##radarEsq", ImVec2(wl, H - hb), ImGuiChildFlags_Borders)) {
            titulo(T("ESTAÇÃO"));
            ImGui::PushFont(f_.negrito);
            ImGui::TextColored(ImVec4(1.f, 0.71f, 0.16f, 1), "%s", grausParaLocator(R.qLat, R.qLon).c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::Text("%.5f, %.5f", R.qLat, R.qLon);
            if (!R.temCentro) ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "%s", T("centro ainda não configurado"));
            else if (!R.centroTxt.empty()) ImGui::TextDisabled(T("digitado: %s"), R.centroTxt.c_str());
            if (ImGui::Button(T("Alterar o centro"), ImVec2(-FLT_MIN, 0))) {
                R.pedirCentro = true;
                std::snprintf(R.entrada, sizeof R.entrada, "%s", R.centroTxt.c_str());
            }

            titulo(T("RECEPÇÃO"));
            const bool aviacao = decRodando_ && ehAviacao(decTipo_);
            bolinha(aviacao ? C_FOSFORO : C_VERM);
            ImGui::TextUnformatted(aviacao ? nomeFonte(decTipo_) : T("nenhum decodificador de aviação rodando"));
            if (!adsbRodando) {
                if (ImGui::Button(T("Ligar o ADS-B (1090 MHz)"), ImVec2(-FLT_MIN, 0))) ligarAdsb = true;
                if (aviacao)
                    ImGui::TextWrapped("%s", T("O ACARS / VDL2 / HFDL só dão a posição de vez em quando. Para o radar, o melhor é o ADS-B."));
            }
            if (adsbRodando) {
                const auto e = d.adsb().estado();
                ImGui::Text(T("Amostragem: %.2f Msps"), e.taxa / 1e6);
                if (!e.taxaOk) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", T("(baixa!)")); }
                ImGui::Text(T("Mensagens: %.1f/s  (%lld no total)"), e.msgsPorS, e.mensagens);
                ImGui::Text(T("Nível dos pulsos: %.1f dBFS"), e.nivelDb);
                ImGui::Text(T("Aviões: %d  (com posição: %d)"), e.avioes, e.comPosicao);
                if (!R.histMsgs.empty()) {
                    float mx = 1;
                    for (float x : R.histMsgs) mx = std::max(mx, x);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, IM_COL32(0x40, 0xff, 0x9a, 255));
                    ImGui::PlotLines("##msgs", R.histMsgs.data(), (int)R.histMsgs.size(), 0, T("mensagens/s (2 min)"), 0, mx * 1.15f,
                                     ImVec2(-FLT_MIN, 46 * s));
                    ImGui::PopStyleColor();
                }
            } else if (aviacao) {
                ImGui::TextWrapped("%s", d.estado().c_str());
            }
            ImGui::Text(T("No ar: %d   na tela: %d"), nVivos, nTela);
            ImGui::Text(T("Fora do alcance: %d   sem posição: %d"), nFora, (int)R.semPos.size());
            if (R.recKm > 0) ImGui::Text(T("Mais longe: %s a %.0f km (%.0f nm)"), R.recNome.c_str(), R.recKm, R.recKm / kNm);

            titulo(T("VISTA"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##alc", &R.alcance, kAlcMin, kAlcMax, T("alcance %.0f km"), ImGuiSliderFlags_Logarithmic);
            for (int k : {50, 150, 450, 1500, 5000, 20000}) {
                char b[16]; std::snprintf(b, sizeof b, "%d##alc%d", k, k);
                if (k != 50) ImGui::SameLine(0, 3 * s);
                if (ImGui::SmallButton(b)) R.alcance = (float)k;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##volta", &R.periodo, 2.f, 12.f, T("agulha: %.1f s por volta"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##persist", &R.persist, 1.f, 30.f, T("eco fica aceso: %.1f s"));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", T("Quanto tempo o avião fica na tela depois que a agulha passa.\n"
                                          "Menor que a volta da agulha: ele some e volta a cada volta (radar de verdade)."));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##mem", &R.memoria, 10, 180, T("some depois de %d s sem sinal"));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", T("ADS-B: se o avião não for ouvido por este tempo, a agulha não acende mais ele.\n"
                                          "(ACARS / VDL2 / HFDL: 15 min, porque mandam a posição de vez em quando.)"));
            ImGui::Checkbox(T("Mapa"), &R.mapa); ImGui::SameLine(wl * 0.5f);
            ImGui::Checkbox(T("Aeroportos"), &R.aeroportos);
            ImGui::Checkbox(T("Anéis"), &R.aneis); ImGui::SameLine(wl * 0.5f);
            ImGui::Checkbox(T("Etiquetas"), &R.etiquetas);
            ImGui::Checkbox(T("Rastro"), &R.rastros); ImGui::SameLine(wl * 0.5f);
            ImGui::Checkbox(T("Vetor 1 min"), &R.vetor);
            ImGui::Checkbox(T("Etiqueta fixa"), &R.fixa); ImGui::SameLine(wl * 0.5f);
            ImGui::Checkbox(T("Fora do alcance"), &R.fora);
            ImGui::Checkbox(T("Posição pela internet (adsb.lol)"), &R.netPos);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", T("Para os aviões que o rádio OUVIU mas sem posição (ACARS / VDL2 / HFDL sem relatório de posição,\n"
                                          "Modo S sem ADS-B): pega a posição na rede aberta adsb.lol, pelo código ICAO, prefixo ou voo.\n"
                                          "Aparecem só em contorno, azul-claro, com [NET] na etiqueta: essa posição não foi o seu receptor."));
            if (R.netPos) {
                // chave OPCIONAL do ADS-B Exchange: e um servico PAGO (assinatura no RapidAPI)
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##adsbx", T("chave ADS-B Exchange (opcional, paga)"), R.chaveX, sizeof R.chaveX,
                                         ImGuiInputTextFlags_Password);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
                    ImGui::TextColored(ImVec4(1.f, 0.75f, 0.25f, 1.f), "%s", T("ADS-B Exchange - ASSINATURA PAGA"));
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", T("Campo opcional. O ADS-B Exchange é um serviço PAGO: para usar, você precisa "
                                               "assinar um plano da API dele no RapidAPI (rapidapi.com/adsbx/api/adsbexchange-com1) "
                                               "e colar aqui a sua chave (X-RapidAPI-Key)."));
                    ImGui::Spacing();
                    ImGui::BulletText("%s", T("O SDR990S não vem com chave: cada usuário usa a própria assinatura."));
                    ImGui::BulletText("%s", T("Os planos têm limite de consultas por mês: com a chave, cada avião sem posição é consultado no máximo a cada 2 minutos."));
                    ImGui::BulletText("%s", T("Com a chave, o radar procura primeiro no ADS-B Exchange; se não achar, no adsb.lol (grátis)."));
                    ImGui::BulletText("%s", T("Sem a chave (campo vazio), usa só o adsb.lol, que é gratuito."));
                    ImGui::BulletText("%s", T("A chave fica guardada só no SDR990S.ini deste computador."));
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", R.posNet.situacao().c_str());   // quebra a linha: a mensagem do ADS-B Exchange e comprida
                ImGui::PopStyleColor();
            }
            const char* kCor[] = {T("cor: empresa"), T("cor: altitude")};
            const char* kSimb[] = {T("símbolo: avião"), T("símbolo: ponto")};
            ImGui::SetNextItemWidth(wl * 0.5f - 12 * s);
            ImGui::Combo("##cor", &R.cor, kCor, 2);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("##simb", &R.simbolo, kSimb, 2);

            titulo(T("LEGENDA"));
            if (R.cor == 0) {
                struct L { ImU32 c; const char* t; };
                const L leg[] = {{C_FOSFORO, T("linha aérea")}, {IM_COL32(0xf2, 0xd8, 0x70, 255), T("carga")},
                                 {IM_COL32(0xc0, 0xf2, 0xa6, 255), T("executiva / táxi aéreo")}, {IM_COL32(0xff, 0x9e, 0x40, 255), T("militar")},
                                 {IM_COL32(0x74, 0xcc, 0xff, 255), T("governo")}, {IM_COL32(0xc8, 0xc8, 0xc8, 255), T("particular")},
                                 {IM_COL32(0xd8, 0x90, 0xff, 255), T("posição do ACARS / VDL2 / HFDL")}, {C_VERM, T("squawk 7500 / 7600 / 7700")}};
                for (const auto& l : leg) { bolinha(l.c); ImGui::TextUnformatted(l.t); }
            } else {
                const ImVec2 q = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                for (int i = 0; i < 60; ++i)
                    ImGui::GetWindowDrawList()->AddRectFilled(q + ImVec2(w * i / 60, 0), q + ImVec2(w * (i + 1) / 60 + 1, 10 * s),
                                                              corAltitude(i * 45000 / 60));
                ImGui::Dummy(ImVec2(w, 12 * s));
                ImGui::TextUnformatted(T("solo      FL150      FL300      FL450"));
            }
            bolinha(C_AERO); ImGui::TextUnformatted(T("aeroporto (quadrado = grande)"));
            bolinha(C_TERRA); ImGui::TextUnformatted(T("estação de terra HFDL (transmissão)"));
            bolinha(C_NET); ImGui::TextUnformatted(T("posição pela internet - contorno"));
            bolinha(C_AMBAR); ImGui::TextUnformatted(T("a sua estação"));
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", T("Roda do mouse: alcance. Arrastar: mover a vista. Duplo clique: centralizar. "
                                       "Clique no avião: ficha. Botão direito: menu. ESC: voltar. F11: tela cheia."));
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---------------- painel da direita: alvos, ficha, eventos, mensagens ----------------
    if (lados) {
        ImGui::SetCursorScreenPos(ImVec2(W - wr, hb));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0x04, 0x0e, 0x09, 255));
        if (ImGui::BeginChild("##radarDir", ImVec2(wr, H - hb), ImGuiChildFlags_Borders)) {
            const float hTot = H - hb;
            std::vector<Radar::Alvo*> lista;
            for (auto& kv : R.alvos)
                if (kv.second.vivo) lista.push_back(&kv.second);
            std::sort(lista.begin(), lista.end(), [](const Radar::Alvo* x, const Radar::Alvo* y) { return x->dist < y->dist; });
            char tt[64];
            std::snprintf(tt, sizeof tt, T("ALVOS (%d)"), (int)(lista.size() + R.semPos.size()));
            titulo(tt);
            const ImGuiTableFlags fl = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##radarAlvos", 7, fl, ImVec2(0, hTot * 0.34f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(T("Alvo"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableSetupColumn("km", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                ImGui::TableSetupColumn("Az", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn(T("Alt"), ImGuiTableColumnFlags_WidthStretch, 0.85f);
                ImGui::TableSetupColumn("kt", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn(T("Fonte"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableSetupColumn("ICAO", ImGuiTableColumnFlags_WidthStretch, 1.05f);   // cabe "Unknown"
                ImGui::TableHeadersRow();
                int id = 0;
                for (Radar::Alvo* pv : lista) {
                    const auto& v = *pv;
                    ImGui::PushID(id++);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const ImU32 cor = emergencia(v.squawk) ? C_VERM : corClasse(v);
                    ImGui::PushStyleColor(ImGuiCol_Text, cor);
                    if (ImGui::Selectable(v.nome().c_str(), R.sel == v.chave, ImGuiSelectableFlags_SpanAllColumns)) R.sel = v.chave;
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { R.sel = v.chave; R.seguir = true; }
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.0f", v.dist);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%03.0f", v.az);
                    ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(textoAlt(v).c_str());
                    ImGui::TableSetColumnIndex(4); if (v.vel >= 0) ImGui::Text("%.0f", v.vel); else ImGui::TextUnformatted("-");
                    ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("%s", (v.net ? v.fonte + "+NET" : v.fonte).c_str());
                    ImGui::TableSetColumnIndex(6);
                    if (v.hex.empty()) ImGui::TextDisabled("Unknown"); else ImGui::TextUnformatted(v.hex.c_str());
                    ImGui::PopID();
                }
                // os que estao sendo ouvidos sem posicao (Modo S / ADS-B sem CPR ainda)
                for (const auto& a : R.semPos) {
                    ImGui::PushID(id++);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", !a.voo.empty() ? a.voo.c_str() : !a.prefixo.empty() ? a.prefixo.c_str() : a.hex.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("-");
                    ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("-");
                    ImGui::TableSetColumnIndex(3);
                    if (a.altitude != AdsbCore::kSemValor) ImGui::TextDisabled("%d ft", a.altitude); else ImGui::TextDisabled("-");
                    ImGui::TableSetColumnIndex(4);
                    if (a.velocidade >= 0) ImGui::TextDisabled("%.0f", a.velocidade); else ImGui::TextDisabled("-");
                    ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("%s", T("sem pos."));
                    ImGui::TableSetColumnIndex(6); ImGui::TextDisabled("%s", a.hex.c_str());
                    ImGui::PopID();
                }
                // as estacoes de terra do HFDL que transmitiram
                for (const auto& kv : R.gs) {
                    const auto& g = kv.second;
                    if (g.lat > 90 || t - g.visto > 1800) continue;
                    ImGui::PushID(id++);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushStyleColor(ImGuiCol_Text, C_TERRA);
                    const std::string curto = g.nome.substr(0, g.nome.find(','));
                    if (ImGui::Selectable(curto.c_str(), R.sel == kv.first, ImGuiSelectableFlags_SpanAllColumns)) R.sel = kv.first;
                    ImGui::PopStyleColor();
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.0f", g.dist);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%03.0f", g.az);
                    ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted("-");
                    ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted("-");
                    ImGui::TableSetColumnIndex(5); ImGui::TextDisabled("%s", T("terra"));
                    ImGui::TableSetColumnIndex(6); ImGui::Text("GS %d", g.id);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            titulo(T("FICHA DO ALVO"));
            const auto it = R.alvos.find(R.sel);
            if (ImGui::BeginChild("##radarFicha", ImVec2(0, hTot * 0.30f))) {
                const auto ig = R.gs.find(R.sel);
                if (ig != R.gs.end()) {
                    const auto& g = ig->second;
                    ImGui::PushFont(fonte(21 * s));
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_TERRA), "%s", g.nome.c_str());
                    ImGui::PopFont();
                    ImGui::TextDisabled("%s", T("Estação de terra do HFDL (transmissão recebida)"));
                    if (ImGui::BeginTable("##fichaGs", 2, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("r", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(T("Transmissões")).x + 10 * s);
                        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
                        auto linhaG = [&](const char* rot, const std::string& val) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", rot);
                            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(val.empty() ? "-" : val.c_str());
                        };
                        char b[160];
                        std::snprintf(b, sizeof b, "%d", g.id); linhaG(T("Estação nº"), b);
                        std::snprintf(b, sizeof b, "%.1f kHz", g.freqKHz); linhaG(T("Frequência"), b);
                        std::snprintf(b, sizeof b, "%.1f dB", g.snr); linhaG("SNR", b);
                        std::snprintf(b, sizeof b, "%d", g.msgs); linhaG(T("Transmissões"), b);
                        linhaG(T("Última"), haQuanto(t - g.visto));
                        std::snprintf(b, sizeof b, "%.0f km (%.0f nm)  az %03.0f\xC2\xB0", g.dist, g.dist / kNm, g.az); linhaG(T("Distância"), b);
                        std::snprintf(b, sizeof b, "%.4f, %.4f", g.lat, g.lon); linhaG(T("Posição"), b);
                        linhaG(T("Frequências"), g.freqs);
                        ImGui::EndTable();
                    }
                    if (ImGui::SmallButton("Google Maps")) {
                        char u[128];
                        std::snprintf(u, sizeof u, "https://www.google.com/maps/search/?api=1&query=%.5f,%.5f", g.lat, g.lon);
                        abrirUrl(u);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(T("Centralizar"))) { R.vLat = g.lat; R.vLon = g.lon; R.seguir = false; }
                } else if (it == R.alvos.end()) {
                    ImGui::TextDisabled("%s", T("Clique num avião no radar ou na lista."));
                    ImGui::TextDisabled("%s", T("Duplo clique na lista: seguir o avião."));
                } else {
                    const auto& v = it->second;
                    ImGui::PushFont(fonte(21 * s));
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(emergencia(v.squawk) ? C_VERM : corClasse(v)), "%s", v.nome().c_str());
                    ImGui::PopFont();
                    if (!v.vivo) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "%s", T("(sem sinal)")); }
                    AeronaveHfdl fa;
                    fa.voo = v.voo; fa.prefixo = v.prefixo; fa.hex = v.hex;
                    if (ImGui::BeginTable("##ficha", 2, ImGuiTableFlags_SizingStretchProp)) {
                        auto linha = [&](const char* rot, const std::string& val, ImU32 cor = 0) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", rot);
                            ImGui::TableSetColumnIndex(1);
                            if (cor) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(cor), "%s", val.empty() ? "-" : val.c_str());
                            else ImGui::TextUnformatted(val.empty() ? "-" : val.c_str());
                        };
                        char b[128];
                        linha(T("Voo"), v.vooCortado ? v.voo + "?  " + T("(o HFDL só manda 6 caracteres)") : v.voo);
                        if (!v.vooNet.empty()) linha(T("Voo completo"), std::string(T("pela internet")) + " (" + v.vooNet + ")", C_NET);
                        if (!v.vooMsg.empty()) linha(T("Voo na mensagem"), v.vooMsg);
                        linha(T("Prefixo"), v.prefixo);
                        linha("ICAO", v.hex.empty() ? std::string("Unknown") : v.hex);
                        if (!v.empresa.empty()) linha(T("Empresa"), v.empresa, corClasse(v));
                        else linha(T("Empresa"), "Unknown");
                        if (!v.tipo.empty()) linha(T("Modelo"), v.tipo);
                        if (!v.operador.empty() && v.classe != "particular") linha(T("Operador"), v.operador);
                        if (!v.pais.empty()) linha(T("País"), v.pais);
                        if (v.noChao) linha(T("Altitude"), T("no chão"));
                        else if (v.alt != AdsbCore::kSemValor) {
                            std::snprintf(b, sizeof b, "%d ft  (FL%03d,  %.0f m)", v.alt, (v.alt + 50) / 100, v.alt * 0.3048);
                            linha(T("Altitude"), b);
                        }
                        if (v.vel >= 0) { std::snprintf(b, sizeof b, "%.0f kt  (%.0f km/h)", v.vel, v.vel * kNm); linha(T("Velocidade"), b); }
                        if (v.rumo >= 0) { std::snprintf(b, sizeof b, "%03.0f\xC2\xB0", v.rumo); linha(T("Rumo"), b); }
                        if (v.vs != AdsbCore::kSemValor) { std::snprintf(b, sizeof b, "%+d ft/min", v.vs); linha(T("Subida"), b); }
                        if (v.squawk >= 0) {
                            std::snprintf(b, sizeof b, "%04d %s", v.squawk, nomeEmergencia(v.squawk));
                            linha("Squawk", b, emergencia(v.squawk) ? C_VERM : 0);
                        }
                        std::snprintf(b, sizeof b, "%.1f km (%.1f nm)  az %03.0f\xC2\xB0", v.dist, v.dist / kNm, v.az);
                        linha(T("Da estação"), b);
                        std::snprintf(b, sizeof b, "%.4f, %.4f", v.eLat <= 90 ? v.eLat : v.lat, v.eLat <= 90 ? v.eLon : v.lon);
                        linha(T("Posição"), b);
                        std::snprintf(b, sizeof b, T("%s, %d msgs, há %.0f s"), v.fonte.c_str(), v.msgs, v.idadeAgora(t));
                        linha(T("Recepção"), b);
                        if (v.net) {
                            std::snprintf(b, sizeof b, T("internet (%s), há %s"), v.netFonte.c_str(), haQuanto(v.idadeAgora(t)).c_str());
                            linha(T("Posição de"), b, C_NET);
                        }
                        ImGui::EndTable();
                    }
                    if (!v.voo.empty() || !v.prefixo.empty()) {
                        if (ImGui::SmallButton("FlightAware")) abrirUrl(!v.voo.empty() ? fa.urlVoo() : fa.urlPrefixo());
                        ImGui::SameLine();
                    }
                    if (!v.hex.empty()) {
                        std::string h = v.hex;
                        for (auto& c : h) c = (char)std::tolower((unsigned char)c);
                        if (ImGui::SmallButton("ADS-B Exchange")) abrirUrl("https://globe.adsbexchange.com/?icao=" + h);
                        ImGui::SameLine();
                    }
                    if (ImGui::SmallButton("Google Maps")) {
                        char u[128];
                        std::snprintf(u, sizeof u, "https://www.google.com/maps/search/?api=1&query=%.5f,%.5f", v.lat, v.lon);
                        abrirUrl(u);
                    }
                    if (ImGui::SmallButton(R.seguir ? T("Parar de seguir") : T("Seguir"))) R.seguir = !R.seguir;
                    ImGui::SameLine();
                    if (ImGui::SmallButton(T("Centralizar"))) { R.vLat = v.eLat <= 90 ? v.eLat : v.lat; R.vLon = v.eLat <= 90 ? v.eLon : v.lon; R.seguir = false; }
                }
            }
            ImGui::EndChild();

            titulo(T("EVENTOS"));
            const float resto = ImGui::GetContentRegionAvail().y;
            if (ImGui::BeginChild("##radarEventos", ImVec2(0, resto * 0.5f - 30 * s), ImGuiChildFlags_Borders)) {
                if (R.eventos.empty()) ImGui::TextDisabled("%s", T("nada ainda"));
                for (const auto& e : R.eventos) {
                    ImGui::TextDisabled("%s", e.hora.c_str());
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, e.cor);
                    ImGui::TextWrapped("%s", e.texto.c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild();
            titulo(T("MENSAGENS DO DECODIFICADOR"));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0x02, 0x06, 0x03, 255));
            if (ImGui::BeginChild("##radarMsgs", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
                // as ultimas ~60 linhas da caixa de texto dos decodificadores
                size_t ini = decTexto_.size();
                for (int n = 0; n < 60 && ini > 0;) {
                    --ini;
                    if (decTexto_[ini] == '\n' && ini + 1 < decTexto_.size()) ++n;
                }
                ImGui::PushFont(f_.mono);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xb0, 0xf0, 0x20, 255));
                ImGui::TextUnformatted(decTexto_.c_str() + ini, decTexto_.c_str() + decTexto_.size());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 30 * s) ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---------------- centro do radar (pergunta na primeira vez) ----------------
    const char* idCentro = "###radarCentro";
    if (R.pedirCentro) { R.pedirCentro = false; R.erroEntrada.clear(); ImGui::OpenPopup(idCentro); }
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560 * s, 0));
    const std::string tituloCentro = std::string(T("Centro do radar")) + idCentro;
    if (ImGui::BeginPopupModal(tituloCentro.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        if (R.fecharCentro) { R.fecharCentro = false; ImGui::CloseCurrentPopup(); }
        ImGui::PushFont(f_.negrito);
        ImGui::TextUnformatted(T("Onde fica a sua estação? Ela será o centro do radar."));
        ImGui::PopFont();
        ImGui::TextWrapped("%s", T("Digite o locator (ex.: GG87 ou GG87jb) ou as coordenadas em graus decimais, latitude e longitude "
                                   "(ex.: -22.8349,-42.3599). Cada um coloca a própria; fica guardado no SDR990S.ini."));
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool enter = ImGui::InputTextWithHint("##centroTxt", "GG87jb   /   -22.8349,-42.3599", R.entrada, sizeof R.entrada,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        double la = 0, lo = 0;
        std::string erroPrevia;
        if (R.entrada[0] && lerCentro(R.entrada, la, lo, erroPrevia))
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.6f, 1), "= %.5f, %.5f   (%s)", la, lo, grausParaLocator(la, lo).c_str());
        else
            ImGui::TextDisabled("%s", " ");
        if (!R.erroEntrada.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", R.erroEntrada.c_str());
        ImGui::Spacing();
        const bool ok = ImGui::Button("OK", ImVec2(110 * s, 0)) || enter;
        if (ok) {
            if (definirCentro(R, R.entrada, R.erroEntrada)) ImGui::CloseCurrentPopup();
        }
        const std::string satLat = Config::instance().str("sat_lat");
        if (!satLat.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(T("Usar o QTH dos Satélites"))) {
                char b[64];
                std::snprintf(b, sizeof b, "%s,%s", satLat.c_str(), Config::instance().str("sat_lon").c_str());
                std::snprintf(R.entrada, sizeof R.entrada, "%s", b);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(R.temCentro ? T("Cancelar") : T("Agora não (voltar)"))) {
            ImGui::CloseCurrentPopup();
            if (!R.temCentro) acao = 1;
        }
        ImGui::EndPopup();
    } else {
        R.fecharCentro = false;
    }

    // ---------------- teclado ----------------
    if (!io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) acao = acao ? acao : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) R.cheia = !R.cheia;
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) || ImGui::IsKeyPressed(ImGuiKey_Equal))
            R.alcance = std::clamp(R.alcance * kPassoRoda, kAlcMin, kAlcMax);
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) || ImGui::IsKeyPressed(ImGuiKey_Minus))
            R.alcance = std::clamp(R.alcance / kPassoRoda, kAlcMin, kAlcMax);
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) { R.vLat = R.qLat; R.vLon = R.qLon; R.seguir = false; R.alcance = kAlcPadrao; }
    }
    g_telaCheia = R.cheia ? 1 : 0;

    // ---------------- acoes (depois de desenhar) ----------------
    if (ligarAdsb) {
        if (!r_.ligado()) ligarDesligar();
        decAberta_ = true;
        escolherDecoder(Decoders::ADSB, true);
        evento(R, T("ADS-B (1090 MHz) ligado para o radar"), C_ESCALA);
    }
    if (acao == 1) fecharRadar(false);
    else if (acao == 2) fecharRadar(true);
}

} // namespace masdr
