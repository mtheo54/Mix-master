// =============================================================================
//  Abrasion Labs // OMNIS
//  ai/Lexicon.cpp
// =============================================================================
#include "Lexicon.h"

#include "EmbeddedData.h"
#include "TextMatch.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace omnis::ai {

namespace {

struct AxisInfo {
    const char* key;
    const char* label;
    const char* unit;
    bool applied;
};

constexpr AxisInfo kAxes[kAxisCount] = {
    {"sub", "sub (20–80 Hz)", "dB", true},
    {"bas", "bas (110–230 Hz)", "dB", true},
    {"bas_medium", "bas-médium (320–640 Hz)", "dB", true},
    {"medium", "médium (0.9–2.6 kHz)", "dB", true},
    {"presence", "présence (3.6–7 kHz)", "dB", true},
    {"air", "air (10–20 kHz)", "dB", true},
    {"largeur", "largeur stéréo", "dB", true},
    {"sonie", "sonie", "LU", true},
    {"proprete", "propreté", "", true},
    {"ressemblance", "ressemblance à la référence", "", true},
    {"punch", "punch / attaque", "", false},
    {"densite", "densité / compression", "", false},
    {"saturation", "saturation / grain", "", false},
};

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == sep) {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    return out;
}

std::vector<std::string> wordsOf(std::string_view expression) {
    std::vector<std::string> out;
    for (auto& t : tokenize(expression))
        if (isWord(t)) out.push_back(t);
    return out;
}

// ---- Grammaire -----------------------------------------------------------------

using Phrases = std::vector<std::vector<std::string>>;

/// Une phrase de la grammaire. Préfixe « = » : comparaison exacte, pour les mots
/// anglais courts qu'une faute supposée confondrait avec un mot français
/// (« less » / « les », « more » / « mort », « so » / « sa »).
Phrases phrases(std::initializer_list<const char*> list) {
    Phrases out;
    for (const char* p : list) {
        const bool exact = p[0] == '=';
        auto words = wordsOf(exact ? p + 1 : p);
        if (exact)
            for (auto& w : words) w.insert(0, 1, '=');
        out.push_back(std::move(words));
    }
    // Les plus longues d'abord : « pas assez de » avant « pas ».
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    return out;
}

bool tokenEquals(const std::string& token, const std::string& word) {
    // « =mot » : égalité stricte. Mots grammaticaux très courts : égalité
    // exacte ; courts : même son (« veu » = « veux ») ; longs : fautes tolérées.
    if (!word.empty() && word[0] == '=') return token.compare(0, std::string::npos, word, 1, std::string::npos) == 0;
    if (token == word) return true;
    if (word.size() < 3 || token.size() < 3) return false;
    if (word.size() < 5) return phoneticKey(token) == phoneticKey(word);
    return fuzzyEquals(token, word);
}

/// Longueur (en jetons) de la plus longue phrase de `list` qui commence en i, ou 0.
std::size_t matchPhrases(const std::vector<std::string>& tokens, std::size_t i, std::size_t end, const Phrases& list) {
    for (const auto& p : list) {
        if (p.empty() || i + p.size() > end) continue;
        bool ok = true;
        for (std::size_t k = 0; k < p.size() && ok; ++k) ok = tokenEquals(tokens[i + k], p[k]);
        if (ok) return p.size();
    }
    return 0;
}

const Phrases& clauseBreaks() {
    static const Phrases p = phrases({"mais", "et", "par contre", "sauf", "puis", "cependant", "pourtant", "=but", "=and", "however",
                                      "though", "=also"});
    return p;
}
/// Coupures après lesquelles le sujet de la proposition précédente reste celui
/// de la suivante : « la basse et le kick sont trop forts ».
bool joinsSubjects(const std::string& token) { return token == "," || token == "et" || token == "and"; }

