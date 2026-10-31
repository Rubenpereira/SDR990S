#pragma once
// ---------------------------------------------------------------------------
//  AdsbEmpresas - quem e o aviao: a companhia aerea (pelo indicativo do voo
//  ou pelo operador do cadastro), ou militar, governo, taxi aereo, executiva,
//  particular. Tudo local (tabela embutida), sem internet.
//
//  * Voo comercial: o indicativo e o codigo ICAO da companhia (3 letras) +
//    o numero do voo: GLO1234 = GOL, TAM3456 = LATAM, AZU4021 = Azul.
//  * Militar: os indicativos das forcas (FAB, RCH, CNV, RRR...) e a faixa de
//    enderecos militares dos EUA (ADF7C8-AFFFFF).
//  * Particular: o indicativo e a propria matricula (PRABC, PTXYZ, N123AB) -
//    e assim que a aviacao geral voa.
//  * O cadastro (hexdb.io) ajuda: o codigo do operador (OperatorFlagCode) e
//    o nome do dono (Forca Aerea, Policia, Taxi Aereo...).
//  * A categoria do ADS-B (TC 1-4) diz se e helicoptero, planador, drone...
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>

namespace masdr {

struct ClasseAviao {
    std::string empresa;    // o texto da coluna "Empresa"
    std::string classe;     // comercial, carga, executiva, militar, governo, taxi, particular (ou vazio)
};

// categoria: tc*8 + ca do ADS-B (0 = desconhecida)
ClasseAviao classificarAviao(const std::string& voo, const std::string& prefixo, uint32_t icao,
                             const std::string& operadorIcao, const std::string& dono, int categoria);
std::string nomeCategoriaAdsb(int categoria);   // "jato grande", "helicoptero"... ("" se nao sabe)
const char* nomeCompanhia(const std::string& codigoIcao);   // nullptr se nao conhece
// O ACARS / VDL2 / HFDL trazem o voo com o codigo IATA da companhia (G31017 = GOL 1017):
// devolve o indicativo ICAO (GLO1017) ou "" se nao sabe converter.
std::string vooParaIcao(const std::string& voo, const std::string& prefixo);

} // namespace masdr
