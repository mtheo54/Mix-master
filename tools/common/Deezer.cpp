// =============================================================================
//  Abrasion Labs // OMNIS
//  tools/common/Deezer.cpp
// =============================================================================
#include "Deezer.h"

#include "ai/ReferenceLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace omnis::tools::deezer {

namespace {

class Reader {
public:
    explicit Reader(const std::string& t) : t_(t) {}
    bool value(Json& out, int depth = 0) {
        if (depth > 64) return false;
        space();
        if (i_ >= t_.size()) return false;
        const char c = t_[i_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.string);
        }
        if (literal("true")) return out.type = Json::Type::Bool, out.boolean = true, true;
        if (literal("false")) return out.type = Json::Type::Bool, out.boolean = false, true;
        if (literal("null")) return out.type = Json::Type::Null, true;
        return number(out);
    }
    bool end() {
        space();
        return i_ == t_.size();
    }

private:
    void space() {
        while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) ++i_;
    }
    bool literal(const char* word) {
        const std::string w(word);
        if (t_.compare(i_, w.size(), w) != 0) return false;
        i_ += w.size();
        return true;
    }
    bool number(Json& out) {
        const std::size_t start = i_;
        while (i_ < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[i_])) || std::string_view("+-.eE").find(t_[i_]) != std::string_view::npos))
            ++i_;
        if (i_ == start) return false;
        out.type = Json::Type::Number;
        out.number = std::strtod(t_.substr(start, i_ - start).c_str(), nullptr);
        return true;
    }
    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) s += static_cast<char>(0xC0 | (cp >> 6)), s += static_cast<char>(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            s += static_cast<char>(0xE0 | (cp >> 12)), s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), s += static_cast<char>(0x80 | (cp & 0x3F));
        else
            s += static_cast<char>(0xF0 | (cp >> 18)), s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    bool hex4(unsigned& cp) {
        if (i_ + 4 > t_.size()) return false;
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = t_[i_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        ++i_; // "
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= t_.size()) return false;
            const char e = t_[i_++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                // Paire de substitution (emoji, etc.).
                if (cp >= 0xD800 && cp < 0xDC00 && t_.compare(i_, 2, "\\u") == 0) {
                    i_ += 2;
                    unsigned low = 0;
                    if (!hex4(low)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out += e; // \" \\ \/
            }
        }
        return false;
    }
    bool array(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++i_;
        space();
        if (i_ < t_.size() && t_[i_] == ']') return ++i_, true;
        while (true) {
            Json v;
            if (!value(v, depth + 1)) return false;
            out.array.push_back(std::move(v));
            space();
            if (i_ >= t_.size()) return false;
            if (t_[i_] == ',') ++i_;
            else if (t_[i_] == ']') return ++i_, true;
            else return false;
        }
    }
    bool object(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++i_;
        space();
        if (i_ < t_.size() && t_[i_] == '}') return ++i_, true;
        while (true) {
            space();
            std::string key;
            if (i_ >= t_.size() || t_[i_] != '"' || !string(key)) return false;
            space();
            if (i_ >= t_.size() || t_[i_++] != ':') return false;
            Json v;
            if (!value(v, depth + 1)) return false;
            out.object[key] = std::move(v);
            space();
            if (i_ >= t_.size()) return false;
            if (t_[i_] == ',') ++i_;
            else if (t_[i_] == '}') return ++i_, true;
            else return false;
        }
    }
    const std::string& t_;
    std::size_t i_ = 0;
};

const Json& nothing() {
    static const Json n;
    return n;
}

/// « Titre (feat. X) » -> « Titre » ; « Titre - Remastered » -> « Titre ».
std::string bare(std::string s) {
    for (const char* cut : {" (", " [", " - "})
        if (const auto p = s.find(cut); p != std::string::npos && p > 0) s.erase(p);
    return s;
}

} // namespace

const Json& Json::operator[](const std::string& key) const {
    if (type != Type::Object) return nothing();
    const auto it = object.find(key);
    return it == object.end() ? nothing() : it->second;
}

bool parseJson(const std::string& text, Json& out) {
    Reader r(text);
    out = Json{};
    return r.value(out) && r.end();
}

namespace {

/// Chemin depuis de l'UTF-8 (accents compris, sous Windows aussi).
std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

/// Lit la réponse ; renseigne la page (suivante, quota, erreur).
bool open(const std::string& text, Json& j, Page* page) {
    const bool ok = parseJson(text, j);
    if (page) {
        *page = Page{};
        if (!ok) page->error = text.empty() ? "pas de réponse" : "réponse illisible";
        else if (j["error"].type == Json::Type::Object) {
            page->quota = static_cast<int>(j["error"].num("code")) == 4;
            page->error = j["error"].str("message");
        } else page->next = j.str("next");
    }
    return ok;
}

} // namespace

