#include "AdsbInternet.h"

#include "../decoders/AdsbCore.h"

#include <windows.h>
#include <wininet.h>

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace masdr {

namespace {

// Baixa um endereco (WinINet: do Windows 7 ao 11, usa o proxy do sistema).
// Devolve o codigo HTTP (0 = sem conexao).
int baixar(const std::string& url, std::string& out, const std::string& cabecalhos = {})
{
    out.clear();
    HINTERNET net = InternetOpenA("SDR990S", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) return 0;
    DWORD to = 10000;
    InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
    DWORD prot = 0x00000800 /*TLS 1.2*/ | 0x00000200 /*TLS 1.1*/;   // Windows 7
    InternetSetOptionA(net, 151 /*INTERNET_OPTION_SECURE_PROTOCOLS*/, &prot, sizeof prot);
    const std::string cab = "Accept: application/json\r\n" + cabecalhos;
    HINTERNET h = InternetOpenUrlA(net, url.c_str(), cab.c_str(), (DWORD)-1,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!h) { InternetCloseHandle(net); return 0; }
    DWORD status = 0, len = sizeof status;
    HttpQueryInfoA(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr);
    char buf[4096];
    DWORD lidos = 0;
    while (InternetReadFile(h, buf, sizeof buf, &lidos) && lidos) {
        out.append(buf, lidos);
        if (out.size() > 65536) break;
    }
    InternetCloseHandle(h);
    InternetCloseHandle(net);
    return (int)status;
}

// o valor de texto de uma chave num JSON simples ("Chave":"valor")
std::string valorJson(const std::string& j, const char* chave)
{
    const std::string k = std::string("\"") + chave + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return {};
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return {};
    p = j.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || j[p] != '"') return {};
    std::string v;
    for (size_t i = p + 1; i < j.size() && j[i] != '"'; ++i) {
        if (j[i] == '\\' && i + 1 < j.size()) {
            const char c = j[++i];
            if (c == 'u' && i + 4 < j.size()) {            // é -> UTF-8
                const unsigned cp = (unsigned)std::strtoul(j.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp < 0x80) v += (char)cp;
                else if (cp < 0x800) { v += (char)(0xC0 | (cp >> 6)); v += (char)(0x80 | (cp & 0x3F)); }
                else { v += (char)(0xE0 | (cp >> 12)); v += (char)(0x80 | ((cp >> 6) & 0x3F)); v += (char)(0x80 | (cp & 0x3F)); }
            } else v += c;
        } else v += j[i];
    }
    // tira os ';' (separador do arquivo) e espacos das pontas
    for (auto& c : v) if (c == ';' || c == '\n' || c == '\r') c = ' ';
    const size_t a = v.find_first_not_of(' '), b = v.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

std::string pastaDoExe()
{
    char p[MAX_PATH]{};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string d(p);
    const auto pos = d.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : d.substr(0, pos);
}

} // namespace

void ConsultaHexdb::iniciar(AdsbCore* core)
{
    parar();
    core_ = core;
    arquivo_ = pastaDoExe() + "\\adsb_prefixos.txt";
    carregar();
    {
        std::lock_guard<std::mutex> lk(m_);
        fila_.clear();
        situacao_ = "hexdb.io";
    }
    vivo_ = true;
    th_ = std::thread([this] { laco(); });
}

void ConsultaHexdb::parar()
{
    vivo_ = false;
    if (th_.joinable()) th_.join();
}

std::string ConsultaHexdb::situacao() const
{
    std::lock_guard<std::mutex> lk(m_);
    return situacao_;
}

// o arquivo: uma linha por aviao, "E49406;PR-XMA;B38M;GOL Linhas Aereas" ou
// "E4F5E5;-;<data>" (o hexdb nao conhece; consulta de novo depois de 30 dias)
void ConsultaHexdb::carregar()
{
    std::lock_guard<std::mutex> lk(m_);
    cache_.clear();
    semCadastro_.clear();
    std::ifstream f(arquivo_, std::ios::binary);
    std::string l;
    while (std::getline(f, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        std::vector<std::string> c;
        std::stringstream ss(l);
        for (std::string x; std::getline(ss, x, ';');) c.push_back(x);
        if (c.size() < 2 || c[0].size() != 6) continue;
        const uint32_t icao = (uint32_t)std::strtoul(c[0].c_str(), nullptr, 16);
        if (c[1] == "-") {
            // "sem cadastro" vale 30 dias (o hexdb vai aprendendo avioes novos)
            if (c.size() > 2 && std::time(nullptr) - (std::time_t)std::atoll(c[2].c_str()) < 30LL * 86400)
                semCadastro_[icao] = true;
            continue;
        }
        Info i;
        i.prefixo = c[1];
        if (c.size() > 2) i.tipo = c[2];
        if (c.size() > 3) i.operador = c[3];
        if (c.size() > 4) i.operadorIcao = c[4];
        cache_[icao] = i;
    }
}

void ConsultaHexdb::guardar(uint32_t icao, const Info& i, bool achou)
{
    std::ofstream f(arquivo_, std::ios::binary | std::ios::app);
    char h[8];
    std::snprintf(h, sizeof h, "%06X", icao);
    if (achou) f << h << ';' << i.prefixo << ';' << i.tipo << ';' << i.operador << ';' << i.operadorIcao << "\r\n";
    else f << h << ";-;" << (long long)std::time(nullptr) << "\r\n";
}

bool ConsultaHexdb::buscar(uint32_t icao, Info& info, bool& achou)
{
    char url[96];
    std::snprintf(url, sizeof url, "https://hexdb.io/api/v1/aircraft/%06X", icao);
    std::string corpo;
    const int st = baixar(url, corpo);
    if (st == 404) { achou = false; return true; }     // o hexdb nao conhece este aviao
    if (st != 200) return false;                         // sem internet / servidor fora
    info.prefixo = valorJson(corpo, "Registration");
    info.tipo = valorJson(corpo, "ICAOTypeCode");
    info.operador = valorJson(corpo, "RegisteredOwners");
    info.operadorIcao = valorJson(corpo, "OperatorFlagCode");
    achou = !info.prefixo.empty();
    return true;
}

void ConsultaHexdb::laco()
{
    using clk = std::chrono::steady_clock;
    auto pausaAte = clk::now();
    while (vivo_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!core_) continue;
        for (uint32_t icao : core_->pedirConsultas()) {
            std::lock_guard<std::mutex> lk(m_);
            fila_.push_back(icao);
        }
        uint32_t icao = 0;
        Info info;
        bool temCache = false, semCad = false;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (fila_.empty()) continue;
            icao = fila_.front();
            auto it = cache_.find(icao);
            if (it != cache_.end()) { info = it->second; temCache = true; }
            else if (semCadastro_.count(icao)) semCad = true;
            if (temCache || semCad) fila_.pop_front();
        }
        if (temCache) { core_->definirInfo(icao, info.prefixo, info.tipo, info.operador, info.operadorIcao); continue; }
        if (semCad) continue;
        // na internet: uma por segundo; sem conexao, tenta de novo em 5 min
        if (clk::now() < pausaAte) continue;
        bool achou = false;
        if (!buscar(icao, info, achou)) {
            std::lock_guard<std::mutex> lk(m_);
            situacao_ = "hexdb.io sem resposta (tenta de novo em 5 min)";
            pausaAte = clk::now() + std::chrono::minutes(5);
            continue;
        }
        pausaAte = clk::now() + std::chrono::seconds(1);
        {
            std::lock_guard<std::mutex> lk(m_);
            fila_.pop_front();
            if (achou) cache_[icao] = info; else semCadastro_[icao] = true;
            ++consultas_;
            situacao_ = "hexdb.io: " + std::to_string(consultas_) + " consulta(s)";
        }
        guardar(icao, info, achou);
        if (achou) core_->definirInfo(icao, info.prefixo, info.tipo, info.operador, info.operadorIcao);
    }
}

