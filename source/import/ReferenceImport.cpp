// =============================================================================
//  Abrasion Labs // OMNIS
//  import/ReferenceImport.cpp
// =============================================================================
#include "ReferenceImport.h"

#include "HttpGet.h"
#include "Json.h"

#include "AudioRead.h"
#include "ai/TextMatch.h"
#include "analysis/SpectralSignature.h"
#include "gfx/AvatarCache.h"
#include "gfx/OmnisAvatarStylizer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

namespace omnis::import {

namespace {

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::uint32_t u8(const std::string& b, std::size_t i) { return i < b.size() ? static_cast<unsigned char>(b[i]) : 0u; }
std::uint32_t le32(const std::string& b, std::size_t i) { return u8(b, i) | (u8(b, i + 1) << 8) | (u8(b, i + 2) << 16) | (u8(b, i + 3) << 24); }
std::uint32_t be32(const std::string& b, std::size_t i) { return (u8(b, i) << 24) | (u8(b, i + 1) << 16) | (u8(b, i + 2) << 8) | u8(b, i + 3); }
std::uint32_t be24(const std::string& b, std::size_t i) { return (u8(b, i) << 16) | (u8(b, i + 1) << 8) | u8(b, i + 2); }
std::uint32_t syncsafe(const std::string& b, std::size_t i) {
    return ((u8(b, i) & 0x7F) << 21) | ((u8(b, i + 1) & 0x7F) << 14) | ((u8(b, i + 2) & 0x7F) << 7) | (u8(b, i + 3) & 0x7F);
}

std::string trim(std::string s) {
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '\0')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) out += static_cast<char>(0xC0 | (cp >> 6)), out += static_cast<char>(0x80 | (cp & 0x3F));
    else if (cp < 0x10000)
        out += static_cast<char>(0xE0 | (cp >> 12)), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
    else
        out += static_cast<char>(0xF0 | (cp >> 18)), out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
}

std::string latin1(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == 0) break;
        appendUtf8(out, c);
    }
    return out;
}

std::string utf16(const std::string& s, bool bigEndian) {
    std::string out;
    std::size_t i = 0;
    if (s.size() >= 2 && ((u8(s, 0) == 0xFF && u8(s, 1) == 0xFE) || (u8(s, 0) == 0xFE && u8(s, 1) == 0xFF))) {
        bigEndian = u8(s, 0) == 0xFE;
        i = 2;
    }
    for (; i + 1 < s.size(); i += 2) {
        std::uint32_t c = bigEndian ? (u8(s, i) << 8) | u8(s, i + 1) : u8(s, i) | (u8(s, i + 1) << 8);
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < s.size()) {
            const std::uint32_t lo = bigEndian ? (u8(s, i + 2) << 8) | u8(s, i + 3) : u8(s, i + 2) | (u8(s, i + 3) << 8);
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        appendUtf8(out, c);
    }
    return out;
}

/// Texte d'une trame ID3 (premier octet : codage ; une seule valeur gardée).
std::string id3Text(const std::string& data) {
    if (data.empty()) return {};
    const unsigned enc = u8(data, 0);
    const std::string body = data.substr(1);
    if (enc == 1) return trim(utf16(body, false));
    if (enc == 2) return trim(utf16(body, true));
    std::string s = body.substr(0, body.find('\0'));
    return trim(enc == 3 ? s : latin1(s));
}

/// Les trames d'un bloc ID3v2 : id -> données (la première de chaque id).
struct Id3Frames {
    std::vector<std::pair<std::string, std::string>> frames;
    int version = 0;
};
Id3Frames id3Frames(const std::string& b) {
    Id3Frames out;
    if (b.size() < 10 || b.compare(0, 3, "ID3") != 0) return out;
    out.version = static_cast<int>(u8(b, 3));
    const unsigned flags = u8(b, 5);
    const std::size_t end = std::min<std::size_t>(b.size(), 10 + syncsafe(b, 6));
    std::size_t pos = 10;
    if ((flags & 0x40) && out.version >= 3) pos += out.version == 4 ? syncsafe(b, pos) : be32(b, pos) + 4;
    const bool v22 = out.version == 2;
    const std::size_t header = v22 ? 6 : 10;
    while (pos + header <= end) {
        const std::string id = b.substr(pos, v22 ? 3 : 4);
        if (id[0] == '\0') break;
        const std::size_t size = v22 ? be24(b, pos + 3) : out.version == 4 ? syncsafe(b, pos + 4) : be32(b, pos + 4);
        if (size == 0 || pos + header + size > end) break;
        out.frames.emplace_back(id, b.substr(pos + header, size));
        pos += header + size;
    }
    return out;
}