std::vector<Artist> parseArtistList(const std::string& text, Page* page) {
    Json j;
    std::vector<Artist> out;
    if (!open(text, j, page)) return out;
    for (const auto& a : j["data"].array) {
        Artist x;
        x.id = static_cast<long long>(a.num("id"));
        x.name = a.str("name");
        x.link = a.str("link");
        x.picture = a.str("picture_xl").empty() ? a.str("picture_big") : a.str("picture_xl");
        x.fans = static_cast<long long>(a.num("nb_fan"));
        x.albums = static_cast<int>(a.num("nb_album"));
        if (!x.name.empty()) out.push_back(std::move(x));
    }
    return out;
}

std::vector<Artist> parseArtists(const std::string& text) { return parseArtistList(text, nullptr); }

namespace {

Artist artistOf(const Json& a) {
    Artist x;
    x.id = static_cast<long long>(a.num("id"));
    x.name = a.str("name");
    x.link = a.str("link");
    x.picture = a.str("picture_xl").empty() ? a.str("picture_big") : a.str("picture_xl");
    x.fans = static_cast<long long>(a.num("nb_fan"));
    x.albums = static_cast<int>(a.num("nb_album"));
    return x;
}

} // namespace

int parseAlbumGenre(const std::string& text) {
    Json j;
    if (!parseJson(text, j) || j["error"].type == Json::Type::Object) return 0;
    return static_cast<int>(j.num("genre_id"));
}

Artist parseArtist(const std::string& text) {
    Json j;
    if (!parseJson(text, j) || j["error"].type == Json::Type::Object) return {};
    return artistOf(j);
}

TrackDetail parseTrackDetail(const std::string& text) {
    Json j;
    TrackDetail d;
    if (!parseJson(text, j) || j["error"].type == Json::Type::Object) return d;
    auto& x = d.track;
    x.id = static_cast<long long>(j.num("id"));
    x.title = j.str("title");
    x.link = j.str("link");
    x.artist = j["artist"].str("name");
    x.artistId = static_cast<long long>(j["artist"].num("id"));
    x.album = j["album"].str("title");
    x.cover = j["album"].str("cover_xl");
    x.durationS = static_cast<int>(j.num("duration"));
    x.isrc = j.str("isrc");
    d.albumId = static_cast<long long>(j["album"].num("id"));
    d.albumDate = j["album"].str("release_date").empty() ? j.str("release_date") : j["album"].str("release_date");
    for (const auto& c : j["contributors"].array) d.contributors.push_back(artistOf(c));
    return d;
}

std::vector<Track> parseTracks(const std::string& text, Page* page) {
    Json j;
    std::vector<Track> out;
    if (!open(text, j, page)) return out;
    for (const auto& t : j["data"].array) {
        Track x;
        x.id = static_cast<long long>(t.num("id"));
        x.title = t.str("title");
        x.link = t.str("link");
        x.artist = t["artist"].str("name");
        x.artistId = static_cast<long long>(t["artist"].num("id"));
        x.album = t["album"].str("title");
        x.cover = t["album"].str("cover_xl").empty() ? t["album"].str("cover_big") : t["album"].str("cover_xl");
        x.durationS = static_cast<int>(t.num("duration"));
        x.isrc = t.str("isrc");
        if (!x.title.empty()) out.push_back(std::move(x));
    }
    return out;
}

std::vector<Album> parseAlbums(const std::string& text, Page* page) {
    Json j;
    std::vector<Album> out;
    if (!open(text, j, page)) return out;
    for (const auto& a : j["data"].array) {
        Album x;
        x.id = static_cast<long long>(a.num("id"));
        x.title = a.str("title");
        x.type = a.str("record_type");
        x.date = a.str("release_date");
        x.link = a.str("link");
        x.genre = static_cast<int>(a.num("genre_id"));
        if (x.id != 0) out.push_back(std::move(x));
    }
    return out;
}

