// ===========================================================================
//  Dois aparelhos (vindo do MB1SDR 1.1.0) - o VFO-A num dongle e o VFO-B noutro (por exemplo dois
//  RTL-TCP: um em HF e outro em VHF). O receptor OUVIDO e r_; o do outro VFO e
//  outro(). Os dois tocam ao mesmo tempo (ouvirOs2_), cada um com o seu volume.
// ===========================================================================

// liga / desliga o modo de dois aparelhos (sempre a partir do VFO-A)
void Ui::ligarDuas(bool on)
{
    if (on == duas_) return;
    std::string e;
    if (ativo_ == 1) ativar(0);          // a troca e feita com o VFO-A ouvido (o aparelho dele)
    if (!on) {
        duas_ = false;
        rb_->ligar(false, e);
        AjusteAudio mudo;                // o aparelho do B fica calado
        mudo.mudo = true;
        rb_->som().ajustar(mudo);
        ra_->setMedirCanal(true);
        aviso("VFO-B: agora no mesmo aparelho do VFO-A", 2.5);
    } else {
        duas_ = true;
        fftO_ = QuadroFft(); suaveO_.clear(); pisoO_ = 1e9f; quadrosOutro_ = 0; autoOutroFeito_ = false;
        ajOutroPrimeiro_ = true; rfOutroAplicado_ = -1.f; tentarOutroAte_ = 0; assinOutro_.clear(); erroOutro_.clear();
#ifndef DUAS_SEM_SUB
        dual_ = true;                    // o escopo do VFO-B ao lado (RX2)
        if (subTex_) criarTexturaSub();
#endif
        aviso("VFO-B com aparelho próprio", 2.5);
    }
    Config::instance().set("b_ativo", duas_);
}

