// ============================================================================
// CleanVowelSynth — DSP implementation 2024-06
// See PluginProcessor.h for architecture overview.
// ============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"

// Parameter ID strings — must match between processor, editor, and state save.
namespace IDs
{
    static constexpr auto morph = "morph";
    static constexpr auto formantScale = "formantScale";
    static constexpr auto brightness = "brightness";
    static constexpr auto breath = "breath";
    static constexpr auto aspiration = "aspiration";
    static constexpr auto vibratoRate = "vibratoRate";
    static constexpr auto vibratoDepth = "vibratoDepth";
    static constexpr auto jitter = "jitter";
    static constexpr auto shimmer = "shimmer";
    static constexpr auto formantDrift = "formantDrift";
    static constexpr auto attackMs = "attackMs";
    static constexpr auto decayMs = "decayMs";
    static constexpr auto sustain = "sustain";
    static constexpr auto releaseMs = "releaseMs";
    static constexpr auto female = "female";
    static constexpr auto portamento = "portamento";
}

// ============================================================================
// VowelVoice — formant data initialization
// ============================================================================

VowelVoice::VowelVoice()
{
    // Formant data: {frequency (Hz), Q factor, relative gain}
    //
    // Sources: Hillenbrand et al. (1995), Hawks & Miller (1995)
    //
    // Q decreases for higher formants (wider bandwidth) to match real vocal
    // tracts. Higher formants have wider bandwidths in acoustic measurements.
    //
    // F1-F5 for /a/ ("ah") and /u/ ("ooh"), male and female variants:
    ahMale   = {{{730.0f,  10.0f, 1.00f}, {1090.0f, 8.0f, 0.80f}, {2440.0f, 6.5f, 0.40f}, {3200.0f, 5.5f, 0.18f}, {3700.0f, 4.5f, 0.10f}}};
    oohMale  = {{{300.0f,  10.0f, 1.00f}, {870.0f,  8.0f, 0.70f}, {2240.0f, 6.5f, 0.35f}, {3200.0f, 5.5f, 0.15f}, {3700.0f, 4.5f, 0.08f}}};
    ahFemale = {{{940.0f,  8.5f,  1.00f}, {1400.0f, 7.0f, 0.75f}, {2800.0f, 5.5f, 0.38f}, {3700.0f, 4.5f, 0.16f}, {4950.0f, 3.5f, 0.08f}}};
    oohFemale= {{{370.0f,  8.5f,  1.00f}, {950.0f,  7.0f, 0.68f}, {2650.0f, 5.5f, 0.32f}, {3700.0f, 4.5f, 0.14f}, {4950.0f, 3.5f, 0.06f}}};
}

// ============================================================================
// Biquad filter — second-order IIR (Direct Form I)
// ============================================================================

void VowelVoice::Biquad::reset()
{
    x1 = x2 = y1 = y2 = 0.0f;
}

// Standard audio EQ cookbook bandpass (constant-0-dB-peak gain).
void VowelVoice::Biquad::setBandPass(float sampleRate, float freq, float q)
{
    freq = juce::jlimit(20.0f, 0.45f * sampleRate, freq);
    q = juce::jmax(0.3f, q);

    const float w0 = 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * q);

    float nb0 = alpha;
    float nb1 = 0.0f;
    float nb2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float na1 = -2.0f * std::cos(w0);
    const float na2 = 1.0f - alpha;

    b0 = nb0 / a0;
    b1 = nb1 / a0;
    b2 = nb2 / a0;
    a1 = na1 / a0;
    a2 = na2 / a0;
}

// Peaking EQ configured as a controllable-depth notch.
// depth=0: full cut at center frequency. depth=1: no effect (unity gain).
// Used for nasal anti-formant simulation.
void VowelVoice::Biquad::setNotch(float sampleRate, float freq, float q, float depth)
{
    freq = juce::jlimit(20.0f, 0.45f * sampleRate, freq);
    q = juce::jmax(0.3f, q);
    depth = juce::jlimit(0.0f, 1.0f, depth);

    const float w0 = 2.0f * juce::MathConstants<float>::pi * freq / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * q);

    const float A = depth;
    const float a0 = 1.0f + alpha / A;
    b0 = (1.0f + alpha * A) / a0;
    b1 = (-2.0f * std::cos(w0)) / a0;
    b2 = (1.0f - alpha * A) / a0;
    a1 = b1;
    a2 = (1.0f - alpha / A) / a0;
}

