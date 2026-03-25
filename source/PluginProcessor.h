// ============================================================================
// CleanVowelSynth — Formant-based vocal synthesizer (VST3, mono)
//
// Synthesis architecture:
//   MIDI note -> Glottal source -> Formant filters -> Nasal notch -> Envelope -> Saturation -> Output
//
// Glottal source:
//   LF-derivative model with three phases per cycle (open, return, closed).
//   Female mode uses higher open quotient and longer return phase for breathier
//   quality. Alternate-cycle asymmetry creates natural subharmonics by varying
//   amplitude and timing on odd/even glottal cycles. Spectral tilt applied via
//   cascaded one-pole LP filters (-6 dB/oct male, -12 dB/oct female).
//
// Formant filtering:
//   Two parallel banks of 5 biquad bandpass filters (A and B), linearly
//   crossfaded by a slow drift oscillator. Each bank targets the same vowel
//   but with slightly offset frequencies (controlled by formantDrift) to
//   simulate articulatory instability. Formant data from Hillenbrand et al.
//   (1995). Q automatically widens at high F0 to prevent thinning.
//
// Vowels: "ah" (morph=0) to "ooh" (morph=1), with male/female formant sets.
//
// Noise: High-passed white noise mixed in for breath (continuous) and
//   aspiration (onset-weighted). Separate tilt filters prevent noise from
//   contaminating glottal filter state.
//
// Nasal coupling: Parametric notch at ~300-340 Hz adds subtle anti-formant.
//
// Envelope: Exponential ADSR. Attack is concave (slow start, fast finish),
//   decay/release are convex (fast start, slow tail).
//
// Legato: Monophonic with held-note tracking. New notes while voice is active
//   glide pitch via portamento without retriggering the envelope.
//
// Vibrato: Delayed onset (~300ms) then 150ms fade-in, mimicking real singers
//   who establish pitch before adding vibrato.
//
// Performance: Formant filter coefficients updated every 32 samples.
//   Precomputed 1/sampleRate. Fast Pade tanh approximation for saturation.
// ============================================================================

#pragma once

#include <JuceHeader.h>

// Monophonic vocal voice with glottal source, formant filtering, and legato.
class VowelVoice
{
public:
    VowelVoice();

    // Call once from prepareToPlay(). Initializes filters, delay buffers, and
    // precomputes invSr. Also calls reset() internally.
    void prepare(double sampleRate, int maximumBlockSize, int numChannels);

    // Zeros all filter state, envelope, phase accumulators. Does NOT change
    // parameter values or formant targets.
    void reset();

    // Trigger a fresh note: sets frequency immediately, resets envelope to
    // Attack stage, resets vibrato onset timer.
    void startNote(int midiNoteNumber, float velocity);

    // Legato transition: sets target frequency for portamento glide without
    // retriggering the envelope. If voice was releasing/idle, restarts attack.
    void glideToNote(int midiNoteNumber, float velocity);

    // Begin release (allowTailOff=true) or kill immediately (false).
    void stopNote(bool allowTailOff);

    // Render numSamples into buffer starting at startSample. Accumulates
    // (+=) into the buffer so multiple voices can share one buffer.
    void render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    bool isActive() const noexcept { return active; }
    int getCurrentMidiNote() const noexcept { return currentMidiNote; }
    float getVelocity() const noexcept { return noteVelocity; }

    // Update all synthesis parameters. Called once per processBlock before
    // rendering. Internally calls updateTargets() to recompute formant targets,
    // envelope coefficients, tilt filters, and nasal notch.
    void setParameters(float morph,           // [0, 1]     vowel blend: ah -> ooh
                       float formantScale,    // [0.75, 1.35] formant frequency multiplier
                       float brightness,      // [0.75, 1.5]  spectral tilt / open quotient
                       float breath,          // [0, 0.06]    continuous breath noise level
                       float aspiration,      // [0, 0.05]    onset aspiration noise level
                       float roughness,       // [0, 1]       cycle irregularity / fry amount
                       float vibratoRate,     // [0, 8] Hz    vibrato LFO frequency
                       float vibratoDepth,    // [0, 0.03]    vibrato pitch deviation (fraction)
                       float jitter,          // [0, 0.006]   random pitch perturbation
                       float shimmer,         // [0, 0.03]    random amplitude perturbation
                       float formantDrift,    // [0, 0.02]    A/B formant frequency spread
                       float attackMs,        // [1, 1000]    envelope attack time
                       float decayMs,         // [1, 1000]    envelope decay time
                       float sustain,         // [0, 1]       envelope sustain level
                       float releaseMs,       // [1, 2000]    envelope release time
                       bool female,           //              select female/male formants & tilt
                       float portamentoMs);   // [0, 500]     legato glide time

private:
    // ---------- Internal types ----------