// ===========================================================================
//  ConsultaPrefixo: prefixo -> endereco ICAO, modelo, operador
// ===========================================================================
struct ConsultaPrefixo::Estado {
    std::mutex m;
    std::map<std::string, Info> achados;
    std::map<std::string, long long> semCadastro;    // prefixo -> quando (vale 30 dias)
    std::deque<std::string> fila;
    std::set<std::string> pedidos;
    std::atomic<bool> vivo{true};
    std::string arquivo;
};

namespace {
std::string chavePrefixo(const std::string& p)
{
    std::string s;
    for (char c : p) {
        if (c == '.' || c == ' ') continue;
        s += (char)std::toupper((unsigned char)c);
    }
    return s;
}
bool ehHex6(const std::string& s)
{
    if (s.size() != 6) return false;
    for (char c : s) if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}
std::vector<std::string> campos(const std::string& l)
{
    std::vector<std::string> c;
    std::stringstream ss(l);
    for (std::string x; std::getline(ss, x, ';');) c.push_back(x);
    return c;
}
} // namespace

ConsultaPrefixo::~ConsultaPrefixo()
{
    if (e_) e_->vivo = false;      // a thread termina sozinha (ja esta solta)
}

void ConsultaPrefixo::comecar()
{
    e_ = std::make_shared<Estado>();
    auto e = e_;
    e->arquivo = pastaDoExe() + "\\acars_prefixos.txt";
    // 1) o que o ADSB ja consultou: "E49406;PR-XMA;B38M;dono;GLO"
    {
        std::ifstream f(pastaDoExe() + "\\adsb_prefixos.txt", std::ios::binary);
        for (std::string l; std::getline(f, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            const auto c = campos(l);
            if (c.size() < 2 || !ehHex6(c[0]) || c[1] == "-" || c[1].empty()) continue;
            Info i;
            i.hex = c[0];
            if (c.size() > 2) i.tipo = c[2];
            if (c.size() > 3) i.operador = c[3];
            if (c.size() > 4) i.operadorIcao = c[4];
            e->achados[chavePrefixo(c[1])] = i;
        }
    }
    // 2) as consultas do ACARS/VDL2/HFDL: "PR-GXW;E48FB7;B38M;dono;GLO" ou "PR-XXX;-;<data>"
    {
        std::ifstream f(e->arquivo, std::ios::binary);
        for (std::string l; std::getline(f, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            const auto c = campos(l);
            if (c.size() < 2 || c[0].empty()) continue;
            const std::string k = chavePrefixo(c[0]);
            if (c[1] == "-") {
                const long long q = c.size() > 2 ? std::atoll(c[2].c_str()) : 0;
                if (std::time(nullptr) - q < 30LL * 86400) e->semCadastro[k] = q;
                continue;
            }
            if (!ehHex6(c[1])) continue;
            Info i;
            i.hex = c[1];
            if (c.size() > 2) i.tipo = c[2];
            if (c.size() > 3) i.operador = c[3];
            if (c.size() > 4) i.operadorIcao = c[4];
            e->achados[k] = i;
        }
    }
    // a thread: uma consulta por vez, no maximo uma por segundo; sem internet, espera 5 min
    std::thread([e] {
        using clk = std::chrono::steady_clock;
        auto pausaAte = clk::now();
        while (e->vivo) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (clk::now() < pausaAte) continue;
            std::string p;
            {
                std::lock_guard<std::mutex> lk(e->m);
                if (e->fila.empty()) continue;
                p = e->fila.front();
            }
            std::string corpo;
            const int st = baixar("https://hexdb.io/reg-hex?reg=" + p, corpo);
            if (st != 200 && st != 404) { pausaAte = clk::now() + std::chrono::minutes(5); continue; }
            std::string hex;
            for (char c : corpo) if (std::isxdigit((unsigned char)c)) hex += (char)std::toupper((unsigned char)c); else if (!std::isspace((unsigned char)c)) { hex.clear(); break; }
            Info i;
            const bool achou = st == 200 && ehHex6(hex);
            if (achou) {
                i.hex = hex;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (baixar("https://hexdb.io/api/v1/aircraft/" + hex, corpo) == 200) {
                    i.tipo = valorJson(corpo, "ICAOTypeCode");
                    i.operador = valorJson(corpo, "RegisteredOwners");
                    i.operadorIcao = valorJson(corpo, "OperatorFlagCode");
                }
            }
            {
                std::lock_guard<std::mutex> lk(e->m);
                e->fila.pop_front();
                if (achou) e->achados[p] = i; else e->semCadastro[p] = (long long)std::time(nullptr);
                std::ofstream f(e->arquivo, std::ios::binary | std::ios::app);
                if (achou) f << p << ';' << i.hex << ';' << i.tipo << ';' << i.operador << ';' << i.operadorIcao << "\r\n";
                else f << p << ";-;" << (long long)std::time(nullptr) << "\r\n";
            }
            pausaAte = clk::now() + std::chrono::seconds(1);
        }
    }).detach();
}

void ConsultaPrefixo::pedir(const std::string& prefixo)
{
    const std::string k = chavePrefixo(prefixo);
    if (k.size() < 3) return;
    for (char c : k) if (!std::isalnum((unsigned char)c) && c != '-') return;
    if (!e_) comecar();
    std::lock_guard<std::mutex> lk(e_->m);
    if (e_->achados.count(k) || e_->semCadastro.count(k) || e_->pedidos.count(k)) return;
    e_->pedidos.insert(k);
    e_->fila.push_back(k);
}

// ===========================================================================
//  ConsultaPosicao: a posicao pela internet (adsb.lol)
// ===========================================================================
struct ConsultaPosicao::Estado {
    std::mutex m;
    std::map<std::string, Pos> res;
    std::map<std::string, double> ultConsulta;   // chave -> relogio da ultima consulta
    std::map<std::string, double> ultPedido;     // chave -> relogio do ultimo pedido (so consulta quem ainda e pedido)
    std::atomic<bool> vivo{true};
    std::string situacao = "adsb.lol";
    int consultas = 0;
    // ADS-B Exchange (RapidAPI), so com a chave do proprio usuario
    std::string chaveX, sitX;
    bool xRecusada = false;                      // 401/403: chave errada ou sem assinatura -> para ate trocar a chave
    double pausaX = 0, pausaLol = 0;
    int consultasX = 0;
};

namespace {
double relogioS()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
// numero de uma chave num JSON simples ("alt_baro":16625); texto ("ground") nao vale
bool numJson(const std::string& j, const char* chave, double& v)
{
    const std::string k = std::string("\"") + chave + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return false;
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return false;
    p = j.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || j[p] == '"') return false;
    char* f = nullptr;
    v = std::strtod(j.c_str() + p, &f);
    return f != j.c_str() + p;
}
// o primeiro aviao da lista "ac":[ {...} ]
std::string primeiroAviao(const std::string& j)
{
    const auto pa = j.find("\"ac\"");
    if (pa == std::string::npos) return {};
    const auto a = j.find('{', pa);
    const auto fimLista = j.find(']', pa);
    if (a == std::string::npos || (fimLista != std::string::npos && fimLista < a)) return {};
    int n = 0;
    for (size_t i = a; i < j.size(); ++i) {
        if (j[i] == '{') ++n;
        else if (j[i] == '}' && --n == 0) return j.substr(a, i - a + 1);
    }
    return {};
}
// o aviao da lista "ac" cujo voo comeca com 'prefixo' (o mais perto do ponto, se houver mais de um)
std::string aviaoComVoo(const std::string& j, const std::string& prefixo, double la0, double lo0)
{
    const auto pa = j.find("\"ac\"");
    if (pa == std::string::npos) return {};
    size_t i = j.find('[', pa);
    if (i == std::string::npos) return {};
    std::string melhor;
    double dMelhor = 1e18;
    int n = 0;
    size_t ini = 0;
    for (++i; i < j.size(); ++i) {
        if (j[i] == '{') { if (n++ == 0) ini = i; }
        else if (j[i] == '}') {
            if (--n == 0) {
                const std::string o = j.substr(ini, i - ini + 1);
                std::string f = valorJson(o, "flight");
                for (auto& c : f) c = (char)std::toupper((unsigned char)c);
                if (f.size() >= prefixo.size() && f.compare(0, prefixo.size(), prefixo) == 0) {
                    double la = 0, lo = 0, d = 0;
                    if (numJson(o, "lat", la) && numJson(o, "lon", lo)) d = (la - la0) * (la - la0) + (lo - lo0) * (lo - lo0);
                    if (d < dMelhor) { dMelhor = d; melhor = o; }
                }
            }
        } else if (j[i] == ']' && n == 0) break;
    }
    return melhor;
}
} // namespace

