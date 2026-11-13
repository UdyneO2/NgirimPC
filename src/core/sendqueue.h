// Antrean kirim ke banyak perangkat (Ngirim v2.10): file yang sama dikirim berurutan ke tiap perangkat yang dicentang,
// dengan status per perangkat dan "ulangi yang gagal". Portabel dan bebas UI; pengiriman sungguhan lewat StartFn.
#pragma once
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ng {

enum class QStatus { Idle, Waiting, Sending, Done, Failed, Cancelled };

struct QueueDevice {
    std::string name, host, pin;
    int port = 0;
    bool checked = true;   // ikut dikirimi
    QStatus status = QStatus::Idle;
    std::string message;   // hasil terakhir (pesan sukses/galat)
};

class SendQueue {
public:
    /** Mulai mengirim ke satu perangkat (asinkron). false = mesin sedang sibuk, coba lagi nanti. */
    using StartFn = std::function<bool(const QueueDevice&, const std::vector<std::filesystem::path>&)>;

    explicit SendQueue(StartFn start) : start_(std::move(start)) {}

    /** Tambah atau perbarui perangkat (kunci host:port). Mengembalikan indeksnya. */
    int upsert(const std::string& name, const std::string& host, int port, const std::string& pin);
    void remove(int idx);
    void setChecked(int idx, bool on);

    std::vector<QueueDevice> devices() const;
    size_t checkedCount() const;
    bool running() const;       // ada perangkat Waiting/Sending
    bool anyFailed() const;     // ada yang Failed/Cancelled (bisa diulang)

    /** Antre semua perangkat tercentang (status apa pun) dengan file ini. false bila tidak ada tujuan. */
    bool start(const std::vector<std::filesystem::path>& files);
    /** Antre ulang hanya yang Failed/Cancelled dengan file terakhir. */
    bool retryFailed();
    /** Batalkan yang masih menunggu; yang sedang dikirim diselesaikan/dibatalkan oleh mesin lewat onFinished. */
    void cancel();

    /** Panggil dari thread UI tiap frame: memulai perangkat berikutnya bila tidak ada yang sedang dikirim. */
    void pump();
    /** Hasil pengiriman perangkat yang sedang aktif (boleh dari thread mesin). Diabaikan bila tidak ada yang aktif. */
    void onFinished(bool ok, const std::string& msg);

private:
    mutable std::mutex mu_;
    std::vector<QueueDevice> dev_;
    std::vector<std::filesystem::path> files_;
    int current_ = -1;
    StartFn start_;
};

}  // namespace ng
