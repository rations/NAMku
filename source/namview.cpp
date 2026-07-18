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
#include <FilePanel.h>
#include <Font.h>
#include <MessageRunner.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>

#include <image.h>

#include <cmath>
#include <cstdio>
#include <cstring>

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

// Resolve <bundle>/Contents/Resources/gui from this plug-in's own image: walk
// the loaded images for the one whose text segment contains this function,
// then go .so -> x86_64-haiku -> Contents -> Resources/gui.
bool resourceDir(BPath &out)
{
    image_info info;
    int32 cookie = 0;
    addr_t marker = (addr_t)&resourceDir;
    while (get_next_image_info(0, &cookie, &info) == B_OK) {
        addr_t text = (addr_t)info.text;
        if (marker < text || marker >= text + (addr_t)info.text_size)
            continue;
        BPath module(info.name);
        BPath archDir, contents;
        if (module.GetParent(&archDir) != B_OK || archDir.GetParent(&contents) != B_OK)
            return false;
        out = contents;
        return out.Append("Resources/gui") == B_OK;
    }
    return false;
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

        BPath dir;
        if (resourceDir(dir)) {
            mBackground = loadBitmap(dir, "background.png");
            mLines = loadBitmap(dir, "lines.png");
            mKnobFace = loadBitmap(dir, "knob_face.png");
            mFileBg = loadBitmap(dir, "file_bg.png");
            mMeterBg = loadBitmap(dir, "meter_bg.png");
            mSwitchHandle = loadBitmap(dir, "switch_handle.png");
            mInputBg = loadBitmap(dir, "input_bg.png");
        } else {
            fprintf(stderr, "NAMku: cannot locate bundle Resources (flat fallback)\n");
        }
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
            BRect si(geo::kSlimIconX, geo::kSlimIconY, geo::kSlimIconX + geo::kSlimIconS,
                     geo::kSlimIconY + geo::kSlimIconS);
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
                bool active = false;
                mInDisp *= 0.80f;
                mOutDisp *= 0.80f;
                mInPeak *= 0.97f;
                mOutPeak *= 0.97f;
                if (mInDisp > 0.002f || mOutDisp > 0.002f || mInPeak > 0.002f || mOutPeak > 0.002f)
                    active = true;
                if (active)
                    Invalidate();
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
                mInDisp = (float)value;
            if ((float)value > mInPeak)
                mInPeak = (float)value;
            return; // the tick loop drives the redraw
        }
        if (id == kOutputMeterId) {
            if ((float)value > mOutDisp)
                mOutDisp = (float)value;
            if ((float)value > mOutPeak)
                mOutPeak = (float)value;
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

    // Output-mode radio rows and the input-calibration fields inside the
    // settings overlay. Rects are computed the same way in drawSettings().
    BRect settingsPanel() const
    {
        return BRect(geo::kSettingsX, geo::kSettingsY, geo::kSettingsX + geo::kSettingsW,
                     geo::kSettingsY + geo::kSettingsH);
    }
    BRect settingsCloseBox() const
    {
        float r = geo::kSettingsX + geo::kSettingsW;
        return BRect(r - 24, geo::kSettingsY + 8, r - 8, geo::kSettingsY + 24);
    }
    BRect outputModeRow(int i) const
    {
        float x = geo::kSettingsX + 24, y = geo::kSettingsY + 74 + i * 26;
        return BRect(x, y, x + 200, y + 22);
    }
    BRect calibrateToggleRect() const
    {
        float x = geo::kSettingsX + 24, y = geo::kSettingsY + 176;
        return BRect(x, y, x + geo::kToggleW, y + geo::kToggleH);
    }
    BRect calValueRect() const
    {
        float x = geo::kSettingsX + 150, y = geo::kSettingsY + 176;
        return BRect(x, y, x + 90, y + 22);
    }

    void handleSettingsClick(BPoint where)
    {
        if (settingsCloseBox().Contains(where) || !settingsPanel().Contains(where)) {
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

        // Clear (x) box at the right end.
        BRect clearBox(row.x + row.w - geo::kClearGlyph - 6, row.y + (row.h - geo::kClearGlyph) / 2,
                       row.x + row.w - 6, row.y + (row.h + geo::kClearGlyph) / 2);
        if (loaded && clearBox.Contains(where)) {
            if (model)
                mController->setModelFile("");
            else
                mController->setIrFile("");
            Invalidate();
            return true;
        }

        // Otherwise open the file panel for this row.
        if (!mFilePanel)
            mFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, 0, false);
        mLoadTarget = model ? 0 : 1;
        mFilePanel->Show();
        return true;
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
        drawMeter(v, geo::kInputMeter, mInDisp, mInPeak, "IN");
        drawMeter(v, geo::kOutputMeter, mOutDisp, mOutPeak, "OUT");
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
        v->SetFont(be_bold_font);
        v->SetFontSize(22);
        v->SetHighColor(rgb(geo::kTextColor));
        const char *title = "NAMku";
        float w = v->StringWidth(title);
        v->DrawString(title, BPoint(geo::kTitleX + (geo::kTitleW - w) / 2.0f, geo::kTitleY + 34));
        v->SetFont(be_plain_font);
    }

    void drawKnob(BView *v, const geo::KnobSpec &k)
    {
        const double norm = mController->getParamNormalized(k.id);
        BRect face(k.cx - k.r, k.cy - k.r, k.cx + k.r, k.cy + k.r);
        if (mKnobFace)
            drawFitted(v, mKnobFace, face);
        else {
            v->SetDrawingMode(B_OP_OVER);
            v->SetHighColor(40, 40, 46);
            v->FillEllipse(face);
        }

        // Pointer: 0 = straight up, +/- kKnobSweepDeg/2 to the sides.
        const double ang = (-geo::kKnobSweepDeg / 2.0 + norm * geo::kKnobSweepDeg) * M_PI / 180.0;
        const float s = (float)std::sin(ang), c = (float)std::cos(ang);
        const float lo = k.r * 0.32f, hi = k.r * (float)geo::kKnobPointerFrac;
        BPoint base(k.cx + lo * s, k.cy - lo * c);
        BPoint tip(k.cx + hi * s, k.cy - hi * c);
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kAzure));
        v->SetPenSize(3.0f);
        v->StrokeLine(base, tip);
        v->SetPenSize(1.0f);
        v->FillEllipse(BRect(tip.x - 2.5f, tip.y - 2.5f, tip.x + 2.5f, tip.y + 2.5f));

        // Label and value.
        v->SetHighColor(rgb(geo::kTextColor));
        v->SetFontSize(12);
        float lw = v->StringWidth(k.label);
        v->DrawString(k.label, BPoint(k.cx - lw / 2.0f, k.cy + geo::kKnobLabelDY));

        Vst::String128 str = {0};
        char val[64] = "";
        if (mController->getParamStringByValue(k.id, norm, str) == kResultOk) {
            UString(str, 128).toAscii(val, sizeof(val));
            v->SetHighColor(rgb(geo::kDimColor));
            v->SetFontSize(11);
            float vw = v->StringWidth(val);
            v->DrawString(val, BPoint(k.cx - vw / 2.0f, k.cy + geo::kKnobValueDY));
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

        // Load glyph: a small folder at the left.
        drawFolderGlyph(v, BPoint(row.x + 8, row.y + row.h / 2.0f));

        // File name / placeholder.
        v->SetDrawingMode(B_OP_OVER);
        v->SetFontSize(12);
        const char *text = loaded ? baseName(path) : row.placeholder;
        v->SetHighColor(loaded ? rgb(geo::kTextColor) : rgb(geo::kDimColor));
        v->DrawString(text, BPoint(row.x + 30, row.y + row.h / 2.0f + 4));

        // Clear (x) glyph at the right, only when loaded.
        if (loaded) {
            float cx = row.x + row.w - geo::kClearGlyph / 2.0f - 6;
            float cy = row.y + row.h / 2.0f;
            float d = geo::kClearGlyph / 2.0f;
            v->SetHighColor(rgb(geo::kDimColor));
            v->SetPenSize(2.0f);
            v->StrokeLine(BPoint(cx - d, cy - d), BPoint(cx + d, cy + d));
            v->StrokeLine(BPoint(cx - d, cy + d), BPoint(cx + d, cy - d));
            v->SetPenSize(1.0f);
        }
    }

    void drawFolderGlyph(BView *v, BPoint c)
    {
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kDimColor));
        v->SetPenSize(1.0f);
        BRect body(c.x, c.y - 4, c.x + 16, c.y + 6);
        v->StrokeRoundRect(body, 2, 2);
        v->StrokeLine(BPoint(c.x + 2, c.y - 4), BPoint(c.x + 6, c.y - 7));
        v->StrokeLine(BPoint(c.x + 6, c.y - 7), BPoint(c.x + 9, c.y - 4));
    }

    void drawMeter(BView *v, const geo::MeterRect &m, float level, float peak, const char *label)
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
        const float inset = 3.0f;
        float clamped = level < 0 ? 0 : (level > 1 ? 1 : level);
        float fillH = clamped * (m.h - 2 * inset);
        if (fillH > 0.5f) {
            BRect fill(m.x + inset, m.y + m.h - inset - fillH, m.x + m.w - inset,
                       m.y + m.h - inset);
            v->SetHighColor(rgb(geo::kAzure));
            v->FillRect(fill);
        }
        // Peak marker.
        float pk = peak < 0 ? 0 : (peak > 1 ? 1 : peak);
        if (pk > 0.01f) {
            float py = m.y + m.h - inset - pk * (m.h - 2 * inset);
            v->SetHighColor(rgb(geo::kPeakColor));
            v->FillRect(BRect(m.x + inset, py - 1, m.x + m.w - inset, py + 1));
        }

        v->SetHighColor(rgb(geo::kDimColor));
        v->SetFontSize(10);
        float lw = v->StringWidth(label);
        v->DrawString(label, BPoint(m.x + (m.w - lw) / 2.0f, m.y + m.h + 14));
    }

    void drawGear(BView *v)
    {
        // Simple gear glyph (Phase 4 wires the settings page to it).
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kDimColor));
        v->SetPenSize(2.0f);
        float r = geo::kGearR;
        v->StrokeEllipse(BPoint(geo::kGearCX, geo::kGearCY), r, r);
        v->StrokeEllipse(BPoint(geo::kGearCX, geo::kGearCY), r * 0.45f, r * 0.45f);
        for (int i = 0; i < 8; ++i) {
            float a = (float)(i * M_PI / 4.0);
            float s = std::sin(a), c = std::cos(a);
            v->StrokeLine(BPoint(geo::kGearCX + r * s, geo::kGearCY - r * c),
                          BPoint(geo::kGearCX + (r + 3) * s, geo::kGearCY - (r + 3) * c));
        }
        v->SetPenSize(1.0f);
    }

    void drawSlimIcon(BView *v)
    {
        BRect r(geo::kSlimIconX, geo::kSlimIconY, geo::kSlimIconX + geo::kSlimIconS,
                geo::kSlimIconY + geo::kSlimIconS);
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kAzure));
        v->StrokeRoundRect(r, 4, 4);
        v->SetFontSize(13);
        float w = v->StringWidth("S");
        v->DrawString("S", BPoint(r.left + (geo::kSlimIconS - w) / 2.0f, r.top + 16));
    }

    // Draw the face bitmap + azure pointer for a knob-like control.
    void drawKnobFace(BView *v, double norm, int cx, int cy, int r)
    {
        BRect face(cx - r, cy - r, cx + r, cy + r);
        if (mKnobFace)
            drawFitted(v, mKnobFace, face);
        else {
            v->SetDrawingMode(B_OP_OVER);
            v->SetHighColor(40, 40, 46);
            v->FillEllipse(face);
        }
        const double ang = (-geo::kKnobSweepDeg / 2.0 + norm * geo::kKnobSweepDeg) * M_PI / 180.0;
        const float s = (float)std::sin(ang), c = (float)std::cos(ang);
        const float lo = r * 0.32f, hi = r * (float)geo::kKnobPointerFrac;
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kAzure));
        v->SetPenSize(3.0f);
        v->StrokeLine(BPoint(cx + lo * s, cy - lo * c), BPoint(cx + hi * s, cy - hi * c));
        v->SetPenSize(1.0f);
        float tx = cx + hi * s, ty = cy - hi * c;
        v->FillEllipse(BRect(tx - 2.5f, ty - 2.5f, tx + 2.5f, ty + 2.5f));
    }

    void dimPanel(BView *v)
    {
        v->SetDrawingMode(B_OP_ALPHA);
        v->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
        v->SetHighColor(0, 0, 0, 170);
        v->FillRect(v->Bounds());
    }

    void drawSettings(BView *v)
    {
        dimPanel(v);
        BRect panel = settingsPanel();
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(0x232028));
        v->FillRoundRect(panel, 8, 8);
        v->SetHighColor(rgb(0x3A3742));
        v->StrokeRoundRect(panel, 8, 8);

        // Title + close.
        v->SetHighColor(rgb(geo::kTextColor));
        v->SetFont(be_bold_font);
        v->SetFontSize(16);
        v->DrawString("SETTINGS", BPoint(panel.left + 24, panel.top + 28));
        v->SetFont(be_plain_font);
        BRect close = settingsCloseBox();
        v->SetHighColor(rgb(geo::kDimColor));
        v->SetPenSize(2.0f);
        v->StrokeLine(BPoint(close.left, close.top), BPoint(close.right, close.bottom));
        v->StrokeLine(BPoint(close.left, close.bottom), BPoint(close.right, close.top));
        v->SetPenSize(1.0f);

        // Output mode radios.
        v->SetFontSize(12);
        v->SetHighColor(rgb(geo::kDimColor));
        v->DrawString("Output Mode", BPoint(panel.left + 24, panel.top + 58));
        const char *modes[3] = {"Raw", "Normalized", "Calibrated"};
        int cur = (int)(mController->getParamNormalized(kOutputModeId) * 2.0 + 0.5);
        for (int i = 0; i < 3; ++i) {
            BRect row = outputModeRow(i);
            const bool gated = (i > 0 && !mHasOutputLevel);
            BPoint dot(row.left + 8, row.top + 11);
            v->SetHighColor(gated ? rgb(0x4A4750) : rgb(geo::kTextColor));
            v->StrokeEllipse(dot, 6, 6);
            if (i == cur) {
                v->SetHighColor(rgb(geo::kAzure));
                v->FillEllipse(dot, 3, 3);
            }
            v->SetHighColor(gated ? rgb(0x5A5760) : rgb(geo::kTextColor));
            v->DrawString(modes[i], BPoint(row.left + 22, row.top + 16));
        }

        // Input calibration.
        BRect tog = calibrateToggleRect();
        const bool calOn = mController->getParamNormalized(kCalibrateInputId) > 0.5;
        v->SetHighColor(mHasInputLevel ? rgb(geo::kDimColor) : rgb(0x4A4750));
        v->DrawString("Input Calibration", BPoint(panel.left + 24, tog.top - 8));
        v->SetHighColor(!mHasInputLevel ? rgb(0x2A2730)
                                        : (calOn ? rgb(geo::kAzure) : rgb(0x2A2730)));
        v->FillRoundRect(tog, geo::kToggleH / 2, geo::kToggleH / 2);
        const int hs = 18;
        float hx = calOn ? tog.right - hs - 2 : tog.left + 2;
        BRect handle(hx, tog.top + 2, hx + hs, tog.top + 2 + hs);
        if (mSwitchHandle)
            drawFitted(v, mSwitchHandle, handle);

        // Calibration level value (drag to edit).
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
        v->SetHighColor(mHasInputLevel ? rgb(geo::kTextColor) : rgb(0x5A5760));
        v->DrawString(buf, BPoint(val.left + 10, val.top + 16));

        // About.
        v->SetHighColor(rgb(geo::kDimColor));
        v->SetFontSize(11);
        v->DrawString("NAMku — Neural Amp Modeler for Haiku",
                      BPoint(panel.left + 24, panel.bottom - 44));
        v->DrawString("DSP by Steven Atkinson (MIT). Raw VST3, no JUCE.",
                      BPoint(panel.left + 24, panel.bottom - 26));
    }

    void drawSlimOverlay(BView *v)
    {
        dimPanel(v);
        const double norm = mController->getParamNormalized(kSlimId);
        drawKnobFace(v, norm, geo::kSlimKnobCX, geo::kSlimKnobCY, geo::kSlimKnobR);
        v->SetDrawingMode(B_OP_OVER);
        v->SetHighColor(rgb(geo::kTextColor));
        v->SetFont(be_bold_font);
        v->SetFontSize(15);
        float w = v->StringWidth("Slim");
        v->DrawString("Slim",
                      BPoint(geo::kSlimKnobCX - w / 2.0f, geo::kSlimKnobCY - geo::kSlimKnobR - 12));
        v->SetFont(be_plain_font);
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

    BBitmap *mBackground = nullptr;
    BBitmap *mLines = nullptr;
    BBitmap *mKnobFace = nullptr;
    BBitmap *mFileBg = nullptr;
    BBitmap *mMeterBg = nullptr;
    BBitmap *mSwitchHandle = nullptr;
    BBitmap *mInputBg = nullptr;

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
