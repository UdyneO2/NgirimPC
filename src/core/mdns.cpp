#include "mdns.h"
#include <algorithm>
#include <chrono>
#include <cstring>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace ng {

static const char* kGroup = "224.0.0.251";
static const uint16_t kPort = 5353;
static const char* kService[] = {"_ngirim", "_tcp", "local"};

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string key(const std::vector<std::string>& labels) {
    std::string k;
    for (auto& l : labels) { k += lower(l); k += '\x1f'; }
    return k;
}
static std::vector<std::string> serviceName() { return {kService[0], kService[1], kService[2]}; }
static bool isService(const std::vector<std::string>& n) { return key(n) == key(serviceName()); }

// ------------------------------------------------------------------ DNS
static bool readName(const uint8_t* p, size_t n, size_t& pos, std::vector<std::string>& out) {
    size_t i = pos, endPos = 0;
    bool jumped = false;
    int hops = 0;
    while (true) {
        if (i >= n) return false;
        uint8_t len = p[i];
        if (len == 0) { i++; if (!jumped) endPos = i; break; }
        if ((len & 0xC0) == 0xC0) {
            if (i + 1 >= n) return false;
            size_t off = (size_t)((len & 0x3F) << 8) | p[i + 1];
            if (!jumped) endPos = i + 2;
            jumped = true;
            if (++hops > 16 || off >= n) return false;
            i = off;
            continue;
        }
        if (len & 0xC0) return false;
        i++;
        if (i + len > n) return false;
        out.emplace_back(reinterpret_cast<const char*>(p + i), len);
        i += len;
    }
    pos = endPos;
    return true;
}

static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

bool parseDns(const uint8_t* p, size_t n, DnsMsg& out) {
    if (n < 12) return false;
    out = DnsMsg();
    out.response = (p[2] & 0x80) != 0;
    uint16_t qd = rd16(p + 4), an = rd16(p + 6), ns = rd16(p + 8), ar = rd16(p + 10);
    size_t pos = 12;
    for (int i = 0; i < qd; i++) {
        std::vector<std::string> nm;
        if (!readName(p, n, pos, nm) || pos + 4 > n) return false;
        out.questions.emplace_back(nm, rd16(p + pos));
        pos += 4;
    }
    int total = an + ns + ar;
    for (int i = 0; i < total; i++) {
        DnsRec r;
        if (!readName(p, n, pos, r.name) || pos + 10 > n) return false;
        r.type = rd16(p + pos);
        r.cls = rd16(p + pos + 2) & 0x7FFF;
        r.ttl = rd32(p + pos + 4);
        size_t rl = rd16(p + pos + 8);
        pos += 10;
        if (pos + rl > n) return false;
        size_t rp = pos;
        if (r.type == 12) readName(p, n, rp, r.target);
        else if (r.type == 33 && rl >= 7) { r.port = rd16(p + pos + 4); rp = pos + 6; readName(p, n, rp, r.target); }
        else if (r.type == 1 && rl == 4) {
            char b[16];
            std::snprintf(b, sizeof b, "%u.%u.%u.%u", p[pos], p[pos + 1], p[pos + 2], p[pos + 3]);
            r.ipv4 = b;
        }
        pos += rl;
        out.records.push_back(std::move(r));
    }
    return true;
}

static void putName(std::vector<uint8_t>& o, const std::vector<std::string>& labels) {
    for (auto& l : labels) {
        size_t len = std::min<size_t>(l.size(), 63);
        // jangan memotong di tengah karakter UTF-8
        while (len > 0 && len < l.size() && ((unsigned char)l[len] & 0xC0) == 0x80) len--;
        o.push_back((uint8_t)len);
        o.insert(o.end(), l.begin(), l.begin() + (long)len);
    }
    o.push_back(0);
}
static void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); }
static void put32(std::vector<uint8_t>& o, uint32_t v) { put16(o, (uint16_t)(v >> 16)); put16(o, (uint16_t)v); }

std::vector<uint8_t> buildQuery() {
    std::vector<uint8_t> o;
    put16(o, 0); put16(o, 0); put16(o, 1); put16(o, 0); put16(o, 0); put16(o, 0);
    putName(o, serviceName());
    put16(o, 12); put16(o, 1);
    return o;
}

