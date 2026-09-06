#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace cotg::hyrax {

// Unique class IDs for the processor and controller components. Generated for
// this plugin; do not reuse elsewhere.
static const Steinberg::FUID kProcessorUID(0x0D33C29F, 0x689E1137, 0xB2F0D2C8, 0xD239E376);
static const Steinberg::FUID kControllerUID(0x887C96B5, 0x4F776785, 0xCFF05388, 0xAACC75D2);

#define kHyraxSubCategory "Fx|Dynamics"

} // namespace cotg::hyrax