float VowelVoice::Biquad::process(float x)
{
    const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
}

// ============================================================================
// One-pole lowpass — used for spectral tilt shaping
// ============================================================================

void VowelVoice::OnePoleLP::reset()
{
    z = 0.0f;
}

void VowelVoice::OnePoleLP::setCutoff(float sampleRate, float cutoffHz)
{
    cutoffHz = juce::jlimit(5.0f, 0.45f * sampleRate, cutoffHz);
    a = std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / sampleRate);
}

float VowelVoice::OnePoleLP::process(float x)
{
    z = (1.0f - a) * x + a * z;
    return z;
}

// ============================================================================
// Voice lifecycle
// ============================================================================

void VowelVoice::prepare(double sampleRate, int maximumBlockSize, int numChannels)
{
    sr = sampleRate;
    invSr = 1.0f / (float) sampleRate;
    blockSize = maximumBlockSize;
    channels = numChannels;

    for (auto& f : filtersA) f.reset();
    for (auto& f : filtersB) f.reset();
    nasalNotch.reset();
    sourceTilt.reset();
    sourceTilt2.reset();
    noiseTilt.reset();
    noiseTilt2.reset();
    for (auto& f : subglottalFilters) f.reset();

    // Subglottal resonances: fixed tracheal coupling frequencies, low Q for broad peaks
    for (size_t i = 0; i < subglottalFilters.size(); ++i)
        subglottalFilters[i].setBandPass((float) sr, subglottalFreqs[i], 2.5f);

    updateTargets();
    currentFormantsA = targetFormantsA;
    currentFormantsB = targetFormantsB;

    for (size_t i = 0; i < filtersA.size(); ++i)
    {
        filtersA[i].setBandPass((float) sr, currentFormantsA[i].freq, currentFormantsA[i].q);
        filtersB[i].setBandPass((float) sr, currentFormantsB[i].freq, currentFormantsB[i].q);
    }

    reset();
}

void VowelVoice::reset()
{
    phase = 0.0f;
    vibratoPhase = 0.0f;
    driftPhase = 0.0f;
    env = 0.0f;
    envStage = EnvStage::Idle;
    active = false;
    tailOff = false;
    currentMidiNote = -1;
    jitterState = 0.0f;
    shimmerState = 0.0f;
    oddCycle = false;
    prevPhase = 0.0f;
    glottalOpenness = 0.0f;
    vibratoOnsetSamples = 0.0f;
    formantUpdateCounter = 0;

    for (auto& f : filtersA) f.reset();
    for (auto& f : filtersB) f.reset();
    nasalNotch.reset();
    sourceTilt.reset();
    sourceTilt2.reset();
    noiseTilt.reset();
    noiseTilt2.reset();
    for (auto& f : subglottalFilters) f.reset();
}

// ============================================================================
// Parameter update
// ============================================================================

void VowelVoice::setParameters(float m, float fs, float br, float brth, float asp, float vibR, float vibD,
                               float jit, float shim, float fDrift,
                               float att, float dec, float sus, float rel, bool fem, float porta)
{
    morph = m;
    formantScale = fs;
    brightness = br;
    breath = brth;
    aspiration = asp;
    vibratoRate = vibR;
    vibratoDepth = vibD;
    jitterAmt = jit;
    shimmerAmt = shim;
    formantDrift = fDrift;
    attackMs = att;
    decayMs = dec;
    sustainLevel = sus;
    releaseMs = rel;
    female = fem;
    portamentoMs = porta;
    updateTargets();
}

VowelVoice::Formant VowelVoice::lerpFormant(const Formant& a, const Formant& b, float t, float scale) const
{
    return { juce::jmap(t, a.freq, b.freq),
             juce::jmap(t, a.q, b.q),
             juce::jmap(t, a.gain, b.gain) * scale };
}

