#pragma once
#include <array>

struct BundledEffect {const char*name;const char*folder;const char*description;};
inline const std::array<BundledEffect,2>&bundledEffects(){
 static const std::array<BundledEffect,2>effects{{
  {"Auto-Leveler","null_jsfx_auto_leveler.vst3","Gently evens out changes in average volume."},
  {"Pressure","null_jsfx_pressure.vst3","One-control compressor for louder passages."}
 }};return effects;
}
inline juce::File bundledEffectFile(int index){return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("Plugins").getChildFile(bundledEffects().at(size_t(index)).folder);}
