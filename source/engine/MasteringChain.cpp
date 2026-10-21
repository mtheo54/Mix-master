// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/MasteringChain.cpp
// =============================================================================
#include "MasteringChain.h"

#include "stages/ClipperStage.h"
#include "stages/LimiterStage.h"
#include "stages/SubControlStage.h"

#include <memory>

namespace omnis {

void buildMasteringChain(Engine& engine) {
    engine.appendStage(std::make_unique<SubControlStage>()); // 1. Sub Control & Mono Maker
    // 2. Match EQ            — module 4
    // 3. Excitation M/S      — module 3
    // 4. Multibande          — module 4
    engine.appendStage(std::make_unique<ClipperStage>());    // 5. Clipper
    engine.appendStage(std::make_unique<LimiterStage>());    // 6. Limiteur brickwall
    // 7. Dither              — avec l'export (module 3 bis)
}

} // namespace omnis
