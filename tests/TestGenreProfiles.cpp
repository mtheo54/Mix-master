// =============================================================================
//  Abrasion Labs // OMNIS
//  tests/TestGenreProfiles.cpp — Les profils de genre livrés : chaque TITRE dans
//  le sous-genre le plus proche par la mesure, au moins 3 titres par profil,
//  aucun nom de titre ni aucune fiche de titre dans le fichier livré.
// =============================================================================
#include "TestFramework.h"

#include "ai/GenreProfiles.h"
#include "ai/ReferenceLibrary.h"

#include <string>
#include <vector>

using namespace omnis;
using namespace omnis::ai;

namespace {

/// Trois sons types, chacun un peu varié : « rage » (fort, tassé, brillant),
/// « plugg » (doux, aéré, sombre), « new wave » (entre les deux, large).
ReferenceEntry track(const std::string& artist, int n, char kind, std::vector<std::string> genres = {"rap fr"}) {
    ReferenceEntry e;
    e.artist = artist;
    e.title = "TITRE_SECRET_" + artist + "_" + std::to_string(n);
    e.genres = std::move(genres);
    auto& s = e.signature;
    const double v = 0.15 * n;
    for (int k = 0; k < params::kMatchBands; ++k) {
        const double x = k / 20.0;
        const double tilt = kind == 'r' ? 6.0 * x : kind == 'p' ? -6.0 * x : 0.0;
        s.midDb[static_cast<std::size_t>(k)] = 60.0 + tilt + v;
        s.sideDb[static_cast<std::size_t>(k)] = s.midDb[static_cast<std::size_t>(k)] - (kind == 'n' ? 6.0 : 14.0);
        s.midSpreadDb[static_cast<std::size_t>(k)] = kind == 'r' ? 6.0 : kind == 'p' ? 14.0 : 10.0;
    }
    s.integratedLufs = kind == 'r' ? -6.0 : kind == 'p' ? -12.0 : -9.0;
    s.plrDb = kind == 'r' ? 6.0 : kind == 'p' ? 11.0 : 9.0;
    s.truePeakDb = s.integratedLufs + s.plrDb;
    s.loudnessRangeLu = kind == 'p' ? 8.0 : 5.0;
    s.subEnergyDb = 70.0;
    s.activeFrames = 100;
    s.music.valid = true;
    s.music.seconds = 180.0;
    s.music.bpm = 140.0 + n;
    s.music.microDb = kind == 'r' ? std::array<double, 4>{3, 3, 3, 4} : std::array<double, 4>{6, 7, 7, 8};
    s.music.centroidHz = kind == 'r' ? 5000 : kind == 'p' ? 2500 : 3800;
    s.music.onsetsPerSecond = kind == 'r' ? 8 : 4;
    return e;
}

} // namespace

OMNIS_TEST("profils de genre: chaque titre dans le sous-genre le plus proche, 3 titres au moins, aucun titre livré") {
    std::vector<ReferenceEntry> base;
    for (int n = 0; n < 3; ++n) base.push_back(track("Jolagreen23", n, 'r')); // new wave fr, rage fr
    for (int n = 0; n < 2; ++n) base.push_back(track("Khali", n, 'p'));       // new wave fr, plugg fr
    for (int n = 0; n < 2; ++n) base.push_back(track("HOUDI", n, 'p'));       // new wave fr, plugg fr
    for (const char* a : {"Leema", "Zeu", "Base 221"}) base.push_back(track(a, 0, 'n')); // new wave fr seul
    // La Fève (new wave fr, plugg fr, rage fr) : 3 sons rage et 3 sons plugg.
    for (int n = 0; n < 3; ++n) base.push_back(track("La Fève", n, 'r'));
    for (int n = 3; n < 6; ++n) base.push_back(track("La Fève", n, 'p'));
    // Hyperpop : 2 titres seulement -> ni profil de famille ni de sous-genre.
    for (int n = 0; n < 2; ++n) base.push_back(track("Sely", n, 'r', {"hyperpop"}));
    std::vector<const ReferenceEntry*> entries;
    for (const auto& e : base) entries.push_back(&e);

    const auto set = buildGenreProfiles(entries, 3);
    for (const auto& p : set.profiles) std::printf("      %-45s %d titres\n", p.title.c_str(), p.profileTitles);

    // La Fève : ses titres rage vont en rage fr, ses titres plugg en plugg fr.
    int rage = 0, plugg = 0;
    for (const auto& a : set.assignments)
        if (a.artist == "La Fève") {
            const bool isRage = a.title.back() <= '2';
            if (isRage && a.style == "rage fr") ++rage;
            if (!isRage && a.style == "plugg fr") ++plugg;
        }
    OMNIS_CHECK(rage == 3);
    OMNIS_CHECK(plugg == 3);

    const auto find = [&](const std::string& title) -> const ReferenceEntry* {
        for (const auto& p : set.profiles)
            if (p.title == title) return &p;
        return nullptr;
    };
    const auto* family = find("Rap Français & New Wave");
    OMNIS_CHECK(family != nullptr && family->profileTitles == 16);
    const auto* rageFr = find("Rap Français & New Wave › rage fr");
    OMNIS_CHECK(rageFr != nullptr && rageFr->profileTitles == 6);
    OMNIS_CHECK(find("Rap Français & New Wave › plugg fr") != nullptr);
    OMNIS_CHECK(find("Hyperpop & Expérimental") == nullptr); // 2 titres < 3
    for (const auto& p : set.profiles) {
        OMNIS_CHECK(p.isProfile() && p.artist == kProfileArtist && p.profileTitles >= 3);
        OMNIS_CHECK(p.signature.music.bpm == 0.0 && p.signature.music.keyRoot < 0); // aucune trace d'un titre
    }

    // Le fichier livré : ni nom de titre, ni fiche de titre ; il se relit à l'identique.
    const std::string text = serializeGenreProfiles(set);
    OMNIS_CHECK(text.find("TITRE_SECRET") == std::string::npos);
    ReferenceLibrary back;
    OMNIS_CHECK(back.parse(text));
    int profiles = 0;
    for (const auto& e : back.entries()) {
        OMNIS_CHECK(!e.measured || e.isProfile());
        OMNIS_CHECK(e.title.empty() || e.isProfile()); // les artistes de nom n'ont pas de titre
        profiles += e.isProfile() ? 1 : 0;
    }
    OMNIS_CHECK(profiles == static_cast<int>(set.profiles.size()));
    // Les artistes sont connus de nom avec les genres de leurs titres.
    bool laFeve = false;
    for (const auto& a : set.artists)
        if (a.artist == "La Fève") {
            laFeve = !a.measured && std::find(a.genres.begin(), a.genres.end(), "rage fr") != a.genres.end() &&
                     std::find(a.genres.begin(), a.genres.end(), "plugg fr") != a.genres.end();
        }
    OMNIS_CHECK(laFeve);
}

OMNIS_TEST("profils de genre: le plug-in livré ne contient que des profils (aucune fiche de titre)") {
    const auto& lib = ReferenceLibrary::builtin();
    int profiles = 0;
    for (const auto& e : lib.entries()) {
        OMNIS_CHECK(!e.measured || (e.isProfile() && e.profileTitles >= 3));
        profiles += e.isProfile() ? 1 : 0;
    }
    std::printf("      %d profil(s) livrés\n", profiles);
    OMNIS_CHECK(profiles >= 7);
    OMNIS_CHECK(lib.measuredArtists().empty()); // « j'ai écouté : … » ne cite aucun artiste livré
}
