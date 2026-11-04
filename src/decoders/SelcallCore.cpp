#include "SelcallCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace masdr {

namespace {
constexpr int kN = 2048;                 // 256 ms a 8 kHz: separa tons de 16 Hz (A e T)
constexpr int kH = 256;                  // um quadro a cada 32 ms
constexpr double kFmin = 270.0, kFmax = 1650.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kLimiar = 6.0;      // o tom continua com o pico >= 6x a mediana do ruido (7,8 dB num bin de 3,9 Hz)
constexpr double kInicio = 15.0;     // mas so comeca uma trilha com >= 15x (11,8 dB): o ruido nao vira trilha
constexpr double kTomSnr = 30.0;     // e so e tom de SELCAL se chegou a >= 30x (14,8 dB) em algum quadro
constexpr double kSumico = 0.35;     // o tom pode sumir ate 0,35 s (desvanecimento) sem cortar a trilha
constexpr double kTomMin = 0.40, kTomMax = 1.80;   // duracao de um tom (1,0 +-0,25 s + a janela de 256 ms)

// ordem dos designadores: os 16 classicos (ordem alfabetica = ordem de frequencia) e os do SELCAL 32
const char kLetras[] = "ABCDEFGHJKLMPQRSTUVWXYZ123456789";
// Anexo 10 Vol. III: tabela classica e a Tabela 3-1 da Emenda 91 (tons "Red")
const double kFreq[32] = {
    312.6, 346.7, 384.6, 426.6, 473.2, 524.8, 582.1, 645.7,      // A B C D E F G H
    716.1, 794.3, 881.0, 977.2, 1083.9, 1202.3, 1333.5, 1479.1,  // J K L M P Q R S
    329.2, 365.2, 405.0, 449.3, 498.3, 552.7, 613.1,             // T U V W X Y Z
    680.0, 754.2, 836.6, 927.9, 1029.2, 1141.6, 1266.2, 1404.4, 1557.8   // 1..9
};

void fft(std::vector<double>& re, std::vector<double>& im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double a = -2 * kPi / (double)len;
        const double wr = std::cos(a), wi = std::sin(a);
        for (size_t i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k) {
                const size_t p = i + k, q = p + len / 2;
                const double tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
                re[q] = re[p] - tr; im[q] = im[p] - ti;
                re[p] += tr; im[p] += ti;
                const double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}
} // namespace

const char* SelcallCore::letras() { return kLetras; }

double SelcallCore::freqTom(char c)
{
    const char* p = std::strchr(kLetras, c);
    return c && p ? kFreq[p - kLetras] : 0.0;
}

SelcallCore::SelcallCore(double taxa) : taxa_(taxa)
{
    janela_.resize(kN);
    for (int i = 0; i < kN; ++i) janela_[i] = (float)(0.5 - 0.5 * std::cos(2 * kPi * i / (kN - 1)));
    re_.resize(kN); im_.resize(kN);
    limpar();
}

void SelcallCore::limpar()
{
    buf_.clear(); base_ = 0; ini_ = 0;
    trilhas_.clear(); tons_.clear(); recentes_.clear(); temAnterior_ = false;
    lista_.clear(); texto_.clear();
    est_ = Estado();
}

std::string SelcallCore::horaUtc() const
{
    const std::time_t t = std::time(nullptr);
    std::tm u{};
#ifdef _WIN32
    gmtime_s(&u, &t);
#else
    gmtime_r(&t, &u);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", u.tm_hour, u.tm_min, u.tm_sec);
    return b;
}

std::string SelcallCore::pegarTexto()
{
    std::string s;
    s.swap(texto_);
    return s;
}

void SelcallCore::alimentar(const float* x, size_t n)
{
    buf_.insert(buf_.end(), x, x + n);
    while (buf_.size() - ini_ >= (size_t)kN) {
        quadro();
        ini_ += kH;
    }
    if (ini_ > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + (std::ptrdiff_t)ini_);
        base_ += (long long)ini_;
        ini_ = 0;
    }
}