namespace {

/// Nom réduit : minuscules, sans accents (latin), lettres et chiffres seulement.
std::string key(const std::string& text) {
    static const char* const kLatin = "aaaaaaaceeeeiiiidnooooo ouuuuytsaaaaaaaceeeeiiiidnooooo ouuuuyty";
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
            static_cast<unsigned char>(text[i + 1]) <= 0xBF)
            c = static_cast<unsigned char>(kLatin[static_cast<unsigned char>(text[++i]) - 0x80]);
        if (c == '$') c = 's'; // A$AP = ASAP
        if (c < 0x80 && std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

/// Au plus une lettre changée, ajoutée ou enlevée.
bool oneEdit(const std::string& a, const std::string& b) {
    if (a == b) return true;
    const std::size_t n = a.size(), m = b.size();
    if (n + 1 < m || m + 1 < n) return false;
    std::size_t i = 0, j = 0;
    bool used = false;
    while (i < n && j < m) {
        if (a[i] == b[j]) {
            ++i, ++j;
            continue;
        }
        if (used) return false;
        used = true;
        if (n > m) ++i;
        else if (m > n) ++j;
        else ++i, ++j;
    }
    return !(used && (i < n || j < m));
}

} // namespace

const Artist* pickArtist(const std::vector<Artist>& found, const std::string& name) {
    // Le même nom (accents, casse, espaces, « $ » mis à part), sinon une seule lettre
    // d'écart pour un nom assez long : « omyll » ne devient pas « Omellette ».
    const std::string wanted = key(name);
    const Artist* best = nullptr;
    for (int pass = 0; pass < 2 && !best; ++pass)
        for (const auto& a : found) {
            const std::string k = key(a.name);
            const bool same = pass == 0 ? k == wanted : wanted.size() >= 5 && oneEdit(k, wanted);
            if (!wanted.empty() && same && (!best || a.fans > best->fans)) best = &a;
        }
    return best;
}

bool sameArtist(const std::string& a, const std::string& b) { return !key(a).empty() && key(a) == key(b); }

const Track* pickTrack(const std::vector<Track>& found, const std::string& artist, const std::string& title) {
    for (const auto& t : found)
        if (ai::namesMatch(t.artist, artist) && ai::namesMatch(t.title, title)) return &t;
    for (const auto& t : found)
        if (ai::namesMatch(t.artist, artist) && ai::namesMatch(bare(t.title), bare(title))) return &t;
    return nullptr;
}

std::string bareTitle(const std::string& title) { return bare(title); }

std::string urlEncode(const std::string& text) {
    std::string out;
    char b[4];
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else if (c == ' ') out += '+';
        else std::snprintf(b, sizeof b, "%%%02X", c), out += b;
    }
    return out;
}

std::string artistSearchUrl(const std::string& name) { return "https://api.deezer.com/search/artist?q=" + urlEncode(name); }

std::vector<std::string> trackSearchUrls(const std::string& artist, const std::string& title) {
    const std::string base = "https://api.deezer.com/search/track?q=";
    return {base + urlEncode("artist:\"" + artist + "\" track:\"" + bare(title) + "\""), base + urlEncode(artist + " " + bare(title))};
}

bool safeUrl(const std::string& url) {
    if (url.rfind("https://", 0) != 0 || url.size() > 2000) return false;
    return std::all_of(url.begin(), url.end(), [](unsigned char c) {
        return std::isalnum(c) || std::string_view("-._~:/?=&%+").find(static_cast<char>(c)) != std::string_view::npos;
    });
}

Fetcher::Fetcher(std::string cacheDir) : dir_(std::move(cacheDir)) {
    std::error_code ec;
    std::filesystem::create_directories(pathOf(dir_), ec);
}

std::string Fetcher::cachedName(const std::string& url) const {
    // Nom ASCII tiré de l'adresse (FNV-1a 64 bits) : curl n'a jamais à écrire un
    // chemin accentué (Windows lui passe la ligne de commande dans sa page de code).
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : url) h = (h ^ c) * 1099511628211ull;
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

bool Fetcher::runCurl(const std::vector<std::string>& urls) {
    // Une liste d'adresses et de fichiers (relatifs au cache) : un seul curl,
    // une seule connexion. curl est lancé depuis le dossier cache.
    const auto dir = pathOf(dir_);
    {
        std::ofstream cfg(dir / "requetes.cfg", std::ios::binary | std::ios::trunc);
        for (const auto& url : urls) cfg << "url = \"" << url << "\"\noutput = \"" << cachedName(url) << ".part\"\n";
    }
    std::error_code ec;
    const auto back = std::filesystem::current_path(ec);
    std::filesystem::current_path(dir, ec);
    if (ec) return false;
    // En parallèle (-Z, curl 7.66 et plus) : un paquet de 40 en ~1 s au lieu de ~25 ;
    // un curl plus ancien refuse l'option (code 2) : on repasse en série.
#if defined(_WIN32)
    const char* parallel = "curl.exe -sSL -Z --parallel-max 8 --max-time 30 -K requetes.cfg 2>NUL";
    const char* serial = "curl.exe -sSL --max-time 30 -K requetes.cfg 2>NUL";
#else
    const char* parallel = "curl -sSL -Z --parallel-max 8 --max-time 30 -K requetes.cfg 2>/dev/null";
    const char* serial = "curl -sSL --max-time 30 -K requetes.cfg 2>/dev/null";
#endif
    int rc = std::system(parallel_ ? parallel : serial);
    if (parallel_ && rc != 0 && urls.size() > 0) {
        std::error_code e2;
        bool any = false;
        for (const auto& url : urls) any = any || std::filesystem::exists(dir / (cachedName(url) + ".part"), e2);
        if (!any) { // rien reçu : option inconnue ? une fois en série pour voir
            rc = std::system(serial);
            for (const auto& url : urls) any = any || std::filesystem::exists(dir / (cachedName(url) + ".part"), e2);
            if (any) parallel_ = false;
        }
    }
    std::filesystem::current_path(back, ec);
    requests_ += static_cast<long long>(urls.size());
    return rc == 0;
}

