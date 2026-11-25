#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mx {

static const Json kNull;

const Json& Json::at(std::size_t i) const { return (t_ == Array && i < vals_.size()) ? vals_[i] : kNull; }

const Json& Json::operator[](const std::string& key) const {
    if (t_ != Object) return kNull;
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return vals_[i];
    return kNull;
}

bool Json::has(const std::string& key) const {
    if (t_ != Object) return false;
    for (const auto& k : keys_) if (k == key) return true;
    return false;
}

Json& Json::set(const std::string& key, Json v) {
    if (t_ != Object) { t_ = Object; keys_.clear(); vals_.clear(); }
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) { vals_[i] = std::move(v); return vals_[i]; }
    keys_.push_back(key);
    vals_.push_back(std::move(v));
    return vals_.back();
}

Json& Json::add(std::string key, Json v) {
    if (t_ != Object) { t_ = Object; keys_.clear(); vals_.clear(); }
    keys_.push_back(std::move(key));
    vals_.push_back(std::move(v));
    return vals_.back();
}

Json& Json::push(Json v) {
    if (t_ != Array) { t_ = Array; keys_.clear(); vals_.clear(); }
    vals_.push_back(std::move(v));
    return vals_.back();
}

bool Json::erase(const std::string& key) {
    if (t_ != Object) return false;
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) {
            keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(i));
            vals_.erase(vals_.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    return false;
}

// ---------- запись ----------
static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += static_cast<char>(c);
        }
    }
    out += '"';
}

static void numberTo(std::string& out, double v) {
    if (!std::isfinite(v)) { out += "null"; return; }
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char b[32]; std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v)); out += b; return;
    }
    char b[40];
    for (int prec = 15; prec <= 17; ++prec) {
        std::snprintf(b, sizeof b, "%.*g", prec, v);
        if (std::strtod(b, nullptr) == v) break;
    }
    out += b;
}

void Json::dumpTo(std::string& out, int indent, int level) const {
    auto nl = [&](int lv) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(indent * lv), ' ');
    };
    switch (t_) {
        case Null: out += "null"; break;
        case Bool: out += n_ != 0 ? "true" : "false"; break;
        case Number: numberTo(out, n_); break;
        case String: escapeTo(out, s_); break;
        case Array:
            if (vals_.empty()) { out += "[]"; break; }
            out += '[';
            for (std::size_t i = 0; i < vals_.size(); ++i) {
                if (i) out += ',';
                nl(level + 1);
                vals_[i].dumpTo(out, indent, level + 1);
            }
            nl(level);
            out += ']';
            break;
        case Object:
            if (vals_.empty()) { out += "{}"; break; }
            out += '{';
            for (std::size_t i = 0; i < vals_.size(); ++i) {
                if (i) out += ',';
                nl(level + 1);
                escapeTo(out, keys_[i]);
                out += indent < 0 ? ":" : ": ";
                vals_[i].dumpTo(out, indent, level + 1);
            }
            nl(level);
            out += '}';
            break;
    }
}

std::string Json::dump(int indent) const {
    std::string s;
    dumpTo(s, indent, 0);
    return s;
}

// ---------- разбор ----------
namespace {

struct Parser {
    const char* p;
    const char* e;
    int depth = 0;

    [[noreturn]] void fail(const char* msg) const { throw std::runtime_error(msg); }
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }

    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 63));
            o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63));
        }
    }

    unsigned hex4() {
        if (e - p < 4) fail("JSON: обрезанный \\u");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("JSON: плохой \\u");
        }
        return v;
    }

    std::string str() {
        // p указывает после открывающей кавычки
        std::string o;
        while (true) {
            if (p >= e) fail("JSON: строка не закрыта");
            char c = *p++;
            if (c == '"') break;
            if (c != '\\') { o += c; continue; }
            if (p >= e) fail("JSON: обрезанный escape");
            char x = *p++;
            switch (x) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        const char* save = p;
                        p += 2;
                        unsigned lo = hex4();
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else p = save;
                    }
                    utf8(o, cp);
                    break;
                }
                default: fail("JSON: неизвестный escape");
            }
        }
        return o;
    }

    Json value() {
        ws();
        if (p >= e) fail("JSON: неожиданный конец");
        char c = *p;
        if (c == '{') {
            if (++depth > 512) fail("JSON: слишком глубоко");
            ++p;
            Json o = Json::object();
            ws();
            if (p < e && *p == '}') { ++p; --depth; return o; }
            while (true) {
                ws();
                if (p >= e || *p != '"') fail("JSON: ждали ключ");
                ++p;
                std::string k = str();
                ws();
                if (p >= e || *p != ':') fail("JSON: ждали ':'");
                ++p;
                o.add(std::move(k), value());
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; break; }
                fail("JSON: ждали ',' или '}'");
            }
            --depth;
            return o;
        }
        if (c == '[') {
            if (++depth > 512) fail("JSON: слишком глубоко");
            ++p;
            Json a = Json::array();
            ws();
            if (p < e && *p == ']') { ++p; --depth; return a; }
            while (true) {
                a.push(value());
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; break; }
                fail("JSON: ждали ',' или ']'");
            }
            --depth;
            return a;
        }
        if (c == '"') { ++p; return Json(str()); }
        if (e - p >= 4 && std::string(p, 4) == "true") { p += 4; return Json(true); }
        if (e - p >= 5 && std::string(p, 5) == "false") { p += 5; return Json(false); }
        if (e - p >= 4 && std::string(p, 4) == "null") { p += 4; return Json(); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* end = nullptr;
            double v = std::strtod(p, &end);
            if (end == p) fail("JSON: плохое число");
            p = end;
            return Json(v);
        }
        fail("JSON: неожиданный символ");
    }
};

}  // namespace

Json Json::parse(const std::string& text) {
    Parser ps{text.data(), text.data() + text.size()};
    Json v = ps.value();
    ps.ws();
    if (ps.p != ps.e) throw std::runtime_error("JSON: лишние данные после значения");
    return v;
}

bool Json::tryParse(const std::string& text, Json& out, std::string* err) {
    try {
        out = parse(text);
        return true;
    } catch (const std::exception& ex) {
        if (err) *err = ex.what();
        return false;
    }
}

void walk(const Json& j, const std::string& key, std::vector<const Json*>& out) {
    if (j.isArray()) {
        for (const auto& x : j.items()) walk(x, key, out);
    } else if (j.isObject()) {
        for (std::size_t i = 0; i < j.size(); ++i) {
            if (j.keys()[i] == key) out.push_back(&j.items()[i]);
            else walk(j.items()[i], key, out);
        }
    }
}

const Json* walkFirst(const Json& j, const std::string& key) {
    std::vector<const Json*> v;
    walk(j, key, v);
    return v.empty() ? nullptr : v.front();
}

}  // namespace mx
