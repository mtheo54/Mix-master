# =============================================================================
#  Abrasion Labs // OMNIS — Intégration du SDK VST3 de Steinberg.
#
#  Deux modes :
#   1. -DOMNIS_VST3_SDK_DIR=/chemin/vers/vst3sdk : SDK local (hors ligne).
#   2. Sinon : téléchargement automatique par FetchContent de la version
#      v3.8.0 (première version sous licence MIT -> compatible distribution
#      commerciale sans contrat Steinberg séparé). Seuls les sous-modules
#      nécessaires sont récupérés (pas de VSTGUI : OMNIS a sa propre UI NanoVG).
# =============================================================================

set(OMNIS_VST3_SDK_DIR "" CACHE PATH "Chemin d'un SDK VST3 local (vide = téléchargement automatique)")
set(OMNIS_VST3_SDK_TAG "v3.8.0_build_66" CACHE STRING "Tag Git du SDK VST3 à télécharger")

# Options du SDK : ni exemples, ni VSTGUI. Le validateur Steinberg est conservé
# et exécuté après chaque compilation du plug-in.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
# Interface : VSTGUI (livré avec le SDK) sous Windows et macOS ; sous Linux
# seulement sur demande (X11, cairo, pango, gtkmm…). Sans elle, l'éditeur
# générique de l'hôte.
if(WIN32 OR APPLE)
    set(OMNIS_GUI_DEFAULT ON)
else()
    set(OMNIS_GUI_DEFAULT OFF)
endif()
option(OMNIS_BUILD_GUI "Interface du plug-in (VSTGUI)" ${OMNIS_GUI_DEFAULT})
set(SMTG_ENABLE_VSTGUI_SUPPORT ${OMNIS_BUILD_GUI} CACHE BOOL "" FORCE)
# VSTGUI sous macOS : sources Objective-C++ (Cocoa).
if(OMNIS_BUILD_GUI AND APPLE)
    enable_language(OBJC OBJCXX)
endif()
set(SMTG_ENABLE_AUV2_BUILDS OFF CACHE BOOL "" FORCE)
# Pas de lien symbolique automatique dans ~/.vst3 (activable à la main).
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Lien symbolique du plug-in dans le dossier VST3 utilisateur")

if(OMNIS_VST3_SDK_DIR)
    get_filename_component(vst3sdk_SOURCE_DIR "${OMNIS_VST3_SDK_DIR}" ABSOLUTE)
    if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/pluginterfaces/vst/ivstaudioprocessor.h")
        message(FATAL_ERROR "OMNIS_VST3_SDK_DIR ne pointe pas vers un SDK VST3 complet : ${vst3sdk_SOURCE_DIR}")
    endif()
    add_subdirectory("${vst3sdk_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG        ${OMNIS_VST3_SDK_TAG}
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES base cmake pluginterfaces public.sdk vstgui4
        GIT_PROGRESS   TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(vst3sdk)
endif()

smtg_enable_vst3_sdk()