void Ui::atualizarDuas()
{
    std::string e;
    if (pedidoConectarB_) {              // CONFIGURACAO > VFO-B > Conectar (feito aqui, fora da janela)
        pedidoConectarB_ = false;
        auto& c = Config::instance();
        const std::string tp = c.str("b_device");
        std::string arg = tp == "rtltcp" ? c.str("b_rtltcp_host") + ":" + c.str("b_rtltcp_port") : c.str("b_serial");
        if (tp.empty()) erroOutro_ = "escolha o aparelho do VFO-B";
        else if (!rb_->selecionar(tp, arg, e)) { erroOutro_ = e; aviso("VFO-B: " + e, 4.0); }
        else {
            erroOutro_.clear(); tentarOutroAte_ = 0; assinOutro_.clear(); rfOutroAplicado_ = -1.f; ajOutroPrimeiro_ = true;
            quadrosOutro_ = 0; autoOutroFeito_ = false; pisoO_ = 1e9f;
            if (duas_ && ra_->ligado() && !rb_->ligar(true, e)) erroOutro_ = e;
        }
    }
    if (pedidoDuas_ >= 0) { const bool on = pedidoDuas_ == 1; pedidoDuas_ = -1; ligarDuas(on); }
    if (!duas_) {
        if (r_ != ra_) { r_->setMedirCanal(false); r_ = ra_; r_->setMedirCanal(true); }
        if (rb_->ligado()) rb_->ligar(false, e);
        return;
    }
    // o aparelho ouvido e o do VFO em uso (no inicio do programa o INI pode vir com o B ouvido)
    {
        Radio* certo = ativo_ == 0 ? ra_ : rb_;
        if (r_ != certo) {
            r_->setMedirCanal(false);
            const bool ligava = r_->ligado();
            r_ = certo;
            r_->setMedirCanal(true);
            if (ligava && !r_->ligado()) r_->ligar(true, e);
            const Receptor& R = rx_[ativo_];
            if (R.modo != r_->modo()) r_->setModo(R.modo);
            if (r_->vfo() != R.f) r_->sintonizar(R.f);
            ajPrimeiro_ = true; rfAplicado_ = -1.f;
        }
    }
    Radio* O = outro();
    const int o = 1 - ativo_;
    Receptor& R = rx_[o];
    // liga e desliga junto com o ouvido (sem ficar tentando: a cada 10 s se der erro)
    if (r_->ligado()) {
        if (!O->ligado() && agora() > tentarOutroAte_) {
            if (!O->ligar(true, e)) {
                erroOutro_ = e; tentarOutroAte_ = agora() + 10.0;
                aviso(std::string("VFO-") + (o == 0 ? "A" : "B") + ": " + e, 4.0);
            } else erroOutro_.clear();
        }
    } else if (O->ligado()) O->ligar(false, e);
    r_->setMedirCanal(true);
    O->setMedirCanal(false);
    // o outro VFO: sintonia, modo, filtro, RF, RIT e o que e comum (ATT, PREAMP, NB2)
    if (O->vfo() != R.f) O->sintonizar(R.f);
    if (O->modo() != R.modo) O->setModo(R.modo);
    {
        const int g = grupoModo(R.modo);
        int bw = (int)std::lround(filHi_[g][R.fil]);
        if (g == 2) bw *= 2;
        bw = std::max(50, bw);
        if (O->banda() != bw) O->setBanda(bw);
    }
    if (R.rf != rfOutroAplicado_) { rfOutroAplicado_ = R.rf; O->setGanho((int)std::lround(4.96f * R.rf)); }
    O->setRit(R.ritOn ? R.rit : 0);
    O->setGanhoExtraDb(-6.f * att_);
    O->setPreamp(pamp_);
    O->setNb2(R.nb2, nb2Nivel_);
    {   // a configuracao comum (taxa, PPM, AGC do dongle, Bias-T, FFT) mudou: vale tambem no outro
        auto& c = Config::instance();
        const std::string a = std::to_string(c.sampleRate()) + "/" + std::to_string(c.ppm()) + "/" + (c.agc() ? "1" : "0") +
                              (c.biasT() ? "1" : "0") + "/" + std::to_string(c.fftSize()) + "/" + c.qmode();
        if (a != assinOutro_) { if (!assinOutro_.empty()) O->aplicarConfig(); assinOutro_ = a; }
    }
    // o espectro do outro aparelho (escopo do Sub RX, S-meter / BUSY do outro, squelch dele)
    {
        QuadroFft q;
        if (O->pegarFft(q)) {
            fftO_ = std::move(q);
            const int nb = (int)fftO_.bins.size();
            if ((int)suaveO_.size() != nb) suaveO_.assign(fftO_.bins.begin(), fftO_.bins.end());
            else for (int i = 0; i < nb; ++i) suaveO_[i] += ((float)fftO_.bins[i] - suaveO_[i]) * 0.45f;
            if (nb > 64) {   // o piso do ruido dele: percentil 30 (sem as beiradas)
                std::vector<int8_t> v(fftO_.bins.begin() + nb / 10, fftO_.bins.end() - nb / 10);
                std::nth_element(v.begin(), v.begin() + v.size() * 3 / 10, v.end());
                const float p = (float)v[v.size() * 3 / 10];
                pisoO_ = pisoO_ > 1e8f ? p : pisoO_ + (p - pisoO_) * 0.08f;
            }
            ++quadrosOutro_;
        }
#ifndef DUAS_SEM_SUB
        // o AUTO da cachoeira do outro aparelho (o piso dele e outro: HF x VHF): uma vez quando chega o
        // espectro dele, de novo a cada TUNE / AUTO e quando ele muda de faixa (mais de 3 MHz)
        if (autoOutroFeito_ && std::llabs((long long)R.f - (long long)autoOutroF_) > 3000000LL) {
            autoOutroFeito_ = false; quadrosOutro_ = 0; pisoO_ = 1e9f;
        }
        if (!autoOutroFeito_ && quadrosOutro_ > 20 && pisoO_ < 1e8f) {
            autoOutroF_ = R.f;
            const float acima = (R.acima > -900.f && R.acima != -1.f) ? R.acima : wfAcimaPiso_;
            const float rcal = R.rangeCal > 0.f ? R.rangeCal : wfRangeCal_;
            R.range = std::clamp(std::round(rcal), 20.f, 120.f);
            R.brilho = std::clamp(std::round((pisoO_ + acima) * 2.f + 100.f), 0.f, 200.f);
            if (R.paleta < 0) R.paleta = paleta_;
            autoOutroFeito_ = true;
        }
#endif
    }
    // o audio do outro VFO (o volume e os filtros dele)
    {
        bool sqlAberto = true;
        if (R.sql > -90.f && !fftO_.bins.empty() && fftO_.taxa) {
            double f0, f1;
            faixaRx(o, f0, f1);
            const int nb = (int)fftO_.bins.size();
            auto bin = [&](double f) { return std::clamp((int)std::lround((f - (double)fftO_.centro) / fftO_.taxa * nb + nb / 2.0), 0, nb - 1); };
            int a = bin(f0), b = bin(f1);
            if (a > b) std::swap(a, b);
            float pk = -200.f;
            for (int i = a; i <= b; ++i) pk = std::max(pk, (float)fftO_.bins[i]);
            // a mesma escala do squelch do VFO ouvido (Ui::novoQuadroFft: peakDbSql_): quanto a potencia media
            // no filtro passa a de um canal vazio do mesmo tamanho, menos 60 (o ruido fica em -60)
            const float pc = pisoCanal(fftO_, b - a + 1);
            if (pc > -900.f) pisoSqlO_ = pisoSqlO_ > 1e8f ? pc : pisoSqlO_ + (pc - pisoSqlO_) * 0.08f;
            const float db = (pk <= -199.f || pisoSqlO_ > 1e8f) ? -120.f : std::clamp(potDb(fftO_.bins, a, b, -9) - pisoSqlO_ - 60.f, -120.f, 60.f);
            static double ultAberto = 0;
            if (db >= R.sql) ultAberto = agora();
            sqlAberto = db >= R.sql || (agora() - ultAberto < sqlHang_);
        }
        sqlAbertoO_ = sqlAberto;
        AjusteAudio a;
        a.mudo = R.mute || sendMudo_ || !O->ligado() || (!ouvirOs2_ && !monSolo_) || !sqlAberto;
        a.volume = R.af <= 0 ? 0.f : (float)(std::pow(R.af / 100.0, 1.5) * 8.0);
        a.nb = R.nb1 ? nbNivel_ / 100.f : 0.f;
        a.nrEsp = R.nr2;
        a.nrEspForca = (int)std::lround(nrEspForca_);
        a.nr = R.nr1 ? nrNivel_ / 100.f : 0.f;
        a.tom = (int)std::lround(tom_);
        a.notch = R.nch; a.notchLargo = R.nchLargo;
        a.notchHz = (float)(150.0 * std::pow(3500.0 / 150.0, std::clamp(R.notch, 0.f, 1.f)));
#ifdef DUAS_SEM_CONT
        a.bef = R.bef; a.befHz = a.notchHz;
#else
        a.bef = R.bef; a.befHz = (float)(150.0 * std::pow(3500.0 / 150.0, std::clamp(R.cont, 0.f, 1.f)));
#endif
        a.aNotch = R.anch; a.bc = R.bc;
        a.apf = R.apf; a.apfHz = cwPitch_;
        a.proc = proc_; a.procNivel = procNivel_;
        if (ajOutroPrimeiro_ || !a.igual(ajOutro_)) { O->som().ajustar(a); ajOutro_ = a; ajOutroPrimeiro_ = false; }
    }
}

