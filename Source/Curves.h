#pragma once
#include <JuceHeader.h>
#include <functional>
#include <cmath>

namespace db
{
constexpr int kTimeSlots = 28;
constexpr int kVolSlots  = 32;

struct Node { float x, y, t; };

// Curva POD (copiable en el hilo de audio). t = tension del segmento que parte del nodo.
struct Curve
{
    static constexpr int maxNodes = 160;
    int n = 2;
    Node nd[maxNodes];

    Curve() { nd[0] = { 0.f, 0.f, 0.f }; nd[1] = { 1.f, 0.f, 0.f }; }

    void clear() { n = 0; }
    void add (float x, float y, float t = 0.f)
    {
        if (n < maxNodes) nd[n++] = { juce::jlimit (0.f, 1.f, x), juce::jlimit (0.f, 1.f, y), t };
    }

    float eval (float x) const noexcept
    {
        if (n <= 0) return 0.f;
        int lo = 0, hi = n;
        while (lo < hi) { int mid = (lo + hi) >> 1; if (nd[mid].x > x) hi = mid; else lo = mid + 1; }
        if (lo == 0) return nd[0].y;
        if (lo >= n) return nd[n - 1].y;
        const Node& a = nd[lo - 1];
        const Node& b = nd[lo];
        float u = (x - a.x) / (b.x - a.x);
        if (a.t != 0.f) u = std::pow (u, std::pow (4.f, a.t));
        return a.y + (b.y - a.y) * u;
    }

    juce::String toString() const
    {
        juce::String s;
        for (int i = 0; i < n; ++i)
            s << juce::String (nd[i].x, 5) << "," << juce::String (nd[i].y, 5) << "," << juce::String (nd[i].t, 3) << ";";
        return s;
    }

