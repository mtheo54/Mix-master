# =============================================================================
#  Abrasion Labs // OMNIS
#  cmake/OmnisCheckNoImages.cmake — Garde-fou (ctest « omnis_no_images ») :
#  aucune image ne part avec le plug-in.
#
#    - le paquet .vst3 (tous ses fichiers, binaire compris) : aucun fichier
#      image, aucune image embarquée dans les octets : PNG (signature suivie de
#      son bloc IHDR), JPEG (SOI puis une suite de segments valides jusqu'aux
#      données : tables DQT, en-tête SOF, SOS), GIF ;
#      les constantes seules d'un lecteur / encodeur (signature PNG écrite par
#      l'encodeur des avatars, en-tête JFIF du codeur stb) ne sont pas des images ;
#    - les sources livrées (data/, source/) : aucun fichier image.
#
#  Les avatars et pochettes vivent seulement dans le cache de l'utilisateur
#  (gfx/AvatarCache.h), écrits sur son poste par omnis_refdb.
#
#  cmake -DPACKAGE=<OMNIS.vst3> -DSOURCE=<racine du dépôt> -P OmnisCheckNoImages.cmake
# =============================================================================
set(image_ext "png|jpe?g|gif|bmp|webp|tiff?|ico|icns|heic|avif|svg")
set(failures "")

# Un JPEG réel à la position `at` (en caractères hexadécimaux) : SOI, puis des
# segments (FF xx + longueur) qui s'enchaînent jusqu'à SOS, avec au moins une
# table de quantification (DB) et un en-tête d'image (C0 … C3).
function(omnis_real_jpeg hex at out)
    set(${out} FALSE PARENT_SCOPE)
    math(EXPR pos "${at} + 4")
    set(dqt FALSE)
    set(sof FALSE)
    foreach(step RANGE 24)
        string(SUBSTRING "${hex}" ${pos} 8 seg)
        string(LENGTH "${seg}" len)
        if(len LESS 8)
            return()
        endif()
        string(SUBSTRING "${seg}" 0 2 ff)
        string(SUBSTRING "${seg}" 2 2 marker)
        if(NOT ff STREQUAL "ff")
            return()
        endif()
        if(marker STREQUAL "da")
            if(dqt AND sof)
                set(${out} TRUE PARENT_SCOPE)
            endif()
            return()
        endif()
        if(marker STREQUAL "db")
            set(dqt TRUE)
        elseif(marker MATCHES "^c[0-3]$")
            set(sof TRUE)
        elseif(NOT marker MATCHES "^(e[0-9a-f]|c4|dd|fe)$")
            return()
        endif()
        string(SUBSTRING "${seg}" 4 4 lenhex)
        math(EXPR seglen "0x${lenhex}")
        if(seglen LESS 2)
            return()
        endif()
        math(EXPR pos "${pos} + 4 + 2 * ${seglen}")
    endforeach()
endfunction()

foreach(root IN ITEMS "${PACKAGE}")
    if(NOT EXISTS "${root}")
        message(FATAL_ERROR "paquet introuvable : ${root}")
    endif()
    file(GLOB_RECURSE files LIST_DIRECTORIES false "${root}/*")
    foreach(f IN LISTS files)
        string(TOLOWER "${f}" lower)
        if(lower MATCHES "\\.(${image_ext})$")
            list(APPEND failures "image dans le paquet : ${f}")
            continue()
        endif()
        # Signatures dans les octets (une image embarquée dans le binaire).
        file(READ "${f}" hex HEX)
        foreach(sig IN ITEMS "89504e470d0a1a0a0000000d49484452" "ffd8ff" "474946383961" "474946383761")
            string(FIND "${hex}" "${sig}" at)
            # Une signature compte si elle commence sur un octet (position paire).
            while(at GREATER_EQUAL 0)
                math(EXPR odd "${at} % 2")
                set(real TRUE)
                if(sig STREQUAL "ffd8ff")
                    omnis_real_jpeg("${hex}" ${at} real)
                endif()
                if(odd EQUAL 0 AND real)
                    list(APPEND failures "image embarquée (${sig}) dans ${f}")
                    break()
                endif()
                math(EXPR next "${at} + 1")
                string(SUBSTRING "${hex}" ${next} -1 rest)
                string(FIND "${rest}" "${sig}" found)
                if(found LESS 0)
                    set(at -1)
                else()
                    math(EXPR at "${next} + ${found}")
                endif()
            endwhile()
        endforeach()
        string(LENGTH "${hex}" n)
        math(EXPR kb "${n} / 2048")
        message(STATUS "vérifié : ${f} (${kb} Ko)")
    endforeach()
endforeach()

foreach(dir IN ITEMS data source)
    file(GLOB_RECURSE files LIST_DIRECTORIES false "${SOURCE}/${dir}/*")
    foreach(f IN LISTS files)
        string(TOLOWER "${f}" lower)
        if(lower MATCHES "\\.(${image_ext})$")
            list(APPEND failures "image dans les sources livrées : ${f}")
        endif()
    endforeach()
endforeach()

if(failures)
    list(JOIN failures "\n  " text)
    message(FATAL_ERROR "OMNIS ne doit livrer aucune image :\n  ${text}")
endif()
message(STATUS "aucune image dans le paquet ni dans les sources livrées")