std::vector<uint8_t> buildAnnounce(const std::string& instance, const std::string& hostLabel, uint16_t port,
                                   const std::string& ipv4, uint32_t ttl) {
    std::vector<uint8_t> o;
    put16(o, 0); put16(o, 0x8400); put16(o, 0); put16(o, 4); put16(o, 0); put16(o, 0);
    std::vector<std::string> inst = {instance, kService[0], kService[1], kService[2]};
    std::vector<std::string> host = {hostLabel, "local"};
    auto rec = [&](const std::vector<std::string>& nm, uint16_t type, uint16_t cls, const std::vector<uint8_t>& rd) {
        putName(o, nm); put16(o, type); put16(o, cls); put32(o, ttl); put16(o, (uint16_t)rd.size());
        o.insert(o.end(), rd.begin(), rd.end());
    };
    std::vector<uint8_t> ptr; putName(ptr, inst);
    rec(serviceName(), 12, 1, ptr);
    std::vector<uint8_t> srv; put16(srv, 0); put16(srv, 0); put16(srv, port); putName(srv, host);
    rec(inst, 33, 0x8001, srv);
    rec(inst, 16, 0x8001, std::vector<uint8_t>{0});
    unsigned a = 0, b = 0, c = 0, d = 0;
    std::sscanf(ipv4.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
    rec(host, 1, 0x8001, std::vector<uint8_t>{(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d});
    return o;
}

// ------------------------------------------------------------------ Mdns
Mdns::Mdns(std::function<void(const std::string&)> log) : log_(std::move(log)) { netInit(); }
Mdns::~Mdns() { stop(); }

static std::string hostLabelFor(const std::string& name) {
    std::string h = "ngirim-";
    for (char c : name) if (std::isalnum((unsigned char)c) && (unsigned char)c < 128) h += (char)std::tolower((unsigned char)c);
    return h.size() > 7 ? h : "ngirim-pc";
}

void Mdns::start() {
    if (running_.exchange(true)) return;
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == NG_BAD_SOCK) { running_ = false; if (log_) log_("ERR mDNS: gagal membuat soket"); return; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#if defined(SO_REUSEPORT) && !defined(_WIN32)
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*)&one, sizeof one);
#endif
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(kPort);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) {
        sockClose(s);
        running_ = false;
        if (log_) log_("ERR mDNS: port 5353 tidak bisa dipakai - pakai IP manual");
        return;
    }
    unsigned char mttl = 255, mloop = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&mttl, sizeof mttl);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&mloop, sizeof mloop);
    sock_ = s;
    refreshIfaces();
    th_ = std::thread([this] { loop(); });
}

void Mdns::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
    if (sock_ != NG_BAD_SOCK) { sockClose(sock_); sock_ = NG_BAD_SOCK; }
}

void Mdns::refreshIfaces() {
    std::vector<std::string> ips = localIPv4();
    for (auto& ip : ips) {
        ip_mreq m;
        m.imr_multiaddr.s_addr = inet_addr(kGroup);
        m.imr_interface.s_addr = inet_addr(ip.c_str());
        setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&m, sizeof m);  // gagal bila sudah anggota: abaikan
    }
    std::lock_guard<std::mutex> l(mu_);
    ips_ = ips;
}

void Mdns::sendAll(const std::vector<uint8_t>& pkt, const std::string& ip) {
    sockaddr_in to;
    std::memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(kPort);
    to.sin_addr.s_addr = inet_addr(kGroup);
    in_addr ifa;
    ifa.s_addr = inet_addr(ip.c_str());
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ifa, sizeof ifa);
    sendto(sock_, (const char*)pkt.data(), (int)pkt.size(), 0, (sockaddr*)&to, sizeof to);
}

void Mdns::sendQuery() {
    std::vector<std::string> ips;
    { std::lock_guard<std::mutex> l(mu_); ips = ips_; }
    auto q = buildQuery();
    for (auto& ip : ips) sendAll(q, ip);
}

void Mdns::sendAnnounce(uint32_t ttl) {
    std::vector<std::string> ips;
    std::string name, host;
    int port;
    { std::lock_guard<std::mutex> l(mu_); ips = ips_; name = annName_; host = annHost_; port = annPort_; }
    if (port <= 0) return;
    for (auto& ip : ips) sendAll(buildAnnounce(name, host, (uint16_t)port, ip, ttl), ip);
}

