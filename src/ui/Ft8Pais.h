#pragma once
// ---------------------------------------------------------------------------
//  Ft8Pais - pais (entidade DXCC) e distancia da estacao que transmite no FT8/FT4
//
//  O pais sai do indicativo pela tabela do cty.dat (AD1C, Ft8Paises.inc):
//  primeiro o indicativo exato, depois o prefixo mais longo que casa.
//  Indicativo com barra: "F/PY1XX" e "PY1XX/F" usam o "F"; /P /M /QRP e o
//  numero de area sao ignorados; /MM e /AM (maritimo/aeronautico) ficam sem pais.
//
//  Quem transmite e o SEGUNDO indicativo da mensagem ("PY2XX PU1ABC -12" ->
//  PU1ABC); no CQ e o que vem depois do CQ (e do "DX", "SA", "POTA"...).
//  Na mensagem de DXpedition ("K1ABC RR73; W9XYZ <KH1/KH7Z> -08") e o depois
//  do ";".
//
//  Distancia: pela grade que a propria estacao mandou (CQ ... GG87, ou
//  "K1ABC W9XYZ EN37") - lembrada para as mensagens seguintes dela; sem grade,
//  aproximada pelo centro da entidade do cty.dat (aparece com "~").
// ---------------------------------------------------------------------------
#include <string>

namespace masdr {
namespace ft8geo {

struct InfoPais {
    bool ok = false;
    std::string nome;          // no idioma da tela
    double lat = 0, lon = 0;   // centro da entidade (ou o do indicativo, no cty.dat)
};

// Pais do indicativo ("<PU1ABC>" tambem serve; "<...>" nao tem pais)
const InfoPais& paisDoIndicativo(const std::string& indicativo);

// Analisa a mensagem decodificada: indicativo de quem transmite e a grade de 4
// caracteres, se ela veio na mensagem. false = texto livre / sem indicativo.
bool transmissor(const std::string& texto, std::string& indicativo, std::string& grade);

// Grade Maidenhead (4, 6 ou 8 caracteres) -> centro do quadrado
bool gradeParaGraus(const std::string& g, double& lat, double& lon);

// graus -> grade de 6 caracteres (GG87jb)
std::string grausParaGrade(double lat, double lon);

// Distancia pelo circulo maximo (km)
double distanciaKm(double la1, double lo1, double la2, double lo2);

} // namespace ft8geo
} // namespace masdr
