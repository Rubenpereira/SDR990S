#include "Ft8Core.h"

#include <ft8_lib/ft8/decode.h>
#include <ft8_lib/ft8/encode.h>
#include <ft8_lib/ft8/message.h>
#include <ft8_lib/fft/kiss_fftr.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace masdr {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float  kFMin = 200.f;          // banda analisada no audio (Hz)
constexpr float  kFMax = 3000.f;
constexpr int    kTimeOsr = 2;           // 2 analises por simbolo
constexpr int    kFreqOsr = 2;           // 2 bins por tom
constexpr int    kMaxCandidatos = 200;
constexpr int    kScoreMin = 10;
constexpr int    kIteracoesLdpc = 30;
// Entrou no periodo com ate isto de atraso: ainda vale (o sinal so comeca
// 0,5 s depois do inicio; o que faltar vira silencio no comeco do buffer).
constexpr double kEntradaMax = 1.0;
// DT: o quadro da analise de um bloco termina meio simbolo depois dele (o
// quadro tem 2 simbolos e anda meio de cada vez), por isso a hora que o
// ft8_lib devolve fica 1 simbolo adiantada; e o FT8 comeca 0,5 s depois
// do inicio do periodo. Conferido nas gravacoes de teste do ft8_lib contra
// o DT do WSJT-X.
constexpr double kInicioNominal = 0.5;

// --- tabela de indicativos (as mensagens com <...> usam o hash de 10, 12
// ou 22 bits de um indicativo que ja passou antes). Igual ao exemplo do
// ft8_lib; uma so tabela (so existe um Ft8Core por vez). ---
constexpr int kTabela = 256;
struct EntradaTabela { char indicativo[12]; uint32_t hash; };
EntradaTabela g_tabela[kTabela];
int g_tabelaN = 0;
std::mutex g_tabelaMutex;     // decodificacao de um periodo por vez

void tabelaLimpar() { std::memset(g_tabela, 0, sizeof g_tabela); g_tabelaN = 0; }

void tabelaEnvelhecer(uint8_t idadeMax)
{
    for (auto& e : g_tabela) {
        if (!e.indicativo[0]) continue;
        const uint8_t idade = (uint8_t)(e.hash >> 24);
        if (idade > idadeMax) { e.indicativo[0] = 0; e.hash = 0; --g_tabelaN; }
        else e.hash = (((uint32_t)idade + 1u) << 24) | (e.hash & 0x3FFFFFu);
    }
}

void tabelaGuardar(const char* indicativo, uint32_t hash)
{
    const uint16_t h10 = (hash >> 12) & 0x3FFu;
    int i = (h10 * 23) % kTabela;
    for (int voltas = 0; g_tabela[i].indicativo[0] && voltas < kTabela; ++voltas) {
        if ((g_tabela[i].hash & 0x3FFFFFu) == hash && std::strcmp(g_tabela[i].indicativo, indicativo) == 0) {
            g_tabela[i].hash &= 0x3FFFFFu;      // visto de novo: idade zero
            return;
        }
        i = (i + 1) % kTabela;
    }
    if (g_tabela[i].indicativo[0]) return;      // cheia
    ++g_tabelaN;
    std::strncpy(g_tabela[i].indicativo, indicativo, 11);
    g_tabela[i].indicativo[11] = 0;
    g_tabela[i].hash = hash;
}

bool tabelaProcurar(ftx_callsign_hash_type_t tipo, uint32_t hash, char* indicativo)
{
    const uint8_t desloc = tipo == FTX_CALLSIGN_HASH_10_BITS ? 12 : (tipo == FTX_CALLSIGN_HASH_12_BITS ? 10 : 0);
    const uint16_t h10 = (hash >> (12 - desloc)) & 0x3FFu;
    int i = (h10 * 23) % kTabela;
    for (int voltas = 0; g_tabela[i].indicativo[0] && voltas < kTabela; ++voltas) {
        if (((g_tabela[i].hash & 0x3FFFFFu) >> desloc) == hash) {
            std::strcpy(indicativo, g_tabela[i].indicativo);
            return true;
        }
        i = (i + 1) % kTabela;
    }
    indicativo[0] = 0;
    return false;
}