const Phrases& intensifiers() {
    static const Phrases p = phrases({"tres", "vraiment", "grave", "beaucoup", "enormement", "ultra", "hyper", "super", "mega", "giga",
                                      "bien", "carrement", "franchement", "totalement", "completement", "archi", "tellement",
                                      "vachement", "salement", "de ouf", "a mort", "de fou", "de malade", "trop trop",
                                      "really", "=very", "=so", "=way", "=much", "a lot", "=lots", "a ton", "totally", "seriously",
                                      "exactement", "pile"});
    return p;
}
const Phrases& diminishers() {
    static const Phrases p = phrases({"un peu", "un petit peu", "un tout petit peu", "petit peu", "legerement", "un poil", "un chouia",
                                      "leger", "legere", "subtilement", "doucement", "a peine", "un brin", "une touche", "une touche de",
                                      "un soupcon", "un soupcon de", "tout doux",
                                      "slightly", "=a =bit", "a little", "a little bit", "a hint", "a hint of", "a tad", "a touch",
                                      "somewhat", "subtly", "lightly", "gently", "=bit", "=pretty"});
    return p;
}
const Phrases& more() {
    static const Phrases p = phrases({"plus", "plus de", "davantage", "rajoute", "ajoute", "rajouter", "ajouter", "monte", "monter",
                                      "boost", "booste", "booster", "augmente", "augmenter", "renforce", "renforcer", "pousse", "pousser",
                                      "manque", "manque de", "il manque", "ca manque", "ca manque de", "pas assez", "pas assez de",
                                      "besoin de", "encore plus", "manquer", "remonte", "accentue",
                                      "=more", "more of", "=add", "add more", "=adds", "raise", "increase", "bring up", "turn up",
                                      "pump up", "=push", "needs more", "need more", "=need", "=needs", "missing", "is missing",
                                      "not enough", "not enough of", "lacks", "lack of", "lacking", "=give", "give me", "give it",
                                      "enhance", "emphasize", "bump", "bump up"});
    return p;
}
const Phrases& less() {
    static const Phrases p = phrases({"moins", "moins de", "baisse", "baisser", "enleve", "enlever", "retire", "retirer", "reduis",
                                      "reduire", "coupe", "couper", "calme", "calmer", "attenue", "attenuer", "adoucis", "adoucir",
                                      "vire", "virer", "degage", "diminue", "diminuer", "allege", "alleger", "retiens",
                                      "reduce", "remove", "less of", "=less", "=cut", "lower", "decrease", "tame", "turn down",
                                      "bring down", "pull back", "dial back", "back off", "get rid of", "take out", "take away",
                                      "fewer", "minus", "=nix"});
    return p;
}
/// « pas trop fort » : un peu moins (pas « un peu plus »).
const Phrases& notTooMuch() {
    static const Phrases p = phrases({"pas trop", "pas trop de", "pas si", "not too", "not so", "=not =that"});
    return p;
}
const Phrases& tooMuch() {
    static const Phrases p = phrases({"trop", "trop de", "=too", "too much", "too much of", "=way =too", "overly", "excessive",
                                      "excessively"});
    return p;
}
const Phrases& negations() {
    static const Phrases p = phrases({"pas", "pas de", "sans", "aucun", "aucune", "evite", "eviter", "jamais", "ni", "without",
                                      "=no", "=not", "never", "=zero", "=avoid"});
    return p;
}
const Phrases& keeps() {
    static const Phrases p = phrases({"garde", "garder", "gardes", "protege", "proteger", "conserve", "conserver", "touche pas",
                                      "touche pas a", "touches pas", "laisse", "laisser", "respecte", "respecter", "preserve", "preserver",
                                      "ne change pas", "change pas", "bouge pas", "pas toucher", "=keep", "dont touch", "dont change",
                                      "dont mess with", "leave", "protect"});
    return p;
}
const Phrases& stateMarkers() {
    static const Phrases p = phrases({"c est", "s est", "ca sonne", "sa sonne", "sonne", "est", "etait", "on dirait", "je trouve",
                                      "ca fait", "sa fait", "le son est", "le mix est", "la prod est", "il y a", "y a", "ca a",
                                      "it sounds", "it is", "=is", "=are", "=was", "sounds", "feels", "it feels", "seems",
                                      "there is", "there s", "there are", "it has"});
    return p;
}
const Phrases& desireMarkers() {
    static const Phrases p = phrases({"je veux", "je voudrais", "veux", "voudrais", "faut", "faudrait", "il faut", "il faudrait", "fais",
                                      "rends", "rend", "mets", "donne", "j aimerais", "aimerais", "envie", "souhaite", "cherche",
                                      "i want", "=want", "make it", "=make", "i d like", "id like", "i would like", "can you",
                                      "could you", "i wish", "i need"});
    return p;
}
const Phrases& referenceTriggers() {
    static const Phrases p = phrases({"comme", "genre", "style", "facon", "type", "a la", "dans le style de", "dans l esprit de",
                                      "inspire de", "sonne comme", "a la maniere de", "dans le genre de", "dans la vibe de",
                                      "vibe de", "esprit de", "=like", "similar to", "sounds like", "kinda like", "in the style of",
                                      "inspired by", "vibe of", "the vibe of", "in the vibe of"});
    return p;
}
const Phrases& determiners() {
    static const Phrases p = phrases({"de", "du", "des", "d", "la", "le", "les", "l", "un", "une", "celui de", "ceux de", "ce que fait",
                                      "de chez", "a", "au", "aux", "sur", "son de", "le son de", "les sons de", "ca",
                                      "=the", "=a", "=an", "=of", "=some", "=that", "=this"});
    return p;
}
const Phrases& providedReference() {
    static const Phrases p = phrases({"ref", "la ref", "reference", "la reference", "le son", "ce son", "l exemple", "le morceau",
                                      "le titre", "ce titre", "ce morceau", "celui la", "celle la", "le son que je t ai envoye",
                                      "the ref", "the reference", "this track", "that track", "this song", "that song"});
    return p;
}
/// Après un concept : « propre de ouf », « clean as hell », « brighter a bit »,
/// « turn the bass down », « de la basse en plus ».
const Phrases& postIntensifiers() {
    static const Phrases p = phrases({"de ouf", "de fou", "de malade", "a mort", "a fond", "au max", "un max", "comme jamais",
                                      "as hell", "to the max", "like crazy", "=asf"});
    return p;
}
const Phrases& postDiminishers() {
    static const Phrases p = phrases({"un peu", "un poil", "un chouia", "legerement", "a peine", "=a =bit", "a little", "a little bit",
                                      "slightly", "a touch", "a tad"});
    return p;
}
const Phrases& postUp() {
    static const Phrases p = phrases({"=up", "en plus", "en plus fort"});
    return p;
}
const Phrases& postDown() {
    static const Phrases p = phrases({"=down", "en moins", "en moins fort"});
    return p;
}

