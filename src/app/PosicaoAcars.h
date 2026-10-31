#pragma once
// ---------------------------------------------------------------------------
//  Posicao escrita no texto de uma mensagem ACARS / VDL2 (o RADAR AEREO usa)
//
//  As aeronaves mandam a posicao no formato do FMS: hemisferio + graus +
//  minutos, sem ponto:
//    S22085W043046     -> 22 graus 08,5 min S, 043 graus 04,6 min W   (minutos com decimo)
//    S233260W0441234   -> 23 graus 32,60 min S, 044 graus 12,34 min W (minutos com centesimo)
//  (relatorios POS, FPN "/RF...", DFDR "C1...", etc.). O libacars, quando
//  decodifica ADS-C, escreve "Lat: -23.5432" e "Lon: -44.2057" - vale tambem.
//  O dumpvdl2, no XID que o aviao manda ao trocar de estacao de terra, escreve
//  "Aircraft location: 21.5S 42.0W 30000 ft" (graus com decimal, hemisferio
//  depois do numero, e a altitude) - vale tambem, com a altitude.
//  So a PRIMEIRA posicao da mensagem conta (nas rotas, as seguintes sao os
//  pontos do caminho).
// ---------------------------------------------------------------------------
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>

namespace masdr {

// altFt (opcional): recebe a altitude em pes quando a mensagem traz (senao fica como estava)
inline bool posicaoAcars(const std::string& b, double& la, double& lo, int* altFt = nullptr)
{
    // 0) "location: 21.5S 42.0W 30000 ft" (dumpvdl2, XID)
    for (size_t pl = b.find("location:"); pl != std::string::npos; pl = b.find("location:", pl + 9)) {
        const char* p = b.c_str() + pl + 9;
        char* f = nullptr;
        const double x = std::strtod(p, &f);
        if (f == p || (*f != 'N' && *f != 'S')) continue;
        const char h1 = *f;
        p = f + 1;
        const double y = std::strtod(p, &f);
        if (f == p || (*f != 'E' && *f != 'W')) continue;
        const char h2 = *f;
        if (x > 90 || y > 180 || x < 0 || y < 0 || (x < 0.01 && y < 0.01)) continue;
        la = h1 == 'S' ? -x : x;
        lo = h2 == 'W' ? -y : y;
        if (altFt) {
            p = f + 1;
            const long a = std::strtol(p, &f, 10);
            while (*f == ' ') ++f;
            if (f != p && f[0] == 'f' && f[1] == 't' && a > -2000 && a < 70000) *altFt = (int)a;
        }
        return true;
    }
    // 1) "Lat: x" ... "Lon: y" (libacars)
    {
        const auto pa = b.find("Lat:"), po = b.find("Lon:");
        if (pa != std::string::npos && po != std::string::npos) {
            char* f1 = nullptr; char* f2 = nullptr;
            const double x = std::strtod(b.c_str() + pa + 4, &f1), y = std::strtod(b.c_str() + po + 4, &f2);
            if (f1 != b.c_str() + pa + 4 && f2 != b.c_str() + po + 4 && std::fabs(x) <= 90 && std::fabs(y) <= 180 &&
                (std::fabs(x) > 0.01 || std::fabs(y) > 0.01)) {
                la = x; lo = y;
                return true;
            }
        }
    }
    // 2) [NS]DDMMm[EW]DDDMMm  ou  [NS]DDMMmm[EW]DDDMMmm
    auto digitos = [&](size_t i) { size_t k = i; while (k < b.size() && std::isdigit((unsigned char)b[k])) ++k; return k - i; };
    auto num = [&](size_t i, size_t n) { return std::atof(b.substr(i, n).c_str()); };
    for (size_t i = 0; i + 13 <= b.size(); ++i) {
        const char h1 = b[i];
        if (h1 != 'N' && h1 != 'S') continue;
        const size_t k = digitos(i + 1);
        if (k != 5 && k != 6) continue;
        const char h2 = b[i + 1 + k];
        if (h2 != 'E' && h2 != 'W') continue;
        if (digitos(i + 2 + k) != k + 1) continue;
        const double div = k == 5 ? 10.0 : 100.0;
        const double gLa = num(i + 1, 2), mLa = num(i + 3, k - 2) / div;
        const double gLo = num(i + 2 + k, 3), mLo = num(i + 5 + k, k - 2) / div;
        if (gLa > 90 || mLa >= 60 || gLo > 180 || mLo >= 60) continue;
        const double x = (gLa + mLa / 60.0) * (h1 == 'S' ? -1 : 1), y = (gLo + mLo / 60.0) * (h2 == 'W' ? -1 : 1);
        if (std::fabs(x) < 0.01 && std::fabs(y) < 0.01) continue;
        la = x; lo = y;
        return true;
    }
    return false;
}

} // namespace masdr
