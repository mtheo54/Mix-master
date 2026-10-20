// =============================================================================
//  Abrasion Labs // OMNIS
//  plugin/vst3/OmnisVersion.h — Identité et version du plug-in.
// =============================================================================
#pragma once

#include "pluginterfaces/base/fplatform.h"

#define OMNIS_VERSION_MAJOR 0
#define OMNIS_VERSION_MINOR 1
#define OMNIS_VERSION_PATCH 0
#define OMNIS_VERSION_BUILD 1

#define OMNIS_STRINGIFY_IMPL(x) #x
#define OMNIS_STRINGIFY(x) OMNIS_STRINGIFY_IMPL(x)

#define OMNIS_VERSION_STR                                                                                    \
    OMNIS_STRINGIFY(OMNIS_VERSION_MAJOR)                                                                     \
    "." OMNIS_STRINGIFY(OMNIS_VERSION_MINOR) "." OMNIS_STRINGIFY(OMNIS_VERSION_PATCH) "." OMNIS_STRINGIFY( \
        OMNIS_VERSION_BUILD)

#define OMNIS_VENDOR "Abrasion Labs"
#define OMNIS_VENDOR_URL ""
#define OMNIS_VENDOR_EMAIL ""
#define OMNIS_PLUGIN_NAME "OMNIS"
#define OMNIS_COPYRIGHT "(c) 2026 Abrasion Labs"