// ---- Abréviations, SMS, argot (data/argot.txt) -----------------------------------

struct Shorthand {
    std::vector<std::string> from, to; ///< `to` vide : la forme est retirée
};

const std::vector<Shorthand>& shorthands() {
    static const std::vector<Shorthand> table = [] {
        std::vector<Shorthand> t;
        std::istringstream in{std::string(data::slang())};
        std::string line;
        while (std::getline(in, line)) {
            const std::string s = trim(line);
            const auto arrow = s.find("=>");
            if (s.empty() || s[0] == '#' || arrow == std::string::npos) continue;
            const std::string target = trim(s.substr(arrow + 2));
            const auto to = target == "_" ? std::vector<std::string>{} : wordsOf(target);
            for (const auto& form : split(s.substr(0, arrow), '|'))
                if (auto from = wordsOf(form); !from.empty()) t.push_back({std::move(from), to});
        }
        std::stable_sort(t.begin(), t.end(), [](const auto& a, const auto& b) { return a.from.size() > b.from.size(); });
        return t;
    }();
    return table;
}

bool isShorthandForm(const std::string& token) {
    for (const auto& s : shorthands())
        if (s.from.size() == 1 && s.from[0] == token) return true;
    return false;
}

bool isStopword(const std::string& t) {
    static const char* kWords[] = {
        "je", "j", "tu", "il", "elle", "on", "nous", "vous", "ils", "ca", "sa", "cela", "ce", "c", "cet", "cette", "ces", "que", "qu",
        "qui", "quoi", "le", "la", "les", "l", "un", "une", "des", "de", "du", "d", "a", "au", "aux", "en", "pour", "avec", "sur",
        "dans", "par", "mon", "ma", "mes", "ton", "ta", "tes", "son", "ses", "notre", "votre", "leur", "se", "s", "y", "est", "soit",
        "sois", "etre", "ai", "as", "avoir", "fait", "faire", "peux", "peut", "stp", "svp", "merci", "oui", "ok", "alors", "donc",
        "aussi", "encore", "juste", "vraiment", "tout", "tous", "toute", "quand", "si", "ou", "mix", "prod", "morceau", "titre",
        "track", "son", "sons", "musique", "son", "niveau", "the", "an", "my", "it", "is", "to", "of", "and", "please", "vois", "voir",
        "base", "chose", "truc", "trucs", "genre", "moi", "toi", "lui", "ici", "la", "comme", "ne", "n", "t", "m", "me", "te", "mais",
        "quelque", "fois", "bah", "bon", "ben", "euh", "wesh", "frero", "frere", "gros", "mec", "bro", "master", "mastering",
        "mixage", "mixer", "mastered", "beat", "instru", "instrumental", "partout", "global", "globalement", "general", "jsp",
        "jpp", "mdr", "lol", "ptdr", "jsais", "jpense", "jcrois", "pk", "pcq", "psk", "jveu", "jveux", "ke", "kan", "koi", "tkt",
        // Anglais courant (les mots de la grammaire sont dans les listes ci-dessus).
        "i", "you", "your", "me", "we", "us", "they", "he", "she", "this", "that", "these", "those", "be", "been", "being", "do",
        "does", "did", "can", "could", "would", "should", "will", "with", "for", "in", "on", "at", "from", "by", "up", "out", "just",
        "get", "got", "turn", "bring", "enough", "song", "sound", "tune", "record", "overall", "whole", "everything", "all", "stuff",
        "thing", "things", "mixdown", "right", "now", "here", "there", "some", "any", "kind", "sort", "lot", "bit", "pretty", "quite",
        "rather", "something", "maybe", "also", "too", "very", "really", "like", "want", "need", "needs", "make", "give", "beat",
        "type", "vibe", "hey", "yeah", "yes", "okay", "ok", "the", "a", "an", "it", "its", "of", "to", "and", "or", "is", "are",
        "was", "has", "have", "had", "much", "way", "so", "if", "then", "than", "when", "what", "which", "who", "sounds",
        "sounding", "feels", "top", "fan", "at", "everywhere", "part", "parts", "section", "drop", "intro", "outro", "hook",
        "refrain", "couplet", "verse",
        // Français parlé
        "ouais", "ouai", "voila", "bref", "enfin", "quoi", "hein", "nan", "non", "carrement", "grave", "ouf",
        "aujourd", "hui", "fin", "dernier", "derniere", "premier", "premiere", "cote", "pareil", "vrai", "comment", "pourquoi",
        "parce", "car", "deja", "plutot", "assez", "moment", "rien", "personne", "faudrait", "faut", "toujours", "maintenant", "dans",
        "avec", "sans", "etait", "suis", "es", "sommes", "etes", "sont", "va", "vais", "vas", "allez", "aller", "veut", "voulez"};
    for (const char* w : kWords)
        if (t == w) return true;
    return false;
}

