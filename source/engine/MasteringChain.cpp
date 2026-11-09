// =============================================================================
//  Abrasion Labs // OMNIS
//  engine/MasteringChain.cpp
// =============================================================================
#include "MasteringChain.h"

#include "stages/ClipperStage.h"
#include "stages/ImagerStage.h"
#include "stages/InflatorStage.h"
#include "stages/CompressorStage.h"
#include "stages/LimiterStage.h"
#include "stages/MatchEqStage.h"
#include "stages/MultibandStage.h"
#include "stages/SubControlStage.h"

#include <memory>

namespace omnis {

void buildMasteringChain(Engine& engine) {
    // Même ordre que params::Module ; l'ordre de traitement par défaut (le rang 0 de ChainOrder,
    // params::kDefaultChain) place le compresseur après le Match EQ.
    using params::ParamId;
    engine.appendStage(std::make_unique<SubControlStage>(), ParamId::SubOn);  // 1. Sub Control & Mono Maker
    engine.appendStage(std::make_unique<MatchEqStage>(), ParamId::MatchOn);   // 2. Match EQ (intention tonale)
    engine.appendStage(std::make_unique<InflatorStage>(), ParamId::InflOn);   // 3. Inflator (densité, chaleur)
    engine.appendStage(std::make_unique<ImagerStage>(), ParamId::ImgOn);      // 4. Imager (largeur multibande)
    engine.appendStage(std::make_unique<MultibandStage>(), ParamId::MbOn);    // 5. Multibande (tenue de la 808)
    engine.appendStage(std::make_unique<ClipperStage>(), ParamId::ClipOn);    // 6. Clipper
    engine.appendStage(std::make_unique<LimiterStage>(), ParamId::LimOn);     // 7. Limiteur brickwall
    engine.appendStage(std::make_unique<CompressorStage>(), ParamId::CompOn); // 8. Compresseur de bus (colle)
    engine.setParameterImmediate(ParamId::ChainOrder, 0.0);
    // 8. Dither              — avec l'export (module 3 bis)
}

} // namespace omnis
