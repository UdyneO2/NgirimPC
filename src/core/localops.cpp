#include "localops.h"

#include <algorithm>
#include <chrono>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ng {

bool isHiddenPath(const fs::path& p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
#else
    std::string n = U(p.filename());
    return !n.empty() && n[0] == '.';
#endif
}

static int64_t toEpoch(const fs::path& p) {
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec);
    if (ec) return 0;
    using namespace std::chrono;
    auto s = time_point_cast<system_clock::duration>(ft - fs::file_time_type::clock::now() + system_clock::now());
    return (int64_t)duration_cast<seconds>(s.time_since_epoch()).count();
}

std::vector<LocalEntry> listDir(const fs::path& dir, std::string& err) {
    std::vector<LocalEntry> out;
    err.clear();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { err = "Bukan folder atau tidak bisa diakses"; return out; }
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        LocalEntry e;
        std::error_code e2;
        e.name = U(it->path().filename());
        e.dir = it->is_directory(e2);
        if (!e.dir) { auto n = it->file_size(e2); e.size = e2 ? 0 : (int64_t)n; }
        e.mtime = toEpoch(it->path());
        e.hidden = isHiddenPath(it->path()) || (!e.name.empty() && e.name[0] == '.');
        out.push_back(std::move(e));
    }
    if (ec) err = "Sebagian isi tidak bisa dibaca";
    return out;
}

int64_t pathSize(const fs::path& p, const std::atomic<bool>* cancel, int64_t* fileCount) {
    std::error_code ec;
    int64_t total = 0;
    if (fs::is_regular_file(p, ec)) { if (fileCount) (*fileCount)++; auto n = fs::file_size(p, ec); return ec ? 0 : (int64_t)n; }
    if (!fs::is_directory(p, ec)) return 0;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (cancel && cancel->load()) break;
        std::error_code e2;
        if (it->is_regular_file(e2)) { auto n = it->file_size(e2); if (!e2) total += (int64_t)n; if (fileCount) (*fileCount)++; }
    }
    return total;
}

static void copyTree(const fs::path& src, const fs::path& dst, Job& job, const std::atomic<bool>& cancel) {
    if (cancel) throw Cancelled();
    std::error_code ec;
    if (fs::is_directory(src, ec)) {
        fs::create_directories(dst, ec);
        std::vector<fs::path> kids;
        for (fs::directory_iterator it(src, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            kids.push_back(it->path());
        for (auto& k : kids) copyTree(k, dst / k.filename(), job, cancel);
        return;
    }
    fs::create_directories(dst.parent_path(), ec);
    bool ok = false;
    try {
        std::ifstream in(src, std::ios::binary);
        if (!in) throw std::runtime_error("tidak bisa membuka " + U(src.filename()));
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("tidak bisa menulis " + U(dst.filename()));
        while (in) {
            in.read(job.buf.data(), (std::streamsize)job.buf.size());
            std::streamsize n = in.gcount();
            if (n <= 0) break;
            out.write(job.buf.data(), n);
            if (!out) throw std::runtime_error("gagal menulis (disk penuh?)");
            job.done += n;
            job.emit(U(src.filename()));
            if (cancel) throw Cancelled();
        }
        ok = true;
    } catch (...) {
        std::error_code e3;
        fs::remove(dst, e3);
        throw;
    }
    if (ok) {
        std::error_code e4;
        fs::last_write_time(dst, fs::last_write_time(src, e4), e4);
    }
}

void copyPaths(const std::vector<fs::path>& srcs, const fs::path& dstDir, bool move, const Callbacks& cb,
               const std::atomic<bool>& cancel) {
    int64_t total = 0;
    for (auto& s : srcs) total += pathSize(s, &cancel);
    Job job(cb, total);
    int ok = 0, failed = 0;
    std::error_code ec;
    for (auto& s : srcs) {
        if (cancel) break;
        std::string nm = U(s.filename());
        try {
            fs::path d = uniqueFile(dstDir / s.filename());
            // tidak boleh menyalin folder ke dalam dirinya sendiri
            fs::path cs = fs::weakly_canonical(s, ec), cd = fs::weakly_canonical(dstDir, ec);
            if (fs::is_directory(s, ec)) {
                fs::path rel = cd.lexically_relative(cs);
                if (!rel.empty() && *rel.begin() != "..") throw std::runtime_error("folder tujuan berada di dalam folder sumber");
            }
            bool renamed = false;
            if (move) {
                std::error_code re;
                int64_t sz = pathSize(s);
                fs::rename(s, d, re);
                if (!re) { job.done += sz; renamed = true; }
            }
            if (!renamed) {
                copyTree(s, d, job, cancel);
                if (move) fs::remove_all(s, ec);
            }
            ok++;
            if (cb.log) cb.log("OK     " + nm);
        } catch (const Cancelled&) {
            break;
        } catch (const std::exception& e) {
            failed++;
            if (cb.log) cb.log("GAGAL  " + nm + ": " + e.what());
        }
    }
    job.emit("", true);
    bool c = cancel.load();
    std::string msg = std::string(c ? "Dibatalkan: " : "Selesai: ") + std::to_string(ok) + " sukses, " + std::to_string(failed) + " gagal";
    if (cb.finish) cb.finish(!c && failed == 0, msg, job.done, total);
}

bool renamePath(const fs::path& from, const std::string& newName, std::string& err) {
    if (newName.empty() || newName.find_first_of("\\/:*?\"<>|") != std::string::npos) { err = "Nama tidak valid"; return false; }
    fs::path to = from.parent_path() / P(newName);
    std::error_code ec;
    if (fs::exists(to, ec)) { err = "Sudah ada item dengan nama itu"; return false; }
    fs::rename(from, to, ec);
    if (ec) { err = ec.message(); return false; }
    return true;
}

bool makeDir(const fs::path& parent, const std::string& name, std::string& err) {
    if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos) { err = "Nama tidak valid"; return false; }
    fs::path d = parent / P(name);
    std::error_code ec;
    if (fs::exists(d, ec)) { err = "Sudah ada item dengan nama itu"; return false; }
    if (!fs::create_directory(d, ec) || ec) { err = ec ? ec.message() : "gagal membuat folder"; return false; }
    return true;
}

bool removeAll(const std::vector<fs::path>& paths, const Callbacks& cb, const std::atomic<bool>& cancel) {
    int ok = 0, failed = 0;
    int64_t total = (int64_t)paths.size(), i = 0;
    for (auto& p : paths) {
        if (cancel) break;
        std::error_code ec;
        if (cb.progress) cb.progress(U(p.filename()), i++, total, 0);
        fs::remove_all(p, ec);
        if (ec || fs::exists(p, ec)) { failed++; if (cb.log) cb.log("GAGAL  hapus " + U(p.filename())); }
        else { ok++; if (cb.log) cb.log("OK     hapus " + U(p.filename())); }
    }
    bool c = cancel.load();
    if (cb.finish) cb.finish(!c && failed == 0, std::string(c ? "Dibatalkan: " : "Selesai: ") + std::to_string(ok) + " dihapus, " + std::to_string(failed) + " gagal", ok + failed, total);
    return failed == 0;
}

}  // namespace ng
