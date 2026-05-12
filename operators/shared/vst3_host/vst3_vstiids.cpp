// vst3_vstiids.cpp — Out-of-line FUID definitions for VST3 component interfaces.
// Uses DEF_CLASS_IID to define exactly the IIDs not covered by coreiids.cpp.

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivsthostapplication.h"

namespace Steinberg {
namespace Vst {

DEF_CLASS_IID (IComponent)
DEF_CLASS_IID (IAudioProcessor)
DEF_CLASS_IID (IEditController)
DEF_CLASS_IID (IHostApplication)
DEF_CLASS_IID (IEventList)
DEF_CLASS_IID (IParameterChanges)
DEF_CLASS_IID (IParamValueQueue)

} // namespace Vst
} // namespace Steinberg
