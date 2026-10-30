# =============================================================================
#  Abrasion Labs // OMNIS — Mastering intelligent, VST3 natif sans framework.
# =============================================================================
cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Version minimale de macOS")
endif()

project(Omnis
    VERSION 0.1.0
    DESCRIPTION "Abrasion Labs // OMNIS — intelligent mixing & mastering processor"
    LANGUAGES C CXX
)

option(OMNIS_BUILD_VST3  "Compiler le plug-in VST3 (télécharge le SDK si besoin)" ON)
option(OMNIS_BUILD_TOOLS "Compiler les outils de banc de test (testgen, analyze, render)" ON)
option(OMNIS_BUILD_TESTS "Compiler les tests unitaires" ON)
option(OMNIS_NATIVE_ARCH "Optimiser pour le CPU de la machine de build (-march=native) — jamais pour une diffusion" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de build" FORCE)
endif()

# -----------------------------------------------------------------------------
#  Options de compilation communes au code OMNIS (pas au SDK)
# -----------------------------------------------------------------------------
add_library(omnis_compile_options INTERFACE)
if(MSVC)
    target_compile_options(omnis_compile_options INTERFACE /W4 /permissive- /utf-8 /fp:precise)
else()
    target_compile_options(omnis_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        # Pas de -ffast-math : il casse les tests NaN/inf et réordonne les
        # sommes (non-reproductibilité bit-à-bit des null-tests).
        -fno-math-errno
    )
    if(OMNIS_NATIVE_ARCH)
        target_compile_options(omnis_compile_options INTERFACE -march=native)
    endif()
endif()

# -----------------------------------------------------------------------------
#  omnis_core : moteur DSP + analyse + paramètres, sans aucune dépendance
# -----------------------------------------------------------------------------
include(cmake/OmnisEmbedData.cmake)
omnis_embed_data(${CMAKE_BINARY_DIR}/generated/EmbeddedData.cpp
    lexicon=${CMAKE_SOURCE_DIR}/data/lexique_fr.txt
    slang=${CMAKE_SOURCE_DIR}/data/argot.txt
    references=${CMAKE_SOURCE_DIR}/data/references.txt
    catalogue=${CMAKE_SOURCE_DIR}/data/catalogue.txt
    elements=${CMAKE_SOURCE_DIR}/data/elements.txt
)

add_library(omnis_core STATIC
    ${CMAKE_BINARY_DIR}/generated/EmbeddedData.cpp
    source/core/AudioBuffer.h
    source/core/Decibels.h
    source/core/DelayLine.h
    source/core/OnePoleSmoother.h
    source/core/Platform.h
    source/core/SpscRingBuffer.h
    source/core/TripleBuffer.h
    source/dsp/Crossover.h
    source/dsp/Fft.cpp
    source/dsp/FirKernelBank.h
    source/dsp/LinearPhaseDesigner.cpp
    source/dsp/LinearPhaseDesigner.h
    source/dsp/Halfband.cpp
    source/dsp/Halfband.h
    source/dsp/Oversampler.cpp
    source/dsp/Oversampler.h
    source/dsp/Waveshaper.h
    source/dsp/Fft.h
    source/dsp/StereoConvolver.cpp
    source/dsp/StereoConvolver.h
    source/dsp/SvfFilter.h
    source/ai/DynamicsIntent.cpp
    source/ai/DynamicsIntent.h
    source/ai/EmbeddedData.h
    source/ai/Feedback.cpp
    source/ai/Feedback.h
    source/ai/Lexicon.cpp
    source/ai/Lexicon.h
    source/ai/MatchIntent.cpp
    source/ai/ConsoleActions.cpp
    source/ai/MatchIntent.h
    source/ai/ReferenceLibrary.cpp
    source/ai/ReferenceLibrary.h
    source/ai/TextMatch.cpp
    source/ai/TextMatch.h
    source/analysis/BandLevels.cpp
    source/analysis/BandLevels.h
    source/analysis/KWeightingFilter.h
    source/analysis/LoudnessMeter.cpp
    source/analysis/LoudnessMeter.h
    source/analysis/SignalStatistics.h
    source/analysis/SpectralSignature.cpp
    source/analysis/MusicDescriptors.cpp
    source/analysis/SpectralSignature.h
    source/analysis/StereoMeter.h
    source/analysis/TruePeakMeter.cpp
    source/analysis/TruePeakMeter.h
    source/params/Parameters.cpp
    source/params/Parameters.h
    source/engine/Engine.cpp
    source/engine/Engine.h
    source/engine/MasteringChain.cpp
    source/engine/MasteringChain.h
    source/engine/ReferenceCapture.cpp
    source/engine/ProdListener.cpp
    source/engine/ReferenceCapture.h
    source/engine/Stage.h
    source/link/LinkBus.cpp
    source/link/LinkBus.h
    source/link/LinkClient.cpp
    source/link/LinkClient.h
    source/link/SharedRegion.cpp
    source/link/SharedRegion.h
    source/stems/Element.cpp
    source/stems/Element.h
    source/stems/ElementClassifier.cpp
    source/stems/ElementClassifier.h
    source/stems/Conductor.cpp
    source/stems/Conductor.h
    source/stems/Correction.h
    source/stems/StemChain.cpp
    source/stems/DrumBus.cpp
    source/stems/StemChain.h
    source/stems/StemSettings.cpp
    source/stems/StemSettings.h
    source/stems/StemTimeline.cpp
    source/stems/StemTimeline.h
    source/stages/ClipperStage.cpp
    source/stages/ClipperStage.h
    source/stages/ImagerStage.cpp
    source/stages/ImagerStage.h
    source/stages/InflatorStage.cpp
    source/stages/InflatorStage.h
    source/stages/LimiterStage.cpp
    source/stages/LimiterStage.h
    source/stages/MatchEqStage.cpp
    source/stages/MatchEqStage.h
    source/stages/MatchKernelBank.cpp
    source/stages/MatchKernelBank.h
    source/stages/MultibandStage.cpp
    source/stages/MultibandStage.h
    source/stages/SubControlStage.cpp
    source/stages/SubKernelBank.cpp
    source/stages/SubKernelBank.h
    source/stages/SubControlStage.h
)
target_include_directories(omnis_core PUBLIC source)
target_compile_features(omnis_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(omnis_core PUBLIC Threads::Threads PRIVATE omnis_compile_options)
if(UNIX AND NOT APPLE)
    # shm_open (link/SharedRegion) : dans librt avant glibc 2.34.
    target_link_libraries(omnis_core PUBLIC rt)
endif()

# -----------------------------------------------------------------------------
#  Plug-in VST3
# -----------------------------------------------------------------------------
if(OMNIS_BUILD_VST3)
    include(cmake/OmnisVst3Sdk.cmake)

    smtg_add_vst3plugin(OMNIS
        source/plugin/vst3/OmnisController.cpp
        source/plugin/vst3/OmnisController.h
        source/plugin/vst3/OmnisFactory.cpp
        source/plugin/vst3/OmnisIds.h
        source/plugin/vst3/OmnisProcessor.cpp
        source/plugin/vst3/UiFeed.cpp
        source/plugin/vst3/OmnisProcessor.h
        source/plugin/vst3/OmnisState.cpp
        source/plugin/vst3/OmnisState.h
        source/plugin/vst3/OmnisVersion.h
    )
    target_include_directories(OMNIS PRIVATE source)
    target_compile_features(OMNIS PRIVATE cxx_std_20)
    # Pas d'omnis_compile_options ici : les en-têtes du SDK ne passent pas -Wshadow
    # / -Wnon-virtual-dtor. Le code DSP (omnis_core) reste compilé en strict.
    target_link_libraries(OMNIS PRIVATE omnis_core sdk)
    # windows.h (inclus par certains en-têtes système) définit min/max en macros.
    target_compile_definitions(OMNIS PRIVATE $<$<PLATFORM_ID:Windows>:NOMINMAX>)
    if(OMNIS_BUILD_GUI)
        target_sources(OMNIS PRIVATE source/plugin/vst3/OmnisEditor.cpp source/plugin/vst3/OmnisEditor.h
            source/plugin/vst3/gui/Widgets.cpp source/plugin/vst3/gui/Widgets.h source/plugin/vst3/gui/Look.h)
        # vstgui_support (vstguieditor.cpp) appelle pluginfactory.cpp de « sdk » :
        # « sdk » après lui sur la ligne de liens.
        target_link_libraries(OMNIS PRIVATE vstgui_support sdk)
        target_compile_definitions(OMNIS PRIVATE OMNIS_HAS_GUI=1)
        # Textes accentués de l'interface : sources en UTF-8.
        target_compile_options(OMNIS PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    endif()

    # Rendu de la console hors écran (PNG), pour la vérifier sans DAW (Linux).
    if(OMNIS_BUILD_GUI AND OMNIS_BUILD_TOOLS AND UNIX AND NOT APPLE)
        add_executable(omnis_guishot tools/guishot/main.cpp
            source/plugin/vst3/OmnisProcessor.cpp source/plugin/vst3/OmnisController.cpp source/plugin/vst3/OmnisEditor.cpp
            source/plugin/vst3/OmnisState.cpp source/plugin/vst3/UiFeed.cpp source/plugin/vst3/gui/Widgets.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/processdata.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp)
        target_include_directories(omnis_guishot PRIVATE source)
        target_compile_definitions(omnis_guishot PRIVATE OMNIS_HAS_GUI=1)
        target_link_libraries(omnis_guishot PRIVATE omnis_tools_common omnis_core vstgui_support sdk ${CMAKE_DL_LIBS})
    endif()

    if(SMTG_MAC)
        smtg_target_set_bundle(OMNIS
            BUNDLE_IDENTIFIER com.abrasionlabs.omnis
            COMPANY_NAME "Abrasion Labs"
        )
    endif()
endif()

# -----------------------------------------------------------------------------
#  Outils de banc de test
# -----------------------------------------------------------------------------
if(OMNIS_BUILD_TOOLS OR OMNIS_BUILD_TESTS)
    # Décodeurs MP3 / FLAC (domaine public, code tiers : compilé sans nos
    # avertissements stricts).
    add_library(omnis_dr_libs STATIC third_party/dr_libs/dr_libs_impl.cpp)
    target_include_directories(omnis_dr_libs PUBLIC third_party/dr_libs)
    if(NOT MSVC)
        target_compile_options(omnis_dr_libs PRIVATE -w)
    endif()

    add_library(omnis_tools_common STATIC
        tools/common/AiFiles.h
        tools/common/AnalysisReport.cpp
        tools/common/AudioRead.cpp
        tools/common/AudioRead.h
        tools/common/AnalysisReport.h
        tools/common/DistortionAnalysis.cpp
        tools/common/DistortionAnalysis.h
        tools/common/LoudnessTuner.cpp
        tools/common/LoudnessTuner.h
        tools/common/TonalBalance.cpp
        tools/common/TonalBalance.h
        tools/common/TestSignals.cpp
        tools/common/TestSignals.h
        tools/common/WavFile.cpp
        tools/common/WavFile.h
    )
    target_include_directories(omnis_tools_common PUBLIC tools/common)
    target_link_libraries(omnis_tools_common PUBLIC omnis_core PRIVATE omnis_dr_libs omnis_compile_options)
endif()

if(OMNIS_BUILD_TOOLS)
    foreach(tool testgen analyze render compare tune match refdb ask learn stems)
        add_executable(omnis_${tool} tools/${tool}/main.cpp)
        target_link_libraries(omnis_${tool} PRIVATE omnis_tools_common omnis_compile_options)
    endforeach()
endif()

# -----------------------------------------------------------------------------
#  Tests
# -----------------------------------------------------------------------------
if(OMNIS_BUILD_TESTS)
    enable_testing()
    add_executable(omnis_tests
        tests/TestFramework.h
        tests/TestMain.cpp
        tests/TestCore.cpp
        tests/TestFilters.cpp
        tests/TestConvolution.cpp
        tests/TestOversampling.cpp
        tests/TestDynamics.cpp
        tests/TestSubControl.cpp
        tests/TestMatchEq.cpp
        tests/TestMultiband.cpp
        tests/TestInflatorImager.cpp
        tests/TestMatchIntent.cpp
        tests/TestLanguage.cpp
        tests/TestLearning.cpp
        tests/TestReferenceCapture.cpp
        tests/TestLink.cpp
        tests/TestStems.cpp
        tests/TestStemChain.cpp
        tests/TestRack.cpp
        tests/TestDrumBus.cpp
        tests/TestMusic.cpp
        tests/TestConductor.cpp
        tests/TestLoudness.cpp
        tests/TestTruePeak.cpp
        tests/TestParameters.cpp
        tests/TestEngine.cpp
        tests/TestWav.cpp
    )
    target_link_libraries(omnis_tests PRIVATE omnis_tools_common omnis_compile_options)
    find_package(Threads REQUIRED)
    target_link_libraries(omnis_tests PRIVATE Threads::Threads)
    add_test(NAME omnis_unit_tests COMMAND omnis_tests)

    # État du processeur VST3 : aller-retour de tous les paramètres (le
    # validateur ne teste que les valeurs par défaut).
    if(OMNIS_BUILD_VST3)
        add_executable(omnis_plugin_tests
            tests/TestFramework.h
            tests/TestMain.cpp
            tests/TestPluginState.cpp
            tests/TestPluginLink.cpp
            tests/TestPluginSlotToggle.cpp
            tests/TestPluginAi.cpp
            source/plugin/vst3/OmnisController.cpp
            source/plugin/vst3/OmnisProcessor.cpp
            source/plugin/vst3/UiFeed.cpp
            source/plugin/vst3/OmnisState.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/processdata.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        )
        target_include_directories(omnis_plugin_tests PRIVATE source)
        target_link_libraries(omnis_plugin_tests PRIVATE omnis_tools_common omnis_core sdk)
        if(APPLE)
            # Le SDK (fstring, FUID) appelle CoreFoundation ; le plug-in le reçoit
            # par smtg_add_vst3plugin, ce programme de test non.
            target_link_libraries(omnis_plugin_tests PRIVATE "-framework CoreFoundation")
        endif()
        target_compile_definitions(omnis_plugin_tests PRIVATE $<$<PLATFORM_ID:Windows>:NOMINMAX>)
        add_test(NAME omnis_plugin_state COMMAND omnis_plugin_tests)
    endif()

    if(OMNIS_BUILD_VST3 AND TARGET validator)
        get_target_property(OMNIS_PLUGIN_PACKAGE OMNIS SMTG_PLUGIN_PACKAGE_PATH)
        add_test(NAME vst3_validator COMMAND validator "${OMNIS_PLUGIN_PACKAGE}")
    endif()
endif()
