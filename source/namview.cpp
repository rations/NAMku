// NAMku native Haiku editor implementation.
//
// The amp-head panel of the original Neural Amp Modeler, composited from the
// plugin's own MIT-licensed PNG layers (gui/make_assets.sh -> resource/gui/,
// loaded from the bundle's Contents/Resources/gui): a textured backdrop and
// line overlay, six knobs (face bitmap + code-drawn pointer), two toggle
// switches, a model row and an IR row (loaded via the controller's
// INamFileLoader), and input/output level meters fed by the hidden meter
// parameters. Every bitmap load is NULL-checked with a flat-colour fallback,
// so a missing Resources dir degrades rather than crashes.
//
// Threading: everything here runs on the host window's looper thread (the
// kPlatformTypeHaikuBView contract); the controller is same-thread, so value
// reads/writes and ParamChanged() need no locking.

#include "namview.h"
#include "namcontroller.h"
#include "namgeometry.h"
#include "namids.h"

#include "pluginterfaces/base/ustring.h"

#include <Bitmap.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Font.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>

#include <image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

using namespace Steinberg;

namespace NAMku
{

namespace
{
const uint32 kMsgTick = 'nmTk';
const float kKnobDragRange = 200.0f; // pixels of vertical drag for full range

rgb_color rgb(uint32_t v)
{
    return {(uint8)((v >> 16) & 0xff), (uint8)((v >> 8) & 0xff), (uint8)(v & 0xff), 255};
}

// Resolve the plug-in bundle's Contents dir from its own image: walk the
// loaded images for the one whose text segment contains this function, then go
// .so -> x86_64-haiku -> Contents.
bool contentsDir(BPath &out)
{
    image_info info;
    int32 cookie = 0;
    addr_t marker = (addr_t)&contentsDir;
    while (get_next_image_info(0, &cookie, &info) == B_OK) {
        addr_t text = (addr_t)info.text;
        if (marker < text || marker >= text + (addr_t)info.text_size)
            continue;
        BPath module(info.name), archDir;
        if (module.GetParent(&archDir) != B_OK || archDir.GetParent(&out) != B_OK)
            return false;
        return true; // out = <bundle>/Contents
    }
    return false;
}

// Install the bundled title/body fonts into the user's non-packaged font dir
// (once) so the app_server registers them and BFont can select them.
void installFonts(const BPath &contents)
{
    BPath fontsDest;
    if (find_directory(B_USER_NONPACKAGED_FONTS_DIRECTORY, &fontsDest) != B_OK)
        return;
    if (fontsDest.Append("NAMku") != B_OK)
        return;
    create_directory(fontsDest.Path(), 0755);
    const char *names[] = {"Michroma-Regular.ttf", "Roboto-Regular.ttf"};
    for (const char *name : names) {
        BPath dst(fontsDest);
        dst.Append(name);
        if (BEntry(dst.Path()).Exists())
            continue;
        BPath src(contents);
        src.Append("Resources/fonts");
        src.Append(name);
        BFile in(src.Path(), B_READ_ONLY);
        BFile out(dst.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        if (in.InitCheck() != B_OK || out.InitCheck() != B_OK)
            continue;
        char buf[8192];
        ssize_t r;
        while ((r = in.Read(buf, sizeof(buf))) > 0)
            out.Write(buf, r);
    }
}

BBitmap *loadBitmap(const BPath &dir, const char *name)
{
    BPath p(dir);
    if (p.Append(name) != B_OK)
        return nullptr;
    BBitmap *bmp = BTranslationUtils::GetBitmapFile(p.Path());
    if (!bmp)
        fprintf(stderr, "NAMku: missing art %s (flat fallback)\n", p.Path());
    return bmp;
}

// Draw a bitmap scaled to fill dest (alpha-composited).
void drawFitted(BView *v, const BBitmap *bmp, BRect dest)
{
    if (!bmp)
        return;
    v->SetDrawingMode(B_OP_ALPHA);
    v->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
    v->DrawBitmap(bmp, bmp->Bounds(), dest);
}
} // namespace

//------------------------------------------------------------------------
// NamPanelView — the BView inside the host's parent view.
//------------------------------------------------------------------------
class NamPanelView : public BView
{
public:
    NamPanelView(BRect frame, NamController *controller)
        : BView(frame, "NAMku-editor", B_FOLLOW_NONE, B_WILL_DRAW), mController(controller)
    {
        SetViewColor(B_TRANSPARENT_COLOR); // we repaint everything ourselves

        BPath contents;
        if (contentsDir(contents)) {
            installFonts(contents);
            BPath dir(contents);
            dir.Append("Resources/gui");
            mBackground = loadBitmap(dir, "background.png");
            mLines = loadBitmap(dir, "lines.png");
            mKnobFace = loadBitmap(dir, "knob_face.png");
            mFileBg = loadBitmap(dir, "file_bg.png");
            mMeterBg = loadBitmap(dir, "meter_bg.png");
            mSwitchHandle = loadBitmap(dir, "switch_handle.png");
            mInputBg = loadBitmap(dir, "input_bg.png");
            mGear = loadBitmap(dir, "gear.png");
            mLoadIcon = loadBitmap(dir, "load.png");
            mClear = loadBitmap(dir, "clear.png");
            mSlimIcon = loadBitmap(dir, "slimmable.png");
            mIrOn = loadBitmap(dir, "ir_on.png");
            mIrOff = loadBitmap(dir, "ir_off.png");
            mModelIcon = loadBitmap(dir, "model.png");
            mGlobe = loadBitmap(dir, "globe.png");
            mArrowL = loadBitmap(dir, "arrow_left.png");
            mArrowR = loadBitmap(dir, "arrow_right.png");
        } else {
            fprintf(stderr, "NAMku: cannot locate bundle Resources (flat fallback)\n");
        }

        // Title (Michroma) and body (Roboto) fonts, matching the original.
        // Fall back to system fonts if the app_server hasn't registered them
        // yet (e.g. the very first open after install).
        mTitleFont = *be_bold_font;
        mTitleFont.SetFamilyAndStyle("Michroma", "Regular");
        mBodyFont = *be_plain_font;
        mBodyFont.SetFamilyAndStyle("Roboto", "Regular");
    }