// CONFIGURACAO > VFO-B: o segundo aparelho
void Ui::configVfoB()
{
    auto& c = Config::instance();
    if (!cfgBLido_) {
        cfgBLido_ = true;
        const std::string tp = c.str("b_device", "rtltcp");
        cfgBTipo_ = tp == "rtlsdr" ? 0 : tp == "sdrplay" ? 2 : 1;
        std::snprintf(cfgBHost_, sizeof cfgBHost_, "%s", c.str("b_rtltcp_host", "127.0.0.1").c_str());
        cfgBPorta_ = (int)c.num("b_rtltcp_port", 1234);
    }
    ImGui::SeparatorText("VFO-B (segundo aparelho)");
    bool on = duas_;
    if (ImGui::Checkbox("VFO-B com aparelho próprio (um dongle em cada VFO)", &on)) {
        if (on) pedidoConectarB_ = true;
        pedidoDuas_ = on ? 1 : 0;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ligado: o VFO-A usa o aparelho de cima e o VFO-B o daqui de baixo (por exemplo um\n"
                          "RTL-TCP em HF e outro em VHF). Os dois recebem ao mesmo tempo e o escopo do VFO-B\n"
                          "aparece ao lado (RX2 / Sub RX).\nDesligado: o VFO-B usa o mesmo aparelho do VFO-A (como antes).");
    if (!duas_ && !on) return;
    const char* tipos[] = {"RTL-SDR (USB)", "RTL-TCP (rede)", "SDRplay"};
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Tipo (VFO-B)"); ImGui::SameLine(170 * s_); ImGui::SetNextItemWidth(300 * s_);
    ImGui::Combo("##btipo", &cfgBTipo_, tipos, 3);
    if (cfgBTipo_ == 1) {
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Endereço (IP)"); ImGui::SameLine(170 * s_); ImGui::SetNextItemWidth(300 * s_);
        ImGui::InputText("##bhost", cfgBHost_, sizeof cfgBHost_);
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Porta"); ImGui::SameLine(170 * s_); ImGui::SetNextItemWidth(300 * s_);
        ImGui::InputInt("##bporta", &cfgBPorta_, 0);
    } else {
        const std::string alvo = cfgBTipo_ == 0 ? "rtlsdr" : "sdrplay";
        std::vector<int> idx;
        for (int i = 0; i < (int)lista_.size(); ++i)
            if (lista_[i].tipo == alvo && !(ra_->temDispositivo() && ra_->tipo() == alvo && ra_->serial() == lista_[i].serial)) idx.push_back(i);
        std::string atual = idx.empty() ? std::string("(nenhum - clique em Procurar)") : std::string();
        int sel = 0;
        for (int k = 0; k < (int)idx.size(); ++k) if (idx[k] == cfgBDisp_) sel = k;
        if (!idx.empty()) atual = lista_[idx[sel]].nome + "  [" + lista_[idx[sel]].serial + "]";
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Dispositivo (VFO-B)"); ImGui::SameLine(170 * s_); ImGui::SetNextItemWidth(300 * s_);
        if (ImGui::BeginCombo("##bdisp", atual.c_str())) {
            for (int k = 0; k < (int)idx.size(); ++k) {
                const auto& d = lista_[idx[k]];
                const std::string t = d.nome + "  [" + d.serial + "]";
                if (ImGui::Selectable(t.c_str(), k == sel)) cfgBDisp_ = idx[k];
            }
            ImGui::EndCombo();
        }
        if (!idx.empty() && cfgBDisp_ < 0) cfgBDisp_ = idx[0];
        ImGui::SameLine();
        if (ImGui::Button("Procurar##b")) lista_ = Radio::listar(ra_->temDispositivo() ? ra_->tipo() : std::string(), ra_->serial(), cfgBTipo_ == 2);
    }
    if (ImGui::Button("Conectar o VFO-B")) {
        const std::string tp = cfgBTipo_ == 0 ? "rtlsdr" : cfgBTipo_ == 2 ? "sdrplay" : "rtltcp";
        c.set("b_device", tp);
        if (cfgBTipo_ == 1) {
            c.set("b_rtltcp_host", std::string(cfgBHost_));
            c.set("b_rtltcp_port", cfgBPorta_);
            c.set("b_serial", std::string());
        } else if (cfgBDisp_ >= 0 && cfgBDisp_ < (int)lista_.size()) c.set("b_serial", lista_[cfgBDisp_].serial);
        pedidoConectarB_ = true;
        if (!duas_) pedidoDuas_ = 1;
    }
    ImGui::SameLine();
    {
        const char* st;
        ImU32 cor;
        if (!erroOutro_.empty()) { st = erroOutro_.c_str(); cor = IM_COL32(0xff, 0x70, 0x60, 255); }
        else if (!rb_->temDispositivo()) { st = "sem aparelho"; cor = IM_COL32(0xb0, 0xb0, 0xb0, 255); }
        else if (rb_->aoVivo()) { st = "recebendo"; cor = IM_COL32(0x70, 0xf0, 0x80, 255); }
        else if (rb_->ligado()) { st = "ligado, sem dados ainda"; cor = IM_COL32(0xf0, 0xd0, 0x60, 255); }
        else { st = "conectado (liga com o POWER)"; cor = IM_COL32(0xb0, 0xd0, 0xff, 255); }
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(cor), "%s", st);
    }
    if (ImGui::Checkbox("Ouvir os dois VFOs ao mesmo tempo", &ouvirOs2_)) c.set("b_dois_audios", ouvirOs2_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ligado: o áudio do VFO-A e o do VFO-B tocam juntos, cada um com o seu volume\n"
                          "(o AF; com SHIFT, o AF2 = o volume do outro VFO).\n"
                          "Desligado: só o VFO selecionado toca (o outro fica mudo).\n"
                          "Também no indicador MON da tela.");
}

