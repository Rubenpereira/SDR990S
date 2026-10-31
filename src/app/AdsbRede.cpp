#include "AdsbRede.h"
#include "../ui/Idioma.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cstdio>

namespace masdr {

namespace {
constexpr uintptr_t kSemSock = ~(uintptr_t)0;
std::string fmt(const char* f, const std::string& a)
{
    char b[256];
    std::snprintf(b, sizeof b, f, a.c_str());
    return b;
}
} // namespace

void AdsbRede::sit(const std::string& s)
{
    std::lock_guard<std::mutex> lk(m_);
    sit_ = s;
}

std::string AdsbRede::alvo() const
{
    std::lock_guard<std::mutex> lk(m_);
    return host_ + ":" + std::to_string(porta_);
}

std::string AdsbRede::situacao() const
{
    std::lock_guard<std::mutex> lk(m_);
    return sit_;
}

void AdsbRede::manter(bool ligar, const std::string& host, int porta)
{
    if (!ligar || host.empty() || porta <= 0) {
        if (vivo_ || th_.joinable()) parar();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        if (vivo_ && host == host_ && porta == porta_) return;
    }
    parar();
    {
        std::lock_guard<std::mutex> lk(m_);
        host_ = host; porta_ = porta;
    }
    core_.limpar();
    vivo_ = true;
    th_ = std::thread([this] { laco(); });
}

void AdsbRede::parar()
{
    vivo_ = false;
    const uintptr_t s = sock_.exchange(kSemSock);
    if (s != kSemSock) closesocket((SOCKET)s);              // destrava o recv/connect
    if (th_.joinable()) th_.join();
    conectado_ = false;
}

void AdsbRede::laco()
{
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    using clk = std::chrono::steady_clock;
    auto tAnt = clk::now();
    // o relogio do AdsbCore anda em tempo real (a idade de cada aviao, as mensagens/s)
    auto entregar = [&](const uint8_t* m, int nbits) {
        const auto a = clk::now();
        const double dt = std::chrono::duration<double>(a - tAnt).count();
        tAnt = a;
        core_.mensagemRede(m, nbits, dt);
    };
    auto esperar = [&](int s) {
        for (int i = 0; i < s * 4 && vivo_; ++i) {
            Sleep(250);
            if (i % 4 == 3) entregar(nullptr, 0);
        }
    };
    while (vivo_) {
        std::string host;
        int porta;
        {
            std::lock_guard<std::mutex> lk(m_);
            host = host_; porta = porta_;
        }
        const std::string nome = host + ":" + std::to_string(porta);
        sit(fmt(T("conectando a %s ..."), nome));
        addrinfo dica{}, *res = nullptr;
        dica.ai_family = AF_INET;
        dica.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(porta).c_str(), &dica, &res) != 0 || !res) {
            sit(fmt(T("endereço inválido: %s"), host));
            esperar(10);
            continue;
        }
        SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s == INVALID_SOCKET) { freeaddrinfo(res); esperar(10); continue; }
        sock_ = (uintptr_t)s;
        // conecta com no maximo 5 s de espera (aparelho desligado nao trava nada)
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        connect(s, res->ai_addr, (int)res->ai_addrlen);
        freeaddrinfo(res);
        fd_set wr, ex;
        FD_ZERO(&wr); FD_ZERO(&ex); FD_SET(s, &wr); FD_SET(s, &ex);
        timeval to{5, 0};
        bool ok = select(0, nullptr, &wr, &ex, &to) > 0 && FD_ISSET(s, &wr);
        if (ok) {
            int erro = 0, tam = sizeof erro;
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&erro, &tam);
            ok = erro == 0;
        }
        if (!ok || !vivo_) {
            if (sock_.exchange(kSemSock) != kSemSock) closesocket(s);
            if (vivo_) sit(fmt(T("sem conexão com %s - tenta de novo em 10 s"), nome));
            esperar(10);
            continue;
        }
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
        DWORD tr = 1000;                                     // recv volta a cada 1 s (o relogio anda)
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tr, sizeof tr);
        conectado_ = true;
        sit(fmt(T("conectado a %s"), nome));
        std::vector<uint8_t> buf;
        char b[8192];
        while (vivo_) {
            const int n = recv(s, b, sizeof b, 0);
            if (n > 0) {
                buf.insert(buf.end(), b, b + n);
                lerBeast(buf, entregar);
                if (buf.size() > (1u << 20)) buf.clear();       // lixo: nao e Beast
            } else if (n < 0 && WSAGetLastError() == WSAETIMEDOUT) {
                entregar(nullptr, 0);
            } else break;                                    // fechou ou caiu
        }
        conectado_ = false;
        if (sock_.exchange(kSemSock) != kSemSock) closesocket(s);
        if (vivo_) {
            sit(fmt(T("a conexão com %s caiu - tenta de novo em 10 s"), nome));
            esperar(10);
        }
    }
    WSACleanup();
}

} // namespace masdr
