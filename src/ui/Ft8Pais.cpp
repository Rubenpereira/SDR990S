#include "Ft8Pais.h"
#include "Idioma.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace masdr {
namespace ft8geo {
namespace {

struct EntCty { const char* en; const char* pt; float lat, lon; };
struct PrefCty { const char* p; short ent; float lat, lon; };
#include "Ft8Paises.inc"

const int kNumEnt = (int)(sizeof kEntCty / sizeof kEntCty[0]);

struct Tabela {
    std::unordered_map<std::string, int> exato;     // indice em kPrefCty
    std::unordered_map<std::string, int> prefixo;
    size_t maior = 1;
    Tabela() {
        const int n = (int)(sizeof kPrefCty / sizeof kPrefCty[0]);
        exato.reserve(n); prefixo.reserve(n);
        for (int i = 0; i < n; ++i) {
            std::string p = kPrefCty[i].p;
            if (!p.empty() && p[0] == '=') exato.emplace(p.substr(1), i);
            else { maior = std::max(maior, p.size()); prefixo.emplace(p, i); }
        }
    }
};

const Tabela& tabela() { static Tabela t; return t; }

bool soDigitos(const std::string& s)
{
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

// sufixos que nao mudam o pais
bool sufixoIgnorado(const std::string& s)
{
    static const char* k[] = {"P", "M", "QRP", "QRPP", "A", "B", "LH", "J"};   // /R, /F, /W... sao paises
    if (soDigitos(s) && s.size() <= 2) return true;
    for (const char* x : k) if (s == x) return true;
    return false;
}

// prefixo mais longo que casa
int procurarPrefixo(const std::string& s)
{
    const auto& t = tabela();
    for (size_t L = std::min(s.size(), t.maior); L >= 1; --L) {
        auto it = t.prefixo.find(s.substr(0, L));
        if (it != t.prefixo.end()) return it->second;
    }
    return -1;
}

int procurar(std::string call)
{
    const auto& t = tabela();
    auto it = t.exato.find(call);
    if (it != t.exato.end()) return it->second;
    if (call.find('/') == std::string::npos) return procurarPrefixo(call);

    std::vector<std::string> p;
    std::stringstream ss(call);
    for (std::string x; std::getline(ss, x, '/');) if (!x.empty()) p.push_back(x);
    if (p.empty()) return -1;
    if (p.back() == "MM" || p.back() == "AM") return -1;     // maritimo / aeronautico movel
    while (p.size() > 1 && sufixoIgnorado(p.back())) p.pop_back();
    if (p.size() == 1) {
        it = t.exato.find(p[0]);
        return it != t.exato.end() ? it->second : procurarPrefixo(p[0]);
    }
    // "F/PY1XX", "PY1XX/F", "VP2E/K1ABC": a parte mais curta e o prefixo do lugar
    const std::string& a = p[0];
    const std::string& b = p[1];
    return procurarPrefixo(a.size() <= b.size() ? a : b);
}

bool ehGrade(const std::string& g)
{
    return g.size() == 4 && g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' &&
           std::isdigit((unsigned char)g[2]) && std::isdigit((unsigned char)g[3]) && g != "RR73";
}

// parece indicativo: letras e numeros (pelo menos um de cada), ou <...>
bool pareceIndicativo(const std::string& s)
{
    if (s.size() >= 3 && s.front() == '<' && s.back() == '>') return true;
    if (s.size() < 3) return false;
    bool letra = false, num = false;
    for (char c : s) {
        if (std::isalpha((unsigned char)c)) letra = true;
        else if (std::isdigit((unsigned char)c)) num = true;
        else if (c != '/') return false;
    }
    return letra && num;
}

} // namespace

const InfoPais& paisDoIndicativo(const std::string& indicativo)
{
    static std::unordered_map<std::string, InfoPais> cache[2];   // [0]=pt, [1]=en
    static const InfoPais vazio;
    std::string c;
    for (char ch : indicativo)
        if (ch != '<' && ch != '>') c += (char)std::toupper((unsigned char)ch);
    if (c.empty() || c.find("...") != std::string::npos) return vazio;
    auto& m = cache[emIngles() ? 1 : 0];
    auto it = m.find(c);
    if (it != m.end()) return it->second;
    if (m.size() > 20000) m.clear();
    InfoPais r;
    const int i = procurar(c);
    if (i >= 0 && kPrefCty[i].ent >= 0 && kPrefCty[i].ent < kNumEnt) {
        const EntCty& e = kEntCty[kPrefCty[i].ent];
        r.ok = true;
        r.nome = emIngles() ? e.en : e.pt;
        const bool proprio = kPrefCty[i].lat < 900.f;
        r.lat = proprio ? kPrefCty[i].lat : e.lat;
        r.lon = proprio ? kPrefCty[i].lon : e.lon;
    }
    return m.emplace(c, r).first->second;
}

bool transmissor(const std::string& texto, std::string& indicativo, std::string& grade)
{
    indicativo.clear();
    grade.clear();
    std::string t = texto;
    const size_t pv = t.find(';');
    const bool dxped = pv != std::string::npos;
    if (dxped) t = t.substr(pv + 1);
    std::vector<std::string> w;
    std::stringstream ss(t);
    for (std::string x; ss >> x;) w.push_back(x);
    if (w.size() < 2) return false;

    size_t idx;
    if (!dxped && (w[0] == "CQ" || w[0] == "QRZ" || w[0] == "DE")) {
        idx = 1;
        if (w.size() > 2 && !pareceIndicativo(w[1])) idx = 2;   // CQ DX / CQ SA / CQ POTA / CQ 123
        if (idx < w.size() - 1 && ehGrade(w[idx + 1])) grade = w[idx + 1];
    } else {
        if (!pareceIndicativo(w[1])) return false;               // texto livre
        idx = 1;
        if (!dxped && w.size() >= 3 && ehGrade(w.back())) grade = w.back();   // "K1ABC W9XYZ EN37" / "... R EN37"
    }
    if (idx >= w.size() || !pareceIndicativo(w[idx])) return false;
    std::string c = w[idx];
    if (c.size() >= 2 && c.front() == '<' && c.back() == '>') c = c.substr(1, c.size() - 2);
    if (c.empty() || c == "...") return false;
    indicativo = c;
    return true;
}

bool gradeParaGraus(const std::string& in, double& lat, double& lon)
{
    std::string s;
    for (char c : in) if (!std::isspace((unsigned char)c)) s += (char)std::toupper((unsigned char)c);
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

std::string grausParaGrade(double lat, double lon)
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

double distanciaKm(double la1, double lo1, double la2, double lo2)
{
    const double r = 3.14159265358979323846 / 180.0;
    const double dp = (la2 - la1) * r, dl = (lo2 - lo1) * r;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) +
                     std::cos(la1 * r) * std::cos(la2 * r) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * 6371.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1 - a)));
}

} // namespace ft8geo
} // namespace masdr