// Um quadro: os picos da faixa acima do ruido, cada um entra na trilha do seu tom
void SelcallCore::quadro()
{
    const float* s = buf_.data() + ini_;
    double media = 0;
    for (int i = 0; i < kN; ++i) media += s[i];
    media /= kN;
    for (int i = 0; i < kN; ++i) { re_[i] = (s[i] - media) * janela_[i]; im_[i] = 0; }
    fft(re_, im_);
    const double bin = taxa_ / kN;
    const int k0 = (int)std::ceil(kFmin / bin), k1 = (int)std::floor(kFmax / bin);
    std::vector<double> P(k1 + 4, 0.0);
    for (int k = k0 - 3; k <= k1 + 3; ++k) P[k] = re_[k] * re_[k] + im_[k] * im_[k];
    std::vector<double> ord(P.begin() + k0, P.begin() + k1 + 1);
    std::nth_element(ord.begin(), ord.begin() + ord.size() / 2, ord.end());
    const double ruido = std::max(ord[ord.size() / 2], 1e-20);
    const double t = (double)(base_ + (long long)ini_ + kN / 2) / taxa_;

    // picos (maximos locais) acima do limiar, do mais forte para o mais fraco
    std::vector<int> pk;
    for (int k = k0; k <= k1; ++k)
        if (P[k] > P[k - 1] && P[k] >= P[k + 1] && P[k] >= kLimiar * ruido) pk.push_back(k);
    std::sort(pk.begin(), pk.end(), [&](int a, int b) { return P[a] > P[b]; });
    std::vector<int> aceitos;
    for (int k : pk) {
        bool ok = true;
        for (int a : aceitos) {
            const int d = std::abs(k - a);
            // A e T ficam a 16,6 Hz (4,25 bins): 3 bins ainda separa os dois.
            // Ate 6 bins de um pico muito mais forte (15 dB) e a saia dele.
            if (d < 3 || (d <= 6 && P[k] < P[a] / 30.0)) { ok = false; break; }
        }
        if (ok) aceitos.push_back(k);
        if (aceitos.size() >= 8) break;
    }
    for (auto& tr : trilhas_) tr.usadoAgora = false;
    for (int k : aceitos) {
        const double l0 = std::log(P[k] + 1e-30), lm = std::log(P[k - 1] + 1e-30), lp = std::log(P[k + 1] + 1e-30);
        const double den = lm - 2 * l0 + lp;
        const double f = (k + (std::fabs(den) > 1e-12 ? std::clamp(0.5 * (lm - lp) / den, -0.5, 0.5) : 0.0)) * bin;
        const double snr = P[k] / ruido;
        // a trilha mais perto (o tom balanca 1 bin de um quadro para o outro)
        Trilha* melhor = nullptr;
        double dm = 1e9;
        for (auto& tr : trilhas_) {
            if (tr.usadoAgora) continue;
            const double d = std::fabs(f - tr.f());
            // ate 1,5 bin (6 Hz) ou 0,6%; e um tom forte nao continua com um pico 23 dB
            // abaixo do maximo dele (e o ruido em volta, que espichava a trilha)
            if (d <= std::max(0.006 * tr.f(), 1.5 * bin) && d < dm && snr >= tr.snrMax / 200.0) { dm = d; melhor = &tr; }
        }
        const double w = std::log10(snr);           // os quadros fortes pesam mais na frequencia
        if (melhor) {
            melhor->fSoma += f * w; melhor->pSoma += w;
            melhor->t1 = t; ++melhor->hits; melhor->usadoAgora = true;
            melhor->snrMax = std::max(melhor->snrMax, snr);
            // portadora (nao para nunca): nao guarda as marcas (nao vira tom e nao cresce a memoria)
            if (melhor->t1 - melhor->t0 <= 4 * kTomMax) melhor->marcas.emplace_back((float)t, (float)snr);
            else if (!melhor->marcas.empty()) { melhor->marcas.clear(); melhor->marcas.shrink_to_fit(); }
        } else if (snr >= kInicio) {
            Trilha tr;
            tr.fSoma = f * w; tr.pSoma = w; tr.t0 = tr.t1 = t; tr.hits = 1; tr.snrMax = snr; tr.usadoAgora = true;
            tr.marcas.emplace_back((float)t, (float)snr);
            trilhas_.push_back(tr);
        }
    }
    // trilhas que sumiram ha mais de 0,35 s acabaram
    for (size_t i = 0; i < trilhas_.size();) {
        if (t - trilhas_[i].t1 > kSumico) {
            const Trilha tr = trilhas_[i];
            trilhas_.erase(trilhas_.begin() + (std::ptrdiff_t)i);
            fecharTrilha(tr, t);
        } else ++i;
    }
    // tons sem par ha muito tempo e o pulso sozinho (sem o segundo) sao esquecidos
    tons_.erase(std::remove_if(tons_.begin(), tons_.end(), [&](const Tom& x) { return t - x.t1 > 3.0; }), tons_.end());
    recentes_.erase(std::remove_if(recentes_.begin(), recentes_.end(), [&](const Tom& x) { return t - x.t1 > 3.0; }), recentes_.end());
    if (temAnterior_ && t - anterior_.t1 > 2.5) temAnterior_ = false;

    // "tons no ar": os dois mais fortes que estao tocando agora (nao portadora)
    const Trilha* m1 = nullptr; const Trilha* m2 = nullptr;
    for (const auto& tr : trilhas_) {
        if (!tr.usadoAgora || tr.hits < 4 || tr.snrMax < kTomSnr || tr.t1 - tr.t0 > kTomMax) continue;
        if (!m1 || tr.snrMax > m1->snrMax) { m2 = m1; m1 = &tr; }
        else if (!m2 || tr.snrMax > m2->snrMax) m2 = &tr;
    }
    est_.tons = m1 && m2;
    if (est_.tons) { est_.f1 = std::min(m1->f(), m2->f()); est_.f2 = std::max(m1->f(), m2->f()); }
}

