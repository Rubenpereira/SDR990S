#include "AdsbEmpresas.h"

#include <cctype>
#include <cstring>

namespace masdr {

namespace {

// tipo: 'P' passageiros, 'C' carga, 'E' executiva (fretamento de jatos)
struct Companhia { const char* icao; const char* nome; char tipo; };

// Codigos ICAO de companhias (3 letras do indicativo do voo)
const Companhia kCompanhias[] = {
    // Brasil
    {"GLO", "GOL Linhas Aéreas", 'P'},        {"TAM", "LATAM Brasil", 'P'},
    {"AZU", "Azul Linhas Aéreas", 'P'},       {"PTB", "VoePass (Passaredo)", 'P'},
    {"PAM", "MAP Linhas Aéreas", 'P'},        {"ONE", "Avianca Brasil", 'P'},
    {"TTL", "Total Linhas Aéreas", 'C'},      {"SID", "Sideral Air Cargo", 'C'},
    {"MWM", "Modern Logistics", 'C'},         {"TUS", "LATAM Cargo Brasil", 'C'},
    // America Latina
    {"LAN", "LATAM Chile", 'P'},              {"LPE", "LATAM Peru", 'P'},
    {"LNE", "LATAM Ecuador", 'P'},            {"LCO", "LATAM Cargo Chile", 'C'},
    {"ARG", "Aerolíneas Argentinas", 'P'},    {"AVA", "Avianca", 'P'},
    {"TAI", "Avianca El Salvador", 'P'},      {"LRC", "Avianca Costa Rica", 'P'},
    {"CMP", "Copa Airlines", 'P'},            {"SKU", "Sky Airline", 'P'},
    {"JAT", "JetSMART", 'P'},                 {"FBZ", "Flybondi", 'P'},
    {"AMX", "Aeroméxico", 'P'},               {"VIV", "Viva Aerobus", 'P'},
    {"VOI", "Volaris", 'P'},                  {"BOV", "Boliviana de Aviación", 'P'},
    // America do Norte
    {"AAL", "American Airlines", 'P'},        {"DAL", "Delta Air Lines", 'P'},
    {"UAL", "United Airlines", 'P'},          {"SWA", "Southwest Airlines", 'P'},
    {"JBU", "JetBlue", 'P'},                  {"ASA", "Alaska Airlines", 'P'},
    {"NKS", "Spirit Airlines", 'P'},          {"FFT", "Frontier Airlines", 'P'},
    {"AAY", "Allegiant Air", 'P'},            {"HAL", "Hawaiian Airlines", 'P'},
    {"SKW", "SkyWest Airlines", 'P'},         {"ENY", "Envoy Air", 'P'},
    {"RPA", "Republic Airways", 'P'},         {"JIA", "PSA Airlines", 'P'},
    {"EDV", "Endeavor Air", 'P'},             {"ASH", "Mesa Airlines", 'P'},
    {"ACA", "Air Canada", 'P'},               {"WJA", "WestJet", 'P'},
    {"TSC", "Air Transat", 'P'},              {"ROU", "Air Canada Rouge", 'P'},
    {"JZA", "Jazz Aviation", 'P'},            {"SWG", "Sunwing Airlines", 'P'},
    {"POE", "Porter Airlines", 'P'},
    {"FDX", "FedEx", 'C'},                    {"UPS", "UPS Airlines", 'C'},
    {"GTI", "Atlas Air", 'C'},                {"ABX", "ABX Air", 'C'},
    {"CKS", "Kalitta Air", 'C'},              {"NCR", "National Airlines", 'C'},
    {"WGN", "Western Global Airlines", 'C'},  {"PAC", "Polar Air Cargo", 'C'},
    // Europa
    {"AFR", "Air France", 'P'},               {"KLM", "KLM", 'P'},
    {"DLH", "Lufthansa", 'P'},                {"BAW", "British Airways", 'P'},
    {"IBE", "Iberia", 'P'},                   {"IBS", "Iberia Express", 'P'},
    {"AEA", "Air Europa", 'P'},               {"VLG", "Vueling", 'P'},
    {"TAP", "TAP Air Portugal", 'P'},         {"ITY", "ITA Airways", 'P'},
    {"AZA", "Alitalia", 'P'},                 {"SWR", "Swiss", 'P'},
    {"AUA", "Austrian Airlines", 'P'},        {"BEL", "Brussels Airlines", 'P'},
    {"SAS", "SAS Scandinavian", 'P'},         {"FIN", "Finnair", 'P'},
    {"RYR", "Ryanair", 'P'},                  {"EZY", "easyJet", 'P'},
    {"EJU", "easyJet Europe", 'P'},           {"WZZ", "Wizz Air", 'P'},
    {"THY", "Turkish Airlines", 'P'},         {"PGT", "Pegasus Airlines", 'P'},
    {"AFL", "Aeroflot", 'P'},                 {"LOT", "LOT Polish Airlines", 'P'},
    {"EIN", "Aer Lingus", 'P'},               {"VIR", "Virgin Atlantic", 'P'},
    {"CFG", "Condor", 'P'},                   {"EWG", "Eurowings", 'P'},
    {"NAX", "Norwegian", 'P'},                {"TVF", "Transavia France", 'P'},
    {"TRA", "Transavia", 'P'},                {"ICE", "Icelandair", 'P'},
    {"AEE", "Aegean Airlines", 'P'},
    {"CLX", "Cargolux", 'C'},                 {"BOX", "AeroLogic", 'C'},
    {"GEC", "Lufthansa Cargo", 'C'},          {"MPH", "Martinair Cargo", 'C'},
    {"BCS", "European Air Transport (DHL)", 'C'}, {"DHK", "DHL Air UK", 'C'},
    // Oriente Medio e Africa
    {"UAE", "Emirates", 'P'},                 {"QTR", "Qatar Airways", 'P'},
    {"ETD", "Etihad Airways", 'P'},           {"SVA", "Saudia", 'P'},
    {"ETH", "Ethiopian Airlines", 'P'},       {"RAM", "Royal Air Maroc", 'P'},
    {"SAA", "South African Airways", 'P'},    {"MSR", "EgyptAir", 'P'},
    {"ELY", "El Al", 'P'},                    {"KQA", "Kenya Airways", 'P'},
    {"RJA", "Royal Jordanian", 'P'},          {"GFA", "Gulf Air", 'P'},
    {"OMA", "Oman Air", 'P'},                 {"KAC", "Kuwait Airways", 'P'},
    {"MEA", "Middle East Airlines", 'P'},     {"TAR", "Tunisair", 'P'},
    {"DAH", "Air Algérie", 'P'},              {"DTA", "TAAG Angola", 'P'},
    {"LAM", "LAM Moçambique", 'P'},           {"DHX", "DHL International", 'C'},
    // Asia e Oceania
    {"SIA", "Singapore Airlines", 'P'},       {"CPA", "Cathay Pacific", 'P'},
    {"JAL", "Japan Airlines", 'P'},           {"ANA", "All Nippon Airways", 'P'},
    {"KAL", "Korean Air", 'P'},               {"AAR", "Asiana Airlines", 'P'},
    {"CCA", "Air China", 'P'},                {"CES", "China Eastern", 'P'},
    {"CSN", "China Southern", 'P'},           {"CHH", "Hainan Airlines", 'P'},
    {"QFA", "Qantas", 'P'},                   {"ANZ", "Air New Zealand", 'P'},
    {"VOZ", "Virgin Australia", 'P'},         {"JST", "Jetstar", 'P'},
    {"AIC", "Air India", 'P'},                {"THA", "Thai Airways", 'P'},
    {"MAS", "Malaysia Airlines", 'P'},        {"GIA", "Garuda Indonesia", 'P'},
    {"EVA", "EVA Air", 'P'},                  {"CAL", "China Airlines", 'P'},
    {"PAL", "Philippine Airlines", 'P'},      {"HVN", "Vietnam Airlines", 'P'},
    {"VJC", "VietJet Air", 'P'},              {"AXM", "AirAsia", 'P'},
    {"IGO", "IndiGo", 'P'},                   {"SEJ", "SpiceJet", 'P'},
    {"ALK", "SriLankan Airlines", 'P'},       {"PIA", "Pakistan International", 'P'},
    {"BBC", "Biman Bangladesh", 'P'},         {"CAO", "Air China Cargo", 'C'},
    // aviacao executiva (fretamento / propriedade compartilhada)
    {"EJA", "NetJets", 'E'},                  {"NJE", "NetJets Europe", 'E'},
    {"LXJ", "Flexjet", 'E'},                  {"VJT", "VistaJet", 'E'},
};

// Indicativos militares (as 3 ou 4 primeiras letras)
struct Militar { const char* pref; const char* nome; };
const Militar kMilitares[] = {
    {"FAB", "Força Aérea Brasileira"},   {"RCH", "Força Aérea dos EUA"},
    {"SAM", "Força Aérea dos EUA"},      {"CNV", "Marinha dos EUA"},
    {"PAT", "Exército dos EUA"},         {"RRR", "Força Aérea Real (Reino Unido)"},
    {"CTM", "Força Aérea Francesa"},     {"GAF", "Força Aérea Alemã"},
    {"IAM", "Força Aérea Italiana"},     {"AME", "Força Aérea Espanhola"},
    {"BAF", "Força Aérea Belga"},        {"NAF", "Força Aérea Holandesa"},
    {"CFC", "Forças Canadenses"},        {"ASY", "Força Aérea Australiana"},
    {"PLF", "Força Aérea Polonesa"},     {"HAF", "Força Aérea Grega"},
    {"NATO", "OTAN"},
};

std::string maiusculas(const std::string& s)
{
    std::string r;
    for (unsigned char c : s) {
        if (c == 0xC3) continue;                 // UTF-8 de acentos latinos: tira o prefixo
        if (c >= 0x80 && c <= 0xBF) {            // e troca a letra acentuada pela sem acento
            static const char kSem[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
            c = (unsigned char)kSem[c - 0x80];
        }
        r += (char)std::toupper(c);
    }
    return r;
}

bool contem(const std::string& s, const char* x) { return s.find(x) != std::string::npos; }

std::string soAlnum(const std::string& s)
{
    std::string r;
    for (unsigned char c : s) if (std::isalnum(c)) r += (char)std::toupper(c);
    return r;
}

// o indicativo e uma matricula? (PRABC, PTXYZ, PPABC, N123AB, CGABC...)
bool pareceMatricula(const std::string& voo, const std::string& prefixo)
{
    if (voo.empty()) return false;
    if (!prefixo.empty() && soAlnum(prefixo) == voo) return true;
    // Brasil: PP, PR, PS, PT, PU + 3 letras
    if (voo.size() == 5 && voo[0] == 'P' && std::strchr("PRSTU", voo[1]) &&
        std::isalpha((unsigned char)voo[2]) && std::isalpha((unsigned char)voo[3]) && std::isalpha((unsigned char)voo[4]))
        return true;
    // EUA: N + digito + ate 4 alfanumericos
    if (voo.size() >= 2 && voo.size() <= 6 && voo[0] == 'N' && std::isdigit((unsigned char)voo[1])) return true;
    return false;
}

const Companhia* companhia(const std::string& cod)
{
    if (cod.size() != 3) return nullptr;
    for (const auto& c : kCompanhias)
        if (cod == c.icao) return &c;
    return nullptr;
}

} // namespace

const char* nomeCompanhia(const std::string& codigoIcao)
{
    const Companhia* c = companhia(codigoIcao);
    return c ? c->nome : nullptr;
}

std::string nomeCategoriaAdsb(int categoria)
{
    const int tc = categoria / 8, ca = categoria % 8;
    if (tc == 4) {
        switch (ca) {
        case 1: return "leve";
        case 2: return "pequeno";
        case 3: return "grande";
        case 4: return "grande (757)";
        case 5: return "pesado";
        case 6: return "alta performance";
        case 7: return "helicóptero";
        default: return "";
        }
    }
    if (tc == 3) {
        switch (ca) {
        case 1: return "planador";
        case 2: return "balão / dirigível";
        case 3: return "paraquedista";
        case 4: return "ultraleve";
        case 6: return "drone";
        case 7: return "veículo espacial";
        default: return "";
        }
    }
    if (tc == 2) return "veículo no aeroporto";
    return "";
}

ClasseAviao classificarAviao(const std::string& voo, const std::string& prefixo, uint32_t icao,
                             const std::string& operadorIcao, const std::string& dono, int categoria)
{
    ClasseAviao r;
    const std::string cat = nomeCategoriaAdsb(categoria);
    auto comCat = [&](const std::string& s) {
        // helicoptero, planador, drone... entram entre parenteses
        if (cat.empty() || cat == "leve" || cat == "pequeno" || cat == "grande" || cat == "grande (757)" ||
            cat == "pesado" || cat == "alta performance")
            return s;
        return s + " (" + cat + ")";
    };
    // o prefixo de 3 letras do voo (ou 4: NATO), seguido de numero
    std::string pref;
    {
        size_t i = 0;
        while (i < voo.size() && std::isalpha((unsigned char)voo[i])) ++i;
        if ((i == 3 || i == 4) && i < voo.size() && std::isdigit((unsigned char)voo[i])) pref = voo.substr(0, i);
    }
    const std::string d = maiusculas(dono);

    // 1) militar pelo indicativo, pela faixa dos EUA ou pelo dono
    for (const auto& m : kMilitares)
        if (pref == m.pref) { r.classe = "militar"; r.empresa = comCat(std::string("Militar - ") + m.nome); return r; }
    if (icao >= 0xADF7C8u && icao <= 0xAFFFFFu) { r.classe = "militar"; r.empresa = comCat("Militar (EUA)"); return r; }
    if (contem(d, "FORCA AEREA") || contem(d, "COMANDO DA AERONAUTICA") || contem(d, "AIR FORCE") ||
        contem(d, "MARINHA") || contem(d, "EXERCITO") || contem(d, "NAVY") || contem(d, "ARMY") ||
        contem(d, "MINISTERIO DA DEFESA") || contem(d, "MINISTRY OF DEFEN")) {
        r.classe = "militar"; r.empresa = comCat("Militar - " + dono); return r;
    }
    // 2) companhia pelo indicativo do voo, depois pelo operador do cadastro
    const Companhia* c = companhia(pref.size() == 3 ? pref : std::string());
    if (!c) c = companhia(operadorIcao);
    if (c) {
        r.classe = c->tipo == 'C' ? "carga" : c->tipo == 'E' ? "executiva" : "comercial";
        r.empresa = c->tipo == 'C' ? std::string(c->nome) + " (carga)"
                  : c->tipo == 'E' ? std::string("Executiva - ") + c->nome : std::string(c->nome);
        return r;
    }
    // 3) pelo dono do cadastro
    if (contem(d, "POLICIA") || contem(d, "POLICE") || contem(d, "BOMBEIRO") || contem(d, "GOVERNO") ||
        contem(d, "GOVERNMENT") || contem(d, "SECRETARIA") || contem(d, "ESTADO DE ") || contem(d, "PREFEITURA") ||
        contem(d, "DEPARTAMENTO DE POLICIA") || contem(d, "RECEITA FEDERAL") || contem(d, "IBAMA")) {
        r.classe = "governo"; r.empresa = comCat("Governo - " + dono); return r;
    }
    if (contem(d, "TAXI AEREO") || contem(d, "TAXI-AEREO") || contem(d, "AIR TAXI") || contem(d, "AEROTAXI")) {
        r.classe = "taxi"; r.empresa = comCat("Táxi aéreo - " + dono); return r;
    }
    if (contem(d, "LINHAS AEREAS") || contem(d, "AIRLINES") || contem(d, "AIRWAYS") || contem(d, "AIR LINES") ||
        contem(d, "TRANSPORTES AEREOS")) {
        r.classe = "comercial"; r.empresa = dono; return r;
    }
    // 4) o indicativo e a matricula: aviacao geral (particular)
    if (pareceMatricula(voo, prefixo)) { r.classe = "particular"; r.empresa = comCat("Particular"); return r; }
    // 5) so a categoria
    if (cat == "leve" || cat == "ultraleve" || cat == "planador" || cat == "helicóptero" || cat == "balão / dirigível") {
        r.classe = "particular"; r.empresa = comCat("Particular"); return r;
    }
    if (cat == "drone") { r.empresa = "Drone"; return r; }
    if (!pref.empty()) r.empresa = pref + " (código não cadastrado)";
    return r;
}

} // namespace masdr
