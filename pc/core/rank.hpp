// Ранжирование результатов поиска и сопоставление песен при переносе плейлистов.
#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "model.hpp"

namespace mx {

// Насколько трек подходит под запрос (как relScore в старом приложении).
double relScore(const Track& t, const std::string& query);
// Совпадение найденного трека x с песней из импортируемого списка (0, если название не сходится).
double matchScore(const Track& x, const std::string& title, const std::string& artist);

struct SearchHit {
    Track track;
    double score = 0;
    int rank = 0;  // позиция внутри своего источника
};

// Порядок источников при равных оценках.
int sourceOrder(const std::string& source);

// Сортирует найденное и убирает дубли «название|исполнитель».
// `local` — свои треки, которые подходят под запрос (идут первыми и дубли не отсекают).
std::vector<Track> mergeResults(std::vector<SearchHit> remote, const std::vector<Track>& local,
                                const std::unordered_map<std::string, bool>& inLibrary);

}  // namespace mx