// Uma trilha acabou: se tem cara de tom de SELCAL, procura o outro tom do pulso
void SelcallCore::fecharTrilha(const Trilha& tr, double agora)
{
    const double hop = (double)kH / taxa_;
    if (tr.snrMax < kTomSnr || tr.t1 - tr.t0 > 4 * kTomMax) return;        // fraco demais ou portadora
    // o tom vai de onde ele esta a menos de 17 dB do maximo (o resto e a cauda
    // no ruido, que espicha a trilha); conta os quadros dentro desse trecho
    const float corte = (float)std::max(kLimiar, tr.snrMax / 50.0);
    double a = 1e9, b = -1e9;
    for (const auto& m : tr.marcas) if (m.second >= corte) { a = std::min(a, (double)m.first); b = std::max(b, (double)m.first); }
    int hits = 0;
    for (const auto& m : tr.marcas) if (m.first >= a && m.first <= b) ++hits;
    const double dur = b - a + hop;
    if (dur < kTomMin || dur > kTomMax || hits < 8 || hits * hop < 0.5 * dur) return;   // curto, portadora ou picado demais
    Tom novo{tr.f(), a, b, tr.snrMax};
    auto sobrepoe = [](double a0, double a1, double b0, double b1) { return std::min(a1, b1) - std::max(a0, b0); };
    auto mesmoTom = [](double x, double y) { return std::fabs(x - y) / std::min(x, y) < 0.03; };
    // O multipercurso do HF "espalha" um tom forte (visto: 630-646 Hz num tom de
    // 640): ele vira duas ou tres trilhas. Pedaco a menos de 3% de um tom que ja
    // acabou e do mesmo tempo e o mesmo tom: junta (ou some, se o tom ja virou pulso).
    if (temAnterior_ && (mesmoTom(novo.f, anterior_.f1) || mesmoTom(novo.f, anterior_.f2)) &&
        sobrepoe(novo.t0, novo.t1, anterior_.t0, anterior_.t1) > 0) return;
    for (size_t i = 0; i < tons_.size(); ++i) {
        Tom& o = tons_[i];
        if (!mesmoTom(o.f, novo.f) || sobrepoe(o.t0, o.t1, novo.t0, novo.t1) <= 0) continue;
        const Tom forte = o.snr >= novo.snr ? o : novo;
        novo.f = forte.f;                      // a frequencia do pedaco mais forte
        novo.t0 = std::min(o.t0, novo.t0); novo.t1 = std::max(o.t1, novo.t1);
        novo.snr = forte.snr;
        if (novo.t1 - novo.t0 + hop > kTomMax) { tons_.erase(tons_.begin() + (std::ptrdiff_t)i); return; }
        tons_.erase(tons_.begin() + (std::ptrdiff_t)i);
        break;
    }
    // o par: comeca e acaba junto (tolerancia de 0,5 s: um tom desvanecido no comeco
    // ou no fim parece mais curto - visto na gravacao real)
    int par = -1;
    double melhor = 0;
    for (size_t i = 0; i < tons_.size(); ++i) {
        const Tom& o = tons_[i];
        const double r = std::max(o.f, novo.f) / std::min(o.f, novo.f);
        if (r < 1.04) continue;                                  // dois tons da tabela ficam >= 5% longe
        const double sb = sobrepoe(o.t0, o.t1, novo.t0, novo.t1);
        const double curto = std::min(o.t1 - o.t0, novo.t1 - novo.t0) + hop;
        if (sb < 0.6 * curto || std::fabs(o.t0 - novo.t0) > 0.5 || std::fabs(o.t1 - novo.t1) > 0.5) continue;
        if (sb > melhor) { melhor = sb; par = (int)i; }
    }
    recentes_.push_back(novo);
    if (par < 0) { tons_.push_back(novo); return; }
    const Tom o = tons_[(size_t)par];
    tons_.erase(tons_.begin() + par);
    // um terceiro tom junto e voz (vogal com harmonicos), nao SELCAL
    const double p0 = std::min(o.t0, novo.t0), p1 = std::max(o.t1, novo.t1);
    int terceiros = 0;
    // (so conta tom de forca parecida: ate 10 dB abaixo do mais fraco do par)
    const double forca = 0.1 * std::min(o.snr, novo.snr);
    // pedaco do mesmo tom nao conta, nem a "saia" (ate 5% de um tom e 10 dB mais
    // fraca que ele; A e T, os tons mais juntos da tabela, ficam a 5,3%)
    auto perto5 = [](double x, double y) { return std::fabs(x - y) / std::min(x, y) < 0.05; };
    auto saia = [&](double f, double snr) {
        if ((perto5(f, o.f) && snr < 0.1 * o.snr) || (perto5(f, novo.f) && snr < 0.1 * novo.snr)) return true;
        for (const auto& x : trilhas_) if (perto5(f, x.f()) && snr < 0.1 * x.snrMax) return true;   // o tom forte ainda tocando
        for (const auto& x : tons_) if (perto5(f, x.f) && snr < 0.1 * x.snr) return true;
        return false;
    };
    auto outro = [&](double f, double snr) { return !mesmoTom(f, o.f) && !mesmoTom(f, novo.f) && !saia(f, snr); };
    for (const auto& x : recentes_)
        if (x.snr >= forca && outro(x.f, x.snr) && sobrepoe(x.t0, x.t1, p0, p1) > 0.5 * (x.t1 - x.t0 + hop)) ++terceiros;
    for (const auto& x : trilhas_)
        if (x.snrMax >= forca && outro(x.f(), x.snrMax) && x.hits >= 8 && x.t1 - x.t0 + hop >= kTomMin && x.t1 - x.t0 <= kTomMax && agora - x.t0 < 2.0 &&
            sobrepoe(x.t0, x.t1, p0, p1) > 0.5 * (x.t1 - x.t0 + hop)) ++terceiros;
    if (terceiros > 0) return;
    Pulso p;
    p.f1 = std::min(o.f, novo.f); p.f2 = std::max(o.f, novo.f);
    p.t0 = p0; p.t1 = p1;
    p.nivel = 10.0 * std::log10(std::min(o.snr, novo.snr));
    if (p.t1 - p.t0 + hop >= 0.55) novoPulso(p);
}

