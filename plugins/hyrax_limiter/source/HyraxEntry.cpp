#include "HyraxController.h"
#include "HyraxIds.h"
#include "HyraxProcessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "Hyrax Limiter"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace cotg::hyrax;

BEGIN_FACTORY_DEF("Code of the Geeks",
                  "https://github.com/jasper046/vst_plugins",
                  "mailto:jasper.claude@codeofthegeeks.com")

    DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               Vst::kDistributable,
               kHyraxSubCategory,
               FULL_VERSION_STR,
               kVstVersionString,
               HyraxProcessor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               stringPluginName "Controller",
               0,
               "",
               FULL_VERSION_STR,
               kVstVersionString,
               HyraxController::createInstance)

END_FACTORY
