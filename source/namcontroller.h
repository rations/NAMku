// NAMku edit controller — parameters plus the INamFileLoader interface that
// lets GUI-less hosts load .nam models and .wav impulse responses.

#pragma once

#include "inamfileloader.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <string>

namespace NAMku {

//------------------------------------------------------------------------
class NamController : public Steinberg::Vst::EditController, public INamFileLoader
{
public:
    static Steinberg::FUnknown *createInstance(void *)
    {
        return (Steinberg::Vst::IEditController *)new NamController();
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream *state) SMTG_OVERRIDE;

    // INamFileLoader
    Steinberg::tresult PLUGIN_API setModelFile(const Steinberg::char8 *path) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setIrFile(const Steinberg::char8 *path) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getModelFile(Steinberg::char8 *buffer,
                                               Steinberg::int32 bufferSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getIrFile(Steinberg::char8 *buffer,
                                            Steinberg::int32 bufferSize) SMTG_OVERRIDE;

    //---Interface---------
    OBJ_METHODS(NamController, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(INamFileLoader)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)

private:
    Steinberg::tresult sendPath(const char *messageID, const Steinberg::char8 *path);
    static Steinberg::tresult copyPath(const std::string &src, Steinberg::char8 *buffer,
                                       Steinberg::int32 bufferSize);

    std::string mModelPath;
    std::string mIrPath;
};

} // namespace NAMku
