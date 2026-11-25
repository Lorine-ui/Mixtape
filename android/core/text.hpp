// Работа с UTF-8 без Qt: нормализация для сравнения названий (латиница, кириллица, греческий, CJK).
#pragma once
#include <string>
#include <vector>

namespace mx {

std::u32string decodeUtf8(const std::string& s);
std::string encodeUtf8(const std::u32string& s);

char32_t lowerCp(char32_t c);
bool isWordCp(char32_t c);  // буква или цифра

std::string lower(const std::string& s);
// Нижний регистр, всё кроме букв и цифр заменено пробелом, пробелы схлопнуты. Аналог nrm() из старого кода.
std::string nrm(const std::string& s);
// Без скобок и без feat., для сравнения названий: аналог norm() из index.html.
std::string normTitle(const std::string& s);

std::string trim(const std::string& s);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);
bool startsWith(const std::string& s, const std::string& p);
bool endsWith(const std::string& s, const std::string& p);
bool contains(const std::string& s, const std::string& p);
bool icontains(const std::string& s, const std::string& p);  // ASCII-регистр
std::vector<std::string> split(const std::string& s, char sep);
// Режет по « - », « – », « — » (дефис с пробелами по краям).
std::vector<std::string> splitDash(const std::string& s);
std::string join(const std::vector<std::string>& v, const std::string& sep);
std::vector<std::string> tokens(const std::string& s);  // слова после nrm

// Процентное кодирование (RFC 3986, как encodeURIComponent).
std::string urlEncode(const std::string& s);
std::string urlDecode(const std::string& s);

// Секунды -> "m:ss" либо "m:ss.d".
std::string fmtTime(double sec, bool tenths = false);
// «1 песня», «3 песни», «25 песен»
std::string songsCount(std::size_t n);

}  // namespace mx
