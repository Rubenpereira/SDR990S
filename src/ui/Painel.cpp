// ---------------------------------------------------------------------------
//  Painel do SDR-990S
//
//  A tela inteira e o painel frontal de um transceptor de HF de mesa, com o
//  layout do classico de 2013 que o autor usa de modelo: tela principal com
//  S-meter de ponteiro, Band Scope e cachoeira, teclas F1-F7 embaixo, tela
//  secundaria MAIN/SUB, dial grande, dial do SUB, teclado de bandas, RIT/XIT,
//  MULTI/CH e as colunas MAIN e SUB RECEIVER com os botoes duplos.
//
//  Tudo e desenhado aqui (sem imagem), num "papel" de 1500 x 600 unidades
//  que cresce e encolhe com a janela - os botoes ficam sempre no lugar.
//  Cada botao faz a funcao do radio; os de transmissao, que num receptor nao
//  tem sentido, ganharam funcoes de recepcao (a dica de cada um diz qual).
// ---------------------------------------------------------------------------
#include "Ui.h"

#include "../app/Config.h"
#include "../dsp/AjustesDsp.h"
#include "../util/Logger.h"
#include "Idioma.h"
#include "Recursos.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#include <windows.h>
#include <mmsystem.h>
#include <sapi.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace masdr {

extern int g_paletaCachoeira;   // Ui.cpp
extern bool g_pedirCaptura;     // main.cpp (F12: captura da tela)
extern std::string g_ultimaCaptura;

namespace {

constexpr float kLW = 1500.f, kLH = 600.f;     // tamanho do "papel" do painel
constexpr float kIfLarguraBaixo = 300.f;        // IF DISPLAY embaixo (px, x escala do Windows)
constexpr double kPi = 3.14159265358979323846;

// Cores do painel
constexpr ImU32 P_CHASSI   = IM_COL32(0x26, 0x28, 0x2b, 255);
constexpr ImU32 P_CHASSI2  = IM_COL32(0x1b, 0x1c, 0x1f, 255);
constexpr ImU32 P_ROTULO   = IM_COL32(0xe6, 0xe6, 0xe6, 255);
constexpr ImU32 P_ROTULO2  = IM_COL32(0xb4, 0xb6, 0xba, 255);
constexpr ImU32 P_LINHA    = IM_COL32(0x44, 0x47, 0x4c, 255);
constexpr ImU32 P_LCD      = IM_COL32(0x03, 0x06, 0x0a, 255);
constexpr ImU32 P_AMBAR    = IM_COL32(0xff, 0xb0, 0x30, 255);
constexpr ImU32 P_LARANJA  = IM_COL32(0xff, 0x9a, 0x1f, 255);
constexpr ImU32 P_AZULBANDA= IM_COL32(0x86, 0x96, 0xe6, 255);
constexpr ImU32 P_VERDE    = IM_COL32(0x3c, 0xff, 0x6e, 255);
constexpr ImU32 P_CIANO    = IM_COL32(0x40, 0xc8, 0xff, 255);
constexpr ImU32 P_AMARELO  = IM_COL32(0xff, 0xe0, 0x30, 255);
constexpr ImU32 P_VERMELHO = IM_COL32(0xff, 0x3a, 0x30, 255);
constexpr ImU32 P_BRANCO   = IM_COL32(0xf4, 0xf6, 0xfa, 255);
constexpr ImU32 P_APAGADO  = IM_COL32(0x60, 0x66, 0x70, 255);

// "Papel" -> tela
struct Mapa {
    float ox = 0, oy = 0, k = 1;
    ImVec2 p(float x, float y) const { return ImVec2(ox + x * k, oy + y * k); }
    float d(float v) const { return v * k; }
};
Mapa M;

// Fontes em varios tamanhos (Fontes::painelT / lcdT) e o aumento dos textos
// pequenos em tela pequena (num notebook de 1366 x 768 os rotulos do painel
// ficariam com 5 px - ilegiveis)
ImFont* g_fp[10] = {};
ImFont* g_fl[8] = {};
float g_txt = 1.f;

float tamReal(float tam) { return tam < 12.f ? std::min(tam * g_txt, std::max(tam, 14.f)) : tam; }

ImFont* melhor(ImFont* f, float px)
{
    ImFont** fam = nullptr; int n = 0;
    if (f && f == g_fp[9]) { fam = g_fp; n = 10; }
    else if (f && f == g_fl[7]) { fam = g_fl; n = 8; }
    if (!fam) return f;
    for (int i = 0; i < n; ++i) if (fam[i] && fam[i]->FontSize >= px * 0.96f) return fam[i];
    return fam[n - 1];
}

double agora()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

ImU32 alfa(ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)std::clamp(a, 0, 255) << IM_COL32_A_SHIFT); }

// texto num ponto do papel; alin 0 = esquerda, 0.5 = centro, 1 = direita
void texto(ImFont* f, float tam, float x, float y, ImU32 cor, const char* s, float alin = 0.f)
{
    if (!f || !s) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float px = M.d(tamReal(tam));
    const bool doPainel = f == g_fp[9];
    f = melhor(f, px);
    const ImVec2 ts = f->CalcTextSizeA(px, FLT_MAX, 0, s);
    const ImVec2 q = M.p(x, y);
    const ImVec2 pos(std::floor(q.x - ts.x * alin), std::floor(q.y));
    // sombra escura atras dos rotulos claros: le-se melhor em cima do cinza
    if (doPainel && ((cor >> IM_COL32_A_SHIFT) & 0xff) > 200 && (((cor >> IM_COL32_R_SHIFT) & 0xff) + ((cor >> IM_COL32_G_SHIFT) & 0xff)) > 300)
        dl->AddText(f, px, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), s);
    dl->AddText(f, px, pos, cor, s);
}
float larguraTexto(ImFont* f, float tam, const char* s)
{
    const float px = M.d(tamReal(tam));
    return melhor(f, px)->CalcTextSizeA(px, FLT_MAX, 0, s).x / M.k;
}

// texto de varias linhas centrado numa caixa do papel
void textoCaixa(ImFont* f, float tam, float x, float y, float w, float h, ImU32 cor, const char* s)
{
    std::vector<std::string> linhas;
    std::string a;
    for (const char* c = s; ; ++c) {
        if (*c == '\n' || *c == 0) { linhas.push_back(a); a.clear(); if (!*c) break; }
        else a += *c;
    }
    const float lh = tamReal(tam) * 1.05f;
    float yy = y + (h - lh * linhas.size()) * 0.5f;
    for (auto& l : linhas) { texto(f, tam, x + w * 0.5f, yy, cor, l.c_str(), 0.5f); yy += lh; }
}

void textoCabe(ImFont* f, float tam, float x, float y, ImU32 cor, const char* s, float alin, float maxW)
{
    for (int i = 0; i < 10 && tam > 4.f && larguraTexto(f, tam, s) > maxW; ++i) tam *= 0.92f;
    texto(f, tam, x, y, cor, s, alin);
}

ImVec2 dirAng(float a) { return ImVec2(std::cos(a), std::sin(a)); }
float angKnob(float t) { return (float)((135.0 + 270.0 * std::clamp(t, 0.f, 1.f)) * kPi / 180.0); }

std::string fmtPasso(int p)
{
    char r[24];
    if (p >= 1000) std::snprintf(r, sizeof r, (p % 1000) ? "%.2f kHz" : "%.0f kHz", p / 1000.0);
    else std::snprintf(r, sizeof r, "%d Hz", p);
    return r;
}

// Estados por controle (angulo do arrasto, toque longo ja disparado)
std::unordered_map<ImGuiID, float>& angArrasto() { static std::unordered_map<ImGuiID, float> m; return m; }
std::unordered_map<ImGuiID, int>& longoFeito() { static std::unordered_map<ImGuiID, int> m; return m; }
std::unordered_map<ImGuiID, int>& parteGiro() { static std::unordered_map<ImGuiID, int> m; return m; }

const char* kNomesFil[3] = {"A", "B", "C"};

struct BandaHam { const char* rot; uint64_t lo, hi, padrao; const char* modo; };
const BandaHam kBandasHam[10] = {
    {"1.8",  1800000,  2000000,  1840000, "LSB"},
    {"3.5",  3500000,  4000000,  3710000, "LSB"},
    {"7",    7000000,  7300000,  7100000, "LSB"},
    {"10",  10100000, 10150000, 10120000, "CW"},
    {"14",  14000000, 14350000, 14200000, "USB"},
    {"18",  18068000, 18168000, 18130000, "USB"},
    {"21",  21000000, 21450000, 21250000, "USB"},
    {"24",  24890000, 24990000, 24950000, "USB"},
    {"28",  28000000, 29700000, 28500000, "USB"},
    {"50",  50000000, 54000000, 50125000, "USB"},
};

struct ItemGen { const char* grupo; uint64_t hz; const char* nome; const char* modo; };
const ItemGen kGen1[] = {
    {"Radiodifusão - ondas longas e médias", 198000, "OL - ondas longas", "AM"},
    {nullptr, 1000000, "OM - ondas médias", "AM"},
    {"Radiodifusão - ondas curtas (SWL)", 2400000, "120 m", "AM"},
    {nullptr, 3300000, "90 m", "AM"},
    {nullptr, 3950000, "75 m", "AM"},
    {nullptr, 4900000, "60 m", "AM"},
    {nullptr, 6050000, "49 m", "AM"},
    {nullptr, 7300000, "41 m", "AM"},
    {nullptr, 9650000, "31 m", "AM"},
    {nullptr, 11850000, "25 m", "AM"},
    {nullptr, 13700000, "22 m", "AM"},
    {nullptr, 15400000, "19 m", "AM"},
    {nullptr, 17700000, "16 m", "AM"},
    {nullptr, 18950000, "15 m", "AM"},
    {nullptr, 21600000, "13 m", "AM"},
    {nullptr, 25850000, "11 m", "AM"},
    {"Utilitárias", 5000000, "Hora padrão (5 MHz)", "AM"},
    {nullptr, 10000000, "Hora padrão (10 MHz)", "AM"},
    {nullptr, 6604000, "VOLMET New York (6604 kHz)", "USB"},
    {nullptr, 8414500, "DSC marítimo (8414,5 kHz)", "USB"},
    {nullptr, 27455000, "Faixa do Cidadão, canal 45", "AM"},
};
const ItemGen kGen2[] = {
    {"VHF", 101700000, "FM - radiodifusão (88 a 108)", "WFM"},
    {nullptr, 119450000, "Aviação (118 a 137)", "AM"},
    {nullptr, 144390000, "APRS (144,390)", "NFM"},
    {nullptr, 145570000, "APRS Brasil (145,570)", "NFM"},
    {nullptr, 146000000, "2 m (144 a 148)", "NFM"},
    {nullptr, 156800000, "Marítimo - canal 16", "NFM"},
    {nullptr, 162000000, "AIS (162,000)", "NFM"},
    {"UHF", 261620000, "Satélite militar (UHF)", "NFM"},
    {nullptr, 439400000, "70 cm (430 a 440)", "NFM"},
    {nullptr, 446006250, "PMR 446 - canal 1", "NFM"},
    {nullptr, 462562500, "FRS 462 MHz", "NFM"},
};

// VOICE: fala a frequencia pela voz do Windows (SAPI - existe do 7 ao 11)
std::atomic<bool> g_falando{false};
void falar(const std::wstring& txt)
{
    if (g_falando.exchange(true)) return;
    std::thread([txt] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            static const CLSID kVoz = {0x96749377, 0x3391, 0x11D2, {0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96}};
            static const IID kIVoz = {0x6C44DF74, 0x72B9, 0x4992, {0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4}};
            ISpVoice* v = nullptr;
            if (SUCCEEDED(CoCreateInstance(kVoz, nullptr, CLSCTX_ALL, kIVoz, (void**)&v)) && v) {
                v->Speak(txt.c_str(), 0, nullptr);
                v->Release();
            }
            CoUninitialize();
        }
        g_falando = false;
    }).detach();
}

std::string pastaExe()
{
    char p[MAX_PATH]{};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string d(p);
    const auto pos = d.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : d.substr(0, pos);
}

} // namespace

// ===========================================================================
//  Pecas desenhadas: tecla, botao duplo, encoder, dial
// ===========================================================================

// Tecla do radio. led: 0 sem LED, 1 apagado, 2 verde, 3 laranja, 4 vermelho,
// 5 ambar. Devolve 1 = clique, 2 = toque longo (SEL), 0 = nada.
int Ui::tecla(const char* id, const char* txt, float x, float y, float w, float h, int led,
              const char* acima, const char* dica, unsigned corTxt, bool fkey)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = M.p(x, y), b = M.p(x + w, y + h);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##t", ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)));
    const ImGuiID iid = ImGui::GetItemID();
    const bool sobre = ImGui::IsItemHovered(), ativo = ImGui::IsItemActive();
    int res = 0;
    if (ImGui::IsItemActivated()) longoFeito()[iid] = 0;
    if (ativo && ImGui::GetIO().MouseDownDuration[0] > 0.6f && longoFeito()[iid] == 0) { longoFeito()[iid] = 1; res = 2; }
    if (ImGui::IsItemDeactivated() && sobre && longoFeito()[iid] == 0) res = 1;
    ImGui::PopID();

    const float r = M.d(2.6f);
    dl->AddRectFilled(ImVec2(a.x - M.d(1), a.y - M.d(1)), ImVec2(b.x + M.d(1), b.y + M.d(1.6f)), IM_COL32(0, 0, 0, 200), r + M.d(1));
    ImU32 c1, c2;
    if (fkey) { c1 = ativo ? IM_COL32(0x8a, 0x58, 0x10, 255) : IM_COL32(0xd8, 0x92, 0x26, 255);
                c2 = ativo ? IM_COL32(0x5a, 0x36, 0x08, 255) : IM_COL32(0x9a, 0x5c, 0x10, 255); }
    else      { c1 = ativo ? IM_COL32(0x2a, 0x2c, 0x30, 255) : sobre ? IM_COL32(0x4e, 0x52, 0x58, 255) : IM_COL32(0x42, 0x45, 0x4a, 255);
                c2 = ativo ? IM_COL32(0x1c, 0x1d, 0x20, 255) : IM_COL32(0x26, 0x28, 0x2c, 255); }
    dl->AddRectFilled(a, b, c2, r);
    dl->AddRectFilledMultiColor(ImVec2(a.x + M.d(0.8f), a.y + M.d(0.8f)), ImVec2(b.x - M.d(0.8f), b.y - M.d(0.8f)), c1, c1, c2, c2);
    dl->AddLine(ImVec2(a.x + r, a.y + M.d(0.9f)), ImVec2(b.x - r, a.y + M.d(0.9f)), IM_COL32(255, 255, 255, fkey ? 110 : 40), M.d(0.7f));
    dl->AddRect(a, b, IM_COL32(0x0c, 0x0c, 0x0e, 255), r, 0, M.d(0.8f));
    if (fkey && !ativo)   // brilho ambar das teclas F
        dl->AddRect(ImVec2(a.x - M.d(1.5f), a.y - M.d(1.5f)), ImVec2(b.x + M.d(1.5f), b.y + M.d(1.5f)), IM_COL32(255, 170, 40, 60), r + M.d(1.5f), 0, M.d(1.4f));
    if (led > 0) {
        ImU32 cl = IM_COL32(0x30, 0x36, 0x32, 255);
        if (led == 2) cl = P_VERDE; else if (led == 3) cl = P_LARANJA; else if (led == 4) cl = P_VERMELHO; else if (led == 5) cl = P_AMBAR;
        const float lw = w * 0.42f;
        const ImVec2 la = M.p(x + (w - lw) * 0.5f, y + 1.6f), lb = M.p(x + (w + lw) * 0.5f, y + 3.4f);
        if (led >= 2) dl->AddRectFilled(ImVec2(la.x - M.d(1.5f), la.y - M.d(1.2f)), ImVec2(lb.x + M.d(1.5f), lb.y + M.d(1.2f)), alfa(cl, 70), M.d(1.5f));
        dl->AddRectFilled(la, lb, cl, M.d(0.8f));
    }
    if (txt && *txt) {
        const bool duas = std::strchr(txt, '\n') != nullptr;
        float tam = std::min(h * (duas ? 0.40f : 0.62f), 11.f);
        std::string maior;
        { std::string a; for (const char* c = txt; ; ++c) { if (*c == '\n' || !*c) { if (a.size() > maior.size()) maior = a; a.clear(); if (!*c) break; } else a += *c; } }
        for (int i = 0; i < 12 && tam > 4.f; ++i) {      // encolhe so o que precisa para caber
            const float lim = h * (duas ? 0.47f : 0.80f);
            if (larguraTexto(f_.painel, tam, maior.c_str()) <= w - 4.f && tamReal(tam) <= lim + 0.01f) break;
            tam *= 0.92f;
        }
        textoCaixa(f_.painel, tam, x, y + (led > 0 ? 1.2f : 0.f), w, h,
                   corTxt ? (ImU32)corTxt : (fkey ? IM_COL32(0x20, 0x0e, 0x00, 255) : IM_COL32(0xff, 0xff, 0xff, 255)), txt);
    }
    if (acima && *acima) textoCabe(f_.painel, 7.2f, x + w * 0.5f, y - 10.2f, IM_COL32(0xf4, 0xf4, 0xf4, 255), acima, 0.5f, w + 12.f);
    if (sobre && dica && *dica && !ativo) ImGui::SetTooltip("%s", dica);
    return res;
}

// Botao giratorio duplo (como o AF-RF): o miolo e o anel de fora sao dois
// controles. Arrastar para cima/direita aumenta, a roda tambem. Comecou o
// arrasto em cima do miolo -> mexe no miolo; no anel -> no anel.
bool Ui::giro(const char* id, float cx, float cy, float r, const char* rotE, const char* rotD,
              float tIn, float tOut, float& dIn, float& dOut, const char* dica, bool duplo)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    dIn = dOut = 0;
    const ImVec2 c = M.p(cx, cy);
    const float R = M.d(r);
    for (int i = 0; i <= 10; ++i) {          // escala 0..10
        const float an = angKnob(i / 10.f);
        const ImVec2 d = dirAng(an);
        const float r1 = R * 1.10f, r2 = R * ((i % 2 == 0) ? 1.25f : 1.19f);
        dl->AddLine(ImVec2(c.x + d.x * r1, c.y + d.y * r1), ImVec2(c.x + d.x * r2, c.y + d.y * r2), P_ROTULO2, M.d(0.8f));
        if (i % 2 == 0) {
            char b[4]; std::snprintf(b, sizeof b, "%d", i);
            const float rr = r * 1.45f;
            texto(f_.painel, 5.0f, cx + d.x * rr, cy + d.y * rr - 3.6f, P_ROTULO2, b, 0.5f);
        }
    }
    if (rotE) {                               // rotulo "AF (o) RF"
        const float yr = cy - r * 1.55f - 9.f;
        if (rotD && *rotD) {
            textoCabe(f_.painel, 7.2f, cx - 5.f, yr, IM_COL32(0xf4, 0xf4, 0xf4, 255), rotE, 1.f, r * 2.1f);
            textoCabe(f_.painel, 7.2f, cx + 5.f, yr, IM_COL32(0xf4, 0xf4, 0xf4, 255), rotD, 0.f, r * 2.1f);
            dl->AddCircle(M.p(cx, yr + 3.6f), M.d(2.4f), P_ROTULO, 12, M.d(0.7f));
            dl->AddCircleFilled(M.p(cx, yr + 3.6f), M.d(1.0f), P_ROTULO, 8);
        } else textoCabe(f_.painel, 7.2f, cx, yr, IM_COL32(0xf4, 0xf4, 0xf4, 255), rotE, 0.5f, r * 4.f);
    }
    ImGui::SetCursorScreenPos(ImVec2(c.x - R * 1.15f, c.y - R * 1.15f));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##g", ImVec2(R * 2.3f, R * 2.3f));
    const ImGuiID iid = ImGui::GetItemID();
    const bool sobre = ImGui::IsItemHovered(), ativo = ImGui::IsItemActive();
    ImGui::PopID();
    const float ri = duplo ? R * 0.62f : R * 0.88f;
    const float dist = std::sqrt((io.MousePos.x - c.x) * (io.MousePos.x - c.x) + (io.MousePos.y - c.y) * (io.MousePos.y - c.y));
    if (ImGui::IsItemActivated()) parteGiro()[iid] = (!duplo || dist <= ri) ? 0 : 1;
    if (ativo) {
        const float dv = (-io.MouseDelta.y + io.MouseDelta.x) / M.d(150.f);
        if (parteGiro()[iid] == 0) dIn = dv; else dOut = dv;
    }
    if (sobre && io.MouseWheel != 0) {
        const float dv = io.MouseWheel * 0.02f;
        if (!duplo || dist <= ri) dIn = dv; else dOut = dv;
    }
    // anel de fora (serrilhado)
    dl->AddCircleFilled(ImVec2(c.x + M.d(1.2f), c.y + M.d(2.f)), R, IM_COL32(0, 0, 0, 150), 48);
    dl->AddCircleFilled(c, R, IM_COL32(0x16, 0x17, 0x19, 255), 48);
    const float rotFora = angKnob(tOut);
    for (int k = 0; k < 40; ++k) {
        const float an = rotFora + k * (float)(2 * kPi / 40);
        const ImVec2 d = dirAng(an);
        dl->AddLine(ImVec2(c.x + d.x * R * (duplo ? 0.80f : 0.93f), c.y + d.y * R * (duplo ? 0.80f : 0.93f)),
                    ImVec2(c.x + d.x * R * 0.985f, c.y + d.y * R * 0.985f), IM_COL32(0x34, 0x36, 0x3a, 255), M.d(0.9f));
    }
    dl->AddCircle(c, R, IM_COL32(0x4a, 0x4c, 0x50, 255), 48, M.d(0.8f));
    if (duplo) {
        const ImVec2 d = dirAng(rotFora);
        dl->AddLine(ImVec2(c.x + d.x * R * 0.70f, c.y + d.y * R * 0.70f), ImVec2(c.x + d.x * R * 0.98f, c.y + d.y * R * 0.98f),
                    (ativo && parteGiro()[iid] == 1) ? P_LARANJA : IM_COL32(0xe0, 0xe0, 0xe0, 255), M.d(1.6f));
    }
    // miolo brilhante
    dl->AddCircleFilled(ImVec2(c.x + M.d(0.8f), c.y + M.d(1.2f)), ri, IM_COL32(0, 0, 0, 160), 40);
    dl->AddCircleFilled(c, ri, IM_COL32(0x0e, 0x0f, 0x11, 255), 40);
    dl->AddCircleFilled(ImVec2(c.x - ri * 0.18f, c.y - ri * 0.24f), ri * 0.72f, IM_COL32(255, 255, 255, 14), 32);
    dl->AddCircleFilled(ImVec2(c.x - ri * 0.26f, c.y - ri * 0.34f), ri * 0.38f, IM_COL32(255, 255, 255, 16), 24);
    dl->AddCircle(c, ri, IM_COL32(0x50, 0x53, 0x58, 255), 40, M.d(0.9f));
    {
        const ImVec2 d = dirAng(angKnob(tIn));
        const ImVec2 pd(c.x + d.x * ri * 0.70f, c.y + d.y * ri * 0.70f);
        dl->AddCircleFilled(pd, std::max(1.5f, ri * 0.12f), (ativo && parteGiro()[iid] == 0) ? P_LARANJA : IM_COL32(0xff, 0x30, 0x30, 255), 12);
    }
    if ((sobre || ativo) && dica) ImGui::SetTooltip("%s", dica);
    return dIn != 0 || dOut != 0;
}

