#include "sendqueue.h"

namespace ng {

int SendQueue::upsert(const std::string& name, const std::string& host, int port, const std::string& pin) {
    std::lock_guard<std::mutex> l(mu_);
    for (size_t i = 0; i < dev_.size(); i++)
        if (dev_[i].host == host && dev_[i].port == port) {
            dev_[i].name = name;
            dev_[i].pin = pin;
            return (int)i;
        }
    QueueDevice d;
    d.name = name; d.host = host; d.port = port; d.pin = pin;
    dev_.push_back(d);
    return (int)dev_.size() - 1;
}

void SendQueue::remove(int idx) {
    std::lock_guard<std::mutex> l(mu_);
    if (idx < 0 || idx >= (int)dev_.size() || idx == current_) return;  // yang sedang dikirimi tidak dilepas
    dev_.erase(dev_.begin() + idx);
    if (current_ > idx) current_--;
}

void SendQueue::setChecked(int idx, bool on) {
    std::lock_guard<std::mutex> l(mu_);
    if (idx >= 0 && idx < (int)dev_.size()) dev_[(size_t)idx].checked = on;
}

std::vector<QueueDevice> SendQueue::devices() const {
    std::lock_guard<std::mutex> l(mu_);
    return dev_;
}

size_t SendQueue::checkedCount() const {
    std::lock_guard<std::mutex> l(mu_);
    size_t n = 0;
    for (auto& d : dev_) if (d.checked) n++;
    return n;
}

bool SendQueue::running() const {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& d : dev_) if (d.status == QStatus::Waiting || d.status == QStatus::Sending) return true;
    return false;
}

bool SendQueue::anyFailed() const {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& d : dev_) if (d.status == QStatus::Failed || d.status == QStatus::Cancelled) return true;
    return false;
}

bool SendQueue::start(const std::vector<std::filesystem::path>& files) {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& d : dev_) if (d.status == QStatus::Waiting || d.status == QStatus::Sending) return false;  // sedang berjalan
    files_ = files;
    bool any = false;
    for (auto& d : dev_) {
        if (d.checked && !files_.empty()) { d.status = QStatus::Waiting; d.message.clear(); any = true; }
        else d.status = QStatus::Idle;
    }
    return any;
}

bool SendQueue::retryFailed() {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& d : dev_) if (d.status == QStatus::Waiting || d.status == QStatus::Sending) return false;
    bool any = false;
    for (auto& d : dev_)
        if (d.status == QStatus::Failed || d.status == QStatus::Cancelled) { d.status = QStatus::Waiting; d.message.clear(); any = true; }
    return any && !files_.empty();
}

void SendQueue::cancel() {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& d : dev_)
        if (d.status == QStatus::Waiting) { d.status = QStatus::Cancelled; d.message = "Dibatalkan"; }
}

void SendQueue::pump() {
    QueueDevice target;
    std::vector<std::filesystem::path> files;
    int idx = -1;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (current_ >= 0) return;
        for (size_t i = 0; i < dev_.size(); i++)
            if (dev_[i].status == QStatus::Waiting) { idx = (int)i; break; }
        if (idx < 0) return;
        target = dev_[(size_t)idx];
        files = files_;
        current_ = idx;
        dev_[(size_t)idx].status = QStatus::Sending;
    }
    const bool started = start_ && start_(target, files);   // di luar kunci: StartFn boleh memanggil mesin
    if (!started) {
        std::lock_guard<std::mutex> l(mu_);
        if (current_ == idx) {   // mesin sibuk: kembali menunggu, dicoba lagi di frame berikutnya
            current_ = -1;
            if (idx < (int)dev_.size() && dev_[(size_t)idx].status == QStatus::Sending) dev_[(size_t)idx].status = QStatus::Waiting;
        }
    }
}

void SendQueue::onFinished(bool ok, const std::string& msg) {
    std::lock_guard<std::mutex> l(mu_);
    if (current_ < 0 || current_ >= (int)dev_.size()) return;
    QueueDevice& d = dev_[(size_t)current_];
    d.status = ok ? QStatus::Done : QStatus::Failed;
    d.message = msg;
    current_ = -1;
}

}  // namespace ng
