// Минимальный JSON без внешних зависимостей: разбор, обход и запись.
#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace mx {

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : t_(Bool), n_(b ? 1 : 0) {}
    Json(int v) : t_(Number), n_(v) {}
    Json(long long v) : t_(Number), n_(static_cast<double>(v)) {}
    Json(double v) : t_(Number), n_(v) {}
    Json(const char* s) : t_(String), s_(s) {}
    Json(std::string s) : t_(String), s_(std::move(s)) {}

    static Json array() { Json j; j.t_ = Array; return j; }
    static Json object() { Json j; j.t_ = Object; return j; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == Null; }
    bool isBool() const { return t_ == Bool; }
    bool isNumber() const { return t_ == Number; }
    bool isString() const { return t_ == String; }
    bool isArray() const { return t_ == Array; }
    bool isObject() const { return t_ == Object; }

    bool asBool(bool d = false) const { return t_ == Bool ? n_ != 0 : d; }
    double asNum(double d = 0) const { return t_ == Number ? n_ : d; }
    // Строка либо значение по умолчанию (числа в строки не превращаются).
    std::string asStr(const std::string& d = std::string()) const { return t_ == String ? s_ : d; }
    const std::string& str() const { return s_; }

    // Для массива: элементы; для объекта: значения (ключи в keys()).
    std::size_t size() const { return (t_ == Array || t_ == Object) ? vals_.size() : 0; }
    const std::vector<Json>& items() const { return vals_; }
    std::vector<Json>& items() { return vals_; }
    const std::vector<std::string>& keys() const { return keys_; }

    const Json& at(std::size_t i) const;
    const Json& operator[](const std::string& key) const;  // нет ключа: Null
    const Json& operator[](const char* key) const { return (*this)[std::string(key)]; }
    bool has(const std::string& key) const;

    Json& set(const std::string& key, Json v);  // заменяет или добавляет
    Json& add(std::string key, Json v);         // без проверки дублей (быстро, для разбора)
    Json& push(Json v);
    bool erase(const std::string& key);

    std::string dump(int indent = -1) const;
    // Бросает std::runtime_error при ошибке.
    static Json parse(const std::string& text);
    // Не бросает: возвращает false и текст ошибки.
    static bool tryParse(const std::string& text, Json& out, std::string* err = nullptr);

private:
    void dumpTo(std::string& out, int indent, int level) const;

    Type t_ = Null;
    double n_ = 0;
    std::string s_;
    std::vector<std::string> keys_;
    std::vector<Json> vals_;
};

// Собирает значения всех ключей `key` на любой глубине (как walk() в старом parsers.js).
// В найденное значение не заходит.
void walk(const Json& j, const std::string& key, std::vector<const Json*>& out);
const Json* walkFirst(const Json& j, const std::string& key);

}  // namespace mx