// Encoder sem fim (RIT/XIT, MULTI/CH): gira arrastando em volta; cada 15 graus
// e um passo. Roda: um passo por dente. Devolve os passos do quadro.
int Ui::encoder(const char* id, float cx, float cy, float r, float& ang, const char* dica, bool* clique)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 c = M.p(cx, cy);
    const float R = M.d(r);
    ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##e", ImVec2(R * 2, R * 2));
    const ImGuiID iid = ImGui::GetItemID();
    const bool sobre = ImGui::IsItemHovered(), ativo = ImGui::IsItemActive();
    ImGui::PopID();
    int passos = 0;
    static std::unordered_map<ImGuiID, float> resto;
    static std::unordered_map<ImGuiID, float> andou;
    const float aM = std::atan2(io.MousePos.y - c.y, io.MousePos.x - c.x);
    if (ImGui::IsItemActivated()) { angArrasto()[iid] = aM; andou[iid] = 0; }
    if (ativo) {
        float d = aM - angArrasto()[iid];
        while (d > kPi) d -= (float)(2 * kPi);
        while (d < -kPi) d += (float)(2 * kPi);
        angArrasto()[iid] = aM;
        ang += d;
        andou[iid] += std::fabs(d);
        resto[iid] += d;
        const float passo = (float)(15.0 * kPi / 180.0);
        while (resto[iid] >= passo) { ++passos; resto[iid] -= passo; }
        while (resto[iid] <= -passo) { --passos; resto[iid] += passo; }
    }
    if (ImGui::IsItemDeactivated() && clique && andou[iid] < 0.08f && sobre) *clique = true;
    if (sobre && io.MouseWheel != 0) {
        int n = (int)std::lround(io.MouseWheel);
        if (n == 0) n = io.MouseWheel > 0 ? 1 : -1;
        passos += n; ang += n * 0.26f;
    }
    dl->AddCircleFilled(ImVec2(c.x + M.d(1.5f), c.y + M.d(2.5f)), R, IM_COL32(0, 0, 0, 150), 48);
    dl->AddCircleFilled(c, R, IM_COL32(0x15, 0x16, 0x18, 255), 48);
    for (int k = 0; k < 48; ++k) {
        const ImVec2 d = dirAng(ang + k * (float)(2 * kPi / 48));
        dl->AddLine(ImVec2(c.x + d.x * R * 0.86f, c.y + d.y * R * 0.86f), ImVec2(c.x + d.x * R * 0.99f, c.y + d.y * R * 0.99f),
                    IM_COL32(0x38, 0x3a, 0x3e, 255), M.d(0.9f));
    }
    const float ri = R * 0.8f;
    dl->AddCircleFilled(c, ri, IM_COL32(0x0f, 0x10, 0x12, 255), 40);
    dl->AddCircleFilled(ImVec2(c.x - ri * 0.2f, c.y - ri * 0.25f), ri * 0.7f, IM_COL32(255, 255, 255, 13), 32);
    dl->AddCircleFilled(ImVec2(c.x - ri * 0.3f, c.y - ri * 0.36f), ri * 0.32f, IM_COL32(255, 255, 255, 18), 24);
    dl->AddCircle(c, ri, IM_COL32(0x48, 0x4a, 0x4e, 255), 40, M.d(0.8f));
    dl->PathArcTo(c, R * 1.18f, (float)(-kPi * 0.85), (float)(-kPi * 0.15), 24);
    dl->PathStroke(P_ROTULO2, 0, M.d(0.8f));
    if (ativo) dl->AddCircle(c, R * 1.04f, alfa(P_LARANJA, 160), 48, M.d(1.2f));
    if ((sobre || ativo) && dica) ImGui::SetTooltip("%s", dica);
    return passos;
}

// Dial principal: disco grande com o furinho do dedo. Arrastar em volta
// sintoniza (100 passos por volta, com aceleracao se girar rapido).
int Ui::dial(const char* id, float cx, float cy, float r, float& ang, bool travado, const char* dica)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 c = M.p(cx, cy);
    const float R = M.d(r);
    ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##d", ImVec2(R * 2, R * 2));
    const ImGuiID iid = ImGui::GetItemID();
    const bool sobre = ImGui::IsItemHovered(), ativo = ImGui::IsItemActive();
    ImGui::PopID();
    int passos = 0;
    static std::unordered_map<ImGuiID, float> resto;
    const float aM = std::atan2(io.MousePos.y - c.y, io.MousePos.x - c.x);
    if (ImGui::IsItemActivated()) angArrasto()[iid] = aM;
    if (ativo && !travado) {
        float d = aM - angArrasto()[iid];
        while (d > kPi) d -= (float)(2 * kPi);
        while (d < -kPi) d += (float)(2 * kPi);
        angArrasto()[iid] = aM;
        ang += d;
        // aceleracao: girando rapido o passo multiplica
        const float vel = std::fabs(d) / std::max(0.001f, io.DeltaTime) / (float)(2 * kPi);
        const float mult = vel > 2.0f ? 10.f : vel > 0.8f ? 4.f : 1.f;
        resto[iid] += d * mult;
        const float passo = (float)(2 * kPi / 100.0);
        while (resto[iid] >= passo) { ++passos; resto[iid] -= passo; }
        while (resto[iid] <= -passo) { --passos; resto[iid] += passo; }
    }
    if (sobre && io.MouseWheel != 0 && !travado) {
        int n = (int)std::lround(io.MouseWheel * 5.f);
        if (n == 0) n = io.MouseWheel > 0 ? 5 : -5;
        passos += n; ang += n * (float)(2 * kPi / 100.0);
    }
    // saia serrilhada
    dl->AddCircleFilled(ImVec2(c.x + M.d(3), c.y + M.d(5)), R * 1.01f, IM_COL32(0, 0, 0, 170), 96);
    dl->AddCircleFilled(c, R, IM_COL32(0x12, 0x13, 0x15, 255), 96);
    for (int k = 0; k < 120; ++k) {
        const ImVec2 d = dirAng(ang + k * (float)(2 * kPi / 120));
        dl->AddLine(ImVec2(c.x + d.x * R * 0.86f, c.y + d.y * R * 0.86f), ImVec2(c.x + d.x * R * 0.995f, c.y + d.y * R * 0.995f),
                    IM_COL32(0x30, 0x32, 0x36, 255), M.d(1.0f));
    }
    dl->AddCircle(c, R * 0.995f, IM_COL32(0x46, 0x48, 0x4c, 255), 96, M.d(1.0f));
    // disco liso com reflexo diagonal (aluminio escovado)
    const float rd = R * 0.84f;
    dl->AddCircleFilled(c, rd, IM_COL32(0x0b, 0x0c, 0x0d, 255), 96);
    for (int i = 0; i < 16; ++i) {           // varias camadas fracas: o reflexo fica liso
        const float w = rd * (0.03f + 0.028f * i);
        const int al = 7;
        const ImVec2 d = dirAng((float)(-kPi * 0.20));
        const ImVec2 n(-d.y, d.x);
        const float L = rd * 0.92f;
        dl->AddQuadFilled(ImVec2(c.x - d.x * L - n.x * w, c.y - d.y * L - n.y * w),
                          ImVec2(c.x + d.x * L - n.x * w, c.y + d.y * L - n.y * w),
                          ImVec2(c.x + d.x * L + n.x * w, c.y + d.y * L + n.y * w),
                          ImVec2(c.x - d.x * L + n.x * w, c.y - d.y * L + n.y * w), IM_COL32(255, 255, 255, al));
    }
    dl->AddCircle(c, rd, IM_COL32(0x3c, 0x3e, 0x42, 255), 96, M.d(1.2f));
    {   // furinho do dedo (gira com o dial)
        const ImVec2 d = dirAng(ang - (float)(kPi * 0.75));
        const ImVec2 f(c.x + d.x * rd * 0.58f, c.y + d.y * rd * 0.58f);
        const float rf = rd * 0.16f;
        dl->AddCircleFilled(f, rf, IM_COL32(0x05, 0x05, 0x06, 255), 32);
        dl->AddCircleFilled(ImVec2(f.x + rf * 0.25f, f.y + rf * 0.3f), rf * 0.7f, IM_COL32(255, 255, 255, 18), 24);
        dl->AddCircle(f, rf, IM_COL32(0x40, 0x42, 0x46, 255), 32, M.d(0.8f));
    }
    if (travado) texto(f_.painel, 8.f, cx, cy - 5.f, alfa(P_VERMELHO, 200), "LOCK", 0.5f);
    if (ativo) dl->AddCircle(c, R * 1.015f, alfa(P_LARANJA, 120), 96, M.d(1.5f));
    if (sobre && dica && !ativo) ImGui::SetTooltip("%s", dica);
    return travado ? 0 : passos;
}

// ===========================================================================
//  Logica do painel
// ===========================================================================
int Ui::grupoModo(const std::string& m) const
{
    if (m == "LSB" || m == "USB") return 0;
    if (m == "CW") return 1;
    if (m == "AM") return 2;
    if (m == "WFM") return 4;
    return 3;     // FM / NFM
}

std::string Ui::fmtFreqLcd(uint64_t hz) const
{
    char b[32];
    const unsigned long long mhz = hz / 1000000, khz = hz / 1000 % 1000, h = hz % 1000;
    std::snprintf(b, sizeof b, "%llu.%03llu.%03llu", mhz, khz, h);
    return b;
}

void Ui::aviso(const std::string& t, double seg) { avisoPainel_ = t; avisoAte_ = agora() + seg; }

void Ui::sincronizarAtivo()
{
    Receptor& R = rx_[ativo_];
    R.f = r_.vfo();
    R.modo = r_.modo();
    // o squelch automatico (VOX) mexe no sql_ direto: guarda no receptor
    if (sqlAplicado_ > -900 && sql_ != sqlAplicado_) R.sql = sql_;
}

// Aplica a cada quadro o que e do receptor ativo (barato: so muda o que mudou)
void Ui::aplicarReceptor()
{
    Receptor& R = rx_[ativo_];
    vol_ = R.af;
    sql_ = R.sql; sqlAplicado_ = sql_;
    mudo_ = R.mute;
    nb_ = R.nb1 ? nbNivel_ : 0.f;
    nr_ = R.nr1 ? nrNivel_ : 0.f;
    nrEsp_ = R.nr2;
    r_.setNb2(R.nb2, nb2Nivel_);
    AjustesDsp::g().agc = R.agcOff ? 0 : std::clamp(R.agc, 1, 3);
    // RF: so manda ao dongle quando o botao mexeu (a CONFIGURACAO tambem ajusta)
    if (R.rf != rfAplicado_) {
        rfAplicado_ = R.rf;
        ganhoRf_ = 100.f - R.rf;
        r_.setGanho((int)std::lround(4.96f * R.rf));
    }
    r_.setGanhoExtraDb(-6.f * att_ + 10.f * pamp_);
    // CW: SHIFT desloca a escuta e compensa no tom - a batida continua no PITCH
    const int g = grupoModo(r_.modo());
    int shift = 0;
    if (g == 1) shift = (int)std::lround(filLo_[1][R.fil] / 10.f) * 10;
    AjustesDsp::g().cwTomHz = std::clamp(cwPitch_ + (float)shift, 250.f, 1400.f);
    r_.setRit((R.ritOn ? R.rit : 0) + shift);
    // largura mexida fora do painel (arrasto da borda no espectro, decoders)
    if (g != grupoAplicado_) { grupoAplicado_ = g; bwAplicado_ = r_.banda(); }
    else if (r_.banda() != bwAplicado_) {
        const int bw = r_.banda();
        filHi_[g][R.fil] = (float)(g == 2 ? bw / 2 : bw);
        bwAplicado_ = bw;
    }
}

void Ui::aplicarFiltro()
{
    const Receptor& R = rx_[ativo_];
    const int g = grupoModo(r_.modo());
    const float hi = filHi_[g][R.fil], lo = filLo_[g][R.fil];
    int bw = (int)std::lround(hi);
    int loCut = 250;
    if (g == 0) loCut = (int)std::lround(lo);
    else if (g == 2) { bw = (int)std::lround(hi * 2); loCut = (int)std::lround(lo); }
    bw = std::max(50, bw);
    r_.setBanda(bw);
    AjustesDsp::g().loCutHz = std::clamp(loCut, 0, 1500);
    bwAplicado_ = bw; grupoAplicado_ = g;
}

void Ui::ativar(int i)
{
    if (i == ativo_ || i < 0 || i > 1) return;
    sincronizarAtivo();
    ativo_ = i;
    const Receptor& R = rx_[i];
    if (R.modo != r_.modo()) r_.setModo(R.modo);
    sintonizar(R.f, false);
    rfAplicado_ = -1;             // o RF deste receptor vale agora
    aplicarFiltro();
    muteAte_ = agora() + 0.25;
}

void Ui::mudarModoPainel(const std::string& m)
{
    if (m != r_.modo()) { r_.setModo(m); muteAte_ = agora() + 0.25; }
    rx_[ativo_].modo = m;
    aplicarFiltro();
    if (zoom_ > 0) centrarParaZoom(true);
}

// Roda do mouse em cima de um digito de uma frequencia do painel: aquele
// digito sobe/desce (como no RXSDR). s e o texto desenhado alinhado a direita
// em xDir; pesoUltimo = quanto vale o ultimo digito (1 Hz, ou 1 kHz quando a
// parte dos Hz e desenhada separada). O digito embaixo do mouse fica sublinhado.
void Ui::rodaDigitos(ImFont* f, float tam, float xDir, float y, const std::string& s, int rx, uint64_t pesoUltimo)
{
    if (!ImGui::IsWindowHovered()) return;          // outra janela (decoders, menu) por cima
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float xr = xDir;
    uint64_t peso = pesoUltimo;
    for (int j = (int)s.size() - 1; j >= 0; --j) {
        const char c[2] = {s[(size_t)j], 0};
        const float wc = larguraTexto(f, tam, c);
        if (c[0] >= '0' && c[0] <= '9') {
            const float tr = tamReal(tam);              // o mesmo tamanho que o texto() usou
            const ImVec2 a = M.p(xr - wc, y + tr * 0.08f), b = M.p(xr, y + tr * 0.92f);
            if (ImGui::IsMouseHoveringRect(a, b, false)) {
                dl->AddLine(ImVec2(a.x + 1, b.y + M.d(0.6f)), ImVec2(b.x - 1, b.y + M.d(0.6f)), P_CIANO, std::max(1.5f, M.d(1.f)));
                if (io.MouseWheel != 0) {
                    int n = (int)std::lround(io.MouseWheel);
                    if (n == 0) n = io.MouseWheel > 0 ? 1 : -1;
                    mudarFreqRx(rx, (int64_t)n * (int64_t)peso);
                    io.MouseWheel = 0;                  // a roda e deste digito (nao rola mais nada)
                }
            }
            if (peso < 1000000000ULL) peso *= 10;
        }
        xr -= wc;
    }
}

void Ui::mudarFreqRx(int rx, int64_t d)
{
    if (rx < 0 || rx > 1 || d == 0) return;
    Receptor& R = rx_[rx];
    if (R.lock || R.memModo) return;
    const uint64_t f = rx == ativo_ ? r_.vfo() : R.f;
    const int64_t nf = (int64_t)f + d;
    if (nf < 1000 || nf > 2000000000LL) return;
    if (rx == ativo_) sintonizar((uint64_t)nf, false);
    R.f = (uint64_t)nf;
}

// M/S: so a "banda" (frequencia, modo, filtro, memoria) troca de lado; os
// knobs e teclas de cada coluna (AF/RF, SQL, NB, NR, NOTCH, AGC, MUTE...) sao
// fisicos e continuam no lugar - antes o squelch fechado de um ia para o outro
void Ui::trocarMainSub()
{
    sincronizarAtivo();
    std::swap(rx_[0].f, rx_[1].f);
    std::swap(rx_[0].modo, rx_[1].modo);
    std::swap(rx_[0].fil, rx_[1].fil);
    std::swap(rx_[0].memModo, rx_[1].memModo);
    std::swap(rx_[0].memCanal, rx_[1].memCanal);
    r_.setModo(rx_[ativo_].modo);
    sintonizar(rx_[ativo_].f, false);
    aplicarFiltro();
    muteAte_ = agora() + 0.25;
}

// Gira um dial: delta em passos (10 Hz, ou 1 Hz com FINE; mais largos em AM/FM)
void Ui::sintonizarRx(int i, int64_t delta)
{
    if (delta == 0) return;
    Receptor& R = rx_[i];
    if (R.lock) return;
    if (R.memModo) {          // em memoria o dial passa os canais
        if (!mem_.empty()) {
            const int64_t n = (int64_t)mem_.size();
            R.memCanal = (int)((((int64_t)R.memCanal + delta) % n + n) % n);
            if (i == ativo_) sintonizarMemoria((size_t)R.memCanal);
            else { R.f = mem_[R.memCanal].f; R.modo = mem_[R.memCanal].modo; }
        }
        return;
    }
    const std::string md = (i == ativo_) ? r_.modo() : R.modo;
    const int g = grupoModo(md);
    int64_t passo = R.fine ? 1 : (g == 2 ? 100 : 10);
    if (g == 3) passo = R.fine ? 10 : 500;
    if (g == 4) passo = R.fine ? 100 : 5000;
    const int64_t df = delta * passo;
    auto mover = [&](int k, int64_t d) {
        Receptor& X = rx_[k];
        if (k == ativo_) {
            const int64_t nf = std::max<int64_t>(1000, (int64_t)r_.vfo() + d);
            sintonizar((uint64_t)nf, false);
            X.f = (uint64_t)nf;
            if (modoCentro_ && std::llabs((int64_t)r_.centro() - nf) > (int64_t)(r_.taxa() / (1.0 + zoom_) * 0.12))
                r_.centralizar(r_.centroSemDc((uint64_t)nf));
        } else {
            X.f = (uint64_t)std::max<int64_t>(1000, (int64_t)X.f + d);
        }
    };
    mover(i, df);
    if (tracking_) mover(1 - i, df);
}

void Ui::irBanda(int b)
{
    if (b < 0 || b >= 10) return;
    Receptor& R = rx_[ativo_];
    if (bandaMem_.size() < 10) bandaMem_.resize(10, Rapida{0, ""});
    const uint64_t f = r_.vfo();       // guarda onde estava, se estava numa banda
    for (int k = 0; k < 10; ++k)
        if (f >= kBandasHam[k].lo && f <= kBandasHam[k].hi) bandaMem_[k] = {f, r_.modo()};
    R.memModo = false;
    const Rapida& m = bandaMem_[b];
    const uint64_t alvo = m.f ? m.f : kBandasHam[b].padrao;
    const std::string modo = m.modo.empty() ? std::string(kBandasHam[b].modo) : m.modo;
    Config::instance().set("qmode", std::string("auto"));
    mudarModoPainel(modo);
    sintonizar(alvo, false);
    muteAte_ = agora() + 0.25;
}

void Ui::digito(int d)
{
    if (!entrando_) { entrando_ = true; entrada_.clear(); }
    if (entrada_.size() < 8) entrada_ += (char)('0' + d);
}

void Ui::confirmarEntrada()
{
    entrando_ = false;
    if (entrada_.empty()) return;
    const double khz = std::atof(entrada_.c_str());
    entrada_.clear();
    if (khz < 1 || khz > 2000000) { aviso("Frequência fora da faixa"); return; }
    rx_[ativo_].memModo = false;
    sintonizar((uint64_t)std::llround(khz * 1000.0), false);
}

// CW T. (sintonia automatica): acha o pico perto do VFO e poe o VFO nele
void Ui::cwTune()
{
    const int nb = (int)fft_.bins.size();
    if (nb < 16 || !r_.aoVivo() || suave_.size() != (size_t)nb) { aviso("Sem sinal para sintonizar"); return; }
    const std::string m = r_.modo();
    if (m == "USB" || m == "LSB") { aviso("CW T.: use em CW, AM ou FM"); return; }
    const double sr = fft_.taxa ? fft_.taxa : r_.taxa();
    const double vfo = (double)r_.vfo();
    const double janela = (m == "CW") ? 800.0 : std::max(1500.0, r_.banda() * 0.6);
    auto bin = [&](double f) { return (int)std::lround((f - (double)fft_.centro) / sr * nb + nb / 2.0); };
    const int a = std::max(1, bin(vfo - janela)), b = std::min(nb - 2, bin(vfo + janela));
    if (b <= a) return;
    int mx = a; float pv = -200;
    for (int i = a; i <= b; ++i) {
        if (std::abs(i - nb / 2) <= 1) continue;               // o DC nao e estacao
        if (suave_[i] > pv) { pv = suave_[i]; mx = i; }
    }
    double off = 0;                    // refina pelo vertice da parabola
    const double y0 = suave_[mx - 1], y1 = suave_[mx], y2 = suave_[mx + 1];
    const double den = y0 - 2 * y1 + y2;
    if (std::fabs(den) > 1e-6) off = std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
    const double f = (double)fft_.centro + ((mx + off) - nb / 2.0) * sr / nb;
    sintonizar((uint64_t)std::llround(f), false);
    aviso("CW T.: sintonizado no pico");
}