ftx_callsign_hash_interface_t g_hashIf = {tabelaProcurar, tabelaGuardar};

int horaHHMMSS(double utc)
{
    const long long s = (long long)std::floor(utc + 1e-6);
    const long long d = ((s % 86400) + 86400) % 86400;
    return (int)((d / 3600) * 10000 + ((d / 60) % 60) * 100 + d % 60);
}
} // namespace

struct Ft8Core::Impl {
    // --- parametros ---
    double fs;
    bool   ft4;
    double simbolo, periodo;
    int bloco = 0, sub = 0, nfft = 0, minBin = 0, maxBin = 0, maxBlocos = 0;
    size_t precisa = 0;                 // amostras de um periodo que a analise usa
    float  ajusteSnr = 0;               // calibracao (dB)

    // --- analise espectral (so a thread de decodificacao mexe) ---
    std::vector<float> janela, quadro, tempo;
    std::vector<kiss_fft_cpx> freq;
    std::vector<char> fftMem;
    kiss_fftr_cfg fftCfg = nullptr;
    std::vector<WF_ELEM_T> mag;
    ftx_waterfall_t wf{};

    // --- relogio e periodo (thread que alimenta) ---
    bool   temRelogio = false;
    double t0 = 0;                      // hora UTC da amostra 0
    long long cont = 0;                 // amostras ja recebidas
    long long periodoAtual = LLONG_MIN;
    bool   valido = false, mandado = false;
    std::vector<float> buf;

    // --- estado lido pela tela ---
    std::atomic<double> noPeriodo{0};
    std::atomic<bool>   esperando{true}, ocupado{false};
    std::atomic<int>    ultimo{0}, total{0};
    std::atomic<double> nivelDb{-120};

    // --- thread de decodificacao ---
    std::mutex m;
    std::condition_variable cv;
    bool sair = false, temTrabalho = false;
    std::vector<float> trabalho;
    int trabalhoHora = 0;
    std::vector<Mensagem> novas;
    std::thread th;

    Impl(double taxa, bool f4) : fs(taxa), ft4(f4)
    {
        simbolo = ft4 ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
        periodo = ft4 ? FT4_SLOT_TIME : FT8_SLOT_TIME;
        bloco = (int)std::lround(fs * simbolo);
        sub = bloco / kTimeOsr;
        nfft = bloco * kFreqOsr;
        // Ruido num bin (janela de Hann: 1,5 bin de largura equivalente) ->
        // referencia de 2500 Hz. +1,8 dB (FT4: +1,6) e a perda do quadro que
        // pega metade de outro simbolo: com sinais sinteticos em ruido branco
        // o SNR medido bate com o real (erro medio 0,1 dB, de -18 a +20 dB).
        // -3 dB: o WSJT-X conta no ruido os sinais fracos misturados nele e
        // da uns 2 a 7 dB a menos que o real numa banda movimentada (medido
        // nas gravacoes de teste do ft8_lib); assim fica perto dele.
        ajusteSnr = (float)(10.0 * std::log10(1.5 * fs / nfft / 2500.0)) + (ft4 ? 1.6f : 1.8f) - 3.0f;

        janela.resize(nfft);
        const float norma = 2.0f / nfft;
        for (int i = 0; i < nfft; ++i) {
            const double s = std::sin(kPi * i / nfft);
            janela[i] = (float)(norma * s * s);
        }
        quadro.assign(nfft, 0.f);
        tempo.resize(nfft);
        freq.resize(nfft / 2 + 1);
        size_t tam = 0;
        kiss_fftr_alloc(nfft, 0, nullptr, &tam);
        fftMem.resize(tam);
        fftCfg = kiss_fftr_alloc(nfft, 0, fftMem.data(), &tam);

        maxBlocos = (int)(periodo / simbolo);
        minBin = (int)(kFMin * simbolo);
        maxBin = (int)(kFMax * simbolo) + 1;
        const int nBins = maxBin - minBin;
        mag.resize((size_t)maxBlocos * kTimeOsr * kFreqOsr * nBins);
        wf.max_blocks = maxBlocos;
        wf.num_blocks = 0;
        wf.num_bins = nBins;
        wf.time_osr = kTimeOsr;
        wf.freq_osr = kFreqOsr;
        wf.block_stride = kTimeOsr * kFreqOsr * nBins;
        wf.mag = mag.data();
        wf.protocol = ft4 ? FTX_PROTOCOL_FT4 : FTX_PROTOCOL_FT8;
        precisa = (size_t)maxBlocos * bloco;

        th = std::thread([this] { laco(); });
    }