// Recompute all derived values from current parameters. Called once per block.
void VowelVoice::updateTargets()
{
    // Morph between "ah" and "ooh" formants, selecting male/female set
    const auto& ah = female ? ahFemale : ahMale;
    const auto& oo = female ? oohFemale : oohMale;

    for (size_t i = 0; i < ah.size(); ++i)
    {
        const auto base = lerpFormant(ah[i], oo[i], morph, 1.0f);
        // A and B banks have slightly offset frequencies for drift effect
        targetFormantsA[i] = { base.freq * formantScale * (1.0f - 0.5f * formantDrift), base.q, base.gain };
        targetFormantsB[i] = { base.freq * formantScale * (1.0f + 0.5f * formantDrift), base.q, base.gain };
    }

    // Exponential envelope coefficients using e^(-5/N) where N = duration in
    // samples. 5 time constants reaches ~99.3% of target. The concave/convex
    // shape is applied in processEnvelope() by how the coefficient is used.
    const float attackSamples  = juce::jmax(1.0f, 0.001f * attackMs  * (float) sr);
    const float decaySamples   = juce::jmax(1.0f, 0.001f * decayMs   * (float) sr);
    const float releaseSamples = juce::jmax(1.0f, 0.001f * releaseMs * (float) sr);
    attackCoeff  = std::exp(-5.0f / attackSamples);
    decayCoeff   = std::exp(-5.0f / decaySamples);
    releaseCoeff = std::exp(-5.0f / releaseSamples);

    // Nasal anti-formant: subtle notch simulating velopharyngeal coupling.
    // Female ~340 Hz, male ~300 Hz; depth 0.45 = ~7 dB cut.
    nasalNotch.setNotch((float) sr, female ? 340.0f : 300.0f, 3.0f, 0.45f);

    // Spectral tilt: lowpass to roll off upper harmonics.
    // Female voices have lower cutoff (breathier), cascaded two stages for
    // -12 dB/oct. Male uses single stage for -6 dB/oct. Second stage offset
    // by 15% for smoother rolloff knee.
    const float tiltBase = female ? 2800.0f : 3200.0f;
    const float tiltTop  = female ? 4200.0f : 5200.0f;
    const float tiltT = juce::jlimit(0.0f, 1.0f, 1.4f - brightness);
    const float tiltFreq = juce::jmap(tiltT, tiltTop, tiltBase);
    sourceTilt.setCutoff((float) sr, tiltFreq);
    sourceTilt2.setCutoff((float) sr, tiltFreq * 1.15f);
    noiseTilt.setCutoff((float) sr, tiltFreq);
    noiseTilt2.setCutoff((float) sr, tiltFreq * 1.15f);
}

// Smooth current formants toward targets and update biquad coefficients.
// Called every 32 samples for performance (avoids 20 trig calls per sample).
void VowelVoice::updateFormantsSmooth()
{
    // Smoothing factor compensated for 32-sample update interval:
    // 1 - (1 - 0.0025)^32 ≈ 0.0773
    constexpr float smoothing = 0.0773f;

    // Widen formant bandwidth at high F0: harmonics are spaced further apart,
    // so narrow peaks miss them and the tone thins out.
    // At 220 Hz: qScale=1.0 (no change), at 523 Hz (C5): ~0.42, capped at 0.35.
    const float qScale = juce::jlimit(0.35f, 1.0f, 220.0f / juce::jmax(frequency, 80.0f));
    const float srf = (float) sr;

    for (size_t i = 0; i < currentFormantsA.size(); ++i)
    {
        currentFormantsA[i].freq += smoothing * (targetFormantsA[i].freq - currentFormantsA[i].freq);
        currentFormantsA[i].q    += smoothing * (targetFormantsA[i].q    - currentFormantsA[i].q);
        currentFormantsA[i].gain += smoothing * (targetFormantsA[i].gain - currentFormantsA[i].gain);

        currentFormantsB[i].freq += smoothing * (targetFormantsB[i].freq - currentFormantsB[i].freq);
        currentFormantsB[i].q    += smoothing * (targetFormantsB[i].q    - currentFormantsB[i].q);
        currentFormantsB[i].gain += smoothing * (targetFormantsB[i].gain - currentFormantsB[i].gain);

        filtersA[i].setBandPass(srf, currentFormantsA[i].freq, currentFormantsA[i].q * qScale);
        filtersB[i].setBandPass(srf, currentFormantsB[i].freq, currentFormantsB[i].q * qScale);
    }
}

// ============================================================================
// Note events
// ============================================================================

