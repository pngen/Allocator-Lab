// Allocator Lab 1.0.0
// Trace serialization, validation and replay validation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/trace.hpp"
#include "allocator_lab/config.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace allocator_lab {
namespace {

// --- Minimal, strict JSON value model ---
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::uint64_t uval = 0;
    bool is_uint = false;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
    const Json* get(const std::string& k) const {
        for (const auto& p : obj) if (p.first == k) return &p.second;
        return nullptr;
    }
    bool is_number() const { return type == Number; }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    bool parse(Json& out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        return pos_ == s_.size();
    }

private:
    void skip_ws() { while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
    bool parse_value(Json& v) {
        skip_ws();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parse_object(v);
        if (c == '[') return parse_array(v);
        if (c == '"') { v.type = Json::String; return parse_string(v.str); }
        if (c == 't') return parse_lit("true", v);
        if (c == 'f') return parse_lit("false", v);
        if (c == 'n') return parse_lit("null", v);
        return parse_number(v);
    }
    bool parse_lit(const char* lit, Json& v) {
        std::size_t n = std::strlen(lit);
        if (s_.compare(pos_, n, lit) != 0) return false;
        pos_ += n;
        v.type = Json::Bool; v.b = (lit[0] == 't');
        if (lit[0] == 'n') v.type = Json::Null;
        return true;
    }
    bool parse_string(std::string& out) {
        if (s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == '"') { ++pos_; return true; }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_];
                switch (e) { case 'n': out.push_back('\n'); break; case 't': out.push_back('\t'); break; case 'r': out.push_back('\r'); break; case 'b': out.push_back('\b'); break; case 'f': out.push_back('\f'); break; case '"': out.push_back('"'); break; case '\\': out.push_back('\\'); break; case '/': out.push_back('/'); break; default: return false; }
                ++pos_;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            } else {
                out.push_back(c); ++pos_;
            }
        }
        return false;
    }
    bool parse_number(Json& v) {
        std::size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        bool isint = true;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') { isint = false; ++pos_; while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) { isint = false; ++pos_; if (pos_ < s_.size() && (s_[pos_]=='+'||s_[pos_]=='-')) ++pos_; while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
        std::string tok = s_.substr(start, pos_ - start);
        if (isint && tok[0] != '-') {
            try {
                std::size_t consumed = 0;
                std::uint64_t u = std::stoull(tok, &consumed);
                if (consumed == tok.size()) { v.type = Json::Number; v.is_uint = true; v.uval = u; return true; }
            } catch (...) {}
        }
        try { v.type = Json::Number; v.is_uint = false; v.num = std::stod(tok); return true; } catch (...) { return false; }
    }
    bool parse_array(Json& v) {
        ++pos_; skip_ws(); v.type = Json::Array;
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        for (;;) {
            Json item;
            if (!parse_value(item)) return false;
            v.arr.push_back(std::move(item));
            skip_ws();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }
    bool parse_object(Json& v) {
        ++pos_; skip_ws(); v.type = Json::Object;
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        for (;;) {
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != '"') return false;
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            Json val;
            if (!parse_value(val)) return false;
            v.obj.emplace_back(std::move(key), std::move(val));
            skip_ws();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }

    const std::string& s_;
    std::size_t pos_ = 0;
};

std::string json_escape(const std::string& in) {
    std::string out; out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
                else out.push_back(c);
        }
    }
    return out;
}

} // namespace

std::string trace_to_json(const Trace& trace) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": " << trace.version << ",\n";
    os << "  \"seed\": " << trace.seed << ",\n";
    os << "  \"name\": \"" << json_escape(trace.name) << "\",\n";
    os << "  \"entries\": [\n";
    for (std::size_t i = 0; i < trace.entries.size(); ++i) {
        const TraceEntry& e = trace.entries[i];
        os << "    { \"seq\": " << e.seq << ", \"step\": " << e.step
           << ", \"op\": " << static_cast<int>(e.op)
           << ", \"id\": " << e.id << ", \"size\": " << e.size
           << ", \"alignment\": " << e.alignment
           << ", \"domain\": " << static_cast<int>(e.domain)
           << ", \"worker\": " << e.worker << ", \"tag\": " << e.tag
           << ", \"has_status\": " << (e.has_status ? "true" : "false");
        if (e.has_status) os << ", \"status_success\": " << (e.status_success ? "true" : "false") << ", \"error_code\": " << e.error_code;
        os << " }";
        if (i + 1 < trace.entries.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n}\n";
    return os.str();
}