    ~Impl()
    {
        { std::lock_guard<std::mutex> lk(m); sair = true; }
        cv.notify_all();
        if (th.joinable()) th.join();
    }

    // Um bloco (1 simbolo) de audio -> kTimeOsr linhas da cachoeira
    void analisarBloco(const float* x)
    {
        if (wf.num_blocks >= wf.max_blocks) return;
        size_t pos = (size_t)wf.num_blocks * wf.block_stride;
        int lido = 0;
        for (int ts = 0; ts < kTimeOsr; ++ts) {
            std::memmove(quadro.data(), quadro.data() + sub, (nfft - sub) * sizeof(float));
            std::memcpy(quadro.data() + (nfft - sub), x + lido, sub * sizeof(float));
            lido += sub;
            for (int i = 0; i < nfft; ++i) tempo[i] = janela[i] * quadro[i];
            kiss_fftr(fftCfg, tempo.data(), freq.data());
            for (int fsub = 0; fsub < kFreqOsr; ++fsub) {
                for (int b = minBin; b < maxBin; ++b) {
                    const int src = b * kFreqOsr + fsub;
                    const float p = freq[src].r * freq[src].r + freq[src].i * freq[src].i;
                    const float db = 10.0f * std::log10(1e-12f + p);
                    const int v = (int)(2 * db + 240);          // 0..255 = -120..+7,5 dB, passo de 0,5 dB
                    mag[pos++] = (WF_ELEM_T)std::clamp(v, 0, 255);
                }
            }
        }
        ++wf.num_blocks;
    }