#ifndef DUAS_SEM_SUB
// O escopo do outro VFO (Sub / DUAL): com dois aparelhos, a janela inteira do aparelho dele
// (sem zoom); senao, centrado na frequencia dele com o mesmo span do principal.
void Ui::janelaSub(const Receptor& O, double span)
{
    if (duas_) {
        const double txO = fftO_.taxa ? (double)fftO_.taxa : (double)outro()->taxa();
        subSpan_ = txO / (1.0 + zoom_);
        subCentro_ = (zoom_ < 0.01f && fftO_.centro) ? (double)fftO_.centro : (double)O.f;
    } else {
        subCentro_ = (double)O.f;
        subSpan_ = span;
    }
}
#endif

// Este VFO esta tocando? (o ouvido, a nao ser com o MON/TXW segurado; o outro, com dois aparelhos
// e "ouvir os dois", ou com o MON/TXW segurado)
bool Ui::tocaRx(int rx) const
{
    if (rx == ativo_) return !(duas_ && monSolo_);
    return duas_ && (ouvirOs2_ || monSolo_);
}

// Clique na etiqueta RX de um VFO (com dois aparelhos): os dois tocam juntos / so o selecionado
void Ui::alternarOuvirOs2()
{
    ouvirOs2_ = !ouvirOs2_;
    Config::instance().set("b_dois_audios", ouvirOs2_);
    aviso(ouvirOs2_ ? "Os dois VFOs tocam juntos" : "Só o VFO selecionado toca", 1.5);
}

