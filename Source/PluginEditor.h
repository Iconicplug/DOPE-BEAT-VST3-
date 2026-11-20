#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace ui
{
constexpr float IMG_W = 1536.f, IMG_H = 1024.f;
inline juce::Colour neon()   { return juce::Colour (0xff39ff14); }
inline juce::Colour violet() { return juce::Colour (0xffb347ff); }

inline juce::Font fnt (float h) { return juce::Font (juce::FontOptions (h, juce::Font::bold)); }

inline void neonText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h,
                      juce::Justification j = juce::Justification::centred, juce::Colour c = neon(), bool glow = true)
{
    g.setFont (fnt (h));
    if (glow)
    {
        g.setColour (c.withAlpha (0.22f));
        const float offs[4][2] = { { -1.2f, 0.f }, { 1.2f, 0.f }, { 0.f, -1.2f }, { 0.f, 1.2f } };
        for (auto& o : offs)
            g.drawText (t, r.translated (o[0], o[1]), j, false);
    }
    g.setColour (c);
    g.drawText (t, r, j, false);
}

inline void setParam (juce::RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}
}

class NeonLnF : public juce::LookAndFeel_V4
{
public:
    NeonLnF()
    {
        setColour (juce::ComboBox::textColourId, ui::neon());
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0a0a12));
        setColour (juce::PopupMenu::textColourId, ui::neon());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff6a00ff));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        juce::Path p; const float cx = (float) w - 14.f, cy = (float) h * 0.5f;
        p.addTriangle (cx - 5, cy - 3, cx + 5, cy - 3, cx, cy + 4);
        g.setColour (ui::neon()); g.fillPath (p);
    }
    juce::Font getComboBoxFont (juce::ComboBox& cb) override { return ui::fnt ((float) cb.getHeight() * 0.48f); }
    void positionComboBoxText (juce::ComboBox& cb, juce::Label& l) override
    {
        l.setBounds (8, 0, cb.getWidth() - 28, cb.getHeight());
        l.setFont (getComboBoxFont (cb));
    }
    juce::Font getPopupMenuFont() override { return ui::fnt (15.f); }
};