/// Pochettes d'un bloc ID3v2 (APIC / PIC) : pochette avant (type 3) en premier.
std::vector<std::string> id3Pictures(const std::string& b) {
    std::vector<std::pair<unsigned, std::string>> found;
    const auto f = id3Frames(b);
    for (const auto& [id, data] : f.frames) {
        if ((id != "APIC" && id != "PIC") || data.size() < 4) continue;
        const unsigned enc = u8(data, 0);
        std::size_t pos = 1;
        if (id == "PIC") pos += 3;
        else pos = data.find('\0', 1) + 1; // type MIME
        if (pos == 0 || pos >= data.size()) continue;
        const unsigned type = u8(data, pos++);
        // Description, terminée par 0 (ou 00 00 aligné en UTF-16).
        if (enc == 1 || enc == 2) {
            while (pos + 1 < data.size() && !(data[pos] == '\0' && data[pos + 1] == '\0')) pos += 2;
            pos += 2;
        } else {
            pos = data.find('\0', pos);
            if (pos == std::string::npos) continue;
            ++pos;
        }
        if (pos < data.size()) found.emplace_back(type, data.substr(pos));
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& c) { return (a.first == 3) > (c.first == 3); });
    std::vector<std::string> out;
    for (auto& [t, bytes] : found) out.push_back(std::move(bytes));
    return out;
}

/// Le chunk « id3 » d'un WAV (ou vide).
std::string wavId3Chunk(const std::string& b) {
    if (b.size() < 12 || b.compare(0, 4, "RIFF") != 0 || b.compare(8, 4, "WAVE") != 0) return {};
    for (std::size_t pos = 12; pos + 8 <= b.size();) {
        const std::string id = b.substr(pos, 4);
        const std::size_t size = le32(b, pos + 4);
        if (id == "id3 " || id == "ID3 ") return b.substr(pos + 8, size);
        pos += 8 + size + (size & 1);
    }
    return {};
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extensionOf(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    return lower(e);
}

/// Un morceau entre parenthèses ou crochets qui n'est pas le titre (clip, prod…).
bool noiseInside(const std::string& inside) {
    const std::string l = lower(inside);
    for (const char* w : {"clip", "official", "officiel", "video", "vidéo", "audio", "lyric", "parole", "visuali", "remaster", "prod", "hq", "hd",
                          "4k", "free", "type beat", "explicit", "music video"})
        if (l.find(w) != std::string::npos) return true;
    return false;
}

std::string withoutNoise(std::string s, bool dropFeat) {
    for (std::size_t i = 0; i < s.size();) {
        const char open = s[i];
        if (open != '(' && open != '[') {
            ++i;
            continue;
        }
        const char close = open == '(' ? ')' : ']';
        const std::size_t end = s.find(close, i);
        if (end == std::string::npos) break;
        const std::string inside = s.substr(i + 1, end - i - 1);
        const std::string l = lower(inside);
        const bool feat = l.rfind("feat", 0) == 0 || l.rfind("ft.", 0) == 0 || l.rfind("with ", 0) == 0;
        if (noiseInside(inside) || (dropFeat && feat)) s.erase(i, end - i + 1);
        else i = end + 1;
    }
    return trim(s);
}

bool isTempoOrJunk(const std::string& part) {
    const std::string l = lower(part);
    if (l.find("bpm") != std::string::npos) return true;
    if (l.rfind("prod", 0) == 0) return true;
    return !l.empty() && std::all_of(l.begin(), l.end(), [](unsigned char c) { return std::isdigit(c) || c == ' ' || c == '.'; });
}

std::string query(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '"' || c == '\\'; }), s.end());
    return s;
}

} // namespace

