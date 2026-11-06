#include "update_client.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <winternl.h>
#include <bcrypt.h>
#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace xfx {
namespace update_detail {
constexpr size_t metadata_limit = 2 * 1024 * 1024, sums_limit = 256 * 1024;
constexpr uint64_t installer_limit = 256ull * 1024 * 1024;
const std::string api_url = "https://api.github.com/repos/Deng-pro-mex/xfx-effect-console/releases?per_page=100";
const std::string asset_prefix = "https://github.com/Deng-pro-mex/xfx-effect-console/releases/download/";
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
void win(bool ok, const char* message) { if (!ok) fail(message); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    Handle(Handle&& h) noexcept : value(std::exchange(h.value, INVALID_HANDLE_VALUE)) {}
    Handle(const Handle&) = delete;
};
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    if (s.size() > metadata_limit * 2) fail("UTF-8 text too large");
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) fail("Invalid UTF-8");
    std::wstring out(static_cast<size_t>(n), L'\0');
    win(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n) == n, "Invalid UTF-8"); return out;
}
uint64_t integer(const std::string& s) {
    uint64_t n = 0;
    if (s.empty() || (s.size() > 1 && s[0] == '0')) fail("Invalid integer");
    auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (error != std::errc{} || end != s.data() + s.size()) fail("Invalid integer"); return n;
}
using Version = std::array<uint32_t, 3>;
Version version(const std::string& s) {
    Version v{}; size_t start = 0;
    for (size_t i = 0; i < 3; ++i) {
        size_t stop = s.find('.', start);
        if ((i < 2 && stop == std::string::npos) || (i == 2 && stop != std::string::npos)) fail("Invalid release version");
        if (stop == std::string::npos) stop = s.size();
        auto n = integer(s.substr(start, stop - start));
        if (n > UINT32_MAX) fail("Version component overflow"); v[i] = static_cast<uint32_t>(n); start = stop + 1;
    }
    return v;
}
std::optional<std::string> tag_version(const std::string& tag) {
    std::string s = tag;
    for (const auto* prefix : {"effect-console-v", "effect-console-", "xfx-native-console-v", "v"}) {
        if (s.rfind(prefix, 0) == 0) { s.erase(0, std::strlen(prefix)); break; }
    }
    try { (void)version(s); return s; } catch (...) { return {}; }
}
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false; std::string text; std::vector<Json> array; std::map<std::string, Json> object;
    const Json& at(const std::string& key) const {
        if (kind != Object) fail("Expected JSON object"); auto it = object.find(key);
        if (it == object.end()) fail("Missing release field"); return it->second;
    }
    const Json* find(const std::string& key) const { auto it = object.find(key); return it == object.end() ? nullptr : &it->second; }
    const std::string& string() const { if (kind != String) fail("Expected JSON string"); return text; }
    bool flag() const { if (kind != Bool) fail("Expected JSON boolean"); return boolean; }
    uint64_t number() const { if (kind != Number) fail("Expected JSON number"); return integer(text); }
};
class Parser {
    const std::string& s; size_t p = 0, nodes = 0;
    void ws() { while (p < s.size() && (s[p] == ' ' || s[p] == '\r' || s[p] == '\n' || s[p] == '\t')) ++p; }
    char take() { if (p == s.size()) fail("Truncated JSON"); return s[p++]; }
    bool eat(char c) { ws(); if (p < s.size() && s[p] == c) { ++p; return true; } return false; }
    static void utf8(std::string& out, uint32_t c) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63)); }
        else if (c < 0x10000) { out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
        else { out += static_cast<char>(0xf0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 63)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    }
    uint32_t hex4() {
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) { char c = take(); n *= 16; if (c >= '0' && c <= '9') n += c - '0'; else if (c >= 'a' && c <= 'f') n += c - 'a' + 10; else if (c >= 'A' && c <= 'F') n += c - 'A' + 10; else fail("Invalid JSON escape"); } return n;
    }
    std::string str() {
        if (take() != '"') fail("Expected JSON string"); std::string out;
        for (;;) {
            unsigned char c = static_cast<unsigned char>(take()); if (c == '"') return out;
            if (c < 32) fail("JSON control character");
            if (c != '\\') { out += static_cast<char>(c); continue; }
            switch (take()) {
            case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': { uint32_t cp = hex4(); if (cp >= 0xd800 && cp <= 0xdbff) { if (take() != '\\' || take() != 'u') fail("Missing JSON low surrogate"); auto low = hex4(); if (low < 0xdc00 || low > 0xdfff) fail("Invalid JSON surrogate"); cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00; } else if (cp >= 0xdc00 && cp <= 0xdfff) fail("Lone JSON surrogate"); utf8(out, cp); break; }
            default: fail("Invalid JSON escape");
            }
        }
    }
    Json value(unsigned depth) {
        if (depth > 24 || ++nodes > 50000) fail("JSON complexity limit"); ws(); if (p == s.size()) fail("Truncated JSON"); Json j;
        char c = s[p];
        if (c == '"') { j.kind = Json::String; j.text = str(); }
        else if (c == '{') {
            ++p; j.kind = Json::Object;
            if (!eat('}')) { do { ws(); auto key = str(); if (!eat(':')) fail("Missing JSON colon"); auto v = value(depth + 1); if (!j.object.emplace(std::move(key), std::move(v)).second) fail("Duplicate JSON key"); } while (eat(',')); if (!eat('}')) fail("Missing JSON brace"); }
        } else if (c == '[') {
            ++p; j.kind = Json::Array; if (!eat(']')) { do { j.array.push_back(value(depth + 1)); } while (eat(',')); if (!eat(']')) fail("Missing JSON bracket"); }
        } else if (c == 't' || c == 'f' || c == 'n') {
            const char* literal = c == 't' ? "true" : c == 'f' ? "false" : "null"; size_t count = std::strlen(literal);
            if (s.compare(p, count, literal)) fail("Invalid JSON literal"); p += count; j.kind = c == 'n' ? Json::Null : Json::Bool; j.boolean = c == 't';
        } else {
            j.kind = Json::Number; size_t begin = p; if (s[p] == '-') ++p;
            if (p >= s.size()) fail("Invalid JSON number");
            if (s[p] == '0') ++p; else { if (s[p] < '1' || s[p] > '9') fail("Invalid JSON number"); while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; }
            if (p < s.size() && s[p] == '.') { ++p; auto start = p; while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; if (p == start) fail("Invalid JSON fraction"); }
            if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) { ++p; if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p; auto start = p; while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; if (p == start) fail("Invalid JSON exponent"); }
            j.text = s.substr(begin, p - begin);
        }
        return j;
    }
