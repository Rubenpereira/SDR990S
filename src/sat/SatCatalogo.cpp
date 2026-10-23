// ---------------------------------------------------------------------------
//  Catalogo de satelites de RADIOAMADOR do decodificador SATELITES
//
//  Frequencias CENTRAIS de descida (sem Doppler). Conferidas com a base do
//  SatNOGS (db.satnogs.org) e a lista de TLE "amateur" da Celestrak em
//  19/10/2026: so entram satelites em orbita com o transmissor marcado como
//  ativo. Satelite de radioamador liga e desliga (bateria, horarios,
//  eventos) - "ativo" nao garante que ele esteja transmitindo na hora.
//
//  Para incluir outro: nome, NORAD, tipo, descida, subida, modo, filtro, nota.
// ---------------------------------------------------------------------------
#include "Rastreador.h"

namespace masdr {

const char* nomeTipoSat(TipoSat t)
{
    switch (t) {
    case TipoSat::FONIA_FM: return "Fonia FM";
    case TipoSat::FONIA_SSB: return "Fonia SSB";
    case TipoSat::SSTV: return "SSTV";
    case TipoSat::DADOS: return "Dados";
    case TipoSat::APT: return "APT";
    case TipoSat::LRIT: return "LRIT";
    case TipoSat::VOZ: return "Fonia";
    }
    return "";
}

const char* nomeCategoria(Categoria c)
{
    if (c == Categoria::METEOROLOGICOS) return "Meteorológicos";
    if (c == Categoria::MILITARES) return "Militares";
    if (c == Categoria::ISS_EVA) return "ISS EVA";
    if (c == Categoria::NAVES_ISS) return "Naves da ISS";
    return "Radioamador";
}

bool categoriaMeteo(TipoSat t)
{
    return t == TipoSat::APT || t == TipoSat::LRIT;
}

const std::vector<SatInfo>& catalogoRadioamador()
{
    using TS = TipoSat;
    static const std::vector<SatInfo> c = {
        // ---------------- FONIA FM (repetidoras V/U e U/V) ----------------
        {"ISS - repetidora FM", 25544, TS::FONIA_FM, 437800000, 145990000, "NFM", 15000, "subida 145,990 com CTCSS 67 Hz"},
        {"ISS - tripulação (ARISS)", 25544, TS::FONIA_FM, 145800000, 145200000, "NFM", 12500, "contatos com escolas; subida 145,200 (R1) / 144,490 (R2/R3)"},
        {"SO-50 (SaudiSat-1C)", 27607, TS::FONIA_FM, 436795000, 145850000, "NFM", 15000, "CTCSS 67 Hz (74,4 Hz liga o timer)"},
        {"AO-27 (EyeSat-A)", 22825, TS::FONIA_FM, 436795000, 145850000, "NFM", 15000, "liga em horários programados"},
        {"AO-91 (Fox-1B)", 43017, TS::FONIA_FM, 145960000, 435250000, "NFM", 12500, "sem CTCSS; só com sol (bateria)"},
        {"AO-123 (ASRTU-1)", 61781, TS::FONIA_FM, 435400000, 145850000, "NFM", 15000, "repetidora V/U"},
        {"PO-101 (Diwata-2B)", 43678, TS::FONIA_FM, 145900000, 437500000, "NFM", 12500, "liga em horários anunciados"},
        {"TEVEL2-1", 63217, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-2", 63219, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-3", 63218, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-4", 63213, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-5", 63214, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-6", 63215, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-7", 63238, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-8", 63239, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},
        {"TEVEL2-9", 63237, TS::FONIA_FM, 436400000, 145970000, "NFM", 15000, "transponder FM em horários"},

        // ---------------- FONIA SSB (transponders lineares: centro da faixa) ----------------
        {"AO-7 (Oscar 7) - modo B", 7530, TS::FONIA_SSB, 145950000, 432150000, "USB", 2700, "faixa 145,925-145,975 (invertido); só com sol"},
        {"FO-29 (JAS-2)", 24278, TS::FONIA_SSB, 435850000, 145950000, "USB", 2700, "faixa 435,800-435,900 (invertido); liga em horários"},
        {"RS-44", 44909, TS::FONIA_SSB, 435640000, 145965000, "USB", 2700, "faixa 435,610-435,670 (invertido)"},
        {"JO-97 (JY1Sat)", 43803, TS::FONIA_SSB, 145865000, 435110000, "USB", 2700, "faixa 145,855-145,875 (invertido)"},
        {"AO-73 (FUNcube-1)", 39444, TS::FONIA_SSB, 145960000, 435140000, "USB", 2700, "faixa 145,950-145,970 (invertido); transponder no eclipse"},

        // ---------------- SSTV (imagens em FM) ----------------
        {"ISS - SSTV (ARISS)", 25544, TS::SSTV, 437550000, 0, "NFM", 20000, "Robot 36, uma imagem a cada 2 min (ariiss.org)"},
        {"UmKA-1 (RS40S)", 57172, TS::SSTV, 437625000, 0, "NFM", 20000, "SSTV periódico"},
        {"SONATE-2", 59112, TS::SSTV, 145880000, 0, "NFM", 20000, "Martin M1"},
        {"UTMN-2 (RS27S)", 57203, TS::SSTV, 436125000, 0, "NFM", 20000, "Robot 36"},
        {"Monitor-3 (RS58S)", 57180, TS::SSTV, 435290000, 0, "NFM", 20000, "Robot 36"},
        {"Vizard-Meteo (RS38S)", 57189, TS::SSTV, 437825000, 0, "NFM", 20000, "Robot 72"},
        {"CO-66 (SEEDS II)", 32791, TS::SSTV, 437485000, 0, "NFM", 20000, "SSTV ocasional"},

        // ---------------- DADOS (AX.25 1200 baud AFSK - decodifica com o direwolf) ----------------
        {"ISS - APRS digipeater", 25544, TS::DADOS, 145825000, 145825000, "NFM", 15000, "APRS 1200 baud (VHF: todo o mundo)"},
        {"ISS - APRS digipeater (UHF)", 25544, TS::DADOS, 437825000, 437825000, "NFM", 15000, "APRS 1200 baud (UHF)"},
        {"NO-44 (PCSat)", 26931, TS::DADOS, 145825000, 145825000, "NFM", 15000, "APRS 1200 baud; só com sol"},
        {"SONATE-2 - APRS", 59112, TS::DADOS, 145825000, 145825000, "NFM", 15000, "APRS digipeater 1200 baud"},
        {"LilacSat-2 (CAS-3H)", 40908, TS::DADOS, 144390000, 144390000, "NFM", 15000, "APRS digipeater 1200 baud"},
        {"CO-57 (XI-IV)", 27848, TS::DADOS, 437490000, 0, "NFM", 15000, "telemetria AX.25 1200"},
        {"CO-55 (CUTE-1)", 27844, TS::DADOS, 437470000, 0, "NFM", 15000, "telemetria AX.25 1200"},
        {"CO-65 (CUTE-1.7)", 32785, TS::DADOS, 437475000, 0, "NFM", 15000, "telemetria AX.25 1200"},
        {"CO-66 (SEEDS II) - dados", 32791, TS::DADOS, 437485000, 0, "NFM", 15000, "telemetria AX.25 1200"},
        {"KKS-1 (Kiseki)", 33499, TS::DADOS, 437445000, 0, "NFM", 15000, "mensagens AX.25 1200"},
        {"OOV-Cube (TUBSAT-30)", 60240, TS::DADOS, 435950000, 0, "NFM", 15000, "AX.25 1200 AFSK"},
        {"Monitor-2 (RS39S)", 57184, TS::DADOS, 435815000, 0, "NFM", 15000, "AX.25 1200 AFSK"},
    };
    return c;
}

// ---------------------------------------------------------------------------
//  Catalogo de ISS EVA: os canais que a ISS usa (e que DA PARA OUVIR numa
//  estacao de solo) enquanto a tripulacao esta fora na caminhada espacial.
//  Pesquisado em 21/10/2026 (lista do VA3RJ/qsl.net, Sven Grahn
//  www.svengrahn.pp.se, orbitalfocus.uk, ariiss.org):
//
//  * A VOZ DA EVA sai pela ISS em VHF: os tres radios VHF do segmento
//    russo levam o audio de dentro (russo ou ingles) e o dos trajes; os
//    canais: 121,750 (canal EVA-1, reserva), 130,167 (VHF-2, a voz da EVA
//    e os comandos do TORU) e 143,625 (VHF-1, a voz principal do segmento
//    russo). Desvio FM de +-30 kHz.
//  * O traje Orlan fala com a estacao em 121,125 MHz (subida: nao da para
//    ouvir da Terra, fica fora da lista); a telemetria dele sai pela
//    Zvezda nos canais de dados da EVA: 247,0 e 231,0 MHz (PCM/FM, no
//    radio aparece como zumbido).
//  * A TV da EVA (Glisser) vai em FM em 463,0 MHz: o programa so ouve o
//    canal, sem imagem.
//  * As frequencias 259,7/296,8 MHz do vaivem espacial (traje EMU em AM)
//    acabaram com o fim do vaivem, em 2011: fora da lista.
//  * As EVAs norte-americanas (EMU) usam radio UHF a bordo; o audio so
//    chega ao solo via banda S. Em VHF o que se ouve e a EVA misturada
//    nos canais russos acima (a ISS retransmite por contingencia).
//
//  Doppler: ate +-4,6 kHz em 143 MHz - siga pelo botao de seguimento.
// ---------------------------------------------------------------------------
const std::vector<SatInfo>& catalogoIssEva()
{
    using TS = TipoSat;
    static const std::vector<SatInfo> c = {
        // ---------------- VOZ (os tres canais VHF da ISS) ----------------
        {"ISS EVA - voz canal 1 (121,75)", 25544, TS::VOZ, 121750000, 0, "NFM", 30000,
         "voz de EVA canal 1 (reserva); tambem TORU e Soyuz; desvio 30 kHz"},
        {"ISS EVA - voz canal 2 (130,167)", 25544, TS::VOZ, 130167000, 0, "NFM", 30000,
         "VHF-2: voz da EVA (russo e ingles) e comandos do TORU"},
        {"ISS EVA - voz principal (143,625)", 25544, TS::VOZ, 143625000, 0, "NFM", 30000,
         "VHF-1: voz principal do segmento russo, leva o audio do traje Orlan fora"},

        // ---------------- TELEMETRIA DOS TRAJES (Orlan, via Zvezda) ----------------
        {"ISS EVA - dados da Orlan (247,0)", 25544, TS::DADOS, 247000000, 0, "NFM", 40000,
         "telemetria PCM/FM do traje (sistema de dados da EVA); no radio parece zumbido"},
        {"ISS EVA - dados da Orlan (231,0)", 25544, TS::DADOS, 231000000, 0, "NFM", 40000,
         "segundo canal de dados da EVA (recebimento da Orlan)"},

        // ---------------- TV (video do caminho espacial) ----------------
        {"ISS EVA - TV (463,0)", 25544, TS::DADOS, 463000000, 0, "WFM", 500000,
         "video da EVA (sistema Glisser); o programa so ouve o canal, sem imagem"},
    };
    return c;
}

// ---------------------------------------------------------------------------
//  Catalogo de NAVES DA ISS: as telemetrias dos veiculos Soyuz/Progress e do
//  segmento russo da ISS. Pesquisado em 21/10/2026 (Sven Grahn
//  www.svengrahn.pp.se - Soyuz Radio Systems / PCM-FM telemetry, lista do
//  VA3RJ/qsl.net, orbitalfocus.uk):
//
//  * 166,0 MHz - a telemetria PCM/FM (sistema BR-9TsU) do Soyuz/Progress:
//    bandas laterais de +-128 kHz (zumbido em 165,875/166,125 no FM
//    estreito). Transmite de continuo no VOO LIVRE - a janela e de 2 a 3
//    dias apos cada lancamento (uns 5 por ano) - e de vez em quando
//    acoplado. A NAVE tem NORAD PROPRIO que muda a cada missao: os itens
//    seguem o NORAD 25544 da ISS (igual quando acoplada; em voo livre a
//    nave fica a centenas de km - a passagem e aproximada).
//  * 628,0/630,0 - telemetria BITS da Zvezda (256 kbps, mesmo formato PCM/
//    FM); 632,0/634,0 - BITS da Zarya. Transmitem por comando da Russia,
//    esporadico.
//  * 922,76 - sistema Regul/KVANT (faixa 922-928): telemetria, voz e
//    rastreamento (o 922,75 do Progress e o portador CW). Sinal largo e
//    raro; mais para registrar no espectro do que para decodificar.
// ---------------------------------------------------------------------------
const std::vector<SatInfo>& catalogoNavesIss()
{
    using TS = TipoSat;
    static const std::vector<SatInfo> c = {
        // ---------------- TELEMETRIA DOS FERROS (Soyuz/Progress) ----------------
        {"Soyuz/Progress - telemetria (166,0)", 25544, TS::DADOS, 166000000, 0, "WFM", 300000,
         "PCM/FM BR-9TsU, bandas em 165,875/166,125; continuo no voo livre (2-3 dias apos o lancamento)"},
        {"Zvezda (ISS) - telemetria BITS (630,0)", 25544, TS::DADOS, 630000000, 0, "WFM", 300000,
         "BITS 256 kbps da Zvezda; por comando da Russia, em par com a 628,0"},
        {"Zvezda (ISS) - telemetria BITS (628,0)", 25544, TS::DADOS, 628000000, 0, "WFM", 300000,
         "segundo canal BITS da Zvezda"},
        {"Zarya (ISS) - telemetria BITS (634,0)", 25544, TS::DADOS, 634000000, 0, "WFM", 300000,
         "BITS da Zarya (transmissoes raras)"},
        {"Zarya (ISS) - telemetria BITS (632,0)", 25544, TS::DADOS, 632000000, 0, "WFM", 300000,
         "segundo canal BITS da Zarya"},
        {"Regul/KVANT (922,76)", 25544, TS::DADOS, 922760000, 0, "WFM", 500000,
         "telemetria+voz do sistema Regul (922-928); o 922,75 do Progress e o portador CW; sinal largo e raro"},
    };
    return c;
}

// ---------------------------------------------------------------------------
//  Catalogo de METEOROLOGICOS (observacao da Terra)
//
//  researching em 20/10/2026 (NOAA OSPO, WMO/OSCAR, SatDump, SatNOGS DB):
//
//  * O APT ANALOGIO DA NOAA ACABOU. A POES foi desativada entre junho e
//    agosto de 2025: NOAA-18 (06/06), NOAA-19 (13/08) e NOAA-15 (19/08, o
//    ultimo APT do mundo). Cada um tinha uma frequencia DIFFERENTE:
//    NOAA-15 = 137,620 MHz | NOAA-18 = 137,9125 MHz | NOAA-19 = 137,100 MHz
//    (a maioria dos tutoriais erra e joga 137,9125 para todos).
//  * NOAA-20, NOAA-21 e Suomi NPP (JPSS) NAO tem APT (so dados em X/Ku).
//  * FY-3 (A/B/C), Tianmu-1, Arktika-M, MetOp, GCOM-W1 e todos os GEO
//    (Meteosat, GOES, Fengyun-4...) NAO tem downlink de VHF: so banda L
//    (1,7 GHz, LRIT/AHRPT) ou banda X - precisam de dish e SDR de banda larga.
//  * O QUE AINDA SE RECEBE EM 137 MHz SAO OS LRPT RUSSOS (digitais):
//    Meteor-M N2-3 (57166) e N2-4 (59051), OQPSK 72 ksym/s em 137,900 MHz
//    (137,1 e 137,9125 como reserva), ~150 kHz de banda, polarizacao RHCP.
//
//  Modo do radio: WFM (FM largo). O APT era FM com desvio de +-15 kHz (banda
//  de RF de 34 kHz) e a subportadora de 2400 Hz em AM; o LRPT e PM/FM largo
//  de ~150 kHz. O FM estreito do programa (25 kHz) cortaria o sinal.
//
//  Os satelites desligados ficam na lista marcados como DESATIVADOS (o
//  radio ainda afina neles, mas nao ha nada para ouvir).
// ---------------------------------------------------------------------------
const std::vector<SatInfo>& catalogoMeteorologicos()
{
    using TS = TipoSat;
    static const std::vector<SatInfo> c = {
        // ---------------- LRPT digital (ATIVOS - 137,9 MHz) ----------------
        {"Meteor-M N2-3", 57166, TS::LRIT, 137900000, 0, "WFM", 150000,
         "LRPT 72 ksym/s (OQPSK); decodifica no SatDump com o IQ bruto", true},
        {"Meteor-M N2-4", 59051, TS::LRIT, 137900000, 0, "WFM", 150000,
         "LRPT 72 ksym/s (OQPSK); decodifica no SatDump com o IQ bruto", true},

        // ---------------- APT analogico (DESATIVADOS em 2025) ----------------
        {"NOAA 19", 33591, TS::APT, 137100000, 0, "WFM", 100000,
         "DESATIVADO em 13/08/2025 (bateria); o APT estava em 137,1 MHz", false},
        {"NOAA 18", 28654, TS::APT, 137912500, 0, "WFM", 100000,
         "DESATIVADO em 06/06/2025 (falha do transmissor)", false},
        {"NOAA 15", 25338, TS::APT, 137620000, 0, "WFM", 100000,
         "DESATIVADO em 19/08/2025 - foi o ultimo APT do mundo", false},
        {"Meteor-M N2-2", 44387, TS::LRIT, 137900000, 0, "WFM", 150000,
         "DESATIVADO (jan/2025): nao transmite mais em 137 MHz", false},
        {"Meteor-M N2-1", 40069, TS::LRIT, 137900000, 0, "WFM", 150000,
         "INATIVO desde 2023 (falha de attitude)", false},
    };
    return c;
}

const std::vector<SatInfo>& catalogo(Categoria c)
{
    if (c == Categoria::METEOROLOGICOS) return catalogoMeteorologicos();
    if (c == Categoria::MILITARES) return catalogoMilitares();
    if (c == Categoria::ISS_EVA) return catalogoIssEva();
    if (c == Categoria::NAVES_ISS) return catalogoNavesIss();
    return catalogoRadioamador();
}

// ---------------------------------------------------------------------------
//  Catalogo de MILITARES (voz / imagem NAO criptografada)
//
//  Fonte: Satellitenwelt (Maik Hermenau) - satfreq_uhf.htm e uhfmilsat.htm,
//  conferido em 20/10/2026 com a Celestrak (TLE), a WMO/OSCAR e a SATCAT.
//
//  REGRA: so entram os satelites de defesa que (a) retransmitem VOZ sem
//  criptografia, (b) tem poucas frequencias - uma ou duas - e (c) ainda estao
//  em orbita. Os de lista grande de frequencias (UFO F2/F7/F10/F11, SICRAL,
//  Skynet 5, ComSatBw, MUOS, SDS 3-F2 a F6, Intelsat 22, Leasat F5,
//  FltSatCom) ficaram de fora: nao adianta colocar so uma frequencia de um
//  satelite que tem dezenas. Fora tambem os que ja cairam (NATO 4B, Skynet
//  4F, SDS 2-F4, SDS 3-F1, DSCS 3-F5) e os que so tem dados (SDS, DSCS,
//  NOSS, TacSat 4, Strela/Parus/Rodnik, Gonets).
//
//  * MILSTAR 1-F1 e 2-F3 (EUA): transponder de voz de 50 kHz na banda
//    AFSATCOM (243,8 MHz). Sao GEO, entao ficam sempre no mesmo azimute.
//  * SKYNET 4C e 4E (Reino Unido): 36 kHz, um canal de voz e um de dados.
//  * MERIDIAN 7 (Russia): transponder linear de voz ABERTA em 483/484 MHz
//    (43 kHz). Orbita HEO/Molniya de 63 graus: e o que passa mais alto pelo
//    Sul do Brasil - as melhores passagens para receber voz militar aberta.
//
//  Modo do radio: NFM com 12,5 kHz de filtro (e assim que se ouve esses
//  transponders de voz; o sinal e mais largo que o filtro, mas a voz sai).
// ---------------------------------------------------------------------------
const std::vector<SatInfo>& catalogoMilitares()
{
    using TS = TipoSat;
    static const std::vector<SatInfo> c = {
        // ---------------- MILSTAR (EUA) - voz AFSATCOM, GEO ----------------
        {"MILSTAR 1-F1 (USA 99)", 22988, TS::VOZ, 243825000, 0, "NFM", 12500, "voz militar; 50 kHz; GEO"},
        {"MILSTAR 2-F3 (USA 164)", 27168, TS::VOZ, 243785000, 0, "NFM", 12500, "voz militar; 50 kHz; GEO"},

        // ---------------- SKYNET 4 (Reino Unido) - 36 kHz, GEO ----------------
        {"Skynet 4C", 20776, TS::VOZ, 254050000, 0, "NFM", 12500, "canal de voz; 36 kHz; GEO"},
        {"Skynet 4C", 20776, TS::VOZ, 257550000, 0, "NFM", 12500, "canal de voz; 36 kHz; GEO"},
        {"Skynet 4E", 25639, TS::VOZ, 254155000, 0, "NFM", 12500, "canal de voz; 36 kHz; GEO"},
        {"Skynet 4E", 25639, TS::VOZ, 257500000, 0, "NFM", 12500, "canal de voz; 36 kHz; GEO"},

        // ---------------- MERIDIAN (RUSSIA) - voz aberta, HEO 63 graus ----------------
        {"Meridian 7", 40296, TS::VOZ, 483750000, 0, "NFM", 12500, "voz militar aberta; transponder de 43 kHz"},
        {"Meridian 7", 40296, TS::VOZ, 484250000, 0, "NFM", 12500, "voz militar aberta; transponder de 43 kHz"},
    };
    return c;
}

} // namespace masdr
