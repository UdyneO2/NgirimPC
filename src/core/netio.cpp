#include "netio.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <iphlpapi.h>
#include <mutex>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace ng {

static const size_t kBuf = 1 << 16;

// ------------------------------------------------------------------ init / util
void netInit() {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    });
#endif
}

void sockShutdown(sock_t s) {
    if (s == NG_BAD_SOCK) return;
#ifdef _WIN32
    shutdown(s, SD_BOTH);
#else
    shutdown(s, SHUT_RDWR);
#endif
}

void sockClose(sock_t s) {
    if (s == NG_BAD_SOCK) return;
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

static int lastErr() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool isTimeout(int e) {
#ifdef _WIN32
    return e == WSAETIMEDOUT || e == WSAEWOULDBLOCK;
#else
    return e == EAGAIN || e == EWOULDBLOCK || e == ETIMEDOUT;
#endif
}

static void setBlocking(sock_t s, bool blocking) {
#ifdef _WIN32
    u_long m = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &m);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? (fl & ~O_NONBLOCK) : (fl | O_NONBLOCK));
#endif
}

sock_t netConnect(const std::string& host, int port, int timeoutMs) {
    netInit();
    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char ps[16];
    std::snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res) throw NetError("Alamat tidak valid: " + host);

    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == NG_BAD_SOCK) { freeaddrinfo(res); throw NetError("Gagal membuat soket"); }
    setBlocking(s, false);
    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) {
        int e = lastErr();
#ifdef _WIN32
        bool pending = (e == WSAEWOULDBLOCK);
#else
        bool pending = (e == EINPROGRESS);
#endif
        if (!pending) { sockClose(s); throw NetDropped("Tidak bisa terhubung ke " + host); }
        fd_set wf, ef;
        FD_ZERO(&wf); FD_ZERO(&ef);
        FD_SET(s, &wf); FD_SET(s, &ef);
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int n = select((int)s + 1, nullptr, &wf, &ef, &tv);
        if (n <= 0) { sockClose(s); throw NetDropped("Waktu habis saat menghubungi " + host); }
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        if (err != 0 || FD_ISSET(s, &ef)) { sockClose(s); throw NetDropped("Koneksi ditolak oleh " + host); }
    }
    setBlocking(s, true);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    return s;
}

sock_t netListen(int& port) {
    netInit();
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == NG_BAD_SOCK) throw NetError("Gagal membuat soket server");
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 16) != 0) {
        sockClose(s);
        throw NetError("Gagal membuka port (firewall atau port dipakai)");
    }
    socklen_t len = sizeof a;
    getsockname(s, (sockaddr*)&a, &len);
    port = ntohs(a.sin_port);
    return s;
}

sock_t netAccept(sock_t listener, std::string& peerIp) {
    sockaddr_in a;
    socklen_t len = sizeof a;
    sock_t c = accept(listener, (sockaddr*)&a, &len);
    if (c == NG_BAD_SOCK) return NG_BAD_SOCK;
    char buf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf);
    peerIp = buf;
    int one = 1;
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    return c;
}

std::vector<std::string> localIPv4() {
    std::vector<std::string> out;
#ifdef _WIN32
    ULONG sz = 0;
    ULONG fl = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    GetAdaptersAddresses(AF_INET, fl, nullptr, nullptr, &sz);
    std::vector<uint8_t> buf(sz ? sz : 1);
    IP_ADAPTER_ADDRESSES* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_INET, fl, nullptr, aa, &sz) == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES* p = aa; p; p = p->Next) {
            if (p->OperStatus != IfOperStatusUp || p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS* u = p->FirstUnicastAddress; u; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
                sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                char b[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &sa->sin_addr, b, sizeof b);
                std::string ip = b;
                if (ip.rfind("169.254.", 0) == 0) continue;  // link-local otomatis
                out.push_back(ip);
            }
        }
    }
#else
    ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) == 0) {
        for (ifaddrs* p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
            if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
            char b[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &((sockaddr_in*)p->ifa_addr)->sin_addr, b, sizeof b);
            out.push_back(b);
        }
        freeifaddrs(ifa);
    }
#endif
    return out;
}

// ------------------------------------------------------------------ modified UTF-8 (java.io.DataOutput.writeUTF)
static void put3(std::string& o, uint32_t u) {
    o += (char)(0xE0 | (u >> 12));
    o += (char)(0x80 | ((u >> 6) & 0x3F));
    o += (char)(0x80 | (u & 0x3F));
}

std::string toModifiedUtf8(const std::string& s) {
    std::string o;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0xFFFD;
        int len = 1;
        if (c < 0x80) { cp = c; }
        else if ((c >> 5) == 6 && i + 1 < n) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); len = 2; }
        else if ((c >> 4) == 14 && i + 2 < n) { cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); len = 3; }
        else if ((c >> 3) == 30 && i + 3 < n) {
            cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            len = 4;
        }
        i += len;
        if (cp == 0) { o += (char)0xC0; o += (char)0x80; }
        else if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) put3(o, cp);
        else { cp -= 0x10000; put3(o, 0xD800 + (cp >> 10)); put3(o, 0xDC00 + (cp & 0x3FF)); }
    }
    return o;
}

