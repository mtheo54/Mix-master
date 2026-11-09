// =============================================================================
//  Abrasion Labs // OMNIS
//  stems/Composition.cpp
// =============================================================================
#include "Composition.h"

#include "core/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace omnis::stems {

Composition buildComposition(const std::vector<link::InstanceView>& instances) {
    Composition out;
    for (const auto& v : instances) {
        BusView b;
        b.id = v.slot;
        b.name = v.name;
        b.content = v.content;
        // Le groupe : le nom du bus (« DRUMS », « MELO », « EFX »…), sinon ce qu'il contient.
        b.family = familyFromName(b.name);
        if (b.family == Family::Unknown) b.family = b.content.mainFamily();
        out.buses.push_back(std::move(b));
    }
    const auto rank = [](Family f) { return f == Family::Unknown ? kFamilyCount : static_cast<int>(f); };
    std::stable_sort(out.buses.begin(), out.buses.end(), [&](const BusView& a, const BusView& b) {
        return rank(a.family) != rank(b.family) ? rank(a.family) < rank(b.family) : a.name < b.name;
    });
    return out;
}

std::string describeListened(const BusView& b) {
    if (b.content.songSeconds < 1.0) return {};
    const auto mmss = [](double s) {
        char t[16];
        const int v = static_cast<int>(std::lround(s));
        std::snprintf(t, sizeof t, "%d:%02d", v / 60, v % 60);
        return std::string(t);
    };
    // Tout entendu jusqu'au point le plus loin : le morceau (ou ce qui en a été joué) en entier.
    if (b.content.songSeconds + 1.5 >= b.content.songEnd) return i18n::tr("morceau écouté : ") + mmss(b.content.songSeconds);
    return i18n::tr("morceau écouté : ") + mmss(b.content.songSeconds) + i18n::tr(" sur ") + mmss(b.content.songEnd) + i18n::tr(" (des passages manquent)");
}

std::string describeComposition(const Composition& c) {
    std::string s;
    for (const auto& b : c.buses) {
        if (!s.empty()) s += "\n";
        s += i18n::tr("BUS « ") + (b.name.empty() ? i18n::tr("sans nom") : b.name) + " » (" + i18n::tr(familyLabel(b.family)) + ")";
        if (b.listening()) {
            s += i18n::tr(" : lance la lecture, j'écoute ce qu'il y a dedans.");
        } else if (b.content.items.empty()) {
            s += i18n::tr(" : rien de net entendu pour l'instant.");
        } else {
            s += " :";
            for (std::size_t i = 0; i < b.content.items.size(); ++i) s += (i ? ", " : " ") + describeItem(b.content.items[i]);
            if (const std::string l = describeListened(b); !l.empty()) s += " (" + l + ")";
        }
    }
    return s;
}

} // namespace omnis::stems