FileTags readId3Tags(const std::string& b) {
    FileTags t;
    const auto f = id3Frames(b);
    for (const auto& [id, data] : f.frames) {
        if ((id == "TIT2" || id == "TT2") && t.title.empty()) t.title = id3Text(data);
        else if ((id == "TPE1" || id == "TP1") && t.artist.empty()) t.artist = id3Text(data);
        else if ((id == "TSRC" || id == "TRC") && t.isrc.empty()) t.isrc = id3Text(data);
    }
    // ID3v1 (128 derniers octets) si rien d'autre.
    if (t.title.empty() && t.artist.empty() && b.size() >= 128 && b.compare(b.size() - 128, 3, "TAG") == 0) {
        t.title = trim(latin1(b.substr(b.size() - 125, 30)));
        t.artist = trim(latin1(b.substr(b.size() - 95, 30)));
    }
    return t;
}

FileTags readFlacTags(const std::string& b) {
    FileTags t;
    if (b.size() < 8 || b.compare(0, 4, "fLaC") != 0) return t;
    for (std::size_t pos = 4; pos + 4 <= b.size();) {
        const unsigned head = u8(b, pos);
        const std::size_t size = be24(b, pos + 1);
        const std::size_t start = pos + 4;
        if ((head & 0x7F) == 4 && start + size <= b.size()) { // VORBIS_COMMENT (petit-boutiste)
            std::size_t p = start + 4 + le32(b, start);
            const std::uint32_t count = le32(b, p);
            p += 4;
            for (std::uint32_t k = 0; k < count && p + 4 <= start + size; ++k) {
                const std::size_t len = le32(b, p);
                const std::string c = b.substr(p + 4, len);
                p += 4 + len;
                const auto eq = c.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = lower(c.substr(0, eq)), value = trim(c.substr(eq + 1));
                if (key == "title" && t.title.empty()) t.title = value;
                else if (key == "artist" && t.artist.empty()) t.artist = value;
                else if (key == "isrc" && t.isrc.empty()) t.isrc = value;
            }
        }
        if (head & 0x80) break; // dernier bloc
        pos = start + size;
    }
    return t;
}

FileTags readWavTags(const std::string& b) {
    FileTags t;
    if (b.size() < 12 || b.compare(0, 4, "RIFF") != 0 || b.compare(8, 4, "WAVE") != 0) return t;
    for (std::size_t pos = 12; pos + 8 <= b.size();) {
        const std::string id = b.substr(pos, 4);
        const std::size_t size = le32(b, pos + 4);
        if (id == "LIST" && b.compare(pos + 8, 4, "INFO") == 0) {
            for (std::size_t q = pos + 12; q + 8 <= std::min(b.size(), pos + 8 + size);) {
                const std::string sub = b.substr(q, 4);
                const std::size_t len = le32(b, q + 4);
                const std::string value = trim(b.substr(q + 8, len));
                if (sub == "INAM" && t.title.empty()) t.title = value;
                else if (sub == "IART" && t.artist.empty()) t.artist = value;
                q += 8 + len + (len & 1);
            }
        }
        pos += 8 + size + (size & 1);
    }
    if (t.title.empty() || t.artist.empty()) {
        const FileTags id3 = readId3Tags(wavId3Chunk(b));
        if (t.title.empty()) t.title = id3.title;
        if (t.artist.empty()) t.artist = id3.artist;
        if (t.isrc.empty()) t.isrc = id3.isrc;
    }
    return t;
}

FileTags readFileTags(const std::filesystem::path& path) {
    const std::string e = extensionOf(path);
    const std::string bytes = readAll(path);
    if (e == "flac") return readFlacTags(bytes);
    if (e == "wav") return readWavTags(bytes);
    return readId3Tags(bytes);
}

std::vector<std::string> readCoverImages(const std::filesystem::path& path) {
    const std::string e = extensionOf(path);
    if (e == "flac") {
        auto pictures = tools::readEmbeddedPictures(path);
        std::stable_sort(pictures.begin(), pictures.end(), [](const auto& a, const auto& b) { return (a.type == 3) > (b.type == 3); });
        std::vector<std::string> out;
        for (auto& p : pictures) out.push_back(std::move(p.bytes));
        return out;
    }
    const std::string bytes = readAll(path);
    return id3Pictures(e == "wav" ? wavId3Chunk(bytes) : bytes);
}

