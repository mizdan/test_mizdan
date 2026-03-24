# CleanVowelSynth (Ubuntu VST3 starter)

This is a JUCE-based VST3 instrument starter for Ubuntu/Linux.

## What it is

- Monophonic vowel pad / vocal-ish synth
- MIDI input instrument
- VST3 target
- Cleaned-up DSP to reduce clicky artifacts

## Requirements

- Ubuntu 22.04+ recommended
- CMake 3.22+
- GCC or Clang
- JUCE checkout
- VST3-capable host such as REAPER or Bitwig

## Install build deps on Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git pkg-config \
    libasound2-dev libjack-jackd2-dev libx11-dev libxext-dev libxrandr-dev \
    libxinerama-dev libxcursor-dev libxcomposite-dev libxrender-dev \
    libxkbcommon-x11-dev libfreetype6-dev libfontconfig1-dev \
    libglu1-mesa-dev mesa-common-dev
```

## Get JUCE

Either clone JUCE next to this project:

```bash
git clone https://github.com/juce-framework/JUCE.git
```

So the folder layout becomes:

```text
CleanVowelSynthLinux/
  CMakeLists.txt
  README.md
  source/
  JUCE/
```

Or keep JUCE elsewhere and pass `-DJUCE_DIR=/path/to/JUCE` when configuring.

## Build

### Option 1: JUCE inside this project

```bash
cmake -B build -G Ninja
cmake --build build --config Release
```

### Option 2: JUCE somewhere else

```bash
cmake -B build -G Ninja -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

## Plugin output

The VST3 bundle is typically produced at:

```text
build/CleanVowelSynth_artefacts/Release/VST3/CleanVowelSynth.vst3
```

## Install for your user

```bash
mkdir -p ~/.vst3
cp -r build/CleanVowelSynth_artefacts/Release/VST3/CleanVowelSynth.vst3 ~/.vst3/
```

Then rescan plugins in your DAW.

## Notes

- This starter is intentionally simple and monophonic.
- The editor is basic but functional.
- Good next upgrades: polyphony, more vowels, pitch bend, modulation wheel, oversampling.
# test_mizdan
