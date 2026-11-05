// Experimental PURE-CORE QA only; no exports, host adapter, native calls, or writes.
// Included only by the offline test executable, not the AEX. A future adapter
// MUST independently bind process/module hashes, script thread, owned project
// and public JSX identity; supplied hashes here are NOT independent evidence.
// No live scanner is provided. Memory enumeration/reads/time are injected.
// Budgets are cooperative; an arbitrary blocking Memory implementation cannot
// be forcibly interrupted. Never retain these pointers for a later write.
//
// Named circular-list/PF/cache observations adapted from parameter_names.py,
// After-Effects-Plugin-Localizer commit dc5a591c78d67ba7b351c99e9f8222af6f160e4c.
// MIT License
// Copyright (c) 2026 verse
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include <algorithm>
#include <array>
#include <bit>
#include <utility>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xfx_qa_inspect {
using Address = std::uint64_t;
static_assert(sizeof(void*) == 8 && sizeof(char16_t) == 2 && std::endian::native == std::endian::little);
constexpr Address root_vtable_rva = 809488;
struct Identity { std::string version; std::array<std::string, 3> hashes; Address flt_base = 0; };
Identity exact_identity() {
    return {"25.4.0.86", {
        "42a2a0135d1e6c4225ca0e943aa48badbcd10c86507730b9ad40ebf71f03def7",
        "b285572116480989486ba327c341d1d5c0e3c9c78465ea56c36c141de96a1aea",
        "67021786017d1dc0b7014b5116416fc0fb95a4666a7ce90c40b23db861240c07"}, 0};
}
struct Region { Address base = 0, size = 0; bool committed = false, private_memory = false, writable = false; };
struct Memory {
    virtual ~Memory() = default;
    virtual std::size_t read(Address address, void* destination, std::size_t size) = 0;
    virtual bool next_region(Address cursor, Region& result) = 0;
    virtual std::uint64_t now_ms() = 0;
};
struct Limits {
    std::uint64_t max_bytes = 8 * 1024 * 1024, max_ms = 100;
    std::size_t max_reads = 16384, max_regions = 4096, max_candidates = 32, max_nodes = 64;
};
struct Row {
    Address root = 0, parent = 0, stream = 0;
    std::array<unsigned char, 16> root_guid{}, guid{};
    std::array<unsigned char, 32> pf_raw{};
    std::string pf_name;
    std::u16string display;
};
struct Report {
    bool supported = false, runtime_enabled = false, host_verified = false, public_identity_verified = false;
    std::string reason;
    std::vector<Row> rows;
};
namespace {
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
constexpr Address max_address = 0x00007fffffffffffULL;
Address end_of(Address base, std::uint64_t size) {
    if (!base || base > max_address || !size || size > max_address - base) fail("Invalid or overflowing memory span");
    return base + size;
}
Address add(Address base, std::uint64_t offset) {
    if (!base || base > max_address || offset > max_address - base) fail("Invalid pointer arithmetic");
    return base + offset;
}
void aligned(Address value) {
    if (!value || value > max_address || value % 8) fail("Null unaligned or noncanonical pointer");
}
bool nonzero(const std::array<unsigned char,16>& value) {
    for (const auto byte : value) if (byte) return true;
    return false;
}
struct Guard { Address address; std::vector<unsigned char> bytes; };
class Reader {
public:
    Reader(Memory& memory, const Limits& limits) : memory_(memory), limits_(limits), start_(memory.now_ms()), last_(start_) {
        const Limits ceiling;
        if (!limits.max_bytes || limits.max_bytes > ceiling.max_bytes || !limits.max_reads || limits.max_reads > ceiling.max_reads ||
            !limits.max_ms || limits.max_ms > ceiling.max_ms || !limits.max_regions || limits.max_regions > ceiling.max_regions ||
            !limits.max_candidates || limits.max_candidates > ceiling.max_candidates || !limits.max_nodes || limits.max_nodes > ceiling.max_nodes)
            fail("Invalid or raised hard QA budget");
    }
    void check_time() {
        const auto now = memory_.now_ms();
        if (now < last_ || now - start_ > limits_.max_ms) fail("Elapsed QA time cap exceeded or clock moved backwards");
        last_ = now;
    }
    std::vector<unsigned char> read(Address address, std::size_t size, bool guard = true) {
        (void)end_of(address, size); check_time();
        if (reads_ >= limits_.max_reads || size > limits_.max_bytes - bytes_) fail("QA read call or byte cap exceeded");
        ++reads_; bytes_ += size;
        std::vector<unsigned char> bytes(size);
        if (memory_.read(address, bytes.data(), size) != size) fail("Incomplete memory read");
        check_time();
        if (guard) guards_.push_back({address, bytes});
        return bytes;
    }
    std::uint64_t u64(Address address) {
        aligned(address); const auto bytes = read(address, 8); std::uint64_t value = 0;
        std::memcpy(&value, bytes.data(), 8); return value;
    }
    std::array<unsigned char,16> guid(Address address) {
        const auto bytes = read(address, 16); std::array<unsigned char,16> value{};
        std::memcpy(value.data(), bytes.data(), value.size());
        if (!nonzero(value)) fail("Empty raw GUID");
        return value;
    }
    std::vector<Address> roots(Address vtable) {
        std::vector<Address> hits; Address cursor = 0;
        std::size_t regions = 0;
        Region region;
        for (;;) {
            check_time();
            const bool found = memory_.next_region(cursor, region);
            check_time();
            if (!found) break;
            if (++regions > limits_.max_regions) fail("QA region enumeration cap exceeded");
            if (region.base < cursor) fail("Non-progressing region enumeration");
            const auto end = end_of(region.base, region.size);
            cursor = end;
            if (!region.committed || !region.private_memory || !region.writable) continue;
            Address at = add(region.base, (8 - region.base % 8) % 8);
            while (at < end && end - at >= 8) {
                const auto size = static_cast<std::size_t>((std::min)(Address{4096}, (end - at) / 8 * 8));
                const auto bytes = read(at, size, false);
                for (std::size_t offset = 0; offset < bytes.size(); offset += 8) {
                    std::uint64_t value = 0; std::memcpy(&value, bytes.data() + offset, 8);
                    if (value != vtable) continue;
                    if (hits.size() >= limits_.max_candidates) fail("QA root candidate cap exceeded");
                    hits.push_back(add(at, offset));
                }
                at = add(at, size);
            }
        }
        return hits;
    }
    void node_used() { if (++nodes_ > limits_.max_nodes) fail("QA linked-node cap exceeded"); }
    std::u16string display(Address stream) {
        const auto address = add(stream, 0xf0);
        const auto cache = read(address, 32);
        std::uint64_t length = 0, capacity = 0, heap = 0;
        std::memcpy(&length, cache.data() + 16, 8); std::memcpy(&capacity, cache.data() + 24, 8);
        std::memcpy(&heap, cache.data(), 8);
        if (length > capacity || capacity > 4096 || (capacity <= 7 && length > 7)) fail("UTF16 cache length/capacity overflow");
        Address storage = address;
        if (capacity > 7) { aligned(heap); storage = heap; }
        const auto bytes = read(storage, static_cast<std::size_t>((length + 1) * 2));
        if (bytes[bytes.size() - 2] || bytes.back()) fail("UTF16 cache lacks trailing terminator");
        std::u16string value; bool high = false;
        for (std::size_t i = 0; i < length; ++i) {
            const auto unit = static_cast<char16_t>(bytes[i * 2] | static_cast<unsigned>(bytes[i * 2 + 1]) << 8);
            if (!unit) fail("Embedded UTF16 NUL");
            if (high) { if (unit < 0xdc00 || unit > 0xdfff) fail("Invalid UTF16 low surrogate"); high = false; }
            else if (unit >= 0xd800 && unit <= 0xdbff) high = true;
            else if (unit >= 0xdc00 && unit <= 0xdfff) fail("Unpaired UTF16 low surrogate");
            value.push_back(unit);
        }
        if (high) fail("Unpaired UTF16 high surrogate");
        return value;
    }
    void verify() {
        // Only local snapshots are compared. No host pointer is dereferenced.
        for (const auto& guard : guards_) if (read(guard.address, guard.bytes.size(), false) != guard.bytes)
            fail("Structure changed during observation");
        check_time();
    }
private:
    Memory& memory_; const Limits& limits_;
    std::uint64_t start_, last_, bytes_ = 0;
    std::size_t reads_ = 0, nodes_ = 0;
    std::vector<Guard> guards_;
};
std::uint64_t local_u64(const std::vector<unsigned char>& bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 8) fail("Invalid local record span");
    std::uint64_t result = 0; std::memcpy(&result, bytes.data() + at, 8); return result;
}
std::vector<Row> observe(Reader& reader, const Identity& identity) {
    const auto vtable = add(identity.flt_base, root_vtable_rva);
    const auto hits = reader.roots(vtable);
    if (hits.size() != 4) fail("Not exactly four structural root candidates; no named/public identity claimed");
    const auto parade = reader.u64(add(hits.front(), 0x20)); aligned(parade);
    const auto begin = reader.u64(add(parade, 0x220)), end = reader.u64(add(parade, 0x228));
    aligned(begin); aligned(end);
    if (end < begin || end - begin != 32) fail("Parade vector must contain exactly four roots");
    const auto members = reader.read(begin, 32);
    std::array<Address,4> ordered{}; std::set<Address> unique;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        ordered[i] = local_u64(members, i * 8); aligned(ordered[i]);
        if (!unique.insert(ordered[i]).second) fail("Duplicate root membership");
    }
    if (unique != std::set<Address>(hits.begin(), hits.end())) fail("Scan candidates disagree with parade membership");
    const auto effect_guid = reader.guid(add(ordered.front(), 8));
    std::vector<Row> rows; std::set<Address> all_streams, all_nodes;
    for (const auto root : ordered) {
        if (reader.u64(root) != vtable || reader.u64(add(root, 0x20)) != parade || reader.guid(add(root, 8)) != effect_guid)
            fail("Root vtable parent or raw GUID differs across parade");
        const auto head = reader.u64(add(root, 0x228)), count = reader.u64(add(root, 0x230));
        aligned(head);
        if (!count || count > 16) fail("Named-list count unsupported or nonintegral bit pattern");
        const auto sentinel = reader.read(head, 16);
        auto node = local_u64(sentinel, 0); const auto tail = local_u64(sentinel, 8);
        aligned(node); aligned(tail);
        Address previous = head; std::set<Address> seen;
        for (std::uint64_t i = 0; i < count; ++i) {
            reader.node_used(); aligned(node);
            if (node == head || !seen.insert(node).second || !all_nodes.insert(node).second) fail("Linked-node cycle alias or early sentinel");
            const auto record = reader.read(node, 40);
            const auto next = local_u64(record, 0), prev = local_u64(record, 8), child = local_u64(record, 32);
            aligned(next); aligned(prev); aligned(child);
            if (prev != previous || !all_streams.insert(child).second) fail("Linked-node backlink or stream alias");
            std::array<unsigned char,16> guid{}; std::memcpy(guid.data(), record.data() + 16, guid.size());
            if (!nonzero(guid) || reader.guid(add(child, 8)) != guid || reader.u64(add(child, 0x20)) != root)
                fail("Child raw GUID or parent guard mismatch");
            Row row; row.root = root; row.parent = parade; row.stream = child; row.root_guid = effect_guid; row.guid = guid;
            const auto pf = reader.read(add(child, 0x40), 32); std::size_t length = 0;
            while (length < pf.size() && pf[length]) ++length;
            if (!length || length == pf.size()) fail("PF byte name empty or unterminated within 32 bytes");
            std::memcpy(row.pf_raw.data(), pf.data(), pf.size());
            row.pf_name.assign(reinterpret_cast<const char*>(pf.data()), length);
            row.display = reader.display(child);
            rows.push_back(std::move(row));
            previous = node; node = next;
        }
        if (node != head || previous != tail) fail("Named-list count or sentinel tail mismatch");
    }
    reader.verify();
    return rows;
}
constexpr char digits[] = "0123456789abcdef";
std::string hex_address(Address address) {
    std::string value(18, '0'); value[1] = 'x';
    for (int i = 17; i >= 2; --i) { value[static_cast<std::size_t>(i)] = digits[address & 15]; address >>= 4; }
    return value;
}
template<std::size_t N> std::string hex_bytes(const std::array<unsigned char,N>& bytes) {
    std::string text; text.reserve(N * 2);
    for (const auto byte : bytes) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}