NameParts splitFileName(const std::string& stem) {
    std::string s = stem;
    for (std::size_t at; (at = s.find("_-_")) != std::string::npos;) s.replace(at, 3, " - ");
    std::replace(s.begin(), s.end(), '_', ' ');
    for (const char* dash : {" \xE2\x80\x93 ", " \xE2\x80\x94 "}) // – —
        for (std::size_t at; (at = s.find(dash)) != std::string::npos;) s.replace(at, std::char_traits<char>::length(dash), " - ");
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const std::size_t at = s.find(" - ", start);
        parts.push_back(trim(s.substr(start, at == std::string::npos ? std::string::npos : at - start)));
        if (at == std::string::npos) break;
        start = at + 3;
    }
    std::vector<std::string> kept;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        std::string p = withoutNoise(parts[i], false);
        if (p.empty() || isTempoOrJunk(p)) continue; // « 01 », « 140bpm », « prod. X »
        // « 01. Titre » / « 01 Titre » en tête.
        if (kept.empty()) {
            std::size_t k = 0;
            while (k < p.size() && std::isdigit(static_cast<unsigned char>(p[k]))) ++k;
            if (k > 0 && k < p.size() && k <= 3 && (p[k] == '.' || p[k] == ' ')) p = trim(p.substr(k + 1));
        }
        if (!p.empty()) kept.push_back(p);
    }
    NameParts out;
    if (kept.size() >= 2) out.first = kept[0], out.second = kept[1];
    return out;
}

std::string bareTitle(const std::string& title) { return withoutNoise(title, true); }

std::string musicBrainzIsrcUrl(const std::string& isrc) {
    return "https://musicbrainz.org/ws/2/isrc/" + urlEncode(isrc) + "?fmt=json&inc=artist-credits";
}

std::string musicBrainzSearchUrl(const std::string& artist, const std::string& title) {
    return "https://musicbrainz.org/ws/2/recording/?fmt=json&limit=10&query=" +
           urlEncode("recording:\"" + query(bareTitle(title)) + "\" AND artist:\"" + query(artist) + "\"");
}

bool parseMusicBrainz(const std::string& json, const std::string& artist, const std::string& title, double fileSeconds, OnlineMatch& out) {
    out = OnlineMatch{};
    Json j;
    if (!parseJson(json, j)) return false;
    for (const auto& r : j["recordings"].array) {
        std::string credit;
        bool artistOk = false;
        for (const auto& c : r["artist-credit"].array) {
            credit += c.str("name") + c.str("joinphrase");
            if (ai::namesMatch(c.str("name"), artist)) artistOk = true;
        }
        if (!artistOk && !ai::namesMatch(credit, artist)) continue;
        if (!ai::namesMatch(bareTitle(r.str("title")), bareTitle(title))) continue;
        const double length = r.num("length") / 1000.0;
        // Un extrait plus court est accepté ; plus long que l'enregistrement : autre version.
        if (length > 0.0 && fileSeconds > 0.0 && fileSeconds > length + std::max(8.0, 0.06 * length)) continue;
        out.found = true;
        out.artist = credit;
        out.title = r.str("title");
        out.mbid = r.str("id");
        out.seconds = length;
        out.isrc = j.str("isrc");
        if (out.isrc.empty() && !r["isrcs"].array.empty()) out.isrc = r["isrcs"].array.front().string;
        return true;
    }
    return false;
}

Fetch onlineFetch() {
    return [](const std::string& url, std::string& body) {
        std::string error;
        return httpGet(url, body, error, 10);
    };
}

Identification identifyFile(const std::filesystem::path& path, double fileSeconds, const std::function<bool(const std::string&)>& knownArtist,
                            const Fetch* fetch) {
    Identification id;
    const FileTags tags = readFileTags(path);
    id.isrc = tags.isrc;
    bool sure = false; // ordre artiste / titre certain
    if (!tags.artist.empty() && !tags.title.empty()) {
        id.artist = tags.artist;
        id.title = tags.title;
        id.source = "étiquettes";
        sure = true;
    } else {
        const auto parts = splitFileName(path.stem().string());
        if (!parts.ok()) {
            id.problem = "Il me faut l'artiste et le titre : nomme le fichier « Artiste - Titre » (ou « Titre - Artiste »).";
            return id;
        }
        const bool firstKnown = knownArtist && knownArtist(parts.first), secondKnown = knownArtist && knownArtist(parts.second);
        if (secondKnown && !firstKnown) {
            id.artist = parts.second;
            id.title = parts.first;
            sure = true;
        } else {
            id.artist = parts.first;
            id.title = parts.second;
            sure = firstKnown && !secondKnown;
        }
        id.source = "nom du fichier";
    }
    if (!fetch || !*fetch) return id;
    id.asked = true;
    OnlineMatch m;
    std::string body;
    bool requested = false;
    const auto ask = [&](const std::string& url) {
        if (requested) std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // 1 requête / s (MusicBrainz)
        requested = true;
        return (*fetch)(url, body);
    };
    if (!id.isrc.empty() && ask(musicBrainzIsrcUrl(id.isrc)) && parseMusicBrainz(body, id.artist, id.title, fileSeconds, m)) id.confirmed = true;
    if (!id.confirmed && ask(musicBrainzSearchUrl(id.artist, id.title)) && parseMusicBrainz(body, id.artist, id.title, fileSeconds, m))
        id.confirmed = true;
    if (!id.confirmed && !sure && ask(musicBrainzSearchUrl(id.title, id.artist)) && parseMusicBrainz(body, id.title, id.artist, fileSeconds, m)) {
        std::swap(id.artist, id.title); // c'était « Titre - Artiste »
        id.confirmed = true;
    }
    if (id.confirmed && id.isrc.empty()) id.isrc = m.isrc;
    return id;
}