void VowelVoice::startNote(int midiNoteNumber, float velocity)
{
    currentMidiNote = midiNoteNumber;
    noteVelocity = velocity;
    frequency = (float) juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    targetFrequency = frequency;
    phaseIncrement = frequency / (float) sr;
    envStage = EnvStage::Attack;
    env = 0.0f;
    vibratoOnsetSamples = 0.0f;
    active = true;
    tailOff = false;
}

void VowelVoice::glideToNote(int midiNoteNumber, float velocity)
{
    currentMidiNote = midiNoteNumber;
    noteVelocity = velocity;
    targetFrequency = (float) juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);

    // Multiplicative portamento: frequency *= inc each sample.
    // This gives a glide that is linear in pitch (log-frequency) space,
    // so equal intervals sound like equal pitch steps regardless of direction.
    if (portamentoMs <= 0.0f || frequency <= 0.0f || targetFrequency <= 0.0f)
    {
        frequency = targetFrequency;
    }
    else
    {
        const float numSamples = 0.001f * portamentoMs * (float) sr;
        portamentoInc = std::pow(targetFrequency / frequency, 1.0f / numSamples);
    }

    // Legato: don't retrigger envelope unless voice was idle/releasing
    if (envStage == EnvStage::Release || envStage == EnvStage::Idle)
    {
        envStage = EnvStage::Attack;
        env = 0.0f;
    }
    tailOff = false;
    active = true;
}

void VowelVoice::stopNote(bool allowTailOff)
{
    if (! allowTailOff)
    {
        active = false;
        envStage = EnvStage::Idle;
        env = 0.0f;
        currentMidiNote = -1;
        return;
    }

    if (active)
        envStage = EnvStage::Release;
}

// ============================================================================
// Per-sample processing
// ============================================================================

// Exponential ADSR envelope.
// Attack: concave curve (slow start, fast finish) — mimics vocal fold engagement.
// Decay:  convex curve (fast drop, settles gently into sustain).
// Release: convex curve (fast initial fade, long natural tail).
float VowelVoice::processEnvelope()
{
    switch (envStage)
    {
        case EnvStage::Idle:
            return 0.0f;

        case EnvStage::Attack:
            env = 1.0f - (1.0f - env) * attackCoeff;
            if (env >= 0.999f) { env = 1.0f; envStage = EnvStage::Decay; }
            break;

        case EnvStage::Decay:
            env = sustainLevel + (env - sustainLevel) * decayCoeff;
            if (env <= sustainLevel + 0.001f) { env = sustainLevel; envStage = EnvStage::Sustain; }
            break;

        case EnvStage::Sustain:
            env = sustainLevel;
            break;

        case EnvStage::Release:
            env *= releaseCoeff;
            if (env <= 0.001f) { env = 0.0f; envStage = EnvStage::Idle; active = false; currentMidiNote = -1; }
            break;
    }
    return env;
}

float VowelVoice::getNoiseSample()
{
    return rng.nextFloat() * 2.0f - 1.0f;
}

