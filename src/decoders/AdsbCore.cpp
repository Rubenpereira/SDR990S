#include "AdsbCore.h"
#include "AdsbEmpresas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace masdr {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kGerador = 0xFFF409;     // CRC do Modo S (x^24 implicito)
constexpr double kMsgUs = 8.0 + 112.0 + 1.0; // preambulo + a mensagem longa (us)
constexpr size_t kMaxAvioes = 5000;        // a lista guarda tudo (registro) ate o Limpar; acima disto sai o mais antigo
constexpr double kConhecido = 60.0;         // s: o endereco vale para DF0/4/5/20/21 e correcao
constexpr double kParCpr = 10.0;            // s: par/impar para a decodificacao global

inline int bit(const uint8_t* m, int i) { return (m[i >> 3] >> (7 - (i & 7))) & 1; }

inline uint32_t campo(const uint8_t* m, int ini, int len)
{
    uint32_t v = 0;
    for (int i = 0; i < len; ++i) v = (v << 1) | (uint32_t)bit(m, ini + i);
    return v;
}

// NL(lat): numero de zonas de longitude da CPR (15 zonas de latitude)
int nl(double lat)
{
    lat = std::fabs(lat);
    if (lat < 1e-9) return 59;
    if (std::fabs(lat - 87.0) < 1e-9) return 2;
    if (lat > 87.0) return 1;
    const double a = 1.0 - std::cos(kPi / (2.0 * 15.0));
    const double b = std::cos(kPi / 180.0 * lat);
    const double x = 1.0 - a / (b * b);
    if (x <= -1.0) return 1;
    return (int)std::floor(2.0 * kPi / std::acos(x));
}

inline double modp(double a, double b) { const double r = std::fmod(a, b); return r < 0 ? r + b : r; }

double distKm(double la1, double lo1, double la2, double lo2)
{
    const double r = kPi / 180.0;
    const double dla = (la2 - la1) * r, dlo = (lo2 - lo1) * r;
    const double h = std::sin(dla / 2) * std::sin(dla / 2) +
                     std::cos(la1 * r) * std::cos(la2 * r) * std::sin(dlo / 2) * std::sin(dlo / 2);
    return 6371.0 * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
}

const char kCharsVoo[] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ##### ###############0123456789######";

} // namespace

// ---------------------------------------------------------------------------
AdsbCore::AdsbCore()
{
    // a sindrome de cada erro de 1 bit numa mensagem de 112 bits
    for (int j = 0; j < 112; ++j) {
        uint8_t m[14] = {};
        m[j >> 3] = (uint8_t)(0x80 >> (j & 7));
        umBit112_[sindrome(m, 112)] = j;
    }
    limpar();
}

void AdsbCore::limpar()
{
    std::lock_guard<std::mutex> lk(mtx_);
    avioes_.clear();
    fila_.clear();
    texto_.clear();
    est_ = Estado();
    contMsgs_ = janelaMsgs_ = 0;
    mag_.clear();
    dc_ = {0, 0};
    ruido_ = sinal_ = 0;
}

// ---------------------------------------------------------------------------
//  a paridade: o resto da divisao da mensagem pelo gerador (0 = sem erro)
// ---------------------------------------------------------------------------
uint32_t AdsbCore::sindrome(const uint8_t* msg, int nbits)
{
    uint32_t r = 0;
    for (int i = 0; i < nbits - 24; ++i) {
        const uint32_t topo = ((r >> 23) & 1u) ^ (uint32_t)bit(msg, i);
        r = (r << 1) & 0xFFFFFFu;
        if (topo) r ^= kGerador;
    }
    return r ^ campo(msg, nbits - 24, 24);
}

