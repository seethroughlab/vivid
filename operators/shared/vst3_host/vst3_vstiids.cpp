// vst3_vstiids.cpp — Out-of-line FUID definitions for VST3 component interfaces.
// Uses DEF_CLASS_IID to define exactly the IIDs not covered by coreiids.cpp.

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

namespace Steinberg {
namespace Vst {

DEF_CLASS_IID (IComponent)
DEF_CLASS_IID (IAudioProcessor)
DEF_CLASS_IID (IEditController)
DEF_CLASS_IID (IComponentHandler)
DEF_CLASS_IID (IComponentHandler2)
DEF_CLASS_IID (IHostApplication)
DEF_CLASS_IID (IConnectionPoint)
DEF_CLASS_IID (IPlugInterfaceSupport)
DEF_CLASS_IID (IEventList)
DEF_CLASS_IID (IParameterChanges)
DEF_CLASS_IID (IParamValueQueue)
DEF_CLASS_IID (IAttributeList)
DEF_CLASS_IID (IMessage)
DEF_CLASS_IID (IUnitInfo)

} // namespace Vst

DEF_CLASS_IID (IPlugView)
DEF_CLASS_IID (IPlugFrame)
DEF_CLASS_IID (IPlugViewContentScaleSupport)

} // namespace Steinberg
