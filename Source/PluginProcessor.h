#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "Curves.h"

class DopeBeatProcessor : public juce::AudioProcessor
{
public:
    DopeBeatProcessor();
    ~DopeBeatProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DOPE BEAT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Parametros ----
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- Slots y curvas ----
    std::atomic<int> timeSlot { 3 }, volSlot { 10 };       // 0 = off
    db::Curve timeCurves[db::kTimeSlots];
    db::Curve volCurves[db::kVolSlots];
    juce::SpinLock curveLock;
    std::atomic<int> curveVersion { 0 };

    void setCurve (bool isTime, int slot, const db::Curve& c);
    db::Curve getCurve (bool isTime, int slot);
    void resetSlot (bool isTime, int slot);
    void randomizeCurves();

    // ---- Datos para la GUI ----
    std::atomic<float> inLevel { 0.f }, outLevel { 0.f }, phaseNow { 0.f }, bpmNow { 120.f }, cycleBeatsNow { 4.f };
    std::atomic<bool> hostSynced { false }, activeNow { false };
    std::atomic<double> sampleRateNow { 44100.0 };
    std::array<std::atomic<float>, 256> waveCols;
    int readScope (float* dst, int n);

private:
    static constexpr int LAT = 4;
    struct Tiny
    {
        float b[8] = {}; int i = 0;
        float push (float x) noexcept { b[i] = x; float y = b[(i - LAT) & 7]; i = (i + 1) & 7; return y; }
    };

    inline float readH (const float* b, double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const float x = (float) (pos - fl);
        const int i = (int) fl;
        const float y0 = b[(i - 1) & mask], y1 = b[i & mask], y2 = b[(i + 1) & mask], y3 = b[(i + 2) & mask];
        const float c0 = y1, c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * x + c2) * x + c1) * x + c0;
    }

    void pushScope (const float* src, int n);

    double sr = 44100.0;
    int mask = 0, bufSize = 0, wpos = 0;
    juce::AudioBuffer<float> ring;
    Tiny dryFull[2], restTiny[2];
    juce::dsp::LinkwitzRileyFilter<float> xo1, xo2;
    juce::dsp::StateVariableTPTFilter<float> lpf;
    juce::SmoothedValue<float> cutSm, satSm;

    double prevD = 0.0, oldD = 0.0, internalPpq = 0.0;
    int fadeLeft = 0, fadeLen = 192, lastCol = -1;
    float gSm = 1.f, bpSm = 0.f;
    db::Curve tCur, vCur;
    bool hasT = false, hasV = false;
    int lv = -1, lts = -1, lvs = -1;
    float fastEnv = 0.f, slowEnv = 0.f;
    int trigLeft = 0, trigTotal = 1, cooldown = 0, noteHeld = 0;

    std::atomic<float> *pBypass, *pTimeMix, *pVolMix, *pSmooth, *pCycle, *pAlgo, *pMode, *pSync, *pBpm, *pX, *pY;

    juce::AbstractFifo fifo { 8192 };
    std::array<float, 8192> fifoData {};
    std::vector<float> scopeTmp, readTmp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DopeBeatProcessor)
};