// Generate one sample of glottal excitation.
//
// Signal flow:
//   1. Portamento glide (multiplicative, log-frequency linear)
//   2. Jitter (smoothed random pitch perturbation)
//   3. Vibrato with delayed onset (300ms delay + 150ms fade-in)
//   4. Phase accumulator with cycle detection
//   5. Alternate-cycle asymmetry (subharmonics from period doubling)
//   6. LF-derivative pulse: open phase (sin rise) + return phase (cos decay) + closed
//   7. Spectral tilt (one-pole LP, cascaded 2x for female)
//   8. Shimmer (smoothed random amplitude modulation)
//
float VowelVoice::processGlottal()
{
    // --- 1. Portamento ---
    if (frequency != targetFrequency)
    {
        frequency *= portamentoInc;
        if ((portamentoInc > 1.0f && frequency >= targetFrequency) ||
            (portamentoInc < 1.0f && frequency <= targetFrequency))
        {
            frequency = targetFrequency;
            portamentoInc = 1.0f;
        }
    }

    // --- 2. Jitter & shimmer state (smoothed random walk) ---
    jitterState += 0.0015f * ((rng.nextFloat() * 2.0f - 1.0f) - jitterState);
    shimmerState += 0.0012f * ((rng.nextFloat() * 2.0f - 1.0f) - shimmerState);

    // --- 3. Vibrato with delayed onset ---
    // Real singers establish the note pitch before adding vibrato.
    // ~300ms silence then ~150ms linear fade-in.
    vibratoOnsetSamples += 1.0f;
    const float delaySamples = 0.30f * (float) sr;
    const float fadeInSamples = 0.15f * (float) sr;
    const float vibEnv = juce::jlimit(0.0f, 1.0f, (vibratoOnsetSamples - delaySamples) / fadeInSamples);
    vibratoPhase += vibratoRate * invSr;
    if (vibratoPhase >= 1.0f)
        vibratoPhase -= 1.0f;

    // --- 4. Phase accumulator ---
    const float vib = 1.0f + vibEnv * vibratoDepth * std::sin(2.0f * juce::MathConstants<float>::pi * vibratoPhase);
    const float f = frequency * vib * (1.0f + jitterAmt * jitterState);
    prevPhase = phase;
    phase += f * invSr;
    if (phase >= 1.0f)
    {
        phase -= std::floor(phase);
        oddCycle = !oddCycle;  // flip on each new glottal cycle
    }

    // --- 5. Alternate-cycle asymmetry ---
    // Models natural vocal fold irregularity (period doubling).
    // Stronger at low pitches (chest voice) and during note onset/offset
    // (where vocal fry naturally occurs).
    const float asymPitch = juce::jlimit(0.0f, 1.0f, (300.0f - frequency) / 200.0f);
    const float asymEnv   = juce::jlimit(0.0f, 1.0f, 1.0f - env * 2.5f);
    const float asymAmt   = 0.15f * juce::jmax(asymPitch, asymEnv * 0.5f);
    const float oqMod = oddCycle ? (1.0f - 0.06f * asymAmt) : 1.0f;
    const float ampMod = oddCycle ? (1.0f - asymAmt) : 1.0f;

    // --- 6. LF-derivative glottal pulse ---
    // Three phases per glottal cycle:
    //   [0, tp):   Opening — vocal folds parting, airflow increases (sin rise)
    //   [tp, oq):  Return  — folds closing, airflow decreases (cos decay)
    //   [oq, 1):   Closed  — folds shut, no airflow (silence)
    //
    // Female voices: higher open quotient (0.58 vs 0.52) and longer return
    // phase (0.18 vs 0.12) for breathier, less abrupt closure.
    const float oq = juce::jlimit(0.48f, 0.72f, (female ? 0.58f : 0.52f) - 0.08f * (brightness - 1.0f)) * oqMod;
    const float rp = (female ? 0.18f : 0.12f) * oqMod;
    const float tp = oq - rp;

    float pulse = 0.0f;

    if (phase < tp)
    {
        const float x = phase / tp;
        pulse = std::sin(0.5f * juce::MathConstants<float>::pi * x);
    }
    else if (phase < oq)
    {
        const float x = (phase - tp) / rp;
        pulse = std::cos(0.5f * juce::MathConstants<float>::pi * x);
    }

    pulse *= ampMod;

    // Track glottal openness for noise modulation in render().
    // During open phase (0 to oq): openness follows the pulse shape.
    // During closed phase (oq to 1): openness is zero.
    glottalOpenness = (phase < oq) ? (phase < tp ? std::sin(0.5f * juce::MathConstants<float>::pi * (phase / tp))
                                                  : std::cos(0.5f * juce::MathConstants<float>::pi * ((phase - tp) / rp)))
                                   : 0.0f;

    // --- 7. Spectral tilt ---
    pulse = sourceTilt.process(pulse);
    if (female)
        pulse = sourceTilt2.process(pulse);

    // --- 8. Shimmer ---
    pulse *= (1.0f + shimmerAmt * shimmerState);
    return pulse;
}

// ============================================================================
// Render — main per-block DSP loop
//
// Signal flow per sample:
//   Glottal source
//   + Breath/aspiration noise (high-passed, onset-weighted)
//   -> 5-formant parallel bandpass bank A & B
//   -> Linear crossfade A/B (slow drift oscillator)
//   -> Nasal anti-formant notch
//   -> Envelope
//   -> Soft saturation (fast Pade tanh)
//   -> Output (mono, accumulated)
// ============================================================================

