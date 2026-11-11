// mDNS / DNS-SD minimal untuk layanan "_ngirim._tcp" (kompatibel dengan NsdManager Android).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "netio.h"

namespace ng {

struct DnsRec {
    std::vector<std::string> name;
    uint16_t type = 0, cls = 0;
    uint32_t ttl = 0;
    std::vector<std::string> target;  // PTR / SRV
    uint16_t port = 0;                // SRV
    std::string ipv4;                 // A
};

struct DnsMsg {
    bool response = false;
    std::vector<std::pair<std::vector<std::string>, uint16_t>> questions;
    std::vector<DnsRec> records;  // answer + authority + additional
};

bool parseDns(const uint8_t* p, size_t n, DnsMsg& out);
std::vector<uint8_t> buildQuery();
std::vector<uint8_t> buildAnnounce(const std::string& instance, const std::string& hostLabel, uint16_t port,
                                   const std::string& ipv4, uint32_t ttl);

struct MdnsPeer {
    std::string name, host;
    int port = 0;
};

class Mdns {
public:
    explicit Mdns(std::function<void(const std::string&)> log);
    ~Mdns();
    void start();
    void stop();
    void query();                                       // kirim pertanyaan sekarang
    void announce(const std::string& name, int port);   // daftarkan layanan ini
    void unannounce();
    std::vector<MdnsPeer> peers() const;

private:
    struct Inst { std::string name, targetKey, ip, srcIp; int port = 0; int64_t seen = 0; };
    std::function<void(const std::string&)> log_;
    std::atomic<bool> running_{false};
    std::thread th_;
    sock_t sock_ = NG_BAD_SOCK;
    mutable std::mutex mu_;
    std::map<std::string, Inst> inst_;
    std::map<std::string, std::string> hosts_;
    std::string annName_, annHost_;
    int annPort_ = 0;
    std::vector<std::string> ips_;
    int64_t lastReply_ = 0;
    std::atomic<bool> wantQuery_{false};

    void loop();
    void refreshIfaces();
    void sendAll(const std::vector<uint8_t>& pkt, const std::string& ip);
    void sendQuery();
    void sendAnnounce(uint32_t ttl);
    void handle(const uint8_t* p, size_t n, const std::string& src);
};

}  // namespace ng
