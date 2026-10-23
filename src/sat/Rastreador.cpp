#include "Rastreador.h"

#include "sgp4/CoordGeodetic.h"
#include "sgp4/CoordTopocentric.h"
#include "sgp4/DateTime.h"
#include "sgp4/Eci.h"
#include "sgp4/Observer.h"
#include "sgp4/SGP4.h"
#include "sgp4/SolarPosition.h"
#include "sgp4/Tle.h"

#include <windows.h>
#include <wininet.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace masdr {

namespace {
constexpr double kGrau = 57.29577951308232;

const libsgp4::DateTime& epoca1970()
{
    static const libsgp4::DateTime e(1970, 1, 1, 0, 0, 0);
    return e;
}
libsgp4::DateTime paraDt(double t) { return epoca1970().AddSeconds(t); }
double deDt(const libsgp4::DateTime& d) { return (d - epoca1970()).TotalSeconds(); }

double lonNormal(double lon)
{
    while (lon > 180) lon -= 360;
    while (lon < -180) lon += 360;
    return lon;
}

// Baixa um endereco inteiro (WinINet: existe do Windows 7 ao 11, usa o proxy do sistema)
bool baixarUrl(const std::string& url, std::string& out, std::string& erro)
{
    HINTERNET net = InternetOpenA("SDR990S", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) { erro = "InternetOpen falhou (" + std::to_string(GetLastError()) + ")"; return false; }
    DWORD to = 20000;
    InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
    // Windows 7: sem isto o WinINet tenta so TLS 1.0 e a Celestrak recusa
    DWORD prot = 0x00000800 /*TLS 1.2*/ | 0x00000200 /*TLS 1.1*/;
    InternetSetOptionA(net, 151 /*INTERNET_OPTION_SECURE_PROTOCOLS*/, &prot, sizeof prot);
    HINTERNET h = InternetOpenUrlA(net, url.c_str(), nullptr, 0,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
    if (!h) {
        erro = "sem conexao com a internet (erro " + std::to_string(GetLastError()) + ")";
        InternetCloseHandle(net);
        return false;
    }
    DWORD status = 0, len = sizeof status;
    HttpQueryInfoA(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr);
    char buf[16384];
    DWORD lidos = 0;
    while (InternetReadFile(h, buf, sizeof buf, &lidos) && lidos) {
        out.append(buf, lidos);
        if (out.size() > (8u << 20)) break;
    }
    InternetCloseHandle(h);
    InternetCloseHandle(net);
    if (status != 200) {
        std::string ini = out.substr(0, 120);
        for (auto& c : ini) if ((unsigned char)c < 0x20) c = ' ';
        erro = "o servidor respondeu " + std::to_string(status) + (ini.empty() ? "" : ": " + ini);
        return false;
    }
    return true;
}

// Separa o texto em TLEs (nome + linha 1 + linha 2; o nome e opcional)
struct TleTxt { std::string nome, l1, l2; };
std::vector<TleTxt> separarTle(const std::string& txt)
{
    std::vector<std::string> ls;
    std::istringstream in(txt);
    std::string l;
    while (std::getline(in, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        if (!l.empty()) ls.push_back(l);
    }
    std::vector<TleTxt> out;
    for (size_t i = 0; i + 1 < ls.size(); ++i) {
        if (ls[i].size() >= 69 && ls[i][0] == '1' && ls[i][1] == ' ' && ls[i + 1].size() >= 69 && ls[i + 1][0] == '2') {
            TleTxt t;
            if (i > 0 && !(ls[i - 1][0] == '2' && ls[i - 1].size() >= 69)) t.nome = ls[i - 1];
            while (!t.nome.empty() && t.nome.back() == ' ') t.nome.pop_back();
            t.l1 = ls[i].substr(0, 69);
            t.l2 = ls[i + 1].substr(0, 69);
            out.push_back(t);
            ++i;
        }
    }
    return out;
}
} // namespace

struct Rastreador::Item {
    libsgp4::Tle tle;
    libsgp4::SGP4 sgp4;
    double epoca = 0;
    std::mutex m;          // o SGP4 guarda estado interno no SDP4 (orbitas altas)
    explicit Item(const libsgp4::Tle& t) : tle(t), sgp4(t), epoca(deDt(t.Epoch())) {}
};

Rastreador::Rastreador() = default;

Rastreador::~Rastreador()
{
    if (dlTh_.joinable()) dlTh_.join();
    if (calcTh_.joinable()) calcTh_.join();
}

double Rastreador::agoraUtc()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (double)(t - 116444736000000000ULL) / 1e7;
}

void Rastreador::definirQth(double lat, double lon, double altM)
{
    std::lock_guard<std::mutex> lk(m_);
    lat_ = std::clamp(lat, -90.0, 90.0);
    lon_ = lonNormal(lon);
    altKm_ = altM / 1000.0;
}

void Rastreador::qth(double& lat, double& lon, double& altKm) const
{
    std::lock_guard<std::mutex> lk(m_);
    lat = lat_; lon = lon_; altKm = altKm_;
}

bool Rastreador::carregarArquivo(const std::string& caminho, std::string& erro, bool mesclar)
{
    std::ifstream f(caminho, std::ios::binary);
    if (!f) { erro = "arquivo nao encontrado: " + caminho; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::map<int, std::shared_ptr<Item>> novos;
    for (const auto& t : separarTle(ss.str())) {
        try {
            libsgp4::Tle tle(t.nome, t.l1, t.l2);
            novos[(int)tle.NoradNumber()] = std::make_shared<Item>(tle);
        } catch (...) {
            // TLE com defeito (ou satelite que ja caiu): fica de fora
        }
    }
    if (novos.empty()) { erro = "nenhum TLE valido em " + caminho; return false; }
    std::lock_guard<std::mutex> lk(m_);
    if (mesclar) {
        // outro grupo (ex.: meteorologicos) por cima: os NORAD nao se repetem
        for (const auto& kv : novos) sats_[kv.first] = kv.second;
    } else {
        sats_.swap(novos);
    }
    return true;
}

size_t Rastreador::nTle() const
{
    std::lock_guard<std::mutex> lk(m_);
    return sats_.size();
}

std::shared_ptr<Rastreador::Item> Rastreador::item(int norad) const
{
    std::lock_guard<std::mutex> lk(m_);
    auto it = sats_.find(norad);
    return it == sats_.end() ? nullptr : it->second;
}

bool Rastreador::temTle(int norad) const { return item(norad) != nullptr; }

double Rastreador::epocaTle(int norad) const
{
    auto it = item(norad);
    return it ? it->epoca : 0.0;
}

void Rastreador::baixar(const std::vector<std::string>& urls, const std::string& caminho)
{
    if (dl_.load() == 1) return;
    if (dlTh_.joinable()) dlTh_.join();
    dl_ = 1;
    {
        std::lock_guard<std::mutex> lk(mdl_);
        dlMsg_ = "baixando os TLE da Celestrak...";
    }
    dlTh_ = std::thread([this, urls, caminho] {
        // cada grupo da Celestrak vem em um arquivo; todos sao salvos juntos
        // no mesmo satelites.tle (o NORAD nao se repete entre os grupos)
        std::string dados, erro;
        size_t n = 0;
        bool algum = false;
        for (const auto& url : urls) {
            std::string parte, erroParte;
            if (!baixarUrl(url, parte, erroParte)) {
                if (erro.empty()) erro = erroParte;
                continue;
            }
            const size_t np = separarTle(parte).size();
            if (np < 1) continue;
            if (!dados.empty()) dados += "\n";
            dados += parte;
            n += np;
            algum = true;
        }
        bool ok = algum && n >= 3;
        if (!ok && erro.empty()) erro = "a resposta nao tem TLE";
        if (ok) {
            const std::string tmp = caminho + ".novo";
            {
                std::ofstream f(tmp, std::ios::binary);
                f << dados;
                ok = (bool)f;
            }
            if (ok) ok = MoveFileExA(tmp.c_str(), caminho.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            if (!ok) erro = "nao consegui gravar " + caminho;
            else ok = carregarArquivo(caminho, erro);
        }
        std::string msg = ok ? "TLE atualizado: " + std::to_string(n) + " satelites" : "TLE nao atualizado - " + erro;
        {
            std::lock_guard<std::mutex> lk(mdl_);
            dlMsg_ = msg;
        }
        dl_ = ok ? 2 : -1;
    });
}

std::string Rastreador::msgDownload() const
{
    std::lock_guard<std::mutex> lk(mdl_);
    return dlMsg_;
}

PosSat Rastreador::posicao(int norad, double t) const
{
    PosSat p;
    auto it = item(norad);
    if (!it) return p;
    double la, lo, al;
    qth(la, lo, al);
    try {
        std::lock_guard<std::mutex> lk(it->m);
        const libsgp4::Eci eci = it->sgp4.FindPosition(paraDt(t));
        libsgp4::Observer obs(la, lo, al);
        const libsgp4::CoordTopocentric tp = obs.GetLookAngle(eci);
        const libsgp4::CoordGeodetic g = eci.ToGeodetic();
        p.az = tp.azimuth * kGrau;
        p.el = tp.elevation * kGrau;
        p.distKm = tp.range;
        p.velRad = tp.rangeRate;
        p.lat = g.latitude * kGrau;
        p.lon = lonNormal(g.longitude * kGrau);
        p.altKm = g.altitude;
        p.ok = true;
    } catch (...) {
        p.ok = false;
    }
    return p;
}

bool Rastreador::ehGeo(int norad) const
{
    auto it = item(norad);
    if (!it) return false;
    try {
        // movimento medio em voltas por dia: geostacionario e ~1 (periodo de 24 h).
        // O HEO/Molniya (Meridian) da ~2 e o LEO, de 12 a 16. Alguns GEO velhos
        // estao em orbitas de cemiterio, com inclinacao grande - por isso o
        // teste e pelo periodo, e nao pela inclinacao.
        const double mm = it->tle.MeanMotion();
        return mm > 0.5 && mm < 1.2;
    } catch (...) {
        return false;
    }
}

std::vector<Passagem> Rastreador::passagens(int norad, double t0, double horas, double elMin, int maxN) const
{
    std::vector<Passagem> out;
    auto it = item(norad);
    if (!it) return out;
    double la, lo, al;
    qth(la, lo, al);
    // copia propria do SGP4: pode rodar em outra thread sem disputar o da tela
    std::unique_ptr<libsgp4::SGP4> sg;
    try { sg = std::make_unique<libsgp4::SGP4>(it->tle); } catch (...) { return out; }
    libsgp4::Observer obs(la, lo, al);
    auto olhar = [&](double t, double* az) -> double {
        try {
            const libsgp4::CoordTopocentric tp = obs.GetLookAngle(sg->FindPosition(paraDt(t)));
            if (az) *az = tp.azimuth * kGrau;
            return tp.elevation * kGrau;
        } catch (...) {
            return -999.0;
        }
    };
    auto el = [&](double t) { return olhar(t, nullptr); };
    // horizonte exato entre a (abaixo/acima) e b, por bissecao (~0,1 s)
    auto cruzar = [&](double a, double b, bool subindo) {
        for (int k = 0; k < 22; ++k) {
            const double m = 0.5 * (a + b);
            if ((el(m) > 0) == subindo) b = m; else a = m;
        }
        return 0.5 * (a + b);
    };

    double t = t0;
    const double fim = t0 + horas * 3600.0;
    double e = el(t);
    if (e < -900) return out;
    if (e > 0) {   // ja esta passando: volta ate a entrada
        double tb = t;
        for (int i = 0; i < 240 && el(tb) > 0; ++i) tb -= 30;
        t = tb;
        e = el(t);
    }
    while (t < fim && (int)out.size() < maxN) {
        const double passo = e < -40 ? 150 : e < -15 ? 60 : 20;
        const double t2 = t + passo;
        const double e2 = el(t2);
        if (e2 < -900) break;
        if (e <= 0 && e2 > 0) {
            Passagem p;
            p.aos = cruzar(t, t2, true);
            // anda de 10 em 10 s ate sair, guardando o ponto mais alto
            double tt = p.aos, melhor = -99, tMelhor = p.aos;
            for (;;) {
                const double tn = tt + 10;
                const double en = el(tn);
                if (en > melhor) { melhor = en; tMelhor = tn; }
                if (en <= 0 || tn - p.aos > 12 * 3600) { p.los = en <= 0 ? cruzar(tt, tn, false) : tn; break; }
                tt = tn;
            }
            // refina o ponto mais alto (secao aurea em +-10 s)
            double a = std::max(p.aos, tMelhor - 10), b = std::min(p.los, tMelhor + 10);
            for (int k = 0; k < 30; ++k) {
                const double m1 = b - (b - a) * 0.618, m2 = a + (b - a) * 0.618;
                if (el(m1) < el(m2)) a = m1; else b = m2;
            }
            p.tca = 0.5 * (a + b);
            p.elMax = olhar(p.tca, &p.azTca);
            olhar(p.aos + 0.5, &p.azAos);
            olhar(p.los - 0.5, &p.azLos);
            const double losP = p.los;
            if (p.elMax >= elMin && p.los > t0) {
                const int n = std::clamp((int)((p.los - p.aos) / 5), 24, 240);
                p.curva.reserve(n + 1);
                for (int i = 0; i <= n; ++i) {
                    double az = 0;
                    const double ev = olhar(p.aos + (p.los - p.aos) * i / n, &az);
                    p.curva.emplace_back((float)az, (float)std::max(0.0, ev));
                }
                out.push_back(std::move(p));
            }
            t = std::max(t2, losP) + 30;
            e = el(t);
            continue;
        }
        t = t2;
        e = e2;
    }
    return out;
}

std::vector<std::pair<float, float>> Rastreador::trilha(int norad, double t0, double t1, double passo) const
{
    std::vector<std::pair<float, float>> out;
    auto it = item(norad);
    if (!it || passo <= 0) return out;
    std::lock_guard<std::mutex> lk(it->m);
    for (double t = t0; t <= t1 + 1e-6; t += passo) {
        try {
            const libsgp4::CoordGeodetic g = it->sgp4.FindPosition(paraDt(t)).ToGeodetic();
            out.emplace_back((float)(g.latitude * kGrau), (float)lonNormal(g.longitude * kGrau));
        } catch (...) {
            break;
        }
    }
    return out;
}

void Rastreador::posicaoSol(double t, double& lat, double& lon)
{
    libsgp4::SolarPosition sp;
    const libsgp4::CoordGeodetic g = sp.FindPosition(paraDt(t)).ToGeodetic();
    lat = g.latitude * kGrau;
    lon = lonNormal(g.longitude * kGrau);
}

void Rastreador::pedirProximas(const std::vector<int>& norads, double elMin)
{
    if (calc_.load()) return;
    if (calcTh_.joinable()) calcTh_.join();
    calc_ = true;
    calcTh_ = std::thread([this, norads, elMin] {
        const double t0 = agoraUtc();
        std::map<int, Passagem> res;
        for (int n : norads) {
            if (res.count(n)) continue;
            auto ps = passagens(n, t0, 48, elMin, 1);
            if (!ps.empty()) {
                ps[0].curva.clear();        // a lista so precisa dos horarios
                ps[0].curva.shrink_to_fit();
                res[n] = ps[0];
            }
        }
        {
            std::lock_guard<std::mutex> lk(mp_);
            prox_.swap(res);
        }
        calc_ = false;
    });
}

bool Rastreador::proxima(int norad, Passagem& p) const
{
    std::lock_guard<std::mutex> lk(mp_);
    auto it = prox_.find(norad);
    if (it == prox_.end()) return false;
    p = it->second;
    return true;
}

} // namespace masdr
