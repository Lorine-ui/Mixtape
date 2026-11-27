#include "text.hpp"

#include <algorithm>
#include <cstdio>

namespace mx {

std::u32string decodeUtf8(const std::string& s) {
    std::u32string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { o += U'�'; ++i; continue; }
        if (i + static_cast<std::size_t>(n) > s.size()) { o += U'�'; break; }
        bool ok = true;
        for (int k = 1; k < n; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((cc >> 6) != 2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { o += U'�'; ++i; continue; }
        o += cp;
        i += static_cast<std::size_t>(n);
    }
    return o;
}

std::string encodeUtf8(const std::u32string& s) {
    std::string o;
    o.reserve(s.size());
    for (char32_t cp : s) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 63));
            o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63));
        }
    }
    return o;
}

char32_t lowerCp(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;           // Latin-1
    if (c >= 0x100 && c <= 0x137) return (c % 2 == 0) ? c + 1 : c;      // Latin Extended-A
    if (c >= 0x139 && c <= 0x148) return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177) return (c % 2 == 0) ? c + 1 : c;
    if (c == 0x178) return 0xFF;
    if (c >= 0x179 && c <= 0x17E) return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;          // греческий
    if (c >= 0x410 && c <= 0x42F) return c + 32;                        // А-Я
    if (c >= 0x400 && c <= 0x40F) return c + 80;                        // Ё, Є, І, Ї и др.
    if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F))
        return (c % 2 == 0) ? c + 1 : c;                                // Ґ и расширенная кириллица
    return c;
}

bool isWordCp(char32_t c) {
    if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c == 0xAA || c == 0xB5 || c == 0xBA) return true;
    if (c < 0xC0) return false;                       // знаки Latin-1
    if (c == 0xD7 || c == 0xF7) return false;
    if (c >= 0x2000 && c <= 0x2BFF) return false;     // пунктуация, символы, стрелки
    if (c >= 0x3000 && c <= 0x303F) return false;     // пунктуация CJK
    if (c >= 0xFE00 && c <= 0xFE0F) return false;     // вариационные селекторы
    if (c >= 0xFF00 && c <= 0xFF0F) return false;
    if (c >= 0x1F000) return false;                   // эмодзи
    return true;
}

std::string lower(const std::string& s) {
    std::u32string u = decodeUtf8(s);
    for (auto& c : u) c = lowerCp(c);
    return encodeUtf8(u);
}

std::string nrm(const std::string& s) {
    std::u32string u = decodeUtf8(s), o;
    bool sp = true;
    for (char32_t c : u) {
        if (isWordCp(c)) { o += lowerCp(c); sp = false; }
        else if (!sp) { o += U' '; sp = true; }
    }
    if (!o.empty() && o.back() == U' ') o.pop_back();
    return encodeUtf8(o);
}

std::string normTitle(const std::string& s) {
    // убрать (...) и [...], затем всё после feat./ft.
    std::string t;
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '[') { ++depth; t += ' '; continue; }
        if ((c == ')' || c == ']') && depth > 0) { --depth; continue; }
        if (depth == 0) t += c;
    }
    std::string l = lower(t);
    for (const char* k : {"feat", "ft"}) {
        std::size_t pos = 0;
        while ((pos = l.find(k, pos)) != std::string::npos) {
            // в старом коде срез шёл от первого вхождения, но слово внутри слова («left») ломало название
            bool wb = pos == 0 || l[pos - 1] == ' ';
            std::size_t after = pos + std::string(k).size();
            bool ab = after >= l.size() || l[after] == '.' || l[after] == ' ';
            if (wb && ab) { l.erase(pos); break; }
            pos = after;
        }
    }
    return nrm(l);
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    std::size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
    return s;
}

bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

bool icontains(const std::string& s, const std::string& p) {
    if (p.empty()) return true;
    auto lw = [](std::string x) { for (auto& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); return x; };
    return lw(s).find(lw(p)) != std::string::npos;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> o;
    std::size_t a = 0;
    while (true) {
        std::size_t b = s.find(sep, a);
        if (b == std::string::npos) { o.push_back(s.substr(a)); break; }
        o.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return o;
}

std::vector<std::string> splitDash(const std::string& s) {
    std::vector<std::string> o;
    std::size_t start = 0, i = 0;
    auto isSp = [](char c) { return c == ' ' || c == '\t'; };
    while (i < s.size()) {
        std::size_t dashLen = 0;
        if (s[i] == '-') dashLen = 1;
        else if (s.compare(i, 3, "\xE2\x80\x93") == 0 || s.compare(i, 3, "\xE2\x80\x94") == 0) dashLen = 3;
        if (dashLen && i > start && isSp(s[i - 1]) && i + dashLen < s.size() && isSp(s[i + dashLen])) {
            o.push_back(trim(s.substr(start, i - start)));
            i += dashLen;
            while (i < s.size() && isSp(s[i])) ++i;
            start = i;
            continue;
        }
        ++i;
    }
    o.push_back(trim(s.substr(start)));
    return o;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string o;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; }
    return o;
}

std::string urlEncode(const std::string& s) {
    std::string o;
    char b[4];
    for (unsigned char c : s) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}

std::string urlDecode(const std::string& s) {
    std::string o;
    auto hv = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && hv(s[i + 1]) >= 0 && hv(s[i + 2]) >= 0) {
            o += static_cast<char>(hv(s[i + 1]) * 16 + hv(s[i + 2]));
            i += 2;
        } else o += s[i];
    }
    return o;
}

std::string fmtTime(double sec, bool tenths) {
    if (sec < 0) sec = 0;
    int m = static_cast<int>(sec / 60);
    double x = sec - m * 60;
    char b[32];
    if (tenths) std::snprintf(b, sizeof b, "%d:%04.1f", m, x);
    else std::snprintf(b, sizeof b, "%d:%02d", m, static_cast<int>(x));
    return b;
}

std::string songsCount(std::size_t n) {
    std::size_t d10 = n % 10, d100 = n % 100;
    const char* w = (d10 == 1 && d100 != 11) ? "песня" : (d10 >= 2 && d10 <= 4 && (d100 < 12 || d100 > 14)) ? "песни" : "песен";
    return std::to_string(n) + " " + w;
}

}  // namespace mx
