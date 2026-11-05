// Offline FakeMemory regression only. Never loads a DLL, scans AE, or writes host memory.
#include "qa_parameter_inspect.cpp"
#include <functional>
#include <iostream>
#include <map>

namespace {
using namespace xfx_qa_inspect;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct FakeMemory final : Memory {
    static constexpr Address base = 0x10000;
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x70000, 0);
    std::vector<Region> pages{{base, 0x8000, true, true, true}};
    Address short_at = 0, stale_at = 0;
    std::size_t reads = 0, enumerations = 0;
    std::uint64_t clock = 0, tick = 0;
    bool repeating_region = false, backwards_clock = false;
    std::map<Address, unsigned> per_address;
    std::size_t read(Address at, void* output, std::size_t size) override {
        ++reads; const auto occurrence = ++per_address[at];
        if (at < base || size > bytes.size() || at - base > bytes.size() - size) return 0;
        const auto got = at == short_at ? size - 1 : size;
        std::memcpy(output, bytes.data() + static_cast<std::size_t>(at - base), got);
        if (at == stale_at && occurrence > 1 && got) static_cast<unsigned char*>(output)[0] ^= 8;
        return got;
    }
    bool next_region(Address cursor, Region& output) override {
        ++enumerations;
        if (repeating_region) { output = pages.front(); return true; }
        for (const auto& page : pages) if (page.base >= cursor) { output = page; return true; }
        return false;
    }
    std::uint64_t now_ms() override { if (backwards_clock) return clock--; clock += tick; return clock; }
    void number(Address at, std::uint64_t value) { put(at, &value, sizeof(value)); }
    void put(Address at, const void* value, std::size_t size) {
        require(at >= base && at - base + size <= bytes.size(), "fixture write out of bounds");
        std::memcpy(bytes.data() + static_cast<std::size_t>(at - base), value, size);
    }
};
struct Fixture {
    FakeMemory memory;
    Identity identity = exact_identity();
    Limits limits;
    static constexpr Address parade = 0x20000, vector = 0x21000;
    std::array<Address, 4> roots{0x10000, 0x12000, 0x14000, 0x16000};
    Fixture() {
        identity.flt_base = 0x180000000ULL;
        memory.number(parade + 0x220, vector); memory.number(parade + 0x228, vector + 32);
        for (std::size_t i = 0; i < roots.size(); ++i) {
            const auto root = roots[i], head = Address{0x30000} + i * 0x1000;
            const auto node = Address{0x40000} + i * 0x1000, child = Address{0x50000} + i * 0x1000;
            memory.number(vector + i * 8, root);
            memory.number(root, identity.flt_base + root_vtable_rva);
            memory.number(root + 0x20, parade);
            std::array<unsigned char, 16> root_guid{}; root_guid.fill(0x11); memory.put(root + 8, root_guid.data(), root_guid.size());
            memory.number(root + 0x228, head); memory.number(root + 0x230, 1);
            memory.number(head, node); memory.number(head + 8, node);
            memory.number(node, head); memory.number(node + 8, head);
            std::array<unsigned char, 16> guid{}; guid.fill(0x22); memory.put(node + 16, guid.data(), guid.size());
            memory.put(child + 8, guid.data(), guid.size());
            memory.number(node + 32, child); memory.number(child + 0x20, root);
            const char label[] = "Slider"; memory.put(child + 0x40, label, sizeof(label));
            const char16_t display[] = u"Slider"; memory.put(child + 0xf0, display, sizeof(display));
            memory.number(child + 0xf0 + 16, 6); memory.number(child + 0xf0 + 24, 7);
        }
    }
    Report inspect() {
        const auto before = memory.bytes;
        const auto result = inspect_candidates(memory, identity, limits);
        require(memory.bytes == before, "read-only inspection modified FakeMemory bytes");
        return result;
    }
};
void rejected(Fixture& fixture) {
    const auto result = fixture.inspect();
    require(!result.supported && result.rows.empty() && !result.reason.empty(), "malformed memory was not fail-closed");
    require(!result.runtime_enabled && !result.host_verified && !result.public_identity_verified,
        "structural QA became an enabling or host/public identity certificate");
}
}
int wmain() {
    int checks = 0, failures = 0;
    const auto test = [&](const char* name, const std::function<void()>& action) {
        ++checks;
        try { action(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("exact four same-parade candidates preserve raw GUID PF and UTF16 observations", [] {
        Fixture f; const auto report = f.inspect();
        require(report.supported && report.rows.size() == 4, "valid structural fixture was not observed");
        require(!report.runtime_enabled && !report.host_verified && !report.public_identity_verified,
            "candidate report falsely enabled private ABI or named identity");
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& row = report.rows[i];
            require(row.root == f.roots[i] && row.parent == Fixture::parade && row.pf_name == "Slider" && row.display == u"Slider",
                "candidate address/order/labels changed");
            require(row.guid[0] == 0x22 && row.root_guid[0] == 0x11, "raw GUID was converted or lost");
        }
    });
    test("supplied exact identity mismatch refuses before every memory observation", [] {
        for (unsigned which = 0; which < 5; ++which) {
            Fixture f;
            if (which == 0) f.identity.version = "25.4.0.87";
            if (which >= 1 && which <= 3) f.identity.hashes[which - 1][0] = '0';
            if (which == 4) f.identity.flt_base = 0;
            rejected(f); require(f.memory.reads == 0 && f.memory.enumerations == 0, "bad identity reached memory");
        }
    });
    test("foreign parade stale parent and wrong vtable cannot become complete candidates", [] {
        for (unsigned which = 0; which < 5; ++which) {
            Fixture f;
            if (which == 0) f.memory.number(f.roots[3] + 0x20, 0x22000);
            if (which == 1) f.memory.number(f.roots[3], 0x180000008ULL);
            if (which == 2) f.memory.stale_at = f.roots[0] + 0x20;
            if (which == 3) f.memory.number(0x50000 + 0x20, f.roots[1]);
            if (which == 4) f.memory.stale_at = 0x50040;
            rejected(f);
        }
    });
    test("parade vector count duplicate alignment and unsigned count overflow fail closed", [] {
        for (unsigned which = 0; which < 5; ++which) {
            Fixture f;
            if (which == 0) f.memory.number(Fixture::parade + 0x228, Fixture::vector + 40);
            if (which == 1) f.memory.number(Fixture::vector + 8, f.roots[0]);
            if (which == 2) f.memory.number(Fixture::parade + 0x220, Fixture::vector + 1);
            if (which == 3) f.memory.number(Fixture::parade + 0x228, Fixture::vector - 8);
            if (which == 4) f.memory.number(f.roots[0] + 0x230, 0x7ff0000000000000ULL); // IEEE infinity bits are not a count.
            rejected(f);
        }
    });
    test("named container null pointers loops count and backlink mismatches fail closed", [] {
        for (unsigned which = 0; which < 7; ++which) {
            Fixture f;
            if (which == 0) f.memory.number(f.roots[0] + 0x228, 0);
            if (which == 1) f.memory.number(0x40000, 0x40000);
            if (which == 2) f.memory.number(0x40000 + 8, 0x40000);
            if (which == 3) f.memory.number(f.roots[0] + 0x230, 2);
            if (which == 4) f.memory.number(0x40000 + 32, 0);
            if (which == 5) f.memory.bytes[0x50000 + 8 - FakeMemory::base] ^= 1;
            if (which == 6) f.memory.number(0x30000 + 8, 0x41000);
            rejected(f); require(f.memory.reads < 1000, "corrupt list caused an unbounded traversal");
        }
    });
    test("partial reads never supply implicit zeroes or partial labels", [] {
        for (const auto at : {Address{0x10000}, Address{0x10020}, Address{0x40000}, Address{0x50040}, Address{0x500f0}}) {
            Fixture f; f.memory.short_at = at; rejected(f);
        }
    });
    test("PF names require 1 through 31 bounded bytes and a terminator", [] {
        Fixture f; const std::string label(31, 'x'); f.memory.put(0x50040, label.c_str(), 32);
        require(f.inspect().supported, "31 byte PF name was refused");
        Fixture full; const std::string unterminated(32, 'x'); full.memory.put(0x50040, unterminated.data(), 32); rejected(full);
        Fixture empty; empty.memory.bytes[0x50040 - FakeMemory::base] = 0; rejected(empty);
    });
    test("UTF16 cache enforces capacity SSO bounds heap pointers terminators and surrogates", [] {
        for (unsigned which = 0; which < 7; ++which) {
            Fixture f;
            if (which == 0) f.memory.number(0x500f0 + 16, 8);
            if (which == 1) f.memory.number(0x500f0 + 24, 4097);
            if (which == 2) { f.memory.number(0x500f0 + 24, 8); f.memory.number(0x500f0, 0); }
            if (which == 3) { const char16_t high = 0xd800; f.memory.put(0x500f0, &high, 2); }
            if (which == 4) { const char16_t low = 0xdc00; f.memory.put(0x500f0, &low, 2); }
            if (which == 5) { const char16_t nonzero = u'x'; f.memory.put(0x500f0 + 12, &nonzero, 2); }
            if (which == 6) { f.memory.number(0x500f0 + 16, UINT64_MAX); f.memory.number(0x500f0 + 24, UINT64_MAX); }
            rejected(f);
        }
        Fixture heap; heap.memory.number(0x500f0 + 24, 16); heap.memory.number(0x500f0, 0x60000);
        const char16_t value[] = u"Slider"; heap.memory.put(0x60000, value, sizeof(value));
        require(heap.inspect().supported, "valid bounded heap string failed");
    });
    test("byte read call elapsed region and candidate caps return no partial success", [] {
        for (unsigned which = 0; which < 6; ++which) {
            Fixture f;
            if (which == 0) f.limits.max_bytes = 8;
            if (which == 1) f.limits.max_reads = 2;
            if (which == 2) { f.limits.max_ms = 1; f.memory.tick = 2; }
            if (which == 3) f.limits.max_regions = 0;
            if (which == 4) f.limits.max_candidates = 3;
            if (which == 5) f.limits.max_nodes = 0;
            rejected(f);
        }
    });
    test("eligible region guards refuse nonwritable shared uncommitted or overflowing spans", [] {
        for (unsigned which = 0; which < 5; ++which) {
            Fixture f;
            if (which == 0) f.memory.pages[0].writable = false;
            if (which == 1) f.memory.pages[0].private_memory = false;
            if (which == 2) f.memory.pages[0].committed = false;
            if (which == 3) f.memory.pages[0].base = UINT64_MAX - 7;
            if (which == 4) f.memory.pages[0].size = 0;
            rejected(f);
        }
    });
    test("unaligned vtable byte patterns are not root candidates", [] {
        Fixture f; f.memory.number(f.roots[0], 0);
        f.memory.number(f.roots[0] + 1, f.identity.flt_base + root_vtable_rva); rejected(f);
    });
    test("multiple four-root parades remain ambiguous and unsupported", [] {
        Fixture f;
        // A fifth vtable hit is never silently chosen as an owned fixture.
        f.memory.number(0x17000, f.identity.flt_base + root_vtable_rva);
        f.memory.number(0x17020, 0x22000); rejected(f);
    });
    test("all linked children are observed without guessing a public parameter index", [] {
        Fixture f;
        f.memory.number(f.roots[0] + 0x230, 2);
        f.memory.number(0x30000 + 8, 0x47000);
        f.memory.number(0x40000, 0x47000);
        f.memory.number(0x47000, 0x30000); f.memory.number(0x47000 + 8, 0x40000);
        std::array<unsigned char,16> guid{}; guid.fill(0x33);
        f.memory.put(0x47000 + 16, guid.data(), guid.size()); f.memory.number(0x47000 + 32, 0x57000);
        f.memory.put(0x57000 + 8, guid.data(), guid.size()); f.memory.number(0x57000 + 0x20, f.roots[0]);
        const char label[] = "Another child"; f.memory.put(0x57040, label, sizeof(label));
        // Empty UTF16 display is represented as empty, not invented from the PF bytes.
        const auto report = f.inspect();
        require(report.supported && report.rows.size() == 5 && report.rows[1].guid[0] == 0x33 &&
            report.rows[1].pf_name == "Another child" && report.rows[1].display.empty(),
            "list order was incorrectly treated as public parameter identity or fallback display");
    });
    test("UTF16 valid surrogate pairs and raw PF high bytes remain lossless observations", [] {
        Fixture f; const char16_t display[] = {0xd83c, 0xdfac, 0};
        f.memory.put(0x500f0, display, sizeof(display)); f.memory.number(0x50100, 2);
        const std::array<unsigned char,3> pf{0x81, 0xff, 0}; f.memory.put(0x50040, pf.data(), pf.size());
        const auto report = f.inspect(); require(report.supported, "paired surrogate or unknown PF bytes were lost");
        const auto text = report_json(report);
        require(text.find("\\uD83C") == std::string::npos && text.find("\\ud83c\\udfac") != std::string::npos &&
            text.find("\\u0081\\u00ff") != std::string::npos,
            "UTF16 or raw byte JSON escaping changed unverified encoding");
    });
    test("nonprogressing regions backwards time and raised hard budgets fail closed", [] {
        Fixture repeating; repeating.memory.repeating_region = true; rejected(repeating);
        Fixture clock; clock.memory.clock = 100; clock.memory.backwards_clock = true; rejected(clock);
        Fixture raised; raised.limits.max_bytes = UINT64_MAX; rejected(raised);
        Fixture wrapping; wrapping.identity.flt_base = 0xfffffffffffffff8ULL; rejected(wrapping);
    });
    test("candidate JSON has explicit non-enabling unverified ownership and no generic match claims", [] {
        Fixture f; const auto report = f.inspect(); const auto text = report_json(report);
        require(text.find("\"runtimeEnabled\":false") != std::string::npos &&
            text.find("\"hostVerified\":false") != std::string::npos &&
            text.find("\"publicIdentityVerified\":false") != std::string::npos,
            "JSON omitted mandatory non-enabling flags");
        require(text.find("0x0000000000010000") != std::string::npos && text.find("22222222222222222222222222222222") != std::string::npos,
            "JSON omitted QA-only pointer or raw GUID evidence");
    });
    std::cout << checks - failures << '/' << checks << " QA parameter pure-core groups passed (no AE/process access)\n";
    return failures ? 1 : 0;
}