std::string quote_bytes(std::string_view value) {
    std::string result = "\"";
    for (const auto item : value) {
        const auto byte = static_cast<unsigned char>(item);
        result += "\\u00"; result += digits[byte >> 4]; result += digits[byte & 15];
    }
    return result + '"';
}
std::string quote_utf16(std::u16string_view value) {
    std::string result = "\"";
    for (const auto unit : value) {
        result += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) result += digits[(unit >> shift) & 15];
    }
    return result + '"';
}
} // namespace
Report inspect_candidates(Memory& memory, const Identity& identity, const Limits& limits) {
    Report result;
    try {
        const auto expected = exact_identity();
        if (identity.version != expected.version || identity.hashes != expected.hashes) fail("Supplied identity differs from compiled expected build hashes");
        aligned(identity.flt_base); (void)add(identity.flt_base, root_vtable_rva);
        Reader reader(memory, limits);
        result.rows = observe(reader, identity);
        result.supported = true;
        result.reason = "Structural observations only; no module/process ownership, public match-name, or live ABI verification";
    } catch (const std::exception& error) {
        result.supported = false; result.rows.clear(); result.reason = error.what();
    } catch (...) {
        result.supported = false; result.rows.clear(); result.reason = "Unknown read-only QA observation failure";
    }
    return result;
}
std::string report_json(const Report& report) {
    std::string text = "{\"kind\":\"xfx-parameter-structural-candidates\",\"schemaVersion\":1,\"runtimeEnabled\":false,\"hostVerified\":false,"
        "\"publicIdentityVerified\":false,\"structuralCandidatesOnly\":true,\"independentModuleHashesVerified\":false,"
        "\"liveScannerAvailable\":false,\"qaOnlyPointersNotLexiconData\":true,\"supported\":";
    text += report.supported ? "true" : "false";
    text += ",\"reason\":" + quote_bytes(report.reason) + ",\"rows\":[";
    for (std::size_t i = 0; i < report.rows.size(); ++i) {
        if (i) text += ',';
        const auto& row = report.rows[i];
        text += "{\"rootPtr\":\"" + hex_address(row.root) + "\",\"paradePtr\":\"" + hex_address(row.parent) +
            "\",\"childPtr\":\"" + hex_address(row.stream) + "\",\"rootRawGuidHex\":\"" + hex_bytes(row.root_guid) +
            "\",\"childRawGuidHex\":\"" + hex_bytes(row.guid) + "\",\"pfNameRaw32Hex\":\"" + hex_bytes(row.pf_raw) +
            "\",\"pfNameBytesEscapedNotDecoded\":" + quote_bytes(row.pf_name) + ",\"displayUtf16\":" + quote_utf16(row.display) + '}';
    }
    return text + "]}";
}
} // namespace xfx_qa_inspect
