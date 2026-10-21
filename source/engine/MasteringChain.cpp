// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/MasteringChain.cpp
// =============================================================================
#include "MasteringChain.h"

#include "stages/SubControlStage.h"

#include <memory>

namespace omnis {

void buildMasteringChain(Engine& engine) {
    engine.appendStage(std::make_unique<SubControlStage>()); // 1. Sub Control & Mono Maker
    // 2. Match EQ            — module 4
    // 3. Excitation M/S      — module 3
    // 4. Multibande          — module 3/4
    // 5. Clipper             — module 3
    // 6. Limiteur            — module 3
    // 7. Dither              — module 3
}

} // namespace omnis
