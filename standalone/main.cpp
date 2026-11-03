// NAMku standalone — play NAMku without a DAW, on the Media Kit or on JACK.
//
// One window with NAMku's own editor, an Audio menu (Media Kit or JACK, and the
// input to play through) and a status line. The plug-in is the same NAMku.vst3
// a DAW loads; the model, IR, knobs, backend, input and window position are
// restored from the last run.
//
// usage: NAMku [path to NAMku.vst3]
//   With no path, NAMku.vst3 is looked for in $NAMKU_VST3, next to this
//   program, in the build tree, and in add-ons/media/VST3 of every install
//   root (the user's non-packaged one first).

#include "mainwindow.h"
#include "pluginhost.h"
#include "settings.h"

#include "version.h"

#include <Alert.h>
#include <Application.h>
#include <FindDirectory.h>
#include <OS.h>
#include <Path.h>
#include <PathFinder.h>
#include <StringList.h>
#include <image.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{

const char *kSignature = "application/x-vnd.rations-NAMku";

// The directory this program was started from, or "".
std::string AppDirectory()
{
    image_info info;
    int32 cookie = 0;
    while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
        if (info.type != B_APP_IMAGE)
            continue;
        std::string path(info.name);
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }
    return std::string();
}

std::string FindBundle()
{
    std::vector<std::string> candidates;
    if (const char *env = getenv("NAMKU_VST3"))
        candidates.emplace_back(env);
    const std::string dir = AppDirectory();
    if (!dir.empty()) {
        candidates.push_back(dir + "/NAMku.vst3");                    // beside the program
        candidates.push_back(dir + "/../../VST3/Release/NAMku.vst3"); // a build tree
    }
    BStringList paths;
    if (BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "media/VST3/NAMku.vst3",
                               B_FIND_PATH_EXISTING_ONLY, paths) == B_OK) {
        for (int32 i = 0; i < paths.CountStrings(); i++)
            candidates.emplace_back(paths.StringAt(i).String());
    }
    std::error_code ec;
    for (const auto &path : candidates)
        if (std::filesystem::is_directory(path, ec))
            return path;
    return std::string();
}

//------------------------------------------------------------------------
class NamkuApp : public BApplication
{
public:
    explicit NamkuApp(std::string bundle) : BApplication(kSignature), fBundle(std::move(bundle))
    {
    }

    void ReadyToRun() override
    {
        if (fBundle.empty())
            fBundle = FindBundle();
        std::string error;
        if (fBundle.empty()) {
            error = "NAMku.vst3 was not found. Install the namku package, or give the "
                    "path to NAMku.vst3 on the command line.";
        } else if (!fHost.load(fBundle, error)) {
            // error says why
        }
        if (!error.empty()) {
            fprintf(stderr, "NAMku: %s\n", error.c_str());
            BAlert *alert = new BAlert("NAMku", error.c_str(), "Quit", nullptr, nullptr,
                                       B_WIDTH_AS_USUAL, B_STOP_ALERT);
            alert->Go();
            PostMessage(B_QUIT_REQUESTED);
            return;
        }

        // Last run's model, IR and knobs, before audio starts.
        BMessage settings = NAMku::LoadSettings();
        std::vector<char> component, controller;
        const void *data = nullptr;
        ssize_t size = 0;
        if (settings.FindData("component", B_RAW_TYPE, &data, &size) == B_OK && size > 0)
            component.assign((const char *)data, (const char *)data + size);
        if (settings.FindData("controller", B_RAW_TYPE, &data, &size) == B_OK && size > 0)
            controller.assign((const char *)data, (const char *)data + size);
        fHost.setState(component, controller);

        // The window starts audio, and from Show() on it is the only thread
        // that touches the plug-in.
        fWindow = new NAMku::MainWindow(&fHost, settings);
        fWindow->Show();
    }

    void AboutRequested() override
    {
        BAlert *about = new BAlert(
            "About NAMku",
            "NAMku " VERSION_STR "\n\n"
            "Neural Amp Modeler player: .nam amp and pedal captures with a cabinet impulse "
            "response, on the Media Kit or JACK. The same NAMku as the VST3 plug-in.\n\n"
            "(C) 2026 rations, MIT. NAM DSP core and AudioDSPTools (C) Steven Atkinson, MIT. "
            "Eigen, MPL 2.0. VST is a trademark of Steinberg Media Technologies GmbH.",
            "OK");
        about->SetFlags(about->Flags() | B_CLOSE_ON_ESCAPE);
        about->Go(nullptr);
    }

private:
    std::string fBundle;
    NAMku::PluginHost fHost;
    NAMku::MainWindow *fWindow = nullptr;
};

} // namespace

int main(int argc, char **argv)
{
    NamkuApp app(argc > 1 ? std::string(argv[1]) : std::string());
    app.Run();
    return 0;
}