class KnobLnF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override
    {
        auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
        const float R = (float) juce::jmin (w, h) * 0.5f;
        g.setColour (juce::Colour (0xff0c0c12));
        g.fillEllipse (juce::Rectangle<float> (R * 1.24f, R * 1.24f).withCentre (c));

        juce::Path track; track.addCentredArc (c.x, c.y, R * 0.88f, R * 0.88f, 0.f, a0, a1, true);
        g.setColour (juce::Colour (0xff1c0535));
        g.strokePath (track, juce::PathStrokeType (R * 0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        const float ang = a0 + pos * (a1 - a0);
        juce::Path val; val.addCentredArc (c.x, c.y, R * 0.88f, R * 0.88f, 0.f, a0, ang, true);
        g.setColour (ui::neon().withAlpha (0.28f));
        g.strokePath (val, juce::PathStrokeType (R * 0.26f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setColour (ui::neon());
        g.strokePath (val, juce::PathStrokeType (R * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        const float dx = std::sin (ang), dy = -std::cos (ang);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (R * 0.2f, R * 0.2f).withCentre ({ c.x + dx * R * 0.88f, c.y + dy * R * 0.88f }));

        ui::neonText (g, juce::String ((int) std::round (pos * 100.f)), juce::Rectangle<float> (R * 1.1f, R * 0.5f).withCentre (c),
                      R * 0.42f);
    }
};

// Rejilla de slots (Time o Volume) sobre las casillas de la imagen
class SlotGrid : public juce::Component
{
public:
    SlotGrid (DopeBeatProcessor& p, bool isTimeGrid) : proc (p), isTime (isTimeGrid)
    {
        rows = isTime ? 7 : 8;
        rowY0 = isTime ? 201.f : 485.f;
        pitch = isTime ? 36.33f : 36.14f;
    }
    void geom (float rx, float ry, float sc) { regionX = rx; regionY = ry; s = sc; }

    juce::Rectangle<float> cell (int col, int row) const
    {
        static const float cx[4] = { 30.f, 157.f, 284.f, 412.f };
        return { (cx[col] - regionX) * s, (rowY0 + (float) row * pitch - regionY) * s, 113.f * s, 27.f * s };
    }
    int slotAt (juce::Point<float> p) const
    {
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < 4; ++c)
                if (cell (c, r).contains (p)) return r * 4 + c;
        return -1;
    }
    int current() const { return isTime ? proc.timeSlot.load() : proc.volSlot.load(); }

    void paint (juce::Graphics& g) override
    {
        const int cur = current();
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const int idx = r * 4 + c;
                auto rc = cell (c, r);
                const bool sel = cur == idx + 1;
                if (sel)
                {
                    g.setColour (ui::neon().withAlpha (0.16f)); g.fillRoundedRectangle (rc, 4.f * s);
                    g.setColour (ui::neon());                   g.drawRoundedRectangle (rc.reduced (1.f), 4.f * s, 1.6f * s);
                }
                else if (idx == hover)
                {
                    g.setColour (ui::violet().withAlpha (0.28f)); g.fillRoundedRectangle (rc, 4.f * s);
                }
                const juce::String name = isTime ? db::timeName (idx) : db::volName (idx);
                auto tr = rc.withTrimmedLeft (7.f * s).withTrimmedRight (20.f * s);
                g.setFont (ui::fnt (13.f * s));
                g.setColour (ui::neon().withAlpha (sel ? 1.f : 0.9f));
                g.drawFittedText (name, tr.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            }
    }
    void mouseMove (const juce::MouseEvent& e) override { int h = slotAt (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = slotAt (e.position);
        if (idx < 0) return;
        auto& slot = isTime ? proc.timeSlot : proc.volSlot;
        if (e.mods.isPopupMenu()) { proc.resetSlot (isTime, idx); slot.store (idx + 1); }
        else slot.store (slot.load() == idx + 1 ? 0 : idx + 1);
        repaint();
    }

private:
    DopeBeatProcessor& proc;
    bool isTime; int rows; float rowY0, pitch, regionX = 0, regionY = 0, s = 1.f; int hover = -1;
};

// Botones con icono: power / dado / lupa
class IconButton : public juce::Component
{
public:
    enum Type { Power, Dice, Lupa };
    IconButton (Type t) : type (t) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    std::function<bool()> isOn;
    juce::Point<float> iconCentre { 0.5f, 0.5f };   // proporcion del bounds
    void paint (juce::Graphics& g) override
    {
        const bool on = isOn ? isOn() : true;
        auto b = getLocalBounds().toFloat();
        auto c = juce::Point<float> (b.getX() + b.getWidth() * iconCentre.x, b.getY() + b.getHeight() * iconCentre.y);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.3f;
        const auto col = on ? ui::neon() : juce::Colour (0xff5a5a66);
        if (type == Power)
        {
            if (! on) { g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillEllipse (juce::Rectangle<float> (b.getHeight() * 0.95f, b.getHeight() * 0.95f).withCentre (c)); }
            juce::Path p; p.addCentredArc (c.x, c.y, r, r, 0.f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
            p.startNewSubPath (c.x, c.y - r * 1.15f); p.lineTo (c.x, c.y - r * 0.1f);
            g.setColour (col.withAlpha (0.3f)); g.strokePath (p, juce::PathStrokeType (r * 0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (col);                  g.strokePath (p, juce::PathStrokeType (r * 0.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (type == Dice)
        {
            auto sq = juce::Rectangle<float> (r * 2.1f, r * 2.1f).withCentre (c);
            g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (sq.expanded (2.f), r * 0.45f);
            g.setColour (col); g.drawRoundedRectangle (sq, r * 0.4f, r * 0.17f);
            const float pips[5][2] = { { -0.55f, -0.55f }, { 0.55f, 0.55f }, { 0.f, 0.f }, { -0.55f, 0.55f }, { 0.55f, -0.55f } };
            for (auto& o : pips)
                g.fillEllipse (juce::Rectangle<float> (r * 0.3f, r * 0.3f).withCentre ({ c.x + o[0] * r, c.y + o[1] * r }));
        }
        else if (on)
        {
            g.setColour (ui::neon().withAlpha (0.3f)); g.fillEllipse (juce::Rectangle<float> (r * 3.6f, r * 3.6f).withCentre (c));
            g.setColour (ui::neon()); g.drawEllipse (juce::Rectangle<float> (r * 3.6f, r * 3.6f).withCentre (c), 2.f);
        }
    }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); repaint(); }
private:
    Type type;
};

// Pad XY: X = filtro (cutoff), Y = saturacion / bitcrush
class XYPad : public juce::Component
{
public:
    XYPad (DopeBeatProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }
    float s = 1.f;
    void paint (juce::Graphics& g) override
    {
        const float x = *proc.apvts.getRawParameterValue ("xyX"), y = *proc.apvts.getRawParameterValue ("xyY");
        auto b = getLocalBounds().toFloat();
        const float px = b.getX() + x * b.getWidth(), py = b.getBottom() - y * b.getHeight();
        g.setColour (ui::neon().withAlpha (0.35f));
        g.drawLine (px, b.getY(), px, b.getBottom(), 1.f); g.drawLine (b.getX(), py, b.getRight(), py, 1.f);
        g.setColour (ui::neon().withAlpha (0.25f)); g.fillEllipse (juce::Rectangle<float> (26.f * s, 26.f * s).withCentre ({ px, py }));
        g.setColour (ui::neon()); g.fillEllipse (juce::Rectangle<float> (11.f * s, 11.f * s).withCentre ({ px, py }));
        g.setColour (juce::Colours::white); g.fillEllipse (juce::Rectangle<float> (4.f * s, 4.f * s).withCentre ({ px, py }));
        ui::neonText (g, "FILTER", b.removeFromBottom (14.f * s), 9.f * s, juce::Justification::centredLeft, ui::neon().withAlpha (0.8f), false);
        ui::neonText (g, "SAT/CRUSH", getLocalBounds().toFloat().removeFromTop (14.f * s), 9.f * s, juce::Justification::centredRight, ui::neon().withAlpha (0.8f), false);
    }
    void mouseDown (const juce::MouseEvent& e) override { proc.apvts.getParameter ("xyX")->beginChangeGesture(); proc.apvts.getParameter ("xyY")->beginChangeGesture(); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float nx = juce::jlimit (0.f, 1.f, e.position.x / (float) getWidth());
        const float ny = juce::jlimit (0.f, 1.f, 1.f - e.position.y / (float) getHeight());
        proc.apvts.getParameter ("xyX")->setValueNotifyingHost (nx);
        proc.apvts.getParameter ("xyY")->setValueNotifyingHost (ny);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { proc.apvts.getParameter ("xyX")->endChangeGesture(); proc.apvts.getParameter ("xyY")->endChangeGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        ui::setParam (proc.apvts.getParameter ("xyX"), 1.f); ui::setParam (proc.apvts.getParameter ("xyY"), 0.f);
    }
private:
    DopeBeatProcessor& proc;
};

// Pantalla de modo (izquierda) y BPM / patron (derecha) de la barra negra superior
class InfoDisplay : public juce::Component
{
public:
    InfoDisplay (DopeBeatProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    float s = 1.f, regionX = 15.f, regionY = 115.f;
    juce::Rectangle<float> modeRect() const { return { (45.f - regionX) * s, (122.f - regionY) * s, 193.f * s, 54.f * s }; }
    juce::Rectangle<float> bpmRect()  const { return { (245.f - regionX) * s, (122.f - regionY) * s, 120.f * s, 54.f * s }; }

    void paint (juce::Graphics& g) override
    {
        const int mode = (int) *proc.apvts.getRawParameterValue ("mode");
        static const char* names[3] = { "LOOP", "MIDI TRIGGER", "AUDIO TRANSIENT" };
        auto m = modeRect();
        juce::Path tri; tri.addTriangle (m.getX() + 4.f * s, m.getCentreY() - 9.f * s, m.getX() + 4.f * s, m.getCentreY() + 9.f * s, m.getX() + 20.f * s, m.getCentreY());
        g.setColour (ui::neon().withAlpha (proc.activeNow.load() ? 1.f : 0.45f)); g.fillPath (tri);
        auto tx = m.withTrimmedLeft (28.f * s);
        ui::neonText (g, names[juce::jlimit (0, 2, mode)], tx.removeFromTop (tx.getHeight() * 0.5f), 17.f * s, juce::Justification::centredLeft);
        const int ts = proc.timeSlot.load(), vs = proc.volSlot.load();
        juce::String pat = ts > 0 ? juce::String (db::timeName (ts - 1)) : juce::String ("-");
        if (vs > 0) pat << " / " << db::volName (vs - 1);
        ui::neonText (g, pat, tx, 11.f * s, juce::Justification::centredLeft, ui::neon().withAlpha (0.85f), false);

        auto b = bpmRect();
        const bool sync = *proc.apvts.getRawParameterValue ("sync") > 0.5f;
        const float bpm = sync ? proc.bpmNow.load() : (float) *proc.apvts.getRawParameterValue ("freeBpm");
        ui::neonText (g, juce::String (bpm, 1), b.removeFromTop (b.getHeight() * 0.62f), 22.f * s);
        ui::neonText (g, sync ? (proc.hostSynced.load() ? "SYNC HOST" : "SYNC (NO HOST)") : "FREE BPM", b, 10.f * s, juce::Justification::centred, ui::neon().withAlpha (0.85f), false);
        g.setColour (sync ? ui::neon() : juce::Colour (0xff5a5a66));
        g.fillEllipse (juce::Rectangle<float> (8.f * s, 8.f * s).withCentre ({ 15.f * s, 35.f * s }));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        downY = e.position.y; dragged = false; startBpm = *proc.apvts.getRawParameterValue ("freeBpm");
        if (modeRect().contains (e.position))
        {
            auto* par = proc.apvts.getParameter ("mode");
            const int cur = (int) *proc.apvts.getRawParameterValue ("mode");
            ui::setParam (par, (float) ((cur + 1) % 3));
        }
        onBpm = bpmRect().contains (e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! onBpm) return;
        const bool sync = *proc.apvts.getRawParameterValue ("sync") > 0.5f;
        if (sync) return;
        if (std::abs (e.position.y - downY) > 3.f) dragged = true;
        ui::setParam (proc.apvts.getParameter ("freeBpm"), juce::jlimit (40.f, 240.f, startBpm + (downY - e.position.y) * 0.5f));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (onBpm && ! dragged)
            ui::setParam (proc.apvts.getParameter ("sync"), *proc.apvts.getRawParameterValue ("sync") > 0.5f ? 0.f : 1.f);
    }
private:
    DopeBeatProcessor& proc; float downY = 0, startBpm = 120; bool dragged = false, onBpm = false;
};

// Medidor E/S + fase del ciclo (pildora inferior izquierda)
class MeterBar : public juce::Component
{
public:
    MeterBar (DopeBeatProcessor& p) : proc (p) {}
    float s = 1.f;
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float lab = 34.f * s, rowH = b.getHeight() / 3.f;
        auto bar = [&] (const char* name, float v, juce::Colour c)
        {
            auto row = b.removeFromTop (rowH).reduced (0.f, 2.f * s);
            ui::neonText (g, name, row.removeFromLeft (lab), 9.f * s, juce::Justification::centredLeft, ui::neon(), false);
            g.setColour (juce::Colour (0xff15151c)); g.fillRoundedRectangle (row, 2.f * s);
            g.setColour (c); g.fillRoundedRectangle (row.withWidth (row.getWidth() * juce::jlimit (0.f, 1.f, v)), 2.f * s);
        };
        bar ("IN",  std::pow (proc.inLevel.load(), 0.5f),  ui::violet());
        bar ("OUT", std::pow (proc.outLevel.load(), 0.5f), ui::neon());
        bar ("PH",  proc.phaseNow.load(), juce::Colour (0xff00e5a0));
    }
private:
    DopeBeatProcessor& proc;
};

// Panel derecho: mapa de calor inmersivo + osciloscopio + editor de curvas
class VisualPanel : public juce::Component
{
public:
    explicit VisualPanel (DopeBeatProcessor& p);
    void setScale (float sc) { s = sc; }
    void update();
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragIdx = -1; }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool editTime = true, focus = false;

private:
    juce::Rectangle<float> plot() const { return { 16.f * s, 52.f * s, (float) getWidth() - 32.f * s, (float) getHeight() - 52.f * s - 24.f * s }; }
    juce::Point<float> toScreen (const db::Node& n, bool isTime) const;
    int hitNode (juce::Point<float> pos) const;
    db::Curve& cur() { return editTime ? tc : vc; }
    int curSlot() const { return editTime ? proc.timeSlot.load() : proc.volSlot.load(); }
    void pushCur() { proc.setCurve (editTime, curSlot() - 1, cur()); }
    void drawCurve (juce::Graphics& g, const db::Curve& c, bool isTime, juce::Colour col, float alpha, bool nodes);

    DopeBeatProcessor& proc;
    db::Curve tc, vc;
    float s = 1.f, phase = 0.f;
    int gridX = 16, gridY = 16, dragIdx = -1;
    bool snapOn = true;
};

class DopeBeatEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DopeBeatEditor (DopeBeatProcessor&);
    ~DopeBeatEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintContent (juce::Graphics&);

    DopeBeatProcessor& proc;
    NeonLnF lnf;
    KnobLnF knobLnf;
    juce::Image bg;
    float s = 1.f;   // todo se maqueta en coordenadas de diseno 1536x1024 y se escala con una transformacion
    struct Content : public juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    } content;

    SlotGrid timeGrid { proc, true }, volGrid { proc, false };
    IconButton bypassBtn { IconButton::Power }, randBtn { IconButton::Dice }, lupaBtn { IconButton::Lupa };
    XYPad xy { proc };
    InfoDisplay info { proc };
    MeterBar meter { proc };
    VisualPanel panel { proc };
    juce::Slider kTime, kVol, kSmooth;
    juce::ComboBox cycleBox, snapBox, algoBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aTime, aVol, aSmooth;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aCycle, aSnap, aAlgo;
    int lastT = -1, lastV = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopeBeatEditor)
};