ImportResult importReferenceFile(const std::filesystem::path& path, const std::filesystem::path& userBase,
                                 const std::function<bool(const std::string&)>& knownArtist,
                                 const std::function<std::vector<std::string>(const std::string&)>& genresOf, const Fetch* fetch) {
    ImportResult r;
    tools::AudioFile audio;
    std::string error;
    if (!tools::readAudioFile(path, audio, error) || audio.numFrames() <= 0) {
        r.message = "Je n'arrive pas à lire ce fichier (WAV, MP3 ou FLAC).";
        return r;
    }
    r.id = identifyFile(path, audio.durationSeconds(), knownArtist, fetch);
    if (!r.id.problem.empty()) {
        r.message = r.id.problem;
        return r;
    }
    auto& e = r.entry;
    e.artist = r.id.artist;
    e.title = r.id.title;
    if (genresOf) e.genres = genresOf(e.artist);
    const auto& right = audio.channels[std::min<std::size_t>(1, audio.channels.size() - 1)];
    e.signature = analysis::SignatureAccumulator::analyzeParallel(audio.channels[0].data(), right.data(), audio.numFrames(), audio.sampleRate, 0);
    e.signature.music = analysis::analyzeMusic(audio.channels[0].data(), right.data(), audio.numFrames(), audio.sampleRate);
    e.measured = true;
    // La pochette du fichier -> avatar OMNIS dans le cache de la personne.
    const auto covers = readCoverImages(path);
    if (!covers.empty()) {
        gfx::OmnisAvatarStylizer::Options o;
        o.style = gfx::OmnisAvatarStylizer::Style::Emblem;
        o.shape = gfx::OmnisAvatarStylizer::Shape::Square;
        gfx::RgbaImage art;
        const auto target = gfx::avatars::coverPath(e.artist, e.title);
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (gfx::OmnisAvatarStylizer(o).stylize(reinterpret_cast<const std::uint8_t*>(covers.front().data()), covers.front().size(), art, &error) &&
            gfx::OmnisAvatarStylizer::savePng(target, art, &error)) {
            gfx::avatars::writeColors(target, gfx::OmnisAvatarStylizer::dominantColors(art));
            e.coverCredit = "Pochette du fichier (avatar OMNIS)";
        }
    }
    // Dans la base de la personne (écriture dans un fichier à côté, puis renommage).
    ai::ReferenceLibrary lib;
    lib.parse(readAll(userBase));
    lib.add(e);
    std::error_code ec;
    std::filesystem::create_directories(userBase.parent_path(), ec);
    auto temp = userBase;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << lib.serialize();
        if (!out) {
            r.message = "Impossible d'écrire ta base de références.";
            return r;
        }
    }
    std::filesystem::rename(temp, userBase, ec);
    if (ec) {
        r.message = "Impossible d'écrire ta base de références.";
        return r;
    }
    r.ok = true;
    const std::string how = r.id.confirmed ? "confirmé par MusicBrainz"
                            : r.id.asked   ? "d'après le " + std::string(r.id.source == "étiquettes" ? "fichier" : "nom du fichier") +
                                               ", MusicBrainz ne le connaît pas"
                                           : "d'après le " + std::string(r.id.source == "étiquettes" ? "fichier" : "nom du fichier");
    r.message = "Ajouté à ta base : " + e.artist + " - " + e.title + " (" + how + ").";
    return r;
}

} // namespace omnis::import
