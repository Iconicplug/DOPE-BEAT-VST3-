#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

// =====================================================================================
//  VisualPanel
// =====================================================================================
VisualPanel::VisualPanel (DopeBeatProcessor& p) : proc (p) {}

void VisualPanel::update()
{
    const int ts = proc.timeSlot.load(), vs = proc.volSlot.load();
    if (dragIdx < 0)
    {
        if (ts > 0) tc = proc.getCurve (true, ts - 1);
        if (vs > 0) vc = proc.getCurve (false, vs - 1);
    }
    phase = proc.phaseNow.load();

    static const float sb[5] = { 1.f, 0.5f, 0.25f, 1.f / 3.f, 0.f };
    const float snapBeats = sb[jlimit (0, 4, (int) *proc.apvts.getRawParameterValue ("snap"))];
    snapOn = snapBeats > 0.f;
    gridX = snapOn ? jlimit (2, 64, (int) std::round (proc.cycleBeatsNow.load() / snapBeats)) : 16;
    gridY = jlimit (4, 32, gridX);
    repaint();
}

Point<float> VisualPanel::toScreen (const db::Node& n, bool isTime) const
{
    auto pl = plot();
    return { pl.getX() + n.x * pl.getWidth(), isTime ? pl.getY() + n.y * pl.getHeight() : pl.getBottom() - n.y * pl.getHeight() };
}