static void putUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else {
        o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
        o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
    }
}

std::string fromModifiedUtf8(const std::string& s) {
    std::vector<uint16_t> u16;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { u16.push_back(c); i++; }
        else if ((c >> 5) == 6 && i + 1 < n) { u16.push_back((uint16_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F))); i += 2; }
        else if ((c >> 4) == 14 && i + 2 < n) {
            u16.push_back((uint16_t)(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F)));
            i += 3;
        } else { u16.push_back(0xFFFD); i++; }
    }
    std::string o;
    for (size_t k = 0; k < u16.size(); k++) {
        uint32_t u = u16[k];
        if (u >= 0xD800 && u < 0xDC00 && k + 1 < u16.size() && u16[k + 1] >= 0xDC00 && u16[k + 1] < 0xE000) {
            u = 0x10000 + ((u - 0xD800) << 10) + (u16[k + 1] - 0xDC00);
            k++;
        } else if (u >= 0xD800 && u < 0xE000) {
            u = 0xFFFD;
        }
        putUtf8(o, u);
    }
    return o;
}

// ------------------------------------------------------------------ Conn
Conn::Conn(sock_t s) : s_(s) {
    out_.reserve(kBuf);
    in_.resize(kBuf);
}

Conn::~Conn() { close(); }

void Conn::close() {
    if (s_ != NG_BAD_SOCK) { sockClose(s_); s_ = NG_BAD_SOCK; }
}

void Conn::setTimeoutMs(int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t);
    setsockopt(s_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof t);
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

void Conn::sendAll(const uint8_t* p, size_t n) {
    while (n > 0) {
#ifdef _WIN32
        int r = send(s_, (const char*)p, (int)(n > (1u << 30) ? (1u << 30) : n), 0);
#else
        ssize_t r = ::send(s_, p, n, MSG_NOSIGNAL);
#endif
        if (r <= 0) {
            if (r < 0 && isTimeout(lastErr())) throw NetDropped("Waktu habis saat mengirim");
            throw NetDropped("Koneksi terputus");
        }
        p += r;
        n -= (size_t)r;
    }
}

void Conn::write(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    if (out_.size() + n > kBuf) {
        flush();
        if (n >= kBuf) { sendAll(b, n); return; }
    }
    out_.insert(out_.end(), b, b + n);
}

void Conn::flush() {
    if (out_.empty()) return;
    sendAll(out_.data(), out_.size());
    out_.clear();
}

void Conn::writeByte(int v) { uint8_t b = (uint8_t)v; write(&b, 1); }

void Conn::writeInt(int32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    write(b, 4);
}

void Conn::writeLong(int64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)((uint64_t)v >> (56 - 8 * i));
    write(b, 8);
}

void Conn::writeUTF(const std::string& utf8) {
    std::string m = toModifiedUtf8(utf8);
    if (m.size() > 65535) throw NetError("String terlalu panjang");
    uint8_t h[2] = {(uint8_t)(m.size() >> 8), (uint8_t)m.size()};
    write(h, 2);
    write(m.data(), m.size());
}

size_t Conn::readSome(void* p, size_t n) {
    if (n == 0) return 0;
    if (inPos_ < inLen_) {
        size_t take = inLen_ - inPos_;
        if (take > n) take = n;
        std::memcpy(p, in_.data() + inPos_, take);
        inPos_ += take;
        return take;
    }
    char* dst = static_cast<char*>(p);
    char* target = n >= kBuf ? dst : reinterpret_cast<char*>(in_.data());
    size_t cap = n >= kBuf ? n : kBuf;
#ifdef _WIN32
    int r = recv(s_, target, (int)(cap > (1u << 30) ? (1u << 30) : cap), 0);
#else
    ssize_t r = ::recv(s_, target, cap, 0);
#endif
    if (r < 0 && isTimeout(lastErr())) throw NetDropped("Waktu habis menunggu data");
    if (r <= 0) throw NetDropped("Koneksi terputus");
    if (target == dst) return (size_t)r;
    inPos_ = 0;
    inLen_ = (size_t)r;
    size_t take = inLen_ > n ? n : inLen_;
    std::memcpy(p, in_.data(), take);
    inPos_ = take;
    return take;
}

void Conn::readFully(void* p, size_t n) {
    char* d = static_cast<char*>(p);
    while (n > 0) {
        size_t r = readSome(d, n);
        d += r;
        n -= r;
    }
}

int Conn::readByte() { uint8_t b; readFully(&b, 1); return b; }

int32_t Conn::readInt() {
    uint8_t b[4];
    readFully(b, 4);
    return (int32_t)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]);
}

int64_t Conn::readLong() {
    uint8_t b[8];
    readFully(b, 8);
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
    return (int64_t)v;
}

std::string Conn::readUTF() {
    uint8_t h[2];
    readFully(h, 2);
    size_t len = ((size_t)h[0] << 8) | h[1];
    std::string m(len, '\0');
    if (len) readFully(&m[0], len);
    return fromModifiedUtf8(m);
}

}  // namespace ng