void VowelVoice::render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (! active)
        return;

    auto* out = buffer.getWritePointer(0);
    const float driftInc = 0.45f * invSr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Formant filter coefficients: updated every 32 samples.
        // The smoothing is slow enough that skipping samples is inaudible,
        // but avoids 20 sin/cos calls per sample from setBandPass().
        if (--formantUpdateCounter <= 0)
        {
            formantUpdateCounter = 32;
            updateFormantsSmooth();
        }

        float src = processGlottal();

        // Breath noise: modulated by glottal openness so aspiration is strongest
        // when the vocal folds are apart (open phase) and quietest when closed.
        // This creates the characteristic "breathy" quality of real female voices
        // rather than the flat white-noise-on-top effect of unmodulated noise.
        // Aspiration: extra noise weighted by onset (high when envelope is low).
        // Both use high-passed noise for brightness.
        if (breath > 0.0f || aspiration > 0.0f)
        {
            const float onsetWeight = juce::jlimit(0.0f, 1.0f, 1.0f - env * 4.0f);
            const float n = getNoiseSample();
            float noiseLp = noiseTilt.process(n);
            if (female)
                noiseLp = noiseTilt2.process(noiseLp);
            const float noiseBright = n - noiseLp;

            // Glottal modulation: noise amplitude follows vocal fold opening.
            // Mix 60% modulated + 40% unmodulated to avoid complete silence
            // during closed phase (some turbulence leaks through in real speech).
            const float noiseModulation = 0.4f + 0.6f * glottalOpenness;
            src += breath * 0.25f * noiseBright * noiseModulation;
            src += aspiration * onsetWeight * noiseBright * noiseModulation;
        }

        // Parallel formant filtering: source is fed through two banks of 5
        // bandpass filters. Each formant's output is scaled by its gain.
        float a = 0.0f, b = 0.0f;
        for (size_t k = 0; k < filtersA.size(); ++k)
        {
            a += currentFormantsA[k].gain * filtersA[k].process(src);
            b += currentFormantsB[k].gain * filtersB[k].process(src);
        }

        // Subglottal coupling: tracheal resonances at ~600, 1400, 2000 Hz.
        // These couple through the glottis and add subtle body/warmth,
        // especially noticeable in the low-mid range. Processed in parallel
        // on the source, mixed at low gain to avoid muddying formant structure.
        float sg = 0.0f;
        for (size_t k = 0; k < subglottalFilters.size(); ++k)
            sg += subglottalGains[k] * subglottalFilters[k].process(src);

        // Slow linear crossfade between A and B banks (~0.45 Hz).
        // Simulates micro-variations in vocal tract shape.
        driftPhase += driftInc;
        if (driftPhase >= 1.0f)
            driftPhase -= 1.0f;
        const float mix = driftPhase;
        float y = (1.0f - mix) * a + mix * b + sg;

        // Nasal anti-formant: subtle notch at ~300-340 Hz
        y = nasalNotch.process(y);

        // Amplitude envelope
        y *= processEnvelope();

        // Soft saturation: Pade [1/1] tanh approximation.
        // x*(27+x^2) / (27+9*x^2) is accurate to ~0.1% for |x| < 3.
        // The 1.4x drive adds warmth without hard clipping.
        {
            const float x = 1.4f * y;
            const float x2 = x * x;
            y = x * (27.0f + x2) / (27.0f + 9.0f * x2);
        }

        out[startSample + i] += y * noteVelocity;
    }
}

// ============================================================================
// Plugin wrapper — parameter layout, MIDI handling, state persistence
// ============================================================================

CleanVowelSynthAudioProcessor::CleanVowelSynthAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{}