    ~NamPanelView() override
    {
        delete mFilePanel;
        delete mBackground;
        delete mLines;
        delete mKnobFace;
        delete mFileBg;
        delete mMeterBg;
        delete mSwitchHandle;
        delete mInputBg;
        delete mGear;
        delete mLoadIcon;
        delete mClear;
        delete mSlimIcon;
        delete mIrOn;
        delete mIrOff;
        delete mModelIcon;
        delete mGlobe;
        delete mArrowL;
        delete mArrowR;
        delete mOff;
    }

    void AttachedToWindow() override
    {
        BView::AttachedToWindow();

        BRect b = Bounds();
        mOff = new BBitmap(b, B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
        if (mOff && mOff->IsValid()) {
            mOffView = new BView(b, "offscreen", B_FOLLOW_NONE, 0);
            mOff->AddChild(mOffView);
        } else {
            delete mOff;
            mOff = nullptr;
            mOffView = nullptr;
        }

        BMessage tick(kMsgTick);
        mTick = new BMessageRunner(BMessenger(this), &tick, 33000); // ~30 Hz
    }

    void DetachedFromWindow() override
    {
        delete mTick;
        mTick = nullptr;
        BView::DetachedFromWindow();
    }

    void Draw(BRect) override
    {
        if (mOff && mOffView && mOff->Lock()) {
            Compose(mOffView);
            mOffView->Sync();
            mOff->Unlock();
            SetDrawingMode(B_OP_COPY);
            DrawBitmap(mOff, B_ORIGIN);
        } else {
            Compose(this);
        }
    }

    void MouseDown(BPoint where) override
    {
        // Slim overlay captures input while open.
        if (mSlimOpen) {
            float dx = where.x - geo::kSlimKnobCX, dy = where.y - geo::kSlimKnobCY;
            if (dx * dx + dy * dy <= (float)(geo::kSlimKnobR * geo::kSlimKnobR))
                startDrag(kSlimId, where);
            else {
                mSlimOpen = false; // click outside dismisses
                Invalidate();
            }
            return;
        }
        // Settings overlay captures input while open.
        if (mSettingsOpen) {
            handleSettingsClick(where);
            return;
        }

        // Gear -> open settings.
        if (hitCircle(where, geo::kGearCX, geo::kGearCY, geo::kGearR + 4)) {
            mSettingsOpen = true;
            Invalidate();
            return;
        }
        // Slim icon (slimmable models only) -> open slim overlay.
        if (mSlimmable) {
            BRect si(geo::kSlimIconX, geo::kSlimIconY, geo::kSlimIconX + geo::kSlimIconW,
                     geo::kSlimIconY + geo::kSlimIconH);
            if (si.Contains(where)) {
                mSlimOpen = true;
                Invalidate();
                return;
            }
        }

        // Knobs: start a vertical drag.
        for (int i = 0; i < geo::kKnobCount; ++i) {
            const geo::KnobSpec &k = geo::kKnobs[i];
            if (hitCircle(where, k.cx, k.cy, k.r)) {
                startDrag(k.id, where);
                return;
            }
        }
        // Toggles.
        for (int i = 0; i < geo::kToggleCount; ++i) {
            const geo::ToggleSpec &t = geo::kToggles[i];
            BRect pill(t.cx - geo::kToggleW / 2, t.cy - geo::kToggleH / 2, t.cx + geo::kToggleW / 2,
                       t.cy + geo::kToggleH / 2);
            if (pill.Contains(where)) {
                double on = mController->getParamNormalized(t.id) > 0.5 ? 0.0 : 1.0;
                editParam(t.id, on);
                Invalidate();
                return;
            }
        }
        // File rows.
        if (handleFileRowClick(geo::kModelRow, where, /*model*/ true))
            return;
        if (handleFileRowClick(geo::kIrRow, where, /*model*/ false))
            return;

        BView::MouseDown(where);
    }

    void MouseMoved(BPoint where, uint32, const BMessage *) override
    {
        if (!mDragParam)
            return;
        double norm = mDragStartNorm + (double)(mDragStartY - where.y) / kKnobDragRange;
        norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
        mController->setParamNormalized(mDragParam, norm);
        mController->performEdit(mDragParam, norm);
        Invalidate();
    }

    void MouseUp(BPoint) override
    {
        if (mDragParam) {
            mController->endEdit(mDragParam);
            mDragParam = 0;
        }
    }

    void MessageReceived(BMessage *message) override
    {
        switch (message->what) {
            case kMsgTick: {
                // ~15 dB/s release at 30 Hz (matches the original meter).
                mInDisp *= 0.944f;
                mOutDisp *= 0.944f;
                if (mInDisp > 0.002f || mOutDisp > 0.002f)
                    Invalidate();
                break;
            }
            case B_MOUSE_WHEEL_CHANGED: {
                float dy = 0;
                if (message->FindFloat("be:wheel_delta_y", &dy) == B_OK && dy != 0)
                    handleWheel(dy);
                break;
            }
            case B_REFS_RECEIVED: {
                entry_ref ref;
                if (message->FindRef("refs", &ref) == B_OK) {
                    BPath path(&ref);
                    if (path.InitCheck() == B_OK) {
                        if (mLoadTarget == 0)
                            mController->setModelFile(path.Path());
                        else
                            mController->setIrFile(path.Path());
                    }
                    Invalidate();
                }
                break;
            }
            default:
                BView::MessageReceived(message);
                break;
        }
    }

    // Called by NamEditorView on the window looper thread.
    void ParamChanged(Vst::ParamID id, Vst::ParamValue value)
    {
        if (id == kInputMeterId) {
            if ((float)value > mInDisp)
                mInDisp = (float)value; // fast attack; tick releases
            return;
        }
        if (id == kOutputMeterId) {
            if ((float)value > mOutDisp)
                mOutDisp = (float)value;
            return;
        }
        // Any visible control value (automation, generic UI, state load).
        Invalidate();
    }

    void ModelCapsChanged(bool slimmable, bool hasInputLevel, bool hasOutputLevel)
    {
        mSlimmable = slimmable;
        mHasInputLevel = hasInputLevel;
        mHasOutputLevel = hasOutputLevel;
        Invalidate();
    }

private:
    void editParam(Vst::ParamID id, double norm)
    {
        mController->beginEdit(id);
        mController->setParamNormalized(id, norm);
        mController->performEdit(id, norm);
        mController->endEdit(id);
    }

    static bool hitCircle(BPoint where, float cx, float cy, float r)
    {
        float dx = where.x - cx, dy = where.y - cy;
        return dx * dx + dy * dy <= r * r;
    }

    void startDrag(Vst::ParamID id, BPoint where)
    {
        mDragParam = id;
        mDragStartY = where.y;
        mDragStartNorm = mController->getParamNormalized(id);
        mController->beginEdit(id);
        SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
    }

    void nudgeParam(Vst::ParamID id, double delta)
    {
        double norm = mController->getParamNormalized(id) + delta;
        norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
        editParam(id, norm);
        Invalidate();
    }

    // Mouse wheel adjusts the knob/slider under the cursor (wheel up = increase).
    void handleWheel(float dy)
    {
        BPoint where;
        uint32 buttons;
        GetMouse(&where, &buttons, false);
        const double step = -dy * 0.05;
        if (mSlimOpen) {
            if (hitCircle(where, geo::kSlimKnobCX, geo::kSlimKnobCY, geo::kSlimKnobR))
                nudgeParam(kSlimId, step);
            return;
        }
        if (mSettingsOpen) {
            if (mHasInputLevel && calValueRect().Contains(where))
                nudgeParam(kInputCalibrationLevelId, step);
            return;
        }
        for (int i = 0; i < geo::kKnobCount; ++i) {
            const geo::KnobSpec &k = geo::kKnobs[i];
            if (hitCircle(where, k.cx, k.cy, k.r)) {
                nudgeParam(k.id, step);
                return;
            }
        }
    }

    // The settings overlay is full-window (as in the original). Control rects
    // are absolute; drawSettings() lays them out the same way.
    BRect settingsCloseBox() const
    {
        return BRect(560, 50, 578, 68);
    }
    BRect calValueRect() const
    {
        return BRect(120, 192, 210, 218);
    }
    BRect calibrateToggleRect() const
    {
        return BRect(143, 232, 143 + geo::kToggleW, 232 + geo::kToggleH);
    }
    BRect outputModeRow(int i) const
    {
        float y = 214 + i * 26;
        return BRect(312, y, 312 + 220, y + 22);
    }

    void handleSettingsClick(BPoint where)
    {
        if (settingsCloseBox().Contains(where)) {
            mSettingsOpen = false;
            Invalidate();
            return;
        }
        // Output mode (index 0/1/2 -> normalized 0/0.5/1). Normalized and
        // Calibrated need model output-level metadata.
        for (int i = 0; i < 3; ++i) {
            if (outputModeRow(i).Contains(where)) {
                if (i > 0 && !mHasOutputLevel)
                    return; // gated
                editParam(kOutputModeId, i * 0.5);
                Invalidate();
                return;
            }
        }
        if (mHasInputLevel) {
            if (calibrateToggleRect().Contains(where)) {
                double on = mController->getParamNormalized(kCalibrateInputId) > 0.5 ? 0.0 : 1.0;
                editParam(kCalibrateInputId, on);
                Invalidate();
                return;
            }
            if (calValueRect().Contains(where)) {
                startDrag(kInputCalibrationLevelId, where); // drag to edit dBu
                return;
            }
        }
    }

    bool handleFileRowClick(const geo::FileRow &row, BPoint where, bool model)
    {
        BRect rect(row.x, row.y, row.x + row.w, row.y + row.h);
        if (!rect.Contains(where))
            return false;

        char path[B_PATH_NAME_LENGTH] = "";
        const bool loaded = fileName(model, path, sizeof(path));

        BRect prevBox(row.x + 30, row.y + 3, row.x + 50, row.y + row.h - 3);
        BRect nextBox(row.x + 50, row.y + 3, row.x + 70, row.y + row.h - 3);
        BRect rightBox(row.x + row.w - 28, row.y + 3, row.x + row.w - 4, row.y + row.h - 3);

        if (prevBox.Contains(where)) {
            cycleFile(model, -1);
            return true;
        }
        if (nextBox.Contains(where)) {
            cycleFile(model, +1);
            return true;
        }
        if (rightBox.Contains(where)) {
            if (loaded) { // clear (x)
                if (model)
                    mController->setModelFile("");
                else
                    mController->setIrFile("");
                Invalidate();
            } else { // globe -> "Get" page
                openUrl(model ? "https://www.neuralampmodeler.com"
                              : "https://www.neuralampmodeler.com");
            }
            return true;
        }

        // Anywhere else on the row opens the file panel.
        if (!mFilePanel)
            mFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, 0, false);
        mLoadTarget = model ? 0 : 1;
        mFilePanel->Show();
        return true;
    }

