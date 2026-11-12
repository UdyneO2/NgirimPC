// Soket TCP berpenyangga + primitif DataInputStream/DataOutputStream Java (big-endian, modified UTF-8).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
static const sock_t NG_BAD_SOCK = INVALID_SOCKET;
#else
typedef int sock_t;
static const sock_t NG_BAD_SOCK = -1;
#endif

namespace ng {

struct NetError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/** Koneksi putus / waktu habis / gagal tersambung: boleh dicoba lanjut (resume). */
struct NetDropped : NetError {
    using NetError::NetError;
};

void netInit();                                   // WSAStartup (Windows), no-op di tempat lain
void sockShutdown(sock_t s);                      // bangunkan thread yang sedang blok di soket ini
void sockClose(sock_t s);
sock_t netConnect(const std::string& host, int port, int timeoutMs);  // melempar NetError
sock_t netListen(int& port);                      // port 0 = acak; diisi port sebenarnya
sock_t netAccept(sock_t listener, std::string& peerIp);  // NG_BAD_SOCK bila listener ditutup
std::vector<std::string> localIPv4();             // alamat IPv4 antarmuka aktif (bukan loopback)

std::string toModifiedUtf8(const std::string& utf8);
std::string fromModifiedUtf8(const std::string& mutf8);

class Conn {
public:
    explicit Conn(sock_t s);
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    sock_t fd() const { return s_; }
    void close();
    void setTimeoutMs(int ms);

    void write(const void* p, size_t n);
    void flush();
    void writeByte(int v);
    void writeInt(int32_t v);
    void writeLong(int64_t v);
    void writeUTF(const std::string& utf8);

    void readFully(void* p, size_t n);
    size_t readSome(void* p, size_t n);  // minimal 1 byte; melempar bila koneksi putus
    int readByte();
    int32_t readInt();
    int64_t readLong();
    std::string readUTF();

private:
    sock_t s_;
    std::vector<uint8_t> out_;
    std::vector<uint8_t> in_;
    size_t inPos_ = 0, inLen_ = 0;
    void sendAll(const uint8_t* p, size_t n);
};

}  // namespace ng