bool isNumber(const std::string& t) {
    if (t.empty()) return false;
    std::size_t i = t[0] == '-' ? 1 : 0;
    bool digit = false;
    for (; i < t.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(t[i]))) digit = true;
        else if (t[i] != '.') return false;
    }
    return digit;
}

/// Meilleure expression du concept qui commence en i : score = 2 × longueur
/// + jetons identiques (une correspondance exacte bat une faute supposée :
/// « ça claque » est « ça claque », pas « ça craque »). 0 si aucune.
std::size_t matchConcept(const std::vector<std::string>& tokens, std::size_t i, std::size_t end, const Concept& c,
                         std::size_t* length = nullptr, const std::vector<char>* grammar = nullptr) {
    std::size_t best = 0, bestLen = 0;
    for (const auto& e : c.tokenized) {
        if (e.empty() || i + e.size() > end) continue;
        bool ok = true;
        std::size_t exact = 0;
        for (std::size_t k = 0; k < e.size() && ok; ++k) {
            // Un mot outil ou de la grammaire n'est jamais une faute de frappe : « turn » n'est
            // pas « terne », « sounds » n'est pas « soupe », « c » n'est pas « hq ».
            const bool tool = grammar ? (*grammar)[i + k] != 0 : isStopword(tokens[i + k]);
            ok = tokens[i + k] == e[k] || (!tool && fuzzyEquals(tokens[i + k], e[k]));
            exact += tokens[i + k] == e[k] ? 1 : 0;
        }
        const std::size_t score = 2 * e.size() + exact;
        if (ok && score > best) {
            best = score;
            bestLen = e.size();
        }
    }
    if (length) *length = bestLen;
    return best;
}