// ---------------------------------------------------------------------------
//  altitude (13 bits do DF0/4/16/20; os 12 do ADS-B entram com M = 0)
//  bits: C1 A1 C2 A2 C4 A4 M B1 Q B2 D2 B4 D4
// ---------------------------------------------------------------------------
int AdsbCore::altitudeAc13(int ac)
{
    if (ac == 0) return kSemValor;
    if (ac & 0x40) return kSemValor;                 // M = 1: em metros (quase nao existe)
    if (ac & 0x10) {                                 // Q = 1: passos de 25 pes
        const int n = ((ac & 0x1F80) >> 2) | ((ac & 0x20) >> 1) | (ac & 0x0F);
        return n * 25 - 1000;
    }
    // Q = 0: codigo de Gillham (passos de 100 pes, o modo C antigo)
    auto b = [ac](int i) { return (ac >> i) & 1; };
    const int C1 = b(12), A1 = b(11), C2 = b(10), A2 = b(9), C4 = b(8), A4 = b(7);
    const int B1 = b(5), B2 = b(3), D2 = b(2), B4 = b(1), D4 = b(0);
    int g500 = (D2 << 7) | (D4 << 6) | (A1 << 5) | (A2 << 4) | (A4 << 3) | (B1 << 2) | (B2 << 1) | B4;
    int n500 = 0;
    for (; g500; g500 >>= 1) n500 ^= g500;            // Gray -> binario
    int g100 = (C1 << 2) | (C2 << 1) | C4, n100 = 0;
    for (; g100; g100 >>= 1) n100 ^= g100;
    if (n100 == 0 || n100 == 5 || n100 == 6) return kSemValor;
    if (n100 == 7) n100 = 5;
    if (n500 & 1) n100 = 6 - n100;
    return n500 * 500 + n100 * 100 - 1300;
}

// squawk (13 bits): C1 A1 C2 A2 C4 A4 X B1 D1 B2 D2 B4 D4
int AdsbCore::squawkId13(int id)
{
    auto b = [id](int i) { return (id >> i) & 1; };
    const int a = (b(7) << 2) | (b(9) << 1) | b(11);
    const int bb = (b(1) << 2) | (b(3) << 1) | b(5);
    const int c = (b(8) << 2) | (b(10) << 1) | b(12);
    const int d = (b(0) << 2) | (b(2) << 1) | b(4);
    return a * 1000 + bb * 100 + c * 10 + d;
}

// ---------------------------------------------------------------------------
//  Prefixo dos EUA (N-number): os enderecos A00001..ADF7C7 sao distribuidos
//  em ordem: N1, N1A, N1AA, N1AB..., N10, N100... (letras sem I e O).
// ---------------------------------------------------------------------------
std::string AdsbCore::prefixoCalculado(uint32_t icao)
{
    static const char kLet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";   // 24, sem I e O
    if (icao < 0xA00001u || icao > 0xADF7C7u) return {};
    long off = (long)(icao - 0xA00001u);
    auto letras = [](long r) -> std::string {                // "", A..Z, AA..ZZ (601 valores)
        if (r == 0) return {};
        --r;
        std::string s(1, kLet[r / 25]);
        if (r % 25) s += kLet[r % 25 - 1];
        return s;
    };
    std::string reg = "N" + std::to_string(off / 101711 + 1);
    off %= 101711;
    if (off <= 600) return reg + letras(off);
    off -= 601;
    reg += std::to_string(off / 10111);
    off %= 10111;
    if (off <= 600) return reg + letras(off);
    off -= 601;
    reg += std::to_string(off / 951);
    off %= 951;
    if (off <= 600) return reg + letras(off);
    off -= 601;
    reg += std::to_string(off / 35);
    off %= 35;
    if (off == 0) return reg;
    if (off <= 24) return reg + kLet[off - 1];
    return reg + std::to_string(off - 25);
}

