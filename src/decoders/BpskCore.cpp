// ---------------------------------------------------------------------------
//  BpskCore.cpp - nucleo de decodificacao BPSK/QPSK (ver BpskCore.h)
// ---------------------------------------------------------------------------

#include "BpskCore.h"

#include <cmath>
#include <cstring>

namespace masdr {

static const double kPi = 3.14159265358979323846;
static const double kDoisPi = 6.28318530717958647692;

// ---------------------------------------------------------------------------
//  Tabelas varicode (codigos publicados das normas; primeiro bit = MSB).
//  O trecho "##TABELAS##" recebe as duas tabelas de 256 codigos (geradas
//  um?nica vez no desenvolvimento; ficam aqui para o build offline).
// ---------------------------------------------------------------------------
const TabelaVaricode kVaricodePsk = {
    {
        0x02ab,
        0x02db,
        0x02ed,
        0x0377,
        0x02eb,
        0x035f,
        0x02ef,
        0x02fd,
        0x02ff,
        0x00ef,
        0x001d,
        0x036f,
        0x02dd,
        0x001f,
        0x0375,
        0x03ab,
        0x02f7,
        0x02f5,
        0x03ad,
        0x03af,
        0x035b,
        0x036b,
        0x036d,
        0x0357,
        0x037b,
        0x037d,
        0x03b7,
        0x0355,
        0x035d,
        0x03bb,
        0x02fb,
        0x037f,
        0x0001,
        0x01ff,
        0x015f,
        0x01f5,
        0x01db,
        0x02d5,
        0x02bb,
        0x017f,
        0x00fb,
        0x00f7,
        0x016f,
        0x01df,
        0x0075,
        0x0035,
        0x0057,
        0x01af,
        0x00b7,
        0x00bd,
        0x00ed,
        0x00ff,
        0x0177,
        0x015b,
        0x016b,
        0x01ad,
        0x01ab,
        0x01b7,
        0x00f5,
        0x01bd,
        0x01ed,
        0x0055,
        0x01d7,
        0x02af,
        0x02bd,
        0x007d,
        0x00eb,
        0x00ad,
        0x00b5,
        0x0077,
        0x00db,
        0x00fd,
        0x0155,
        0x007f,
        0x01fd,
        0x017d,
        0x00d7,
        0x00bb,
        0x00dd,
        0x00ab,
        0x00d5,
        0x01dd,
        0x00af,
        0x006f,
        0x006d,
        0x0157,
        0x01b5,
        0x015d,
        0x0175,
        0x017b,
        0x02ad,
        0x01f7,
        0x01ef,
        0x01fb,
        0x02bf,
        0x016d,
        0x02df,
        0x000b,
        0x005f,
        0x002f,
        0x002d,
        0x0003,
        0x003d,
        0x005b,
        0x002b,
        0x000d,
        0x01eb,
        0x00bf,
        0x001b,
        0x003b,
        0x000f,
        0x0007,
        0x003f,
        0x01bf,
        0x0015,
        0x0017,
        0x0005,
        0x0037,
        0x007b,
        0x006b,
        0x00df,
        0x005d,
        0x01d5,
        0x02b7,
        0x01bb,
        0x02b5,
        0x02d7,
        0x03b5,
        0x03bd,
        0x03bf,
        0x03d5,
        0x03d7,
        0x03db,
        0x03dd,
        0x03df,
        0x03eb,
        0x03ed,
        0x03ef,
        0x03f5,
        0x03f7,
        0x03fb,
        0x03fd,
        0x03ff,
        0x0555,
        0x0557,
        0x055b,
        0x055d,
        0x055f,
        0x056b,
        0x056d,
        0x056f,
        0x0575,
        0x0577,
        0x057b,
        0x057d,
        0x057f,
        0x05ab,
        0x05ad,
        0x05af,
        0x05b5,
        0x05b7,
        0x05bb,
        0x05bd,
        0x05bf,
        0x05d5,
        0x05d7,
        0x05db,
        0x05dd,
        0x05df,
        0x05eb,
        0x05ed,
        0x05ef,
        0x05f5,
        0x05f7,
        0x05fb,
        0x05fd,
        0x05ff,
        0x06ab,
        0x06ad,
        0x06af,
        0x06b5,
        0x06b7,
        0x06bb,
        0x06bd,
        0x06bf,
        0x06d5,
        0x06d7,
        0x06db,
        0x06dd,
        0x06df,
        0x06eb,
        0x06ed,
        0x06ef,
        0x06f5,
        0x06f7,
        0x06fb,
        0x06fd,
        0x06ff,
        0x0755,
        0x0757,
        0x075b,
        0x075d,
        0x075f,
        0x076b,
        0x076d,
        0x076f,
        0x0775,
        0x0777,
        0x077b,
        0x077d,
        0x077f,
        0x07ab,
        0x07ad,
        0x07af,
        0x07b5,
        0x07b7,
        0x07bb,
        0x07bd,
        0x07bf,
        0x07d5,
        0x07d7,
        0x07db,
        0x07dd,
        0x07df,
        0x07eb,
        0x07ed,
        0x07ef,
        0x07f5,
        0x07f7,
        0x07fb,
        0x07fd,
        0x07ff,
        0x0aab,
        0x0aad,
        0x0aaf,
        0x0ab5,
        0x0ab7,
        0x0abb,
        0x0abd,
        0x0abf,
        0x0ad5,
        0x0ad7,
        0x0adb,
        0x0add,
        0x0adf,
        0x0aeb,
        0x0aed,
        0x0aef,
        0x0af5,
        0x0af7,
        0x0afb,
        0x0afd,
        0x0aff,
        0x0b55,
        0x0b57,
        0x0b5b,
    }
};
const TabelaVaricode kVaricodeMfsk = {
    {
        0x075c,
        0x0760,
        0x0768,
        0x076c,
        0x0770,
        0x0774,
        0x0778,
        0x077c,
        0x00a8,
        0x0780,
        0x07a0,
        0x07a8,
        0x07ac,
        0x00ac,
        0x07b0,
        0x07b4,
        0x07b8,
        0x07bc,
        0x07c0,
        0x07d0,
        0x07d4,
        0x07d8,
        0x07dc,
        0x07e0,
        0x07e8,
        0x07ec,
        0x07f0,
        0x07f4,
        0x07f8,
        0x07fc,
        0x0800,
        0x0a00,
        0x0004,
        0x01c0,
        0x01fc,
        0x02d8,
        0x02a8,
        0x02a0,
        0x0200,
        0x01bc,
        0x01f4,
        0x01f0,
        0x02b4,
        0x01e0,
        0x00a0,
        0x01d8,
        0x01d4,
        0x01e8,
        0x00e0,
        0x00f0,
        0x0140,
        0x0154,
        0x0174,
        0x0160,
        0x016c,
        0x01a0,
        0x0180,
        0x01ac,
        0x01ec,
        0x01f8,
        0x02c0,
        0x01dc,
        0x02bc,
        0x01d0,
        0x0280,
        0x00bc,
        0x0100,
        0x00d4,
        0x00dc,
        0x00b8,
        0x00f8,
        0x0150,
        0x0158,
        0x00c0,
        0x01b4,
        0x017c,
        0x00f4,
        0x00e8,
        0x00fc,
        0x00d0,
        0x00ec,
        0x01b0,
        0x00d8,
        0x00b4,
        0x00b0,
        0x015c,
        0x01a8,
        0x0168,
        0x0170,
        0x0178,
        0x01b8,
        0x02e8,
        0x02d0,
        0x02ec,
        0x02d4,
        0x02b0,
        0x02ac,
        0x0014,
        0x0060,
        0x0038,
        0x0034,
        0x0008,
        0x0050,
        0x0058,
        0x0030,
        0x0018,
        0x0080,
        0x0070,
        0x002c,
        0x0040,
        0x001c,
        0x0010,
        0x0054,
        0x0078,
        0x0020,
        0x0028,
        0x000c,
        0x003c,
        0x006c,
        0x0068,
        0x0074,
        0x005c,
        0x007c,
        0x02dc,
        0x02b8,
        0x02e0,
        0x02f0,
        0x0a80,
        0x0aa0,
        0x0aa8,
        0x0aac,
        0x0ab0,
        0x0ab4,
        0x0ab8,
        0x0abc,
        0x0ac0,
        0x0ad0,
        0x0ad4,
        0x0ad8,
        0x0adc,
        0x0ae0,
        0x0ae8,
        0x0aec,
        0x0af0,
        0x0af4,
        0x0af8,
        0x0afc,
        0x0b00,
        0x0b40,
        0x0b50,
        0x0b54,
        0x0b58,
        0x0b5c,
        0x0b60,
        0x0b68,
        0x0b6c,
        0x0b70,
        0x0b74,
        0x0b78,
        0x0b7c,
        0x02f4,
        0x02f8,
        0x02fc,
        0x0300,
        0x0340,
        0x0350,
        0x0354,
        0x0358,
        0x035c,
        0x0360,
        0x0368,
        0x036c,
        0x0370,
        0x0374,
        0x0378,
        0x037c,
        0x0380,
        0x03a0,
        0x03a8,
        0x03ac,
        0x03b0,
        0x03b4,
        0x03b8,
        0x03bc,
        0x03c0,
        0x03d0,
        0x03d4,
        0x03d8,
        0x03dc,
        0x03e0,
        0x03e8,
        0x03ec,
        0x03f0,
        0x03f4,
        0x03f8,
        0x03fc,
        0x0400,
        0x0500,
        0x0540,
        0x0550,
        0x0554,
        0x0558,
        0x055c,
        0x0560,
        0x0568,
        0x056c,
        0x0570,
        0x0574,
        0x0578,
        0x057c,
        0x0580,
        0x05a0,
        0x05a8,
        0x05ac,
        0x05b0,
        0x05b4,
        0x05b8,
        0x05bc,
        0x05c0,
        0x05d0,
        0x05d4,
        0x05d8,
        0x05dc,
        0x05e0,
        0x05e8,
        0x05ec,
        0x05f0,
        0x05f4,
        0x05f8,
        0x05fc,
        0x0600,
        0x0680,
        0x06a0,
        0x06a8,
        0x06ac,
        0x06b0,
        0x06b4,
        0x06b8,
        0x06bc,
        0x06c0,
        0x06d0,
        0x06d4,
        0x06d8,
        0x06dc,
        0x06e0,
        0x06e8,
        0x06ec,
        0x06f0,
        0x06f4,
        0x06f8,
        0x06fc,
        0x0700,
        0x0740,
        0x0750,
        0x0754,
        0x0758,
    }
};

// ---------------------------------------------------------------------------
//  Viterbi
// ---------------------------------------------------------------------------
ViterbiC::ViterbiC(int k, int poly1, int poly2)
    : k_(0), p1_(0), p2_(0)
{
    for (int i = 0; i < kCamadas; ++i) {
        met_[i] = nullptr;
        hist_[i] = nullptr;
    }
    configura(k, poly1, poly2);
}

ViterbiC::~ViterbiC()
{
    for (int i = 0; i < kCamadas; ++i) {
        delete[] met_[i];
        delete[] hist_[i];
    }
}

void ViterbiC::configura(int k, int poly1, int poly2)
{
    const bool igual = (k_ == k && p1_ == poly1 && p2_ == poly2);
    if (!igual) {
        const int nEstNovo = 1 << (k - 1);
        for (int i = 0; i < kCamadas; ++i) {
            delete[] met_[i]; delete[] hist_[i];
            met_[i] = new int[nEstNovo > 0 ? nEstNovo : 1];
            hist_[i] = new int[nEstNovo > 0 ? nEstNovo : 1];
        }
        k_ = k; p1_ = poly1; p2_ = poly2;
        montaTabelas();
    }
    reset();
}

void ViterbiC::montaTabelas()
{
    const int nEst = 1 << (k_ - 1);
    (void)nEst;
    for (int i = 0; i < (1 << k_); ++i)
        saida_[i] = EncoderC::paridade(p1_ & i) | (EncoderC::paridade(p2_ & i) << 1);
    for (int i = 0; i < 256; ++i) {
        metTab_[0][i] = (short)(128 - i);
        metTab_[1][i] = (short)(i - 128);
    }
}

void ViterbiC::reset()
{
    const int nEst = 1 << (k_ - 1);
    for (int i = 0; i < kCamadas; ++i) {
        std::memset(met_[i], 0, sizeof(int) * (size_t)nEst);
        std::memset(hist_[i], 0, sizeof(int) * (size_t)nEst);
    }
    ptr_ = 0;
}

int ViterbiC::decode(const unsigned char* sym, int* metOut)
{
    const int nEst = 1 << (k_ - 1);
    const int ant = (ptr_ - 1 + kCamadas) % kCamadas;
    const int met[4] = {
        metTab_[0][sym[1]] + metTab_[0][sym[0]],
        metTab_[0][sym[1]] + metTab_[1][sym[0]],
        metTab_[1][sym[1]] + metTab_[0][sym[0]],
        metTab_[1][sym[1]] + metTab_[1][sym[0]]
    };

    for (int n = 0; n < nEst; ++n) {
        const int s0 = n, s1 = n + nEst;
        const int p0 = s0 >> 1, p1 = s1 >> 1;
        const int m0 = met_[ant][p0] + met[saida_[s0]];
        const int m1 = met_[ant][p1] + met[saida_[s1]];
        if (m0 > m1) {
            met_[ptr_][n] = m0;
            hist_[ptr_][n] = p0;
        } else {
            met_[ptr_][n] = m1;
            hist_[ptr_][n] = p1;
        }
    }
    ptr_ = (ptr_ + 1) % kCamadas;

    if (ptr_ % chunk_ == 0) {
        int p = (ptr_ - 1 + kCamadas) % kCamadas;
        int melhor = met_[p][0], bestState = 0;
        for (int i = 0; i < nEst; ++i) {
            if (met_[p][i] > melhor) { melhor = met_[p][i]; bestState = i; }
        }
        int seq[kCamadas] = {};
        seq[p] = bestState;
        for (int i = 0; i < 12 * k_; ++i) {          // volta o tubo inteiro
            const int ant2 = (p - 1 + kCamadas) % kCamadas;
            seq[ant2] = hist_[p][seq[p]];
            p = ant2;
        }
        // metrica = custo do lote (diferenca entre o fim e o comeco dele)
        if (metOut) *metOut = met_[p][seq[p]];
        int c = 0;
        int p2 = p;
        for (int i = 0; i < chunk_; ++i) {
            c = (c << 1) | (seq[p2] & 1);
            p2 = (p2 + 1) % kCamadas;
        }
        if (metOut) *metOut = met_[p2][seq[p2]] - *metOut;
        return c;
    }
    // renormalizacao como na fonte publica: evita acumular fascas de int
    if (met_[ptr_][0] > 1073741823) {
        for (int i = 0; i < kCamadas; ++i)
            for (int j = 0; j < nEst; ++j) met_[i][j] -= 1073741823;
    } else if (met_[ptr_][0] < -1073741824) {
        for (int i = 0; i < kCamadas; ++i)
            for (int j = 0; j < nEst; ++j) met_[i][j] += 1073741823;
    }
    return -1;
}

// ---------------------------------------------------------------------------
//  Configuracao dos modos (L, dcdbits, filtro, qpsk, fec, k, poly1, poly2,
//                           interlace, varicode MFSK)
// ---------------------------------------------------------------------------
const BpskCore::ModoP BpskCore::kModos[9] = {
    { 256,  32, 0, false, false, 0,      0x00, 0x00, false, false },  // 0 BPSK31
    { 256,  32, 0, true,  false, 5,      0x17, 0x19, false, false },  // 1 QPSK31
    { 128,  64, 0, false, false, 0,      0x00, 0x00, false, false },  // 2 BPSK63
    { 128,  64, 0, true,  false, 5,      0x17, 0x19, false, false },  // 3 QPSK63
    { 128,  64, 0, false, true,  7,      0x6d, 0x4f, false, true  },  // 4 BPSK63F (sem entrelacador)
    {  64, 128, 1, false, false, 0,      0x00, 0x00, false, false },  // 5 BPSK125
    {  64, 128, 1, true,  false, 5,      0x17, 0x19, false, false },  // 6 QPSK125
    {  32, 256, 1, false, false, 0,      0x00, 0x00, false, false },  // 7 BPSK250
    {  64, 128, 1, false, true,  7,      0x6d, 0x4f, true,  true  },  // 8 BPSK125F (PSK125R)
};

int BpskCore::nModos() { return 9; }

const char* BpskCore::nomeModo(int i)
{
    static const char* kNom[9] = {
        "BPSK31", "QPSK31", "BPSK63", "QPSK63", "BPSK63F",
        "BPSK125", "QPSK125", "BPSK250", "BPSK125F"
    };
    return (i >= 0 && i < 9) ? kNom[i] : "?";
}

BpskCore::BpskCore()
    : vit0_(7, 0x6d, 0x4f), vit1_(7, 0x6d, 0x4f)
{
    setModo(0);
}

void BpskCore::setModo(int indice)
{
    std::lock_guard<std::mutex> lk(mtx_);
    indice = indice >= 0 && indice < nModos() ? indice : 0;
    modo_ = indice;
    m_ = &kModos[indice];

    const int L = m_->L;
    M_ = L > 15 ? L / 16 : 1;
    for (int i = 0; i < 16; ++i) syncbuf_[i] = 0.0;
    bitclk_ = 0;
    faseNco_ = 0.0;
    freq_ = centro_;

    // fir1: filtro casado da norma
    if (m_->filtro == 0) {
        // cosseno levantado (janela (1-cos)/128)
        nf1_ = 65;
        for (int i = 0; i < nf1_; ++i)
            f1_[i] = (1.0 - std::cos(kDoisPi * (double)i / 64.0)) / 128.0;
        // pskcore: filtro de 65 taps da fonte de dados da norma
        nf2_ = 65;
        static const double kCore[65] = {
            4.3453566e-005, -0.00049122414, -0.00078771292, -0.0013507826,
            -0.0021287814,  -0.003133466,  -0.004366817,  -0.0058112187,
            -0.0074249976,  -0.0091398882, -0.010860157,  -0.012464086,
            -0.013807772,   -0.014731191,  -0.015067057,  -0.014650894,
            -0.013333425,   -0.01099166,   -0.0075431246, -0.0029527849,
             0.0027546292,   0.0094932775,  0.017113308,   0.025403511,
             0.034099681,    0.042895839,   0.051458575,   0.059444853,
             0.066521003,    0.072381617,   0.076767694,   0.079481619,
             0.080420311,    0.079481619,   0.076767694,   0.072381617,
             0.066521003,    0.059444853,   0.051458575,   0.042895839,
             0.034099681,    0.025403511,   0.017113308,   0.0094932775,
             0.0027546292,  -0.0029527849, -0.0075431246, -0.01099166,
            -0.013333425,   -0.014650894,  -0.015067057,  -0.014731191,
            -0.013807772,   -0.012464086,  -0.010860157,  -0.0091398882,
            -0.0074249976,  -0.0058112187, -0.004366817,  -0.003133466,
            -0.0021287814,  -0.0013507826, -0.00078771292,-0.00049122414,
             4.3453566e-005
        };
        static_assert(sizeof(kCore) / sizeof(kCore[0]) == 65, "pskcore precisa de 65 taps");
        for (int i = 0; i < 65; ++i) f2_[i] = kCore[i];
    } else {
        // sinc com janela Blackman: fc = 1/L no fir1 e 1/16 no fir2 (da fonte)
        auto wsinc = [](double* out, double fc, int len) {
            double norm = 0;
            const double k1 = kDoisPi / len;
            const double k2 = kDoisPi * fc;
            const double l2 = len / 2.0;
            for (int i = 0; i <= len; ++i)
                out[i] = (i == (int)l2) ? 1.0 : std::sin(k2 * (i - l2)) / (k2 * (i - l2));
            for (int i = 0; i <= len; ++i)
                out[i] *= (0.42 - 0.5 * std::cos(k1 * i) + 0.08 * std::cos(2.0 * k1 * i));
            for (int i = 0; i <= len; ++i) norm += out[i];
            for (int i = 0; i <= len; ++i) out[i] /= norm;
        };
        nf1_ = 65; wsinc(f1_, 1.0 / L, 64);
        nf2_ = 65; wsinc(f2_, 1.0 / 16.0, 64);
    }

    delete ent0_; delete ent1_;
    ent0_ = m_->inter ? new EntrelacadorC(2, 40, EntrelacadorC::REV) : nullptr;
    ent1_ = m_->inter ? new EntrelacadorC(2, 40, EntrelacadorC::REV) : nullptr;
    const int kVit = m_->fec ? m_->k : 5;
    vit0_.configura(kVit, m_->fec ? m_->poly1 : 0x17, m_->fec ? m_->poly2 : 0x19);
    vit1_.configura(kVit, m_->fec ? m_->poly1 : 0x17, m_->fec ? m_->poly2 : 0x19);
    if (m_->fec) { vit0_.setChunk(4); vit1_.setChunk(4); }
    else { vit0_.setChunk(8); vit1_.setChunk(8); }

    // estado dos filtros limpo a cada troca de modo
    for (int i = 0; i < nf1_; ++i) buf1_[i] = { 0.0, 0.0 };
    for (int i = 0; i < nf2_; ++i) buf2_[i] = { 0.0, 0.0 };
    b1i_ = 0; b1f_ = 0; b2i_ = 0;

    tab0_ = m_->varMfsk ? &kVaricodeMfsk : &kVaricodePsk;

    reiniciarRx();
}

void BpskCore::reiniciarRx()
{
    simboloAnt_ = { 1.0, 0.0 };
    nivelMedio_ = 0;
    qRe_ = qIm_ = 0;
    metrica_ = afcMetrica_ = 0;
    dcd_ = false;
    dcdshreg_ = 0;
    dcdOff_ = 0;
    shreg_ = shreg2_ = 0;
    shregM_ = shreg2M_ = 1;   // o '1' semente e o bit inicial do caractere
                              // corrente: o disparo do proximo codigo completa
                              // o anterior (convencao fldigi dos modos FEC)
    parAnt_ = 128;
    vitPasso_ = false;
    fecMet0_ = fecMet1_ = 0;
    if (ent0_) ent0_->flush();
    if (ent1_) ent1_->flush();
}

// ---------------------------------------------------------------------------
//  alimentar: 8 kHz real -> NCO -> fir1 (decimada) -> fir2 -> simbolos
// ---------------------------------------------------------------------------
void BpskCore::alimentar(const float* x, size_t n)
{
    std::lock_guard<std::mutex> lk(mtx_);

    // buffers locais da decimacao (a medida do filtro corrente)
    for (size_t i = 0; i < n; ++i) {
        // NCO: traz o sinal (em ~1800 Hz) para baseband
        const double cs = std::cos(faseNco_), sn = std::sin(faseNco_);
        const std::complex<double> z(-x[i] * cs, x[i] * sn);
        faseNco_ += kDoisPi * freq_ / fs_;
        if (faseNco_ > kDoisPi) faseNco_ -= kDoisPi;

        // fir1: janela de nf1_ amostras CONSECUTIVAS (como na fonte publica,
        // o mac roda sobre o fim do buffer) e saida a cada M_ amostras
        buf1_[b1i_ % (unsigned)nf1_] = z;
        b1i_ = (b1i_ + 1) % (unsigned)nf1_;
        if (++b1f_ < M_) continue;
        b1f_ = 0;

        std::complex<double> z1(0.0, 0.0);
        for (int t = 0; t < nf1_; ++t)
            z1 += f1_[t] * buf1_[(unsigned)((b1i_ + t) % (unsigned)nf1_)];

        // fir2: roda em cada amostra decimada
        buf2_[b2i_] = z1;
        b2i_ = (b2i_ + 1) % (unsigned)nf2_;
        std::complex<double> z2(0.0, 0.0);
        for (int t = 0; t < nf2_; ++t)
            z2 += f2_[t] * buf2_[(unsigned)((int)b2i_ - 1 - t + nf2_) % (unsigned)nf2_];

        // relogio de simbolo: desenho do envelope no syncbuf
        const int idx = (int)bitclk_;
        syncbuf_[idx] = 0.8 * syncbuf_[idx] + 0.2 * std::abs(z2);

        double dif = 0.0, amp = 0.0;
        for (int k = 0; k < 8; ++k) {
            dif += (syncbuf_[k] - syncbuf_[k + 8]);
            amp += (syncbuf_[k] + syncbuf_[k + 8]);
        }
        if (amp > 0) dif /= amp;
        bitclk_ -= dif / 5.0;
        bitclk_ += 1.0;
        if (bitclk_ < 0) bitclk_ += 16.0;
        if (bitclk_ >= 16.0) {
            bitclk_ -= 16.0;
            ++simbolos_;
            ajustaAfc(z2);
            amostraSimbolo(z2);
        }
    }
}

// ---------------------------------------------------------------------------
//  AFC de fase igual ao da fonte: erro entre a decisao "dura" e a fase,
//  limitado a +/- 90 graus, aplicado devagar.
// ---------------------------------------------------------------------------
void BpskCore::ajustaAfc(const std::complex<double>& z2)
{
    const double fase = std::arg(z2);
    double err = fase - std::round(fase / (kPi / 2.0)) * (kPi / 2.0);
    if (err < -kPi / 2.0 || err > kPi / 2.0) return;
    if (afcMetrica_ < 0.05) return;
    const double freqErr = err * fs_ / (kDoisPi * m_->L) / m_->dcdbits;
    freq_ -= freqErr;
    if (freq_ < centro_ - 200) freq_ = centro_ - 200;
    if (freq_ > centro_ + 200) freq_ = centro_ + 200;
}

// ---------------------------------------------------------------------------
//  Decisao do simbolo: fase diferencial -> bits/soft bits -> FEC/varicode
// ---------------------------------------------------------------------------
void BpskCore::amostraSimbolo(const std::complex<double>& z2)
{
    double fase = std::arg(std::conj(simboloAnt_) * z2);
    simboloAnt_ = z2;
    if (fase < 0) fase += kDoisPi;

    unsigned bits;
    int n;
    if (m_->qpsk) {
        n = 4;
        bits = (unsigned)(((int)(fase / (kPi / 2.0) + 0.5)) & 3);
    } else {
        n = 2;
        bits = (unsigned)((((int)(fase / kPi + 0.5)) & 1) << 1);
    }

    const double sigAmp = std::abs(z2);
    nivelMedio_ = decMedia(nivelMedio_, sigAmp, 50);

    // bit mole (so nos modos com FEC)
    unsigned char soft = 128;
    if (m_->fec) {
        double softAmp = 2.0;
        if (sigAmp > 0 && nivelMedio_ > 0) {
            const double r = std::sqrt(sigAmp > nivelMedio_ ? sigAmp / nivelMedio_ : nivelMedio_ / sigAmp);
            softAmp = r < 1.0 ? 1.0 : (r > 1e6 ? 1e6 : r);
        }
        double alfa = fase / kPi;
        if (alfa > 1.0) alfa = 2.0 - alfa;
        const double softAng = 127.0 - 255.0 * alfa;
        soft = (unsigned char)((softAng / (1.0 + softAmp / 2.0)) + 128.0);
    }

    // qualidade da constelacao (0..100)
    const double cval = std::cos(n * fase), sval = std::sin(n * fase);
    qRe_ = decMedia(qRe_, cval, 50);
    qIm_ = decMedia(qIm_, sval, 50);
    metrica_ = 100.0 * std::norm(std::complex<double>(qRe_, qIm_));
    if (metrica_ > 100) metrica_ = 100;
    afcMetrica_ = decMedia(afcMetrica_, std::norm(std::complex<double>(qRe_, qIm_)), 50);

    // DCD
    dcdPasso((int)bits);

    if (m_->fec) {
        rxPskr(soft);
    } else if (m_->qpsk) {
        rxQpsk((int)bits);
    } else {
        rxBit(bits ? 0 : 1);          // bit 1 = sem mudanca de fase
    }
}

// ---------------------------------------------------------------------------
//  DCD (detecta a portadora com dados)
// ---------------------------------------------------------------------------
void BpskCore::dcdPasso(int bits)
{
    dcdshreg_ = (dcdshreg_ << 2) | (unsigned)(bits & 3);

    int set = -1;
    if (dcdshreg_ == 0xAAAAAAAAu) {
        if (!m_->qpsk && !m_->fec) set = 1;
    } else if (dcdshreg_ == 0x0A0A0A0Au || dcdshreg_ == 0x28282828u ||
               dcdshreg_ == 0xA0A0A0A0u || dcdshreg_ == 0x82828282u) {
        if (m_->fec) set = 1;
    } else if (dcdshreg_ == 0x00000000u) {
        if (!m_->fec && !m_->qpsk) set = 0;
    } else {
        // estado intermedio: a metrica decide (fonte publica usa o squelch)
        if (metrica_ > 15.0) dcd_ = true;
        else dcd_ = false;
        if (--dcdOff_ < 0) dcdOff_ = 0;
        return;
    }

    if (set == 1) {
        dcdOff_ = 0;
        dcd_ = true;
        qRe_ = 1.0; qIm_ = 0.0;
    } else if (set == 0) {
        if (++dcdOff_ > 5) {
            dcdOff_ = 0;
            dcd_ = false;
            qRe_ = qIm_ = 0.0;
            shreg_ = shreg2_ = 0;
            shregM_ = shreg2M_ = 0;
        }
    }
}

// ---------------------------------------------------------------------------
//  Varicode
// ---------------------------------------------------------------------------
void BpskCore::chegouCaractere(int c)
{
    if (c < 0) return;
    // \r rende linha nova; outros controles fora (sai \t e \n)
    if (c == 13) c = 10;
    if (c < 0x20 && c != 10 && c != 9) return;
    if (c == 0) return;
    ++caracteres_;
    pendente_ += (char)c;
}

void BpskCore::chegouCaractere2(int c) { chegouCaractere(c); }

void BpskCore::rxBit(int bit)
{
    if (m_->varMfsk) {
        shregM_ = (shregM_ << 1) | (unsigned)(bit ? 1 : 0);
        if ((shregM_ & 7u) == 1u) {
            // o '1' ganho em shregM_=1 do reinicio e o primeiro bit do
            // codigo corrente; o dispalo ocorre no primeiro '1' seguinte
            // (o "001" final nunca aparece no meio de um codigo MFSK)
            const int c = tab0_->achar(shregM_ >> 1);
            // votacao no nivel do caractere (fldigi): stream 0 emite quando
            // sua media de metrica nao perde para a da stream 1
            if (fecMet0_ >= fecMet1_)
                if (c >= 0 && c != 0 && dcd_) chegouCaractere(c);
            shregM_ = 1;
        }
    } else {
        shreg_ = (shreg_ << 1) | (unsigned)(bit ? 1 : 0);
        if ((shreg_ & 3u) == 0u) {
            const int c = tab0_->achar(shreg_ >> 2);
            if (c >= 0 && c != 0 && dcd_) chegouCaractere(c);
            shreg_ = 0;
        }
    }
}

void BpskCore::rxBit2(int bit)
{
    if (m_->varMfsk) {
        shreg2M_ = (shreg2M_ << 1) | (unsigned)(bit ? 1 : 0);
        if ((shreg2M_ & 7u) == 1u) {
            const int c = tab0_->achar(shreg2M_ >> 1);
            // votacao no nivel do caractere (fldigi): stream 1 emite quando
            // ganha da stream 0
            if (fecMet0_ < fecMet1_)
                if (c >= 0 && c != 0 && dcd_) chegouCaractere2(c);
            shreg2M_ = 1;
        }
    } else {
        shreg2_ = (shreg2_ << 1) | (unsigned)(bit ? 1 : 0);
        if ((shreg2_ & 3u) == 0u) {
            const int c = tab0_->achar(shreg2_ >> 2);
            if (c >= 0 && c != 0 && dcd_) chegouCaractere2(c);
            shreg2_ = 0;
        }
    }
}

// ---------------------------------------------------------------------------
//  QPSK sem FEC: viterbi K=5 (lote de 8 bits) direto no varicode
// ---------------------------------------------------------------------------
void BpskCore::rxQpsk(int bits)
{
    bits = (4 - bits) & 3;               // espectro USB: passos invertidos
    bits = (2 - bits) & 3;               // recupera o byte do encoder
                                         // (fase medida = (2+par)&3; provado
                                         //  no TestaBpsk: 0 erros em 305 sim)
    unsigned char sym[2];
    sym[0] = (bits & 1) ? 255 : 0;       // pares diretos: poly1 no bit baixo,
    sym[1] = (bits & 2) ? 255 : 0;       // poly2 no bit alto (o mesmo convenio
                                         // do TestaViterbi, ida e volta 1.000)
    const int c = vit0_.decode(sym, nullptr);
    if (c >= 0) {
        rxBit(c & 0x80); rxBit(c & 0x40); rxBit(c & 0x20); rxBit(c & 0x10);
        rxBit(c & 0x08); rxBit(c & 0x04); rxBit(c & 0x02); rxBit(c & 0x01);
    }
}

// ---------------------------------------------------------------------------
//  FEC dos modos F: dois viterbis (um atrasado do outro) + votacao de
//  caractere + entrelacador 2x2x40 (so o modo 125)
// ---------------------------------------------------------------------------
void BpskCore::rxPskr(unsigned char soft)
{
    // par = [simbolo novo, simbolo anterior] (a fonte publica chama de
    // symbolpair e alimenta os dois viterbis alternadamente)
    unsigned char par[2];
    par[0] = soft;
    par[1] = parAnt_;
    parAnt_ = soft;

    unsigned char twosym[2];
    if (!vitPasso_) {
        vitPasso_ = true;
        twosym[0] = par[0];
        twosym[1] = par[1];
        if (m_->inter) ent1_->symbols(twosym);
        unsigned char tmp = twosym[0]; twosym[0] = twosym[1]; twosym[1] = tmp;
        int met = 0;
        const int c = vit1_.decode(twosym, &met);
        if (c >= 0) {
            // fldigi rx_pskr: as duas streams acumulam SEMPRE (o voto e
            // feito no nivel do caractere dentro de rxBit/rxBit2)
            fecMet1_ = decMedia(fecMet1_, (double)met, 20);
            rxBit2(c & 0x08); rxBit2(c & 0x04); rxBit2(c & 0x02); rxBit2(c & 0x01);
        }
    } else {
        vitPasso_ = false;
        twosym[0] = par[0];
        twosym[1] = par[1];
        if (m_->inter) ent0_->symbols(twosym);
        unsigned char tmp = twosym[0]; twosym[0] = twosym[1]; twosym[1] = tmp;
        int met = 0;
        const int c = vit0_.decode(twosym, &met);
        if (c >= 0) {
            fecMet0_ = decMedia(fecMet0_, (double)met, 20);
            rxBit(c & 0x08); rxBit(c & 0x04); rxBit(c & 0x02); rxBit(c & 0x01);
        }
    }
}

// ---------------------------------------------------------------------------
//  Interface com o app
// ---------------------------------------------------------------------------
void BpskCore::limpar()
{
    std::lock_guard<std::mutex> lk(mtx_);
    pendente_.clear();
    caracteres_ = 0;
    simbolos_ = 0;
    reiniciarRx();
}

std::string BpskCore::pegarTexto()
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::string s;
    s.swap(pendente_);
    return s;
}

BpskCore::Estado BpskCore::estado() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    Estado e;
    e.ok = dcd_;
    e.desvioHz = -(freq_ - centro_);
    e.nivelDb = nivelMedio_ > 1e-9 ? 20.0 * std::log10(nivelMedio_) : -200.0;
    e.metrica = metrica_;
    e.simbolos = simbolos_;
    e.caracteres = caracteres_;
    return e;
}

} // namespace masdr