    // Formant descriptor: center frequency, Q factor, and relative gain.
    struct Formant
    {
        float freq = 1000.0f;
        float q = 1.0f;
        float gain = 1.0f;
    };

    // Second-order IIR filter (Direct Form I). Used for both bandpass formant
    // filters and the parametric nasal notch.
    struct Biquad
    {
        void reset();
        void setBandPass(float sampleRate, float freq, float q);

        // Parametric notch: depth 0 = full cut, 1 = no effect.
        void setNotch(float sampleRate, float freq, float q, float depth);

        float process(float x);

        float b0{}, b1{}, b2{}, a1{}, a2{};  // filter coefficients
        float x1{}, x2{}, y1{}, y2{};        // delay elements
    };

    // One-pole lowpass filter. Used for spectral tilt shaping.
    // Transfer function: H(z) = (1-a) / (1 - a*z^-1)
    struct OnePoleLP
    {
        void reset();
        void setCutoff(float sampleRate, float cutoffHz);
        float process(float x);
        float a{}, z{};
    };

    // ---------- Internal methods ----------

    // Interpolate between two formant descriptors. Used to morph ah <-> ooh.
    Formant lerpFormant(const Formant& a, const Formant& b, float t, float scale) const;

    // Recompute formant targets from morph/scale/drift, envelope coefficients
    // from ADSR times, nasal notch, and spectral tilt filters. Called when
    // parameters change (once per audio block).
    void updateTargets();

    // Smoothly interpolate current formant values toward targets and update
    // biquad coefficients. Called every 32 samples for performance.
    void updateFormantsSmooth();

    // Generate one sample of the glottal excitation signal. Includes
    // portamento, vibrato (with delayed onset), jitter, LF-derivative pulse
    // model, alternate-cycle asymmetry, spectral tilt, and shimmer.
    float processGlottal();

    // Advance the exponential ADSR envelope by one sample. Returns the
    // current envelope value [0, 1].
    float processEnvelope();

    // Return one sample of white noise in [-1, 1].
    float getNoiseSample();

    // ---------- State ----------

    double sr = 44100.0;
    float invSr = 1.0f / 44100.0f;  // precomputed for per-sample math
    int blockSize = 512;
    int channels = 2;
    int formantUpdateCounter = 0;    // counts down to next filter coeff update

    bool active = false;
    bool tailOff = false;
    int currentMidiNote = -1;
    float noteVelocity = 0.0f;

    // -- Pitch / portamento --
    float frequency = 220.0f;        // current instantaneous frequency (Hz)
    float targetFrequency = 220.0f;  // portamento destination frequency
    float portamentoInc = 1.0f;      // multiplicative glide factor per sample
    float portamentoMs = 60.0f;
    float phase = 0.0f;              // glottal phase accumulator [0, 1)
    float phaseIncrement = 0.0f;

    // -- Envelope --
    float env = 0.0f;
    enum class EnvStage { Idle, Attack, Decay, Sustain, Release } envStage = EnvStage::Idle;

    float attackMs = 40.0f;
    float decayMs = 120.0f;
    float sustainLevel = 0.86f;
    float releaseMs = 280.0f;

    float attackCoeff = 0.0f;   // exponential approach coefficient per sample
    float decayCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    // -- Synthesis parameters --
    float morph = 0.0f;          // 0 = ah, 1 = ooh
    float formantScale = 1.0f;
    float brightness = 0.9f;
    float breath = 0.015f;
    float aspiration = 0.01f;
    float roughness = 0.25f;
    float vibratoRate = 5.5f;    // Hz
    float vibratoDepth = 0.010f; // fraction of frequency
    float jitterAmt = 0.0015f;   // pitch jitter amount
    float shimmerAmt = 0.008f;   // amplitude shimmer amount
    float formantDrift = 0.008f; // A/B frequency spread
    bool female = true;