void Mdns::query() { wantQuery_ = true; }

void Mdns::announce(const std::string& name, int port) {
    {
        std::lock_guard<std::mutex> l(mu_);
        annName_ = name; annHost_ = hostLabelFor(name); annPort_ = port;
    }
    if (running_) { sendAnnounce(120); }
}

void Mdns::unannounce() {
    if (running_) sendAnnounce(0);  // goodbye
    std::lock_guard<std::mutex> l(mu_);
    annPort_ = 0;
}

void Mdns::loop() {
    int64_t lastQ = 0, lastIf = nowMs();
    while (running_) {
        int64_t t = nowMs();
        if (wantQuery_.exchange(false) || t - lastQ > 4000) { sendQuery(); lastQ = t; }
        if (t - lastIf > 10000) { refreshIfaces(); lastIf = t; }
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(sock_, &rf);
        timeval tv{0, 300000};
        int n = select((int)sock_ + 1, &rf, nullptr, nullptr, &tv);
        if (n <= 0) continue;
        uint8_t buf[2048];
        sockaddr_in from;
        socklen_t fl = sizeof from;
        int r = (int)recvfrom(sock_, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
        if (r <= 0) continue;
        char ipb[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ipb, sizeof ipb);
        handle(buf, (size_t)r, ipb);
    }
}

void Mdns::handle(const uint8_t* p, size_t n, const std::string& src) {
    DnsMsg m;
    if (!parseDns(p, n, m)) return;
    int64_t t = nowMs();
    if (!m.response) {
        std::string name, host;
        int port;
        { std::lock_guard<std::mutex> l(mu_); name = annName_; host = annHost_; port = annPort_; }
        if (port <= 0 || t - lastReply_ < 800) return;
        std::vector<std::string> inst = {name, kService[0], kService[1], kService[2]};
        std::vector<std::string> hn = {host, "local"};
        for (auto& q : m.questions) {
            bool hit = (isService(q.first) && (q.second == 12 || q.second == 255)) ||
                       (key(q.first) == key(inst) && (q.second == 33 || q.second == 16 || q.second == 255)) ||
                       (key(q.first) == key(hn) && (q.second == 1 || q.second == 255));
            if (hit) { lastReply_ = t; sendAnnounce(120); return; }
        }
        return;
    }
    std::lock_guard<std::mutex> l(mu_);
    for (auto& r : m.records) {
        if (r.type == 12 && isService(r.name) && !r.target.empty()) {
            std::string k = key(r.target);
            if (r.ttl == 0) { inst_.erase(k); continue; }
            Inst& i = inst_[k];
            i.name = r.target[0];
            i.seen = t;
            i.srcIp = src;
        } else if (r.type == 33 && r.name.size() == 4 && !r.target.empty()) {
            auto it = inst_.find(key(r.name));
            if (it == inst_.end()) {
                if (r.ttl == 0) continue;
                it = inst_.emplace(key(r.name), Inst()).first;
                it->second.name = r.name[0];
            }
            it->second.port = r.port;
            it->second.targetKey = key(r.target);
            it->second.seen = t;
            it->second.srcIp = src;
        } else if (r.type == 1 && !r.ipv4.empty()) {
            hosts_[key(r.name)] = r.ipv4;
        }
    }
}

std::vector<MdnsPeer> Mdns::peers() const {
    std::vector<MdnsPeer> out;
    int64_t t = nowMs();
    std::lock_guard<std::mutex> l(mu_);
    for (auto& kv : inst_) {
        const Inst& i = kv.second;
        if (i.port <= 0 || t - i.seen > 30000) continue;
        auto h = hosts_.find(i.targetKey);
        std::string ip = h != hosts_.end() ? h->second : i.srcIp;
        if (ip.empty()) continue;
        if (annPort_ > 0 && i.port == annPort_ && lower(i.name) == lower(annName_)) continue;  // diri sendiri
        out.push_back({i.name, ip, i.port});
    }
    std::sort(out.begin(), out.end(), [](const MdnsPeer& a, const MdnsPeer& b) { return lower(a.name) < lower(b.name); });
    return out;
}

}  // namespace ng