    // Cycle to the previous/next same-extension file in the loaded file's
    // directory (alphabetical). No-op when nothing is loaded.
    void cycleFile(bool model, int dir)
    {
        char cur[B_PATH_NAME_LENGTH] = "";
        if (!fileName(model, cur, sizeof(cur)))
            return;
        BPath curPath(cur), parent;
        if (curPath.GetParent(&parent) != B_OK)
            return;
        const char *ext = model ? "nam" : "wav";

        std::vector<std::string> files;
        BDirectory d(parent.Path());
        BEntry e;
        while (d.GetNextEntry(&e) == B_OK) {
            BPath p;
            if (e.GetPath(&p) != B_OK)
                continue;
            const char *dot = std::strrchr(p.Leaf(), '.');
            if (dot && strcasecmp(dot + 1, ext) == 0)
                files.push_back(p.Path());
        }
        if (files.size() < 2)
            return;
        std::sort(files.begin(), files.end());
        int idx = -1;
        for (size_t i = 0; i < files.size(); ++i)
            if (files[i] == curPath.Path()) {
                idx = (int)i;
                break;
            }
        if (idx < 0)
            return;
        int n = (int)files.size();
        const std::string &next = files[(size_t)(((idx + dir) % n + n) % n)];
        if (model)
            mController->setModelFile(next.c_str());
        else
            mController->setIrFile(next.c_str());
        Invalidate();
    }

