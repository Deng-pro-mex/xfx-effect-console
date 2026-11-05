// Independent read-only QA ExternalObject, implemented from the documented ABI.
// This is NOT an AEX, installer, AE adapter, project operation or code attestation.
// SHA-256 describes the files on disk backing already-loaded module paths, not
// live executable pages. No memory scanning, module loading, networking or writes.
// IMPORTANT: ReadFile, version-resource reads and BCrypt are synchronous here.
// They have no timeout or forced cancellation and could block the calling host
// thread (including reported AE/CNG/file-IO interactions). Do not load in AE
// until separately approved in a disposable project and reviewed for this risk.
// This helper never creates workers, pins/leaks its own DLL, or pumps messages.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma pack(push, 8)
struct QaTaggedData {
    union { long integer; double number; char* string; void* object; } data;
    long type;
    long filler;
};
#pragma pack(pop)
static_assert(sizeof(void*) == 8, "QA ExternalObject supports Windows x64 only");
static_assert(sizeof(long) == 4 && sizeof(QaTaggedData) == 16 && alignof(QaTaggedData) == 8);
static_assert(offsetof(QaTaggedData, type) == 8 && offsetof(QaTaggedData, filler) == 12);

namespace xfx_qa {
constexpr long type_string = 4, type_integer = 123;
constexpr long bad_argument = 20, rejected = 32, wrong_lifecycle = 33, no_memory = -28;
struct Version {
    unsigned major = 0, minor = 0, build = 0, private_part = 0;
    bool operator==(const Version&) const = default;
};
struct Process { DWORD pid = 0; std::uint64_t creation = 0; std::wstring image; };
struct Evidence { std::wstring canonical; Version version; std::string sha256; };
struct Source {
    virtual ~Source() = default;
    virtual Process process() = 0;
    virtual std::wstring module(std::wstring_view name) = 0;
    virtual Evidence observe(const std::wstring& path) = 0;
};
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
constexpr char hex[] = "0123456789abcdef";
std::string hex64(std::uint64_t value) {
    std::string text(16,'0');
    for (int index = 15; index >= 0; --index) { text[static_cast<std::size_t>(index)] = hex[value & 15u]; value >>= 4; }
    return text;
}
std::string app_version(const Version& value) {
    if (value.major == 0 || value.major > 65535 || value.minor > 65535 ||
        value.build > 65535 || value.private_part > 65535) fail("Invalid fixed executable version");
    return std::to_string(value.major) + '.' + std::to_string(value.minor) +
        (value.build == 0 ? "" : "." + std::to_string(value.build)) + 'x' + std::to_string(value.private_part);
}
std::string quote_utf16(std::wstring_view value) {
    std::string result = "\"";
    bool expects_low = false;
    for (const auto unit : value) {
        const auto scalar = static_cast<unsigned>(unit);
        if (expects_low) {
            if (scalar < 0xDC00 || scalar > 0xDFFF) fail("Unpaired UTF-16 high surrogate");
            expects_low = false;
        } else if (scalar >= 0xD800 && scalar <= 0xDBFF) {
            expects_low = true;
        } else if (scalar >= 0xDC00 && scalar <= 0xDFFF) {
            fail("Unpaired UTF-16 low surrogate");
        }
        result += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) result += hex[(scalar >> shift) & 15u];
    }
    if (expects_low) fail("Unpaired UTF-16 high surrogate");
    result += '"'; return result;
}
std::wstring checked_path(std::wstring_view input) {
    if (input.starts_with(L"\\\\?\\")) input.remove_prefix(4);
    if (input.size() < 4 || input.size() >= 32768 || input[1] != L':' || input[2] != L'\\' ||
        !((input[0] >= L'A' && input[0] <= L'Z') || (input[0] >= L'a' && input[0] <= L'z')))
        fail("Expected absolute local drive path");
    if (input.find(L'/') != std::wstring_view::npos || input.find(L':',2) != std::wstring_view::npos ||
        input.find(L'\0') != std::wstring_view::npos)
        fail("Ambiguous file path");
    std::size_t begin = 3;
    while (begin < input.size()) {
        const auto slash = input.find(L'\\', begin);
        const auto part = input.substr(begin, slash == std::wstring_view::npos ? input.size() - begin : slash - begin);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
            fail("Noncanonical file path segment");
        if (slash == std::wstring_view::npos) break;
        begin = slash + 1;
        if (begin == input.size()) fail("Expected file path, not directory");
    }
    (void)quote_utf16(input);
    return std::wstring(input);
}
bool equal_path(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) return false;
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
        static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}