    // SNR em 2500 Hz, na escala do WSJT-X. Sinal: a potencia no tom de cada
    // simbolo. Ruido: o "chao" da banda em volta (+-400 Hz, o periodo todo),
    // pelo percentil de 20% - os outros tons do proprio sinal nao servem: o
    // simbolo ocupa so meia janela da FFT e vaza para os vizinhos.
    int calcularSnr(const ftx_candidate_t& c, const ftx_message_t& msg)
    {
        uint8_t tons[FT4_NN > FT8_NN ? FT4_NN : FT8_NN];
        int nn;
        if (ft4) { ft4_encode(msg.payload, tons); nn = FT4_NN; }
        else     { ft8_encode(msg.payload, tons); nn = FT8_NN; }
        double sinal = 0;
        int nSinal = 0;
        for (int k = 0; k < nn; ++k) {
            if (ft4 && (k == 0 || k == nn - 1)) continue;      // rampas
            const int b = c.time_offset + k;
            if (b < 0 || b >= wf.num_blocks) continue;
            const size_t i0 = ((((size_t)b * kTimeOsr + c.time_sub) * kFreqOsr + c.freq_sub) * wf.num_bins) + c.freq_offset;
            const double v = std::pow(10.f, WF_ELEM_MAG(mag[i0 + tons[k]]) / 10.f);
            sinal += v;
            ++nSinal;
        }
        if (!nSinal) return -24;
        const int viz = (int)std::lround(400.0 * simbolo);
        const int b0 = std::max(0, c.freq_offset - viz), b1 = std::min(wf.num_bins, c.freq_offset + viz);
        // percentil de 20% em cada instante, depois a media no tempo: com o
        // AGC do receptor o chao sobe e desce junto com os sinais fortes, e
        // um percentil do periodo todo pegaria so os momentos quietos.
        std::vector<uint8_t> lin;
        lin.reserve((size_t)(b1 - b0));
        double soma = 0;
        int nb = 0;
        for (int b = 0; b < wf.num_blocks; ++b) {
            const uint8_t* l = &mag[(size_t)b * wf.block_stride];     // time_sub 0, freq_sub 0
            lin.assign(l + b0, l + b1);
            if (lin.size() < 8) continue;
            auto q = lin.begin() + lin.size() / 5;
            std::nth_element(lin.begin(), q, lin.end());
            soma += std::pow(10.0, WF_ELEM_MAG(*q) / 10.0) / 0.2231;      // percentil 20% -> media (ruido exponencial)
            ++nb;
        }
        if (!nb) return -24;
        const double pr = std::max(1e-20, soma / nb);
        const double ps = sinal / nSinal;
        const double rel = std::max(1e-3, (ps - pr) / pr);
        const double db = 10.0 * std::log10(rel) + ajusteSnr;
        return (int)std::lround(std::clamp(db, -30.0, 40.0));
    }

    std::vector<Mensagem> decodificar(const float* x, size_t n, int hora)
    {
        std::vector<Mensagem> saida;
        std::lock_guard<std::mutex> lt(g_tabelaMutex);
        wf.num_blocks = 0;
        std::fill(quadro.begin(), quadro.end(), 0.f);
        double energia = 0;
        for (size_t i = 0; i < n; ++i) energia += (double)x[i] * x[i];
        nivelDb = n ? 10.0 * std::log10(energia / n + 1e-20) : -120.0;
        for (size_t pos = 0; pos + bloco <= n && wf.num_blocks < wf.max_blocks; pos += bloco) analisarBloco(x + pos);

        std::vector<ftx_candidate_t> cand(kMaxCandidatos);
        const int nc = ftx_find_candidates(&wf, kMaxCandidatos, cand.data(), kScoreMin);
        std::vector<std::vector<uint8_t>> vistos;
        for (int i = 0; i < nc; ++i) {
            const ftx_candidate_t& c = cand[i];
            ftx_message_t msg;
            ftx_decode_status_t st;
            if (!ftx_decode_candidate(&wf, &c, kIteracoesLdpc, &msg, &st)) continue;
            std::vector<uint8_t> chave(msg.payload, msg.payload + FTX_PAYLOAD_LENGTH_BYTES);
            if (std::find(vistos.begin(), vistos.end(), chave) != vistos.end()) continue;   // a mesma, achada 2x
            vistos.push_back(chave);
            char texto[FTX_MAX_MESSAGE_LENGTH + 8];
            ftx_message_offsets_t ofs;
            if (ftx_message_decode(&msg, &g_hashIf, texto, &ofs) != FTX_MESSAGE_RC_OK) continue;
            Mensagem mm;
            mm.hora = hora;
            mm.hz = (float)((minBin + c.freq_offset + (float)c.freq_sub / kFreqOsr) / simbolo);
            const double t = (c.time_offset + (double)c.time_sub / kTimeOsr) * simbolo;
            mm.dt = (float)(t - simbolo - kInicioNominal);
            mm.snr = calcularSnr(c, msg);
            mm.texto = texto;
            saida.push_back(std::move(mm));
        }
        tabelaEnvelhecer(10);
        std::sort(saida.begin(), saida.end(), [](const Mensagem& a, const Mensagem& b) { return a.hz < b.hz; });
        return saida;
    }

