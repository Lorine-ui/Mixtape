// Языки интерфейса. Надписи в коде — по-русски; перевод подставляется там, где текст рисуется
// (drawText, measure, ellipsize), поэтому отдельно оборачивать каждую строку не нужно.
// Названия песен и имена не переводятся: переводится только строка, целиком собранная из известных надписей.
#pragma once
#include <string>

namespace mx::ui {

// 0 ru, 1 en, 2 uk, 3 es, 4 de, 5 fr, 6 pt, 7 it, 8 pl, 9 tr
void setLanguage(int lang);
int languageFromId(const std::string& id);
int languageCount();
const char* languageId(int i);    // "ru", "en", …
const char* languageName(int i);  // название на самом языке: «Deutsch», «Türkçe»…

// Перевод надписи на текущий язык (или она сама, если перевода нет).
std::string tr(const std::string& s);

}  // namespace mx::ui