void Ui::falarFrequencia(int i)
{
    const uint64_t f = (i == ativo_) ? r_.vfo() : rx_[i].f;
    const std::string md = (i == ativo_) ? r_.modo() : rx_[i].modo;
    const std::string s = fmtFreqLcd(f);
    std::wstring w;
    for (char c : s) {
        if (c == '.') w += emIngles() ? L" point " : L" ponto ";
        else { w += (wchar_t)c; w += L' '; }
    }
    w += L", ";
    for (char c : md) { w += (wchar_t)c; w += L' '; }
    falar(w);
}

void Ui::tocarUltimaGravacao()
{
    std::string arq = ultimaGravacao_;
    if (arq.empty()) {      // a mais nova da pasta Gravacoes
        WIN32_FIND_DATAA fd;
        const std::string pasta = pastaExe() + "\\Gravacoes\\";
        HANDLE h = FindFirstFileA((pasta + "*.wav").c_str(), &fd);
        FILETIME melhor{}; std::string nome;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (CompareFileTime(&fd.ftLastWriteTime, &melhor) > 0) { melhor = fd.ftLastWriteTime; nome = fd.cFileName; }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        if (!nome.empty()) arq = pasta + nome;
    }
    if (arq.empty()) { aviso("Nenhuma gravação ainda (REC grava)"); return; }
    if (r_.som().gravando()) { aviso("Pare a gravação antes (STOP)"); return; }
    PlaySoundA(arq.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    aviso("PLAY: " + arq.substr(arq.find_last_of('\\') + 1), 4);
}

// ---------------------------------------------------------------------------
//  Guardar / ler o estado do painel no SDR990S.ini
// ---------------------------------------------------------------------------
void Ui::carregarPainel()
{
    auto& c = Config::instance();
    // filtros padrao (A / B / C) por grupo: SSB, CW, AM, FM, WFM
    const float hi0[5][3] = {{2700, 2400, 3600}, {500, 250, 1200}, {4000, 3000, 5000}, {12500, 10000, 25000}, {200000, 150000, 250000}};
    // instalacao nova: SSB FIL A = HI 2,7 kHz / LO 200 Hz (knob HI/SHIFT-LO/WIDTH em ~4 e 2)
    const float lo0[5][3] = {{200, 400, 100}, {0, 0, 0}, {200, 250, 100}, {0, 0, 0}, {0, 0, 0}};
    for (int g = 0; g < 5; ++g)
        for (int k = 0; k < 3; ++k) {
            char ch[32]; std::snprintf(ch, sizeof ch, "fil_%d_%d", g, k);
            const std::string v = c.str(ch);
            float h = hi0[g][k], l = lo0[g][k];
            if (!v.empty()) std::sscanf(v.c_str(), "%f,%f", &h, &l);
            filHi_[g][k] = h; filLo_[g][k] = l;
        }
    for (int i = 0; i < 2; ++i) {
        Receptor& R = rx_[i];
        R.rf = std::clamp(c.gainTenths() / 4.96f, 0.f, 100.f);
        R.af = vol_;
        if (i == 1) { R.f = 14200000; R.modo = "USB"; }
        const std::string v = c.str(i == 0 ? "rx_main" : "rx_sub");
        if (!v.empty()) {
            std::stringstream ss(v);
            std::string t; std::vector<std::string> p;
            while (std::getline(ss, t, ',')) p.push_back(t);
            if (p.size() >= 24) {
                size_t k = 0;
                auto bo = [&]() { return p[k++] == "1"; };
                R.f = std::strtoull(p[k++].c_str(), nullptr, 10); R.modo = p[k++];
                R.fil = std::clamp(std::atoi(p[k++].c_str()), 0, 2);
                R.af = (float)std::atof(p[k++].c_str()); R.rf = (float)std::atof(p[k++].c_str()); R.sql = (float)std::atof(p[k++].c_str());
                R.nb1 = bo(); R.nb2 = bo(); R.nr1 = bo(); R.nr2 = bo(); R.nch = bo(); R.nchLargo = bo();
                R.bef = bo(); R.anch = bo(); R.bc = bo(); R.apf = bo(); R.mute = bo();
                R.notch = (float)std::atof(p[k++].c_str());
                R.agc = std::clamp(std::atoi(p[k++].c_str()), 1, 3); R.agcOff = bo();
                R.rit = std::atoi(p[k++].c_str()); R.ritOn = bo();
                if (k < p.size()) R.lock = bo();
                if (k < p.size()) R.fine = bo();
            }
        }
        if (v.empty()) R.nr2 = true;          // instalacao nova: NR ESPECTRAL ligado (forca 10)
        if (R.f < 1000) R.f = 7100000;
        if (R.modo.empty()) R.modo = "LSB";
    }
    rx_[0].f = r_.vfo(); rx_[0].modo = r_.modo();      // o MAIN e o que o radio tinha
    ativo_ = 0;
    txVfo_ = (int)std::clamp(c.num("p_tx", 0), 0LL, 1LL);
    att_ = (int)std::clamp(c.num("p_att", 0), 0LL, 3LL);
    pamp_ = (int)std::clamp(c.num("p_pamp", 0), 0LL, 2LL);
    medidorModo_ = (int)std::clamp(c.num("p_meter", 0), 0LL, 2LL);
    dim_ = (int)std::clamp(c.num("p_dim", 0), 0LL, 3LL);
    sDisp_ = (int)std::clamp(c.num("p_sdisp", 0), 0LL, 2LL);
    scp_ = (int)std::clamp(c.num("p_scp", 0), 0LL, 2LL);
    modoCentro_ = c.flag("p_centro", false);
    tracking_ = c.flag("p_track", false);
    proc_ = c.flag("p_proc", false);
    nbNivel_ = (float)c.dbl("p_nb1", 40); nb2Nivel_ = (float)c.dbl("p_nb2", 0.5);
    nrNivel_ = (float)c.dbl("p_nr1", 40);
    procNivel_ = (float)c.dbl("p_procn", 0.5);
    cwPitch_ = (float)std::clamp(c.dbl("p_pitch", 700), 300.0, 1000.0);
    sqlMargem_ = (float)c.dbl("p_sqlm", 2); sqlHang_ = (float)c.dbl("p_sqlh", 0.3);
    paleta_ = (int)std::clamp(c.num("p_paleta", 0), 0LL, 3LL); g_paletaCachoeira = paleta_;
    rxeq_ = (int)std::clamp(c.num("p_rxeq", 0), 0LL, 4LL);
    painelFrac_ = (float)std::clamp(c.dbl("p_painel_frac", 0.66), 0.3, 1.0);
    if (c.str("p_if_inicial").empty()) { ifOn_ = true; c.set("p_if_inicial", std::string("1")); }   // 1a vez: IF DISPLAY ligado embaixo
    fracBaixo_ = (float)std::clamp(c.dbl("p_esp_baixo", 0.35), 0.1, 0.85);
    bandaMem_.assign(10, Rapida{0, ""});
    for (int b = 0; b < 10; ++b) {
        char ch[16]; std::snprintf(ch, sizeof ch, "banda_%d", b);
        const std::string v = c.str(ch);
        const auto p = v.find(',');
        if (p != std::string::npos) bandaMem_[b] = {std::strtoull(v.c_str(), nullptr, 10), v.substr(p + 1)};
    }
    qm_.clear();
    for (int q = 0; q < 10; ++q) {
        char ch[16]; std::snprintf(ch, sizeof ch, "qm_%d", q);
        const std::string v = c.str(ch);
        const auto p = v.find(',');
        if (p != std::string::npos) qm_.push_back({std::strtoull(v.c_str(), nullptr, 10), v.substr(p + 1)});
    }
    aplicarFiltro();
}

void Ui::salvarPainel()
{
    auto& c = Config::instance();
    sincronizarAtivo();
    for (int g = 0; g < 5; ++g)
        for (int k = 0; k < 3; ++k) {
            char ch[32], v[64];
            std::snprintf(ch, sizeof ch, "fil_%d_%d", g, k);
            std::snprintf(v, sizeof v, "%.0f,%.0f", filHi_[g][k], filLo_[g][k]);
            c.set(ch, std::string(v));
        }
    for (int i = 0; i < 2; ++i) {
        const Receptor& R = rx_[i];
        std::ostringstream o;
        auto b = [](bool x) { return x ? "1" : "0"; };
        o << R.f << ',' << R.modo << ',' << R.fil << ',' << R.af << ',' << R.rf << ',' << R.sql << ','
          << b(R.nb1) << ',' << b(R.nb2) << ',' << b(R.nr1) << ',' << b(R.nr2) << ',' << b(R.nch) << ',' << b(R.nchLargo) << ','
          << b(R.bef) << ',' << b(R.anch) << ',' << b(R.bc) << ',' << b(R.apf) << ',' << b(R.mute) << ','
          << R.notch << ',' << R.agc << ',' << b(R.agcOff) << ',' << R.rit << ',' << b(R.ritOn) << ','
          << b(R.lock) << ',' << b(R.fine);
        c.set(i == 0 ? "rx_main" : "rx_sub", o.str());
    }
    // o radio volta a abrir no MAIN
    if (ativo_ == 1) { c.set("vfo", (long long)rx_[0].f); c.set("modo", rx_[0].modo); }
    c.set("p_tx", txVfo_); c.set("p_att", att_); c.set("p_pamp", pamp_); c.set("p_meter", medidorModo_);
    c.set("p_dim", dim_); c.set("p_sdisp", sDisp_); c.set("p_scp", scp_);
    c.set("p_centro", modoCentro_); c.set("p_track", tracking_); c.set("p_proc", proc_);
    c.set("p_nb1", (double)nbNivel_); c.set("p_nb2", (double)nb2Nivel_); c.set("p_nr1", (double)nrNivel_);
    c.set("p_procn", (double)procNivel_); c.set("p_pitch", (double)cwPitch_);
    c.set("p_sqlm", (double)sqlMargem_); c.set("p_sqlh", (double)sqlHang_);
    c.set("p_paleta", paleta_); c.set("p_rxeq", rxeq_);
    c.set("p_painel_frac", (double)painelFrac_); c.set("p_esp_baixo", (double)fracBaixo_);
    for (int b = 0; b < 10 && b < (int)bandaMem_.size(); ++b) {
        if (!bandaMem_[b].f) continue;
        char ch[16]; std::snprintf(ch, sizeof ch, "banda_%d", b);
        c.set(ch, std::to_string(bandaMem_[b].f) + "," + bandaMem_[b].modo);
    }
    for (int q = 0; q < (int)qm_.size(); ++q) {
        char ch[16]; std::snprintf(ch, sizeof ch, "qm_%d", q);
        c.set(ch, std::to_string(qm_[q].f) + "," + qm_[q].modo);
    }
}

// ===========================================================================
//  O painel inteiro
// ===========================================================================
void Ui::painel()
{
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    // O radio fica no alto; embaixo, a cachoeira grande com o IF DISPLAY (como o
    // RXSDR). A barra entre os dois e arrastavel; em cima de tudo = so o radio.
    const float divH = std::max(6.f, 7.f * s_);
    float hPainel = std::min(W * kLH / kLW, H * std::clamp(painelFrac_, 0.3f, 1.0f));
    baixoAtivo_ = (H - hPainel - divH) >= 90.f * std::max(1.f, s_);
    if (!baixoAtivo_) hPainel = std::min(W * kLH / kLW, H);
    M.k = hPainel / kLH;
    M.ox = std::floor((W - kLW * M.k) * 0.5f);
    M.oy = baixoAtivo_ ? 0.f : std::floor((H - kLH * M.k) * 0.5f);
    for (int i = 0; i < 10; ++i) g_fp[i] = f_.painelT[i] ? f_.painelT[i] : f_.painel;
    for (int i = 0; i < 8; ++i) g_fl[i] = f_.lcdT[i] ? f_.lcdT[i] : f_.lcd;
    g_txt = std::clamp(1.50f / M.k, 1.0f, 1.65f);

    sincronizarAtivo();
    aplicarReceptor();
    if (sleepAte_ > 0 && agora() > sleepAte_) {
        sleepAte_ = 0;
        std::string e; r_.ligar(false, e);
        aviso("TIMER: rádio desligado", 5);
    }
    if (scp_ == 2) {                 // tela cheia do Band Scope (monitor externo)
        telaCheia();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) scp_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_F12)) g_pedirCaptura = true;
        return;
    }

    if (baixoAtivo_) {
        const float yb = std::floor(kLH * M.k);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(ImVec2(0, yb));
        ImGui::InvisibleButton("##divPainel", ImVec2(W, divH));
        const bool so = ImGui::IsItemHovered(), at = ImGui::IsItemActive();
        if (so || at) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (!at) ImGui::SetTooltip("Arraste para cima ou para baixo: tamanho do rádio x cachoeira");
        }
        if (at) painelFrac_ = std::clamp((io.MousePos.y - divH * 0.5f) / H, 0.3f, 1.0f);
        dl->AddRectFilled(ImVec2(0, yb), ImVec2(W, yb + divH), at ? IM_COL32(0x37, 0xc0, 0xd6, 255) : so ? IM_COL32(0x40, 0x46, 0x4c, 255) : IM_COL32(0x16, 0x18, 0x1a, 255));
        dl->AddLine(ImVec2(W * 0.5f - 20, yb + divH * 0.5f), ImVec2(W * 0.5f + 20, yb + divH * 0.5f), IM_COL32(0x80, 0x84, 0x8a, 255), 2.f);
        areaBaixo(0, yb + divH, W, H - yb - divH);
    }
    chassi();
    colunaEsquerda();
    telaPrincipal();
    teclasTela();
    teclasF();
    controlesBaixo();
    colunaModos();
    areaCentral();
    blocoBandas();
    blocoMeio();
    colunaMain();
    colunaSub();
    popupsPainel();

    // teclado do PC: numeros digitam a frequencia (kHz), Enter confirma, Esc cancela
    if (!io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        // pelos caracteres (teclado normal, teclado numerico e colar/digitar remoto)
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const ImWchar ch = io.InputQueueCharacters[i];
            if (ch >= '0' && ch <= '9') digito((int)(ch - '0'));
        }
        if (entrando_ && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) confirmarEntrada();
        if (entrando_ && ImGui::IsKeyPressed(ImGuiKey_Backspace) && !entrada_.empty()) entrada_.pop_back();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { entrando_ = false; entrada_.clear(); }
        if (ImGui::IsKeyPressed(ImGuiKey_F12)) { g_pedirCaptura = true; }
    }
    // aviso curto do painel (no meio da tela principal)
    if (!avisoPainel_.empty() && agora() < avisoAte_) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float tw = larguraTexto(f_.painel, 9.f, avisoPainel_.c_str());
        const float cx = 352.f, cy = 250.f;
        dl->AddRectFilled(M.p(cx - tw * 0.5f - 10, cy - 4), M.p(cx + tw * 0.5f + 10, cy + 14), IM_COL32(0, 0, 0, 215), M.d(4));
        dl->AddRect(M.p(cx - tw * 0.5f - 10, cy - 4), M.p(cx + tw * 0.5f + 10, cy + 14), alfa(P_AMBAR, 200), M.d(4));
        texto(f_.painel, 9.f, cx, cy - 1, P_AMBAR, avisoPainel_.c_str(), 0.5f);
    }
}

// SCP 2x: o Band Scope na janela inteira, com a frequencia em cima (como o
// monitor externo que o radio de verdade aceita). ESC ou o botao volta.
void Ui::telaCheia()
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    // aqui o "papel" e a janela inteira em largura
    M.k = W / kLW; M.ox = 0; M.oy = 0;
    const float alt = H / M.k;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), P_LCD);
    const Receptor& O = rx_[1 - ativo_];
    char b[160];
    texto(f_.painel, 11.f, 12, 6, ativo_ == 0 ? P_BRANCO : P_AMARELO, ativo_ == 0 ? "MAIN" : "SUB");
    texto(f_.painel, 11.f, 70, 6, P_BRANCO, r_.modo().c_str());
    texto(f_.lcd, 26.f, 130, 2, r_.ligado() ? P_BRANCO : P_APAGADO, fmtFreqLcd(r_.vfo()).c_str());
    std::snprintf(b, sizeof b, "%s  %s  %s", ativo_ == 0 ? "SUB" : "MAIN", O.modo.c_str(), fmtFreqLcd(O.f).c_str());
    texto(f_.painel, 9.f, 420, 10, IM_COL32(0xb8, 0xc0, 0xcc, 255), b);
    std::snprintf(b, sizeof b, "S-meter %.0f dB   SPAN %.0fk   %s", peakDb_, r_.taxa() / (1.0 + zoom_) / 1000.0, modoCentro_ ? "CENTER" : "FIXED");
    texto(f_.painel, 9.f, 700, 10, P_CIANO, b);
    if (tecla("kScpVolta", "VOLTAR AO PAINEL  (ESC)", 1290, 5, 200, 20, 3, nullptr, "Volta ao painel do rádio")) scp_ = 0;
    escopo(6, 32, kLW - 12, alt - 38);
    if (!avisoPainel_.empty() && agora() < avisoAte_) texto(f_.painel, 9.f, 750, 40, P_AMBAR, avisoPainel_.c_str(), 0.5f);
}

// Caixa do radio: chapa, nome, placa do modelo, LEDs BUSY
void Ui::chassi()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = M.p(0, 0), b = M.p(kLW, kLH);
    dl->AddRectFilledMultiColor(a, b, P_CHASSI, P_CHASSI, P_CHASSI2, P_CHASSI2);
    dl->AddRect(a, b, IM_COL32(0x50, 0x53, 0x58, 255), M.d(10), 0, M.d(1.5f));
    dl->AddLine(M.p(8, 1.5f), M.p(kLW - 8, 1.5f), IM_COL32(255, 255, 255, 40), M.d(1));
    // divisorias finas
    dl->AddLine(M.p(1010, 26), M.p(1010, 590), P_LINHA, M.d(1));
    dl->AddLine(M.p(1182, 26), M.p(1182, 400), P_LINHA, M.d(0.8f));
    dl->AddLine(M.p(1293, 26), M.p(1293, 590), P_LINHA, M.d(0.8f));
    dl->AddLine(M.p(1396, 262), M.p(1396, 590), P_LINHA, M.d(0.8f));
    // nome do modelo em Gabriola dourado, no espaco acima da tela principal
    if (f_.gabriola) {
        const float tamG = 23.f;
        const float wG = larguraTexto(f_.gabriola, tamG, "SDR990S");
        const float xG = 412.f - wG * 0.5f, yG = 1.5f;
        texto(f_.gabriola, tamG, xG + 0.8f, yG + 0.8f, IM_COL32(0x20, 0x12, 0x00, 220), "SDR990S");
        texto(f_.gabriola, tamG, xG, yG, IM_COL32(0xe8, 0xb8, 0x40, 255), "SDR990S");
    }
    // placa do modelo
    dl->AddRectFilled(M.p(1180, 6), M.p(1488, 22), IM_COL32(0x1a, 0x1b, 0x1e, 255), M.d(2));
    dl->AddRect(M.p(1180, 6), M.p(1488, 22), IM_COL32(0x55, 0x58, 0x5e, 255), M.d(2), 0, M.d(0.8f));
    texto(f_.painel, 6.2f, 1188, 10.5f, IM_COL32(0xe8, 0xb8, 0x40, 255), "HF/VHF/UHF  SDR  RECEIVER");
    texto(f_.painel, 11.f, 1482, 7.f, IM_COL32(0xc8, 0xcc, 0xd2, 255), "SDR-990S", 1.f);
    // LEDs MAIN BUSY / SUB BUSY
    const bool aberto = r_.ligado() && r_.aoVivo() && sqlAberto_ && (sql_ > -90 || smAtual_ > 0.22f);
    auto ledBusy = [&](float x, const char* rot, bool lig) {
        texto(f_.painel, 5.8f, x + 15, 4, P_ROTULO, rot, 0.5f);
        if (lig) dl->AddRectFilled(M.p(x - 2, 11), M.p(x + 32, 19), alfa(P_VERDE, 60), M.d(2));
        dl->AddRectFilled(M.p(x, 13), M.p(x + 30, 17), lig ? P_VERDE : IM_COL32(0x1a, 0x22, 0x1c, 255), M.d(1));
    };
    ledBusy(800, "MAIN BUSY/TX", aberto && ativo_ == 0);
    ledBusy(955, "SUB BUSY/TX", aberto && ativo_ == 1);
    // botao direito no chassi = MENU
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##menu990");
}

