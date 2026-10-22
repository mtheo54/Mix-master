// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/TextMatch.cpp
// =============================================================================
#include "TextMatch.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace omnis::ai {

namespace {

bool isAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

} // namespace

std::string normalizeText(std::string_view text) {
    static const std::pair<std::string_view, char> kMap[] = {
        {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"É", 'e'}, {"È", 'e'}, {"Ê", 'e'}, {"à", 'a'},
        {"â", 'a'}, {"ä", 'a'}, {"À", 'a'}, {"Â", 'a'}, {"ç", 'c'}, {"Ç", 'c'}, {"î", 'i'}, {"ï", 'i'},
        {"Î", 'i'}, {"ô", 'o'}, {"ö", 'o'}, {"Ô", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"Û", 'u'},
        {"ÿ", 'y'}, {"œ", 'o'}, {"’", '\''}, {"‘", '\''}, {"«", ' '}, {"»", ' '}, {"−", '-'}, {"–", '-'},
    };
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        bool mapped = false;
        for (const auto& [from, to] : kMap) {
            if (text.substr(i, from.size()) == from) {
                if (from == "œ") out += "oe";
                else out += to;
                i += from.size();
                mapped = true;
                break;
            }
        }
        if (mapped) continue;
        char c = text[i++];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out += c;
    }
    // Lettres répétées 3 fois ou plus (« fooort », « trooop ») -> une seule.
    std::string reduced;
    for (std::size_t i = 0; i < out.size();) {
        std::size_t run = 1;
        while (i + run < out.size() && out[i + run] == out[i]) ++run;
        const bool letter = out[i] >= 'a' && out[i] <= 'z';
        reduced.append(letter && run >= 3 ? 1 : run, out[i]);
        i += run;
    }
    return reduced;
}

std::vector<std::string> tokenize(std::string_view raw) {
    const std::string text = normalizeText(raw);
    std::vector<std::string> tokens;
    std::string current;
    const auto flush = [&] {
        if (!current.empty()) tokens.push_back(current);
        current.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        // Nombres décimaux et négatifs (« -8,5 ») gardés entiers.
        const bool numberPart = (c == '.' || c == ',') && !current.empty() && std::isdigit(static_cast<unsigned char>(current.back())) &&
                                i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]));
        const bool sign = c == '-' && current.empty() && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]));
        if (isAlnum(c) || numberPart || sign) {
            current += c == ',' ? '.' : c;
        } else {
            flush();
            if (c == ',' || c == '.' || c == '!' || c == '?' || c == ';' || c == ':') tokens.emplace_back(1, c);
        }
    }
    flush();
    return tokens;
}

bool isWord(std::string_view t) { return !t.empty() && (isAlnum(t[0]) || t[0] == '-'); }

std::string phoneticKey(std::string_view word) {
    std::string w(word);
    std::string out;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const char c = w[i];
        const char next = i + 1 < w.size() ? w[i + 1] : '\0';
        if (c == 'p' && next == 'h') { out += 'f'; ++i; continue; }
        if (c == 'q' && next == 'u') { out += 'k'; ++i; continue; }
        if (c == 'c' && next == 'k') { out += 'k'; ++i; continue; }
        if (c == 'c' && next == 'h') { out += 'x'; ++i; continue; } // « ch » : un seul son
        if (c == 'c') { out += (next == 'e' || next == 'i' || next == 'y') ? 's' : 'k'; continue; }
        if (c == 'q') { out += 'k'; continue; }
        if (c == 'y') { out += 'i'; continue; }
        if (c == 'z') { out += 's'; continue; }
        if (c == 'h') continue; // muet
        if (c == 'e' && next == 'a' && i + 2 < w.size() && w[i + 2] == 'u') { out += 'o'; i += 2; continue; } // eau
        if (c == 'a' && next == 'u') { out += 'o'; ++i; continue; }
        out += c;
    }
    // Doubles lettres réduites.
    std::string reduced;
    for (char c : out)
        if (reduced.empty() || reduced.back() != c) reduced += c;
    // Finales muettes (une seule passe pour ne pas vider les mots courts).
    while (reduced.size() > 3 && (reduced.back() == 's' || reduced.back() == 'x' || reduced.back() == 't' || reduced.back() == 'd' ||
                                  reduced.back() == 'e'))
        reduced.pop_back();
    return reduced;
}

int damerauLevenshtein(std::string_view a, std::string_view b) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
    for (std::size_t i = 0; i <= n; ++i) d[i][0] = static_cast<int>(i);
    for (std::size_t j = 0; j <= m; ++j) d[0][j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= n; ++i)
        for (std::size_t j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[n][m];
}

bool fuzzyEquals(std::string_view word, std::string_view reference) {
    if (word == reference) return true;
    // Un nom avec des chiffres (« 2hollis », « 808 ») ne se compare qu'exactement.
    const auto hasDigit = [](std::string_view s) { return std::any_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    if (hasDigit(word) || hasDigit(reference)) return false;
    const std::string a = phoneticKey(word), b = phoneticKey(reference);
    if (a == b) return word.size() >= 3 || reference.size() <= 3;
    const std::size_t len = std::min(a.size(), b.size());
    // Une faute garde le premier son (« 2oli » n'est pas « poli »).
    if (len < 4 || a[0] != b[0]) return false;
    const int allowed = len >= 8 ? 2 : 1;
    return damerauLevenshtein(a, b) <= allowed;
}

} // namespace omnis::ai