Error trace_from_json(const std::string& text, Trace& out) {
    Json root;
    JsonParser parser(text);
    if (!parser.parse(root) || root.type != Json::Object) return make_error(ErrorCode::trace_malformed, "trace JSON is not a valid object");
    const Json* jv = root.get("version");
    if (!jv || !jv->is_number() || !jv->is_uint) return make_error(ErrorCode::trace_malformed, "missing/invalid version");
    if (jv->uval != kTraceVersion) return make_error(ErrorCode::trace_rejected, "unsupported trace version");
    std::uint64_t seed = 0;
    if (const Json* js = root.get("seed")) { if (!js->is_number() || !js->is_uint) return make_error(ErrorCode::trace_malformed, "invalid seed"); seed = js->uval; }
    std::string name;
    if (const Json* jn = root.get("name")) { if (jn->type != Json::String) return make_error(ErrorCode::trace_malformed, "invalid name"); name = jn->str; }
    const Json* je = root.get("entries");
    if (!je || je->type != Json::Array) return make_error(ErrorCode::trace_malformed, "missing entries array");
    out = Trace{}; out.version = static_cast<std::uint32_t>(jv->uval); out.seed = seed; out.name = name;
    for (const Json& item : je->arr) {
        if (item.type != Json::Object) return make_error(ErrorCode::trace_malformed, "entry not an object");
        TraceEntry e;
        auto need_uint = [&](const char* k, std::uint64_t& o) -> bool { const Json* p = item.get(k); if (!p || !p->is_uint) return false; o = p->uval; return true; };
        auto need_int = [&](const char* k, int& o) -> bool { std::uint64_t u; if (!need_uint(k, u)) return false; o = static_cast<int>(u); return true; };
        int op = -1, domain = -1, worker_i = 0, tag_i = 0;
        if (!need_uint("seq", e.seq)) return make_error(ErrorCode::trace_malformed, "entry missing seq");
        if (!need_uint("step", e.step)) return make_error(ErrorCode::trace_malformed, "entry missing step");
        if (!need_int("op", op) || op < 0 || op > 6) return make_error(ErrorCode::trace_malformed, "malformed op value");
        if (!need_uint("id", e.id)) return make_error(ErrorCode::trace_malformed, "entry missing id");
        if (!need_uint("size", e.size)) return make_error(ErrorCode::trace_malformed, "entry missing size");
        if (!need_uint("alignment", e.alignment)) return make_error(ErrorCode::trace_malformed, "entry missing alignment");
        if (!need_int("domain", domain) || domain < 0 || domain > 5) return make_error(ErrorCode::trace_malformed, "malformed domain value");
        if (!need_int("worker", worker_i)) return make_error(ErrorCode::trace_malformed, "entry missing worker");
        if (!need_int("tag", tag_i)) return make_error(ErrorCode::trace_malformed, "entry missing tag");
        e.op = static_cast<TraceOperationType>(op);
        e.domain = static_cast<MemoryDomain>(domain);
        e.worker = static_cast<WorkerId>(worker_i); e.tag = static_cast<std::uint32_t>(tag_i);
        if (const Json* ph = item.get("has_status")) { if (ph->type != Json::Bool) return make_error(ErrorCode::trace_malformed, "invalid has_status"); e.has_status = ph->b; }
        if (e.has_status) {
            if (const Json* ps = item.get("status_success")) { if (ps->type != Json::Bool) return make_error(ErrorCode::trace_malformed, "invalid status_success"); e.status_success = ps->b; }
            if (const Json* pe = item.get("error_code")) { if (!pe->is_uint || pe->uval > 65535) return make_error(ErrorCode::trace_malformed, "invalid error_code"); e.error_code = static_cast<std::uint16_t>(pe->uval); }
        }
        out.entries.push_back(e);
    }
    return validate_trace(out);
}

std::vector<std::uint8_t> trace_to_binary(const Trace& trace) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(trace.entries.size() * 64 + 64);
    auto put_u32 = [&](std::uint32_t v) { bytes.push_back(static_cast<std::uint8_t>(v & 0xFF)); bytes.push_back(static_cast<std::uint8_t>((v>>8)&0xFF)); bytes.push_back(static_cast<std::uint8_t>((v>>16)&0xFF)); bytes.push_back(static_cast<std::uint8_t>((v>>24)&0xFF)); };
    auto put_u64 = [&](std::uint64_t v) { for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>((v >> (8*i)) & 0xFF)); };
    put_u32(kTraceMagic);
    put_u32(trace.version);
    put_u64(trace.seed);
    std::uint32_t name_len = static_cast<std::uint32_t>(trace.name.size());
    put_u32(name_len);
    bytes.insert(bytes.end(), trace.name.begin(), trace.name.end());
    put_u32(static_cast<std::uint32_t>(trace.entries.size()));
    for (const auto& e : trace.entries) {
        put_u64(e.seq); put_u64(e.step); put_u32(static_cast<std::uint32_t>(e.op)); put_u64(e.id);
        put_u64(e.size); put_u64(e.alignment); put_u32(static_cast<std::uint32_t>(e.domain));
        put_u32(e.worker); put_u32(e.tag); put_u32(static_cast<std::uint32_t>(e.has_status ? 1 : 0));
        put_u32(static_cast<std::uint32_t>(e.status_success ? 1 : 0)); put_u32(e.error_code);
    }
    return bytes;
}