// O pais pelo bloco de enderecos (ICAO Anexo 10, Vol. III, tabela 9-1)
const char* AdsbCore::paisDoIcao(uint32_t a)
{
    struct Bloco { uint32_t ini, fim; const char* nome; };
    static const Bloco kBlocos[] = {
        {0xE40000, 0xE7FFFF, "Brasil"},        {0xA00000, 0xAFFFFF, "EUA"},
        {0xE00000, 0xE3FFFF, "Argentina"},     {0xE80000, 0xE80FFF, "Chile"},
        {0xE84000, 0xE84FFF, "Equador"},       {0xE88000, 0xE88FFF, "Paraguai"},
        {0xE8C000, 0xE8CFFF, "Peru"},          {0xE90000, 0xE90FFF, "Uruguai"},
        {0xE94000, 0xE94FFF, "Bolivia"},       {0x0AC000, 0x0ACFFF, "Colombia"},
        {0x0D8000, 0x0DFFFF, "Venezuela"},     {0x0D0000, 0x0D7FFF, "Mexico"},
        {0x0B0000, 0x0B0FFF, "Cuba"},          {0x0C2000, 0x0C2FFF, "Panama"},
        {0xC00000, 0xC3FFFF, "Canada"},        {0x400000, 0x43FFFF, "Reino Unido"},
        {0x3C0000, 0x3FFFFF, "Alemanha"},      {0x380000, 0x3BFFFF, "Franca"},
        {0x340000, 0x37FFFF, "Espanha"},       {0x300000, 0x33FFFF, "Italia"},
        {0x490000, 0x497FFF, "Portugal"},      {0x480000, 0x487FFF, "Holanda"},
        {0x448000, 0x44FFFF, "Belgica"},       {0x4B0000, 0x4B7FFF, "Suica"},
        {0x440000, 0x447FFF, "Austria"},       {0x4B8000, 0x4BFFFF, "Turquia"},
        {0x4CA000, 0x4CAFFF, "Irlanda"},       {0x478000, 0x47FFFF, "Noruega"},
        {0x4A8000, 0x4AFFFF, "Suecia"},        {0x458000, 0x45FFFF, "Dinamarca"},
        {0x460000, 0x467FFF, "Finlandia"},     {0x488000, 0x48FFFF, "Polonia"},
        {0x140000, 0x15FFFF, "Russia"},        {0x780000, 0x7BFFFF, "China"},
        {0x840000, 0x87FFFF, "Japao"},         {0x800000, 0x83FFFF, "India"},
        {0x7C0000, 0x7FFFFF, "Australia"},     {0xC80000, 0xC87FFF, "Nova Zelandia"},
        {0x896000, 0x896FFF, "Emirados Arabes"}, {0x06A000, 0x06AFFF, "Catar"},
        {0x710000, 0x717FFF, "Arabia Saudita"}, {0x738000, 0x73FFFF, "Israel"},
        {0x008000, 0x00FFFF, "Africa do Sul"}, {0x010000, 0x017FFF, "Egito"},
        {0x020000, 0x027FFF, "Marrocos"},
    };
    for (const auto& b : kBlocos)
        if (a >= b.ini && a <= b.fim) return b.nome;
    return "";
}

// ---------------------------------------------------------------------------
//  os avioes
// ---------------------------------------------------------------------------
AdsbCore::Interno* AdsbCore::pegar(uint32_t icao, bool criar)
{
    auto it = avioes_.find(icao);
    if (it != avioes_.end()) return &it->second;
    if (!criar) return nullptr;
    Interno& v = avioes_[icao];
    char h[8];
    std::snprintf(h, sizeof h, "%06X", icao);
    v.a.icao = icao;
    v.a.hex = h;
    v.a.pais = paisDoIcao(icao);
    v.a.prefixo = prefixoCalculado(icao);
    v.criado = relogio_;
    return &v;
}

bool AdsbCore::conhecido(uint32_t icao) const
{
    auto it = avioes_.find(icao);
    return it != avioes_.end() && relogio_ - it->second.visto < kConhecido;
}

std::string AdsbCore::horaUtc() const
{
    const std::time_t t = std::time(nullptr);
    std::tm g{};
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", g.tm_hour, g.tm_min, g.tm_sec);
    return b;
}

void AdsbCore::linha(const std::string& s)
{
    texto_ += "[ADSB] " + horaUtc() + " UTC  " + s + "\n";
    if (texto_.size() > 200000) texto_.erase(0, 100000);
}

