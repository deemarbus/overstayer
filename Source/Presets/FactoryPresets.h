#pragma once

#include <array>
#include <span>

namespace ivory::presets
{
struct Setting { const char* id; float value; };
struct FactoryPreset { const char* name; std::span<const Setting> settings; };

inline constexpr Setting warmth[] { {"input", 0}, {"mix", 100} };
inline constexpr Setting drumBus[] { {"input", 11}, {"mix", 75}, {"dual", 1}, {"lowCut", 1}, {"classicMode", 0} };
inline constexpr Setting kick[] { {"input", 17}, {"mix", 90}, {"second", 1}, {"dual", 1}, {"classicMode", 4} };
inline constexpr Setting nineOhNine[] { {"input", 13}, {"mix", 78}, {"second", 1}, {"classicMode", 1}, {"highShelf", 1} };
inline constexpr Setting acid[] { {"input", 18}, {"dual", 1}, {"second", 1}, {"classicMode", 2}, {"mix", 85} };
inline constexpr Setting synth[] { {"input", 7}, {"second", 1}, {"classicMode", 3}, {"mix", 90} };
inline constexpr Setting transformer[] { {"input", 14}, {"dual", 1}, {"second", 1}, {"classicMode", 4}, {"lowShelf", 1} };
inline constexpr Setting tape[] { {"input", 9}, {"classicMode", 5}, {"mix", 100} };
inline constexpr Setting vocal[] { {"input", 6}, {"mix", 70}, {"extended", 1}, {"hpOn", 1}, {"hpFreq", 75}, {"p1Freq", 13}, {"p1Pre", 5}, {"treblePre", 2} };
inline constexpr Setting parallel[] { {"input", 25}, {"dual", 1}, {"second", 1}, {"mix", 22}, {"classicMode", 1} };
inline constexpr Setting master[] { {"input", 2}, {"mix", 70}, {"oversampling", 2}, {"extended", 1}, {"bassPre", 1.5f}, {"treblePre", 1}, {"p1Pre", -1.0f} };
inline constexpr Setting heavy[] { {"input", 30}, {"dual", 1}, {"second", 1}, {"classicMode", 2}, {"mix", 100}, {"output", -2} };

inline constexpr std::array<FactoryPreset, 12> factory {
    FactoryPreset {"Subtle Analog Warmth", warmth},
    FactoryPreset {"Drum Bus Saturation", drumBus},
    FactoryPreset {"Techno Kick Drive", kick},
    FactoryPreset {"909 Drum Character", nineOhNine},
    FactoryPreset {"Acid Bass Harmonics", acid},
    FactoryPreset {"Analog Synth Color", synth},
    FactoryPreset {"Transformer Bass", transformer},
    FactoryPreset {"Tape-Style Treble", tape},
    FactoryPreset {"Vocal Presence", vocal},
    FactoryPreset {"Parallel Distortion", parallel},
    FactoryPreset {"Master Bus Harmonics", master},
    FactoryPreset {"Heavy Saturation", heavy}
};
} // namespace ivory::presets