Error trace_from_binary(const std::vector<std::uint8_t>& bytes, Trace& out) {
    std::size_t p = 0;
    auto rd_u32 = [&](std::uint32_t& v) -> bool { if (p + 4 > bytes.size()) return false; v = static_cast<std::uint32_t>(bytes[p]) | (static_cast<std::uint32_t>(bytes[p+1])<<8) | (static_cast<std::uint32_t>(bytes[p+2])<<16) | (static_cast<std::uint32_t>(bytes[p+3])<<24); p += 4; return true; };
    auto rd_u64 = [&](std::uint64_t& v) -> bool { if (p + 8 > bytes.size()) return false; v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(bytes[p+i]) << (8*i); p += 8; return true; };
    std::uint32_t magic = 0;
    if (!rd_u32(magic) || magic != kTraceMagic) return make_error(ErrorCode::trace_malformed, "bad binary magic");
    std::uint32_t version = 0;
    if (!rd_u32(version)) return make_error(ErrorCode::trace_malformed, "truncated version");
    if (version != kTraceVersion) return make_error(ErrorCode::trace_rejected, "unsupported trace version");
    std::uint64_t seed = 0;
    if (!rd_u64(seed)) return make_error(ErrorCode::trace_malformed, "truncated seed");
    std::uint32_t name_len = 0;
    if (!rd_u32(name_len) || p + name_len > bytes.size()) return make_error(ErrorCode::trace_malformed, "truncated name");
    std::string name(reinterpret_cast<const char*>(&bytes[p]), name_len); p += name_len;
    if (name_len > 1u << 24) return make_error(ErrorCode::trace_malformed, "oversized name");
    std::uint32_t count = 0;
    if (!rd_u32(count)) return make_error(ErrorCode::trace_malformed, "truncated count");
    out = Trace{}; out.version = version; out.seed = seed; out.name = name; out.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TraceEntry e;
        if (!rd_u64(e.seq)) return make_error(ErrorCode::trace_malformed, "truncated entry");
        std::uint32_t op; std::uint32_t domain;
        if (!rd_u64(e.step) || !rd_u32(op) || !rd_u64(e.id)) return make_error(ErrorCode::trace_malformed, "truncated entry");
        if (!rd_u64(e.size) || !rd_u64(e.alignment) || !rd_u32(domain)) return make_error(ErrorCode::trace_malformed, "truncated entry");
        e.op = static_cast<TraceOperationType>(op);
        e.domain = static_cast<MemoryDomain>(domain);
        std::uint32_t w; std::uint32_t tg; std::uint32_t hs; std::uint32_t ss; std::uint32_t ec;
        if (!rd_u32(w) || !rd_u32(tg) || !rd_u32(hs) || !rd_u32(ss) || !rd_u32(ec)) return make_error(ErrorCode::trace_malformed, "truncated entry");
        e.worker = static_cast<WorkerId>(w); e.tag = tg; e.has_status = hs != 0; e.status_success = ss != 0; e.error_code = static_cast<std::uint16_t>((ec > 65535) ? 65535 : ec);
        if (e.op > TraceOperationType::PhaseMarker) return make_error(ErrorCode::trace_malformed, "malformed op");
        if (e.domain > MemoryDomain::Mapped) return make_error(ErrorCode::trace_malformed, "malformed domain");
        out.entries.push_back(e);
    }
    if (p != bytes.size()) return make_error(ErrorCode::trace_malformed, "trailing bytes in binary trace");
    return validate_trace(out);
}

void TraceReplayValidator::reset(std::uint64_t seed) { (void)seed; live_ids_.clear(); live_ = 0; processed_ = 0; }

bool TraceReplayValidator::is_live(AllocationId id) const noexcept {
    for (AllocationId x : live_ids_) if (x == id) return true;
    return false;
}