// ---------------------------------------------------------------------------
//  uma mensagem (56 ou 112 bits)
// ---------------------------------------------------------------------------
bool AdsbCore::mensagem(uint8_t* msg, int nbits, bool podeCorrigir, bool jaCorrigida)
{
    const int df = msg[0] >> 3;
    if ((df >= 16) != (nbits == 112)) return false;
    uint32_t syn = sindrome(msg, nbits);
    uint32_t icao = 0;
    bool podeCriar = false, corrigida = jaCorrigida;

    switch (df) {
    case 17:
    case 18:
        if (syn) {
            if (!podeCorrigir) return false;
            const auto it = umBit112_.find(syn);
            if (it == umBit112_.end()) return false;
            msg[it->second >> 3] ^= (uint8_t)(0x80 >> (it->second & 7));
            corrigida = true;
        }
        if (df == 18) {
            const int cf = msg[0] & 7;                 // 0 e 6: endereco ICAO de verdade
            if (cf != 0 && cf != 6) return false;
        }
        icao = (uint32_t)msg[1] << 16 | (uint32_t)msg[2] << 8 | msg[3];
        if (corrigida && !conhecido(icao)) return false;
        podeCriar = !corrigida;
        break;
    case 11:
        if (jaCorrigida) return false;
        icao = (uint32_t)msg[1] << 16 | (uint32_t)msg[2] << 8 | msg[3];
        if (syn == 0) podeCriar = true;
        else if (syn >= 0x80 || !conhecido(icao)) return false;   // a paridade leva o codigo do interrogador
        break;
    case 0: case 4: case 5: case 16: case 20: case 21:
        if (jaCorrigida) return false;
        icao = syn;                                    // endereco/paridade
        if (!conhecido(icao)) return false;
        break;
    default:
        return false;
    }
    if (icao == 0 || icao == 0xFFFFFF) return false;

    Interno* v = pegar(icao, podeCriar);
    if (!v) return false;
    v->visto = relogio_;
    ++v->a.msgs;
    ++est_.mensagens;
    ++contMsgs_;
    if (corrigida) ++est_.corrigidas;

    switch (df) {
    case 0: case 4: case 16: case 20: {
        const int alt = altitudeAc13(((msg[2] & 0x1F) << 8) | msg[3]);
        if (alt != kSemValor) v->a.altitude = alt;
        break;
    }
    case 5: case 21: {
        const int sq = squawkId13(((msg[2] & 0x1F) << 8) | msg[3]);
        if (sq != v->a.squawk) alertaSquawk(*v, sq);
        v->a.squawk = sq;
        break;
    }
    case 17: case 18:
        decodificarEs(*v, msg + 4);
        break;
    default:
        break;
    }

    if (!v->anunciado && v->a.msgs >= 2) {
        v->anunciado = true;
        if (!v->consultado) fila_.insert(icao);      // (os dos EUA tambem: modelo e dono)
        v->consultado = true;
        std::string s = "novo  " + v->a.hex;
        if (!v->a.voo.empty()) s += "  " + v->a.voo;
        if (!v->a.prefixo.empty()) s += "  " + v->a.prefixo;
        if (!v->a.pais.empty()) s += "  (" + v->a.pais + ")";
        linha(s);
    }
    return true;
}

// ---------------------------------------------------------------------------
//  ADS-B (DF17/18): os 56 bits do campo ME
// ---------------------------------------------------------------------------
void AdsbCore::decodificarEs(Interno& v, const uint8_t* me)
{
    const int tc = me[0] >> 3;
    if (tc >= 1 && tc <= 4) {
        if (me[0] & 7) v.a.categoria = tc * 8 + (me[0] & 7);   // categoria (leve, pesado, helicoptero...)
        // identificacao: 8 caracteres de 6 bits
        std::string s;
        for (int i = 0; i < 8; ++i) {
            const char c = kCharsVoo[campo(me, 8 + 6 * i, 6)];
            if (c != '#' && c != ' ') s += c;
        }
        if (!s.empty() && s != v.a.voo) {
            const bool tinha = !v.a.voo.empty();
            v.a.voo = s;
            if (v.anunciado || tinha) {
                std::string l = v.a.hex + "  voo " + s;
                if (!v.a.prefixo.empty()) l += "  " + v.a.prefixo;
                linha(l);
            }
        }
    } else if (tc >= 5 && tc <= 8) {
        // no chao: a velocidade (movimento) e o rumo; a posicao fica de fora
        v.a.noChao = true;
        const int mov = (int)campo(me, 5, 7);
        double kt = -1;
        if (mov == 1) kt = 0;
        else if (mov >= 2 && mov <= 8) kt = 0.125 * (mov - 1);
        else if (mov >= 9 && mov <= 12) kt = 1.0 + 0.25 * (mov - 8);
        else if (mov >= 13 && mov <= 38) kt = 2.0 + 0.5 * (mov - 12);
        else if (mov >= 39 && mov <= 93) kt = 15.0 + (mov - 38);
        else if (mov >= 94 && mov <= 108) kt = 70.0 + 2.0 * (mov - 93);
        else if (mov >= 109 && mov <= 123) kt = 100.0 + 5.0 * (mov - 108);
        else if (mov == 124) kt = 175;
        if (kt >= 0) v.a.velocidade = kt;
        if (campo(me, 12, 1)) v.a.rumo = campo(me, 13, 7) * 360.0 / 128.0;
    } else if ((tc >= 9 && tc <= 18) || (tc >= 20 && tc <= 22)) {
        v.a.noChao = false;
        const int alt12 = (int)campo(me, 8, 12);
        if (tc <= 18) {
            const int ac13 = ((alt12 & 0xFC0) << 1) | (alt12 & 0x3F);
            const int alt = altitudeAc13(ac13);
            if (alt != kSemValor) v.a.altitude = alt;
        } else if (alt12) {
            v.a.altitude = (int)std::lround(alt12 * 3.28084);   // GNSS: em metros
        }
        const int impar = (int)campo(me, 21, 1);
        posicaoAr(v, impar, (int)campo(me, 22, 17), (int)campo(me, 39, 17));
    } else if (tc == 19) {
        const int st = (int)campo(me, 5, 3);
        if (st == 1 || st == 2) {
            const int vew = (int)campo(me, 14, 10), vns = (int)campo(me, 25, 10);
            if (vew && vns) {
                const double k = st == 2 ? 4.0 : 1.0;
                const double vx = (vew - 1) * k * (campo(me, 13, 1) ? -1 : 1);
                const double vy = (vns - 1) * k * (campo(me, 24, 1) ? -1 : 1);
                v.a.velocidade = std::hypot(vx, vy);
                v.a.rumo = modp(std::atan2(vx, vy) * 180.0 / kPi, 360.0);
            }
        } else if (st == 3 || st == 4) {
            if (campo(me, 13, 1)) v.a.rumo = campo(me, 14, 10) * 360.0 / 1024.0;
            const int as = (int)campo(me, 25, 10);
            if (as) v.a.velocidade = (as - 1) * (st == 4 ? 4.0 : 1.0);
        }
        const int vr = (int)campo(me, 37, 9);
        if (vr) v.a.vertical = (vr - 1) * 64 * (campo(me, 36, 1) ? -1 : 1);
    } else if (tc == 28 && campo(me, 5, 3) == 1) {
        // situacao do aviao: emergencia e o squawk
        const int sq = squawkId13((int)campo(me, 11, 13));
        if (sq && sq != v.a.squawk) alertaSquawk(v, sq);
        if (sq) v.a.squawk = sq;
    }
}

