#include <cstdio>
#include "../src/core/sendqueue.h"

using namespace ng;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct Fake {
    std::vector<std::string> started;   // urutan perangkat yang dikirimi
    int busyTimes = 0;                  // berapa kali StartFn menolak (mesin sibuk)
    size_t lastFiles = 0;
    SendQueue q{[this](const QueueDevice& d, const std::vector<std::filesystem::path>& f) {
        if (busyTimes > 0) { busyTimes--; return false; }
        started.push_back(d.name);
        lastFiles = f.size();
        return true;
    }};
};

static QStatus st(const SendQueue& q, int i) { return q.devices()[(size_t)i].status; }

int main() {
    const std::vector<std::filesystem::path> files = {"a.bin", "b.bin"};
    { // berurutan, hanya yang dicentang, satu per satu
        Fake f;
        f.q.upsert("HP-A", "10.0.0.1", 5000, "111111");
        f.q.upsert("HP-B", "10.0.0.2", 5000, "222222");
        f.q.upsert("HP-C", "10.0.0.3", 5000, "333333");
        f.q.setChecked(1, false);
        CHECK(f.q.checkedCount() == 2);
        CHECK(f.q.start(files));
        CHECK(f.q.running());
        f.q.pump();
        CHECK(f.started.size() == 1 && f.started[0] == "HP-A" && f.lastFiles == 2);
        f.q.pump();                                   // masih mengirim ke A: tidak boleh mulai C
        CHECK(f.started.size() == 1);
        f.q.onFinished(true, "ok A");
        CHECK(st(f.q, 0) == QStatus::Done && st(f.q, 2) == QStatus::Waiting && st(f.q, 1) == QStatus::Idle);
        f.q.pump();
        CHECK(f.started.size() == 2 && f.started[1] == "HP-C");
        f.q.onFinished(false, "putus");
        CHECK(st(f.q, 2) == QStatus::Failed && f.q.devices()[2].message == "putus");
        CHECK(!f.q.running() && f.q.anyFailed());
        f.q.onFinished(true, "nyasar");               // tidak ada yang aktif: diabaikan
        CHECK(st(f.q, 2) == QStatus::Failed);
    }
    { // ulangi yang gagal: hanya yang gagal, dengan file yang sama
        Fake f;
        f.q.upsert("A", "h1", 1, "p"); f.q.upsert("B", "h2", 1, "p");
        f.q.start(files);
        f.q.pump(); f.q.onFinished(true, "ok");
        f.q.pump(); f.q.onFinished(false, "gagal");
        CHECK(f.q.retryFailed());
        CHECK(st(f.q, 0) == QStatus::Done && st(f.q, 1) == QStatus::Waiting);
        f.q.pump();
        CHECK(f.started.size() == 3 && f.started[2] == "B" && f.lastFiles == 2);
        f.q.onFinished(true, "ok");
        CHECK(!f.q.anyFailed() && !f.q.retryFailed());
    }
    { // mesin sibuk: kembali menunggu lalu dicoba lagi
        Fake f;
        f.q.upsert("A", "h1", 1, "p");
        f.busyTimes = 2;
        f.q.start(files);
        f.q.pump(); CHECK(st(f.q, 0) == QStatus::Waiting && f.started.empty());
        f.q.pump(); CHECK(f.started.empty());
        f.q.pump(); CHECK(f.started.size() == 1 && st(f.q, 0) == QStatus::Sending);
    }
    { // batal: yang menunggu dibatalkan, yang sedang dikirim menunggu hasil mesin
        Fake f;
        f.q.upsert("A", "h1", 1, "p"); f.q.upsert("B", "h2", 1, "p"); f.q.upsert("C", "h3", 1, "p");
        f.q.start(files);
        f.q.pump();
        f.q.cancel();
        CHECK(st(f.q, 0) == QStatus::Sending && st(f.q, 1) == QStatus::Cancelled && st(f.q, 2) == QStatus::Cancelled);
        f.q.onFinished(false, "Dibatalkan");
        CHECK(!f.q.running() && f.q.anyFailed());
        f.q.pump();
        CHECK(f.started.size() == 1);                 // tidak ada yang dilanjutkan
        CHECK(f.q.retryFailed());                     // semua bisa diulang
    }
    { // upsert menimpa perangkat yang sama; remove tidak menyentuh yang sedang dikirim; start tanpa tujuan/file
        Fake f;
        int a = f.q.upsert("A", "h1", 1, "old");
        CHECK(f.q.upsert("A2", "h1", 1, "new") == a && f.q.devices().size() == 1 && f.q.devices()[0].pin == "new");
        CHECK(!f.q.start({}));
        f.q.start(files); f.q.pump();
        f.q.remove(0);
        CHECK(f.q.devices().size() == 1);
        f.q.onFinished(true, "ok");
        f.q.remove(0);
        CHECK(f.q.devices().empty() && !f.q.start(files));
    }
    std::puts(fails ? "ADA TES GAGAL" : "SELESAI: antrean lulus");
    return fails ? 1 : 0;
}