// Coluna da esquerda: liga, TIMER, PF A, DIM, SEND, AT, jaques
void Ui::colunaEsquerda()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool lig = r_.ligado();
    {
        const int k = tecla("kPwr", "", 30, 38, 58, 24, 0, nullptr, lig ? "Desligar o rádio (para o dongle)" : "Ligar o rádio (abre o dongle)");
        if (k) ligarDesligar();
        const ImVec2 c = M.p(59, 50);
        const float r = M.d(6);
        dl->PathArcTo(c, r, (float)(-kPi * 0.5 + 0.7), (float)(kPi * 1.5 - 0.7), 24);
        dl->PathStroke(lig ? P_VERDE : P_ROTULO, 0, M.d(1.6f));
        dl->AddLine(ImVec2(c.x, c.y - r - M.d(1)), ImVec2(c.x, c.y + M.d(0.5f)), lig ? P_VERDE : P_ROTULO, M.d(1.6f));
        dl->AddRectFilled(M.p(52, 32), M.p(66, 35), lig ? P_VERDE : IM_COL32(0x22, 0x2a, 0x24, 255), M.d(1));
    }
    {
        char d[160];
        if (sleepAte_ > 0) std::snprintf(d, sizeof d, "TIMER: desliga em %d min (clique para mudar)", (int)((sleepAte_ - agora()) / 60) + 1);
        else std::snprintf(d, sizeof d, "TIMER: desligar o rádio depois de um tempo (sleep)");
        if (tecla("kTimer", "TIMER", 30, 86, 58, 17, sleepAte_ > 0 ? 3 : 0, "SET", d)) ImGui::OpenPopup("##timer");
    }
    if (tecla("kPfa", "PF A", 22, 122, 34, 15, 0, nullptr, "PF A: abre os DECODIFICADORES (FT8, FT4, CW, RTTY, DMR, TETRA, AIS, APRS...)")) decAberta_ = true;
    {
        const char* nm[4] = {"100%", "75%", "50%", "30%"};
        char d[96]; std::snprintf(d, sizeof d, "DIM: brilho das telas (%s)", nm[dim_]);
        if (tecla("kDim", "DIM", 62, 122, 34, 15, 0, "SEL", d)) dim_ = (dim_ + 1) % 4;
    }
    if (tecla("kSend", "SEND", 30, 152, 58, 17, sendMudo_ ? 4 : 0, nullptr, "SEND: silencia o áudio (MUTE geral)")) sendMudo_ = !sendMudo_;
    {
        const int k = tecla("kAt", "AT", 30, 188, 58, 17, autoWf_ ? 2 : 0, "TUNE",
                            "AT: ajuste automático da cachoeira e do espectro (clique)\nSegurar: guarda a aparência atual como padrão do AT");
        if (k == 1) autoCachoeira();
        if (k == 2) padrao();
    }
    auto jaque = [&](float y, const char* rot) {
        texto(f_.painel, 6.6f, 59, y - 22, P_ROTULO, rot, 0.5f);
        const ImVec2 c = M.p(59, y);
        dl->AddCircleFilled(c, M.d(10), IM_COL32(0x10, 0x10, 0x12, 255), 32);
        dl->AddCircle(c, M.d(10), IM_COL32(0x60, 0x62, 0x66, 255), 32, M.d(1.4f));
        dl->AddCircleFilled(c, M.d(4), IM_COL32(0, 0, 0, 255), 16);
    };
    jaque(250, "PHONES");
    jaque(306, "PADDLE");
    texto(f_.painel, 8.f, 59, 334, P_ROTULO2, "USB", 0.5f);
    for (int i = 0; i < 2; ++i) {
        const float x = 40 + i * 24.f;
        dl->AddRectFilled(M.p(x, 350), M.p(x + 14, 382), IM_COL32(0x0c, 0x0c, 0x0e, 255), M.d(1.5f));
        dl->AddRect(M.p(x, 350), M.p(x + 14, 382), IM_COL32(0x60, 0x62, 0x66, 255), M.d(1.5f), 0, M.d(1));
        dl->AddRectFilled(M.p(x + 4, 354), M.p(x + 10, 378), IM_COL32(0x2a, 0x6a, 0xd0, 200), M.d(1));
    }
    texto(f_.painel, 6.6f, 59, 452, P_ROTULO, "MIC", 0.5f);
    {
        const ImVec2 c = M.p(59, 500);
        dl->AddCircleFilled(c, M.d(25), IM_COL32(0x9a, 0x9c, 0xa0, 255), 40);
        dl->AddCircleFilled(c, M.d(21), IM_COL32(0x22, 0x23, 0x26, 255), 40);
        for (int i = 0; i < 8; ++i) {
            const ImVec2 d = dirAng((float)(i * kPi / 4));
            dl->AddCircleFilled(ImVec2(c.x + d.x * M.d(11), c.y + d.y * M.d(11)), M.d(2), IM_COL32(0xb0, 0xa0, 0x70, 255), 10);
        }
    }
    if (ImGui::BeginPopup("##timer")) {
        ImGui::TextDisabled("Desligar o rádio depois de:");
        const int mins[] = {15, 30, 60, 90, 120};
        for (int m : mins) {
            char r[32]; std::snprintf(r, sizeof r, "%d minutos", m);
            if (ImGui::Selectable(r)) sleepAte_ = agora() + m * 60.0;
        }
        if (ImGui::Selectable("Desligado (cancelar)")) sleepAte_ = 0;
        ImGui::EndPopup();
    }
}

// S-meter de ponteiro na tela principal (desenhado, fundo claro como no radio)
void Ui::medidorAnalogico(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = M.p(x, y), b = M.p(x + w, y + h);
    dl->AddRectFilledMultiColor(a, b, IM_COL32(0xf2, 0xee, 0xe2, 255), IM_COL32(0xf2, 0xee, 0xe2, 255),
                                IM_COL32(0xd8, 0xd2, 0xc0, 255), IM_COL32(0xd8, 0xd2, 0xc0, 255));
    dl->AddRect(a, b, IM_COL32(0x60, 0x60, 0x60, 255), 0, 0, M.d(0.8f));
    dl->PushClipRect(a, b, true);
    // Geometria como a do medidor do radio: pivo bem abaixo do mostrador,
    // arcos grandes e abertos (o de cima pega ~80% da largura), +-29 graus.
    const float sx = w / 900.f, sy = h / 450.f;
    auto ponto = [&](float pos, float raio) {
        const float an = (float)((-90.0 + (pos - 0.5) * 58.0) * kPi / 180.0);
        return ImVec2(x + (450 + raio * std::cos(an)) * sx, y + (877 + raio * std::sin(an)) * sy);
    };
    const float sPos[] = {0.06f, 0.117f, 0.17f, 0.224f, 0.282f, 0.34f, 0.392f, 0.444f, 0.495f, 0.546f};
    const float dbPos[] = {0.62f, 0.693f, 0.77f, 0.845f, 0.92f, 0.996f};
    const ImU32 preto = IM_COL32(0x10, 0x10, 0x10, 255), verm = IM_COL32(0xd0, 0x10, 0x10, 255);
    // Escalas como no medidor do radio, em arcos em volta do mesmo pivo:
    // S (o ponteiro le esta), PO, SWR, Id, COMP, a faixa do ALC e o Vd.
    // Num receptor so o S anda; as outras ficam de enfeite, como no radio em RX.
    const ImU32 cinza = IM_COL32(0x22, 0x22, 0x22, 255);
    auto txMed = [&](float tamPapel, float bx, float by, ImU32 cor, const char* t, float alin) {
        // escalas pequenas: tamanho direto (sem o aumento dos rotulos), minimo legivel
        const float px = std::max(M.d(tamPapel), 6.5f);
        ImFont* fo = melhor(f_.painel, px);
        const ImVec2 ts = fo->CalcTextSizeA(px, FLT_MAX, 0, t);
        const ImVec2 q = M.p(bx, by);
        dl->AddText(fo, px, ImVec2(std::floor(q.x - ts.x * alin), std::floor(q.y - ts.y * 0.5f)), cor, t);
    };
    auto arco = [&](float p0, float p1, float raio, ImU32 cor, float esp) {
        const int n = 40;
        for (int k = 0; k < n; ++k) {
            const ImVec2 q0 = ponto(p0 + (p1 - p0) * k / n, raio), q1 = ponto(p0 + (p1 - p0) * (k + 1) / n, raio);
            dl->AddLine(M.p(q0.x, q0.y), M.p(q1.x, q1.y), cor, M.d(esp));
        }
    };
    auto marca = [&](float pos, float r0, float r1, ImU32 cor, float esp) {
        const ImVec2 q0 = ponto(pos, r0), q1 = ponto(pos, r1);
        dl->AddLine(M.p(q0.x, q0.y), M.p(q1.x, q1.y), cor, M.d(esp));
    };
    auto pontinho = [&](float pos, float raio, ImU32 cor) {
        const ImVec2 q = ponto(pos, raio);
        dl->AddCircleFilled(M.p(q.x, q.y), M.d(0.55f), cor, 6);
    };
    auto rotuloEsq = [&](float pos, float raio, const char* t) {   // S, PO, SWR... na coluna da esquerda
        const ImVec2 q = ponto(pos, raio);
        txMed(4.6f, q.x - 1.5f, q.y, cinza, t, 1.f);
    };
    // --- S: 1..9 em preto, +20/+40/+60 dB em vermelho (numeros por cima do arco)
    arco(0.06f, 0.546f, 765, preto, 1.2f);
    arco(0.546f, 0.996f, 765, verm, 1.6f);
    for (int i = 0; i < 10; ++i) {
        marca(sPos[i], 765, (i % 2) ? 805.f : 790.f, preto, 1.f);
        if (i % 2 == 1) {
            char t[4]; std::snprintf(t, sizeof t, "%d", i);
            const ImVec2 q = ponto(sPos[i], 840);
            texto(f_.painel, 7.f, q.x, q.y - 4, preto, t, 0.5f);
        }
        if (i < 9) pontinho((sPos[i] + sPos[i + 1]) * 0.5f, 777, preto);
    }
    for (int i = 0; i < 6; ++i) {
        marca(dbPos[i], 765, (i % 2) ? 805.f : 790.f, verm, 1.f);
        if (i % 2 == 1) {
            const char* t[] = {"", "+20", "", "+40", "", "+60dB"};
            const ImVec2 q = ponto(dbPos[i], 842);
            texto(f_.painel, 6.f, q.x, q.y - 4, verm, t[i], i == 5 ? 0.75f : 0.5f);
        }
    }
    rotuloEsq(0.045f, 765, "S");
    // --- PO (W): 0..250, escala quadratica como a de potencia
    {
        const float a0 = 0.10f, a1 = 0.95f, R = 712;
        auto pw = [&](float w) { return a0 + (a1 - a0) * std::sqrt(w / 250.f); };
        arco(a0, a1, R, cinza, 0.9f);
        const float rot[] = {0, 25, 50, 100, 150, 200, 250};
        const char* rt[] = {"0", "25", "50", "100", "150", "200", "250W"};
        for (int k = 0; k < 7; ++k) {
            marca(pw(rot[k]), R, R + 12, cinza, 0.9f);
            const ImVec2 q = ponto(pw(rot[k]), R - 24);
            txMed(4.3f, q.x, q.y, cinza, rt[k], k == 6 ? 0.8f : 0.5f);
        }
        const float mn[] = {5, 10, 15, 20, 35, 75, 125, 175, 225};
        for (float m : mn) pontinho(pw(m), R + 6, cinza);
        rotuloEsq(a0 - 0.012f, R, "PO");
    }
    // --- SWR: 1  1.5  2  3  infinito
    {
        const float a0 = 0.15f, a1 = 0.89f, R = 660;
        auto sw = [&](float f) { return a0 + (a1 - a0) * f; };
        arco(a0, a1, R, cinza, 0.9f);
        const float fr[] = {0.f, 0.3f, 0.48f, 0.7f, 1.f};
        const char* rt[] = {"1", "1.5", "2", "3", "\xE2\x88\x9E"};
        for (int k = 0; k < 5; ++k) {
            marca(sw(fr[k]), R, R + 11, cinza, 0.9f);
            const ImVec2 q = ponto(sw(fr[k]), R - 23);
            txMed(4.3f, q.x, q.y, cinza, k == 4 ? "oo" : rt[k], 0.5f);
        }
        const float mn[] = {0.12f, 0.22f, 0.39f, 0.58f, 0.85f};
        for (float m : mn) pontinho(sw(m), R + 6, cinza);
        rotuloEsq(a0 - 0.012f, R, "SWR");
    }
    // --- Id (A): 0..15
    {
        const float a0 = 0.20f, a1 = 0.84f, R = 608;
        auto id = [&](float v) { return a0 + (a1 - a0) * v / 15.f; };
        arco(a0, a1, R, cinza, 0.9f);
        for (int v = 0; v <= 15; ++v) {
            if (v % 5 == 0) {
                marca(id((float)v), R, R + 10, cinza, 0.9f);
                char t[8]; std::snprintf(t, sizeof t, v == 15 ? "%dA" : "%d", v);
                const ImVec2 q = ponto(id((float)v), R - 22);
                txMed(4.2f, q.x, q.y, cinza, t, v == 15 ? 0.7f : 0.5f);
            } else pontinho(id((float)v), R + 5, cinza);
        }
        rotuloEsq(a0 - 0.012f, R, "Id");
    }
    // --- COMP (dB): 0..20
    {
        const float a0 = 0.25f, a1 = 0.78f, R = 556;
        auto cp = [&](float v) { return a0 + (a1 - a0) * v / 20.f; };
        arco(a0, a1, R, cinza, 0.9f);
        for (int v = 0; v <= 20; v += 2) {
            if (v % 10 == 0) {
                marca(cp((float)v), R, R + 10, cinza, 0.9f);
                char t[8]; std::snprintf(t, sizeof t, v == 20 ? "%ddB" : "%d", v);
                const ImVec2 q = ponto(cp((float)v), R - 22);
                txMed(4.1f, q.x, q.y, cinza, t, v == 20 ? 0.7f : 0.5f);
            } else pontinho(cp((float)v), R + 5, cinza);
        }
        rotuloEsq(a0 - 0.012f, R, "COMP");
    }
    // --- faixa do ALC (vermelha, com a seta) e o Vd (46-54 V)
    {
        const float R = 506;
        arco(0.36f, 0.47f, R, verm, 2.2f);
        const ImVec2 s0 = ponto(0.36f, R), s1 = ponto(0.375f, R + 9), s2 = ponto(0.375f, R - 9);
        dl->AddTriangleFilled(M.p(s0.x, s0.y), M.p(s1.x, s1.y), M.p(s2.x, s2.y), verm);
        marca(0.47f, R - 8, R + 8, verm, 1.2f);
        const ImVec2 q = ponto(0.415f, R - 22);
        txMed(4.3f, q.x, q.y, verm, "ALC", 0.5f);
        const float v0 = 0.52f, v1 = 0.64f;
        arco(v0, v1, R, cinza, 0.9f);
        marca(v0, R, R + 11, cinza, 0.9f);
        marca(v1, R, R + 11, cinza, 0.9f);
        const ImVec2 qa = ponto(v0, R - 21), qb = ponto(v1, R - 21), qc = ponto(v1 + 0.115f, R - 21);
        txMed(4.f, qa.x, qa.y, cinza, "46", 0.5f);
        txMed(4.f, qb.x, qb.y, cinza, "54V", 0.5f);
        txMed(4.f, qc.x, qc.y, cinza, "Vd", 0.5f);
    }
    {   // ponteiro
        const float pos = std::clamp(smAtual_, 0.f, 1.02f);
        const ImVec2 p0 = ponto(pos, 420), p1 = ponto(pos, 845);
        dl->AddLine(M.p(p0.x + 0.6f, p0.y + 0.8f), M.p(p1.x + 0.6f, p1.y + 0.8f), IM_COL32(0, 0, 0, 60), M.d(1.6f));
        dl->AddLine(M.p(p0.x, p0.y), M.p(p1.x, p1.y), IM_COL32(0xd8, 0x18, 0x10, 255), M.d(1.4f));
    }
    dl->PopClipRect();
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + M.d(10)), IM_COL32(255, 250, 220, 60), IM_COL32(255, 250, 220, 60),
                                IM_COL32(255, 250, 220, 0), IM_COL32(255, 250, 220, 0));
}

// Band Scope: espectro + regua + cachoeira dentro de um retangulo do papel
void Ui::escopo(float x, float y, float w, float h)
{
    const ImVec2 a = M.p(x, y), b = M.p(x + w, y + h);
    escopoTela(a.x, a.y, b.x - a.x, b.y - a.y, espFrac_, "##lcdEsc", ifOn_ && !baixoAtivo_);
}

// O Band Scope em coordenadas da tela (serve para o da tela do radio e para a
// cachoeira grande de baixo). Cada um com o seu ID: os dois podem estar na tela.
void Ui::escopoTela(float sx, float sy, float sw, float shh, float& frac, const char* id, bool comIf)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(id);
    dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + shh), IM_COL32(0, 0, 0, 255));
    float xm = sx;
    if (comIf) {
        const float wIf = std::floor(sw * 0.24f);
        ifDisplay(sx, sy, wIf, shh);
        xm = sx + wIf + 2.f * s_;
    }
    const float wm = sx + sw - xm;
    const float hEsc = std::max(14.f * s_, M.d(10));
    const float hEsp = std::floor(std::clamp(frac, 0.10f, 0.85f) * (shh - hEsc));
    // PAUSE: o espectro fica parado (troca a curva pela congelada so para desenhar)
    const bool troca = pausa_ && congelado_.size() == suave_.size() && !suave_.empty();
    if (troca) suave_.swap(congelado_);
    espectro(xm, sy, wm, hEsp);
    if (troca) suave_.swap(congelado_);
    escala(xm, sy + hEsp, wm, hEsc);
    ImGui::SetCursorScreenPos(ImVec2(xm, sy + hEsp));
    ImGui::InvisibleButton("##div990", ImVec2(std::max(1.f, wm), hEsc));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) frac = std::clamp((io.MousePos.y - sy - hEsc * 0.5f) / (shh - hEsc), 0.10f, 0.85f);
    cachoeira(xm, sy + hEsp + hEsc, wm, shh - hEsp - hEsc);
    // marca do outro VFO e do NOTCH no espectro
    const float u = std::max(1.f, s_);
    dl->PushClipRect(ImVec2(xm, sy), ImVec2(xm + wm, sy + hEsp), true);
    {
        const Receptor& O = rx_[1 - ativo_];
        const float xo = (float)hzParaX((double)O.f, xm, wm);
        const ImU32 co = ativo_ == 0 ? P_AMARELO : P_BRANCO;
        dl->AddLine(ImVec2(xo, sy), ImVec2(xo, sy + hEsp), alfa(co, 140), u);
        dl->AddTriangleFilled(ImVec2(xo - 4 * u, sy), ImVec2(xo + 4 * u, sy), ImVec2(xo, sy + 5 * u), co);
        const Receptor& R = rx_[ativo_];
        if ((R.nch || R.bef) && (r_.modo() == "USB" || r_.modo() == "LSB")) {
            const double nh = 150.0 * std::pow(3500.0 / 150.0, std::clamp(R.notch, 0.f, 1.f));
            const double fn = r_.modo() == "USB" ? (double)r_.vfo() + nh : (double)r_.vfo() - nh;
            const float xn = (float)hzParaX(fn, xm, wm);
            dl->AddLine(ImVec2(xn, sy + 5 * u), ImVec2(xn, sy + hEsp), alfa(P_CIANO, 200), R.bef ? 3.f * u : 1.5f * u);
            dl->AddTriangleFilled(ImVec2(xn - 4 * u, sy), ImVec2(xn + 4 * u, sy), ImVec2(xn, sy + 5 * u), P_CIANO);
        }
    }
    dl->PopClipRect();
    if (pausa_) {
        ImGui::PushFont(f_.negrito);
        const ImVec2 ts = ImGui::CalcTextSize("PAUSE");
        dl->AddRectFilled(ImVec2(sx + sw - ts.x - 12 * u, sy + 2 * u), ImVec2(sx + sw - 2 * u, sy + ts.y + 6 * u), IM_COL32(0, 0, 0, 200), 3 * u);
        dl->AddText(ImVec2(sx + sw - ts.x - 7 * u, sy + 4 * u), P_AMBAR, "PAUSE");
        ImGui::PopFont();
    }
    ImGui::PopID();
}