ConsultaPosicao::~ConsultaPosicao()
{
    if (e_) e_->vivo = false;
}

void ConsultaPosicao::comecar()
{
    e_ = std::make_shared<Estado>();
    auto e = e_;
    std::thread([e] {
        // le o primeiro aviao da resposta (o formato v2 e o mesmo no adsb.lol e no ADS-B Exchange)
        // "pt:LAT,LON,VOO": o aviao perto do ponto cujo voo comeca com VOO
        auto lerPt = [](const std::string& val, double& la, double& lo, std::string& voo) {
            const auto a = val.find(','), b = val.find(',', a + 1);
            if (a == std::string::npos || b == std::string::npos) return false;
            la = std::atof(val.substr(0, a).c_str()); lo = std::atof(val.substr(a + 1, b - a - 1).c_str());
            voo = val.substr(b + 1);
            return voo.size() >= 3 && std::fabs(la) <= 90 && std::fabs(lo) <= 180;
        };
        auto ler = [](const std::string& corpo, double agora, const char* fonte, const std::string& chave) {
            Pos p;
            p.fonte = fonte;
            std::string o;
            if (chave.rfind("pt:", 0) == 0) {
                const std::string val = chave.substr(3);
                const auto a = val.find(','), b = val.find(',', a + 1);
                if (a != std::string::npos && b != std::string::npos)
                    o = aviaoComVoo(corpo, val.substr(b + 1), std::atof(val.c_str()), std::atof(val.c_str() + a + 1));
            } else o = primeiroAviao(corpo);
            if (o.empty()) return p;
            p.hex = valorJson(o, "hex");
            for (auto& c : p.hex) c = (char)std::toupper((unsigned char)c);
            if (!p.hex.empty() && p.hex[0] == '~') p.hex.clear();       // endereco nao-ICAO (TIS-B)
            p.voo = valorJson(o, "flight");
            p.prefixo = valorJson(o, "r");
            p.tipo = valorJson(o, "t");
            double la = 0, lo = 0, x = 0;
            if (numJson(o, "lat", la) && numJson(o, "lon", lo) && std::fabs(la) <= 90 && std::fabs(lo) <= 180) {
                double idade = 0;
                if (!numJson(o, "seen_pos", idade)) numJson(o, "seen", idade);
                p.lat = la; p.lon = lo; p.quando = agora - idade; p.achou = true;
            }
            if (numJson(o, "gs", x)) p.gs = x;
            if (numJson(o, "track", x)) p.trk = x;
            if (numJson(o, "alt_baro", x)) p.alt = (int)std::lround(x);
            else if (valorJson(o, "alt_baro") == "ground") p.noChao = true;
            if (numJson(o, "baro_rate", x) || numJson(o, "geom_rate", x)) p.vs = (int)std::lround(x);
            return p;
        };
        double proxima = 0;
        while (e->vivo) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const double agora = relogioS();
            if (agora < proxima) continue;
            std::string chaveX;
            bool usarX = false, usarLol = false;
            {
                std::lock_guard<std::mutex> lk(e->m);
                chaveX = e->chaveX;
                usarX = !chaveX.empty() && !e->xRecusada && agora >= e->pausaX;
                usarLol = agora >= e->pausaLol;
            }
            if (!usarX && !usarLol) continue;
            // com a assinatura paga, cada aviao no maximo a cada 2 min (os planos tem limite por mes)
            const double intervalo = usarX ? 120 : 60;
            // a chave ainda pedida (ultimos 30 s) e consultada ha mais tempo
            std::string ch;
            {
                std::lock_guard<std::mutex> lk(e->m);
                double melhor = 1e18;
                for (const auto& [k, tp] : e->ultPedido) {
                    if (agora - tp > 30) continue;
                    const auto it = e->ultConsulta.find(k);
                    const double tc = it == e->ultConsulta.end() ? -1e9 : it->second;
                    if (agora - tc < intervalo) continue;
                    if (tc < melhor) { melhor = tc; ch = k; }
                }
            }
            if (ch.size() < 5) continue;
            const std::string tipo = ch.substr(0, ch.find(':')), val = ch.substr(ch.find(':') + 1);
            std::string hexMin = val;
            for (auto& c : hexMin) c = (char)std::tolower((unsigned char)c);
            double ptLa = 0, ptLo = 0;
            std::string ptVoo;
            if (tipo == "pt" && !lerPt(val, ptLa, ptLo, ptVoo)) continue;
            char ptTxt[64] = {};
            std::snprintf(ptTxt, sizeof ptTxt, "%.2f/%.2f", ptLa, ptLo);
            const std::string pto = ptTxt;
            {
                std::lock_guard<std::mutex> lk(e->m);
                e->ultConsulta[ch] = agora;
            }
            proxima = agora + 2;
            Pos p;
            bool respondeu = false;
            // 1) ADS-B Exchange (RapidAPI) - so com a chave do usuario
            if (usarX) {
                const char* host = "adsbexchange-com1.p.rapidapi.com";
                std::string url = std::string("https://") + host + "/v2/";
                if (tipo == "pt") {                         // 150 milhas nauticas em volta do ponto
                    std::string la = pto.substr(0, pto.find('/')), lo = pto.substr(pto.find('/') + 1);
                    url += "lat/" + la + "/lon/" + lo + "/dist/150/";
                } else
                    url += tipo == "hex" ? "hex/" + hexMin + "/" : tipo == "reg" ? "registration/" + val + "/" : "callsign/" + val + "/";
                std::string corpo;
                const int st = baixar(url, corpo, "X-RapidAPI-Key: " + chaveX + "\r\nX-RapidAPI-Host: " + host + "\r\n");
                std::lock_guard<std::mutex> lk(e->m);
                if (e->chaveX != chaveX) continue;                 // trocaram a chave no meio
                if (st == 200) {
                    p = ler(corpo, agora, "ADS-B Exchange", ch);
                    respondeu = true;
                    ++e->consultasX;
                    e->sitX = "ADS-B Exchange: " + std::to_string(e->consultasX) + " consulta(s)";
                } else if (st == 401 || st == 403) {
                    e->xRecusada = true;
                    e->sitX = "ADS-B Exchange recusou a chave (HTTP " + std::to_string(st) + "): confira a chave e a assinatura";
                } else if (st == 429) {
                    e->pausaX = agora + 600;
                    e->sitX = "ADS-B Exchange: limite da assinatura (HTTP 429) - tenta de novo em 10 min";
                } else {
                    e->pausaX = agora + 120;
                    e->sitX = st ? "ADS-B Exchange: HTTP " + std::to_string(st) + " - tenta de novo em 2 min"
                                 : "ADS-B Exchange sem resposta (tenta de novo em 2 min)";
                }
            }
            // 2) adsb.lol (aberto, gratis): sem chave, ou quando o ADS-B Exchange nao achou / nao respondeu
            if (!p.achou && usarLol) {
                std::string url = "https://api.adsb.lol/v2/";
                url += tipo == "pt" ? "point/" + pto + "/150" : tipo == "hex" ? "hex/" + hexMin : tipo == "reg" ? "reg/" + val : "callsign/" + val;
                std::string corpo;
                const int st = baixar(url, corpo);
                std::lock_guard<std::mutex> lk(e->m);
                if (st == 0 || st == 429 || st >= 500) {
                    e->situacao = st ? "adsb.lol ocupado (HTTP " + std::to_string(st) + ") - tenta de novo em 2 min"
                                     : "adsb.lol sem resposta (tenta de novo em 2 min)";
                    e->pausaLol = agora + 120;
                } else if (st != 200) {
                    e->situacao = "adsb.lol recusou (HTTP " + std::to_string(st) + ") - tenta de novo em 30 min";
                    e->pausaLol = agora + 1800;
                } else {
                    p = ler(corpo, agora, "adsb.lol", ch);
                    respondeu = true;
                    ++e->consultas;
                    e->situacao = "adsb.lol: " + std::to_string(e->consultas) + " consulta(s)";
                }
            }
            if (respondeu) {
                std::lock_guard<std::mutex> lk(e->m);
                e->res[ch] = p;
            }
        }
    }).detach();
}