void VisualPanel::drawCurve (Graphics& g, const db::Curve& c, bool isTime, Colour col, float alpha, bool nodes)
{
    Path path;
    for (int i = 0; i < c.n; ++i)
    {
        const auto pt = toScreen (c.nd[i], isTime);
        if (i == 0) { path.startNewSubPath (pt); continue; }
        const auto& a = c.nd[i - 1];
        if (a.t != 0.f && c.nd[i].x > a.x)
        {
            const float k = std::pow (4.f, a.t);
            for (int j = 1; j <= 14; ++j)
            {
                const float u = (float) j / 14.f;
                path.lineTo (toScreen (db::Node { a.x + (c.nd[i].x - a.x) * u, a.y + (c.nd[i].y - a.y) * std::pow (u, k), 0.f }, isTime));
            }
        }
        else path.lineTo (pt);
    }
    auto pl = plot();
    if (! isTime)
    {
        Path f (path);
        f.lineTo (pl.getRight(), pl.getBottom()); f.lineTo (pl.getX(), pl.getBottom()); f.closeSubPath();
        g.setGradientFill (ColourGradient (col.withAlpha (0.28f * alpha), 0.f, pl.getY(), col.withAlpha (0.f), 0.f, pl.getBottom(), false));
        g.fillPath (f);
    }
    g.setColour (col.withAlpha (0.22f * alpha));
    g.strokePath (path, PathStrokeType (9.f * s, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (col.withAlpha (alpha));
    g.strokePath (path, PathStrokeType (2.6f * s, PathStrokeType::curved, PathStrokeType::rounded));

    if (nodes)
        for (int i = 0; i < c.n; ++i)
        {
            const auto pt = toScreen (c.nd[i], isTime);
            const float r = 6.5f * s;
            g.setColour (col.withAlpha (0.28f)); g.fillEllipse (Rectangle<float> (r * 3.f, r * 3.f).withCentre (pt));
            g.setColour (Colour (0xff08080c));  g.fillEllipse (Rectangle<float> (r * 2.f, r * 2.f).withCentre (pt));
            g.setColour (i == dragIdx ? Colours::white : col); g.drawEllipse (Rectangle<float> (r * 2.f, r * 2.f).withCentre (pt), 2.2f * s);
        }
}

void VisualPanel::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    Path clip; clip.addRoundedRectangle (b, 24.f * s);
    g.saveState();
    g.reduceClipRegion (clip);
    g.setColour (Colour (0xff040406)); g.fillRect (b);

    auto pl = plot();

    // osciloscopio: audio entrante por posicion de fase
    {
        const float mid = pl.getCentreY();
        g.setColour (Colour (0xff8c8c96).withAlpha (0.30f));
        for (int i = 0; i < 256; ++i)
        {
            const float a = jlimit (0.f, 1.f, std::pow (proc.waveCols[(size_t) i].load(), 0.6f) * 1.2f);
            const float x = pl.getX() + ((float) i + 0.5f) / 256.f * pl.getWidth(), h = a * pl.getHeight() * 0.46f;
            g.drawLine (x, mid - h, x, mid + h, jmax (1.f, pl.getWidth() / 256.f));
        }
    }

    // rejilla cuantizada
    for (int i = 0; i <= gridX; ++i)
    {
        const float x = pl.getX() + (float) i / (float) gridX * pl.getWidth();
        g.setColour (Colours::white.withAlpha ((i % 4 == 0) ? 0.22f : 0.09f));
        g.drawLine (x, pl.getY(), x, pl.getBottom(), 1.f);
    }
    for (int j = 0; j <= gridY; ++j)
    {
        const float y = pl.getY() + (float) j / (float) gridY * pl.getHeight();
        g.setColour (Colours::white.withAlpha ((j % 4 == 0) ? 0.16f : 0.06f));
        g.drawLine (pl.getX(), y, pl.getRight(), y, 1.f);
    }
    g.setColour (ui::neon().withAlpha (0.5f)); g.drawRoundedRectangle (pl, 8.f * s, 1.2f * s);

    const int ts = proc.timeSlot.load(), vs = proc.volSlot.load();
    const auto tCol = ui::neon(), vCol = Colour (0xffc040ff);
    const bool showV = vs > 0 && (! focus || ! editTime);
    const bool showT = ts > 0 && (! focus || editTime);
    if (showV) drawCurve (g, vc, false, vCol, editTime ? 0.6f : 1.f, ! editTime);
    if (showT) drawCurve (g, tc, true, tCol, editTime ? 1.f : 0.6f, editTime);

    // cabezal de reproduccion
    {
        const float x = pl.getX() + phase * pl.getWidth();
        g.setColour (Colours::white.withAlpha (0.18f)); g.drawLine (x, pl.getY(), x, pl.getBottom(), 7.f * s);
        g.setColour (Colours::white.withAlpha (0.9f));  g.drawLine (x, pl.getY(), x, pl.getBottom(), 1.6f * s);
        if (showT)
        {
            const auto pt = toScreen (db::Node { phase, tc.eval (phase), 0.f }, true);
            g.setColour (tCol); g.fillEllipse (Rectangle<float> (10.f * s, 10.f * s).withCentre (pt));
        }
        if (showV)
        {
            const auto pt = toScreen (db::Node { phase, vc.eval (phase), 0.f }, false);
            g.setColour (vCol); g.fillEllipse (Rectangle<float> (10.f * s, 10.f * s).withCentre (pt));
        }
    }

    // etiquetas de ejes
    ui::neonText (g, "NOW", Rectangle<float> (pl.getX() + 6.f * s, pl.getY() + 2.f * s, 60.f * s, 14.f * s), 10.f * s, Justification::centredLeft, ui::neon().withAlpha (0.8f), false);
    ui::neonText (g, "-CICLO", Rectangle<float> (pl.getX() + 6.f * s, pl.getBottom() - 16.f * s, 80.f * s, 14.f * s), 10.f * s, Justification::centredLeft, ui::neon().withAlpha (0.8f), false);
    ui::neonText (g, "100%", Rectangle<float> (pl.getRight() - 66.f * s, pl.getY() + 2.f * s, 60.f * s, 14.f * s), 10.f * s, Justification::centredRight, Colour (0xffc040ff), false);
    ui::neonText (g, "0%", Rectangle<float> (pl.getRight() - 66.f * s, pl.getBottom() - 16.f * s, 60.f * s, 14.f * s), 10.f * s, Justification::centredRight, Colour (0xffc040ff), false);
    ui::neonText (g, "DOBLE CLIC: NODO   CLIC DER: BORRAR   RUEDA: CURVATURA   ARRASTRAR: MOVER",
                  Rectangle<float> (pl.getX(), pl.getBottom() + 4.f * s, pl.getWidth(), 16.f * s), 10.f * s, Justification::centred, ui::neon().withAlpha (0.65f), false);

    if (curSlot() == 0)
        ui::neonText (g, editTime ? "SELECCIONA UN SLOT DE TIEMPO" : "SELECCIONA UN SLOT DE VOLUMEN", pl, 20.f * s);

    // cabecera
    auto btn = [&] (Rectangle<float> r, const String& t, bool on, Colour c)
    {
        g.setColour (on ? c.withAlpha (0.22f) : Colour (0x66000000)); g.fillRoundedRectangle (r, 6.f * s);
        g.setColour (on ? c : c.withAlpha (0.45f)); g.drawRoundedRectangle (r, 6.f * s, (on ? 1.8f : 1.f) * s);
        ui::neonText (g, t, r, 13.f * s, Justification::centred, c.withAlpha (on ? 1.f : 0.6f), on);
    };
    btn ({ 16.f * s, 10.f * s, 150.f * s, 28.f * s }, "TIME CURVE", editTime, tCol);
    btn ({ 172.f * s, 10.f * s, 170.f * s, 28.f * s }, "VOLUME CURVE", ! editTime, vCol);
    String info = String ("CICLO ") + String (proc.cycleBeatsNow.load(), 2) + " BEATS   GRID " + String (gridX) + (snapOn ? "" : " (LIBRE)");
    ui::neonText (g, info, Rectangle<float> (b.getWidth() - 420.f * s, 10.f * s, 404.f * s, 28.f * s), 12.f * s, Justification::centredRight, ui::neon().withAlpha (0.9f), false);
    g.restoreState();
}

int VisualPanel::hitNode (Point<float> pos) const
{
    if (curSlot() == 0) return -1;
    const auto& c = editTime ? tc : vc;
    int best = -1; float bd = 12.f * s;
    for (int i = 0; i < c.n; ++i)
    {
        const float d = toScreen (c.nd[i], editTime).getDistanceFrom (pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void VisualPanel::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;
    if (p.y < 46.f * s)
    {
        if (Rectangle<float> (16.f * s, 10.f * s, 150.f * s, 28.f * s).contains (p)) editTime = true;
        else if (Rectangle<float> (172.f * s, 10.f * s, 170.f * s, 28.f * s).contains (p)) editTime = false;
        return;
    }
    if (curSlot() == 0) return;
    const int i = hitNode (p);
    if (i < 0) return;
    if (e.mods.isPopupMenu())
    {
        auto& c = cur();
        if (i > 0 && i < c.n - 1) { for (int k = i; k < c.n - 1; ++k) c.nd[k] = c.nd[k + 1]; --c.n; pushCur(); }
        return;
    }
    dragIdx = i;
}

void VisualPanel::mouseDrag (const MouseEvent& e)
{
    if (dragIdx < 0 || curSlot() == 0) return;
    auto& c = cur();
    if (dragIdx >= c.n) { dragIdx = -1; return; }
    auto pl = plot();
    float nx = jlimit (0.f, 1.f, (e.position.x - pl.getX()) / pl.getWidth());
    float ny = jlimit (0.f, 1.f, (e.position.y - pl.getY()) / pl.getHeight());
    float v = editTime ? ny : 1.f - ny;
    if (snapOn && ! e.mods.isAltDown())
    {
        nx = std::round (nx * (float) gridX) / (float) gridX;
        v  = std::round (v * (float) gridY) / (float) gridY;
    }
    if (dragIdx == 0) nx = 0.f;
    else if (dragIdx == c.n - 1) nx = 1.f;
    else nx = jlimit (c.nd[dragIdx - 1].x, c.nd[dragIdx + 1].x, nx);
    c.nd[dragIdx].x = nx;
    c.nd[dragIdx].y = jlimit (0.f, 1.f, v);
    pushCur();
}

void VisualPanel::mouseDoubleClick (const MouseEvent& e)
{
    if (curSlot() == 0 || ! plot().contains (e.position)) return;
    auto& c = cur();
    const int hit = hitNode (e.position);
    if (hit >= 0) { c.nd[hit].t = 0.f; pushCur(); return; }
    if (c.n >= db::Curve::maxNodes) return;
    auto pl = plot();
    float nx = (e.position.x - pl.getX()) / pl.getWidth();
    float v = editTime ? (e.position.y - pl.getY()) / pl.getHeight() : 1.f - (e.position.y - pl.getY()) / pl.getHeight();
    if (snapOn) { nx = std::round (nx * (float) gridX) / (float) gridX; v = std::round (v * (float) gridY) / (float) gridY; }
    nx = jlimit (0.f, 1.f, nx); v = jlimit (0.f, 1.f, v);
    int idx = 1;
    while (idx < c.n - 1 && c.nd[idx].x <= nx) ++idx;
    for (int k = c.n; k > idx; --k) c.nd[k] = c.nd[k - 1];
    c.nd[idx] = { nx, v, 0.f };
    ++c.n;
    pushCur();
}

void VisualPanel::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (curSlot() == 0 || ! plot().contains (e.position)) return;
    auto& c = cur();
    auto pl = plot();
    const float nx = (e.position.x - pl.getX()) / pl.getWidth();
    int idx = 0;
    for (int i = 0; i < c.n - 1; ++i) if (c.nd[i].x <= nx) idx = i;
    c.nd[idx].t = jlimit (-1.f, 1.f, c.nd[idx].t + w.deltaY * 0.6f);
    pushCur();
}

// =====================================================================================
//  Editor
// =====================================================================================
DopeBeatEditor::DopeBeatEditor (DopeBeatProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    bg = ImageFileFormat::loadFrom (BinaryData::GBET_png, (size_t) BinaryData::GBET_pngSize);

    for (auto* k : { &kTime, &kVol, &kSmooth })
    {
        k->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        k->setLookAndFeel (&knobLnf);
    }
    kTime.setDoubleClickReturnValue (true, 1.0);
    kVol.setDoubleClickReturnValue (true, 1.0);
    kSmooth.setDoubleClickReturnValue (true, 0.15);
    aTime   = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "timeMix", kTime);
    aVol    = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "volMix", kVol);
    aSmooth = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "smooth", kSmooth);

    auto fill = [this] (ComboBox& cb, const char* id)
    {
        if (auto* ch = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id))) cb.addItemList (ch->choices, 1);
        cb.setLookAndFeel (&lnf);
        cb.setJustificationType (Justification::centredLeft);
    };
    fill (cycleBox, "cycle"); fill (snapBox, "snap"); fill (algoBox, "algo");
    aCycle = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "cycle", cycleBox);
    aSnap  = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "snap", snapBox);
    aAlgo  = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "algo", algoBox);

    bypassBtn.iconCentre = { 0.51f, 0.50f };
    bypassBtn.isOn = [this] { return *proc.apvts.getRawParameterValue ("bypass") < 0.5f; };
    bypassBtn.onClick = [this] { ui::setParam (proc.apvts.getParameter ("bypass"), *proc.apvts.getRawParameterValue ("bypass") > 0.5f ? 0.f : 1.f); };
    randBtn.iconCentre = { 0.49f, 0.47f };
    randBtn.onClick = [this] { proc.randomizeCurves(); };
    lupaBtn.iconCentre = { 0.48f, 0.48f };
    lupaBtn.isOn = [this] { return panel.focus; };
    lupaBtn.onClick = [this] { panel.focus = ! panel.focus; };

    content.onPaint = [this] (Graphics& g) { paintContent (g); };
    addAndMakeVisible (content);
    content.addAndMakeVisible (panel);
    content.addAndMakeVisible (timeGrid); content.addAndMakeVisible (volGrid);
    content.addAndMakeVisible (xy); content.addAndMakeVisible (info); content.addAndMakeVisible (meter);
    content.addAndMakeVisible (kTime); content.addAndMakeVisible (kVol); content.addAndMakeVisible (kSmooth);
    content.addAndMakeVisible (cycleBox); content.addAndMakeVisible (snapBox); content.addAndMakeVisible (algoBox);
    content.addAndMakeVisible (bypassBtn); content.addAndMakeVisible (randBtn); content.addAndMakeVisible (lupaBtn);

    setResizable (true, true);
    setResizeLimits (576, 384, 2304, 1536);
    getConstrainer()->setFixedAspectRatio ((double) (ui::IMG_W / ui::IMG_H));
    setSize (864, 576);
    startTimerHz (30);
}