std::string join(const std::vector<std::string>& tokens, std::size_t from, std::size_t to) {
    std::string s;
    for (std::size_t k = from; k < to; ++k) {
        if (!s.empty()) s += ' ';
        s += tokens[k];
    }
    return s;
}

std::string formatNumber(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+g", v);
    return buf;
}

} // namespace

bool isCommonWord(const std::string& t) {
    if (isStopword(t) || isShorthandForm(t)) return true;
    const std::vector<std::string> one{t};
    for (const Phrases* list : {&intensifiers(), &diminishers(), &more(), &less(), &tooMuch(), &negations(), &keeps(),
                                &stateMarkers(), &desireMarkers(), &referenceTriggers(), &clauseBreaks()})
        if (matchPhrases(one, 0, 1, *list) == 1) return true;
    return false;
}

const char* axisKey(Axis a) noexcept { return kAxes[static_cast<int>(a)].key; }
const char* axisLabel(Axis a) noexcept { return kAxes[static_cast<int>(a)].label; }
const char* axisUnit(Axis a) noexcept { return kAxes[static_cast<int>(a)].unit; }
bool axisApplied(Axis a) noexcept { return kAxes[static_cast<int>(a)].applied; }

std::optional<Axis> axisFromKey(std::string_view key) noexcept {
    for (int i = 0; i < kAxisCount; ++i)
        if (key == kAxes[i].key) return static_cast<Axis>(i);
    return std::nullopt;
}

bool Lexicon::parse(std::string_view text, std::string* error) {
    bool ok = true;
    std::istringstream in{std::string(text)};
    std::string line;
    int number = 0;
    const auto fail = [&](const std::string& why) {
        ok = false;
        if (error) *error += "ligne " + std::to_string(number) + " : " + why + "\n";
    };
    while (std::getline(in, line)) {
        ++number;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const auto colon = t.find(':');
        const auto arrow = t.find("=>");
        if (colon == std::string::npos || arrow == std::string::npos || arrow < colon) {
            fail("format attendu « qualite|defaut id : expressions => effets »");
            continue;
        }
        std::istringstream head(t.substr(0, colon));
        std::string kind, id;
        head >> kind >> id;
        if ((kind != "qualite" && kind != "defaut") || id.empty()) {
            fail("type inconnu « " + kind + " »");
            continue;
        }
        Concept c;
        c.id = id;
        c.flaw = kind == "defaut";
        for (auto& e : split(t.substr(colon + 1, arrow - colon - 1), '|')) {
            if (e.empty()) continue;
            c.tokenized.push_back(wordsOf(e));
            c.expressions.push_back(std::move(e));
        }
        bool effectsOk = true;
        for (const auto& effect : split(t.substr(arrow + 2), ',')) {
            if (effect.empty()) continue;
            std::istringstream e(effect);
            std::string key;
            double value = 0.0;
            e.imbue(std::locale::classic());
            if (!(e >> key >> value)) {
                fail("effet illisible « " + effect + " »");
                effectsOk = false;
                break;
            }
            if (key == "cible_lufs") c.targetLufs = value;
            else if (auto axis = axisFromKey(key)) c.effect[*axis] = value;
            else {
                fail("axe inconnu « " + key + " »");
                effectsOk = false;
                break;
            }
        }
        if (!effectsOk || c.expressions.empty()) continue;
        add(std::move(c));
    }
    return ok;
}