    void laco()
    {
        for (;;) {
            std::vector<float> x;
            int hora;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] { return sair || temTrabalho; });
                if (sair) return;
                x.swap(trabalho);
                hora = trabalhoHora;
                temTrabalho = false;
                ocupado = true;
            }
            std::vector<Mensagem> r = decodificar(x.data(), x.size(), hora);
            {
                std::lock_guard<std::mutex> lk(m);
                ultimo = (int)r.size();
                total += (int)r.size();
                for (auto& mm : r) novas.push_back(std::move(mm));
                if (novas.size() > 2000) novas.erase(novas.begin(), novas.begin() + (novas.size() - 2000));
                ocupado = false;
            }
        }
    }

    void feed(const float* x, size_t n, double utcFim)
    {
        if (!n) return;
        const double fimEst = t0 + (double)(cont + (long long)n - 1) / fs;
        const double erro = utcFim - fimEst;
        if (!temRelogio || std::fabs(erro) > 0.5) {
            // primeira vez, ou o audio pulou (fila cheia, troca de aparelho):
            // acerta o relogio e recomeca o periodo
            t0 = utcFim - (double)(cont + (long long)n - 1) / fs;
            temRelogio = true;
            periodoAtual = LLONG_MIN;
        } else {
            t0 += 0.02 * erro;    // segue o relogio do PC devagar (o bloco chega com atraso variavel)
        }
        for (size_t i = 0; i < n; ++i) {
            const double t = t0 + (double)(cont + (long long)i) / fs;
            const long long k = (long long)std::floor(t / periodo);
            if (k != periodoAtual) {
                periodoAtual = k;
                const double dentro = t - (double)k * periodo;
                valido = dentro < kEntradaMax;
                mandado = false;
                buf.clear();
                buf.reserve(precisa);
                if (valido) buf.assign((size_t)std::lround(dentro * fs), 0.f);
                esperando = !valido;
            }
            if (valido && !mandado) {
                buf.push_back(x[i]);
                if (buf.size() >= precisa) {
                    mandado = true;
                    std::lock_guard<std::mutex> lk(m);
                    trabalho.swap(buf);
                    trabalhoHora = horaHHMMSS((double)periodoAtual * periodo);
                    temTrabalho = true;
                    cv.notify_one();
                }
            }
        }
        cont += (long long)n;
        const double tFim = t0 + (double)(cont - 1) / fs;
        noPeriodo = tFim - std::floor(tFim / periodo) * periodo;
    }
};

Ft8Core::Ft8Core(double taxa, bool ft4) : d_(std::make_unique<Impl>(taxa, ft4))
{
    std::lock_guard<std::mutex> lt(g_tabelaMutex);
    tabelaLimpar();
}

Ft8Core::~Ft8Core() = default;

void Ft8Core::feed(const float* x, size_t n, double utcFim) { d_->feed(x, n, utcFim); }

std::vector<Ft8Core::Mensagem> Ft8Core::decodificarAgora(const float* x, size_t n, int hora)
{
    return d_->decodificar(x, std::min(n, d_->precisa), hora);
}

std::vector<Ft8Core::Mensagem> Ft8Core::pegar()
{
    std::lock_guard<std::mutex> lk(d_->m);
    std::vector<Mensagem> r;
    r.swap(d_->novas);
    return r;
}

bool   Ft8Core::ft4() const { return d_->ft4; }
double Ft8Core::periodo() const { return d_->periodo; }
double Ft8Core::segundoNoPeriodo() const { return d_->noPeriodo.load(); }
bool   Ft8Core::esperandoInicio() const { return d_->esperando.load(); }
bool   Ft8Core::decodificando() const { return d_->ocupado.load(); }
int    Ft8Core::noUltimoPeriodo() const { return d_->ultimo.load(); }
int    Ft8Core::total() const { return d_->total.load(); }
double Ft8Core::ultimoNivelDb() const { return d_->nivelDb.load(); }

} // namespace masdr