DopeBeatEditor::~DopeBeatEditor()
{
    stopTimer();
    for (auto* k : { &kTime, &kVol, &kSmooth }) k->setLookAndFeel (nullptr);
    for (auto* c : { &cycleBox, &snapBox, &algoBox }) c->setLookAndFeel (nullptr);
}

void DopeBeatEditor::resized()
{
    content.setBounds (0, 0, (int) ui::IMG_W, (int) ui::IMG_H);
    content.setTransform (AffineTransform::scale ((float) getWidth() / ui::IMG_W, (float) getHeight() / ui::IMG_H));
    s = 1.f;
    auto R = [this] (float x, float y, float w, float h)
    { return Rectangle<int> ((int) std::round (x * s), (int) std::round (y * s), (int) std::round (w * s), (int) std::round (h * s)); };

    panel.setBounds (R (574, 21, 942, 937));  panel.setScale (s);
    timeGrid.setBounds (R (24, 195, 516, 256)); timeGrid.geom (24, 195, s);
    volGrid.setBounds (R (24, 479, 516, 292));  volGrid.geom (24, 479, s);
    xy.setBounds (R (400, 26, 136, 138)); xy.s = s;
    bypassBtn.setBounds (R (18, 16, 90, 92));
    randBtn.setBounds (R (18, 915, 90, 90));
    lupaBtn.setBounds (R (28, 832, 50, 50));
    info.setBounds (R (15, 115, 357, 66)); info.s = s;
    meter.setBounds (R (100, 937, 185, 42)); meter.s = s;

    const float cx[3] = { 160.f, 288.f, 416.f };
    kTime.setBounds (R (cx[0] - 52, 790, 104, 104));
    kVol.setBounds (R (cx[1] - 52, 790, 104, 104));
    kSmooth.setBounds (R (cx[2] - 52, 790, 104, 104));

    cycleBox.setBounds (R (961, 981, 169, 30));
    snapBox.setBounds (R (1143, 981, 181, 30));
    algoBox.setBounds (R (1337, 981, 175, 30));
}