void SelcallCore::novoPulso(const Pulso& p)
{
    if (temAnterior_) {
        const double gap = p.t0 - anterior_.t1;   // 0,2 +- 0,1 s (medido pelo centro das janelas)
        if (gap >= -0.4 && gap <= 0.9) {
            const double f[4] = {anterior_.f1, anterior_.f2, p.f1, p.f2};
            double desvio = 0;
            bool sel32 = false;
            const std::string cod = decodificar(f, desvio, sel32);
            if (!cod.empty()) {
                temAnterior_ = false;
                Chamada c;
                c.codigo = cod; c.hora = horaUtc(); c.desvioHz = desvio;
                c.nivelDb = std::min(anterior_.nivel, p.nivel); c.selcal32 = sel32;
                auto it = std::find_if(lista_.begin(), lista_.end(), [&](const Chamada& x) { return x.codigo == cod; });
                if (it != lista_.end()) { c.vezes = it->vezes + 1; lista_.erase(it); }
                lista_.insert(lista_.begin(), c);
                if (lista_.size() > 300) lista_.pop_back();
                char b[200];
                std::snprintf(b, sizeof b, "[SELCAL] %s UTC  %s%s  (tons %.0f+%.0f / %.0f+%.0f Hz, desvio %+.0f Hz, %.0f dB)\n",
                              c.hora.c_str(), cod.c_str(), sel32 ? "  SELCAL32" : "",
                              anterior_.f1, anterior_.f2, p.f1, p.f2, desvio, c.nivelDb);
                texto_ += b;
                ++est_.chamadas;
                est_.ultimo = cod; est_.ultimaHora = c.hora;
                return;
            }
        }
    }
    anterior_ = p;                 // pode ser o 1o pulso da proxima chamada
    temAnterior_ = true;
}