// Embaixo do radio: barra de botoes (como a do RXSDR), IF DISPLAY a esquerda e
// a cachoeira grande. A barra fina entre o radio e a cachoeira sobe e desce.
void Ui::areaBaixo(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = std::max(1.f, s_);
    const float hb = 28 * u;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + hb), IM_COL32(0x0a, 0x14, 0x0d, 255));
    dl->AddLine(ImVec2(x, y + hb - 1), ImVec2(x + w, y + hb - 1), IM_COL32(0x1c, 0x36, 0x24, 255));
    Receptor& R = rx_[ativo_];
    float bx = x + 8 * u;
    const float by = y + 3 * u, bh = hb - 6 * u;
    auto bt = [&](const char* rot, float bw, bool on, unsigned cor = 0) {
        const bool c = botao(rot, bx, by, bw * u, bh, on, cor);
        bx += bw * u + 4 * u;
        return c;
    };
    if (bt("IF DISPLAY", 92, ifOn_, IM_COL32(0xff, 0x9a, 0x1f, 255))) ifOn_ = !ifOn_;
    if (bt("NR ESPECTRAL", 112, R.nr2, IM_COL32(0xff, 0x9a, 0x1f, 255))) R.nr2 = !R.nr2;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("NR ESPECTRAL (o mesmo do RXSDR): tira o chiado frequência por frequência.\nNo painel é a tecla NR2; a força fica no anel do botão NR1/NR2 ou na setinha ao lado.");
    if (bt("v", 22, false)) ImGui::OpenPopup("##forcaNr");
    if (ImGui::BeginPopup("##forcaNr")) {
        ImGui::Text("Força do NR ESPECTRAL: %d", (int)nrEspForca_);
        ImGui::SetNextItemWidth(260 * u);
        ImGui::SliderFloat("##fz990", &nrEspForca_, 0, 100, "%.0f");
        ImGui::EndPopup();
    }
    if (bt("NR1", 46, R.nr1)) R.nr1 = !R.nr1;
    if (bt("NB1", 46, R.nb1)) R.nb1 = !R.nb1;
    bx += 8 * u;
    if (bt("AUTO", 56, autoWf_)) autoCachoeira();
    if (bt("PADRÃO", 66, false)) padrao();
    if (bt("ZOOM -", 62, false)) { zoom_ = std::max(0.f, zoom_ <= 1 ? 0.f : (zoom_ + 1) / 2 - 1); centrarParaZoom(true); }
    if (bt("ZOOM +", 62, zoom_ > 0)) { zoom_ = std::min(31.f, (zoom_ + 1) * 2 - 1); centrarParaZoom(true); }
    if (bt("MUTE", 56, R.mute, IM_COL32(0xff, 0x50, 0x50, 255))) R.mute = !R.mute;
    bx += 8 * u;
    {   // BW: largura do filtro do receptor ouvido (o mesmo do knob HI/SHIFT - LO/WIDTH)
        const int g = grupoModo(r_.modo());
        float& hi = filHi_[g][R.fil];
        const float lo = filLo_[g][R.fil];
        const float bwAtual = g == 2 ? hi * 2.f : hi;          // AM: as duas bandas laterais
        auto fmtBwK = [](float v) {
            char b[24];
            if (v >= 10000) std::snprintf(b, sizeof b, "%.0fk", v / 1000.f);
            else if (v >= 1000) std::snprintf(b, sizeof b, "%.2fk", v / 1000.f);
            else std::snprintf(b, sizeof b, "%.0f", v);
            return std::string(b);
        };
        auto aplicarBw = [&](float v) {
            static const float mn[5] = {1000, 50, 4000, 6000, 100000}, mx[5] = {5000, 2500, 16000, 30000, 250000};
            v = std::clamp(v, mn[g], mx[g]);
            hi = g == 2 ? v / 2.f : v;
            if (g == 0) hi = std::max(hi, lo + 400.f);
            aplicarFiltro();
        };
        const std::string rot = "BW " + fmtBwK(bwAtual);
        if (bt(rot.c_str(), 84, false, IM_COL32(0x40, 0xc8, 0xff, 255))) ImGui::OpenPopup("##bw990");
        const bool sobre = ImGui::IsItemHovered();
        static const float passo[5] = {100, 50, 1000, 2500, 10000};
        if (sobre && ImGui::GetIO().MouseWheel != 0) aplicarBw(bwAtual + (ImGui::GetIO().MouseWheel > 0 ? passo[g] : -passo[g]));
        if (sobre) ImGui::SetTooltip("BW: largura do filtro (%s  -  FIL %s)\nClique: lista de larguras.  Roda do mouse: ajusta.\n"
                                     "O mesmo do knob HI/SHIFT · LO/WIDTH (coluna MAIN RECEIVER) e da borda do filtro no espectro.",
                                     r_.modo().c_str(), kNomesFil[R.fil]);
        if (ImGui::BeginPopup("##bw990")) {
            static const float kSsb[] = {1800, 2100, 2400, 2700, 3000, 3600, 4000, 5000};
            static const float kCw[] = {50, 100, 200, 300, 500, 800, 1000, 2000};
            static const float kAm[] = {4000, 6000, 8000, 10000, 12000, 16000};
            static const float kFm[] = {6000, 8000, 10000, 12500, 15000, 20000, 25000};
            static const float kWfm[] = {100000, 150000, 180000, 200000, 250000};
            const float* l = g == 0 ? kSsb : g == 1 ? kCw : g == 2 ? kAm : g == 3 ? kFm : kWfm;
            const int n = g == 0 ? 8 : g == 1 ? 8 : g == 2 ? 6 : g == 3 ? 7 : 5;
            ImGui::TextDisabled("Largura (%s, FIL %s)", r_.modo().c_str(), kNomesFil[R.fil]);
            for (int i = 0; i < n; ++i) {
                const std::string t = fmtBwK(l[i]) + (l[i] < 1000 ? " Hz" : "Hz");
                if (ImGui::Selectable(t.c_str(), std::fabs(bwAtual - l[i]) < 1.f)) aplicarBw(l[i]);
            }
            ImGui::EndPopup();
        }
    }
    // a direita: receptor, modo, frequencia e hora UTC
    {
        SYSTEMTIME st; GetSystemTime(&st);
        char t[96];
        std::snprintf(t, sizeof t, "%s  %s  %s      %02d:%02d UTC", ativo_ == 0 ? "MAIN" : "SUB", r_.modo().c_str(),
                      fmtFreqLcd(r_.vfo()).c_str(), st.wHour, st.wMinute);
        ImGui::PushFont(f_.freqMouse ? f_.freqMouse : f_.negrito);
        const ImVec2 ts = ImGui::CalcTextSize(t);
        if (x + w - ts.x - 12 * u > bx) dl->AddText(ImVec2(x + w - ts.x - 12 * u, y + (hb - ts.y) * 0.5f), IM_COL32(0x40, 0xff, 0x70, 255), t);
        ImGui::PopFont();
    }
    // IF DISPLAY + cachoeira
    const float yy = y + hb + 2 * u, hh = h - hb - 2 * u;
    float xx = x;
    if (ifOn_) {
        const float largIf[3] = {220.f, 300.f, 420.f};          // estreita / media / larga (o menu do IF DISPLAY)
        const float wIf = std::min(largIf[std::clamp(ifTam_, 0, 2)] * u, w * 0.45f);
        ifDisplay(x + 2 * u, yy, wIf, hh);
        xx = x + wIf + 6 * u;
    }
    escopoTela(xx, yy, x + w - xx, hh, fracBaixo_, "##baixoEsc", false);
}

// Tela principal: medidor, linha de estado, frequencias, escopo, rotulos F
void Ui::telaPrincipal()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool gigante = false;          // SCP 2x virou a tela cheia (telaCheia)
    const float xDir = gigante ? 1006.f : 702.f;       // SCP 2x: o escopo ocupa as duas telas
    dl->AddRectFilled(M.p(122, 28), M.p(xDir, 388), IM_COL32(0x0a, 0x0a, 0x0c, 255), M.d(10));
    dl->AddRect(M.p(122, 28), M.p(xDir, 388), IM_COL32(0x55, 0x57, 0x5c, 255), M.d(10), 0, M.d(1.2f));
    dl->AddRectFilledMultiColor(M.p(130, 30), M.p(xDir - 8, 40), IM_COL32(255, 255, 255, 18), IM_COL32(255, 255, 255, 18),
                                IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    dl->AddRectFilled(M.p(138, 40), M.p(gigante ? 998.f : 628.f, 374), P_LCD, M.d(2));
    // chanfro de baixo com os triangulos das teclas F
    dl->AddQuadFilled(M.p(138, 388), M.p(628, 388), M.p(640, 404), M.p(126, 404), IM_COL32(0x16, 0x17, 0x1a, 255));
    for (int i = 0; i < 7; ++i) {
        const float cx = 175.8f + i * 59.6f;
        dl->AddTriangleFilled(M.p(cx - 4, 399), M.p(cx + 4, 399), M.p(cx, 392), IM_COL32(0xa0, 0xa4, 0xaa, 255));
    }

    const bool lig = r_.ligado();
    const Receptor& R = rx_[ativo_];
    const Receptor& O = rx_[1 - ativo_];
    const std::string modo = r_.modo();
    const bool grande = scp_ >= 1;
    char b[160];

    if (!grande) {
        medidorAnalogico(144, 46, 166, 84);
        const char* agcTxt[4] = {"AGC-OFF", "AGC-S", "AGC-M", "AGC-F"};
        const char* fonte = !r_.temDispositivo() ? "--" : r_.tipo() == "rtltcp" ? "TCP" : r_.tipo() == "sdrplay" ? "RSP" : "RTL";
        std::snprintf(b, sizeof b, "RX\xE2\x96\xBC%s", fonte);
        texto(f_.painel, 6.8f, 316, 45, P_BRANCO, b);
        std::snprintf(b, sizeof b, "PRE%s", pamp_ ? (pamp_ == 1 ? "1" : "2") : "-");
        texto(f_.painel, 6.8f, 360, 45, pamp_ ? P_AMBAR : P_APAGADO, b);
        if (att_) std::snprintf(b, sizeof b, "ATT%d", att_ * 6); else std::snprintf(b, sizeof b, "ATT-");
        texto(f_.painel, 6.8f, 392, 45, att_ ? P_AMBAR : P_APAGADO, b);
        texto(f_.painel, 6.8f, 428, 45, P_VERDE, agcTxt[R.agcOff ? 0 : std::clamp(R.agc, 1, 3)]);
        {
            SYSTEMTIME st; GetSystemTime(&st);
            const char* mes[12] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
            std::snprintf(b, sizeof b, "%02d/%s %02d:%02dU", st.wDay, mes[std::clamp((int)st.wMonth, 1, 12) - 1],
                          st.wHour, st.wMinute);
            texto(f_.painel, 6.8f, 562, 45, P_CIANO, b, 1.f);
        }
        float cxp = 316;          // chips das funcoes ligadas
        auto chip = [&](const char* t, bool on) {
            if (!on || cxp > 520) return;
            const float tw = larguraTexto(f_.painel, 5.8f, t) + 5;
            dl->AddRect(M.p(cxp, 55), M.p(cxp + tw, 65.5f), P_CIANO, M.d(1), 0, M.d(0.6f));
            texto(f_.painel, 5.8f, cxp + tw * 0.5f, 55.4f, P_CIANO, t, 0.5f);
            cxp += tw + 3;
        };
        chip("NB1", R.nb1); chip("NB2", R.nb2); chip("NR1", R.nr1); chip("NR2", R.nr2);
        chip(R.nchLargo ? "NCH-W" : "NCH", R.nch); chip("BEF", R.bef); chip("A.NCH", R.anch); chip("BC", R.bc);
        chip("APF", R.apf); chip("PROC", proc_); chip("TRACK", tracking_); chip("XVTR", r_.xvtr() > 0);
        chip("BIAS", Config::instance().biasT()); chip("LOCK", R.lock); chip("FINE", R.fine);
        if (sleepAte_ > 0) { char t[24]; std::snprintf(t, sizeof t, "SLEEP %d", (int)((sleepAte_ - agora()) / 60) + 1); chip(t, true); }
        if (r_.som().gravando()) chip("REC", true);
        // RIT
        std::snprintf(b, sizeof b, "%s%+.2f", R.ritOn ? "RIT " : "", (R.ritOn ? R.rit : 0) / 1000.0);
        texto(f_.painel, 6.f, 562, 56, R.ritOn ? P_BRANCO : P_APAGADO, b, 1.f);
        // modo + RX/TX + VFO / M.CH
        texto(f_.painel, 10.5f, 316, 68, P_BRANCO, modo.c_str());
        dl->AddRectFilled(M.p(316, 84), M.p(334, 93), lig ? IM_COL32(0x10, 0x90, 0x30, 255) : IM_COL32(0x20, 0x30, 0x24, 255), M.d(1));
        texto(f_.painel, 6.5f, 325, 84.3f, P_BRANCO, "RX", 0.5f);
        dl->AddRectFilled(M.p(316, 96), M.p(334, 105), (txVfo_ == ativo_) ? IM_COL32(0x80, 0x18, 0x18, 255) : IM_COL32(0x30, 0x1a, 0x1a, 255), M.d(1));
        texto(f_.painel, 6.5f, 325, 96.3f, (txVfo_ == ativo_) ? P_BRANCO : P_APAGADO, "TX", 0.5f);
        if (R.memModo && !mem_.empty()) {
            const int ch = std::clamp(R.memCanal, 0, (int)mem_.size() - 1);
            std::snprintf(b, sizeof b, "M.CH %03d", ch + 1);
            texto(f_.painel, 7.f, 350, 72, P_AMBAR, b);
            const std::string nome = mem_[ch].nome.substr(0, 30);
            texto(f_.painel, 6.f, 340, 108, P_AMBAR, nome.c_str());
        } else texto(f_.painel, 7.f, 350, 72, P_BRANCO, ativo_ == 0 ? "VFO-M" : "VFO-S");
        if (entrando_) {
            const std::string t = (entrada_.empty() ? std::string("_") : entrada_) + " kHz";
            texto(f_.lcd, 24.f, 562, 72, P_AMBAR, t.c_str(), 1.f);
            texto(f_.painel, 6.f, 562, 106, P_AMBAR, "digite em kHz e ENT (CLR apaga)", 1.f);
        } else {
            const std::string fs = fmtFreqLcd(r_.vfo());
            const std::string mhz = fs.substr(0, fs.size() - 4), hz = fs.substr(fs.size() - 4);
            const float wHz = larguraTexto(f_.lcd, 17.f, hz.c_str());
            texto(f_.lcd, 30.f, 562 - wHz, 66, lig ? P_BRANCO : P_APAGADO, mhz.c_str(), 1.f);
            texto(f_.lcd, 17.f, 562, 79, lig ? IM_COL32(0xc8, 0xd0, 0xdc, 255) : P_APAGADO, hz.c_str(), 1.f);
            rodaDigitos(f_.lcd, 30.f, 562 - wHz, 66, mhz, ativo_, 1000);
            rodaDigitos(f_.lcd, 17.f, 562, 79, hz, ativo_, 1);
            if (!R.memModo) texto(f_.painel, 6.f, 562, 106, P_APAGADO, ("FIL-" + std::string(kNomesFil[R.fil])).c_str(), 1.f);
        }
        // linha do outro receptor (pequena) e do filtro
        {
            std::snprintf(b, sizeof b, "%s  %s", ativo_ == 0 ? "SUB" : "MAIN", O.modo.c_str());
            texto(f_.painel, 7.f, 316, 118, ativo_ == 0 ? P_AMARELO : P_BRANCO, b);
            texto(f_.lcd, 11.f, 462, 117, IM_COL32(0xb8, 0xc0, 0xcc, 255), fmtFreqLcd(O.f).c_str(), 1.f);
            rodaDigitos(f_.lcd, 11.f, 462, 117, fmtFreqLcd(O.f), 1 - ativo_, 1);
            const int g = grupoModo(modo);
            if (g == 0) std::snprintf(b, sizeof b, "HI %.2fk LO %.2fk", filHi_[0][R.fil] / 1000.f, filLo_[0][R.fil] / 1000.f);
            else if (g == 1) std::snprintf(b, sizeof b, "W %.0f SH %+.0f", filHi_[1][R.fil], filLo_[1][R.fil]);
            else if (g == 2) std::snprintf(b, sizeof b, "HI %.1fk LO %.2fk", filHi_[2][R.fil] / 1000.f, filLo_[2][R.fil] / 1000.f);
            else std::snprintf(b, sizeof b, "BW %.1fk", r_.banda() / 1000.f);
            texto(f_.painel, 6.2f, 562, 119, P_CIANO, b, 1.f);
        }
        {   // S-meter em texto (METER)
            const char* sTxt[] = {"S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9"};
            const float sPos[] = {0.06f, 0.117f, 0.17f, 0.224f, 0.282f, 0.34f, 0.392f, 0.444f, 0.495f, 0.546f};
            const float p = smAtual_;
            if (medidorModo_ == 0) {
                if (p < 0.546f) { int k = 0; for (int i = 0; i < 10; ++i) if (p >= sPos[i]) k = i; std::snprintf(b, sizeof b, "%s", sTxt[k]); }
                else std::snprintf(b, sizeof b, "S9+%d", (int)std::lround((p - 0.546f) / (0.996f - 0.546f) * 60.f / 5.f) * 5);
            } else if (medidorModo_ == 1) std::snprintf(b, sizeof b, "%.0f dB", peakDb_);
            else std::snprintf(b, sizeof b, "PK %.0f", peakDbSql_);
            texto(f_.painel, 6.6f, 307, 121, IM_COL32(0x10, 0x10, 0x10, 255), b, 1.f);
        }
    } else {
        std::snprintf(b, sizeof b, "%s %s   %s  %s %s   %s", ativo_ == 0 ? "MAIN" : "SUB", modo.c_str(), fmtFreqLcd(r_.vfo()).c_str(),
                      ativo_ == 0 ? "SUB" : "MAIN", O.modo.c_str(), fmtFreqLcd(O.f).c_str());
        texto(f_.painel, 7.f, 146, 43, P_BRANCO, b);
    }

    // cabecalho do Band Scope
    const float yEsc = grande ? 54.f : 132.f;
    const float xFim = gigante ? 994.f : 563.f;
    {
        const double span = r_.taxa() / (1.0 + zoom_);
        std::snprintf(b, sizeof b, "SPAN %.0fk", span / 1000.0);
        texto(f_.painel, 6.f, 146, yEsc, P_BRANCO, b);
        texto(f_.painel, 6.5f, (146 + xFim) * 0.5f, yEsc, P_AMARELO, "Band Scope", 0.5f);
        texto(f_.painel, 6.f, xFim, yEsc, P_BRANCO, modoCentro_ ? "CENTER MODE" : "FIXED MODE", 1.f);
        if (scopeAtt_) { std::snprintf(b, sizeof b, "ATT %ddB", scopeAtt_ * 10); texto(f_.painel, 6.f, 210, yEsc, P_AMBAR, b); }
    }
    escopo(146, yEsc + 9, xFim - 146, 346 - (yEsc + 9));
    {   // rotulos das teclas F
        const char* rot[7] = {"RF/AF", "ATT", "MARKER", "PAUSE", "M/S", "SPAN", "MODE"};
        const bool on[7] = {ifOn_, scopeAtt_ > 0, memVisivel_, pausa_, ativo_ == 1, zoom_ > 0, modoCentro_};
        for (int i = 0; i < 7; ++i) {
            const float cx = 175.8f + i * 59.6f;
            dl->AddRectFilled(M.p(cx - 27, 352), M.p(cx + 27, 366), on[i] ? IM_COL32(0x20, 0x3a, 0x60, 255) : IM_COL32(0x12, 0x16, 0x1c, 255), M.d(1.5f));
            texto(f_.painel, 6.6f, cx, 354.5f, on[i] ? P_BRANCO : P_ROTULO2, rot[i], 0.5f);
        }
    }
    if (!lig) texto(f_.painel, 9.f, 352, 300, P_AMBAR, "Depois aperte o botão liga (canto de cima, à esquerda)", 0.5f);
    if (dim_ > 0) dl->AddRectFilled(M.p(138, 40), M.p(gigante ? 998.f : 628.f, 374), IM_COL32(0, 0, 0, dim_ * 45));
}

// As sete teclas ao lado da tela principal (os nomes aparecem na tela)
void Ui::teclasTela()
{
    auto& c = Config::instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float yy[7] = {48, 94, 140, 186, 232, 278, 324};
    char l1[32], l2[32];
    for (int i = 0; i < 7; ++i) {
        const char* d = "";
        bool on = false;
        switch (i) {
        case 0: std::snprintf(l1, sizeof l1, "ANT"); std::snprintf(l2, sizeof l2, "%s", r_.tipo() == "rtltcp" ? "TCP" : r_.tipo() == "sdrplay" ? "RSP" : "RTL");
                d = "ANT: escolher o aparelho (RTL-SDR USB, RTL-TCP pela rede, SDRplay) - abre a CONFIGURAÇÃO"; break;
        case 1: std::snprintf(l1, sizeof l1, "ATT");
                if (att_) std::snprintf(l2, sizeof l2, "%ddB", att_ * 6); else std::snprintf(l2, sizeof l2, "OFF");
                on = att_ > 0; d = "ATT: atenuador 0 / 6 / 12 / 18 dB"; break;
        case 2: { const std::string q = c.qmode(); std::snprintf(l1, sizeof l1, "PSEL");
                std::snprintf(l2, sizeof l2, "%s", r_.xvtr() ? "XVTR" : q == "on" ? "Q-ON" : q == "off" ? "Q-OFF" : "AUTO"); on = q != "off";
                d = "PSEL: entrada de HF (amostragem direta Q) - AUTO liga abaixo de 24 MHz, ON, OFF"; break; }
        case 3: std::snprintf(l1, sizeof l1, "PAMP");
                if (pamp_) std::snprintf(l2, sizeof l2, "PRE%d", pamp_); else std::snprintf(l2, sizeof l2, "OFF");
                on = pamp_ > 0; d = "PAMP: pré-amplificador OFF / +10 dB / +20 dB"; break;
        case 4: std::snprintf(l1, sizeof l1, "T-AGC"); std::snprintf(l2, sizeof l2, "%s", c.agc() ? "ON" : "OFF"); on = c.agc();
                d = "T-AGC (no lugar do MAX-P): AGC do dongle ligado/desligado.\nCom T-AGC ligado, o RF continua valendo como ganho digital (6 dB abaixo do manual)"; break;
        case 5: { const char* m[3] = {"S", "dB", "PEAK"}; std::snprintf(l1, sizeof l1, "METER"); std::snprintf(l2, sizeof l2, "%s", m[medidorModo_]);
                d = "METER: leitura do S-meter em S, em dB ou de pico"; break; }
        case 6: std::snprintf(l1, sizeof l1, "RATE"); std::snprintf(l2, sizeof l2, "%.3g M", r_.taxa() / 1e6);
                d = "RATE (no lugar do TX-FIL): taxa de amostragem do dongle (largura do Band Scope)"; break;
        }
        {
            dl->AddRectFilled(M.p(568, yy[i] + 2), M.p(624, yy[i] + 40), IM_COL32(0x08, 0x0c, 0x12, 255), M.d(1));
            dl->AddLine(M.p(568, yy[i] + 41), M.p(624, yy[i] + 41), IM_COL32(0x40, 0x44, 0x4a, 255), M.d(0.6f));
            texto(f_.painel, 7.4f, 596, yy[i] + 8, P_BRANCO, l1, 0.5f);
            texto(f_.painel, 7.4f, 596, yy[i] + 20, on ? P_AMBAR : P_ROTULO2, l2, 0.5f);
            dl->AddTriangleFilled(M.p(640, yy[i] + 15), M.p(646, yy[i] + 11), M.p(646, yy[i] + 19), IM_COL32(0xa0, 0xa4, 0xaa, 255));
        }
        char id[16]; std::snprintf(id, sizeof id, "kSide%d", i);
        const float xk = 652.f;
        if (!tecla(id, "", xk, yy[i] + 7, 40, 17, 0, nullptr, d)) continue;
        switch (i) {
        case 0: abrirConfig(); break;
        case 1: att_ = (att_ + 1) % 4; break;
        case 2: { const std::string q = c.qmode(); c.set("qmode", std::string(q == "auto" ? "on" : q == "on" ? "off" : "auto"));
                  r_.aplicarConfig(); sintonizar(r_.vfo(), false); break; }
        case 3: pamp_ = (pamp_ + 1) % 3; break;
        case 4: c.set("agc", !c.agc()); r_.aplicarConfig(); break;
        case 5: medidorModo_ = (medidorModo_ + 1) % 3; break;
        case 6: ImGui::OpenPopup("##taxa"); break;
        }
    }
    if (ImGui::BeginPopup("##taxa")) {
        ImGui::TextDisabled("Taxa de amostragem (largura do Band Scope)");
        const unsigned tx[] = {250000, 1024000, 1400000, 1800000, 1920000, 2048000, 2400000, 2560000, 2880000, 3200000};
        for (unsigned t : tx) {
            char r[32]; std::snprintf(r, sizeof r, "%.3f Msps", t / 1e6);
            if (ImGui::Selectable(r, c.sampleRate() == t)) { c.set("sample_rate", (long long)t); r_.aplicarConfig(); }
        }
        ImGui::EndPopup();
    }
}

