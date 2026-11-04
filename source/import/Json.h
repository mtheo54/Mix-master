// =============================================================================
//  Abrasion Labs // OMNIS
//  import/Json.h — Lecteur JSON minimal (réponses de MusicBrainz).
// =============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

namespace omnis::import {

/// Une valeur JSON (lecteur minimal : ce que renvoie une API suffit).
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    [[nodiscard]] const Json& operator[](const std::string& key) const;
    [[nodiscard]] std::string str(const std::string& key) const { return (*this)[key].string; }
    [[nodiscard]] double num(const std::string& key) const { return (*this)[key].number; }
};

/// false : texte qui n'est pas du JSON.
bool parseJson(const std::string& text, Json& out);

} // namespace omnis::import
