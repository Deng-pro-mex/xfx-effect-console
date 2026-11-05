#define XFX_QA_IDENTITY_TESTING
#include "qa_host_identity.cpp"
#include <functional>
#include <iostream>
#include <thread>

namespace {
using namespace xfx_qa;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()>& action) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("expected fail-closed rejection");
}
struct FakeSource final : Source {
    Process host{42, 0x0123456789abcdefULL, L"C:\\AE\\AfterFX.exe"};
    Version version{25,4,0,86};
    std::array<std::wstring,3> paths{L"C:\\AE\\FLT.dll", L"C:\\AE\\TDB.dll", L"C:\\AE\\U.dll"};
    bool hash_failure = false, path_mismatch = false, module_version_mismatch = false;
    std::string digest = std::string(64,'a');
    int observations = 0, module_queries = 0;
    std::function<void()> on_observe;
    Process process() override { return host; }
    std::wstring module(std::wstring_view name) override {
        ++module_queries;
        if (name == L"FLT.dll") return paths[0];
        if (name == L"TDB.dll") return paths[1];
        if (name == L"U.dll") return paths[2];
        throw std::runtime_error("unexpected module lookup");
    }
    Evidence observe(const std::wstring& path) override {
        ++observations;
        if (on_observe) on_observe();
        if (hash_failure) throw std::runtime_error("fake read/hash failed");
        return {path_mismatch ? L"C:\\OTHER\\AfterFX.exe" : path,
            module_version_mismatch && path != host.image ? Version{25,3,0,86} : version, digest};
    }
};
std::string call_identity() {
    QaTaggedData out{};
    require(identity(nullptr,0,&out) == 0, "identity did not succeed");
    require(out.type == type_string && out.filler == 0 && out.data.string, "string TaggedData ownership/type broken");
    std::string value(out.data.string); ESFreeMem(out.data.string); return value;
}
void error(long expected, QaTaggedData* args = nullptr, long count = 0) {
    QaTaggedData out{}; out.type = 999; out.filler = 999; out.data.number = 9;
    require(identity(args,count,&out) == expected, "wrong ABI return error code");
    require(out.type == type_integer && out.filler == 0 && out.data.integer == expected,
        "error out not initialized integer code");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    int checks = 0, failures = 0;
    const auto test = [&](const char* name, const std::function<void()>& action) {
        ++checks;
        try { action(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& failure) { ++failures; std::cerr << "FAIL " << name << ": " << failure.what() << '\n'; }
        ESTerminate(); injected_source = nullptr;
    };
    test("fixed-version schema matches AE 25.4x86 and optional nonzero build", [] {
        require(app_version({25,4,0,86}) == "25.4x86", "zero build was emitted or private omitted");
        require(app_version({25,4,2,86}) == "25.4.2x86", "nonzero build omitted");
        require(app_version({25,0,0,0}) == "25.0x0", "zero minor/private schema changed");
    });
    test("UTF16 JSON escapes every ASCII and paired surrogate unit in lowercase", [] {
        require(quote_utf16(L"A\\\"\n中\xD83C\xDFAC") ==
            "\"\\u0041\\u005c\\u0022\\u000a\\u4e2d\\ud83c\\udfac\"", "noncanonical image escaping");
        rejects([] { (void)quote_utf16(std::wstring(1,static_cast<wchar_t>(0xD800))); });
        rejects([] { (void)quote_utf16(std::wstring(1,static_cast<wchar_t>(0xDC00))); });
    });
    test("identity emits exact strict field order without extra metadata or whitespace", [] {
        FakeSource source;
        const std::string expected =
            "{\"pid\":42,\"creationTime\":\"0123456789abcdef\",\"image\":\""
            "\\u0043\\u003a\\u005c\\u0041\\u0045\\u005c\\u0041\\u0066\\u0074\\u0065\\u0072\\u0046\\u0058\\u002e\\u0065\\u0078\\u0065"
            "\",\"version\":\"25.4x86\",\"modules\":{\"FLT\":\"" + std::string(64,'a') +
            "\",\"TDB\":\"" + std::string(64,'a') + "\",\"U\":\"" + std::string(64,'a') + "\"}}";
        require(collect_identity(source) == expected, "identity strict JSON schema mismatch");
        require(source.module_queries == 3 && source.observations == 4, "did not observe exact executable and 3 loaded modules");
    });
    test("only exact AE executable basenames accepted before any file IO", [] {
        for (const auto* image : {L"C:\\AE\\foo.exe", L"C:\\AE\\AfterFX.exe.evil", L"C:\\AE\\AfterFXBeta.com", L"C:\\AE\\AfterFXa.exe"}) {
            FakeSource source; source.host.image = image;
            rejects([&] { (void)collect_identity(source); });
            require(source.observations == 0, "non-AE host opened a file");
        }
        for (const auto* image : {L"C:\\AE\\AfterFXBeta.exe", L"C:\\AE\\AfterFX.com", L"C:\\AE\\afterfx.EXE"}) {
            FakeSource source; source.host.image = image;
            require(!collect_identity(source).empty(), "whitelisted AE basename rejected");
        }
    });
    test("missing loaded module fails closed without fabricating a disk lookup", [] {
        for (std::size_t index = 0; index < 3; ++index) {
            FakeSource source; source.paths[index].clear();
            rejects([&] { (void)collect_identity(source); });
        }
    });
    test("module outside executable directory or wrong basename is rejected", [] {
        for (const auto* path : {L"C:\\evil\\FLT.dll", L"C:\\AE\\other.dll", L"C:\\AEevil\\FLT.dll", L"FLT.dll", L"C:\\AE\\..\\FLT.dll"}) {
            FakeSource source; source.paths[0] = path;
            rejects([&] { (void)collect_identity(source); });
        }
    });
    test("canonical path mismatch module version mismatch and hash IO errors fail closed", [] {
        FakeSource source; source.path_mismatch = true;
        rejects([&] { (void)collect_identity(source); });
        source.path_mismatch = false; source.module_version_mismatch = true;
        rejects([&] { (void)collect_identity(source); });
        source.module_version_mismatch = false; source.hash_failure = true;
        rejects([&] { (void)collect_identity(source); });
    });
    test("hash must be complete lowercase SHA256 and process identity must exist", [] {
        for (const auto& hash : {std::string(63,'a'), std::string(65,'a'), std::string(64,'A'), std::string(64,'z'), std::string()}) {
            FakeSource source; source.digest = hash;
            rejects([&] { (void)collect_identity(source); });
        }
        FakeSource source; source.host.pid = 0;
        rejects([&] { (void)collect_identity(source); });
        source.host.pid = 42; source.host.creation = 0;
        rejects([&] { (void)collect_identity(source); });
    });
    test("ABI signature lifecycle count out and malloc string ownership", [] {
        require(ESGetVersion() == 1, "ES protocol version changed");
        error(wrong_lifecycle);
        require(std::string(ESInitialize(nullptr,0)) == "identity", "incorrect registered method signature");
        FakeSource source; injected_source = &source;
        require(call_identity() == collect_identity(source), "ABI string did not contain collector JSON");
        QaTaggedData argument{};
        error(bad_argument,&argument,1); error(bad_argument,nullptr,-1); error(bad_argument,nullptr,1);
        require(identity(nullptr,0,nullptr) == bad_argument, "null out accepted");
        require(ESInitialize(nullptr,1) == nullptr, "initialize unexpected args accepted");
        ESFreeMem(nullptr); ESTerminate(); error(wrong_lifecycle);
    });
    test("different thread cannot query reinitialize or terminate owner session", [] {
        require(ESInitialize(nullptr,0) != nullptr, "initialization failed");
        FakeSource source; injected_source = &source;
        bool query_rejected = false, init_rejected = false;
        std::thread worker([&] {
            QaTaggedData out{};
            query_rejected = identity(nullptr,0,&out) == wrong_lifecycle && out.type == type_integer;
            init_rejected = ESInitialize(nullptr,0) == nullptr;
            ESTerminate();
        });
        worker.join();
        require(query_rejected && init_rejected, "thread ownership bypass");
        require(!call_identity().empty(), "foreign terminate cleared owner");
    });
    test("same-thread nested identity initialize and terminate fail closed without blocking", [] {
        require(ESInitialize(nullptr,0) != nullptr, "initialization failed");
        FakeSource source; injected_source = &source;
        int nested_calls = 0;
        source.on_observe = [&] {
            error(wrong_lifecycle);
            require(ESInitialize(nullptr,0) == nullptr, "nested initializer bypassed busy state");
            ESTerminate();
            ++nested_calls;
        };
        require(!call_identity().empty() && nested_calls == 4, "outer query did not survive reentry");
        source.on_observe = {};
        require(!call_identity().empty(), "nested terminate cleared owner state");
    });
    test("collector failures become integer rejection without string ownership leak", [] {
        require(ESInitialize(nullptr,0) != nullptr, "initialization failed");
        FakeSource source; source.hash_failure = true; injected_source = &source;
        error(rejected);
    });
    test("production collector rejects this actual non-AE test host", [] {
        require(ESInitialize(nullptr,0) != nullptr, "initialization failed");
        injected_source = nullptr; error(rejected);
    });
    test("production DLL exports ABI and refuses actual non-AE host with no overrides", [&] {
        require(argc == 3, "missing production DLL path or independent SHA256");
        HMODULE dll = LoadLibraryW(argv[1]); require(dll != nullptr, "could not load our own QA DLL");
        struct ModuleCloser { HMODULE value; ~ModuleCloser() { FreeLibrary(value); } } closer{dll};
        using Initialize = char* (*)(QaTaggedData*,long);
        using Query = long (*)(QaTaggedData*,long,QaTaggedData*);
        using Terminate = void (*)();
        using Free = void (*)(void*);
        using GetVersion = long (*)();
        const auto initialize = reinterpret_cast<Initialize>(GetProcAddress(dll,"ESInitialize"));
        const auto query = reinterpret_cast<Query>(GetProcAddress(dll,"identity"));
        const auto terminate = reinterpret_cast<Terminate>(GetProcAddress(dll,"ESTerminate"));
        const auto free_memory = reinterpret_cast<Free>(GetProcAddress(dll,"ESFreeMem"));
        const auto get_version = reinterpret_cast<GetVersion>(GetProcAddress(dll,"ESGetVersion"));
        require(initialize && query && terminate && free_memory && get_version, "ABI export missing");
        require(!GetProcAddress(dll,"injected_source"), "production exported identity override");
        require(get_version() == 1 && std::string(initialize(nullptr,0)) == "identity", "production ABI registration mismatch");
        QaTaggedData out{};
        require(query(nullptr,0,&out) == rejected && out.type == type_integer && out.data.integer == rejected,
            "actual non-AE executable was authenticated");
        free_memory(nullptr); terminate();
    });
    test("file size and CNG object allocation caps reject zero and oversized values", [] {
        validate_module_size(1); validate_module_size(128ULL * 1024ULL * 1024ULL);
        rejects([] { validate_module_size(0); });
        rejects([] { validate_module_size(128ULL * 1024ULL * 1024ULL + 1); });
        validate_provider_object_size(1); validate_provider_object_size(1024u * 1024u);
        rejects([] { validate_provider_object_size(0); });
        rejects([] { validate_provider_object_size(1024u * 1024u + 1); });
    });
    test("real BCrypt streaming SHA256 matches independent Get-FileHash and rejects wrong size", [&] {
        require(argc == 3, "missing production DLL path or independent SHA256");
        const std::wstring wide_hash(argv[2]);
        std::string expected;
        for (const wchar_t unit : wide_hash) {
            require(unit <= 127, "independent digest is not ASCII");
            expected += static_cast<char>(unit);
        }
        valid_digest(expected);
        Handle file(CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        require(file.get() != INVALID_HANDLE_VALUE, "cannot open our DLL read-only");
        const auto before = information(file.get());
        const auto bytes = (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
        require(bytes > 65536, "fixture does not cover multi-block streaming");
        require(sha256(file.get(),bytes) == expected, "real CNG hash differs from independent Get-FileHash");
        require(same_file(before,information(file.get())), "read-only hash changed file identity");
        require(equal_path(final_path(file.get()),checked_path(argv[1])), "handle canonical path binding changed");
        LARGE_INTEGER zero{};
        require(SetFilePointerEx(file.get(),zero,nullptr,FILE_BEGIN) != FALSE, "cannot reset read handle");
        rejects([&] { (void)sha256(file.get(),0); });
        rejects([&] { (void)sha256(file.get(),bytes + 1); });
    });
    std::cout << checks << " tests, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