// 7500 / 7600 / 7700: avisa na janela (uma vez por mudanca)
void AdsbCore::alertaSquawk(Interno& v, int sq)
{
    if (sq != 7500 && sq != 7600 && sq != 7700) return;
    linha(v.a.hex + (v.a.voo.empty() ? "" : "  " + v.a.voo) + (v.a.prefixo.empty() ? "" : "  " + v.a.prefixo) +
          "  SQUAWK " + std::to_string(sq) +
          (sq == 7700 ? "  (EMERGENCIA)" : sq == 7600 ? "  (falha de radio)" : "  (interferencia ilicita)"));
}

// ---------------------------------------------------------------------------
//  posicao no ar (CPR): global com um par par/impar, depois local
// ---------------------------------------------------------------------------
void AdsbCore::posicaoAr(Interno& v, int impar, int latCpr, int lonCpr)
{
    v.cprLat[impar] = latCpr;
    v.cprLon[impar] = lonCpr;
    v.cprT[impar] = relogio_;
    const double kEsc = 131072.0;    // 2^17
    const bool temPos = v.a.lat <= 90 && relogio_ - v.posT < 600;

    double lat = 91, lon = 181;
    if (std::fabs(v.cprT[0] - v.cprT[1]) <= kParCpr) {
        const double la0 = v.cprLat[0] / kEsc, la1 = v.cprLat[1] / kEsc;
        const double lo0 = v.cprLon[0] / kEsc, lo1 = v.cprLon[1] / kEsc;
        const double j = std::floor(59.0 * la0 - 60.0 * la1 + 0.5);
        double rl0 = (360.0 / 60.0) * (modp(j, 60.0) + la0);
        double rl1 = (360.0 / 59.0) * (modp(j, 59.0) + la1);
        if (rl0 >= 270.0) rl0 -= 360.0;
        if (rl1 >= 270.0) rl1 -= 360.0;
        if (nl(rl0) == nl(rl1) && std::fabs(rl0) <= 90 && std::fabs(rl1) <= 90) {
            const int nlz = nl(impar ? rl1 : rl0);
            const int ni = std::max(nlz - impar, 1);
            const double m = std::floor(lo0 * (nlz - 1) - lo1 * nlz + 0.5);
            lat = impar ? rl1 : rl0;
            lon = (360.0 / ni) * (modp(m, (double)ni) + (impar ? lo1 : lo0));
            if (lon >= 180.0) lon -= 360.0;
        }
    }
    const bool global = lat <= 90;
    if (!global && temPos) {
        // local: com a posicao anterior como referencia (ate ~180 milhas)
        const double dlat = 360.0 / (60 - impar);
        const double fl = latCpr / kEsc;
        const double j = std::floor(v.a.lat / dlat) + std::floor(0.5 + modp(v.a.lat, dlat) / dlat - fl);
        const double la = dlat * (j + fl);
        const int ni = std::max(nl(la) - impar, 1);
        const double dlon = 360.0 / ni;
        const double fo = lonCpr / kEsc;
        const double m = std::floor(v.a.lon / dlon) + std::floor(0.5 + modp(v.a.lon, dlon) / dlon - fo);
        double lo = dlon * (m + fo);
        if (lo >= 180.0) lo -= 360.0;
        lat = la;
        lon = lo;
    }
    if (lat > 90) return;
    // Plausibilidade: um aviao anda no maximo ~0,5 km/s. A primeira posicao
    // so vale quando duas decodificacoes globais concordam; uma posicao que
    // "salta" e descartada, mas se as globais insistirem (3 vezes) noutro
    // lugar, a anterior e que estava errada.
    auto perto = [](double la1, double lo1, double t1, double la2, double lo2, double t2) {
        return distKm(la1, lo1, la2, lo2) <= 0.5 * std::max(1.0, std::fabs(t2 - t1)) + 5.0;
    };
    if (!temPos) {
        if (!global) return;
        if (v.candT > -1e8 && relogio_ - v.candT < 60 && perto(v.candLat, v.candLon, v.candT, lat, lon, relogio_)) {
            v.candT = -1e9;
        } else {
            v.candLat = lat; v.candLon = lon; v.candT = relogio_;
            return;
        }
    } else if (!perto(v.a.lat, v.a.lon, v.posT, lat, lon, relogio_)) {
        if (!global) return;
        if (v.candT > -1e8 && relogio_ - v.candT < 60 && perto(v.candLat, v.candLon, v.candT, lat, lon, relogio_)) {
            if (++v.saltos < 3) { v.candLat = lat; v.candLon = lon; v.candT = relogio_; return; }
        } else {
            v.candLat = lat; v.candLon = lon; v.candT = relogio_; v.saltos = 1;
            return;
        }
    }
    v.saltos = 0;
    v.a.lat = lat;
    v.a.lon = lon;
    v.posT = relogio_;
}

