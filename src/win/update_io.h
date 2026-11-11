// Menghubungkan ng::Updater (portabel) dengan implementasi Windows (WinHTTP, penggantian exe).
// Pemakaian di UI:  static ng::Updater up(kVersion, plat::makeUpdaterIO());  up.check(pre);
#pragma once
#include "../core/updater.h"
#include "plat.h"

namespace plat {

inline ng::UpdaterIO makeUpdaterIO() {
    ng::UpdaterIO io;
    io.httpGet = [](const std::string& u, std::string& b, std::string& e) { return httpGet(u, b, e); };
    io.httpDownload = [](const std::string& u, const std::string& d, const std::function<void(int64_t, int64_t)>& p,
                         const std::atomic<bool>& c, std::string& e) { return httpDownload(u, d, p, c, e); };
    io.applyUpdate = [](const std::string& f, std::string& e) { return applyUpdate(f, e); };
    io.tempDir = [] { return tempDir(); };
    return io;
}

}  // namespace plat
