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

Phrases phrases(std::initializer_list<const char*> list) {
    Phrases out;
    for (const char* p : list) out.push_back(wordsOf(p));
    // Les plus longues d'abord : « pas assez de » avant « pas ».
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    return out;
}

bool tokenEquals(const std::string& token, const std::string& word) {
    // Mots grammaticaux très courts : égalité exacte ; courts : même son
    // (« veu » = « veux ») ; longs : fautes tolérées.
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
    static const Phrases p = phrases({"mais", "et", "par contre", "sauf", "puis", "cependant", "pourtant", "but", "and"});
    return p;
}
const Phrases& intensifiers() {
    static const Phrases p = phrases({"tres", "vraiment", "grave", "beaucoup", "enormement", "ultra", "hyper", "super", "mega", "bien",
                                      "carrement", "franchement", "totalement", "completement", "archi", "tellement",
                                      "really", "exactement", "pile", "vachement", "salement", "de ouf", "a mort"});
    return p;
}
const Phrases& diminishers() {
    static const Phrases p = phrases({"un peu", "un petit peu", "un tout petit peu", "petit peu", "legerement", "un poil", "un chouia",
                                      "leger", "legere", "subtilement", "doucement", "a peine", "un brin", "une touche", "une touche de",
                                      "slightly", "a bit", "a little", "un soupcon", "un soupcon de"});
    return p;
}
const Phrases& more() {
    static const Phrases p = phrases({"plus", "plus de", "davantage", "rajoute", "ajoute", "rajouter", "ajouter", "monte", "monter",
                                      "boost", "booste", "booster", "augmente", "augmenter", "renforce", "renforcer", "pousse", "pousser",
                                      "manque", "manque de", "il manque", "ca manque", "ca manque de", "pas assez", "pas assez de",
                                      "besoin de", "encore plus", "lacks", "lack of", "manquer", "needs more"});
    return p;
}
const Phrases& less() {
    static const Phrases p = phrases({"moins", "moins de", "baisse", "baisser", "enleve", "enlever", "retire", "retirer", "reduis",
                                      "reduire", "coupe", "couper", "calme", "calmer", "attenue", "attenuer", "adoucis", "adoucir",
                                      "vire", "virer", "degage", "reduce", "remove", "less of"});
    return p;
}
/// « pas trop fort » : un peu moins (pas « un peu plus »).
const Phrases& notTooMuch() {
    static const Phrases p = phrases({"pas trop", "pas trop de", "not too", "pas si"});
    return p;
}
const Phrases& tooMuch() {
    static const Phrases p = phrases({"trop", "trop de", "too much"});
    return p;
}
const Phrases& negations() {
    static const Phrases p = phrases({"pas", "pas de", "sans", "aucun", "aucune", "evite", "eviter", "jamais", "ni", "without"});
    return p;
}
const Phrases& keeps() {
    static const Phrases p = phrases({"garde", "garder", "gardes", "protege", "proteger", "conserve", "conserver", "touche pas",
                                      "touche pas a", "touches pas", "laisse", "laisser", "respecte", "respecter", "preserve", "preserver",
                                      "ne change pas", "change pas", "bouge pas", "pas toucher", "keep", "dont touch"});
    return p;
}
const Phrases& stateMarkers() {
    static const Phrases p = phrases({"c est", "s est", "ca sonne", "sa sonne", "sonne", "est", "etait", "on dirait", "je trouve",
                                      "ca fait", "sa fait", "le son est", "le mix est", "la prod est", "it sounds", "it is"});
    return p;
}
const Phrases& desireMarkers() {
    static const Phrases p = phrases({"je veux", "je voudrais", "veux", "voudrais", "faut", "faudrait", "il faut", "il faudrait", "fais",
                                      "rends", "rend", "mets", "donne", "j aimerais", "aimerais", "envie", "souhaite", "cherche",
                                      "i want", "make it"});
    return p;
}
const Phrases& referenceTriggers() {
    static const Phrases p = phrases({"comme", "genre", "style", "facon", "type", "a la", "dans le style de", "dans l esprit de",
                                      "inspire de", "sonne comme", "a la maniere de", "dans le genre de", "like", "similar to",
                                      "dans la vibe de", "vibe de", "esprit de"});
    return p;
}
const Phrases& determiners() {
    static const Phrases p = phrases({"de", "du", "des", "d", "la", "le", "les", "l", "un", "une", "celui de", "ceux de", "ce que fait",
                                      "de chez", "a", "au", "aux", "the", "sur", "son de", "le son de", "les sons de", "ca"});
    return p;
}
const Phrases& providedReference() {
    static const Phrases p = phrases({"ref", "la ref", "reference", "la reference", "le son", "ce son", "l exemple", "le morceau",
                                      "le titre", "ce titre", "ce morceau", "celui la", "celle la", "le son que je t ai envoye"});
    return p;
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
        "jpp", "mdr", "lol", "ptdr", "jsais", "jpense", "jcrois", "pk", "pcq", "psk", "jveu", "jveux", "ke", "kan", "koi", "tkt"};
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
                         std::size_t* length = nullptr) {
    std::size_t best = 0, bestLen = 0;
    for (const auto& e : c.tokenized) {
        if (e.empty() || i + e.size() > end) continue;
        bool ok = true;
        std::size_t exact = 0;
        for (std::size_t k = 0; k < e.size() && ok; ++k) {
            ok = fuzzyEquals(tokens[i + k], e[k]);
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
    if (isStopword(t)) return true;
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

Interpretation interpret(std::string_view text, const Lexicon& lexicon) {
    Interpretation out;
    const std::vector<std::string> tokens = tokenize(text);

    // Propositions : ponctuation et conjonctions.
    std::vector<std::pair<std::size_t, std::size_t>> clauses;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= tokens.size();) {
        if (i == tokens.size()) {
            clauses.emplace_back(start, i);
            break;
        }
        if (!isWord(tokens[i])) {
            clauses.emplace_back(start, i);
            start = ++i;
            continue;
        }
        if (const std::size_t n = matchPhrases(tokens, i, tokens.size(), clauseBreaks())) {
            clauses.emplace_back(start, i);
            i += n;
            start = i;
            continue;
        }
        ++i;
    }

    for (const auto& [from, end] : clauses) {
        double intensity = 1.0, direction = 1.0;
        bool explicitMore = false, negation = false, keep = false, stateSeen = false, desireSeen = false, diminished = false;
        const auto resetModifiers = [&] {
            intensity = 1.0;
            direction = 1.0;
            explicitMore = negation = keep = diminished = false;
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
                keep = true;
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
                        for (const auto& c : lexicon.concepts()) isConcept = isConcept || matchConcept(tokens, k, end, c) > 0;
                        if (isConcept || matchPhrases(tokens, k, end, desireMarkers()) || matchPhrases(tokens, k, end, keeps()) ||
                            matchPhrases(tokens, k, end, more()) || matchPhrases(tokens, k, end, less()) ||
                            matchPhrases(tokens, k, end, tooMuch()) || matchPhrases(tokens, k, end, intensifiers()) ||
                            matchPhrases(tokens, k, end, diminishers()) || matchPhrases(tokens, k, end, negations()) ||
                            tokens[k] == "avec" || tokens[k] == "en" ||
                            tokens[k] == "pour" || tokens[k] == "qui" || tokens[k] == "que")
                            break;
                        ++k;
                    }
                    // Déterminants en fin de mention retirés (« fein de » -> « fein »).
                    std::size_t last = k;
                    while (last > j && matchPhrases(tokens, last - 1, last, determiners()) == 1) --last;
                    if (last > j) out.referenceMentions.push_back(join(tokens, j, last));
                    j = k;
                }
                resetModifiers();
                i = j;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, notTooMuch())) {
                direction = -1.0;
                intensity *= 0.5;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, diminishers())) {
                intensity *= 0.5;
                diminished = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, intensifiers())) {
                intensity *= 1.6;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, more())) {
                direction = 1.0;
                explicitMore = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, less())) {
                direction = -1.0;
                i += n;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, tooMuch())) {
                // « je veux que ce soit trop propre » : intensif ; sinon « trop brillant ».
                if (desireSeen) intensity *= 1.6;
                else direction = -1.0;
                i += n;
                continue;
            }

            // Concept le mieux reconnu à cette position.
            const Concept* best = nullptr;
            std::size_t bestScore = 0, bestLen = 0;
            for (const auto& c : lexicon.concepts()) {
                std::size_t len = 0;
                const std::size_t score = matchConcept(tokens, i, end, c, &len);
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
                if (!keep) {
                    for (int a = 0; a < kAxisCount; ++a)
                        out.desire.v[static_cast<std::size_t>(a)] += hit.weight * best->effect.v[static_cast<std::size_t>(a)];
                    if (best->targetLufs && hit.weight > 0.0) out.lufs = *best->targetLufs;
                }
                out.hits.push_back(std::move(hit));
                resetModifiers();
                i += bestLen;
                continue;
            }
            if (std::size_t n = matchPhrases(tokens, i, end, negations())) {
                negation = true;
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
    }
    return out;
}

} // namespace omnis::ai