public:
    explicit Parser(const std::string& input) : s(input) {}
    Json parse() { if (s.size() > metadata_limit) fail("Release response too large"); (void)wide(s); auto j = value(0); ws(); if (p != s.size()) fail("Trailing JSON bytes"); return j; }
};
struct Url { std::string host, path; };
Url url(const std::string& input) {
    if (input.size() > 8192 || input.rfind("https://", 0)) fail("Only HTTPS URLs are permitted");
    for (unsigned char c : input) if (c <= 32 || c >= 127 || c == '\\' || c == '#' || c == '@') fail("Unsafe URL character");
    auto slash = input.find('/', 8); if (slash == std::string::npos) fail("Missing URL path");
    Url u{input.substr(8, slash - 8), input.substr(slash)};
    if (u.host.find(':') != std::string::npos || u.host.empty()) fail("Unsafe URL authority");
    auto path = u.path.substr(0, u.path.find('?'));
    if (path.find('%') != std::string::npos || path.find("//") != std::string::npos) fail("Ambiguous URL path");
    size_t start = 1;
    while (start <= path.size()) { auto end = path.find('/', start); if (end == std::string::npos) end = path.size(); auto part = path.substr(start, end - start); if (part == "." || part == "..") fail("Unsafe URL path"); start = end + 1; }
    return u;
}
void allowed_url(const std::string& input, bool api, bool redirected) {
    auto u = url(input);
    if (api) { if (input != api_url || redirected) fail("API redirect refused"); return; }
    if (input.rfind(asset_prefix, 0) == 0) return;
    if (redirected && ((u.host == "release-assets.githubusercontent.com" && u.path.rfind("/github-production-release-asset/", 0) == 0) ||
        (u.host == "objects.githubusercontent.com" && u.path.rfind("/github-production-release-asset-", 0) == 0))) return;
    fail("Untrusted release asset host/path");
}
struct Response { unsigned status = 200; std::string location, length; std::function<size_t(char*, size_t)> read; };
#ifdef XFX_UPDATE_CLIENT_TESTING
std::function<Response(const std::string&)> testing_open;
#endif
// A worker registers only its currently owned request cancellation; shutdown
// requests cancellation before joining, never detaches code from an unloading AEX.
thread_local std::function<void(std::function<void()>)> register_cancel;
struct Internet {
    HINTERNET session = nullptr, connection = nullptr;
    std::atomic<HINTERNET> request{nullptr};
    void cancel() { if (auto value = request.exchange(nullptr)) WinHttpCloseHandle(value); }
    ~Internet() { cancel(); if (connection) WinHttpCloseHandle(connection); if (session) WinHttpCloseHandle(session); }
};
std::string header(HINTERNET request, DWORD name) {
    DWORD size = 0;
    if (!WinHttpQueryHeaders(request, name, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX)) {
        if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) fail("Cannot read HTTP header");
    }
    if (size > 32768) fail("HTTP header too large");
    std::vector<wchar_t> out(static_cast<size_t>(size) / sizeof(wchar_t) + 1);
    win(WinHttpQueryHeaders(request, name, WINHTTP_HEADER_NAME_BY_INDEX, out.data(), &size, WINHTTP_NO_HEADER_INDEX) != FALSE, "Cannot read HTTP header");
    std::string result;
    for (wchar_t c : out) { if (!c) break; if (c > 127) fail("Invalid HTTP header"); result += static_cast<char>(c); } return result;
}
Response open_response(const std::string& input) {
#ifdef XFX_UPDATE_CLIENT_TESTING
    if (!testing_open) fail("Offline test requires transport fixture"); return testing_open(input);
#else
    auto u = url(input); auto h = std::make_shared<Internet>();
    h->session = WinHttpOpen(L"EffectConsole-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    win(h->session != nullptr, "Cannot open WinHTTP session");
    win(WinHttpSetTimeouts(h->session, 5000, 5000, 10000, 10000) != FALSE, "Cannot set HTTP timeouts");
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    win(WinHttpSetOption(h->session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)) != FALSE, "Cannot require TLS 1.2");
    DWORD retries = 0; win(WinHttpSetOption(h->session, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)) != FALSE, "Cannot disable connect retries");
    auto host = wide(u.host), path = wide(u.path);
    h->connection = WinHttpConnect(h->session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0); win(h->connection != nullptr, "Cannot connect to update server");
    h->request = WinHttpOpenRequest(h->connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    win(h->request != nullptr, "Cannot open update request");
    if (register_cancel) register_cancel([weak = std::weak_ptr<Internet>(h)] { if (auto active = weak.lock()) active->cancel(); });
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    win(WinHttpSetOption(h->request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) != FALSE, "Cannot restrict HTTP features");
    DWORD max_headers = 32768; win(WinHttpSetOption(h->request, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &max_headers, sizeof(max_headers)) != FALSE, "Cannot bound response headers");
    win(WinHttpSendRequest(h->request, L"Accept: application/vnd.github+json\r\nAccept-Encoding: identity\r\nX-GitHub-Api-Version: 2022-11-28\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE, "Update request failed");
    win(WinHttpReceiveResponse(h->request, nullptr) != FALSE, "Update response failed");
    if (!header(h->request, WINHTTP_QUERY_CONTENT_ENCODING).empty()) fail("Encoded HTTP response refused");
    DWORD status = 0, size = sizeof(status);
    win(WinHttpQueryHeaders(h->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE, "Missing HTTP status");
    return {status, header(h->request, WINHTTP_QUERY_LOCATION), header(h->request, WINHTTP_QUERY_CONTENT_LENGTH), [h](char* dst, size_t cap) { DWORD count = 0; win(WinHttpReadData(h->request, dst, static_cast<DWORD>(cap), &count) != FALSE, "Update stream interrupted"); return static_cast<size_t>(count); }};
#endif
}
using Sink = std::function<void(const char*, size_t)>;
void fetch(const std::string& first, uint64_t limit, const Sink& sink, const std::atomic<bool>& stop, bool api = false) {
    auto deadline = std::chrono::steady_clock::now() + (api ? std::chrono::seconds(30) : std::chrono::seconds(180));
    auto guard = [&] { if (stop.load() || std::chrono::steady_clock::now() >= deadline) fail("Update cancelled or total timeout exceeded"); };
    std::string next = first;
    for (unsigned hop = 0; hop <= 3; ++hop) {
        guard(); allowed_url(next, api, hop != 0); auto response = open_response(next); guard();
        if (response.status == 301 || response.status == 302 || response.status == 303 || response.status == 307 || response.status == 308) {
            if (api || hop == 3 || response.location.empty()) fail("Update redirect refused/limit exceeded"); next = response.location; continue;
        }
        if (response.status == 403 || response.status == 429) fail("GitHub rate limit/access denied; retry later");
        if (response.status != 200) fail("Update server returned unsuccessful HTTP status");
        std::optional<uint64_t> length;
        if (!response.length.empty()) { length = integer(response.length); if (*length > limit) fail("HTTP body exceeds size limit"); }
        std::array<char, 65536> buffer{}; uint64_t count = 0;
        for (;;) { guard(); auto n = response.read(buffer.data(), buffer.size()); guard(); if (n > buffer.size() || n > limit - count) fail("HTTP body exceeds size limit"); if (!n) break; sink(buffer.data(), n); count += n; }
        if (length && count != *length) fail("Truncated/inconsistent HTTP body"); return;
    }
    fail("Redirect limit exceeded");
}
std::string text_fetch(const std::string& input, size_t limit, const std::atomic<bool>& stop, bool api = false) {
    std::string text; fetch(input, limit, [&](const char* data, size_t n) { text.append(data, n); }, stop, api); return text;
}
std::string sha_text(std::string s) {
    if (s.size() != 64) fail("Invalid SHA256 digest");
    for (auto& c : s) { if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a'); else if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) fail("Invalid SHA256 digest"); } return s;
}
struct Candidate { std::string version, notes, url, hash; uint64_t size = 0; };
Candidate choose(const std::string& raw, const std::string& current, const std::atomic<bool>& stop) {
    const auto installed = version(current); auto doc = Parser(raw).parse(); if (doc.kind != Json::Array || doc.array.size() > 100) fail("Expected bounded releases array");
    const Json* best = nullptr; std::string best_version; Version highest = installed;
    for (const auto& item : doc.array) {
        auto tag = item.at("tag_name").string(); auto v = tag_version(tag); if (!v) continue;
        if (item.at("draft").flag() || item.at("prerelease").flag()) continue;
        // A generic version tag may be another product in the shared repository.
        if (tag.rfind("effect-console-", 0) != 0 && tag.rfind("xfx-native-console-", 0) != 0) {
            const auto& assets = item.at("assets");
            if (assets.kind != Json::Array) fail("Invalid release assets");
            const auto expected = "EffectConsoleInstaller-" + *v + ".exe";
            if (std::none_of(assets.array.begin(), assets.array.end(), [&](const Json& a) {
                    return a.at("name").string() == expected;
                })) continue;
        }
        auto parsed = version(*v); if (parsed > highest) { highest = parsed; best = &item; best_version = *v; }
    }
    if (!best) return {};
    Candidate result; result.version = best_version;
    if (auto notes = best->find("body"); notes && notes->kind != Json::Null) result.notes = notes->string().substr(0, 65536);
    auto expected = "EffectConsoleInstaller-" + best_version + ".exe";
    auto tag = best->at("tag_name").string(); const auto& assets = best->at("assets");
    if (assets.kind != Json::Array || assets.array.size() > 100) fail("Invalid release assets");
    const Json *installer = nullptr, *sums = nullptr;
    for (const auto& a : assets.array) {
        auto name = a.at("name").string();
        if (name == expected) { if (installer) fail("Ambiguous installer assets"); installer = &a; }
        if (name == "SHA256SUMS" || name == "SHA256SUMS.txt") { if (sums) fail("Ambiguous SHA256SUMS assets"); sums = &a; }
    }
    if (!installer) fail("Release installer does not match release version");
    result.size = installer->at("size").number(); if (!result.size || result.size > installer_limit) fail("Installer size limit exceeded");
    result.url = installer->at("browser_download_url").string(); allowed_url(result.url, false, false);
    if (result.url != asset_prefix + tag + "/" + expected) fail("Installer asset URL/version mismatch");
    const auto* digest = installer->find("digest");
    if (digest && digest->kind != Json::Null && !digest->string().empty()) {
        auto d = digest->string(); if (d.rfind("sha256:", 0)) fail("Unsupported asset digest"); result.hash = sha_text(d.substr(7));
    } else {
        if (!sums) fail("No authenticated SHA256 digest or release SHA256SUMS");
        auto size = sums->at("size").number(); if (!size || size > sums_limit) fail("SHA256SUMS size limit exceeded");
        auto sum_url = sums->at("browser_download_url").string(); allowed_url(sum_url, false, false);
        if (sum_url != asset_prefix + tag + "/" + sums->at("name").string()) fail("SHA256SUMS does not belong to selected release");
        auto body = text_fetch(sum_url, static_cast<size_t>(size), stop); if (body.size() != size) fail("SHA256SUMS asset size mismatch"); size_t start = 0;
        while (start < body.size()) {
            auto end = body.find('\n', start); if (end == std::string::npos) end = body.size(); auto line = body.substr(start, end - start); if (!line.empty() && line.back() == '\r') line.pop_back(); start = end + 1;
            if (line.empty()) continue;
            if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) fail("Malformed SHA256SUMS line");
            auto h = sha_text(line.substr(0, 64)); auto name = line.substr(66);
            if (name == expected) { if (!result.hash.empty()) fail("Duplicate installer checksum"); result.hash = std::move(h); }
        }
        if (result.hash.empty()) fail("Installer checksum missing from SHA256SUMS");
    }
    return result;
}
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr; std::vector<unsigned char> object;
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) fail("Cannot open SHA256 provider");
        DWORD bytes = 0, size = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&bytes), sizeof(bytes), &size, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr; fail("Cannot initialize SHA256"); }
        object.resize(bytes);
        if (BCryptCreateHash(algorithm, &hash, object.data(), bytes, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr; fail("Cannot initialize SHA256"); }
    }
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void add(const char* data, size_t n) { if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), static_cast<ULONG>(n), 0) < 0) fail("SHA256 update failed"); }
    std::string finish() { std::array<unsigned char, 32> out{}; if (BCryptFinishHash(hash, out.data(), static_cast<ULONG>(out.size()), 0) < 0) fail("SHA256 finalization failed"); std::string result; for (auto b : out) { result += "0123456789abcdef"[b >> 4]; result += "0123456789abcdef"[b & 15]; } return result; }
};
struct Security {
    std::vector<unsigned char> token; PSECURITY_DESCRIPTOR descriptor = nullptr; PSID user = nullptr;
    Security() {
        HANDLE raw = nullptr; win(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw) != FALSE, "Cannot read cache identity"); Handle h(raw); DWORD size = 0;
        GetTokenInformation(h.value, TokenUser, nullptr, 0, &size); token.resize(size);
        win(GetTokenInformation(h.value, TokenUser, token.data(), size, &size) != FALSE, "Cannot read cache SID"); user = reinterpret_cast<TOKEN_USER*>(token.data())->User.Sid;
        LPWSTR sid = nullptr; win(ConvertSidToStringSidW(user, &sid) != FALSE, "Cannot encode cache SID");
        std::wstring sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + std::wstring(sid) + L")"; LocalFree(sid);
        win(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != FALSE, "Cannot create private cache ACL");
    }
    ~Security() { if (descriptor) LocalFree(descriptor); }
    void check(HANDLE h) const {
        PSID owner = nullptr; PACL acl = nullptr; PSECURITY_DESCRIPTOR sd = nullptr;
        if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &acl, nullptr, &sd) != ERROR_SUCCESS) fail("Cannot inspect cache ACL");
        auto trusted = [&](PSID sid) { return EqualSid(sid, user) || IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid); };
        bool safe = owner && trusted(owner) && acl;
        if (acl) for (DWORD i = 0; i < acl->AceCount; ++i) {
            void* ace = nullptr; if (!GetAce(acl, i, &ace)) { safe = false; break; }
            auto* header = static_cast<ACE_HEADER*>(ace);
            if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                auto* allow = static_cast<ACCESS_ALLOWED_ACE*>(ace);
                constexpr DWORD write_access = GENERIC_ALL | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER | DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD;
                if ((allow->Mask & write_access) && !trusted(&allow->SidStart)) safe = false;
            } else if (header->AceType != ACCESS_DENIED_ACE_TYPE && header->AceType != SYSTEM_AUDIT_ACE_TYPE) safe = false;
        }
        LocalFree(sd); if (!safe) fail("Cache is writable/owned by an untrusted identity");
    }
};
void inspect_file(HANDLE h, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{}; win(GetFileInformationByHandle(h, &info) != FALSE, "Cannot inspect cache path");
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || (!!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory) || (!directory && info.nNumberOfLinks != 1)) fail("Reparse/hardlink/invalid cache path refused");
}
// Bind every child operation to an already inspected directory object, never
// re-resolve its full pathname. OBJ_DONT_REPARSE also refuses a concurrent reparse
// mutation. Win32 directory share modes alone do not exclude metadata writers.
Handle relative_open(HANDLE parent, const std::wstring& name, DWORD access, DWORD share, ULONG disposition, bool directory, PSECURITY_DESCRIPTOR security = nullptr) {
    if (name.empty() || name.find_first_of(L"\\/:") != std::wstring::npos || name.size() > 240) fail("Invalid local cache child name");
    using Open = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    using Error = ULONG(NTAPI*)(NTSTATUS);
    auto module = GetModuleHandleW(L"ntdll.dll");
    auto open = reinterpret_cast<Open>(GetProcAddress(module, "NtCreateFile"));
    auto error = reinterpret_cast<Error>(GetProcAddress(module, "RtlNtStatusToDosError"));
    if (!open || !error) fail("Safe relative cache I/O is unavailable");
    UNICODE_STRING unicode{}; unicode.Buffer = const_cast<PWSTR>(name.data()); unicode.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)); unicode.MaximumLength = unicode.Length;
    OBJECT_ATTRIBUTES attributes{}; attributes.Length = sizeof(attributes); attributes.RootDirectory = parent; attributes.ObjectName = &unicode;
    attributes.Attributes = 0x40 /* OBJ_CASE_INSENSITIVE */ | 0x1000 /* OBJ_DONT_REPARSE */; attributes.SecurityDescriptor = security;
    IO_STATUS_BLOCK result{}; HANDLE handle = INVALID_HANDLE_VALUE;
    auto status = open(&handle, access | SYNCHRONIZE, &attributes, &result, nullptr, FILE_ATTRIBUTE_NORMAL, share, disposition,
        0x20 /* FILE_SYNCHRONOUS_IO_NONALERT */ | 0x200000 /* FILE_OPEN_REPARSE_POINT */ | (directory ? 1u : 0x40u), nullptr, 0);
    if (status < 0) { SetLastError(error(status)); return Handle(); }
    return Handle(handle);
}
struct Cache {
    std::filesystem::path directory; Security security; std::vector<Handle> locks;
    explicit Cache(std::filesystem::path p) : directory(std::move(p)) {
        auto raw = directory.wstring();
        if (!directory.is_absolute() || raw.size() > 2000 || raw.size() < 3 || raw[1] != L':' || raw[2] != L'\\' || raw.rfind(L"\\\\", 0) == 0) fail("Cache must be an absolute local drive path");
        for (const auto& part : directory.relative_path()) {
            auto s = part.wstring();
            if (s.empty() || s == L"." || s == L".." || s.back() == L'.' || s.back() == L' ' || s.find_first_of(L":<>\"|?*") != std::wstring::npos ||
                std::any_of(s.begin(), s.end(), [](wchar_t ch) { return ch < 32; })) fail("Unsafe cache path component");
            auto base = s.substr(0, s.find(L'.'));
            for (auto& ch : base) if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
            if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" || base == L"CONIN$" || base == L"CONOUT$" ||
                (base.size() == 4 && (base.rfind(L"COM", 0) == 0 || base.rfind(L"LPT", 0) == 0) &&
                    ((base[3] >= L'0' && base[3] <= L'9') || base[3] == L'\u00b9' || base[3] == L'\u00b2' || base[3] == L'\u00b3'))) fail("DOS device cache component refused");
        }
        auto cursor = directory.root_path();
        Handle root(CreateFileW(cursor.c_str(), FILE_TRAVERSE | FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (root.value == INVALID_HANDLE_VALUE) fail("Cannot lock cache drive"); inspect_file(root.value, true); locks.push_back(std::move(root));
        if (directory == cursor) fail("Drive root is not a private update cache");
        for (const auto& part : directory.relative_path()) {
            cursor /= part;
            auto child = relative_open(locks.back().value, part.wstring(), FILE_TRAVERSE | FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, 3 /* FILE_OPEN_IF */, true, security.descriptor);
            if (child.value == INVALID_HANDLE_VALUE) fail("Cannot safely open private cache directory");
            inspect_file(child.value, true); if (cursor == directory) security.check(child.value); locks.push_back(std::move(child));
        }
        if (GetDriveTypeW(directory.root_path().c_str()) != DRIVE_FIXED) fail("Cache must be on a fixed local drive");
    }
};
std::string file_hash(HANDLE h, uint64_t expected) {
    LARGE_INTEGER size{}; win(GetFileSizeEx(h, &size) != FALSE, "Cannot inspect cached installer size"); if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) != expected) fail("Cached installer size mismatch");
    Hash hash; std::array<char, 65536> data{};
    for (;;) { DWORD n = 0; win(ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &n, nullptr) != FALSE, "Cannot read cached installer"); if (!n) break; hash.add(data.data(), n); } return hash.finish();
}
std::filesystem::path download(const Candidate& c, const std::filesystem::path& directory, const std::atomic<bool>& stop) {
    Cache cache(directory);
    // Only local, validated version and SHA256 text participate in the filename.
    auto target = directory / wide("EffectConsoleUpdate-" + c.version + "-" + c.hash + ".exe");
    {
        auto existing = relative_open(cache.locks.back().value, target.filename().wstring(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, 1 /* FILE_OPEN */, false);
        if (existing.value != INVALID_HANDLE_VALUE) { inspect_file(existing.value, false); cache.security.check(existing.value); if (file_hash(existing.value, c.size) != c.hash) fail("Cached installer hash mismatch; original retained"); return target; }
        if (GetLastError() != ERROR_FILE_NOT_FOUND) fail("Cannot safely access cached installer");
    }
    std::array<unsigned char, 16> random{}; if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) fail("Cannot generate temporary filename");
    std::wstring name = L".xfx-download-"; for (auto b : random) { name += L"0123456789abcdef"[b >> 4]; name += L"0123456789abcdef"[b & 15]; } name += L".tmp";
    auto file = relative_open(cache.locks.back().value, name, GENERIC_READ | GENERIC_WRITE | DELETE | READ_CONTROL, 0, 2 /* FILE_CREATE */, false, cache.security.descriptor);
    if (file.value == INVALID_HANDLE_VALUE) fail("Cannot create installer temporary file");
    try {
        inspect_file(file.value, false); Hash hash; uint64_t count = 0;
        fetch(c.url, c.size, [&](const char* data, size_t n) { DWORD written = 0; win(WriteFile(file.value, data, static_cast<DWORD>(n), &written, nullptr) != FALSE && written == n, "Cannot write installer temporary file"); hash.add(data, n); count += n; }, stop);
        if (count != c.size || hash.finish() != c.hash) fail("Installer SHA256/size verification failed");
        win(FlushFileBuffers(file.value) != FALSE, "Cannot flush verified installer");
        if (stop.load()) fail("Update cancelled");
        auto destination = target.filename().wstring(); size_t bytes = destination.size() * sizeof(wchar_t);
        std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + bytes); auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        rename->ReplaceIfExists = FALSE; rename->RootDirectory = cache.locks.back().value; rename->FileNameLength = static_cast<DWORD>(bytes); std::memcpy(rename->FileName, destination.data(), bytes);
        using Rename = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
        auto rename_file = reinterpret_cast<Rename>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
        IO_STATUS_BLOCK renamed{};
        if (!rename_file || rename_file(file.value, &renamed, rename, static_cast<ULONG>(buffer.size()), static_cast<FILE_INFORMATION_CLASS>(10) /* FileRenameInformation */) < 0)
            fail("Cannot publish verified installer; original retained");
    } catch (...) { FILE_DISPOSITION_INFO remove{TRUE}; SetFileInformationByHandle(file.value, FileDispositionInfo, &remove, sizeof(remove)); throw; }
    return target;
}
} // namespace update_detail

