// =============================================================================
//  Abrasion Labs // OMNIS
//  link/SharedRegion.h — Mémoire partagée nommée entre instances du plug-in.
//
//  Les instances d'OMNIS d'un même projet doivent se parler même quand l'hôte
//  les charge dans des processus séparés (Bitwig, FL Studio en « pont ») :
//  une simple variable globale ne suffit pas. Une région nommée, de taille
//  fixe, remplie de zéros à la création :
//      Windows : CreateFileMapping (espace « Local\ », session de l'utilisateur)
//      macOS / Linux : shm_open + mmap
//  Non temps réel : ouverture et fermeture hors du thread audio.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>

namespace omnis::link {

class SharedRegion {
public:
    SharedRegion() = default;
    ~SharedRegion() { close(); }
    SharedRegion(const SharedRegion&) = delete;
    SharedRegion& operator=(const SharedRegion&) = delete;

    /// Ouvre (ou crée, remplie de zéros) la région `name` de `size` octets.
    /// `name` : lettres, chiffres, points ; 30 caractères au plus (macOS).
    bool open(const std::string& name, std::size_t size);
    void close() noexcept;

    [[nodiscard]] void* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool isOpen() const noexcept { return data_ != nullptr; }

    /// Supprime le nom (POSIX : la région survit sinon au dernier processus ;
    /// Windows : rien à faire, elle disparaît avec la dernière vue). Tests.
    static void unlink(const std::string& name) noexcept;

private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
#if defined(_WIN32)
    void* mapping_ = nullptr;
#endif
};

} // namespace omnis::link