    void fromString (const juce::String& s)
    {
        clear();
        for (auto& tok : juce::StringArray::fromTokens (s, ";", ""))
        {
            auto p = juce::StringArray::fromTokens (tok, ",", "");
            if (p.size() >= 3) add (p[0].getFloatValue(), p[1].getFloatValue(), p[2].getFloatValue());
        }
        if (n < 2) *this = Curve();
    }
};

inline Curve fromFn (const std::function<float (float)>& f, int pts = 48)
{
    Curve c; c.clear();
    pts = juce::jmin (pts, Curve::maxNodes);
    for (int i = 0; i < pts; ++i) { float x = (float) i / (float) (pts - 1); c.add (x, f (x)); }
    return c;
}

inline Curve flat (float v) { Curve c; c.clear(); c.add (0, v); c.add (1, v); return c; }

inline Curve ramps (int n, float h)           // rampas 0->h (n veces) = reverse / scratch
{ Curve c; c.clear(); for (int i = 0; i < n; ++i) { c.add ((float) i / n, 0); c.add ((float) (i + 1) / n, h); } return c; }

inline Curve stutter (int n)                  // repite el trozo anterior n veces
{ Curve c; c.clear(); for (int i = 0; i < n; ++i) { float y = (float) (i + 1) / n; c.add ((float) i / n, y); c.add ((float) (i + 1) / n, y); } return c; }

inline Curve pulses (int n, float duty, float hi = 1.f, float lo = 0.f, float shift = 0.f)
{
    Curve c; c.clear();
    for (int i = 0; i < n; ++i)
    {
        float s = ((float) i + shift) / n, e = ((float) i + shift + duty) / n, f = ((float) i + 1.f + shift) / n;
        c.add (s, hi); c.add (e, hi); c.add (e, lo); c.add (juce::jmin (f, 1.f), lo);
    }
    return c;
}

inline Curve sideDuck (int n, float rise)
{
    Curve c; c.clear();
    for (int i = 0; i < n; ++i)
    {
        c.add ((float) i / n, 0.f, 0.8f);
        c.add (((float) i + rise) / n, 1.f);
        c.add ((float) (i + 1) / n, 1.f);
    }
    return c;
}

inline Curve stepsFromString (const juce::String& bits, float lo = 0.f, float hi = 1.f)
{
    Curve c; c.clear(); int n = bits.length();
    for (int i = 0; i < n; ++i)
    {
        float y = bits[i] == '1' ? hi : lo;
        c.add ((float) i / n, y); c.add ((float) (i + 1) / n, y);
    }
    return c;
}

inline Curve randomBits (int seed, int steps, float prob)
{
    juce::Random r (seed); juce::String b;
    for (int i = 0; i < steps; ++i) b << ((i == 0 || r.nextFloat() < prob) ? "1" : "0");
    return stepsFromString (b);
}

inline Curve randomSteps (int seed, int steps)
{
    juce::Random r (seed); Curve c; c.clear();
    for (int i = 0; i < steps; ++i)
    {
        float y = (float) r.nextInt (7) * 0.125f;
        c.add ((float) i / steps, y); c.add ((float) (i + 1) / steps, y);
    }
    return c;
}

inline const char* timeName (int i)
{
    static const char* n[kTimeSlots] = {
        "HALF SPEED", "QUARTER", "TAPE STOP", "STOP+HOLD",
        "FREEZE", "REVERSE", "REV 1/4", "REV 1/8",
        "STUTTER 2", "STUTTER 4", "STUTTER 8", "STUTTER 16",
        "SCRATCH 1", "SCRATCH 2", "SCRATCH 3", "WOBBLE",
        "BACK 1/4", "BACK 1/2", "BACK 1", "TAPE START",
        "DOWN-UP", "SPEED 3/4", "SKIP BACK", "SKIP FWD",
        "RANDOM 1", "RANDOM 2", "ECHO 1/8", "NORMAL" };
    return n[juce::jlimit (0, kTimeSlots - 1, i)];
}

inline const char* volName (int i)
{
    static const char* n[kVolSlots] = {
        "GATE 4", "GATE 8", "GATE 16", "GATE 32",
        "TRANCE 8", "TRANCE 16", "TRIPLET 12", "TRIPLET 6",
        "SIDE 2", "SIDE 4", "SIDE 8", "SIDE 16",
        "FADE IN", "FADE OUT", "SWELL", "CUT 1/2",
        "TREMOLO 8", "TREMOLO 16", "SAW UP 4", "SAW DN 4",
        "SAW UP 8", "SAW DN 8", "OFFBEAT", "SYNCOPE",
        "RANDOM 1", "RANDOM 2", "RANDOM 3", "RANDOM 4",
        "BURST", "PUMP 4", "DUCK 2ND", "FULL" };
    return n[juce::jlimit (0, kVolSlots - 1, i)];
}

inline Curve defaultTime (int i)
{
    const float pi = juce::MathConstants<float>::pi;
    switch (i)
    {
        case 0:  { Curve c; c.clear(); c.add (0, 0); c.add (1, 0.5f); return c; }
        case 1:  { Curve c; c.clear(); c.add (0, 0); c.add (1, 0.75f); return c; }
        case 2:  { Curve c; c.clear(); c.add (0, 0, 0.5f); c.add (1, 0.5f); return c; }
        case 3:  { Curve c; c.clear(); c.add (0, 0, 0.5f); c.add (0.5f, 0.25f); c.add (1, 0.75f); return c; }
        case 4:  { Curve c; c.clear(); c.add (0, 0); c.add (1, 1); return c; }
        case 5:  return ramps (2, 1.f);
        case 6:  return ramps (4, 0.5f);
        case 7:  return ramps (8, 0.25f);
        case 8:  return stutter (2);
        case 9:  return stutter (4);
        case 10: return stutter (8);
        case 11: return stutter (16);
        case 12: return fromFn ([=] (float x) { return 0.2f * (1.f - std::cos (4.f * pi * x)); });
        case 13: return fromFn ([=] (float x) { return 0.15f * (1.f - std::cos (8.f * pi * x)); }, 97);
        case 14: return fromFn ([=] (float x) { return 0.25f * (1.f - std::cos (2.f * pi * (2.f * x + 6.f * x * x))); }, 120);
        case 15: return fromFn ([=] (float x) { return 0.04f * (1.f - std::cos (16.f * pi * x)); }, 129);
        case 16: return flat (0.25f);
        case 17: return flat (0.5f);
        case 18: return flat (1.f);
        case 19: return fromFn ([] (float x) { return x - 0.5f * x * x; });
        case 20: return fromFn ([=] (float x) { return (1.f - std::cos (pi * x)) / pi; });
        case 21: { Curve c; c.clear(); c.add (0, 0); c.add (1, 0.25f); return c; }
        case 22: { Curve c; c.clear(); for (int k = 0; k < 8; ++k) { float y = k * 0.0625f; c.add (k / 8.f, y); c.add ((k + 1) / 8.f, y); } return c; }
        case 23: { Curve c; c.clear(); for (int k = 0; k < 8; ++k) { float y = 0.75f - k * 0.0625f; c.add (k / 8.f, y); c.add ((k + 1) / 8.f, y); } return c; }
        case 24: return randomSteps (11, 8);
        case 25: return randomSteps (23, 16);
        case 26: return flat (0.125f);
        default: return flat (0.f);
    }
}

inline Curve defaultVol (int i)
{
    const float pi = juce::MathConstants<float>::pi;
    switch (i)
    {
        case 0:  return pulses (4, 0.5f);
        case 1:  return pulses (8, 0.5f);
        case 2:  return pulses (16, 0.5f);
        case 3:  return pulses (32, 0.5f);
        case 4:  return pulses (8, 0.35f);
        case 5:  return pulses (16, 0.35f);
        case 6:  return pulses (12, 0.5f);
        case 7:  return pulses (6, 0.5f);
        case 8:  return sideDuck (2, 0.7f);
        case 9:  return sideDuck (4, 0.7f);
        case 10: return sideDuck (8, 0.7f);
        case 11: return sideDuck (16, 0.7f);
        case 12: { Curve c; c.clear(); c.add (0, 0); c.add (1, 1); return c; }
        case 13: { Curve c; c.clear(); c.add (0, 1); c.add (1, 0); return c; }
        case 14: { Curve c; c.clear(); c.add (0, 0); c.add (0.5f, 1); c.add (1, 0); return c; }
        case 15: { Curve c; c.clear(); c.add (0, 1); c.add (0.5f, 1); c.add (0.5f, 0); c.add (1, 0); return c; }
        case 16: return fromFn ([=] (float x) { return 0.5f + 0.5f * std::cos (16.f * pi * x); }, 97);
        case 17: return fromFn ([=] (float x) { return 0.5f + 0.5f * std::cos (32.f * pi * x); }, 129);
        case 18: { Curve c; c.clear(); for (int k = 0; k < 4; ++k) { c.add (k / 4.f, 0); c.add ((k + 1) / 4.f, 1); } return c; }
        case 19: { Curve c; c.clear(); for (int k = 0; k < 4; ++k) { c.add (k / 4.f, 1); c.add ((k + 1) / 4.f, 0); } return c; }
        case 20: { Curve c; c.clear(); for (int k = 0; k < 8; ++k) { c.add (k / 8.f, 0); c.add ((k + 1) / 8.f, 1); } return c; }
        case 21: { Curve c; c.clear(); for (int k = 0; k < 8; ++k) { c.add (k / 8.f, 1); c.add ((k + 1) / 8.f, 0); } return c; }
        case 22: return pulses (4, 0.5f, 1.f, 0.f, 0.5f);
        case 23: return stepsFromString ("1011010010110100");
        case 24: return randomBits (3, 16, 0.6f);
        case 25: return randomBits (7, 16, 0.6f);
        case 26: return randomBits (13, 16, 0.5f);
        case 27: return randomBits (29, 32, 0.6f);
        case 28: return stepsFromString ("1010101011111111");
        case 29: { Curve c; c.clear(); for (int k = 0; k < 4; ++k) { c.add (k / 4.f, 0.25f, 0.7f); c.add ((k + 1) / 4.f, 1.f); } return c; }
        case 30: { Curve c; c.clear(); c.add (0, 1); c.add (0.5f, 1); c.add (0.5f, 0.15f); c.add (1, 0.15f); return c; }
        default: return flat (1.f);
    }
}

// Generador "Smart Randomize"
inline Curve randomTime (juce::Random& r)
{
    const float pi = juce::MathConstants<float>::pi;
    switch (r.nextInt (5))
    {
        case 0: { int n = 4 << r.nextInt (3); Curve c; c.clear();
                  for (int i = 0; i < n; ++i) { float y = r.nextInt (7) * 0.125f; c.add ((float) i / n, y); c.add ((float) (i + 1) / n, y); }
                  return c; }
        case 1: { float amp = 0.1f + r.nextFloat() * 0.2f; float cyc = (float) (1 + r.nextInt (6));
                  return fromFn ([=] (float x) { return amp * (1.f - std::cos (2.f * pi * cyc * x)); }, 129); }
        case 2: { int n = 2 << r.nextInt (3); return ramps (n, 2.f / (float) n); }
        case 3: return stutter (2 << r.nextInt (4));
        default: { Curve c; c.clear(); c.add (0, 0, 0.3f + r.nextFloat() * 0.6f); c.add (1, 0.3f + r.nextFloat() * 0.5f); return c; }
    }
}

inline Curve randomVol (juce::Random& r)
{
    switch (r.nextInt (4))
    {
        case 0: return randomBits (r.nextInt (10000), 16, 0.5f + r.nextFloat() * 0.3f);
        case 1: return sideDuck (2 << r.nextInt (3), 0.5f + r.nextFloat() * 0.4f);
        case 2: return pulses (4 << r.nextInt (3), 0.25f + r.nextFloat() * 0.5f);
        default: { Curve c; c.clear(); int n = 4 << r.nextInt (2);
                   for (int k = 0; k < n; ++k) { c.add ((float) k / n, 0); c.add ((float) (k + 1) / n, 1); }
                   return c; }
    }
}
} // namespace db
