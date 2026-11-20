#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout DopeBeatProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    l.add (std::make_unique<AudioParameterBool> (id ("bypass"), "Bypass", false));
    l.add (std::make_unique<AudioParameterFloat> (id ("timeMix"), "Time Mix", NormalisableRange<float> (0.f, 1.f), 1.f));
    l.add (std::make_unique<AudioParameterFloat> (id ("volMix"), "Volume Mix", NormalisableRange<float> (0.f, 1.f), 1.f));
    l.add (std::make_unique<AudioParameterFloat> (id ("smooth"), "Smooth", NormalisableRange<float> (0.f, 1.f), 0.15f));
    l.add (std::make_unique<AudioParameterChoice> (id ("cycle"), "Cycle", StringArray { "CICLO 1/4", "CICLO 1/2", "CICLO 1", "CICLO 2", "CICLO 4" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (id ("snap"), "Snap", StringArray { "SNAP 1/4", "SNAP 1/8", "SNAP 1/16", "SNAP 1/8T", "SNAP LIBRE" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (id ("algo"), "Band", StringArray { "STANDARD", "BANDA LOW", "BANDA MID", "BANDA HIGH" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (id ("mode"), "Mode", StringArray { "LOOP", "MIDI TRIGGER", "AUDIO TRANSIENT" }, 0));
    l.add (std::make_unique<AudioParameterBool> (id ("sync"), "Sync BPM", true));
    l.add (std::make_unique<AudioParameterFloat> (id ("freeBpm"), "Free BPM", NormalisableRange<float> (40.f, 240.f, 0.1f), 120.f));
    l.add (std::make_unique<AudioParameterFloat> (id ("xyX"), "XY Cutoff", NormalisableRange<float> (0.f, 1.f), 1.f));
    l.add (std::make_unique<AudioParameterFloat> (id ("xyY"), "XY Saturation", NormalisableRange<float> (0.f, 1.f), 0.f));
    return l;
}

DopeBeatProcessor::DopeBeatProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DopeBeatState", createLayout())
{
    pBypass = apvts.getRawParameterValue ("bypass");  pTimeMix = apvts.getRawParameterValue ("timeMix");
    pVolMix = apvts.getRawParameterValue ("volMix");  pSmooth  = apvts.getRawParameterValue ("smooth");
    pCycle  = apvts.getRawParameterValue ("cycle");   pAlgo    = apvts.getRawParameterValue ("algo");
    pMode   = apvts.getRawParameterValue ("mode");    pSync    = apvts.getRawParameterValue ("sync");
    pBpm    = apvts.getRawParameterValue ("freeBpm"); pX       = apvts.getRawParameterValue ("xyX");
    pY      = apvts.getRawParameterValue ("xyY");

    for (int i = 0; i < db::kTimeSlots; ++i) timeCurves[i] = db::defaultTime (i);
    for (int i = 0; i < db::kVolSlots; ++i)  volCurves[i]  = db::defaultVol (i);
    for (auto& c : waveCols) c.store (0.f);
}

bool DopeBeatProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto o = l.getMainOutputChannelSet();
    if (o != AudioChannelSet::stereo() && o != AudioChannelSet::mono()) return false;
    return o == l.getMainInputChannelSet();
}

void DopeBeatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    sampleRateNow.store (sr);
    bufSize = nextPowerOfTwo ((int) (sr * 35.0));
    mask = bufSize - 1;
    ring.setSize (2, bufSize);
    ring.clear();
    wpos = 0; prevD = oldD = 0.0; fadeLeft = 0;
    fadeLen = jmax (16, (int) (sr * 0.004));
    for (auto& t : dryFull) t = Tiny();
    for (auto& t : restTiny) t = Tiny();
    gSm = 1.f; bpSm = pBypass->load() > 0.5f ? 1.f : 0.f;

    dsp::ProcessSpec spec { sr, (uint32) jmax (1, samplesPerBlock), 2 };
    xo1.prepare (spec); xo2.prepare (spec); lpf.prepare (spec);
    xo1.setCutoffFrequency (250.f); xo2.setCutoffFrequency (3000.f);
    lpf.setType (dsp::StateVariableTPTFilterType::lowpass);
    lpf.setCutoffFrequency (20000.f);
    cutSm.reset (sr, 0.03); satSm.reset (sr, 0.03);
    cutSm.setCurrentAndTargetValue (20000.f); satSm.setCurrentAndTargetValue (0.f);

    scopeTmp.assign ((size_t) jmax (1, samplesPerBlock) + 8, 0.f);
    readTmp.assign (8192, 0.f);
    fifo.reset();
    setLatencySamples (LAT);
    lv = lts = lvs = -1;
}

void DopeBeatProcessor::pushScope (const float* src, int n)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) std::memcpy (fifoData.data() + s1, src, (size_t) n1 * sizeof (float));
    if (n2 > 0) std::memcpy (fifoData.data() + s2, src + n1, (size_t) n2 * sizeof (float));
    fifo.finishedWrite (n1 + n2);
}

int DopeBeatProcessor::readScope (float* dst, int n)
{
    const int ready = fifo.getNumReady();
    if (ready <= 0) return 0;
    const int toRead = jmin (ready, (int) readTmp.size());
    int s1, n1, s2, n2;
    fifo.prepareToRead (toRead, s1, n1, s2, n2);
    if (n1 > 0) std::memcpy (readTmp.data(), fifoData.data() + s1, (size_t) n1 * sizeof (float));
    if (n2 > 0) std::memcpy (readTmp.data() + n1, fifoData.data() + s2, (size_t) n2 * sizeof (float));
    fifo.finishedRead (n1 + n2);
    const int got = n1 + n2;
    const int take = jmin (n, got);
    std::memcpy (dst, readTmp.data() + (got - take), (size_t) take * sizeof (float));
    return take;
}

void DopeBeatProcessor::processBlock (AudioBuffer<float>& buf, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int ns = buf.getNumSamples();
    const int nch = jmin (2, buf.getNumChannels());
    if (ns == 0 || nch == 0) return;

    // ---- MIDI: cambio de slots en vivo ----
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (n >= 36 && n < 36 + db::kTimeSlots)      { timeSlot.store (n - 36 + 1); ++noteHeld; }
            else if (n >= 72 && n < 72 + db::kVolSlots)  { volSlot.store (n - 72 + 1);  ++noteHeld; }
        }
        else if (m.isNoteOff()) { if (noteHeld > 0) --noteHeld; }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) noteHeld = 0;
    }

    // ---- Reloj del host ----
    double bpm = pBpm->load();
    double barBeats = 4.0, ppqStart = internalPpq;
    const bool sync = pSync->load() > 0.5f;
    bool hosted = false;
    if (sync)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                hosted = true;
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto ts = pos->getTimeSignature()) barBeats = ts->numerator * 4.0 / jmax (1, ts->denominator);
                if (pos->getIsPlaying())
                    if (auto pp = pos->getPpqPosition()) { ppqStart = *pp; internalPpq = *pp; }
            }
    bpm = jlimit (20.0, 400.0, bpm);
    hostSynced.store (hosted);
    bpmNow.store ((float) bpm);

    static const double cycMul[5] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
    const double cycleBeats = jmax (0.25, cycMul[jlimit (0, 4, (int) pCycle->load())] * barBeats);
    cycleBeatsNow.store ((float) cycleBeats);
    const double spb = 60.0 / bpm * sr;
    const double ppqPerSample = bpm / (60.0 * sr);
    const double cycleSamples = cycleBeats * spb;
    const double maxDelay = (double) (bufSize - 16);

    // ---- Curvas activas (copia segura) ----
    {
        const int ts = timeSlot.load(), vs = volSlot.load(), cv = curveVersion.load();
        if (cv != lv || ts != lts || vs != lvs)
        {
            const SpinLock::ScopedTryLockType sl (curveLock);
            if (sl.isLocked())
            {
                hasT = ts > 0; hasV = vs > 0;
                if (hasT) tCur = timeCurves[ts - 1];
                if (hasV) vCur = volCurves[vs - 1];
                lv = cv; lts = ts; lvs = vs;
            }
        }
    }

    // ---- Parametros del bloque ----
    const float tm = pTimeMix->load(), vm = pVolMix->load();
    const int algo = (int) pAlgo->load(), mode = (int) pMode->load();
    const float tau = 0.0005f + pSmooth->load() * 0.05f;
    const float gCoef = 1.f - std::exp (-1.f / (tau * (float) sr));
    const float bCoef = 1.f - std::exp (-1.f / (0.015f * (float) sr));
    const bool bypass = pBypass->load() > 0.5f;
    cutSm.setTargetValue (20.f * std::pow (1000.f, pX->load()));
    satSm.setTargetValue (pY->load());

    float* io[2] = { buf.getWritePointer (0), nch > 1 ? buf.getWritePointer (1) : nullptr };
    float* rb[2] = { ring.getWritePointer (0), ring.getWritePointer (1) };
    float inPk = 0.f, outPk = 0.f;
    const int scopeN = jmin (ns, (int) scopeTmp.size());
    bool anyActive = false;

    for (int i = 0; i < ns; ++i)
    {
        float x[2] = { io[0][i], nch > 1 ? io[1][i] : 0.f };

        // detector de transientes
        const float mono = nch > 1 ? 0.5f * (std::abs (x[0]) + std::abs (x[1])) : std::abs (x[0]);
        fastEnv += 0.3f * (mono - fastEnv);
        slowEnv += 0.0005f * (mono - slowEnv);
        if (cooldown > 0) --cooldown;
        else if (mode == 2 && fastEnv > 0.05f && fastEnv > slowEnv * 3.f)
        {
            trigTotal = jmax (1, (int) cycleSamples); trigLeft = trigTotal; cooldown = (int) (0.08 * sr);
        }

        bool active = mode == 0 ? true : (mode == 1 ? noteHeld > 0 : trigLeft > 0);
        double phase;
        if (mode == 2) phase = 1.0 - (double) trigLeft / (double) trigTotal;
        else { const double cyc = (ppqStart + i * ppqPerSample) / cycleBeats; phase = cyc - std::floor (cyc); }
        if (mode == 2 && trigLeft > 0) --trigLeft;
        anyActive = anyActive || active;

        // columnas de forma de onda (para dibujar sobre los golpes)
        {
            const int col = jlimit (0, 255, (int) (phase * 256.0));
            if (col != lastCol) { waveCols[(size_t) col].store (0.f, std::memory_order_relaxed); lastCol = col; }
            if (mono > waveCols[(size_t) col].load (std::memory_order_relaxed)) waveCols[(size_t) col].store (mono, std::memory_order_relaxed);
        }

        // separacion en bandas + escritura en el buffer circular
        float eng[2], rest[2];
        for (int c = 0; c < nch; ++c)
        {
            if (algo == 0) { eng[c] = x[c]; rest[c] = 0.f; }
            else
            {
                float lo, r1, mid, hi;
                xo1.processSample (c, x[c], lo, r1);
                xo2.processSample (c, r1, mid, hi);
                eng[c] = algo == 1 ? lo : (algo == 2 ? mid : hi);
                rest[c] = (lo + mid + hi) - eng[c];
            }
            rb[c][wpos] = eng[c];
        }

        // retardo objetivo (curva de tiempo) y compuerta (curva de volumen)
        const float v = (hasT && active) ? jlimit (0.f, 1.f, tCur.eval ((float) phase)) : 0.f;
        const double d = jlimit (0.0, maxDelay, (double) v * cycleSamples);
        const float gT = (hasV && active) ? jlimit (0.f, 1.f, vCur.eval ((float) phase)) : 1.f;

        // anti-click: salto brusco del cabezal -> crossfade
        if (std::abs (d - prevD) > 6.0) { oldD = prevD; fadeLeft = fadeLen; }
        prevD = d;

        gSm += gCoef * (gT - gSm);
        const float gApplied = 1.f + (gSm - 1.f) * vm;
        bpSm += bCoef * ((bypass ? 1.f : 0.f) - bpSm);

        const float cut = cutSm.getNextValue();
        const float sat = satSm.getNextValue();
        if ((i & 15) == 0) lpf.setCutoffFrequency (cut);
        const float drive = 1.f + sat * 7.f, satMix = jmin (1.f, sat * 3.f), invT = 1.f / std::tanh (drive);
        const float q = sat > 0.5f ? std::pow (2.f, 15.f - (sat - 0.5f) * 2.f * 10.f) : 0.f;

        float f = 0.f;
        if (fadeLeft > 0) f = 1.f - (float) fadeLeft / (float) fadeLen;

        for (int c = 0; c < nch; ++c)
        {
            const float dryEng = rb[c][(wpos - LAT) & mask];
            float wet = readH (rb[c], (double) wpos - (double) LAT - d);
            if (fadeLeft > 0)
            {
                const float o = readH (rb[c], (double) wpos - (double) LAT - oldD);
                wet = o * (1.f - f) + wet * f;
            }
            const float tOut = dryEng * (1.f - tm) + wet * tm;
            float y = restTiny[c].push (rest[c]) + tOut * gApplied;

            // XY: filtro + saturacion + bitcrush
            if (cut < 19500.f) y = lpf.processSample (c, y);
            if (sat > 0.001f)
            {
                const float s = std::tanh (drive * y) * invT;
                y = y + (s - y) * satMix;
                if (q > 0.f) y = std::round (y * q) / q;
            }

            const float dry = dryFull[c].push (x[c]);
            const float out = y * (1.f - bpSm) + dry * bpSm;
            io[c][i] = out;
            inPk = jmax (inPk, std::abs (x[c]));
            outPk = jmax (outPk, std::abs (out));
        }
        if (fadeLeft > 0) --fadeLeft;
        wpos = (wpos + 1) & mask;
        internalPpq += ppqPerSample;

        if (i < scopeN) scopeTmp[(size_t) i] = nch > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
        phaseNow.store ((float) phase, std::memory_order_relaxed);
    }

    pushScope (scopeTmp.data(), scopeN);
    inLevel.store (jmax (inPk, inLevel.load() * 0.85f));
    outLevel.store (jmax (outPk, outLevel.load() * 0.85f));
    activeNow.store (anyActive);
}