juce::AudioProcessorValueTreeState::ParameterLayout CleanVowelSynthAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    //                                        ID                  Name              Range                    Default
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::morph,         "Morph",         NormalisableRange<float>(0.0f, 1.0f),      0.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::formantScale,  "Formant Scale", NormalisableRange<float>(0.75f, 1.35f),    1.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::brightness,    "Brightness",    NormalisableRange<float>(0.75f, 1.5f),     0.9f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::breath,        "Breath",        NormalisableRange<float>(0.0f, 0.06f),     0.015f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::aspiration,    "Aspiration",    NormalisableRange<float>(0.0f, 0.05f),     0.01f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::vibratoRate,   "Vibrato Rate",  NormalisableRange<float>(0.0f, 8.0f),      5.5f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::vibratoDepth,  "Vibrato Depth", NormalisableRange<float>(0.0f, 0.03f),     0.010f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::jitter,        "Jitter",        NormalisableRange<float>(0.0f, 0.006f),    0.0015f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::shimmer,       "Shimmer",       NormalisableRange<float>(0.0f, 0.03f),     0.008f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::formantDrift,  "Formant Drift", NormalisableRange<float>(0.0f, 0.02f),     0.008f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::attackMs,      "Attack ms",     NormalisableRange<float>(1.0f, 1000.0f),   40.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::decayMs,       "Decay ms",      NormalisableRange<float>(1.0f, 1000.0f),   120.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::sustain,       "Sustain",       NormalisableRange<float>(0.0f, 1.0f),      0.86f));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::releaseMs,     "Release ms",    NormalisableRange<float>(1.0f, 2000.0f),   280.0f));
    p.push_back(std::make_unique<AudioParameterBool> (IDs::female,        "Female",        true));
    p.push_back(std::make_unique<AudioParameterFloat>(IDs::portamento,    "Portamento ms", NormalisableRange<float>(0.0f, 500.0f),    60.0f));

    return { p.begin(), p.end() };
}

void CleanVowelSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    voice.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void CleanVowelSynthAudioProcessor::releaseResources() {}

#if ! JucePlugin_IsMidiEffect
bool CleanVowelSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}
#endif

// Process one audio block: update parameters, handle MIDI with legato, render.
//
// MIDI legato logic:
//   - Note-on while voice active: glide to new note (no envelope retrigger)
//   - Note-off of current note while others held: glide to most recent held note
//   - Note-off with nothing held: normal release
//   - All-notes-off: immediate kill
void CleanVowelSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    voice.setParameters(*apvts.getRawParameterValue(IDs::morph),
                        *apvts.getRawParameterValue(IDs::formantScale),
                        *apvts.getRawParameterValue(IDs::brightness),
                        *apvts.getRawParameterValue(IDs::breath),
                        *apvts.getRawParameterValue(IDs::aspiration),
                        *apvts.getRawParameterValue(IDs::vibratoRate),
                        *apvts.getRawParameterValue(IDs::vibratoDepth),
                        *apvts.getRawParameterValue(IDs::jitter),
                        *apvts.getRawParameterValue(IDs::shimmer),
                        *apvts.getRawParameterValue(IDs::formantDrift),
                        *apvts.getRawParameterValue(IDs::attackMs),
                        *apvts.getRawParameterValue(IDs::decayMs),
                        *apvts.getRawParameterValue(IDs::sustain),
                        *apvts.getRawParameterValue(IDs::releaseMs),
                        apvts.getRawParameterValue(IDs::female)->load() > 0.5f,
                        *apvts.getRawParameterValue(IDs::portamento));

    // Sample-accurate MIDI: render audio between events, process event, repeat.
    int samplePos = 0;
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        const int msgPos = metadata.samplePosition;
        const int blockLen = juce::jlimit(0, buffer.getNumSamples() - samplePos, msgPos - samplePos);

        if (blockLen > 0)
            voice.render(buffer, samplePos, blockLen);

        if (msg.isNoteOn())
        {
            if (voice.isActive())
                voice.glideToNote(msg.getNoteNumber(), msg.getFloatVelocity());
            else
                voice.startNote(msg.getNoteNumber(), msg.getFloatVelocity());

            heldNotes.add(msg.getNoteNumber());
        }
        else if (msg.isNoteOff())
        {
            heldNotes.removeFirstMatchingValue(msg.getNoteNumber());

            if (msg.getNoteNumber() == voice.getCurrentMidiNote())
            {
                if (heldNotes.size() > 0)
                    voice.glideToNote(heldNotes.getLast(), voice.getVelocity());
                else
                    voice.stopNote(true);
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            heldNotes.clear();
            voice.stopNote(false);
        }

        samplePos = msgPos;
    }

    // Render remaining samples after last MIDI event
    if (samplePos < buffer.getNumSamples())
        voice.render(buffer, samplePos, buffer.getNumSamples() - samplePos);
}

juce::AudioProcessorEditor* CleanVowelSynthAudioProcessor::createEditor()
{
    return new CleanVowelSynthAudioProcessorEditor (*this);
}

// State persistence: serialize/deserialize all parameter values as XML.
void CleanVowelSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void CleanVowelSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CleanVowelSynthAudioProcessor();
}
