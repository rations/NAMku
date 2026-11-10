// MainWindow implementation. See mainwindow.h for the threading contract.

#include "mainwindow.h"

#include "pluginhost.h"
#include "settings.h"

#include "pluginterfaces/base/funknown.h"

#include <Alert.h>
#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <String.h>
#include <StringView.h>
#include <View.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;

namespace NAMku
{

namespace
{

enum : uint32 {
    kMsgBackend = 'nkBk',   // "backend": "mediakit" | "jack"
    kMsgInput = 'nkIn',     // "source": capture source, "" for none
    kMsgMeters = 'nkMt',    // pump the meters into the controller
    kMsgStatus = 'nkSt',    // refresh the status line
    kMsgAudioInfo = 'nkAI', // show the audio status in an alert
    kMsgOpenPreset = 'nkPo', // show the open panel
    kMsgSavePreset = 'nkPs', // show the save panel
    kMsgPresetOpened = 'nkPO', // "refs": the preset to load
    kMsgPresetSaved = 'nkPS',  // "directory" + "name": where to save
};

// The preset file name extension (Kasen's effects window uses it too).
const char *kPresetExtension = ".kasenpreset";

// The editor's size if it cannot say (NAMku's is 600 x 400).
const float kDefaultWidth = 600;
const float kDefaultHeight = 400;

void Tell(const char *title, const std::string &text)
{
    BAlert *alert =
        new BAlert(title, text.c_str(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
    alert->Go(nullptr); // asynchronous: the window keeps running
}

} // namespace

//------------------------------------------------------------------------
MainWindow::MainWindow(PluginHost *host, const BMessage &settings)
    : BWindow(BRect(100, 100, 100 + kDefaultWidth - 1, 100 + kDefaultHeight - 1), "NAMku",
              B_TITLED_WINDOW,
              B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE),
      fHost(host), fSettings(settings)
{
    // Menus
    BMenuBar *bar = new BMenuBar(BRect(0, 0, kDefaultWidth - 1, 0), "menu");
    BMenu *app = new BMenu("NAMku");
    app->AddItem(new BMenuItem("About NAMku" B_UTF8_ELLIPSIS, new BMessage(B_ABOUT_REQUESTED)));
    app->AddSeparatorItem();
    app->AddItem(new BMenuItem("Open preset" B_UTF8_ELLIPSIS, new BMessage(kMsgOpenPreset), 'O'));
    app->AddItem(new BMenuItem("Save preset" B_UTF8_ELLIPSIS, new BMessage(kMsgSavePreset), 'S'));
    app->AddSeparatorItem();
    app->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
    app->SetTargetForItems(this);
    bar->AddItem(app);

    BMenu *audio = new BMenu("Audio");
    BMessage *mk = new BMessage(kMsgBackend);
    mk->AddString("backend", "mediakit");
    audio->AddItem(fMediaKitItem = new BMenuItem("Media Kit", mk));
    BMessage *jack = new BMessage(kMsgBackend);
    jack->AddString("backend", "jack");
    audio->AddItem(fJackItem = new BMenuItem("JACK", jack));
    audio->AddSeparatorItem();
    fInputMenu = new BMenu("Input");
    audio->AddItem(fInputMenu);
    audio->AddSeparatorItem();
    audio->AddItem(new BMenuItem("Audio status" B_UTF8_ELLIPSIS, new BMessage(kMsgAudioInfo)));
    bar->AddItem(audio);
    AddChild(bar);
    fMenuHeight = bar->Frame().Height() + 1;

    // The editor, below the menu bar
    fEditorParent =
        new BView(BRect(0, fMenuHeight, kDefaultWidth - 1, fMenuHeight + kDefaultHeight - 1),
                  "editor", B_FOLLOW_NONE, 0);
    fEditorParent->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    AddChild(fEditorParent);

    // The status line, below the editor (FitToEditor places and sizes it)
    fStatus = new BStringView(BRect(4, 2, kDefaultWidth - 5, 2), "status", "");
    fStatus->ResizeToPreferred(); // the height of one line of the font
    fStatusHeight = fStatus->Frame().Height() + 5;
    BView *statusBack = new BView(BRect(0, 0, kDefaultWidth - 1, fStatusHeight - 1), "status-back",
                                  B_FOLLOW_NONE, 0);
    statusBack->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    statusBack->AddChild(fStatus);
    AddChild(statusBack);

    float width = kDefaultWidth;
    float height = kDefaultHeight;
    IPlugView *view = host->controller()->createView(Vst::ViewType::kEditor);
    if (view && view->isPlatformTypeSupported(kPlatformTypeHaikuBView) == kResultTrue) {
        fEditor = owned(view);
        ViewRect size{};
        if (fEditor->getSize(&size) == kResultTrue && size.getWidth() > 0 && size.getHeight() > 0) {
            width = size.getWidth();
            height = size.getHeight();
        }
    } else if (view) {
        view->release();
    }
    FitToEditor(width, height);

    // A new BWindow is locked by the thread that made it until Show(), so the
    // editor is attached under the looper lock, as the platform contract says.
    if (fEditor) {
        fEditor->setFrame(this);
        if (fEditor->attached(fEditorParent, kPlatformTypeHaikuBView) != kResultOk) {
            fprintf(stderr, "NAMku: the editor could not be attached\n");
            fEditor->setFrame(nullptr);
            fEditor = nullptr;
        }
    }

    BRect frame;
    if (fSettings.FindRect("frame", &frame) == B_OK)
        MoveTo(frame.LeftTop());
    MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);

    BMessage meters(kMsgMeters);
    fMeterPump = new BMessageRunner(BMessenger(this), &meters, 33000); // ~30 Hz
    BMessage status(kMsgStatus);
    fStatusPump = new BMessageRunner(BMessenger(this), &status, 1000000);

    const char *backend = fSettings.GetString("backend", "mediakit");
    StartAudio(backend, false);
}

//------------------------------------------------------------------------
MainWindow::~MainWindow()
{
    delete fOpenPanel;
    delete fSavePanel;
    delete fMeterPump;
    delete fStatusPump;
    DetachEditor();
}

//------------------------------------------------------------------------
void MainWindow::FitToEditor(float width, float height)
{
    fEditorParent->ResizeTo(width - 1, height - 1);
    BView *statusBack = FindView("status-back");
    if (statusBack) {
        statusBack->MoveTo(0, fMenuHeight + height);
        statusBack->ResizeTo(width - 1, fStatusHeight - 1);
        fStatus->ResizeTo(width - 9, fStatusHeight - 5);
    }
    ResizeTo(width - 1, fMenuHeight + height + fStatusHeight - 1);
}

//------------------------------------------------------------------------
void MainWindow::DetachEditor()
{
    if (!fEditor)
        return;
    fEditor->setFrame(nullptr);
    if (fEditor->removed() != kResultOk)
        fprintf(stderr, "NAMku: the editor could not be detached\n");
    fEditor = nullptr;
}

//------------------------------------------------------------------------
void MainWindow::StartAudio(const char *backend, bool tellUser)
{
    std::string error;
    if (!fHost->startAudio(backend, error)) {
        // JACK not running (or not installed): fall back to the Media Kit so
        // the app still plays, and say why.
        if (strcmp(backend, "jack") == 0) {
            std::string mkError;
            if (fHost->startAudio("mediakit", mkError))
                error += "\n\nPlaying on the Media Kit instead.";
        }
        Tell("Audio", error);
    } else if (tellUser) {
        UpdateStatus();
    }
    fSettings.SetString("backend", fHost->backend().c_str());

    // The input last chosen on this backend, if it is still there.
    BString key("input:");
    key << fHost->backend().c_str();
    const char *saved = fSettings.GetString(key.String(), "");
    if (saved[0] && fHost->audioRunning()) {
        for (const auto &source : fHost->inputSources()) {
            if (source == saved) {
                ChooseInput(saved);
                break;
            }
        }
    }
    UpdateMenus();
    UpdateStatus();
}

//------------------------------------------------------------------------
void MainWindow::ChooseInput(const char *source)
{
    std::string error;
    if (!fHost->setInputSource(source, error)) {
        Tell("Input", error);
    } else {
        BString key("input:");
        key << fHost->backend().c_str();
        fSettings.SetString(key.String(), source);
    }
    UpdateStatus();
}

//------------------------------------------------------------------------
void MainWindow::UpdateMenus()
{
    const bool jack = fHost->backend() == "jack";
    fMediaKitItem->SetMarked(!jack);
    fJackItem->SetMarked(jack);
}

//------------------------------------------------------------------------
void MainWindow::MenusBeginning()
{
    // The capture sources change as devices come and go: list them afresh.
    while (BMenuItem *item = fInputMenu->RemoveItem((int32)0))
        delete item;
    BMessage *none = new BMessage(kMsgInput);
    none->AddString("source", "");
    BMenuItem *noneItem = new BMenuItem("None", none);
    noneItem->SetMarked(fHost->inputSource().empty());
    fInputMenu->AddItem(noneItem);
    const std::vector<std::string> sources = fHost->inputSources();
    if (!sources.empty())
        fInputMenu->AddSeparatorItem();
    for (const auto &source : sources) {
        BMessage *message = new BMessage(kMsgInput);
        message->AddString("source", source.c_str());
        // The Media Kit lists sources as "media:<device> / <output> / <channel>".
        const char *label = source.c_str();
        if (source.compare(0, 6, "media:") == 0)
            label += 6;
        BMenuItem *item = new BMenuItem(label, message);
        item->SetMarked(source == fHost->inputSource());
        fInputMenu->AddItem(item);
    }
    fInputMenu->SetTargetForItems(this);
    fInputMenu->SetEnabled(fHost->audioRunning());
    UpdateMenus();
    BWindow::MenusBeginning();
}

//------------------------------------------------------------------------
void MainWindow::ShowPresetPanel(bool save)
{
    BFilePanel *&panel = save ? fSavePanel : fOpenPanel;
    if (!panel) {
        BMessenger target(this);
        BMessage message(save ? kMsgPresetSaved : kMsgPresetOpened);
        panel = new BFilePanel(save ? B_SAVE_PANEL : B_OPEN_PANEL, &target, nullptr, B_FILE_NODE,
                               false, &message);
        panel->Window()->SetTitle(save ? "NAMku: Save preset" : "NAMku: Open preset");
        // The folder last used for presets, else ~/config/settings/NAMku/presets.
        const char *dir = fSettings.GetString("presetDir", nullptr);
        BPath path;
        if (dir) {
            panel->SetPanelDirectory(dir);
        } else if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK &&
                   path.Append("NAMku/presets") == B_OK &&
                   create_directory(path.Path(), 0755) == B_OK) {
            panel->SetPanelDirectory(path.Path());
        }
        if (save) {
            BString name("NAMku");
            name << kPresetExtension;
            panel->SetSaveText(name.String());
        }
    }
    panel->Show();
}

//------------------------------------------------------------------------
void MainWindow::PresetChosen(BMessage *message, bool save)
{
    BPath path;
    if (save) {
        entry_ref dir;
        const char *name = nullptr;
        if (message->FindRef("directory", &dir) != B_OK ||
            message->FindString("name", &name) != B_OK || !name)
            return;
        BString file(name);
        if (!file.EndsWith(kPresetExtension))
            file << kPresetExtension;
        if (path.SetTo(&dir) != B_OK || path.Append(file.String()) != B_OK)
            return;
    } else {
        entry_ref ref;
        if (message->FindRef("refs", &ref) != B_OK || path.SetTo(&ref) != B_OK)
            return;
    }
    std::string error;
    const bool ok = save ? fHost->savePreset(path.Path(), error)
                         : fHost->loadPreset(path.Path(), error);
    if (!ok) {
        Tell(save ? "Save preset" : "Open preset", error);
        return;
    }
    BPath parent;
    if (path.GetParent(&parent) == B_OK)
        fSettings.SetString("presetDir", parent.Path());
    BString title("NAMku: ");
    title << path.Leaf();
    SetTitle(title.String());
}

//------------------------------------------------------------------------
void MainWindow::UpdateStatus()
{
    fStatus->SetText(fHost->statusLine().c_str());
}

//------------------------------------------------------------------------
bool MainWindow::QuitRequested()
{
    // Save what to restore next time, then take the editor down on this
    // thread with the looper locked, before the window and its views go.
    std::vector<char> component, controller;
    if (fHost->getState(component, controller)) {
        fSettings.RemoveName("component");
        fSettings.RemoveName("controller");
        fSettings.AddData("component", B_RAW_TYPE, component.data(), component.size());
        if (!controller.empty())
            fSettings.AddData("controller", B_RAW_TYPE, controller.data(), controller.size());
    }
    fSettings.SetRect("frame", Frame());
    if (SaveSettings(fSettings) != B_OK)
        fprintf(stderr, "NAMku: could not save the settings\n");

    delete fMeterPump;
    fMeterPump = nullptr;
    delete fStatusPump;
    fStatusPump = nullptr;
    DetachEditor();
    fHost->stopAudio();
    return true;
}

//------------------------------------------------------------------------
void MainWindow::MessageReceived(BMessage *message)
{
    switch (message->what) {
        case kMsgMeters: {
            Vst::ParameterChangeTransfer *transfer = fHost->outputTransfer();
            Vst::ParamID id;
            Vst::ParamValue value;
            int32 sampleOffset;
            while (transfer && transfer->getNextChange(id, value, sampleOffset))
                fHost->controller()->setParamNormalized(id, value);
            break;
        }
        case kMsgStatus:
            UpdateStatus();
            break;
        case kMsgBackend: {
            const char *backend = message->GetString("backend", "mediakit");
            if (fHost->backend() != backend || !fHost->audioRunning())
                StartAudio(backend, true);
            break;
        }
        case kMsgInput:
            ChooseInput(message->GetString("source", ""));
            break;
        case kMsgAudioInfo:
            Tell("Audio status", fHost->statusLine());
            break;
        case kMsgOpenPreset:
            ShowPresetPanel(false);
            break;
        case kMsgSavePreset:
            ShowPresetPanel(true);
            break;
        case kMsgPresetOpened:
            PresetChosen(message, false);
            break;
        case kMsgPresetSaved:
            PresetChosen(message, true);
            break;
        case B_ABOUT_REQUESTED:
            be_app->PostMessage(B_ABOUT_REQUESTED);
            break;
        default:
            BWindow::MessageReceived(message);
            break;
    }
}

//------------------------------------------------------------------------
tresult PLUGIN_API MainWindow::resizeView(IPlugView *view, ViewRect *newSize)
{
    if (!view || !newSize || view != fEditor)
        return kInvalidArgument;
    FitToEditor(newSize->getWidth(), newSize->getHeight());
    return fEditor->onSize(newSize);
}

//------------------------------------------------------------------------
tresult PLUGIN_API MainWindow::queryInterface(const TUID iid, void **obj)
{
    if (!obj)
        return kInvalidArgument;
    if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
        *obj = static_cast<IPlugFrame *>(this);
        return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
}

} // namespace NAMku