    void openUrl(const char *url)
    {
        char *argv[] = {const_cast<char *>(url)};
        be_roster->Launch("application/x-vnd.Be.URL.http", 1, argv);
    }

    bool fileName(bool model, char *buf, int32 size)
    {
        tresult r =
            model ? mController->getModelFile(buf, size) : mController->getIrFile(buf, size);
        return r == kResultOk && buf[0] != 0;
    }

    //--- drawing ---------------------------------------------------------
    void Compose(BView *v)
    {
        v->SetFont(&mBodyFont); // Roboto for all body text unless overridden
        v->SetDrawingMode(B_OP_COPY);
        if (mBackground)
            v->DrawBitmap(mBackground, mBackground->Bounds(), v->Bounds());
        else {
            v->SetHighColor(rgb(geo::kBgColor));
            v->FillRect(v->Bounds());
        }
        if (mLines)
            drawFitted(v, mLines, v->Bounds());

        drawTitle(v);
        for (int i = 0; i < geo::kKnobCount; ++i)
            drawKnob(v, geo::kKnobs[i]);
        for (int i = 0; i < geo::kToggleCount; ++i)
            drawToggle(v, geo::kToggles[i]);
        drawFileRow(v, geo::kModelRow, true);
        drawFileRow(v, geo::kIrRow, false);
        drawMeter(v, geo::kInputMeter, mInDisp, "IN");
        drawMeter(v, geo::kOutputMeter, mOutDisp, "OUT");
        drawGear(v);
        if (mSlimmable)
            drawSlimIcon(v);

        if (mSettingsOpen)
            drawSettings(v);
        if (mSlimOpen)
            drawSlimOverlay(v);
    }