    // -- Modulation state --
    float vibratoPhase = 0.0f;
    float vibratoOnsetSamples = 0.0f;  // samples elapsed since note attack
    float driftPhase = 0.0f;           // A/B crossfade oscillator phase
    float jitterState = 0.0f;          // smoothed random pitch deviation
    float shimmerState = 0.0f;         // smoothed random amplitude deviation
    float articulationSamples = 0.0f;  // elapsed since last note articulation
    float articulationStrength = 1.0f; // detached notes stronger than legato
    float releaseSamples = 0.0f;       // elapsed since release stage began
    float phrasePitchDrift = 0.0f;     // slow phrase-level intonation drift
    float phrasePitchTarget = 0.0f;    // target for the slow drift
    float phrasePitchHoldSamples = 0.0f; // countdown to choose a new drift target
    float cycleOpenQOffset = 0.0f;     // per-cycle open quotient deviation
    float cycleReturnScale = 1.0f;     // per-cycle return-phase scaling
    float cycleAmplitude = 1.0f;       // per-cycle source strength variation
    float cycleBreathLeak = 0.0f;      // incomplete closure amount per cycle
    float cycleSkew = 0.0f;            // per-cycle phase skew for pulse asymmetry
    bool oddCycle = false;             // alternates each glottal cycle
    float prevPhase = 0.0f;
    float glottalOpenness = 0.0f;      // [0,1] how open the glottis is (for noise modulation)

    // -- Formant data (Hillenbrand et al. 1995) --
    // Each array: {F1, F2, F3, F4, F5} with {freq, Q, gain}
    std::array<Formant, 5> ahMale;
    std::array<Formant, 5> oohMale;
    std::array<Formant, 5> ahFemale;
    std::array<Formant, 5> oohFemale;

    // -- Formant filter state --
    // Two parallel banks for drift crossfade; targets smoothed toward current
    std::array<Formant, 5> currentFormantsA;
    std::array<Formant, 5> currentFormantsB;
    std::array<Formant, 5> targetFormantsA;
    std::array<Formant, 5> targetFormantsB;

    std::array<Biquad, 5> filtersA;  // formant bank A
    std::array<Biquad, 5> filtersB;  // formant bank B
    Biquad nasalNotch;               // anti-formant at ~300-340 Hz

    // Subglottal resonances (~600, 1400, 2000 Hz). Tracheal resonances that
    // couple through the glottis, adding subtle low-frequency body.
    std::array<Biquad, 3> subglottalFilters;
    static constexpr std::array<float, 3> subglottalFreqs = { 600.0f, 1400.0f, 2000.0f };
    static constexpr std::array<float, 3> subglottalGains = { 0.08f,  0.04f,   0.03f  };

    // -- Spectral tilt filters --
    // sourceTilt/2: shape glottal pulse spectrum
    // noiseTilt/2: shape breath noise (separate to avoid state contamination)
    // Two stages cascaded for female (-12 dB/oct), one stage for male (-6 dB/oct)
    OnePoleLP sourceTilt;
    OnePoleLP sourceTilt2;
    OnePoleLP noiseTilt;
    OnePoleLP noiseTilt2;

    juce::Random rng;
};

// ============================================================================
// VST3 plugin wrapper. Handles parameter management, MIDI routing with legato
// logic (held-note tracking), and delegates all DSP to VowelVoice.
// ============================================================================
class CleanVowelSynthAudioProcessor  : public juce::AudioProcessor
{
public:
    CleanVowelSynthAudioProcessor();
    ~CleanVowelSynthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #if ! JucePlugin_IsMidiEffect
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    VowelVoice voice;

    // Tracks all currently held MIDI notes for legato behavior.
    // When the current note is released while others are held, the voice
    // glides to the most recently held note instead of entering release.
    juce::Array<int> heldNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CleanVowelSynthAudioProcessor)
};
