#include "AdsbInternet.h"

#include "../decoders/AdsbCore.h"

#include <windows.h>
#include <wininet.h>

#include <chrono>
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
int baixar(const std::string& url, std::string& out)
{
    out.clear();
    HINTERNET net = InternetOpenA("SDR990S", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) return 0;
    DWORD to = 10000;
    InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
    DWORD prot = 0x00000800 /*TLS 1.2*/ | 0x00000200 /*TLS 1.1*/;   // Windows 7
    InternetSetOptionA(net, 151 /*INTERNET_OPTION_SECURE_PROTOCOLS*/, &prot, sizeof prot);
    HINTERNET h = InternetOpenUrlA(net, url.c_str(), "Accept: application/json\r\n", (DWORD)-1,
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

} // namespace masdr