// ESC, F1-F7 e SWL/SCP
void Ui::teclasF()
{
    if (tecla("kEsc", "ESC", 124, 412, 28, 15, 0, nullptr, "ESC: fecha as janelas abertas e cancela a digitação")) {
        cfgAberta_ = sobreAberta_ = memAberta_ = decAberta_ = false;
        entrando_ = false; entrada_.clear();
    }
    const char* dicas[7] = {
        "F1 RF/AF: liga/desliga o IF DISPLAY (zoom de 20 kHz em volta da sintonia)",
        "F2 ATT: atenuador do Band Scope (escurece a imagem em passos de 10 dB)",
        "F3 MARKER: mostra/esconde os nomes das memórias em cima do espectro",
        "F4 PAUSE: congela o espectro e a cachoeira",
        "F5 M/S: troca o receptor ouvido entre MAIN e SUB",
        "F6 SPAN: largura do Band Scope (cheia, 1/2, 1/4, 1/8, 1/16)",
        "F7 MODE: CENTER (a faixa acompanha a sintonia) ou FIXED (a faixa fica parada)"};
    for (int i = 0; i < 7; ++i) {
        char id[8], rot[4];
        std::snprintf(id, sizeof id, "kF%d", i + 1);
        std::snprintf(rot, sizeof rot, "F%d", i + 1);
        const float cx = 175.8f + i * 59.6f;
        if (!tecla(id, rot, cx - 21, 412, 42, 15, 0, nullptr, dicas[i], 0, true)) continue;
        switch (i) {
        case 0: ifOn_ = !ifOn_; break;
        case 1: {
            const int novo = (scopeAtt_ + 1) % 4;
            wfBrilho_ = std::clamp(wfBrilho_ + (scopeAtt_ - novo) * 20.f, 0.f, 200.f);
            scopeAtt_ = novo; autoWf_ = false; break; }
        case 2: memVisivel_ = !memVisivel_; break;
        case 3: pausa_ = !pausa_; if (pausa_) congelado_ = suave_; break;
        case 4: ativar(1 - ativo_); break;
        case 5: {
            const float z[5] = {0, 1, 3, 7, 15};
            int k = 0; for (int j = 0; j < 5; ++j) if (std::fabs(zoom_ - z[j]) < 0.3f) k = j;
            zoom_ = z[(k + 1) % 5];
            centrarParaZoom(true); break; }
        case 6: modoCentro_ = !modoCentro_;
                if (modoCentro_) r_.centralizar(r_.centroSemDc(r_.vfo()));
                break;
        }
    }
    const char* scpDica = "SWL/SCP: Band Scope grande (1x: a tela principal inteira; 2x: tela cheia, como o monitor externo do rádio). De novo volta";
    if (tecla("kScp", "SCP", 654, 424, 36, 15, scp_ ? 3 : 0, "SWL", scpDica)) scp_ = (scp_ + 1) % 3;
}

// Teclas e botoes de baixo (VOX, PROC, FBK, MONI e os 5 botoes duplos)
void Ui::controlesBaixo()
{
    {   // VOX = squelch automatico, liga/desliga: aceso = squelch fechando o chiado
        Receptor& Rv = rx_[ativo_];
        const bool ligado = sqlMedindo_ || Rv.sql > -90.f;
        if (tecla("kVox", "VOX", 150, 446, 46, 15, sqlMedindo_ ? 3 : ligado ? 2 : 0, "SEL",
                  "VOX: squelch automático.\nClique: mede o ruído por 4 s e fecha o squelch logo acima (a margem é o VOX GAIN).\n"
                  "Clique de novo: desliga e o áudio volta (squelch aberto).")) {
            if (sqlMedindo_) { sqlMedindo_ = false; Rv.sql = -90.f; sql_ = -90.f; sqlAplicado_ = -90.f; }
            else if (Rv.sql > -90.f) { Rv.sql = -90.f; sql_ = -90.f; sqlAplicado_ = -90.f; aviso("Squelch aberto"); }
            else if (r_.aoVivo()) { sqlMedindo_ = true; sqlAmostras_.clear(); sqlIni_ = agora(); sqlUlt_ = 0; }
        }
    }
    if (tecla("kProc", "PROC", 252, 446, 46, 15, proc_ ? 2 : 0, "SEL",
              "PROC: compressor de áudio - levanta os sinais fracos (força no PROC IN)")) proc_ = !proc_;
    if (tecla("kFbk", "FBK", 356, 446, 46, 15, r_.decoders().tipo() == Decoders::CW ? 2 : 0, nullptr,
              "FBK: abre o decodificador de CW (telegrafia em texto)")) {
        decAberta_ = true;
        if (r_.decoders().tipo() != Decoders::CW) escolherDecoder(Decoders::CW, true);
    }
    if (tecla("kMoni", "MONI", 458, 446, 46, 15, 0, nullptr, "MONI: escolher a saída de som (placa / fone)")) {
        saidas_ = SomNativo::listarSaidas();
        ImGui::OpenPopup("##saida");
    }
    if (ImGui::BeginPopup("##saida")) {
        auto& c = Config::instance();
        ImGui::TextDisabled("Saída de som");
        if (ImGui::Selectable("Padrão do Windows", c.str("audio_saida").empty())) r_.trocarSaida("");
        for (const auto& s : saidas_)
            if (ImGui::Selectable(s.c_str(), c.str("audio_saida") == s)) r_.trocarSaida(s);
        ImGui::EndPopup();
    }
    float di, dO; char d[220];
    std::snprintf(d, sizeof d, "MIC: tonalidade do áudio (%s %d%%)\nVOX GAIN: margem do squelch automático (%.0f dB)",
                  tom_ < 45 ? "grave" : tom_ > 55 ? "agudo" : "plano", (int)std::lround(std::fabs(tom_ - 50) * 2), sqlMargem_);
    if (giro("gMic", 173, 534, 25, "MIC", "VOX GAIN", tom_ / 100.f, sqlMargem_ / 10.f, di, dO, d)) {
        tom_ = std::clamp(tom_ + di * 100.f, 0.f, 100.f); if (di != 0) rxeq_ = 0;
        sqlMargem_ = std::clamp(sqlMargem_ + dO * 10.f, 0.f, 10.f);
    }
    std::snprintf(d, sizeof d, "PROC IN: força do compressor de áudio (%d%%)\nPROC OUT: retorno do ponteiro do S-meter (%.1f s)",
                  (int)std::lround(procNivel_ * 100), smRetorno_);
    if (giro("gProc", 275, 534, 25, "PROC IN", "PROC OUT", procNivel_, (smRetorno_ - 0.1f) / 2.9f, di, dO, d)) {
        procNivel_ = std::clamp(procNivel_ + di, 0.f, 1.f);
        smRetorno_ = std::clamp(smRetorno_ + dO * 2.9f, 0.1f, 3.f);
        Config::instance().set("smeter_retorno", (double)smRetorno_);
    }
    std::snprintf(d, sizeof d, "PWR: brilho da cachoeira (%.0f)\nCAR: contraste / range da cachoeira (%.0f dB)", wfBrilho_, wfRange_);
    if (giro("gPwr", 379, 534, 25, "PWR", "CAR", wfBrilho_ / 200.f, (wfRange_ - 20.f) / 100.f, di, dO, d)) {
        wfBrilho_ = std::clamp(wfBrilho_ + di * 200.f, 0.f, 200.f);
        wfRange_ = std::clamp(wfRange_ + dO * 100.f, 20.f, 120.f);
        autoWf_ = false;
    }
    std::snprintf(d, sizeof d, "KEY SPEED: velocidade da cachoeira (%.0fx)\nDELAY: tempo para o squelch fechar (%.1f s)", wfSpeed_, sqlHang_);
    if (giro("gKey", 481, 534, 25, "KEY SPEED", "DELAY", (wfSpeed_ - 1) / 9.f, sqlHang_ / 2.f, di, dO, d)) {
        static float acc = 0; acc += di * 9.f;
        while (acc >= 1) { wfSpeed_ = std::min(10.f, wfSpeed_ + 1); acc -= 1; }
        while (acc <= -1) { wfSpeed_ = std::max(1.f, wfSpeed_ - 1); acc += 1; }
        sqlHang_ = std::clamp(sqlHang_ + dO * 2.f, 0.f, 2.f);
    }
    std::snprintf(d, sizeof d, "CW PITCH: altura do tom do CW e centro do APF (%.0f Hz)\nMONITOR: altura do espectro em relação à cachoeira (%d%%)",
                  cwPitch_, (int)std::lround(espFrac_ * 100));
    if (giro("gCw", 584, 534, 25, "CW PITCH", "MONITOR", (cwPitch_ - 300.f) / 700.f, (espFrac_ - 0.1f) / 0.75f, di, dO, d)) {
        static float acc = 0; acc += di * 700.f;
        while (acc >= 10) { cwPitch_ = std::min(1000.f, cwPitch_ + 10); acc -= 10; }
        while (acc <= -10) { cwPitch_ = std::max(300.f, cwPitch_ - 10); acc += 10; }
        espFrac_ = std::clamp(espFrac_ + dO * 0.75f, 0.1f, 0.85f);
    }
}

// Coluna dos modos (LSB/USB, CW, FSK/PSK, FM/AM, DATA)
void Ui::colunaModos()
{
    const std::string m = r_.modo();
    if (tecla("kSsb", "LSB\n/USB", 712, 382, 54, 25, (m == "LSB" || m == "USB") ? 2 : 0, nullptr,
              "LSB/USB: banda lateral (troca entre LSB e USB)")) {
        if (m == "LSB") mudarModoPainel("USB");
        else if (m == "USB") mudarModoPainel("LSB");
        else mudarModoPainel(r_.vfo() < 10000000 ? "LSB" : "USB");
    }
    texto(f_.painel, 5.f, 739, 408.5f, P_ROTULO2, "REV", 0.5f);
    if (tecla("kCw", "CW", 712, 420, 54, 21, m == "CW" ? 2 : 0, nullptr, "CW: telegrafia (largura e SHIFT no HI/LO, tom no CW PITCH)"))
        mudarModoPainel("CW");
    texto(f_.painel, 5.f, 739, 442.5f, P_ROTULO2, "REV", 0.5f);
    if (tecla("kFsk", "FSK\n/PSK", 712, 454, 54, 25, r_.decoders().tipo() == Decoders::RTTY ? 2 : 0, nullptr,
              "FSK/PSK: decodificador de RTTY (FSK) - abre a janela já em RTTY")) {
        decAberta_ = true;
        if (r_.decoders().tipo() != Decoders::RTTY) escolherDecoder(Decoders::RTTY, true);
    }
    texto(f_.painel, 5.f, 739, 480.5f, P_ROTULO2, "FM-N", 0.5f);
    {
        const bool fm = m == "FM" || m == "NFM" || m == "WFM" || m == "AM";
        const int k = tecla("kFmam", "FM\n/AM", 712, 492, 54, 25, fm ? 2 : 0, nullptr,
                            "FM/AM: alterna AM / FM estreito\nSegurar (SEL): FM largo (radiodifusão 88-108)");
        if (k == 1) mudarModoPainel(m == "AM" ? "NFM" : "AM");
        if (k == 2) mudarModoPainel("WFM");
    }
    texto(f_.painel, 5.f, 739, 518.5f, P_ROTULO2, "SEL", 0.5f);
    if (tecla("kData", "DATA", 712, 530, 54, 21, decAberta_ ? 2 : 0, nullptr,
              "DATA: abre os decodificadores digitais (FT8, FT4, DMR, TETRA, AIS, APRS, ACARS, HFDL, SSTV, WEFAX, DRM...)"))
        decAberta_ = !decAberta_;
}

// Tela secundaria (MAIN / SUB)
void Ui::telaSub()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(M.p(778, 28), M.p(1006, 252), IM_COL32(0x0a, 0x0a, 0x0c, 255), M.d(6));
    dl->AddRect(M.p(778, 28), M.p(1006, 252), IM_COL32(0x55, 0x57, 0x5c, 255), M.d(6), 0, M.d(1.1f));
    dl->AddRectFilled(M.p(786, 36), M.p(998, 244), P_LCD, M.d(2));
    const bool lig = r_.ligado();
    char b[96];
    if (sDisp_ == 0) {
        for (int i = 0; i < 2; ++i) {
            const Receptor& R = rx_[i];
            const float y0 = i == 0 ? 46.f : 120.f;
            const bool at = i == ativo_;
            const uint64_t f = at ? r_.vfo() : R.f;
            const std::string md = at ? r_.modo() : R.modo;
            // etiqueta MAIN (branca) / SUB (amarela); a do receptor ouvido fica cheia
            const ImU32 corEt = i == 0 ? P_BRANCO : P_AMARELO;
            const ImVec2 ea = M.p(794, y0 + 2), eb = M.p(830, y0 + 15);   // etiqueta; o LED fica antes dela
            if (at) dl->AddRectFilled(ea, eb, corEt, M.d(1));
            else dl->AddRect(ea, eb, alfa(corEt, 160), M.d(1), 0, M.d(0.8f));
            texto(f_.painel, 7.2f, 812, y0 + 3.5f, at ? IM_COL32(0, 0, 0, 255) : alfa(corEt, 190), i == 0 ? "MAIN" : "SUB", 0.5f);
            {   // LED: aceso (verde, com brilho) no receptor que esta sendo ouvido
                const ImVec2 cl = M.p(792 - 7, y0 + 8.5f);
                if (at) {
                    dl->AddCircleFilled(cl, M.d(6.f), alfa(P_VERDE, 60), 20);
                    dl->AddCircleFilled(cl, M.d(3.6f), P_VERDE, 16);
                    dl->AddCircleFilled(ImVec2(cl.x - M.d(1), cl.y - M.d(1)), M.d(1.2f), IM_COL32(220, 255, 230, 255), 8);
                } else {
                    dl->AddCircleFilled(cl, M.d(3.6f), IM_COL32(0x14, 0x24, 0x18, 255), 16);
                    dl->AddCircle(cl, M.d(3.6f), IM_COL32(0x30, 0x40, 0x34, 255), 16, M.d(0.6f));
                }
            }
            texto(f_.painel, 10.f, 836, y0 + 1, lig ? P_BRANCO : P_APAGADO, md.c_str());
            texto(f_.painel, 6.6f, 798, y0 + 24, P_ROTULO2, R.memModo ? "MEM" : "VFO");
            if (txVfo_ == i) {
                dl->AddRectFilled(M.p(798, y0 + 36), M.p(816, y0 + 45), IM_COL32(0x80, 0x18, 0x18, 255), M.d(1));
                texto(f_.painel, 6.f, 807, y0 + 36.5f, P_BRANCO, "TX", 0.5f);
            }
            std::snprintf(b, sizeof b, "%+.3f", (R.ritOn ? R.rit : 0) / 1000.0);
            texto(f_.painel, 9.f, 990, y0 + 1, R.ritOn ? P_BRANCO : P_APAGADO, b, 1.f);
            const std::string fs = fmtFreqLcd(f);
            texto(f_.lcd, i == 0 ? 32.f : 28.f, 990, y0 + 17, lig ? (at ? P_BRANCO : IM_COL32(0xc6, 0xcc, 0xd8, 255)) : P_APAGADO, fs.c_str(), 1.f);
            rodaDigitos(f_.lcd, i == 0 ? 32.f : 28.f, 990, y0 + 17, fs, i, 1);
            if (R.lock) texto(f_.painel, 6.f, 836, y0 + 24, P_VERMELHO, "LOCK");
            if (R.fine) texto(f_.painel, 6.f, 862, y0 + 24, P_CIANO, "FINE");
            if (R.sql > -90.f) {   // squelch ligado neste receptor (vermelho = fechando o som agora)
                const bool fechado = at && lig && !sqlAberto_;
                std::snprintf(b, sizeof b, fechado ? "SQL %+d FECHADO" : "SQL %+d", (int)std::lround(R.sql));
                texto(f_.painel, 6.f, 798, y0 + 46, fechado ? P_VERMELHO : P_AMBAR, b);
            }
            if (R.mute) texto(f_.painel, 6.f, 872, y0 + 46, P_VERMELHO, "MUTE");
        }
        if (txVfo_ != ativo_) texto(f_.painel, 7.f, 892, 104, P_AMBAR, "SPLIT", 0.5f);
        if (tracking_) texto(f_.painel, 7.f, 944, 104, P_CIANO, "TRACK", 0.5f);
        arcoVfo();
    } else if (sDisp_ == 1) {
        SYSTEMTIME u, l; GetSystemTime(&u); GetLocalTime(&l);
        std::snprintf(b, sizeof b, "%02d:%02d:%02d", u.wHour, u.wMinute, u.wSecond);
        texto(f_.painel, 8.f, 892, 48, P_CIANO, "UTC", 0.5f);
        texto(f_.lcd, 36.f, 892, 60, P_BRANCO, b, 0.5f);
        std::snprintf(b, sizeof b, "%02d:%02d:%02d", l.wHour, l.wMinute, l.wSecond);
        texto(f_.painel, 8.f, 892, 136, P_CIANO, "LOCAL", 0.5f);
        texto(f_.lcd, 26.f, 892, 148, IM_COL32(0xc6, 0xcc, 0xd8, 255), b, 0.5f);
        std::snprintf(b, sizeof b, "%02d/%02d/%04d", l.wDay, l.wMonth, l.wYear);
        texto(f_.painel, 8.f, 892, 214, P_ROTULO2, b, 0.5f);
    } else {
        texto(f_.painel, 7.f, 792, 40, P_AMARELO, r_.decoders().tipo() == Decoders::NENHUM ? "DECODER: nenhum (DATA abre)" : "DECODER");
        std::vector<std::string> linhas;
        std::string a;
        for (char ch : decTexto_) { if (ch == '\n') { linhas.push_back(a); a.clear(); } else if ((unsigned char)ch >= 32) a += ch; }
        if (!a.empty()) linhas.push_back(a);
        const int n = 16;
        const int ini = std::max(0, (int)linhas.size() - n);
        float y = 52;
        for (int i = ini; i < (int)linhas.size(); ++i) {
            texto(f_.mono ? f_.mono : f_.painel, 7.4f, 792, y, P_VERDE, linhas[i].substr(0, 44).c_str());
            y += 11.6f;
        }
    }
    if (dim_ > 0) dl->AddRectFilled(M.p(786, 36), M.p(998, 244), IM_COL32(0, 0, 0, dim_ * 45));
}