// Os 4 tons -> "AB-CD". Procura o desvio de sintonia (o mesmo para os 4) que
// faz todos cairem na tabela; primeiro so com os 16 tons classicos.
std::string SelcallCore::decodificar(const double f[4], double& desvio, bool& sel32)
{
    for (int conjunto = 16; conjunto <= 32; conjunto += 16) {
        const double tol = conjunto == 16 ? 0.012 : 0.009;   // meio caminho: 5,3% (32) / 10,9% (16)
        double melhorCusto = 1e9, melhorErro = 1, melhorD = 0;
        int melhorIdx[4] = {-1, -1, -1, -1};
        for (double d = -60.0; d <= 60.0; d += 0.25) {
            double maxErr = 0;
            int idx[4];
            for (int i = 0; i < 4; ++i) {
                const double g = f[i] - d;
                double e = 1e9;
                for (int j = 0; j < conjunto; ++j) {
                    const double r = std::fabs(g - kFreq[j]) / kFreq[j];
                    if (r < e) { e = r; idx[i] = j; }
                }
                maxErr = std::max(maxErr, e);
            }
            const double custo = maxErr + std::fabs(d) * 1e-5;
            if (custo < melhorCusto) {
                melhorCusto = custo; melhorErro = maxErr; melhorD = d;
                std::copy(idx, idx + 4, melhorIdx);
            }
        }
        if (melhorErro > tol) continue;
        // 4 tons diferentes (nenhuma letra se repete no codigo)
        bool rep = false;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) if (melhorIdx[i] == melhorIdx[j]) rep = true;
        if (rep) continue;
        auto par = [&](int x, int y) {
            if (x > y) std::swap(x, y);                       // ordem dos designadores (A..S, T..Z, 1..9)
            return std::string(1, kLetras[x]) + kLetras[y];
        };
        desvio = melhorD;
        sel32 = false;
        for (int i = 0; i < 4; ++i) if (melhorIdx[i] >= 16) sel32 = true;
        return par(melhorIdx[0], melhorIdx[1]) + "-" + par(melhorIdx[2], melhorIdx[3]);
    }
    return {};
}

} // namespace masdr