struct UpdateClient::Impl {
    std::string current; std::filesystem::path cache;
    mutable std::mutex mutex; UpdateInfo info; update_detail::Candidate candidate;
    std::thread worker; std::atomic<bool> stopping{false}; bool busy = false;
    std::mutex cancel_mutex; std::function<void()> cancel_io;
    Impl(std::string v, std::filesystem::path p) : current(std::move(v)), cache(std::move(p)) {}
    ~Impl() {
        stopping.store(true);
        std::function<void()> cancel;
        { std::lock_guard lock(cancel_mutex); cancel = cancel_io; }
        if (cancel) cancel();
        if (worker.joinable()) worker.join();
    }
    void start(bool downloading) {
        std::lock_guard lock(mutex);
        if (busy || stopping.load() || (downloading && info.state != UpdateInfo::Available)) return;
        // Worker publishes busy=false only after its last mutex access; joining that
        // finished thread here cannot wait for I/O or contend with its publication.
        if (worker.joinable()) worker.join();
        busy = true; info.state = downloading ? UpdateInfo::Downloading : UpdateInfo::Checking; info.message.clear(); info.installer.clear();
        if (!downloading) { info.version.clear(); info.notes.clear(); candidate = {}; }
        try { worker = std::thread([this, downloading] {
            update_detail::register_cancel = [this](std::function<void()> cancel) {
                std::lock_guard lock(cancel_mutex);
                cancel_io = std::move(cancel);
                if (stopping.load() && cancel_io) cancel_io();
            };
            struct ClearRegistration { ~ClearRegistration() { update_detail::register_cancel = {}; } } cleanup;
            try {
                if (downloading) {
                    auto path = update_detail::download(candidate, cache, stopping);
                    std::lock_guard guard(mutex); info.installer = std::move(path); info.state = UpdateInfo::Ready; info.message = "Installer verified; ready for explicit user action"; busy = false;
                } else {
                    (void)update_detail::version(current);
                    auto body = update_detail::text_fetch(update_detail::api_url, update_detail::metadata_limit, stopping, true);
                    auto found = update_detail::choose(body, current, stopping);
                    std::lock_guard guard(mutex); candidate = std::move(found); info.version = candidate.version; info.notes = candidate.notes;
                    info.state = candidate.version.empty() ? UpdateInfo::Current : UpdateInfo::Available;
                    info.message = candidate.version.empty() ? "No newer official Effect Console release" : "Official update available"; busy = false;
                }
            } catch (const std::exception& e) { std::lock_guard guard(mutex); info.state = UpdateInfo::Failed; info.installer.clear(); info.message = e.what(); busy = false; }
            catch (...) { std::lock_guard guard(mutex); info.state = UpdateInfo::Failed; info.installer.clear(); info.message = "Unexpected update failure"; busy = false; }
        }); } catch (...) { busy = false; info.state = UpdateInfo::Failed; info.message = "Cannot start update worker"; }
    }
};
UpdateClient::UpdateClient(std::string current_version, std::filesystem::path cache_dir) : impl_(std::make_unique<Impl>(std::move(current_version), std::move(cache_dir))) {}
UpdateClient::~UpdateClient() = default;
void UpdateClient::check() { impl_->start(false); }
void UpdateClient::download() { impl_->start(true); }
UpdateInfo UpdateClient::status() const { std::lock_guard lock(impl_->mutex); return impl_->info; }
std::shared_ptr<void> UpdateClient::lock_ready_installer() const {
    update_detail::Candidate candidate; std::filesystem::path directory, selected;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->busy || impl_->info.state != UpdateInfo::Ready) update_detail::fail("Installer is not ready");
        candidate = impl_->candidate; directory = impl_->cache; selected = impl_->info.installer;
    }
    const auto expected = directory / update_detail::wide("EffectConsoleUpdate-" + candidate.version + "-" + candidate.hash + ".exe");
    if (selected != expected) update_detail::fail("Installer path changed");
    struct Lease {
        update_detail::Cache cache;
        update_detail::Handle file;
        Lease(const std::filesystem::path& dir, const std::filesystem::path& target,
              const update_detail::Candidate& c) : cache(dir),
            file(update_detail::relative_open(cache.locks.back().value, target.filename().wstring(),
                GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, 1, false)) {
            if (file.value == INVALID_HANDLE_VALUE) update_detail::fail("Cannot lock installer for launch");
            update_detail::inspect_file(file.value, false); cache.security.check(file.value);
            if (update_detail::file_hash(file.value, c.size) != c.hash) update_detail::fail("Installer changed after download");
            // ShellExecute opens by pathname. Ensure that pathname currently names
            // this exact pinned object, not a redirected ancestor or another file.
            update_detail::Handle path(CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (path.value == INVALID_HANDLE_VALUE) update_detail::fail("Installer launch path unavailable");
            update_detail::inspect_file(path.value, false);
            BY_HANDLE_FILE_INFORMATION a{}, b{};
            if (!GetFileInformationByHandle(file.value, &a) || !GetFileInformationByHandle(path.value, &b) ||
                a.dwVolumeSerialNumber != b.dwVolumeSerialNumber || a.nFileIndexHigh != b.nFileIndexHigh || a.nFileIndexLow != b.nFileIndexLow)
                update_detail::fail("Installer launch path identity changed");
        }
    };
    return std::make_shared<Lease>(directory, expected, candidate);
}
} // namespace xfx
