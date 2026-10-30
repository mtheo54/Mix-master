# =============================================================================
#  Abrasion Labs // OMNIS
#  cmake/OmnisCheckNoImages.cmake — Garde-fou (ctest « omnis_no_images ») :
#  aucune image ne part avec le plug-in.
#
#    - le paquet .vst3 (tous ses fichiers, binaire compris) : aucun fichier
#      image, aucune signature PNG / JPEG / GIF / WebP dans les octets ;
#    - les sources livrées (data/, source/) : aucun fichier image.
#
#  Les avatars et pochettes vivent seulement dans le cache de l'utilisateur
#  (gfx/AvatarCache.h), écrits sur son poste par omnis_refdb.
#
#  cmake -DPACKAGE=<OMNIS.vst3> -DSOURCE=<racine du dépôt> -P OmnisCheckNoImages.cmake
# =============================================================================
set(image_ext "png|jpe?g|gif|bmp|webp|tiff?|ico|icns|heic|avif|svg")
set(failures "")

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
        foreach(sig IN ITEMS "89504e470d0a1a0a" "ffd8ffe0" "ffd8ffe1" "ffd8ffdb" "474946383961" "474946383761")
            string(FIND "${hex}" "${sig}" at)
            # Une signature compte si elle commence sur un octet (position paire).
            while(at GREATER_EQUAL 0)
                math(EXPR odd "${at} % 2")
                if(odd EQUAL 0)
                    list(APPEND failures "signature d'image (${sig}) dans ${f}")
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