// ---------------------------------------------------------------------------
//  a entrada: IQ -> (sem DC) -> magnitude a 2 Msps
// ---------------------------------------------------------------------------
void AdsbCore::alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps)
{
    if (!iq || !n || !sps) return;
    std::lock_guard<std::mutex> lk(mtx_);
    est_.taxa = sps;
    est_.taxaOk = sps >= 1800000;
    const double antes = relogio_;
    relogio_ += (double)n / sps;
    if (!est_.taxaOk) return;
    if (sps != sps_) { sps_ = sps; mag_.clear(); }
    const float kDc = 1e-4f;
    const size_t base = mag_.size();
    mag_.resize(base + n);
    float* m = mag_.data() + base;
    for (size_t k = 0; k < n; ++k) {
        dc_ += (iq[k] - dc_) * kDc;          // o DC do dongle (fica bem no meio dos 1090 MHz)
        m[k] = std::abs(iq[k] - dc_);
    }
    const size_t janela = (size_t)(kMsgUs * (sps / 1.0e6)) + 16;
    if (mag_.size() >= janela + 16384) demodular();

    // a cada segundo: mensagens por segundo e a limpeza da lista
    janelaMsgs_ += relogio_ - antes;
    if (janelaMsgs_ >= 1.0) {
        est_.msgsPorS = contMsgs_ / janelaMsgs_;
        contMsgs_ = janelaMsgs_ = 0;
        // os avioes ficam na lista (registro) ate o botao Limpar; so se passar
        // de kMaxAvioes sai o ouvido ha mais tempo
        while (avioes_.size() > kMaxAvioes) {
            auto velho = avioes_.begin();
            for (auto it = avioes_.begin(); it != avioes_.end(); ++it)
                if (it->second.visto < velho->second.visto) velho = it;
            avioes_.erase(velho);
        }
        int na = 0, np = 0;
        for (const auto& kv : avioes_)      // os ouvidos no ultimo minuto
            if (kv.second.a.msgs >= 2 && relogio_ - kv.second.visto < kConhecido) { ++na; if (kv.second.a.lat <= 90) ++np; }
        est_.avioes = na;
        est_.comPosicao = np;
        est_.nivelDb = sinal_ > 0 ? 20.0 * std::log10(sinal_) : -100.0;
    }
}