std::wstring_view basename(std::wstring_view path) { return path.substr(path.find_last_of(L'\\') + 1); }
std::wstring_view directory(std::wstring_view path) { return path.substr(0,path.find_last_of(L'\\')); }
void valid_digest(std::string_view value) {
    if (value.size() != 64 || value.find_first_not_of("0123456789abcdef") != std::string_view::npos)
        fail("Expected lowercase SHA256");
}
std::string collect_identity(Source& source) {
    const auto host = source.process();
    const auto image = checked_path(host.image);
    const auto name = basename(image);
    if (!host.pid || !host.creation ||
        !(equal_path(name,L"AfterFX.exe") || equal_path(name,L"AfterFXBeta.exe") || equal_path(name,L"AfterFX.com")))
        fail("Not an allowed AE process");
    const auto executable = source.observe(image);
    const auto canonical_image = checked_path(executable.canonical);
    if (!equal_path(image,canonical_image)) fail("Executable path binding mismatch");
    valid_digest(executable.sha256);
    const auto version = app_version(executable.version);
    constexpr std::array<std::wstring_view,3> names{L"FLT.dll",L"TDB.dll",L"U.dll"};
    constexpr std::array<std::string_view,3> keys{"FLT","TDB","U"};
    std::string modules;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto loaded = checked_path(source.module(names[index]));
        if (!equal_path(basename(loaded),names[index]) || !equal_path(directory(loaded),directory(canonical_image)))
            fail("Loaded module is outside executable directory or has wrong name");
        const auto evidence = source.observe(loaded);
        const auto canonical = checked_path(evidence.canonical);
        if (!equal_path(canonical,loaded) || !equal_path(directory(canonical),directory(canonical_image)) ||
            evidence.version != executable.version) fail("Loaded module version or path binding mismatch");
        valid_digest(evidence.sha256);
        if (index) modules += ',';
        modules += '"'; modules += keys[index]; modules += "\":\"" + evidence.sha256 + '"';
    }
    return "{\"pid\":" + std::to_string(host.pid) + ",\"creationTime\":\"" + hex64(host.creation) +
        "\",\"image\":" + quote_utf16(canonical_image) + ",\"version\":\"" + version +
        "\",\"modules\":{" + modules + "}}";
}

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle() { if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_,INVALID_HANDLE_VALUE)) {}
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};
BY_HANDLE_FILE_INFORMATION information(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle,&info)) fail("Cannot read file identity");
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) fail("Reparse points are forbidden");
    return info;
}
bool same_file(const BY_HANDLE_FILE_INFORMATION& before, const BY_HANDLE_FILE_INFORMATION& after) {
    return before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
        before.nFileIndexHigh == after.nFileIndexHigh && before.nFileIndexLow == after.nFileIndexLow &&
        before.nFileSizeHigh == after.nFileSizeHigh && before.nFileSizeLow == after.nFileSizeLow &&
        before.ftLastWriteTime.dwHighDateTime == after.ftLastWriteTime.dwHighDateTime &&
        before.ftLastWriteTime.dwLowDateTime == after.ftLastWriteTime.dwLowDateTime &&
        before.dwFileAttributes == after.dwFileAttributes;
}
std::wstring final_path(HANDLE handle) {
    std::wstring path(32768,L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle,path.data(),static_cast<DWORD>(path.size()),FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= path.size()) fail("Cannot bind canonical handle path");
    path.resize(length); return checked_path(path);
}
std::wstring module_path(HMODULE module) {
    std::wstring path(32768,L'\0');
    const DWORD length = GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) fail("Cannot read loaded module path");
    path.resize(length); return checked_path(path);
}
Version file_version(const std::wstring& path) {
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(),&unused);
    if (!size || size > 16u * 1024u * 1024u) fail("Missing or oversized file version resource");
    std::vector<unsigned char> storage(size);
    if (!GetFileVersionInfoW(path.c_str(),0,size,storage.data())) fail("Cannot read fixed file version");
    void* pointer = nullptr; UINT bytes = 0;
    if (!VerQueryValueW(storage.data(),L"\\",&pointer,&bytes) || !pointer || bytes < sizeof(VS_FIXEDFILEINFO))
        fail("Missing fixed file version");
    VS_FIXEDFILEINFO version{}; std::memcpy(&version,pointer,sizeof(version));
    if (version.dwSignature != VS_FFI_SIGNATURE) fail("Invalid fixed version signature");
    return {HIWORD(version.dwFileVersionMS),LOWORD(version.dwFileVersionMS),
        HIWORD(version.dwFileVersionLS),LOWORD(version.dwFileVersionLS)};
}
void validate_module_size(std::uint64_t bytes) {
    if (bytes == 0 || bytes > 128ULL * 1024ULL * 1024ULL) fail("Executable/module must be between 1 byte and 128 MiB");
}
void validate_provider_object_size(DWORD bytes) {
    if (bytes == 0 || bytes > 1024u * 1024u) fail("CNG hash object must fit 1 MiB");
}
void ntcheck(NTSTATUS status) { if (status < 0) fail("BCrypt SHA256 operation failed"); }
std::string sha256(HANDLE file, std::uint64_t expected_size) {
    validate_module_size(expected_size);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    ntcheck(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    struct CloseAlgorithm { BCRYPT_ALG_HANDLE value; ~CloseAlgorithm() { BCryptCloseAlgorithmProvider(value,0); } } close_algorithm{algorithm};
    DWORD object_size = 0, digest_size = 0, bytes = 0;
    ntcheck(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&bytes,0));
    ntcheck(BCryptGetProperty(algorithm,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&digest_size),sizeof(digest_size),&bytes,0));
    validate_provider_object_size(object_size);
    if (digest_size != 32) fail("Unexpected SHA256 provider");
    std::vector<unsigned char> object(object_size);
    BCRYPT_HASH_HANDLE hash = nullptr;
    ntcheck(BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0));
    struct DestroyHash { BCRYPT_HASH_HANDLE value; ~DestroyHash() { BCryptDestroyHash(value); } } destroy_hash{hash};
    std::array<unsigned char,65536> buffer{};
    std::uint64_t total = 0;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr)) fail("Cannot read module file bytes");
        if (read == 0) break;
        if (read > expected_size - total) fail("Module size changed while hashing");
        total += read;
        ntcheck(BCryptHashData(hash,buffer.data(),read,0));
    }
    if (total != expected_size) fail("Module size changed while hashing");
    std::array<unsigned char,32> digest{};
    ntcheck(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0));
    std::string text; text.reserve(64);
    for (const auto byte : digest) { text += hex[byte >> 4]; text += hex[byte & 15u]; }
    return text;
}
class WindowsSource final : public Source {
public:
    Process process() override {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)) fail("Cannot read process creation time");
        return {GetCurrentProcessId(), (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime,
            module_path(nullptr)};
    }
    std::wstring module(std::wstring_view name) override {
        const std::wstring terminated(name);
        HMODULE loaded = GetModuleHandleW(terminated.c_str());
        if (!loaded) fail("Required module is not loaded");
        return module_path(loaded);
    }
    Evidence observe(const std::wstring& input) override {
        const auto path = checked_path(input);
        // Pin every directory from volume root to parent, rejecting all reparse
        // ancestors. Share only reads: normal writes/renames/replacements cannot
        // race the version/path/hash observation while these handles are held.
        const auto pin_directory = [&](std::size_t end) {
            const auto ancestor = path.substr(0,end);
            Handle directory_handle(CreateFileW(ancestor.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,
                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            if (directory_handle.get() == INVALID_HANDLE_VALUE) fail("Cannot pin path ancestry");
            if (!(information(directory_handle.get()).dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) fail("Invalid path ancestor");
            pinned_.push_back(std::move(directory_handle));
        };
        pin_directory(3);
        for (std::size_t slash = path.find(L'\\',3); slash != std::wstring::npos; slash = path.find(L'\\',slash + 1))
            pin_directory(slash);
        Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) fail("Cannot open immutable read-only file observation");
        const auto before = information(file.get());
        if (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) fail("Expected module file");
        const auto canonical = final_path(file.get());
        if (!equal_path(canonical,path)) fail("File handle canonical path mismatch");
        const auto size = (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
        validate_module_size(size);
        const auto version = file_version(canonical);
        const auto digest = sha256(file.get(),size);
        if (!same_file(before,information(file.get())) || !equal_path(canonical,final_path(file.get())))
            fail("File identity changed during observation");
        pinned_.push_back(std::move(file));
        return {canonical,version,digest};
    }
private:
    std::vector<Handle> pinned_;
};
std::string production_identity() { WindowsSource source; return collect_identity(source); }
// A recursive mutex plus explicit busy state makes same-thread reentry defined:
// std::mutex::try_lock on a mutex already owned by this thread is not portable.
std::recursive_mutex lifecycle_mutex;
DWORD owner_thread = 0;
bool collecting = false;
#ifdef XFX_QA_IDENTITY_TESTING
// Test binary only. No exported setter and no overrides in the production DLL.
Source* injected_source = nullptr;
#endif
} // namespace xfx_qa

#define QA_EXPORT extern "C" __declspec(dllexport)
QA_EXPORT long ESGetVersion() { return 1; }
QA_EXPORT char* ESInitialize(QaTaggedData*, long argc) {
    if (argc != 0) return nullptr;
    try {
        std::unique_lock lock(xfx_qa::lifecycle_mutex,std::try_to_lock);
        if (!lock.owns_lock() || xfx_qa::collecting) return nullptr;
        const DWORD thread = GetCurrentThreadId();
        if (xfx_qa::owner_thread != 0 && xfx_qa::owner_thread != thread) return nullptr;
        xfx_qa::owner_thread = thread;
        static char signature[] = "identity";
        return signature;
    } catch (...) { return nullptr; }
}
QA_EXPORT void ESTerminate() {
    try {
        std::unique_lock lock(xfx_qa::lifecycle_mutex,std::try_to_lock);
        if (!lock.owns_lock() || xfx_qa::collecting) return;
        if (xfx_qa::owner_thread == GetCurrentThreadId()) xfx_qa::owner_thread = 0;
    } catch (...) {}
}
QA_EXPORT void ESFreeMem(void* memory) { std::free(memory); }
QA_EXPORT long identity(QaTaggedData*, long argc, QaTaggedData* out) {
    if (!out) return xfx_qa::bad_argument;
    *out = {};
    const auto error = [&](long code) {
        out->type = xfx_qa::type_integer; out->data.integer = code; return code;
    };
    if (argc != 0) return error(xfx_qa::bad_argument);
    try {
        std::unique_lock lock(xfx_qa::lifecycle_mutex,std::try_to_lock);
        if (!lock.owns_lock() || xfx_qa::collecting || xfx_qa::owner_thread == 0 ||
            xfx_qa::owner_thread != GetCurrentThreadId()) return error(xfx_qa::wrong_lifecycle);
        xfx_qa::collecting = true;
        struct ResetBusy { ~ResetBusy() { xfx_qa::collecting = false; } } reset_busy;
#ifdef XFX_QA_IDENTITY_TESTING
        const auto json = xfx_qa::injected_source ? xfx_qa::collect_identity(*xfx_qa::injected_source) : xfx_qa::production_identity();
#else
        const auto json = xfx_qa::production_identity();
#endif
        auto* memory = static_cast<char*>(std::malloc(json.size() + 1));
        if (!memory) return error(xfx_qa::no_memory);
        std::memcpy(memory,json.c_str(),json.size() + 1);
        out->data.string = memory; out->type = xfx_qa::type_string;
        return 0;
    } catch (const std::bad_alloc&) { return error(xfx_qa::no_memory); }
    catch (...) { return error(xfx_qa::rejected); }
}
