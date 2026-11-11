// Operasi file lokal (portabel): daftar folder, salin/pindah dengan progres, ganti nama, ukuran.
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "proto.h"

namespace ng {

struct LocalEntry {
    std::string name;
    int64_t size = 0;
    bool dir = false;
    int64_t mtime = 0;  // detik epoch
    bool hidden = false;
};

std::vector<LocalEntry> listDir(const fs::path& dir, std::string& err);
int64_t pathSize(const fs::path& p, const std::atomic<bool>* cancel = nullptr, int64_t* fileCount = nullptr);
bool isHiddenPath(const fs::path& p);

// Salin atau pindah srcs ke dalam folder dstDir (nama bentrok diberi akhiran " (1)").
// Sinkron: panggil dari thread kerja. Melaporkan lewat cb.log/progress/finish.
void copyPaths(const std::vector<fs::path>& srcs, const fs::path& dstDir, bool move, const Callbacks& cb,
               const std::atomic<bool>& cancel);

bool renamePath(const fs::path& from, const std::string& newName, std::string& err);
bool makeDir(const fs::path& parent, const std::string& name, std::string& err);
bool removeAll(const std::vector<fs::path>& paths, const Callbacks& cb, const std::atomic<bool>& cancel);  // permanen

}  // namespace ng
