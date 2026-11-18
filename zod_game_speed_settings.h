#ifndef ZOD_GAME_SPEED_SETTINGS_H
#define ZOD_GAME_SPEED_SETTINGS_H

// Seven ORIGINAL ZOD Options speeds:
// 25%, 50%, 75%, 100%, 150%, 200%, 400%.
// Save the chosen percentage, NOT a code-defined player name or session-only value.
#include <fstream>
#include <stdio.h>

static const int ZOD_GAME_SPEED_PRESETS = 7;

inline int ZodGameSpeedPercent(int preset)
{
    static const int original_speeds[ZOD_GAME_SPEED_PRESETS] =
        {25, 50, 75, 100, 150, 200, 400};
    if(preset < 0 || preset >= ZOD_GAME_SPEED_PRESETS) preset = 3;
    return original_speeds[preset];
}

inline int ZodPresetForGameSpeedPercent(int percent)
{
    for(int i = 0; i < ZOD_GAME_SPEED_PRESETS; ++i)
        if(ZodGameSpeedPercent(i) == percent) return i;
    return 3; // 100% default for a fresh installation/corrupt file.
}

inline int ZodLoadSavedGameSpeedPreset()
{
    std::ifstream settings("game_speed_settings.txt", std::ios::in);
    int percent = 100;
    if(settings && (settings >> percent))
        return ZodPresetForGameSpeedPercent(percent);
    return 3;
}

inline float ZodSavedGameSpeedMultiplier()
{
    return ZodGameSpeedPercent(ZodLoadSavedGameSpeedPreset()) / 100.0f;
}

inline bool ZodSaveGameSpeedPreset(int preset)
{
    if(preset < 0 || preset >= ZOD_GAME_SPEED_PRESETS) return false;
    std::ofstream settings("game_speed_settings.txt", std::ios::out | std::ios::trunc);
    if(!settings)
    {
        fprintf(stderr, "ZOD Game Speed: could not save settings.\n");
        return false;
    }
    settings << ZodGameSpeedPercent(preset) << "\n";
    return settings.good();
}

#endif