std::string Lexicon::serialize() const {
    std::string out = "# OMNIS — lexique (généré)\n";
    for (const auto& c : concepts_) {
        out += c.flaw ? "defaut " : "qualite ";
        out += c.id + " : ";
        for (std::size_t i = 0; i < c.expressions.size(); ++i) out += (i ? " | " : "") + c.expressions[i];
        out += " =>";
        bool first = true;
        for (int a = 0; a < kAxisCount; ++a) {
            const double v = c.effect.v[static_cast<std::size_t>(a)];
            if (std::fabs(v) < 1e-9) continue;
            out += std::string(first ? " " : ", ") + kAxes[a].key + " " + formatNumber(std::round(v * 100.0) / 100.0);
            first = false;
        }
        if (c.targetLufs) out += std::string(first ? " " : ", ") + "cible_lufs " + formatNumber(*c.targetLufs);
        out += "\n";
    }
    return out;
}

void Lexicon::add(Concept c) {
    if (Concept* existing = find(c.id)) *existing = std::move(c);
    else concepts_.push_back(std::move(c));
}

Concept* Lexicon::find(std::string_view id) noexcept {
    for (auto& c : concepts_)
        if (c.id == id) return &c;
    return nullptr;
}

const Concept* Lexicon::find(std::string_view id) const noexcept {
    for (const auto& c : concepts_)
        if (c.id == id) return &c;
    return nullptr;
}

const Lexicon& Lexicon::builtin() {
    static const Lexicon lexicon = [] {
        Lexicon l;
        l.parse(data::lexicon());
        return l;
    }();
    return lexicon;
}

std::vector<std::string> expandShorthand(std::vector<std::string> tokens) {
    std::vector<std::string> out;
    out.reserve(tokens.size() + 4);
    for (std::size_t i = 0; i < tokens.size();) {
        const Shorthand* rule = nullptr;
        for (const auto& s : shorthands()) {
            if (i + s.from.size() > tokens.size()) continue;
            if (std::equal(s.from.begin(), s.from.end(), tokens.begin() + static_cast<std::ptrdiff_t>(i))) {
                rule = &s;
                break;
            }
        }
        if (!rule) {
            out.push_back(std::move(tokens[i++]));
            continue;
        }
        out.insert(out.end(), rule->to.begin(), rule->to.end());
        i += rule->from.size();
    }
    return out;
}

namespace {

/// Éléments du morceau qui peuvent être le SUJET d'un niveau : « la 808 est trop
/// forte », « the vocals need to be louder ». Le niveau (fort, discret, devant,
/// lointain) dit alors s'il faut monter ou baisser l'élément ; il ne touche pas
/// la sonie du master.
bool isElement(const Concept& c) {
    static const char* kIds[] = {"basse", "kick", "voix", "aigus", "mediums", "bas_medium", "caisse_claire", "hats", "melodie"};
    for (const char* id : kIds)
        if (c.id == id) return true;
    return false;
}
bool isLevel(const Concept& c) { return c.id == "fort" || c.id == "discret" || c.id == "presence" || c.id == "lointain"; }
/// Sens d'un niveau : + pour « fort », « devant » ; − pour « discret », « lointain ».
double levelSense(const Concept& c) { return c.effect[Axis::Loudness] + c.effect[Axis::Presence]; }

struct Clause {
    std::size_t from = 0, end = 0;
    bool joins = false; ///< coupée par « , », « et », « and » : le sujet peut continuer
};

} // namespace