void DopeBeatProcessor::setCurve (bool isTime, int slot, const db::Curve& c)
{
    {
        const SpinLock::ScopedLockType sl (curveLock);
        if (isTime && slot >= 0 && slot < db::kTimeSlots) timeCurves[slot] = c;
        else if (! isTime && slot >= 0 && slot < db::kVolSlots) volCurves[slot] = c;
    }
    curveVersion.fetch_add (1);
}

db::Curve DopeBeatProcessor::getCurve (bool isTime, int slot)
{
    const SpinLock::ScopedLockType sl (curveLock);
    return isTime ? timeCurves[jlimit (0, db::kTimeSlots - 1, slot)] : volCurves[jlimit (0, db::kVolSlots - 1, slot)];
}

void DopeBeatProcessor::resetSlot (bool isTime, int slot)
{
    setCurve (isTime, slot, isTime ? db::defaultTime (slot) : db::defaultVol (slot));
}

void DopeBeatProcessor::randomizeCurves()
{
    Random r (Time::currentTimeMillis());
    const int ts = timeSlot.load(), vs = volSlot.load();
    if (ts > 0) setCurve (true, ts - 1, db::randomTime (r));
    if (vs > 0) setCurve (false, vs - 1, db::randomVol (r));
}

void DopeBeatProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    if (xml == nullptr) return;
    xml->setAttribute ("timeSlot", timeSlot.load());
    xml->setAttribute ("volSlot", volSlot.load());
    {
        const SpinLock::ScopedLockType sl (curveLock);
        for (int i = 0; i < db::kTimeSlots; ++i)
        { auto* e = xml->createNewChildElement ("TC"); e->setAttribute ("i", i); e->setAttribute ("d", timeCurves[i].toString()); }
        for (int i = 0; i < db::kVolSlots; ++i)
        { auto* e = xml->createNewChildElement ("VC"); e->setAttribute ("i", i); e->setAttribute ("d", volCurves[i].toString()); }
    }
    copyXmlToBinary (*xml, dest);
}

void DopeBeatProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    timeSlot.store (jlimit (0, db::kTimeSlots, xml->getIntAttribute ("timeSlot", 3)));
    volSlot.store (jlimit (0, db::kVolSlots, xml->getIntAttribute ("volSlot", 10)));
    {
        const SpinLock::ScopedLockType sl (curveLock);
        for (auto* e : xml->getChildWithTagNameIterator ("TC"))
        { int i = e->getIntAttribute ("i", -1); if (i >= 0 && i < db::kTimeSlots) timeCurves[i].fromString (e->getStringAttribute ("d")); }
        for (auto* e : xml->getChildWithTagNameIterator ("VC"))
        { int i = e->getIntAttribute ("i", -1); if (i >= 0 && i < db::kVolSlots) volCurves[i].fromString (e->getStringAttribute ("d")); }
    }
    xml->deleteAllChildElementsWithTagName ("TC");
    xml->deleteAllChildElementsWithTagName ("VC");
    apvts.replaceState (ValueTree::fromXml (*xml));
    curveVersion.fetch_add (1);
}

AudioProcessorEditor* DopeBeatProcessor::createEditor() { return new DopeBeatEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DopeBeatProcessor(); }