Error TraceReplayValidator::validate(const TraceEntry& e) {
    ++processed_;
    switch (e.op) {
        case TraceOperationType::Allocate:
            if (is_live(e.id)) return make_error(ErrorCode::trace_rejected, "duplicate live allocation id");
            live_ids_.push_back(e.id); ++live_;
            break;
        case TraceOperationType::Free:
            if (!is_live(e.id)) return make_error(ErrorCode::trace_rejected, "free of non-live/nonexistent id");
            { for (std::size_t i = 0; i < live_ids_.size(); ++i) if (live_ids_[i] == e.id) { live_ids_[i] = live_ids_.back(); live_ids_.pop_back(); break; } }
            --live_;
            break;
        case TraceOperationType::Reallocate:
            if (!is_live(e.id)) return make_error(ErrorCode::trace_rejected, "reallocate of non-live id");
            break;
        case TraceOperationType::Trim:
        case TraceOperationType::Reset:
        case TraceOperationType::Barrier:
        case TraceOperationType::PhaseMarker:
            break;
    }
    return Error{};
}

Error validate_trace(const Trace& trace) {
    if (trace.version != kTraceVersion) return make_error(ErrorCode::trace_rejected, "unsupported trace version");
    if (trace.entry_count() == 0) return Error{};
    TraceReplayValidator v;
    v.reset(trace.seed);
    std::uint64_t max_live_bytes = 0, cur_live_bytes = 0;
    std::vector<std::pair<AllocationId, std::uint64_t>> live_sizes;
    for (const TraceEntry& e : trace.entries) {
        if (e.id == 0) return make_error(ErrorCode::trace_malformed, "allocation id 0 is reserved");
        if (e.op > TraceOperationType::PhaseMarker) return make_error(ErrorCode::trace_malformed, "malformed op value");
        if (e.domain > MemoryDomain::Mapped) return make_error(ErrorCode::trace_malformed, "malformed domain value");
        if (e.op == TraceOperationType::Allocate || e.op == TraceOperationType::Reallocate) {
            if (e.size > (std::numeric_limits<std::size_t>::max)()) return make_error(ErrorCode::trace_malformed, "size overflow");
            if (e.alignment != 0 && !is_power_of_two(e.alignment)) return make_error(ErrorCode::trace_malformed, "alignment not power of two");
        }
        Error err = v.validate(e);
        if (err) return err;
        if (e.op == TraceOperationType::Allocate) { auto it = std::find_if(live_sizes.begin(), live_sizes.end(), [&](auto& p){ return p.first == e.id; }); if (it != live_sizes.end()) return make_error(ErrorCode::trace_malformed, "size record conflict"); live_sizes.emplace_back(e.id, e.size); cur_live_bytes += e.size; }
        else if (e.op == TraceOperationType::Free) { auto it = std::find_if(live_sizes.begin(), live_sizes.end(), [&](auto& p){ return p.first == e.id; }); if (it != live_sizes.end()) { cur_live_bytes -= it->second; live_sizes.erase(it); } }
        else if (e.op == TraceOperationType::Reallocate) { auto it = std::find_if(live_sizes.begin(), live_sizes.end(), [&](auto& p){ return p.first == e.id; }); if (it != live_sizes.end()) { cur_live_bytes -= it->second; it->second = e.size; cur_live_bytes += e.size; } }
        if (cur_live_bytes > max_live_bytes) max_live_bytes = cur_live_bytes;
    }
    return Error{};
}

Error save_trace_json(const Trace& trace, const std::filesystem::path& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return make_error(ErrorCode::operation_failed, "cannot open trace file");
    std::string s = trace_to_json(trace);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    return f ? Error{} : make_error(ErrorCode::operation_failed, "cannot write trace file");
}

Error load_trace_json(const std::filesystem::path& path, Trace& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return make_error(ErrorCode::operation_failed, "cannot open trace file");
    std::ostringstream os; os << f.rdbuf();
    std::string s = os.str();
    if (s.size() > (64ull << 20)) return make_error(ErrorCode::trace_malformed, "oversized trace");
    return trace_from_json(s, out);
}

Error save_trace_binary(const Trace& trace, const std::filesystem::path& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return make_error(ErrorCode::operation_failed, "cannot open trace file");
    std::vector<std::uint8_t> b = trace_to_binary(trace);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return f ? Error{} : make_error(ErrorCode::operation_failed, "cannot write trace file");
}

Error load_trace_binary(const std::filesystem::path& path, Trace& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return make_error(ErrorCode::operation_failed, "cannot open trace file");
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() > (64ull << 20)) return make_error(ErrorCode::trace_malformed, "oversized trace");
    return trace_from_binary(b, out);
}

} // namespace allocator_lab