// Escala em meia-lua embaixo da tela SUB: o alto de um tambor grande visto
// pela janela, com as marcas e os numeros. Gira junto com o VFO do receptor
// que esta sendo ouvido (o ponteiro vermelho fica parado no meio). Arrastar
// para os lados ou a roda do mouse em cima dela tambem sintonizam.
void Ui::arcoVfo()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const bool lig = r_.ligado();
    const Receptor& R0 = rx_[ativo_];
    const double f = (double)r_.vfo();
    const int g = grupoModo(r_.modo());
    // escala por modo: marca grande (com numero) e marca pequena
    const double maior = g <= 1 ? 1000.0 : (g == 4 ? 100000.0 : 10000.0);
    const double menor = maior / 10.0;
    constexpr double kGrausMaior = 4.0;                         // uma marca grande = 4 graus do tambor
    const double radMaior = kGrausMaior * kPi / 180.0;

    // a escala anda suave ate a frequencia nova; salto grande (troca de banda) vai direto
    const double dif = f - arcoF_;
    if (arcoF_ <= 0 || std::fabs(dif) > 40 * maior) arcoF_ = f;
    else arcoF_ += dif * std::min(1.0, (double)io.DeltaTime * 16.0);
    if (std::fabs(f - arcoF_) < menor * 0.002) arcoF_ = f;
    const double fv = arcoF_;

    // geometria (coordenadas do papel): circulo grande, so o alto aparece
    const float x0 = 788, x1 = 996, y0 = 168, y1 = 243;
    const float cx = 892, raio = 250, topo = 176;
    const float cy = topo + raio;
    const ImVec2 c = M.p(cx, cy);
    const float Rp = M.d(raio);
    dl->PushClipRect(M.p(x0, y0), M.p(x1, y1), true);
    const float a0 = (float)(-kPi / 2 - 0.5), a1 = (float)(-kPi / 2 + 0.5);
    // corpo do tambor, prateado: aneis do prata claro (borda de cima) ao
    // prata escuro; desligado fica cinza apagado
    // (vai ate o fim da tela: embaixo da escala tambem e prata)
    for (int i = 0; i < 16; ++i) {
        const float t = i / 15.f;
        int v = (int)(0xe2 - t * 0x6a);                 // 0xe2 -> 0x78
        if (!lig) v = v * 45 / 100;
        dl->PathArcTo(c, Rp + M.d(4.f - i * 4.6f), a0, a1, 64);
        dl->PathStroke(IM_COL32(v, std::min(255, v + 2), std::min(255, v + 6), 255), 0, M.d(5.6f));   // sobrepoe: sem riscos entre os aneis
    }
    dl->PathArcTo(c, Rp + M.d(1.5f), a0, a1, 64);
    dl->PathStroke(IM_COL32(255, 255, 255, lig ? 70 : 20), 0, M.d(3.f));         // reflexo do metal
    dl->PathArcTo(c, Rp + M.d(5.6f), a0, a1, 64);
    dl->PathStroke(IM_COL32(0xf4, 0xf6, 0xfa, lig ? 230 : 70), 0, M.d(0.9f));     // brilho na borda
    // marcas e numeros
    const ImU32 corMarca = lig ? IM_COL32(0x0c, 0x0c, 0x0e, 255) : IM_COL32(0x20, 0x20, 0x24, 255);   // preto no prata
    const long long k0 = (long long)std::floor((fv - 8 * maior) / menor), k1 = (long long)std::ceil((fv + 8 * maior) / menor);
    for (long long k = k0; k <= k1; ++k) {
        const double v = k * menor;
        const double th = (v - fv) / maior * radMaior;
        if (std::fabs(th) > 0.48) continue;
        const float s = (float)std::sin(th), co = (float)std::cos(th);
        const long long km = ((k % 10) + 10) % 10;
        const float len = km == 0 ? 10.f : (km == 5 ? 6.5f : 3.8f);
        const ImVec2 pa(c.x + s * (Rp + M.d(1)), c.y - co * (Rp + M.d(1)));
        const ImVec2 pb(c.x + s * (Rp - M.d(len)), c.y - co * (Rp - M.d(len)));
        dl->AddLine(pa, pb, corMarca, M.d(km == 0 ? 1.15f : 0.8f));
        if (km == 0) {
            char b[16];
            if (g == 4) std::snprintf(b, sizeof b, "%.1f", v / 1e6);
            else std::snprintf(b, sizeof b, "%03lld", ((long long)std::llround(v / 1000.0) % 1000 + 1000) % 1000);
            const float rt = raio - 18.5f;
            texto(f_.painel, 6.4f, cx + s * rt, cy - co * rt - 3.2f, corMarca, b, 0.5f);
        }
    }
    // sombra nas pontas (o tambor some para os lados)
    dl->AddRectFilledMultiColor(M.p(x0 - 3, y0), M.p(x0 + 34, y1), IM_COL32(3, 6, 10, 255), IM_COL32(3, 6, 10, 0),
                                IM_COL32(3, 6, 10, 0), IM_COL32(3, 6, 10, 255));
    dl->AddRectFilledMultiColor(M.p(x1 - 34, y0), M.p(x1 + 3, y1), IM_COL32(3, 6, 10, 0), IM_COL32(3, 6, 10, 255),
                                IM_COL32(3, 6, 10, 255), IM_COL32(3, 6, 10, 0));
    // meia-lua escura no meio da borda de baixo (o cubo do tambor, como no radio)
    {
        const ImVec2 cm = M.p(cx, y1 + 5.f);
        const ImVec2 r0(M.d(50.f), M.d(17.f));
        dl->AddEllipseFilled(cm, ImVec2(r0.x + M.d(2.2f), r0.y + M.d(2.2f)), IM_COL32(0x58, 0x5c, 0x64, lig ? 255 : 160), 0.f, 64);   // borda clara
        dl->AddEllipseFilled(cm, r0, IM_COL32(0x0c, 0x0d, 0x10, 255), 0.f, 64);
        dl->AddEllipseFilled(ImVec2(cm.x, cm.y + M.d(3.f)), ImVec2(r0.x * 0.82f, r0.y * 0.7f), IM_COL32(0x1c, 0x1e, 0x24, 255), 0.f, 64);   // fundo com volume
        dl->PathEllipticalArcTo(cm, ImVec2(r0.x - M.d(1.2f), r0.y - M.d(1.2f)), 0.f, (float)(-kPi * 0.92), (float)(-kPi * 0.08), 48);
        dl->PathStroke(IM_COL32(255, 255, 255, lig ? 40 : 15), 0, M.d(0.8f));                                               // reflexo fino
    }
    // ponteiro fixo no meio
    const ImU32 corPont = lig ? P_VERMELHO : P_APAGADO;
    dl->AddLine(M.p(cx, topo - 6), M.p(cx, topo + 11.5f), alfa(corPont, 110), M.d(2.6f));   // para acima dos numeros
    dl->AddLine(M.p(cx, topo - 6), M.p(cx, topo + 11.5f), corPont, M.d(1.1f));
    dl->AddTriangleFilled(M.p(cx - 4, topo - 8), M.p(cx + 4, topo - 8), M.p(cx, topo - 2), corPont);
    dl->PopClipRect();
    // de qual receptor e a escala
    texto(f_.painel, 5.8f, x1 - 2, y0 - 3, ativo_ == 0 ? alfa(P_BRANCO, 150) : alfa(P_AMARELO, 170), ativo_ == 0 ? "MAIN" : "SUB", 1.f);

    // arrastar / roda do mouse
    ImGui::SetCursorScreenPos(M.p(x0, y0));
    ImGui::InvisibleButton("##arcoVfo", ImVec2(M.d(x1 - x0), M.d(y1 - y0)));
    const bool sobre = ImGui::IsItemHovered(), ativo = ImGui::IsItemActive();
    if (!lig || R0.lock || R0.memModo) { arcoResto_ = 0; return; }   // em memoria o dial passa canais: aqui nao
    int64_t passo = R0.fine ? 1 : (g == 2 ? 100 : 10);           // o mesmo passo do dial (sintonizarRx)
    if (g == 3) passo = R0.fine ? 10 : 500;
    if (g == 4) passo = R0.fine ? 100 : 5000;
    double dHz = 0;
    if (ativo && io.MouseDelta.x != 0) {
        const double hzPorPapel = maior / (raio * radMaior);   // a escala acompanha o mouse
        dHz -= (io.MouseDelta.x / M.k) * hzPorPapel;
    }
    if (sobre && io.MouseWheel != 0) {
        int n = (int)std::lround(io.MouseWheel);
        if (n == 0) n = io.MouseWheel > 0 ? 1 : -1;
        dHz += n * menor;
    }
    if (dHz != 0) {
        arcoResto_ += dHz;
        const int64_t p = (int64_t)(arcoResto_ / (double)passo);
        if (p) { sintonizarRx(ativo_, p); arcoResto_ -= (double)p * passo; }
    }
    if (!ativo) arcoResto_ = 0;
    if (sobre || ativo) {
        char d[160];
        std::snprintf(d, sizeof d, "Escala do VFO (%s): arraste para os lados para sintonizar\nRoda do mouse: %s por dente",
                      ativo_ == 0 ? "MAIN" : "SUB", fmtPasso((int)menor).c_str());
        ImGui::SetTooltip("%s", d);
    }
}

// Centro: tela SUB, RX/TX, M>S, SPLIT, M/S, TF-SET e o dial
void Ui::areaCentral()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    telaSub();
    texto(f_.painel, 6.6f, 806, 258, P_ROTULO, "MAIN", 0.5f);
    texto(f_.painel, 6.6f, 976, 258, P_ROTULO, "SUB", 0.5f);
    if (tecla("kMrx", "RX", 782, 268, 48, 19, ativo_ == 0 ? 2 : 1, nullptr, "MAIN RX: ouvir o receptor MAIN")) ativar(0);
    if (tecla("kMtx", "TX", 782, 296, 48, 19, txVfo_ == 0 ? 4 : 1, nullptr, "MAIN TX: marca o MAIN como frequência de \"TX\" (SPLIT / TF-SET)")) txVfo_ = 0;
    if (tecla("kMs", "M>S", 838, 270, 48, 19, 0, nullptr, "M>S: copia o MAIN para o SUB (frequência, modo e filtro)")) {
        sincronizarAtivo();
        rx_[1].f = rx_[0].f; rx_[1].modo = rx_[0].modo; rx_[1].fil = rx_[0].fil;
        if (ativo_ == 1) { r_.setModo(rx_[1].modo); sintonizar(rx_[1].f, false); aplicarFiltro(); }
    }
    texto(f_.painel, 6.6f, 893, 258, P_ROTULO, "SPLIT", 0.5f);
    dl->AddRectFilled(M.p(880, 268), M.p(906, 272), txVfo_ != ativo_ ? P_LARANJA : IM_COL32(0x2a, 0x24, 0x1c, 255), M.d(1));
    if (tecla("kMsw", "M/S", 896, 278, 48, 19, 0, nullptr,
              "M/S: troca as FREQUÊNCIAS do MAIN e do SUB (frequência, modo e filtro)\n"
              "Volume, squelch, NB/NR e os outros botões de cada coluna ficam onde estão, como no rádio.\n"
              "Para OUVIR o outro receptor use MAIN RX / SUB RX (o LED verde mostra qual está tocando)")) {
        trocarMainSub();
    }
    if (tecla("kSrx", "RX", 952, 268, 48, 19, ativo_ == 1 ? 2 : 1, nullptr, "SUB RX: ouvir o receptor SUB")) ativar(1);
    if (tecla("kStx", "TX", 952, 296, 48, 19, txVfo_ == 1 ? 4 : 1, nullptr, "SUB TX: marca o SUB como frequência de \"TX\" (SPLIT)")) txVfo_ = 1;
    {   // TF-SET: segurando, escuta a frequencia de "TX"
        tecla("kTf", "TF-\nSET", 958, 326, 42, 30, tfSet_ ? 3 : 0, nullptr, "TF-SET: enquanto segura, escuta a frequência marcada como TX");
        const bool seg = ImGui::IsItemActive();
        if (seg && !tfSet_ && txVfo_ != ativo_) { tfSet_ = true; tfAtivoAntes_ = ativo_ != 0; ativar(txVfo_); }
        else if (!seg && tfSet_) { tfSet_ = false; ativar(tfAtivoAntes_ ? 1 : 0); }
    }
    const int p = dial("dMain", 880, 478, 98, angDialA_, rx_[0].lock,
                       "Dial principal: sintoniza o MAIN (arraste girando ou use a roda do mouse)\nGirar rápido acelera. FINE = passos de 1 Hz");
    if (p) sintonizarRx(0, p);
}

// Bloco das bandas, memoria, gravador e as teclas do dial principal
void Ui::blocoBandas()
{
    auto& c = Config::instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tecla("kRxant", "RX ANT", 1022, 34, 46, 18, c.biasT() ? 2 : 0, nullptr, "RX ANT: liga a alimentação da antena ativa (Bias-T do dongle)")) {
        c.set("bias_t", !c.biasT()); r_.aplicarConfig();
    }
    if (tecla("kDrv", "DRV", 1074, 34, 46, 18, c.dcRemove() ? 2 : 1, nullptr, "DRV: remove o risco do DC (centro do dongle) da imagem e do som"))
        c.set("dc_remove", !c.dcRemove());
    {
        char d[160];
        if (r_.xvtr()) std::snprintf(d, sizeof d, "XVTR: conversor de frequência (upconverter) - %.2f MHz", r_.xvtr() / 1e6);
        else std::snprintf(d, sizeof d, "XVTR: conversor de frequência (upconverter) - desligado");
        if (tecla("kXvtr", "XVTR", 1126, 34, 46, 18, r_.xvtr() ? 3 : 0, nullptr, d)) ImGui::OpenPopup("##xvtr");
    }
    const float cols[3] = {1022, 1074, 1126};
    const float linhasY[3] = {66, 102, 138};
    int bandaAtual = -1;
    for (int b = 0; b < 10; ++b) if (r_.vfo() >= kBandasHam[b].lo && r_.vfo() <= kBandasHam[b].hi) bandaAtual = b;
    int k = 0;
    for (int lin = 0; lin < 3; ++lin)
        for (int col = 0; col < 3; ++col, ++k) {
            const float x = cols[col], y = linhasY[lin];
            dl->AddRectFilled(M.p(x, y - 2), M.p(x + 46, y + 10), P_AZULBANDA, M.d(1));
            textoCabe(f_.painel, 6.4f, x + 23, y - 2.2f, IM_COL32(0x08, 0x0c, 0x28, 255), kBandasHam[k].rot, 0.5f, 40.f);
            char id[12], dg[4], d[140]; std::snprintf(id, sizeof id, "kB%d", k); std::snprintf(dg, sizeof dg, "%d", k + 1);
            std::snprintf(d, sizeof d, "Banda de %s MHz (volta onde estava). Com ENT: número %d", kBandasHam[k].rot, k + 1);
            if (tecla(id, entrando_ ? dg : "", x, y + 11, 46, 18, bandaAtual == k ? 2 : 0, nullptr, d)) {
                if (entrando_) digito(k + 1); else irBanda(k);
            }
        }
    dl->AddRectFilled(M.p(1074, 172), M.p(1120, 184), P_AZULBANDA, M.d(1));
    textoCabe(f_.painel, 6.4f, 1097, 171.8f, IM_COL32(0x08, 0x0c, 0x28, 255), "50", 0.5f, 40.f);
    if (tecla("kB9", entrando_ ? "0" : "", 1074, 185, 46, 18, bandaAtual == 9 ? 2 : 0, nullptr, "Banda de 6 m (50 MHz). Com ENT: número 0")) {
        if (entrando_) digito(0); else irBanda(9);
    }
    if (tecla("kClr", "CLR", 1022, 185, 46, 18, 0, nullptr, "CLR: apaga o que foi digitado (com ENT) / cancela")) {
        if (entrando_ && !entrada_.empty()) entrada_.clear(); else entrando_ = false;
    }
    if (tecla("kEnt", "ENT", 1126, 185, 46, 18, entrando_ ? 3 : 0, nullptr,
              "ENT: digitar a frequência em kHz pelas teclas de banda (1.8=1 ... 50=0) ou pelo teclado, e ENT de novo")) {
        if (entrando_) confirmarEntrada(); else { entrando_ = true; entrada_.clear(); }
    }
    if (tecla("kGen1", "GEN 1", 1022, 216, 46, 18, 0, nullptr, "GEN 1: cobertura geral em HF - radiodifusão em ondas curtas, médias e utilitárias"))
        ImGui::OpenPopup("##gen1");
    if (tecla("kGen2", "GEN 2", 1074, 216, 46, 18, 0, nullptr, "GEN 2: VHF / UHF - FM, aviação, 2 m, marítimo, 70 cm, PMR..."))
        ImGui::OpenPopup("##gen2");
    if (tecla("kMenu", "MENU", 1126, 216, 46, 18, 0, nullptr, "MENU: configuração, memórias, decoders, idioma, sobre", IM_COL32(0xff, 0xb0, 0x20, 255)))
        ImGui::OpenPopup("##menu990");
    if (tecla("kSelM", "<MAIN", 1018, 254, 50, 22, ativo_ == 0 ? 2 : 1, nullptr, "<MAIN: o receptor MAIN passa a ser o ouvido e o comandado")) ativar(0);
    if (tecla("kUp", "UP", 1076, 244, 42, 18, 0, nullptr, "UP: sobe 1 MHz")) sintonizar(r_.vfo() + 1000000, false);
    if (tecla("kDown", "DOWN", 1076, 268, 42, 18, 0, nullptr, "DOWN: desce 1 MHz")) { if (r_.vfo() > 1001000) sintonizar(r_.vfo() - 1000000, false); }
    if (tecla("kSelS", "SUB>", 1126, 254, 50, 22, ativo_ == 1 ? 2 : 1, nullptr, "SUB>: o receptor SUB passa a ser o ouvido e o comandado")) ativar(1);
    auto titulo = [&](float y, const char* t) {
        const float tw = larguraTexto(f_.painel, 6.4f, t);
        dl->AddLine(M.p(1022, y + 4), M.p(1097 - tw * 0.5f - 3, y + 4), P_ROTULO2, M.d(0.7f));
        dl->AddLine(M.p(1097 + tw * 0.5f + 3, y + 4), M.p(1172, y + 4), P_ROTULO2, M.d(0.7f));
        texto(f_.painel, 6.4f, 1097, y, P_ROTULO, t, 0.5f);
    };
    titulo(294, "MEMORY");
    Receptor& R = rx_[ativo_];
    if (tecla("kMv", "M/V", 1022, 304, 46, 18, R.memModo ? 3 : 0, nullptr, "M/V: alterna entre VFO e canais de memória (o MULTI/CH e o dial passam os canais)")) {
        if (mem_.empty()) aviso("Nenhuma memória gravada (M.IN grava)");
        else {
            R.memModo = !R.memModo;
            if (R.memModo) {
                int mais = 0; long long melhor = LLONG_MAX;
                for (int i = 0; i < (int)mem_.size(); ++i) {
                    const long long dd = std::llabs((long long)mem_[i].f - (long long)r_.vfo());
                    if (dd < melhor) { melhor = dd; mais = i; }
                }
                R.memCanal = mais; sintonizarMemoria((size_t)mais);
            }
        }
    }
    if (tecla("kMin", "M.IN", 1074, 304, 46, 18, 0, nullptr, "M.IN: grava a frequência atual numa memória (com nome)")) {
        memAberta_ = true;
        std::snprintf(memNome_, sizeof memNome_, "%s %s", fmtFreq(r_.vfo()).c_str(), r_.modo().c_str());
    }
    if (tecla("kMv2", "M>V", 1126, 304, 46, 18, 0, nullptr, "M>V: copia o canal de memória para o VFO e volta ao VFO")) R.memModo = false;
    titulo(330, "RECORDER");
    if (tecla("kRec", "REC", 1022, 340, 46, 18, r_.som().gravando() ? 4 : 0, nullptr, "REC: grava o que se ouve (WAV na pasta Gravacoes)")) {
        if (!r_.som().gravando()) alternarGravacao();
    }
    if (tecla("kStop", "STOP", 1074, 340, 46, 18, 0, nullptr, "STOP: para a gravação ou a reprodução")) {
        if (r_.som().gravando()) alternarGravacao();
        PlaySoundA(nullptr, nullptr, 0);
    }
    if (tecla("kPlay", "PLAY", 1126, 340, 46, 18, 0, nullptr, "PLAY: toca a última gravação")) tocarUltimaGravacao();
    titulo(366, "QUICK MEMO");
    {
        const char* nm[3] = {"MAIN/SUB", "RELÓGIO", "DECODER"};
        char d[120]; std::snprintf(d, sizeof d, "S.DISP: o que a tela pequena mostra (%s)", nm[sDisp_]);
        if (tecla("kSdisp", "S.DISP", 1022, 376, 46, 18, 0, nullptr, d)) sDisp_ = (sDisp_ + 1) % 3;
    }
    {
        char d[120]; std::snprintf(d, sizeof d, "MR (QUICK MEMO): chama as memórias rápidas, uma por clique (%d gravadas)", (int)qm_.size());
        if (tecla("kQmr", "MR", 1074, 376, 46, 18, 0, nullptr, d)) {
            if (qm_.empty()) aviso("QUICK MEMO vazia (M.IN ao lado grava)");
            else {
                qmIdx_ = (qmIdx_ + 1) % (int)qm_.size();
                mudarModoPainel(qm_[qmIdx_].modo);
                sintonizar(qm_[qmIdx_].f, false);
                char a[48]; std::snprintf(a, sizeof a, "QUICK MEMO %d", qmIdx_ + 1); aviso(a);
            }
        }
    }
    if (tecla("kQmin", "M.IN", 1126, 376, 46, 18, 0, nullptr, "M.IN (QUICK MEMO): guarda a frequência atual nas memórias rápidas (até 10)")) {
        qm_.insert(qm_.begin(), Rapida{r_.vfo(), r_.modo()});
        if (qm_.size() > 10) qm_.resize(10);
        qmIdx_ = -1;
        aviso("Guardado na QUICK MEMO");
    }
    // teclas do dial principal
    Receptor& A = rx_[0];
    if (tecla("kCwtA", "", 1018, 424, 36, 15, 0, "CW T.", "CW T.: sintonia automática no pico mais próximo (CW / AM / FM) - MAIN")) { if (ativo_ != 0) ativar(0); cwTune(); }
    {
        char d[96]; std::snprintf(d, sizeof d, "FIL/SEL: troca o filtro do MAIN (A / B / C) - agora %s", kNomesFil[A.fil]);
        if (tecla("kFilA", "", 1062, 424, 36, 15, 0, "FIL/SEL", d)) { A.fil = (A.fil + 1) % 3; if (ativo_ == 0) aplicarFiltro(); }
    }
    if (tecla("kFineA", "", 1018, 456, 36, 15, A.fine ? 2 : 0, "FINE", "FINE: passos finos no dial do MAIN (1 Hz)")) A.fine = !A.fine;
    if (tecla("kVoiceA", "", 1018, 488, 36, 15, 0, "VOICE", "VOICE: fala a frequência e o modo do MAIN")) falarFrequencia(0);
    if (tecla("kLockA", "", 1018, 520, 36, 15, A.lock ? 4 : 0, "LOCK", "LOCK: trava o dial do MAIN")) A.lock = !A.lock;

    if (ImGui::BeginPopup("##xvtr")) {
        ImGui::TextDisabled("Conversor de frequência (upconverter) antes do dongle");
        const struct { const char* n; uint64_t hz; } op[] = {{"Desligado", 0}, {"125 MHz (Ham It Up, SpyVerter)", 125000000},
                                                             {"120 MHz", 120000000}, {"100 MHz", 100000000}, {"106,25 MHz", 106250000}};
        for (const auto& o : op)
            if (ImGui::Selectable(o.n, r_.xvtr() == o.hz)) {
                r_.setXvtr(o.hz);
                if (o.hz) c.set("qmode", std::string("off"));
                r_.aplicarConfig();
            }
        ImGui::EndPopup();
    }
    auto listaGen = [&](const char* id, const ItemGen* it, size_t n) {
        if (!ImGui::BeginPopup(id)) return;
        for (size_t i = 0; i < n; ++i) {
            if (it[i].grupo) ImGui::SeparatorText(it[i].grupo);
            char r[160], mhz[24];
            std::snprintf(mhz, sizeof mhz, "%.4f", it[i].hz / 1e6);
            for (char* p = mhz; *p; ++p) if (*p == '.') *p = ',';
            std::snprintf(r, sizeof r, "%11s MHz   %-38s %s", mhz, it[i].nome, it[i].modo);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(r)) {
                rx_[ativo_].memModo = false;
                if (std::string(it[i].modo) != "WFM" && it[i].hz < 30000000) Config::instance().set("qmode", std::string("auto"));
                mudarModoPainel(it[i].modo);
                sintonizar(it[i].hz, false);
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    };
    listaGen("##gen1", kGen1, sizeof kGen1 / sizeof kGen1[0]);
    listaGen("##gen2", kGen2, sizeof kGen2 / sizeof kGen2[0]);
}