    void drawTitle(BView *v)
    {
        v->SetDrawingMode(B_OP_OVER);
        v->SetFont(&mTitleFont);
        v->SetFontSize(22);
        v->SetHighColor(rgb(geo::kTextColor));
        const char *title = "NAMku";
        float w = v->StringWidth(title);
        v->DrawString(title, BPoint(geo::kTitleX + (geo::kTitleW - w) / 2.0f, geo::kTitleY + 34));
        v->SetFont(&mBodyFont);
    }

    // Stroke an arc in the knob angle convention (0 = up, clockwise positive).
    // BView::StrokeArc draws a smooth, anti-aliased Bezier arc; its angles are
    // degrees measured counter-clockwise from east (3 o'clock). Converting from
    // the knob convention, an angle A maps to (90 - A): the start becomes
    // (90 - a0) and the swept span negates.
    void strokeArc(BView *v, float cx, float cy, float radius, double a0, double a1, float pen,
                   rgb_color color)
    {
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(color);
        v->SetPenSize(pen);
        v->StrokeArc(BPoint(cx, cy), radius, radius, (float)(90.0 - a0), (float)-(a1 - a0));
        v->SetPenSize(1.0f);
    }

    // Face bitmap + dim track arc + azure value arc + pointer notch.
    void drawKnobBody(BView *v, double norm, int cx, int cy, int r)
    {
        BRect face(cx - r, cy - r, cx + r, cy + r);
        if (mKnobFace)
            drawFitted(v, mKnobFace, face);
        else {
            v->SetDrawingMode(B_OP_OVER);
            v->SetHighColor(40, 40, 46);
            v->FillEllipse(face);
        }

        const double a0 = -geo::kKnobSweepDeg / 2.0; // min (down-left)
        const double aVal = a0 + norm * geo::kKnobSweepDeg;
        const float arcR = r * (float)geo::kKnobArcFrac;
        strokeArc(v, cx, cy, arcR, a0, -a0, 3.0f, rgb(0x2E2B34)); // full track
        if (norm > 0.001)
            strokeArc(v, cx, cy, arcR, a0, aVal, 3.0f, rgb(geo::kAzure)); // value

        // Pointer notch on the face at the value angle.
        const double ang = aVal * M_PI / 180.0;
        const float s = (float)std::sin(ang), c = (float)std::cos(ang);
        const float lo = r * 0.45f, hi = r * (float)geo::kKnobPointerFrac;
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kAzure));
        v->SetPenSize(3.0f);
        v->StrokeLine(BPoint(cx + lo * s, cy - lo * c), BPoint(cx + hi * s, cy - hi * c));
        v->SetPenSize(1.0f);
    }