// Procura o preambulo e decodifica os bits (PPM: bit 1 = a primeira metade
// mais forte). Funciona em qualquer taxa >= 1,8 Msps: as amostras viram uma
// funcao "degrau" e a energia de cada meio-chip (0,5 us) e a integral dela
// na janela certa, com o inicio da mensagem achado com fracao de amostra.
void AdsbCore::demodular()
{
    const double r = sps_ / 1.0e6;                         // amostras por microssegundo
    const size_t n = mag_.size();
    const size_t janela = (size_t)(kMsgUs * r) + 16;
    if (n <= janela + 2) return;
    const float* m = mag_.data();
    soma_.resize(n + 1);
    soma_[0] = 0;
    for (size_t k = 0; k < n; ++k) soma_[k + 1] = soma_[k] + m[k];
    const double* P = soma_.data();
    // integral de m (degraus) em [a, b), em amostras
    auto I = [&](double a, double b) -> double {
        const size_t ia = (size_t)a, ib = (size_t)b;
        if (ia == ib) return m[ia] * (b - a);
        return (P[ib] - P[ia]) - m[ia] * (a - (double)ia) + m[ib] * (b - (double)ib);
    };
    const double w = 0.5 * r;                               // meio chip
    // pulsos do preambulo (0; 1,0; 3,5; 4,5 us) e os vazios entre eles
    auto nota = [&](double t0, double& pulso, double& vazio) {
        pulso = (I(t0, t0 + w) + I(t0 + r, t0 + r + w) + I(t0 + 3.5 * r, t0 + 3.5 * r + w) +
                 I(t0 + 4.5 * r, t0 + 4.5 * r + w)) / (4.0 * w);
        vazio = (I(t0 + w, t0 + r) + I(t0 + 1.5 * r, t0 + 3.5 * r) + I(t0 + 4.0 * r, t0 + 4.5 * r) +
                 I(t0 + 5.0 * r, t0 + 8.0 * r)) / (6.0 * r);
    };
    const double ruido = ruido_ > 0 ? ruido_ : P[n] / (double)n;
    const float corte = (float)(1.5 * ruido);
    const size_t lim = n - janela;
    const int iMeio = (int)(0.25 * r), iSeg = (int)(1.25 * r);
    size_t i = 1;
    uint8_t msg[14];
    float conf[112];
    for (; i < lim; ++i) {
        // filtro rapido: o primeiro e o segundo pulso acima do ruido
        if (m[i + iMeio] < corte || m[i + iSeg] < corte) continue;
        double pl, vl;
        nota((double)i, pl, vl);
        if (pl < 2.0 * vl || pl < 1.6 * ruido) continue;
        // o instante fino: o melhor contraste pulso/vazio em +-0,6 amostra
        double melhorT = (double)i, melhor = pl - vl;
        for (int d = -6; d <= 6; d += 2) {
            const double t = (double)i + 0.1 * d;
            if (t < 1) continue;
            nota(t, pl, vl);
            if (pl - vl > melhor) { melhor = pl - vl; melhorT = t; }
        }
        bool aceita = false;
        int nbits = 56;
        for (int tent = 0; tent < 3 && !aceita; ++tent) {
            const double t0 = melhorT + (tent == 0 ? 0.0 : (tent == 1 ? 0.25 : -0.25));
            std::memset(msg, 0, sizeof msg);
            for (int k = 0; k < 112; ++k) {
                const double ini = t0 + (8.0 + k) * r;
                const double a = I(ini, ini + w), b = I(ini + w, ini + 2.0 * w);
                if (a > b) msg[k >> 3] |= (uint8_t)(0x80 >> (k & 7));
                conf[k] = (float)std::fabs(a - b);
            }
            const int df = msg[0] >> 3;
            nbits = df >= 16 ? 112 : 56;
            uint8_t copia[14];
            std::memcpy(copia, msg, sizeof msg);
            aceita = mensagem(copia, nbits, true, false);
            if (!aceita && nbits == 112) {
                // os bits menos firmes: troca 1, 2 ou 3 deles (so vale para
                // aviao ja conhecido; o DF tambem pode estar entre eles)
                constexpr int kChase = 8;
                int idx[kChase], nIdx = 0;
                for (int q = 0; q < kChase; ++q) {
                    int pior = -1;
                    for (int k = 0; k < 112; ++k) {
                        bool usado = false;
                        for (int u = 0; u < nIdx; ++u) usado |= idx[u] == k;
                        if (!usado && (pior < 0 || conf[k] < conf[pior])) pior = k;
                    }
                    idx[nIdx++] = pior;
                }
                auto troca = [&](uint8_t* c, int k) { c[k >> 3] ^= (uint8_t)(0x80 >> (k & 7)); };
                for (int x = 0; x < nIdx && !aceita; ++x)
                    for (int y = x; y < nIdx && !aceita; ++y)
                        for (int z = y; z < nIdx && !aceita; ++z) {
                            if (z != y && y == x) continue;          // (x,x,z) repetiria (x,z)
                            std::memcpy(copia, msg, sizeof msg);
                            troca(copia, idx[x]);
                            if (y != x) troca(copia, idx[y]);
                            if (z != y) troca(copia, idx[z]);
                            const int d2 = copia[0] >> 3;
                            if (d2 != 17 && d2 != 18) continue;
                            aceita = mensagem(copia, 112, false, true);
                        }
            }
        }
        if (aceita) {
            nota(melhorT, pl, vl);
            sinal_ = sinal_ > 0 ? sinal_ * 0.98 + 0.02 * pl : pl;
            i += (size_t)((8.0 + nbits) * r) - 1;
        }
    }
    // o ruido: a media das magnitudes do bloco (os pulsos sao raros)
    ruido_ = ruido_ > 0 ? ruido_ * 0.7 + 0.3 * (P[n] / (double)n) : P[n] / (double)n;
    const size_t consumido = std::min(i, n);
    mag_.erase(mag_.begin(), mag_.begin() + (long)consumido);
}