#ifdef DUAS_FAIXARX
// a faixa (Hz) que o filtro do receptor rx ocupa (o squelch do outro aparelho mede nela)
void Ui::faixaRx(int rx, double& f0, double& f1) const
{
    const Receptor& R = rx_[rx];
    const bool at = rx == ativo_;
    const double v = at ? (double)r_->vfo() : (double)R.f;
    const std::string m = at ? r_->modo() : R.modo;
    double bw;
    if (at) bw = r_->banda();
    else {
        const int g = grupoModo(m);
        bw = g == 2 ? filHi_[g][R.fil] * 2.0 : filHi_[g][R.fil];
    }
    bw = std::max(50.0, bw);
    if (m == "USB") { f0 = v; f1 = v + bw; }
    else if (m == "LSB") { f0 = v - bw; f1 = v; }
    else { f0 = v - bw / 2; f1 = v + bw / 2; }
}
#endif

// BUSY de um VFO: o ouvido = squelch aberto (e sinal); o outro, com dois aparelhos = o squelch DELE aberto
// (com o squelch aberto, -90, o S-meter dele acima do ruido); no mesmo aparelho, o S-meter lido no espectro.
bool Ui::ocupadoRx(int rx) const
{
    const Receptor& R = rx_[rx];
    if (rx == ativo_) return sqlAberto_ && (R.sql > -90 || smAtual_ > 0.22f);
#ifdef DUAS_SEM_SMOUTRO
    const float smO = 0.f;           // este painel nao le o S-meter do outro receptor
#else
    const float smO = smOutro_;
#endif
    if (duas_) {
        const Radio* O = outro();
        if (!O || !O->aoVivo()) return false;
        return R.sql > -90.f ? sqlAbertoO_ : smO > 0.30f;
    }
    return smO > 0.30f;
}