    void drawKnob(BView *v, const geo::KnobSpec &k)
    {
        const double norm = mController->getParamNormalized(k.id);
        drawKnobBody(v, norm, k.cx, k.cy, k.r);

        // Label above the knob.
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kTextColor));
        v->SetFontSize(12);
        float lw = v->StringWidth(k.label);
        v->DrawString(k.label, BPoint(k.cx - lw / 2.0f, k.cy - geo::kKnobLabelDY));

        // Value (with unit) below the knob.
        Vst::String128 str = {0};
        char val[64] = "";
        if (mController->getParamStringByValue(k.id, norm, str) == kResultOk) {
            UString(str, 128).toAscii(val, sizeof(val));
            char disp[80];
            if (k.unit)
                std::snprintf(disp, sizeof(disp), "%s %s", val, k.unit);
            else
                std::snprintf(disp, sizeof(disp), "%s", val);
            v->SetHighColor(rgb(geo::kDimColor));
            v->SetFontSize(11);
            float vw = v->StringWidth(disp);
            v->DrawString(disp, BPoint(k.cx - vw / 2.0f, k.cy + geo::kKnobValueDY));
        }
    }

    void drawToggle(BView *v, const geo::ToggleSpec &t)
    {
        const bool on = mController->getParamNormalized(t.id) > 0.5;
        BRect pill(t.cx - geo::kToggleW / 2, t.cy - geo::kToggleH / 2, t.cx + geo::kToggleW / 2,
                   t.cy + geo::kToggleH / 2);
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(on ? rgb(geo::kAzure) : rgb(0x2A2730));
        v->FillRoundRect(pill, geo::kToggleH / 2, geo::kToggleH / 2);

        // Handle at left (off) or right (on).
        const int hs = 18;
        float hx = on ? pill.right - hs - 2 : pill.left + 2;
        float hy = t.cy - hs / 2.0f;
        BRect handle(hx, hy, hx + hs, hy + hs);
        if (mSwitchHandle)
            drawFitted(v, mSwitchHandle, handle);
        else {
            v->SetHighColor(rgb(geo::kTextColor));
            v->FillEllipse(handle);
        }

        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kTextColor));
        v->SetFontSize(11);
        float lw = v->StringWidth(t.label);
        v->DrawString(t.label, BPoint(t.cx - lw / 2.0f, t.cy + geo::kToggleH / 2 + 15));
    }

    void drawFileRow(BView *v, const geo::FileRow &row, bool model)
    {
        BRect rect(row.x, row.y, row.x + row.w, row.y + row.h);
        if (mFileBg)
            drawFitted(v, mFileBg, rect);
        else {
            v->SetDrawingMode(B_OP_OVER);
            v->SetHighColor(rgb(0x14161B));
            v->FillRoundRect(rect, 6, 6);
        }

        char path[B_PATH_NAME_LENGTH] = "";
        const bool loaded = fileName(model, path, sizeof(path));
        const float cy = row.y + row.h / 2.0f;

        // Status icon to the left of the pill: amp icon (model) / IR on-off.
        if (model)
            drawIcon(v, mModelIcon, geo::kRowIconCX, cy);
        else
            drawIcon(v, loaded ? mIrOn : mIrOff, geo::kRowIconCX, cy);

        // Load (folder) + prev/next arrows at the pill's left.
        drawIcon(v, mLoadIcon, row.x + 16, cy);
        drawIcon(v, mArrowL, row.x + 40, cy);
        drawIcon(v, mArrowR, row.x + 60, cy);

        // File name / placeholder, centered in the middle region (clipped).
        v->SetDrawingMode(B_OP_OVER);
        v->SetFontSize(12);
        const char *text = loaded ? baseName(path) : row.placeholder;
        v->SetHighColor(loaded ? rgb(geo::kTextColor) : rgb(geo::kDimColor));
        const float midL = row.x + 78, midR = row.x + row.w - 30;
        BString disp(text);
        clipToWidth(v, disp, midR - midL);
        float tw = v->StringWidth(disp.String());
        v->DrawString(disp.String(), BPoint(midL + (midR - midL - tw) / 2.0f, cy + 4));

        // Clear (loaded) or globe "Get" (empty) at the pill's right.
        drawIcon(v, loaded ? mClear : mGlobe, row.x + row.w - 16, cy);
    }

    // Truncate `s` with an ellipsis so it fits within maxW at the current font.
    void clipToWidth(BView *v, BString &s, float maxW)
    {
        if (v->StringWidth(s.String()) <= maxW)
            return;
        while (s.Length() > 1) {
            BString t(s);
            t << B_UTF8_ELLIPSIS;
            if (v->StringWidth(t.String()) <= maxW) {
                s = t;
                return;
            }
            s.Truncate(s.Length() - 1);
        }
    }

    // Replicates the original NAMMeterControl / Linux LevelMeter rendering:
    // the fill track is a 10 px column centred in the meter, inset by
    // (0.1*H - 10) top and bottom; a black grid every 2 px gives the segmented
    // look, and a bright tick sits at the top of the fill. No separate peak
    // indicator (the original draws none).
    void drawMeter(BView *v, const geo::MeterRect &m, float level, const char *label)
    {
        BRect rect(m.x, m.y, m.x + m.w, m.y + m.h);
        if (mMeterBg)
            drawFitted(v, mMeterBg, rect);
        else {
            v->SetDrawingMode(B_OP_OVER);
            v->SetHighColor(rgb(0x101216));
            v->FillRect(rect);
        }

        v->SetDrawingMode(B_OP_OVER);
        const float trackW = 10.0f;
        const float trackX = m.x + (m.w - trackW) / 2.0f;
        const float topMargin = 0.1f * m.h - 10.0f;
        const float trackY = m.y + topMargin;
        const float trackH = m.h - 2.0f * topMargin;

        const float lv = level < 0 ? 0 : (level > 1 ? 1 : level);
        if (lv > 0.0f) {
            const float h = trackH * lv;
            const float fillTop = trackY + trackH - h;
            v->SetHighColor(rgb(geo::kAzure));
            v->FillRect(BRect(trackX, fillTop, trackX + trackW, trackY + trackH));
            // Black segmented grid over the track.
            v->SetHighColor(0, 0, 0);
            for (float y = trackY + 2.0f; y < trackY + trackH; y += 2.0f)
                v->FillRect(BRect(trackX, y, trackX + trackW, y));
            // Bright tick at the top of the fill.
            v->SetHighColor(rgb(0x6A9FF0));
            v->FillRect(BRect(trackX, fillTop, trackX + trackW, fillTop));
        }

        v->SetHighColor(rgb(geo::kDimColor));
        v->SetFontSize(10);
        float lw = v->StringWidth(label);
        v->DrawString(label, BPoint(m.x + (m.w - lw) / 2.0f, m.y + m.h + 14));
    }

    // Draw an icon bitmap (rendered at 2x) at its display size, centered.
    void drawIcon(BView *v, const BBitmap *bmp, float cx, float cy)
    {
        if (!bmp)
            return;
        BRect b = bmp->Bounds();
        float w = (b.Width() + 1) / 2.0f; // rendered at 2x -> half-size
        float h = (b.Height() + 1) / 2.0f;
        BRect dest(cx - w / 2.0f, cy - h / 2.0f, cx + w / 2.0f, cy + h / 2.0f);
        v->SetDrawingMode(B_OP_ALPHA);
        v->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
        v->DrawBitmap(bmp, b, dest);
    }

    void drawGear(BView *v)
    {
        if (mGear)
            drawIcon(v, mGear, geo::kGearCX, geo::kGearCY);
    }

    void drawSlimIcon(BView *v)
    {
        drawIcon(v, mSlimIcon, geo::kSlimIconX + geo::kSlimIconW / 2.0f,
                 geo::kSlimIconY + geo::kSlimIconH / 2.0f);
    }

    void dimPanel(BView *v)
    {
        v->SetDrawingMode(B_OP_ALPHA);
        v->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
        v->SetHighColor(0, 0, 0, 170);
        v->FillRect(v->Bounds());
    }

    // Full-window settings overlay (matches the original layout): title + close
    // at top, input calibration on the left, output mode on the right, and a
    // model-info / credits band along the bottom.
    void drawSettings(BView *v)
    {
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(0x161318));
        v->FillRect(v->Bounds());
        v->SetHighColor(rgb(0x2E2B34));
        v->StrokeRoundRect(BRect(8, 8, geo::kWinW - 8, geo::kWinH - 8), 10, 10);

        // Title + close.
        v->SetHighColor(rgb(geo::kTextColor));
        v->SetFont(&mTitleFont);
        v->SetFontSize(24);
        float tw = v->StringWidth("SETTINGS");
        v->DrawString("SETTINGS", BPoint((geo::kWinW - tw) / 2.0f, 68));
        v->SetFont(&mBodyFont);
        BRect close = settingsCloseBox();
        v->SetHighColor(rgb(geo::kDimColor));
        v->SetPenSize(2.0f);
        v->StrokeLine(BPoint(close.left, close.top), BPoint(close.right, close.bottom));
        v->StrokeLine(BPoint(close.left, close.bottom), BPoint(close.right, close.top));
        v->SetPenSize(1.0f);

        // --- Input calibration (left) ---
        BRect val = calValueRect();
        if (mInputBg)
            drawFitted(v, mInputBg, val);
        Vst::String128 str = {0};
        char buf[64] = "";
        if (mController->getParamStringByValue(
                kInputCalibrationLevelId, mController->getParamNormalized(kInputCalibrationLevelId),
                str) == kResultOk)
            UString(str, 128).toAscii(buf, sizeof(buf));
        v->SetDrawingMode(B_OP_OVER);
        v->SetFontSize(13);
        v->SetHighColor(mHasInputLevel ? rgb(geo::kTextColor) : rgb(0x5A5760));
        float vw = v->StringWidth(buf);
        v->DrawString(buf, BPoint(val.left + (val.Width() - vw) / 2.0f, val.top + 18));

        BRect tog = calibrateToggleRect();
        const bool calOn = mController->getParamNormalized(kCalibrateInputId) > 0.5;
        v->SetHighColor(!mHasInputLevel ? rgb(0x2A2730)
                                        : (calOn ? rgb(geo::kAzure) : rgb(0x2A2730)));
        v->FillRoundRect(tog, geo::kToggleH / 2, geo::kToggleH / 2);
        const int hs = 18;
        float hx = calOn ? tog.right - hs - 2 : tog.left + 2;
        BRect handle(hx, tog.top + 2, hx + hs, tog.top + 2 + hs);
        if (mSwitchHandle)
            drawFitted(v, mSwitchHandle, handle);
        v->SetDrawingMode(B_OP_OVER);
        v->SetFontSize(12);
        v->SetHighColor(mHasInputLevel ? rgb(geo::kTextColor) : rgb(0x5A5760));
        float cw = v->StringWidth("Calibrate Input");
        v->DrawString("Calibrate Input",
                      BPoint(val.left + (val.Width() - cw) / 2.0f, tog.bottom + 16));

        // --- Output mode (right) ---
        v->SetFontSize(13);
        v->SetHighColor(rgb(geo::kTextColor));
        v->DrawString("Output Mode", BPoint(340, 200));
        const char *modes[3] = {"Raw", "Normalized", "Calibrated"};
        int cur = (int)(mController->getParamNormalized(kOutputModeId) * 2.0 + 0.5);
        v->SetFontSize(12);
        for (int i = 0; i < 3; ++i) {
            BRect row = outputModeRow(i);
            const bool gated = (i > 0 && !mHasOutputLevel);
            BPoint dot(row.left + 8, row.top + 11);
            v->SetHighColor(gated ? rgb(0x4A4750) : rgb(geo::kAzure));
            v->StrokeEllipse(dot, 6, 6);
            if (i == cur) {
                v->SetHighColor(rgb(geo::kAzure));
                v->FillEllipse(dot, 3, 3);
            }
            v->SetHighColor(gated ? rgb(0x5A5760) : rgb(geo::kTextColor));
            BString label(modes[i]);
            if (gated)
                label << " [Not supported by model]";
            v->DrawString(label.String(), BPoint(row.left + 22, row.top + 16));
        }

        // --- Bottom band: separator + model info (left) + credits (right) ---
        v->SetHighColor(rgb(0x2A2730));
        v->StrokeLine(BPoint(24, 306), BPoint(geo::kWinW - 24, 306));
        v->SetFontSize(11);
        v->SetHighColor(rgb(geo::kDimColor));
        char model[B_PATH_NAME_LENGTH] = "";
        const bool haveModel = fileName(true, model, sizeof(model));
        v->DrawString("Model information:", BPoint(28, 326));
        v->DrawString(haveModel ? baseName(model) : "(no model loaded)", BPoint(28, 342));

        v->DrawString("NAMku — Neural Amp Modeler for Haiku", BPoint(300, 326));
        v->DrawString("© 2026 rations · MIT License", BPoint(300, 342));
        v->DrawString("Based on NeuralAmpModelerPlugin by Steven Atkinson", BPoint(300, 358));
        v->SetHighColor(rgb(geo::kAzure));
        v->DrawString("www.neuralampmodeler.com", BPoint(300, 374));
    }

    void drawSlimOverlay(BView *v)
    {
        dimPanel(v);
        const double norm = mController->getParamNormalized(kSlimId);
        drawKnobBody(v, norm, geo::kSlimKnobCX, geo::kSlimKnobCY, geo::kSlimKnobR);
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kTextColor));
        v->SetFont(&mTitleFont);
        v->SetFontSize(15);
        float w = v->StringWidth("Slim");
        v->DrawString("Slim",
                      BPoint(geo::kSlimKnobCX - w / 2.0f, geo::kSlimKnobCY - geo::kSlimKnobR - 12));
        v->SetFont(&mBodyFont);
        v->SetFontSize(11);
        v->SetHighColor(rgb(geo::kDimColor));
        const char *hint = "click outside to close";
        float hw = v->StringWidth(hint);
        v->DrawString(
            hint, BPoint(geo::kSlimKnobCX - hw / 2.0f, geo::kSlimKnobCY + geo::kSlimKnobR + 26));
    }

    static const char *baseName(const char *path)
    {
        const char *slash = std::strrchr(path, '/');
        return slash ? slash + 1 : path;
    }

    NamController *mController;

    BFont mTitleFont; // Michroma (or bold fallback)
    BFont mBodyFont;  // Roboto (or plain fallback)

    BBitmap *mBackground = nullptr;
    BBitmap *mLines = nullptr;
    BBitmap *mKnobFace = nullptr;
    BBitmap *mFileBg = nullptr;
    BBitmap *mMeterBg = nullptr;
    BBitmap *mSwitchHandle = nullptr;
    BBitmap *mInputBg = nullptr;
    BBitmap *mGear = nullptr;
    BBitmap *mLoadIcon = nullptr;
    BBitmap *mClear = nullptr;
    BBitmap *mSlimIcon = nullptr;
    BBitmap *mIrOn = nullptr;
    BBitmap *mIrOff = nullptr;
    BBitmap *mModelIcon = nullptr;
    BBitmap *mGlobe = nullptr;
    BBitmap *mArrowL = nullptr;
    BBitmap *mArrowR = nullptr;

    BBitmap *mOff = nullptr;
    BView *mOffView = nullptr;
    BMessageRunner *mTick = nullptr;
    BFilePanel *mFilePanel = nullptr;

    Vst::ParamID mDragParam = 0; // 0 = no active drag
    float mDragStartY = 0;
    double mDragStartNorm = 0;
    int mLoadTarget = 0; // 0 = model, 1 = IR

    float mInDisp = 0, mOutDisp = 0, mInPeak = 0, mOutPeak = 0;

    bool mSettingsOpen = false;
    bool mSlimOpen = false;
    bool mSlimmable = false;
    bool mHasInputLevel = false;
    bool mHasOutputLevel = false;
};

//------------------------------------------------------------------------
// NamEditorView
//------------------------------------------------------------------------

NamEditorView::NamEditorView(NamController *controller) : HaikuPlugView(controller)
{
    Steinberg::ViewRect size(0, 0, geo::kWinW, geo::kWinH);
    setRect(size);
}

BView *NamEditorView::createHaikuView(BRect frame)
{
    mPanel = new NamPanelView(frame, static_cast<NamController *>(getController()));
    return mPanel;
}

void NamEditorView::removedFromParent()
{
    mPanel = nullptr; // HaikuPlugView deletes the BView
    HaikuPlugView::removedFromParent();
}

void NamEditorView::ParamChanged(Vst::ParamID id, Vst::ParamValue value)
{
    if (mPanel)
        mPanel->ParamChanged(id, value);
}

void NamEditorView::ModelCapsChanged(bool slimmable, bool hasInputLevel, bool hasOutputLevel)
{
    if (mPanel)
        mPanel->ModelCapsChanged(slimmable, hasInputLevel, hasOutputLevel);
}

} // namespace NAMku