void ConsultaPosicao::chaveAdsbx(const std::string& chave)
{
    std::string k;
    for (char c : chave) if (!std::isspace((unsigned char)c)) k += c;
    if (!e_) comecar();
    std::lock_guard<std::mutex> lk(e_->m);
    if (k == e_->chaveX) return;
    e_->chaveX = k;
    e_->xRecusada = false;
    e_->pausaX = 0;
    e_->consultasX = 0;
    e_->sitX = k.empty() ? "" : "ADS-B Exchange: chave pronta";
    e_->ultConsulta.clear();                 // com a chave nova, consulta todos de novo
}

void ConsultaPosicao::pedir(const std::string& chave)
{
    if (!e_) comecar();
    std::lock_guard<std::mutex> lk(e_->m);
    e_->ultPedido[chave] = relogioS();
}

bool ConsultaPosicao::pegar(const std::string& chave, Pos& p)
{
    if (!e_) return false;
    std::lock_guard<std::mutex> lk(e_->m);
    const auto it = e_->res.find(chave);
    if (it == e_->res.end()) return false;
    p = it->second;
    return true;
}

std::string ConsultaPosicao::situacao()
{
    if (!e_) return "adsb.lol";
    std::lock_guard<std::mutex> lk(e_->m);
    return e_->chaveX.empty() || e_->sitX.empty() ? e_->situacao : e_->sitX + "\n" + e_->situacao;
}

bool ConsultaPrefixo::info(const std::string& prefixo, Info& i)
{
    if (!e_) comecar();
    std::lock_guard<std::mutex> lk(e_->m);
    const auto it = e_->achados.find(chavePrefixo(prefixo));
    if (it == e_->achados.end()) return false;
    i = it->second;
    return true;
}

} // namespace masdr
