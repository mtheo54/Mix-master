# =============================================================================
#  Abrasion Labs // OMNIS
#  cmake/OmnisRemoveFolderIcon.cmake — Sous Windows, le SDK VST3 copie dans le
#  paquet une icône de dossier (PlugIn.ico : le logo VST de Steinberg) et son
#  desktop.ini. OMNIS ne livre aucune image ni le logo VST (une marque) : on les
#  retire juste après (étape POST_BUILD ajoutée après celle du SDK).
#
#  cmake -DPACKAGE=<OMNIS.vst3> -P OmnisRemoveFolderIcon.cmake
# =============================================================================
foreach(name IN ITEMS PlugIn.ico desktop.ini)
    set(file "${PACKAGE}/${name}")
    if(EXISTS "${file}")
        file(TO_NATIVE_PATH "${file}" native)
        # Le SDK les marque « système » : attribut retiré avant l'effacement.
        execute_process(COMMAND attrib -s -h -r "${native}" OUTPUT_QUIET ERROR_QUIET)
        file(REMOVE "${file}")
        message(STATUS "retiré du paquet : ${name}")
    endif()
endforeach()
if(EXISTS "${PACKAGE}")
    file(TO_NATIVE_PATH "${PACKAGE}" native)
    execute_process(COMMAND attrib -s "${native}" OUTPUT_QUIET ERROR_QUIET) # dossier ordinaire
endif()