Interpretation interpret(std::string_view text, const Lexicon& lexicon) {
    Interpretation out;
    const std::vector<std::string> tokens = expandShorthand(tokenize(text));
    std::vector<char> grammar(tokens.size());
    for (std::size_t i = 0; i < tokens.size(); ++i) grammar[i] = isWord(tokens[i]) && isCommonWord(tokens[i]);

    // Propositions : ponctuation et conjonctions.
    std::vector<Clause> clauses;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= tokens.size();) {
        if (i == tokens.size()) {
            clauses.push_back({start, i, false});
            break;
        }
        if (!isWord(tokens[i])) {
            clauses.push_back({start, i, joinsSubjects(tokens[i])});
            start = ++i;
            continue;
        }
        if (const std::size_t n = matchPhrases(tokens, i, tokens.size(), clauseBreaks())) {
            clauses.push_back({start, i, n == 1 && joinsSubjects(tokens[i])});
            i += n;
            start = i;
            continue;
        }
        ++i;
    }

    // Sujets en attente (indices dans out.hits) : éléments nommés sans
    // modificateur, que le niveau qui suit peut orienter.
    std::vector<std::size_t> subjects;
    bool subjectsLast = false; ///< rien n'a été compris depuis le dernier sujet
    bool carry = false;

    for (const auto& clause : clauses) {
        const std::size_t from = clause.from, end = clause.end;
        if (!(carry && subjectsLast)) subjects.clear();
        double intensity = 1.0, direction = 1.0;
        bool explicitMore = false, negation = false, keep = false, stateSeen = false, desireSeen = false, diminished = false;
        bool modified = false; ///< un modificateur attend son concept
        const auto resetModifiers = [&] {
            intensity = 1.0;
            direction = 1.0;
            explicitMore = negation = keep = diminished = modified = false;
        };
        for (std::size_t i = from; i < end;) {
            const std::string& t = tokens[i];
            if (t == kReferenceMarker) {
                ++i;
                continue;
            }

            // Sonie explicite : « -8 lufs », « -8lufs ».
            if (isNumber(t) && i + 1 < end && (tokens[i + 1] == "lufs" || tokens[i + 1] == "lu" || fuzzyEquals(tokens[i + 1], "lufs"))) {
                out.lufs = -std::fabs(std::strtod(t.c_str(), nullptr));
                i += 2;
                continue;
            }
            if (t.size() > 4 && t.compare(t.size() - 4, 4, "lufs") == 0 && isNumber(t.substr(0, t.size() - 4))) {
                out.lufs = -std::fabs(std::strtod(t.c_str(), nullptr));
                ++i;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, desireMarkers())) {
                desireSeen = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, keeps())) {
                keep = modified = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, referenceTriggers())) {
                // « exactement comme » / « un peu comme » : la ressemblance.
                if (intensity > 1.0) out.desire[Axis::Resemblance] += 0.3;
                if (diminished) out.desire[Axis::Resemblance] -= 0.2;
                std::size_t j = i + n;
                while (j < end) {
                    const std::size_t d = matchPhrases(tokens, j, end, determiners());
                    if (d == 0) break;
                    j += d;
                }
                if (j < end && tokens[j] == kReferenceMarker) {
                    // nom déjà reconnu dans la base : rien à collecter
                } else if (matchPhrases(tokens, j, end, providedReference()) > 0) {
                    out.mentionsProvidedReference = true;
                    j += matchPhrases(tokens, j, end, providedReference());
                } else {
                    std::size_t k = j;
                    while (k < end && k - j < 4) {
                        if (tokens[k] == kReferenceMarker) break; // « fein de <artiste reconnu> »
                        bool isConcept = false;
                        for (const auto& c : lexicon.concepts()) isConcept = isConcept || matchConcept(tokens, k, end, c, nullptr, &grammar) > 0;
                        if (isConcept || matchPhrases(tokens, k, end, desireMarkers()) || matchPhrases(tokens, k, end, keeps()) ||
                            matchPhrases(tokens, k, end, more()) || matchPhrases(tokens, k, end, less()) ||
                            matchPhrases(tokens, k, end, tooMuch()) || matchPhrases(tokens, k, end, intensifiers()) ||
                            matchPhrases(tokens, k, end, diminishers()) || matchPhrases(tokens, k, end, negations()) ||
                            tokens[k] == "avec" || tokens[k] == "en" || tokens[k] == "with" ||
                            tokens[k] == "pour" || tokens[k] == "qui" || tokens[k] == "que" || tokens[k] == "but")
                            break;
                        ++k;
                    }
                    // Déterminants en fin de mention retirés (« fein de » -> « fein »).
                    std::size_t last = k;
                    while (last > j && matchPhrases(tokens, last - 1, last, determiners()) == 1) --last;
                    // Une mention faite seulement de mots outils (« type beat ») n'est pas un nom.
                    bool content = false;
                    for (std::size_t m = j; m < last; ++m) content = content || !isStopword(tokens[m]);
                    if (last > j && content) out.referenceMentions.push_back(join(tokens, j, last));
                    j = k;
                }
                resetModifiers();
                i = j;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, notTooMuch())) {
                direction = -1.0;
                intensity *= 0.5;
                modified = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, diminishers())) {
                intensity *= 0.5;
                diminished = modified = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, intensifiers())) {
                intensity *= 1.6;
                modified = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, more())) {
                direction = 1.0;
                explicitMore = modified = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, less())) {
                direction = -1.0;
                modified = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, tooMuch())) {
                // « je veux que ce soit trop propre » : intensif ; sinon « trop brillant ».
                if (desireSeen) intensity *= 1.6;
                else direction = -1.0;
                modified = true;
                i += n;
                continue;
            }

            // Concept le mieux reconnu à cette position.
            const Concept* best = nullptr;
            std::size_t bestScore = 0, bestLen = 0;
            for (const auto& c : lexicon.concepts()) {
                std::size_t len = 0;
                const std::size_t score = matchConcept(tokens, i, end, c, &len, &grammar);
                if (score > bestScore) {
                    bestScore = score;
                    bestLen = len;
                    best = &c;
                }
            }
            if (best) {
                double sign;
                if (best->flaw) sign = explicitMore ? 1.0 : -1.0;     // une plainte : on corrige
                else if (negation) sign = stateSeen ? 1.0 : -direction; // « c'est pas propre » / « pas de disto »
                else sign = direction;
                ConceptHit hit{best->id, join(tokens, i, i + bestLen), sign * intensity, keep};
                const bool neutral = !modified && !negation;
                // Modificateurs placés après : « propre de ouf », « brighter a bit », « turn the bass down ».
                std::size_t j = i + bestLen;
                bool turned = false;
                for (;;) {
                    if (std::size_t n = matchPhrases(tokens, j, end, postIntensifiers())) hit.weight *= 1.6, j += n;
                    else if (std::size_t n2 = matchPhrases(tokens, j, end, postDiminishers())) hit.weight *= 0.5, j += n2;
                    else if (std::size_t n3 = best->flaw ? 0 : matchPhrases(tokens, j, end, postUp())) hit.weight = std::fabs(hit.weight), turned = true, j += n3;
                    else if (std::size_t n4 = best->flaw ? 0 : matchPhrases(tokens, j, end, postDown())) hit.weight = -std::fabs(hit.weight), turned = true, j += n4;
                    else break;
                }
                if (j > i + bestLen) hit.text = join(tokens, i, j);

                if (isLevel(*best) && !keep && !subjects.empty()) {
                    // « la 808 est trop forte » : la 808 baisse, la sonie ne bouge pas.
                    const double s = hit.weight * levelSense(*best) >= 0.0 ? 1.0 : -1.0;
                    for (std::size_t k : subjects) {
                        auto& subject = out.hits[k];
                        subject.weight = s * std::fabs(hit.weight) * std::fabs(subject.weight);
                        subject.text += " … " + hit.text;
                    }
                    subjects.clear();
                    subjectsLast = false;
                } else {
                    if (best->targetLufs && hit.weight > 0.0 && !keep) out.lufs = *best->targetLufs;
                    out.hits.push_back(std::move(hit));
                    if (isElement(*best) && !best->flaw && neutral && !turned && !keep) {
                        if (!subjectsLast) subjects.clear();
                        subjects.push_back(out.hits.size() - 1);
                        subjectsLast = true;
                    } else {
                        subjectsLast = false;
                    }
                }
                resetModifiers();
                i = j;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, negations())) {
                negation = modified = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, stateMarkers())) {
                stateSeen = true;
                i += n;
                continue;
            }
            if (!isStopword(t) && !isNumber(t) && t.size() >= 3) out.unknownWords.push_back(t);
            ++i;
        }
        // « la basse, trop » : un modificateur resté seul vise les sujets.
        if (modified && !subjects.empty() && subjectsLast && !keep) {
            const double s = negation ? (stateSeen ? 1.0 : -direction) : direction;
            for (std::size_t k : subjects) out.hits[k].weight = s * intensity * std::fabs(out.hits[k].weight);
            subjects.clear();
            subjectsLast = false;
        }
        carry = clause.joins;
    }

    // Les effets s'additionnent une fois les sujets orientés.
    for (const auto& hit : out.hits) {
        if (hit.protect) continue;
        const Concept* c = lexicon.find(hit.conceptId);
        if (!c) continue;
        for (int a = 0; a < kAxisCount; ++a) out.desire.v[static_cast<std::size_t>(a)] += hit.weight * c->effect.v[static_cast<std::size_t>(a)];
    }
    return out;
}

} // namespace omnis::ai
