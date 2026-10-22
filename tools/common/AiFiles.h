// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/AiFiles.h — Bases livrées + fichiers utilisateur.
//
//  Lexique et références = base livrée (compilée) puis, si un fichier
//  utilisateur est donné, ses entrées par-dessus (même id / même titre :
//  remplacées). C'est ce que fera le plug-in avec son dossier de données.
// =============================================================================
#pragma once

#include "ai/Lexicon.h"
#include "ai/ReferenceLibrary.h"

#include <fstream>
#include <sstream>
#include <string>

namespace omnis::tools {

inline bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

inline bool writeTextFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

inline ai::Lexicon loadLexicon(const std::string& userPath) {
    ai::Lexicon l = ai::Lexicon::builtin();
    std::string text;
    if (!userPath.empty() && readTextFile(userPath, text)) l.parse(text);
    return l;
}

inline ai::ReferenceLibrary loadLibrary(const std::string& userPath) {
    ai::ReferenceLibrary l = ai::ReferenceLibrary::builtin();
    std::string text;
    if (!userPath.empty() && readTextFile(userPath, text)) {
        ai::ReferenceLibrary user;
        user.parse(text);
        for (const auto& e : user.entries()) l.add(e);
    }
    return l;
}

} // namespace omnis::tools