std::vector<std::string> Fetcher::getAll(const std::vector<std::string>& urls) {
    namespace fs = std::filesystem;
    const auto dir = pathOf(dir_);
    std::vector<std::string> out(urls.size());
    const auto readFile = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto quota = [](const std::string& t) { return t.find("\"error\"") != std::string::npos && t.find("\"code\":4") != std::string::npos; };
    std::vector<std::size_t> missing;
    for (std::size_t i = 0; i < urls.size(); ++i) {
        if (!safeUrl(urls[i])) continue;
        std::error_code ec;
        const auto cached = dir / (cachedName(urls[i]) + ".json");
        if (keep && fs::exists(cached, ec)) out[i] = readFile(cached);
        else missing.push_back(i);
    }
    // Paquets de 40 au plus, un paquet toutes les 5 s au mieux : sous la limite de l'API.
    for (int attempt = 0; attempt < 6 && !missing.empty(); ++attempt) {
        std::vector<std::size_t> again;
        for (std::size_t start = 0; start < missing.size(); start += 40) {
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<std::string> batch;
            const std::size_t end = std::min(missing.size(), start + 40);
            for (std::size_t k = start; k < end; ++k) batch.push_back(urls[missing[k]]);
            runCurl(batch);
            for (std::size_t k = start; k < end; ++k) {
                const auto i = missing[k];
                std::error_code ec;
                const auto part = dir / (cachedName(urls[i]) + ".part");
                std::string text = fs::exists(part, ec) ? readFile(part) : std::string();
                fs::remove(part, ec);
                if (text.empty() || quota(text)) {
                    again.push_back(i);
                    continue;
                }
                out[i] = text;
                if (keep && text.find("\"error\"") == std::string::npos) {
                    std::ofstream(dir / (cachedName(urls[i]) + ".json"), std::ios::binary) << text;
                }
            }
            const auto spent = std::chrono::steady_clock::now() - t0;
            if (end - start >= 10 && spent < std::chrono::milliseconds(5000)) std::this_thread::sleep_for(std::chrono::milliseconds(5000) - spent);
        }
        missing.swap(again);
        if (!missing.empty()) std::this_thread::sleep_for(std::chrono::seconds(5 + 5 * attempt)); // quota : attendre
    }
    return out;
}

std::string Fetcher::fetchBytes(const std::string& url, std::string& error) {
    if (!safeUrl(url)) {
        error = "adresse refusée : " + url;
        return {};
    }
#if defined(_WIN32)
    FILE* pipe = _popen(("curl.exe -sSfL --max-time 30 \"" + url + "\" 2>NUL").c_str(), "rb");
#else
    FILE* pipe = popen(("curl -sSfL --max-time 30 \"" + url + "\" 2>/dev/null").c_str(), "r");
#endif
    if (!pipe) {
        error = "curl introuvable";
        return {};
    }
    std::string bytes;
    char buffer[65536];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, pipe)) > 0;) {
        bytes.append(buffer, n);
        if (bytes.size() > (32u << 20)) break; // une image, pas plus de 32 Mo
    }
#if defined(_WIN32)
    const int rc = _pclose(pipe);
#else
    const int rc = pclose(pipe);
#endif
    ++requests_;
    if (rc != 0 || bytes.empty()) {
        error = "téléchargement impossible (internet ? curl ?) : " + url;
        return {};
    }
    return bytes;
}

bool Fetcher::download(const std::string& url, const std::string& path, std::string& error) {
    if (!safeUrl(url)) {
        error = "adresse refusée : " + url;
        return false;
    }
    namespace fs = std::filesystem;
    const auto dir = pathOf(dir_);
    runCurl({url});
    std::error_code ec;
    const auto part = dir / (cachedName(url) + ".part");
    if (!fs::exists(part, ec) || fs::file_size(part, ec) == 0) {
        fs::remove(part, ec);
        error = "téléchargement impossible (internet ? curl ?) : " + url;
        return false;
    }
    fs::copy_file(part, pathOf(path), fs::copy_options::overwrite_existing, ec);
    fs::remove(part, ec);
    if (ec) error = "copie impossible : " + path;
    return !ec;
}

} // namespace omnis::tools::deezer