// Bloco do meio: RIT/XIT, MULTI/CH, TRACKING, AGC, RXEQ/TXEQ, dial do SUB
void Ui::blocoMeio()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    texto(f_.painel, 6.8f, 1237, 28, P_ROTULO, "RIT/XIT", 0.5f);
    {
        const int p = encoder("eRit", 1237, 70, 28, angRit_,
                              "RIT/XIT: ajusta o deslocamento da escuta (10 Hz por passo)\nRIT = no MAIN, XIT = no SUB (ligue nas teclas abaixo)");
        if (p) {
            bool algum = false;
            for (int i = 0; i < 2; ++i) if (rx_[i].ritOn) { rx_[i].rit = std::clamp(rx_[i].rit + p * 10, -9990, 9990); algum = true; }
            if (!algum) aviso("Ligue o RIT ou o XIT primeiro");
        }
    }
    if (tecla("kRit", "RIT", 1190, 112, 30, 17, rx_[0].ritOn ? 2 : 0, nullptr, "RIT: liga o deslocamento da escuta no MAIN")) rx_[0].ritOn = !rx_[0].ritOn;
    if (tecla("kXit", "XIT", 1224, 112, 30, 17, rx_[1].ritOn ? 2 : 0, nullptr, "XIT: (num receptor) liga o deslocamento da escuta no SUB")) rx_[1].ritOn = !rx_[1].ritOn;
    if (tecla("kCl", "CL", 1258, 112, 30, 17, 0, nullptr, "CL: zera o RIT e o XIT")) { rx_[0].rit = rx_[1].rit = 0; }
    texto(f_.painel, 6.8f, 1237, 142, P_ROTULO, "MULTI/CH", 0.5f);
    {
        bool clique = false;
        char d[220];
        std::snprintf(d, sizeof d, "MULTI/CH: sintoniza em passos de %s (clique no botão para trocar o passo)\nEm memória (M/V): passa os canais",
                      fmtPasso(passo_).c_str());
        const int p = encoder("eMulti", 1237, 184, 28, angMulti_, d, &clique);
        if (clique) ImGui::OpenPopup("##passo");
        Receptor& R = rx_[ativo_];
        if (p) {
            if (R.memModo && !mem_.empty()) {
                R.memCanal = ((R.memCanal + p) % (int)mem_.size() + (int)mem_.size()) % (int)mem_.size();
                sintonizarMemoria((size_t)R.memCanal);
            } else {
                const int64_t ps = std::max(1, passo_);
                const int64_t nf = ((int64_t)r_.vfo() / ps + p) * ps;
                if (nf > 1000) sintonizar((uint64_t)nf, false);
            }
        }
        dl->AddCircleFilled(M.p(1272, 152), M.d(2), R.memModo ? P_LARANJA : IM_COL32(0x40, 0x30, 0x20, 255), 8);
    }
    if (ImGui::BeginPopup("##passo")) {
        ImGui::TextDisabled("Passo do MULTI/CH e da roda do mouse no espectro");
        const int ps[] = {10, 50, 100, 500, 1000, 2500, 5000, 6250, 8330, 9000, 10000, 12500, 25000, 100000};
        for (int p : ps) if (ImGui::Selectable(fmtPasso(p).c_str(), passo_ == p)) passo_ = p;
        ImGui::EndPopup();
    }
    Receptor& A = rx_[0];
    if (tecla("kTrack", "", 1190, 246, 42, 15, tracking_ ? 2 : 0, "TRACKING", "TRACKING: o SUB anda junto com o MAIN quando se gira o dial")) tracking_ = !tracking_;
    if (tecla("kPfb", "", 1246, 246, 42, 15, memAberta_ ? 2 : 0, "PF B", "PF B: abre a lista de MEMÓRIAS (procurar, editar, excluir)")) memAberta_ = !memAberta_;
    {
        const char* ag[4] = {"", "SLOW", "MID", "FAST"};
        char d[120]; std::snprintf(d, sizeof d, "AGC/SEL: velocidade do AGC do MAIN (SLOW / MID / FAST) - agora %s", ag[std::clamp(A.agc, 1, 3)]);
        if (tecla("kAgcA", "", 1190, 278, 42, 15, A.agcOff ? 1 : 2, "AGC/SEL", d)) { A.agc = A.agc % 3 + 1; A.agcOff = false; }
    }
    if (tecla("kAgcoA", "", 1246, 278, 42, 15, A.agcOff ? 4 : 0, "AGC OFF", "AGC OFF: desliga o AGC do MAIN (o volume passa a depender do RF)")) A.agcOff = !A.agcOff;
    {
        const char* nm[5] = {"OFF", "HB1 agudo", "HB2 mais agudo", "BB1 grave", "BB2 mais grave"};
        char d[120]; std::snprintf(d, sizeof d, "RXEQ/SEL: equalização do áudio - %s", nm[rxeq_]);
        if (tecla("kRxeq", "", 1190, 310, 42, 15, rxeq_ ? 2 : 0, "RXEQ/SEL", d)) {
            rxeq_ = (rxeq_ + 1) % 5;
            const float tons[5] = {50, 68, 85, 32, 15};
            tom_ = tons[rxeq_];
        }
    }
    {
        const char* nm[4] = {"Eclipse", "Oceano", "Clássica", "Cinza"};
        char d[120]; std::snprintf(d, sizeof d, "TXEQ/SEL (cores): paleta da cachoeira - %s", nm[paleta_]);
        if (tecla("kTxeq", "", 1246, 310, 42, 15, 0, "TXEQ/SEL", d)) {
            paleta_ = (paleta_ + 1) % 4; g_paletaCachoeira = paleta_;
            lutRef_ = 1e9f; lutRange_ = 1e9f;      // refaz as cores (e recolore tudo)
        }
    }
    const int p = dial("dSub", 1118, 516, 50, angDialB_, rx_[1].lock, "Dial do SUB: sintoniza o receptor SUB");
    if (p) sintonizarRx(1, p);
    Receptor& B = rx_[1];
    if (tecla("kCwtB", "", 1190, 424, 36, 15, 0, "CW T.", "CW T.: sintonia automática no pico mais próximo - SUB")) { if (ativo_ != 1) ativar(1); cwTune(); }
    {
        char d[96]; std::snprintf(d, sizeof d, "FIL/SEL: troca o filtro do SUB (A / B / C) - agora %s", kNomesFil[B.fil]);
        if (tecla("kFilB", "", 1246, 424, 36, 15, 0, "FIL/SEL", d)) { B.fil = (B.fil + 1) % 3; if (ativo_ == 1) aplicarFiltro(); }
    }
    if (tecla("kFineB", "", 1222, 456, 36, 15, B.fine ? 2 : 0, "FINE", "FINE: passos finos no dial do SUB")) B.fine = !B.fine;
    if (tecla("kVoiceB", "", 1222, 488, 36, 15, 0, "VOICE", "VOICE: fala a frequência e o modo do SUB")) falarFrequencia(1);
    if (tecla("kLockB", "", 1222, 520, 36, 15, B.lock ? 4 : 0, "LOCK", "LOCK: trava o dial do SUB")) B.lock = !B.lock;
}

// Coluna do MAIN (NB1/NB2, NOTCH/SQL, NCH, BEF, A.NCH, BC, HI/LO, APF, MUTE, AF/RF)
void Ui::colunaMain()
{
    Receptor& A = rx_[0];
    float di, dO; char d[240];
    std::snprintf(d, sizeof d, "NB1: nível do apagador de pulsos no áudio (%d%%)\nNB2: nível do apagador de pulsos no IQ (%d%%)",
                  (int)std::lround(nbNivel_), (int)std::lround(nb2Nivel_ * 100));
    if (giro("gNb", 1345, 66, 22, "NB1", "NB2", nbNivel_ / 100.f, nb2Nivel_, di, dO, d)) {
        nbNivel_ = std::clamp(nbNivel_ + di * 100.f, 0.f, 100.f);
        nb2Nivel_ = std::clamp(nb2Nivel_ + dO, 0.f, 1.f);
    }
    if (tecla("kNb1", "", 1302, 106, 38, 14, A.nb1 ? 2 : 0, "NB1", "NB1: apagador de pulsos (áudio) do MAIN")) A.nb1 = !A.nb1;
    if (tecla("kNb2", "", 1350, 106, 38, 14, A.nb2 ? 2 : 0, "NB2", "NB2: apagador de pulsos (IQ, antes do demodulador) do MAIN")) A.nb2 = !A.nb2;
    {
        const double nh = 150.0 * std::pow(3500.0 / 150.0, std::clamp(A.notch, 0.f, 1.f));
        char sq[24]; if (A.sql <= -90) std::snprintf(sq, sizeof sq, "aberto"); else std::snprintf(sq, sizeof sq, "%d dB", (int)A.sql);
        std::snprintf(d, sizeof d, "NOTCH: frequência do notch / BEF do MAIN (%.0f Hz)\nSQL: squelch do MAIN (%s)", nh, sq);
    }
    if (giro("gNsA", 1345, 180, 22, "NOTCH", "SQL", A.notch, (A.sql + 90.f) / 120.f, di, dO, d)) {
        A.notch = std::clamp(A.notch + di, 0.f, 1.f);
        A.sql = std::clamp(A.sql + dO * 120.f, -90.f, 30.f);
    }
    {
        const int k = tecla("kNchA", "", 1302, 220, 38, 14, A.nch ? (A.nchLargo ? 3 : 2) : 0, "NCH/SEL",
                            "NCH: notch manual do MAIN (posição no botão NOTCH)\nSegurar (SEL): estreito / largo");
        if (k == 1) A.nch = !A.nch;
        if (k == 2) { A.nch = true; A.nchLargo = !A.nchLargo; }
    }
    if (tecla("kBefA", "", 1350, 220, 38, 14, A.bef ? 2 : 0, "BEF/SEL", "BEF: filtro eliminador de faixa (largo e fundo) do MAIN, no NOTCH")) A.bef = !A.bef;
    if (tecla("kAnchA", "", 1302, 250, 38, 14, A.anch ? 2 : 0, "A.NCH/SEL", "A.NCH: notch automático - tira a portadora/apito sozinho (MAIN)")) A.anch = !A.anch;
    if (tecla("kBcA", "", 1350, 250, 38, 14, A.bc ? 2 : 0, "BC", "BC: cancelador de batimentos - tira vários apitos (MAIN)")) A.bc = !A.bc;
    {   // HI/SHIFT - LO/WIDTH: vale para o receptor ouvido (LED SUB aceso = SUB)
        Receptor& R = rx_[ativo_];
        const int g = grupoModo(r_.modo());
        float& hi = filHi_[g][R.fil];
        float& lo = filLo_[g][R.fil];
        float tIn = 0.5f, tOut = 0.5f;
        switch (g) {
        case 0: std::snprintf(d, sizeof d, "HI: corte alto do filtro (%.2f kHz)\nLO: corte baixo do filtro (%.2f kHz)  - FIL %s", hi / 1000.f, lo / 1000.f, kNomesFil[R.fil]);
                tIn = (hi - 1000) / 4000.f; tOut = lo / 1000.f; break;
        case 1: std::snprintf(d, sizeof d, "SHIFT: desloca a faixa do CW (%+.0f Hz)\nWIDTH: largura do filtro de CW (%.0f Hz)  - FIL %s", lo, hi, kNomesFil[R.fil]);
                tIn = (lo + 800) / 1600.f; tOut = (hi - 50) / 2450.f; break;
        case 2: std::snprintf(d, sizeof d, "HI: corte alto do AM (%.1f kHz)\nLO: corte baixo do AM (%.2f kHz)  - FIL %s", hi / 1000.f, lo / 1000.f, kNomesFil[R.fil]);
                tIn = (hi - 2000) / 6000.f; tOut = lo / 1000.f; break;
        case 3: std::snprintf(d, sizeof d, "WIDTH: largura do FM (%.1f kHz)  - FIL %s", hi / 1000.f, kNomesFil[R.fil]);
                tIn = (hi - 6000) / 24000.f; break;
        default: std::snprintf(d, sizeof d, "WIDTH: largura do FM largo (%.0f kHz)  - FIL %s", hi / 1000.f, kNomesFil[R.fil]);
                tIn = (hi - 100000) / 150000.f; break;
        }
        if (giro("gHilo", 1340, 334, 28, "HI/SHIFT", "LO/WIDTH", tIn, tOut, di, dO, d)) {
            switch (g) {
            case 0: hi = std::clamp(hi + di * 4000.f, 1000.f, 5000.f); lo = std::clamp(lo + dO * 1000.f, 0.f, 1000.f);
                    hi = std::max(hi, lo + 400.f); break;
            case 1: lo = std::clamp(lo + di * 1600.f, -800.f, 800.f); hi = std::clamp(hi + dO * 2450.f, 50.f, 2500.f); break;
            case 2: hi = std::clamp(hi + di * 6000.f, 2000.f, 8000.f); lo = std::clamp(lo + dO * 1000.f, 0.f, 1000.f); break;
            case 3: hi = std::clamp(hi + di * 24000.f, 6000.f, 30000.f); break;
            default: hi = std::clamp(hi + di * 150000.f, 100000.f, 250000.f); break;
            }
            aplicarFiltro();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        texto(f_.painel, 5.f, 1384, 350, P_ROTULO, "SUB", 0.5f);
        dl->AddCircleFilled(M.p(1384, 362), M.d(2.4f), ativo_ == 1 ? P_VERDE : IM_COL32(0x20, 0x30, 0x24, 255), 10);
    }
    if (tecla("kApfA", "", 1302, 388, 38, 14, A.apf ? 2 : 0, "APF/SEL", "APF: filtro de pico de áudio para CW, no CW PITCH (MAIN)")) A.apf = !A.apf;
    if (tecla("kMuteA", "", 1350, 388, 38, 14, A.mute ? 4 : 0, "MUTE", "MUTE: silencia o MAIN")) A.mute = !A.mute;
    std::snprintf(d, sizeof d, "AF: volume do MAIN (%d%%)\nRF: ganho de RF do dongle no MAIN (%.1f dB)", (int)std::lround(A.af), 49.6f * A.rf / 100.f);
    if (giro("gAfA", 1345, 478, 27, "AF", "RF", A.af / 100.f, A.rf / 100.f, di, dO, d)) {
        A.af = std::clamp(A.af + di * 100.f, 0.f, 100.f);
        A.rf = std::clamp(A.rf + dO * 100.f, 0.f, 100.f);
    }
}

// Coluna do SUB (NR1/NR2 no alto, e o SUB RECEIVER)
void Ui::colunaSub()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Receptor& A = rx_[0];
    Receptor& B = rx_[1];
    float di, dO; char d[240];
    std::snprintf(d, sizeof d, "NR1: força do redutor de ruído (%d%%)\nNR2: força do NR espectral (%d%%)",
                  (int)std::lround(nrNivel_), (int)std::lround(nrEspForca_));
    if (giro("gNr", 1447, 66, 22, "NR1", "NR2", nrNivel_ / 100.f, nrEspForca_ / 100.f, di, dO, d)) {
        nrNivel_ = std::clamp(nrNivel_ + di * 100.f, 0.f, 100.f);
        nrEspForca_ = std::clamp(nrEspForca_ + dO * 100.f, 0.f, 100.f);
    }
    if (tecla("kNr1", "", 1404, 106, 38, 14, A.nr1 ? 2 : 0, "NR1", "NR1: redutor de ruído do MAIN")) A.nr1 = !A.nr1;
    if (tecla("kNr2", "", 1452, 106, 38, 14, A.nr2 ? 2 : 0, "NR2", "NR2 = NR ESPECTRAL (o mesmo do RXSDR) no MAIN - força no anel do botão NR1/NR2")) A.nr2 = !A.nr2;
    {
        const double nh = 150.0 * std::pow(3500.0 / 150.0, std::clamp(B.notch, 0.f, 1.f));
        char sq[24]; if (B.sql <= -90) std::snprintf(sq, sizeof sq, "aberto"); else std::snprintf(sq, sizeof sq, "%d dB", (int)B.sql);
        std::snprintf(d, sizeof d, "NOTCH: frequência do notch / BEF do SUB (%.0f Hz)\nSQL: squelch do SUB (%s)", nh, sq);
    }
    if (giro("gNsB", 1447, 180, 22, "NOTCH", "SQL", B.notch, (B.sql + 90.f) / 120.f, di, dO, d)) {
        B.notch = std::clamp(B.notch + di, 0.f, 1.f);
        B.sql = std::clamp(B.sql + dO * 120.f, -90.f, 30.f);
    }
    {
        const int k = tecla("kNchB", "", 1404, 220, 38, 14, B.nch ? (B.nchLargo ? 3 : 2) : 0, "NCH/SEL", "NCH: notch manual do SUB\nSegurar (SEL): estreito / largo");
        if (k == 1) B.nch = !B.nch;
        if (k == 2) { B.nch = true; B.nchLargo = !B.nchLargo; }
    }
    if (tecla("kBefB", "", 1452, 220, 38, 14, B.bef ? 2 : 0, "BEF/SEL", "BEF: filtro eliminador de faixa do SUB")) B.bef = !B.bef;
    if (tecla("kAnchB", "", 1404, 250, 38, 14, B.anch ? 2 : 0, "A.NCH/SEL", "A.NCH: notch automático do SUB")) B.anch = !B.anch;
    if (tecla("kBcB", "", 1452, 250, 38, 14, B.bc ? 2 : 0, "BC", "BC: cancelador de batimentos do SUB")) B.bc = !B.bc;
    dl->AddRectFilled(M.p(1402, 272), M.p(1492, 284), IM_COL32(0x3a, 0x3c, 0x42, 255), M.d(1));
    dl->AddRect(M.p(1402, 272), M.p(1492, 284), IM_COL32(0x80, 0x84, 0x8c, 255), M.d(1), 0, M.d(0.7f));
    texto(f_.painel, 6.6f, 1447, 273.5f, P_BRANCO, "SUB RECEIVER", 0.5f);
    {
        const char* ag[4] = {"", "SLOW", "MID", "FAST"};
        char t[96]; std::snprintf(t, sizeof t, "AGC/SEL: velocidade do AGC do SUB - agora %s", ag[std::clamp(B.agc, 1, 3)]);
        if (tecla("kAgcB", "", 1404, 302, 38, 14, B.agcOff ? 1 : 2, "AGC/SEL", t)) { B.agc = B.agc % 3 + 1; B.agcOff = false; }
    }
    if (tecla("kAgcoB", "", 1452, 302, 38, 14, B.agcOff ? 4 : 0, "AGC OFF", "AGC OFF: desliga o AGC do SUB")) B.agcOff = !B.agcOff;
    if (tecla("kNb1B", "", 1404, 330, 38, 14, B.nb1 ? 2 : 0, "NB1/SEL", "NB1 do SUB (nível no botão NB1 do MAIN)")) B.nb1 = !B.nb1;
    if (tecla("kNb2B", "", 1452, 330, 38, 14, B.nb2 ? 2 : 0, "NB2/SEL", "NB2 do SUB")) B.nb2 = !B.nb2;
    if (tecla("kNr1B", "", 1404, 358, 38, 14, B.nr1 ? 2 : 0, "NR1/SEL", "NR1 do SUB (força no botão NR1)")) B.nr1 = !B.nr1;
    if (tecla("kNr2B", "", 1452, 358, 38, 14, B.nr2 ? 2 : 0, "NR2/SEL", "NR2 (NR espectral) do SUB")) B.nr2 = !B.nr2;
    if (tecla("kApfB", "", 1404, 388, 38, 14, B.apf ? 2 : 0, "APF/SEL", "APF do SUB")) B.apf = !B.apf;
    if (tecla("kMuteB", "", 1452, 388, 38, 14, B.mute ? 4 : 0, "MUTE", "MUTE: silencia o SUB")) B.mute = !B.mute;
    std::snprintf(d, sizeof d, "AF: volume do SUB (%d%%)\nRF: ganho de RF do dongle no SUB (%.1f dB)", (int)std::lround(B.af), 49.6f * B.rf / 100.f);
    if (giro("gAfB", 1447, 478, 27, "AF", "RF", B.af / 100.f, B.rf / 100.f, di, dO, d)) {
        B.af = std::clamp(B.af + di * 100.f, 0.f, 100.f);
        B.rf = std::clamp(B.rf + dO * 100.f, 0.f, 100.f);
        if (ativo_ != 1) aviso("Este AF/RF é do SUB. Ouvindo o MAIN: use o AF/RF da coluna MAIN (ao lado)", 3.0);
    }
}

// MENU (tecla MENU ou botao direito no chassi)
void Ui::popupsPainel()
{
    if (ImGui::BeginPopup("##menu990")) {
        ImGui::TextDisabled("SDR-990S  v" RXSDR_VERSAO);
        ImGui::Separator();
        if (ImGui::MenuItem("Configuração (aparelho, som, S-meter)")) abrirConfig();
        if (ImGui::MenuItem("Memórias")) memAberta_ = true;
        if (ImGui::MenuItem("Nomes das memórias no espectro", nullptr, memVisivel_)) memVisivel_ = !memVisivel_;
        if (ImGui::MenuItem("Decodificadores digitais")) decAberta_ = true;
        if (ImGui::MenuItem(r_.som().gravando() ? "Parar gravação" : "Gravar áudio")) alternarGravacao();
        ImGui::Separator();
        if (ImGui::MenuItem("Salvar configuração agora")) {
            salvarEstado(); Config::instance().salvar(true);
            aviso("Configuração salva");
        }
        if (ImGui::BeginMenu("Idioma / Language")) {
            if (ImGui::MenuItem("Português", nullptr, !emIngles())) { definirIngles(false); Config::instance().set("lang", std::string("pt")); }
            if (ImGui::MenuItem("English", nullptr, emIngles())) { definirIngles(true); Config::instance().set("lang", std::string("en")); }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Capturar a tela (PNG)", "F12")) { g_pedirCaptura = true; }
        if (ImGui::MenuItem("Sobre")) sobreAberta_ = true;
        ImGui::EndPopup();
    }
}

} // namespace masdr