void DopeBeatEditor::paint (Graphics& g) { g.fillAll (Colours::black); }

void DopeBeatEditor::paintContent (Graphics& g)
{
    g.fillAll (Colours::black);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (bg, 0, 0, (int) ui::IMG_W, (int) ui::IMG_H, 0, 0, bg.getWidth(), bg.getHeight());

    auto R = [this] (float x, float y, float w, float h) { return Rectangle<float> (x * s, y * s, w * s, h * s); };
    ui::neonText (g, "DOPE BEAT", R (98, 34, 186, 56), 29.f * s);

    const char* names[3] = { "TIME MIX", "VOL MIX", "SMOOTH" };
    const float cx[3] = { 160.f, 288.f, 416.f };
    for (int i = 0; i < 3; ++i)
        ui::neonText (g, names[i], R (cx[i] - 46, 878, 92, 15), 11.f * s, Justification::centred, ui::neon(), false);
}

void DopeBeatEditor::timerCallback()
{
    const int t = proc.timeSlot.load(), v = proc.volSlot.load();
    if (t != lastT || v != lastV) { lastT = t; lastV = v; timeGrid.repaint(); volGrid.repaint(); }
    panel.update();
    xy.repaint(); meter.repaint(); info.repaint(); bypassBtn.repaint(); lupaBtn.repaint();
}
