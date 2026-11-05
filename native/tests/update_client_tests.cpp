// All HTTP bytes below are local fixtures. This test never opens a network socket
// and never executes an installer. Include the implementation to reach its private
// transport seam without adding a production API or requiring the AE SDK.
#define XFX_UPDATE_CLIENT_TESTING
#include "../src/update_client.cpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
namespace ud = xfx::update_detail;
using xfx::UpdateInfo;
static void require(bool yes, const char* label) { if (!yes) throw std::runtime_error(label); }
static ud::Response bytes(std::string body, unsigned code = 200, std::string location = {}, std::string length = {}) {
    auto shared = std::make_shared<std::pair<std::string, size_t>>(std::move(body), 0);
    return {code, std::move(location), std::move(length), [shared](char* dst, size_t cap) {
        auto n = (std::min)(cap, shared->first.size() - shared->second);
        std::memcpy(dst, shared->first.data() + shared->second, n); shared->second += n; return n;
    }};
}
static std::string release(std::string version = "1.0.27", std::string digest = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", std::string flags = "") {
    return "[{\"tag_name\":\"effect-console-v" + version + "\",\"draft\":false,\"prerelease\":false,\"body\":\"Notes \\u4e2d\\u6587\",\"assets\":[{\"name\":\"EffectConsoleInstaller-" + version + ".exe\",\"size\":3,\"browser_download_url\":\"https://github.com/X1039538408/yuanchuan-plugins/releases/download/effect-console-v" + version + "/EffectConsoleInstaller-" + version + ".exe\",\"digest\":\"" + digest + "\"}]" + flags + "}]";
}
static UpdateInfo settle(xfx::UpdateClient& c) {
    auto end = std::chrono::steady_clock::now() + 5s;
    for (;;) {
        auto s = c.status();
        if (s.state != UpdateInfo::Checking && s.state != UpdateInfo::Downloading) return s;
        require(std::chrono::steady_clock::now() < end, "worker timeout");
        std::this_thread::sleep_for(1ms);
    }
}
#include <fstream>
#include <future>
#include <map>
#include <windows.h>
#include <winioctl.h>
namespace fs = std::filesystem;
static const std::string api = "https://api.github.com/repos/X1039538408/yuanchuan-plugins/releases?per_page=100";
static const std::string asset = "https://github.com/X1039538408/yuanchuan-plugins/releases/download/effect-console-v1.0.27/EffectConsoleInstaller-1.0.27.exe";
static const std::string hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static std::string changed(std::string s, const std::string& from, const std::string& to) {
    auto pos = s.find(from); require(pos != std::string::npos, "fixture replacement"); s.replace(pos, from.size(), to); return s;
}
struct Temp {
    fs::path root;
    Temp() {
        static unsigned n = 0;
        root = fs::temp_directory_path() / (L"xfx-update-fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++n));
        require(fs::create_directory(root), "create isolated fixture root");
    }
    ~Temp() {
        auto p = fs::absolute(root).lexically_normal();
        if (p.parent_path() == fs::absolute(fs::temp_directory_path()).lexically_normal() && p.filename().wstring().rfind(L"xfx-update-fixture-", 0) == 0) fs::remove_all(p);
    }
};
static std::string read_bytes(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void metadata(std::string text) {
    ud::testing_open = [text = std::move(text)](const std::string& url) {
        if (url == api) return bytes(text);
        if (url == asset) return bytes("abc");
        throw std::runtime_error("unexpected fixture URL");
    };
}
static UpdateInfo checked(std::string text, std::string current = "1.0.26") {
    metadata(std::move(text)); Temp tmp; xfx::UpdateClient c(std::move(current), tmp.root / "cache"); c.check(); return settle(c);
}
static bool junction(const fs::path& link, const fs::path& target) {
    struct Buffer { DWORD tag; WORD length, reserved, sub_offset, sub_length, print_offset, print_length; wchar_t data[2048]; } b{};
    auto name = fs::absolute(target).wstring(), sub = L"\\??\\" + name;
    b.tag = IO_REPARSE_TAG_MOUNT_POINT; b.sub_length = static_cast<WORD>(sub.size() * 2); b.print_offset = static_cast<WORD>((sub.size() + 1) * 2); b.print_length = static_cast<WORD>(name.size() * 2);
    std::memcpy(b.data, sub.c_str(), (sub.size() + 1) * 2); std::memcpy(reinterpret_cast<char*>(b.data) + b.print_offset, name.c_str(), (name.size() + 1) * 2);
    b.length = static_cast<WORD>(8 + b.print_offset + b.print_length + 2);
    fs::create_directory(link);
    HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    DWORD returned = 0; bool ok = h != INVALID_HANDLE_VALUE && DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, &b, static_cast<DWORD>(b.length + 8), nullptr, 0, &returned, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h); return ok;
}
static void available() {
    auto s = checked(release());
    require(s.state == UpdateInfo::Available && s.version == "1.0.27", "new official matching release must be Available");
    require(s.notes == "Notes 中文", "release notes decode Unicode");
}
static void versions() {
    require(checked(release("1.0.9")).state == UpdateInfo::Current, "numeric version downgrade rejected");
    require(checked(release("1.0.26")).state == UpdateInfo::Current, "equal version rejected");
    require(checked(release("1.0.100")).state == UpdateInfo::Available, "numeric version upgrade");
    for (const auto& v : {"1.0.027", "1.0", "1.0.27-beta", "1.0.27+build", "1.0.4294967296", " 1.0.27"})
        require(checked(release(v)).state != UpdateInfo::Available, "strict release version grammar");
    require(checked(release(), "garbage").state == UpdateInfo::Failed, "invalid installed version fails closed");
}
static void release_filter() {
    require(checked(changed(release(), "\"draft\":false", "\"draft\":true")).state == UpdateInfo::Current, "draft rejected");
    require(checked(changed(release(), "\"prerelease\":false", "\"prerelease\":true")).state == UpdateInfo::Current, "prerelease rejected");
    require(checked(changed(release(), "EffectConsoleInstaller-1.0.27.exe\",", "EffectConsoleInstaller-1.0.28.exe\",")).state == UpdateInfo::Failed, "mismatched installer refused");
    require(checked(changed(release(), "effect-console-v1.0.27\",", "framefit-v9.0.0\",")).state == UpdateInfo::Current, "other repository product ignored");
    auto other = changed(release("9.0.0"), "effect-console-v9.0.0\",", "v9.0.0\",");
    other = changed(other, "EffectConsoleInstaller-9.0.0.exe", "FrameFitInstaller-9.0.0.exe");
    auto mixed = other; mixed.pop_back(); mixed += "," + release("1.0.28").substr(1);
    require(checked(mixed).version == "1.0.28", "another product's generic higher tag cannot mask this product release");
    auto both = release("1.0.28"); both.pop_back(); both += "," + release().substr(1);
    require(checked(both).version == "1.0.28", "choose highest stable product version, not array order");
}
static void strict_json() {
    for (const auto& text : {release() + "x", changed(release(), "\"draft\":false", "\"draft\":false,\"draft\":true"), changed(release(), "\"size\":3", "\"size\":3.0"), changed(release(), "Notes \\u4e2d\\u6587", "\\ud800"), changed(release(), "\"draft\":false", "\"draft\":0"), std::string("[") + std::string(60, '[') + std::string(60, ']') + "]"})
        require(checked(text).state == UpdateInfo::Failed, "malformed or ambiguous JSON rejected");
    auto invalid = release(); invalid[invalid.find("Notes")] = static_cast<char>(0xff);
    require(checked(invalid).state == UpdateInfo::Failed, "invalid UTF-8 rejected");
}
static void digest_rules() {
    require(checked(release("1.0.27", "")).state == UpdateInfo::Failed, "no checksum fails closed");
    require(checked(release("1.0.27", "md5:" + hash)).state == UpdateInfo::Failed, "unsupported digest fails closed");
    require(checked(release("1.0.27", "sha256:abc")).state == UpdateInfo::Failed, "short digest rejected");
}
static void url_rules() {
    for (const auto& url : {"http://github.com/X1039538408/yuanchuan-plugins/releases/download/a/b", "https://evil.example/payload.exe", "https://github.com.evil.example/a", "https://github.com@evil.example/a", "https://github.com:444/X1039538408/yuanchuan-plugins/releases/download/a/b", "https://github.com/other/repo/releases/download/a/b", "https://github.com/X1039538408/yuanchuan-plugins/releases/download/a/%2e%2e/b", "https://github.com/X1039538408/yuanchuan-plugins/releases/download/a/b#frag"})
        require(checked(changed(release(), asset, url)).state == UpdateInfo::Failed, "untrusted asset URL refused");
}
static void download_ok() {
    metadata(release()); Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); require(settle(c).state == UpdateInfo::Available, "check before download");
    c.download(); auto s = settle(c); if (s.state != UpdateInfo::Ready) throw std::runtime_error("verified download Ready: " + s.message);
    require(s.installer.parent_path() == t.root / "cache" && s.installer.extension() == ".exe", "generated local filename");
    require(s.installer.filename() != "EffectConsoleInstaller-1.0.27.exe", "not remote filename");
    require(read_bytes(s.installer) == "abc", "persist exact verified bytes");
    require(std::distance(fs::directory_iterator(t.root / "cache"), fs::directory_iterator{}) == 1, "no temporary files remain");
}
static void launch_lease() {
    metadata(release()); Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache");
    c.check(); settle(c); c.download(); auto info = settle(c);
    require(info.state == UpdateInfo::Ready, "seed launch lease");
    auto lease = c.lock_ready_installer(); require(bool(lease), "verified installer must have a live lease");
    HANDLE writer = CreateFileW(info.installer.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    require(writer == INVALID_HANDLE_VALUE, "verified launch lease must block installer writes");
    require(!DeleteFileW(info.installer.c_str()), "verified launch lease must block replacement");
    require(!MoveFileExW((t.root / "cache").c_str(), (t.root / "moved").c_str(), 0), "lease must pin cache path identity");
    lease.reset();
    { std::ofstream file(info.installer, std::ios::binary); file << "bad"; }
    bool blocked = false;
    try { c.lock_ready_installer(); } catch (const std::exception&) { blocked = true; }
    require(blocked, "bytes modified after download must never be launched");
}
static void failure_preserves_original() {
    metadata(release()); Temp t; fs::path final;
    { xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); auto s = settle(c); require(s.state == UpdateInfo::Ready, "fixture seed verified"); final = s.installer; }
    metadata(release());
    ud::testing_open = [](const std::string& url) { if (url == api) return bytes(release()); return bytes("bad"); };
    { xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); auto s = settle(c); require(s.state == UpdateInfo::Ready || s.state == UpdateInfo::Failed, "cached verified file may be reused or download rejected"); }
    require(read_bytes(final) == "abc", "original installer untouched");
    // A corrupt stream in a fresh cache must not publish anything.
    xfx::UpdateClient bad("1.0.26", t.root / "bad-cache"); bad.check(); settle(bad); bad.download(); require(settle(bad).state == UpdateInfo::Failed, "hash mismatch refused");
    require(fs::is_empty(t.root / "bad-cache"), "hash failure cleans temporary file");
}
static void download_bounds() {
    for (auto mode : {0, 1, 2, 3}) {
        Temp t;
        ud::testing_open = [mode](const std::string& url) {
            if (url == api) return bytes(release());
            if (mode == 0) return bytes("ab");
            if (mode == 1) return bytes("abcd");
            if (mode == 2) return bytes("abc", 200, {}, "1000000000000");
            ud::Response r = bytes("abc"); r.read = [](char*, size_t) -> size_t { throw std::runtime_error("broken connection"); }; return r;
        };
        xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "short/long/huge/interrupted download refused");
        require(fs::is_empty(t.root / "cache"), "failed stream removes temp");
    }
    require(checked(changed(release(), "\"size\":3", "\"size\":268435457")).state == UpdateInfo::Failed, "metadata installer size capped");
    ud::testing_open = [](const std::string&) { return bytes(std::string(2 * 1024 * 1024 + 1, ' ')); };
    Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); require(settle(c).state == UpdateInfo::Failed, "API response body capped even without Content-Length");
}
static void statuses_redirects() {
    for (unsigned code : {403u, 404u, 429u, 500u}) {
        ud::testing_open = [code](const std::string&) { return bytes("{}", code); };
        Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); auto s = settle(c); require(s.state == UpdateInfo::Failed && !s.message.empty(), "HTTP errors visible, no retry loop");
    }
    for (const auto& location : {"http://github.com/x", "https://evil.example/x", "https://release-assets.githubusercontent.com.evil.example/x", "https://github.com/other/repo/x"}) {
        ud::testing_open = [location](const std::string& url) { return url == api ? bytes(release()) : bytes({}, 302, location); };
        Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "unsafe redirect refused");
    }
    ud::testing_open = [](const std::string& url) { if (url == api) return bytes(release()); if (url == asset) return bytes({}, 302, "https://release-assets.githubusercontent.com/github-production-release-asset/fixture?sig=fixture"); return bytes("abc"); };
    Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Ready, "official HTTPS asset redirect allowed");
    ud::testing_open = [](const std::string&) { return bytes({}, 302, api); };
    Temp loop; xfx::UpdateClient l("1.0.26", loop.root / "cache"); l.check(); require(settle(l).state == UpdateInfo::Failed, "redirect cycle bounded");
}
static void sums_fallback() {
    auto json = release("1.0.27", ""); auto p = json.find("\"digest\":\"\""); json.replace(p, 11, "\"digest\":null");
    json = changed(json, "}]}]", "},{\"name\":\"SHA256SUMS.txt\",\"size\":100,\"browser_download_url\":\"https://github.com/X1039538408/yuanchuan-plugins/releases/download/effect-console-v1.0.27/SHA256SUMS.txt\"}]}]");
    for (auto mode : {0, 1, 2}) {
        ud::testing_open = [json, mode](const std::string& url) {
            if (url == api) return bytes(json);
            if (url == asset) return bytes("abc");
            std::string line = hash + "  EffectConsoleInstaller-1.0.27.exe\n";
            if (mode == 1) line += line;
            if (mode == 2) line = hash + "  ../EffectConsoleInstaller-1.0.27.exe\n";
            return bytes(line);
        };
        Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); auto s = settle(c);
        require(s.state == (mode == 0 ? UpdateInfo::Available : UpdateInfo::Failed), "SUMS exact single matching checksum required");
        if (!mode) { c.download(); require(settle(c).state == UpdateInfo::Ready, "SUMS verified download"); }
    }
}
static void sums_metadata_size() {
    auto json = changed(release(), "\"digest\":\"sha256:" + hash + "\"", "\"digest\":null");
    json = changed(json, "}]}]", "},{\"name\":\"SHA256SUMS.txt\",\"size\":1,\"browser_download_url\":\"https://github.com/X1039538408/yuanchuan-plugins/releases/download/effect-console-v1.0.27/SHA256SUMS.txt\"}]}]");
    ud::testing_open = [json](const std::string& u) { return bytes(u == api ? json : hash + "  EffectConsoleInstaller-1.0.27.exe\n"); };
    Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); require(settle(c).state == UpdateInfo::Failed, "checksum asset bytes must match declared size");
}
static void cache_leaf_safety() {
    metadata(release()); Temp t; fs::create_directory(t.root / "outside");
    require(junction(t.root / "cache", t.root / "outside"), "leaf junction fixture");
    { xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "cache directory junction refused"); }
    require(fs::is_empty(t.root / "outside"), "leaf junction caused no outside write");
    require(RemoveDirectoryW((t.root / "cache").c_str()) != 0, "remove fixture cache junction");
    fs::path installer;
    { xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); auto s = settle(c); require(s.state == UpdateInfo::Ready, "seed cache for hardlink test"); installer = s.installer; }
    auto original = t.root / "outside" / "original"; { std::ofstream out(original, std::ios::binary); out << "abc"; }
    require(installer.parent_path() == t.root / "cache", "hardlink replacement confined to fixture"); require(DeleteFileW(installer.c_str()) != 0, "remove seeded installer");
    require(CreateHardLinkW(installer.c_str(), original.c_str(), nullptr) != 0, "create final-file hardlink fixture");
    { xfx::UpdateClient c("1.0.26", t.root / "cache"); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "final installer hardlink rejected even with matching hash"); }
    require(read_bytes(original) == "abc", "hardlink target preserved");
    auto insecure = t.root / "insecure"; fs::create_directory(insecure); PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr) != 0, "create insecure cache ACL fixture");
    bool changed_acl = SetFileSecurityW(insecure.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor) != 0; LocalFree(descriptor); require(changed_acl, "set insecure cache ACL fixture");
    xfx::UpdateClient c("1.0.26", insecure); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "publicly writable cache refused"); require(fs::is_empty(insecure), "untrusted cache receives no installer");
}
static void publish_conflict_preserves_file() {
    Temp t; auto cache = t.root / "cache";
    auto target = cache / ("EffectConsoleUpdate-1.0.27-" + hash + ".exe");
    ud::testing_open = [&](const std::string& u) {
        if (u == api) return bytes(release());
        std::ofstream collision(target, std::ios::binary); collision << "original-conflicting-file"; collision.close(); return bytes("abc");
    };
    xfx::UpdateClient c("1.0.26", cache); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "concurrent final-file conflict refused");
    require(read_bytes(target) == "original-conflicting-file", "publish conflict never overwrites original");
    require(std::distance(fs::directory_iterator(cache), fs::directory_iterator{}) == 1, "conflict removes own temporary file only");
}
static void directory_mutation_lock() {
    Temp t; auto cache = t.root / "cache"; bool renamed = false;
    ud::testing_open = [&](const std::string& u) {
        if (u == api) return bytes(release());
        renamed = MoveFileExW(cache.c_str(), (t.root / "moved-cache").c_str(), 0) != FALSE;
        return bytes("abc");
    };
    xfx::UpdateClient c("1.0.26", cache); c.check(); settle(c); c.download(); auto result = settle(c);
    require(result.state == UpdateInfo::Ready, "directory lock fixture download");
    require(!renamed && read_bytes(result.installer) == "abc", "open cache ancestor cannot be renamed/replaced during download");
}
static void paths() {
    metadata(release()); Temp t; fs::create_directory(t.root / "outside");
    require(junction(t.root / "link", t.root / "outside"), "junction fixture must be available");
    { xfx::UpdateClient c("1.0.26", t.root / "link" / "cache"); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "reparse ancestor refused"); require(fs::is_empty(t.root / "outside"), "no outside writes"); }
    require(RemoveDirectoryW((t.root / "link").c_str()) != 0, "remove fixture junction");
    for (const auto& p : {fs::path("relative-cache"), t.root / "cache:stream", t.root / ".." / "bad", t.root / "trailing.", t.root / "CON", t.root / "nul.txt", t.root / "CoM1.cache"}) {
        xfx::UpdateClient c("1.0.26", p); c.check(); settle(c); c.download(); require(settle(c).state == UpdateInfo::Failed, "unsafe local cache path refused");
    }
}
static void terminal_allows_next_operation() {
    metadata(release()); Temp t; xfx::UpdateClient c("1.0.26", t.root / "cache");
    for (unsigned i = 0; i < 1500; ++i) {
        c.check();
        while (c.status().state == UpdateInfo::Checking) std::this_thread::yield();
        require(c.status().state == UpdateInfo::Available, "repeated check available");
        c.download();
        auto s = settle(c); require(s.state == UpdateInfo::Ready, "visible terminal state must accept immediate next operation");
    }
}
static void async_worker() {
    auto gate = std::make_shared<std::promise<void>>(); auto wait = gate->get_future().share();
    ud::testing_open = [wait](const std::string&) { wait.wait(); return bytes(release()); };
    Temp t; auto c = std::make_unique<xfx::UpdateClient>("1.0.26", t.root / "cache"); auto start = std::chrono::steady_clock::now(); c->check();
    const bool returned = std::chrono::steady_clock::now() - start < 100ms && c->status().state == UpdateInfo::Checking;
    c->check(); c->download(); const bool ignored = c->status().state == UpdateInfo::Checking;
    gate->set_value(); require(returned, "check must not block UI"); require(ignored, "busy operations ignored"); require(settle(*c).state == UpdateInfo::Available, "async check completion");
    c.reset();
}
static void async_download_and_join() {
    metadata(release()); Temp t; auto c = std::make_unique<xfx::UpdateClient>("1.0.26", t.root / "cache"); c->check(); require(settle(*c).state == UpdateInfo::Available, "join fixture check");
    auto gate = std::make_shared<std::promise<void>>(); auto wait = gate->get_future().share(); auto entered = std::make_shared<std::promise<void>>(); auto started = entered->get_future();
    ud::testing_open = [wait, entered](const std::string&) { entered->set_value(); wait.wait(); return bytes("abc"); };
    auto begin = std::chrono::steady_clock::now(); c->download(); bool nonblocking = std::chrono::steady_clock::now() - begin < 100ms;
    const bool reading = started.wait_for(2s) == std::future_status::ready;
    const bool state = c->status().state == UpdateInfo::Downloading; c->check(); const bool ignored = c->status().state == UpdateInfo::Downloading;
    auto destroyed = std::async(std::launch::async, [client = std::move(c)]() mutable { client.reset(); });
    const bool joined = destroyed.wait_for(20ms) == std::future_status::timeout;
    gate->set_value(); destroyed.get();
    require(nonblocking && reading && state && ignored, "download must not block UI and busy check ignored"); require(joined, "destructor must join owned active worker");
    require(fs::is_empty(t.root / "cache"), "cancelled destructor cleans download temporary file");
}
static void shutdown_cancels_request() {
    auto entered = std::make_shared<std::promise<void>>(); auto started = entered->get_future();
    auto cancelled = std::make_shared<std::promise<void>>(); auto released = cancelled->get_future().share();
    ud::testing_open = [entered, cancelled, released](const std::string&) {
        if (ud::register_cancel) ud::register_cancel([cancelled] { cancelled->set_value(); });
        entered->set_value(); released.wait(); return bytes(release());
    };
    Temp t; auto c = std::make_unique<xfx::UpdateClient>("1.0.26", t.root / "cache"); c->check();
    require(started.wait_for(1s) == std::future_status::ready, "request entered fixture");
    auto destroyed = std::async(std::launch::async, [client = std::move(c)]() mutable { client.reset(); });
    const bool stopped = destroyed.wait_for(200ms) == std::future_status::ready;
    if (!stopped) cancelled->set_value(); // release old implementation without hanging this regression.
    destroyed.get(); require(stopped, "shutdown must actively cancel pending request before joining");
}
int main() {
    unsigned failures = 0, total = 0;
    const std::pair<const char*, void(*)()> tests[] = {{"official available", available}, {"strict numeric versions", versions}, {"release filtering", release_filter}, {"strict limited JSON", strict_json}, {"digest fail-closed", digest_rules}, {"official URL allowlist", url_rules}, {"verified download", download_ok}, {"verified launch lease", launch_lease}, {"failure preserves originals", failure_preserves_original}, {"response/download limits", download_bounds}, {"HTTP/redirect controls", statuses_redirects}, {"SHA256SUMS fallback", sums_fallback}, {"SUMS metadata size", sums_metadata_size}, {"cache leaf ACL/hardlink", cache_leaf_safety}, {"cache directory mutation lock", directory_mutation_lock}, {"publish conflict preserves original", publish_conflict_preserves_file}, {"cache path safety", paths}, {"nonblocking owned worker", async_worker}, {"async download/destructor join", async_download_and_join}, {"terminal next-operation race", terminal_allows_next_operation}, {"shutdown cancels request", shutdown_cancels_request}};
    for (auto [name, fn] : tests) { ++total; try { fn(); std::cout << "PASS " << name << '\n'; } catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; } }
    ud::testing_open = {};
    std::cout << total - failures << '/' << total << " update client groups passed (offline fixtures only)\n"; return failures ? 1 : 0;
}
