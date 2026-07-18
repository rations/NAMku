// NAMku native Haiku editor (IPlugView / kPlatformTypeHaikuBView).
//
// NamEditorView is the IPlugView the controller returns from createView(); the
// BView it creates (NamPanelView, defined in namview.cpp) lives on the host
// window's looper thread. The controller pushes value changes in through
// ParamChanged() (same looper thread — see NamController::setParamNormalized);
// user edits go out through EditController::beginEdit/performEdit/endEdit,
// which reach the host's IComponentHandler.

#pragma once

#include "haikuplugview.h"

namespace NAMku
{

class NamController;
class NamPanelView;

//------------------------------------------------------------------------
class NamEditorView : public Steinberg::HaikuPlugView
{
public:
    explicit NamEditorView(NamController *controller);

    // Called by the controller on the host window's looper thread whenever a
    // parameter value changes (automation, generic UI, state load, metering).
    void ParamChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

    // Called by the controller when the processor reports model capabilities,
    // so the editor can enable/disable the model-gated controls.
    void ModelCapsChanged(bool slimmable, bool hasInputLevel, bool hasOutputLevel);

protected:
    BView *createHaikuView(BRect frame) SMTG_OVERRIDE;
    void removedFromParent() SMTG_OVERRIDE;

private:
    NamPanelView *mPanel = nullptr; // owned by HaikuPlugView (fView), cached here
};

} // namespace NAMku