// ---------------------------------------------------------------------------
std::vector<AdsbCore::Aviao> AdsbCore::avioes() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<Aviao> v;
    for (const auto& kv : avioes_) {
        if (kv.second.a.msgs < 2) continue;
        Aviao a = kv.second.a;
        a.idade = std::max(0.0, relogio_ - kv.second.visto);
        a.ativo = a.idade < kConhecido;
        const ClasseAviao c = classificarAviao(a.voo, a.prefixo, a.icao, a.operadorIcao, a.operador, a.categoria);
        a.empresa = c.empresa;
        a.classe = c.classe;
        v.push_back(a);
    }
    std::sort(v.begin(), v.end(), [](const Aviao& x, const Aviao& y) { return x.idade < y.idade; });
    return v;
}

AdsbCore::Estado AdsbCore::estado() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return est_;
}

std::string AdsbCore::pegarTexto()
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::string s;
    s.swap(texto_);
    return s;
}

void AdsbCore::definirInfo(uint32_t icao, const std::string& prefixo, const std::string& tipo,
                           const std::string& operador, const std::string& operadorIcao)
{
    std::lock_guard<std::mutex> lk(mtx_);
    Interno* v = pegar(icao, false);
    if (!v) return;
    const bool novo = !prefixo.empty() && v->a.prefixo != prefixo;
    if (!prefixo.empty()) v->a.prefixo = prefixo;
    if (!tipo.empty()) v->a.tipo = tipo;
    if (!operador.empty()) v->a.operador = operador;
    if (!operadorIcao.empty()) v->a.operadorIcao = operadorIcao;
    if (novo) {
        std::string s = v->a.hex + "  prefixo " + prefixo;
        if (!v->a.voo.empty()) s += "  voo " + v->a.voo;
        if (!tipo.empty()) s += "  " + tipo;
        // a empresa (ou Militar / Particular...); o nome do dono de aviao
        // particular nao vai para o texto
        const ClasseAviao c = classificarAviao(v->a.voo, v->a.prefixo, v->a.icao, v->a.operadorIcao,
                                               v->a.operador, v->a.categoria);
        if (!c.empresa.empty()) s += "  " + c.empresa;
        linha(s);
    }
}

void AdsbCore::limparLista()
{
    std::lock_guard<std::mutex> lk(mtx_);
    avioes_.clear();
    fila_.clear();
    est_.avioes = est_.comPosicao = 0;
}

std::vector<uint32_t> AdsbCore::pedirConsultas()
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<uint32_t> v(fila_.begin(), fila_.end());
    fila_.clear();
    return v;
}

} // namespace masdr
