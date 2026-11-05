#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace xfx {
struct UpdateInfo {
    enum State { Idle, Checking, Current, Available, Downloading, Ready, Failed };
    State state = Idle;
    std::string version, notes, message;
    std::filesystem::path installer;
};

// Network and disk work run on one owned worker. UI callers only enqueue/read snapshots.
// No installer launch is performed. Destruction joins the bounded worker; destroy off
// the AE UI thread during normal operation if shutdown latency matters.
class UpdateClient {
public:
    UpdateClient(std::string current_version, std::filesystem::path cache_dir);
    ~UpdateClient();
    UpdateClient(const UpdateClient&) = delete;
    UpdateClient& operator=(const UpdateClient&) = delete;
    void check();
    void download();
    UpdateInfo status() const;
    // Explicit user-install step only: rehash and deny write/delete until the
    // returned lease is released after process creation. Throws on any change.
    std::shared_ptr<void> lock_ready_installer() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace xfx
