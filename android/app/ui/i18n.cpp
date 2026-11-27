#include "i18n.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "text.hpp"

namespace mx::ui {

namespace {

constexpr int kLangs = 10;  // ru + 9 переводов
struct Row {
    const char* ru;
    const char* t[kLangs - 1];  // en, uk, es, de, fr, pt, it, pl, tr; nullptr — берём английский
};

const char* const kIds[kLangs] = {"ru", "en", "uk", "es", "de", "fr", "pt", "it", "pl", "tr"};
const char* const kNames[kLangs] = {"Русский", "English", "Українська", "Español", "Deutsch", "Français", "Português", "Italiano", "Polski", "Türkçe"};

const Row kRows[] = {
#include "i18n_table.inc"
};

struct Key {
    std::string ru;
    const Row* row;
    bool wordStart, wordEnd;  // начинается/кончается буквой — тогда совпадение только на границе слова
};

struct Tables {
    std::unordered_map<std::string, const Row*> exact;
    std::vector<Key> keys;  // от длинных к коротким
};

char32_t firstCp(const std::string& s) {
    std::u32string u = decodeUtf8(s.substr(0, std::min<std::size_t>(4, s.size())));
    return u.empty() ? 0 : u[0];
}

char32_t lastCp(const std::string& s, std::size_t end) {  // символ, кончающийся перед байтом end
    if (end == 0) return 0;
    std::size_t b = end - 1;
    while (b > 0 && (static_cast<unsigned char>(s[b]) & 0xC0) == 0x80) --b;
    std::u32string u = decodeUtf8(s.substr(b, end - b));
    return u.empty() ? 0 : u[0];
}

const Tables& tables() {
    static Tables t = [] {
        Tables r;
        for (const Row& row : kRows) {
            r.exact.emplace(row.ru, &row);
            std::string k = row.ru;
            if (k.empty()) continue;
            r.keys.push_back({k, &row, isWordCp(firstCp(k)), isWordCp(lastCp(k, k.size()))});
        }
        std::stable_sort(r.keys.begin(), r.keys.end(), [](const Key& a, const Key& b) { return a.ru.size() > b.ru.size(); });
        return r;
    }();
    return t;
}

int g_lang = 0;
std::unordered_map<std::string, std::string> g_cache[kLangs];

const char* pick(const Row* r) { return r->t[g_lang - 1] ? r->t[g_lang - 1] : r->t[0]; }

bool hasCyrillic(const std::string& s) {
    for (unsigned char c : s)
        if (c == 0xD0 || c == 0xD1) return true;
    return false;
}

// Строка, склеенная из надписей и чисел/латиницы («Найдено 3 из 10»): переводим по кусочкам.
// Если хоть одна русская буква осталась непереведённой (название песни и т. п.) — оставляем как есть.
std::string compose(const std::string& s) {
    const Tables& t = tables();
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const Key* hit = nullptr;
        for (const Key& k : t.keys) {
            if (k.ru.size() > s.size() - i || s.compare(i, k.ru.size(), k.ru) != 0) continue;
            if (k.wordStart && isWordCp(lastCp(s, i))) continue;
            if (k.wordEnd && i + k.ru.size() < s.size() && isWordCp(firstCp(s.substr(i + k.ru.size(), 4)))) continue;
            hit = &k;
            break;
        }
        if (hit) {
            out += pick(hit->row);
            i += hit->ru.size();
            continue;
        }
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xD0 || c == 0xD1) return s;  // непереведённая кириллица
        std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        out += s.substr(i, n);
        i += n;
    }
    return out;
}

}  // namespace

void setLanguage(int lang) { g_lang = std::clamp(lang, 0, kLangs - 1); }

int languageFromId(const std::string& id) {
    for (int i = 0; i < kLangs; ++i)
        if (id == kIds[i]) return i;
    return 0;
}

int languageCount() { return kLangs; }
const char* languageId(int i) { return kIds[std::clamp(i, 0, kLangs - 1)]; }
const char* languageName(int i) { return kNames[std::clamp(i, 0, kLangs - 1)]; }

std::string tr(const std::string& s) {
    if (g_lang == 0 || s.empty() || !hasCyrillic(s)) return s;
    auto& cache = g_cache[g_lang];
    auto it = cache.find(s);
    if (it != cache.end()) return it->second;
    const Tables& t = tables();
    auto e = t.exact.find(s);
    std::string r = e != t.exact.end() ? std::string(pick(e->second)) : compose(s);
    if (cache.size() > 20000) cache.clear();
    cache.emplace(s, r);
    return r;
}

}  // namespace mx::ui
