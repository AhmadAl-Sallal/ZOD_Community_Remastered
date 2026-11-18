#ifndef ZOD_VOLUME_SETTINGS_H
#define ZOD_VOLUME_SETTINGS_H

// Persisted sound effects and background music volume for the ZOD main menu.
// The values are independent percentages (0..100), never fixed player presets.
#include <fstream>
#include <stdio.h>

struct ZodSavedVolumes
{
    int sound;
    int music;
    ZodSavedVolumes() : sound(100), music(100) {}
};

inline int ZodClampPercent(int value)
{
    if(value < 0) return 0;
    if(value > 100) return 100;
    return value;
}

inline ZodSavedVolumes ZodLoadSavedVolumes()
{
    ZodSavedVolumes result;
    std::ifstream file("volume_settings.txt", std::ios::in);
    int sound, music;
    if(file && (file >> sound >> music))
    {
        if(sound >= 0 && sound <= 100 && music >= 0 && music <= 100)
        {
            result.sound = sound;
            result.music = music;
        }
    }
    return result;
}

inline bool ZodSaveVolumes(const ZodSavedVolumes& values)
{
    std::ofstream file("volume_settings.txt",
                       std::ios::out | std::ios::trunc);
    if(!file)
    {
        fprintf(stderr, "ZOD Volume: cannot save volume_settings.txt\n");
        return false;
    }
    file << ZodClampPercent(values.sound) << "\n"
         << ZodClampPercent(values.music) << "\n";
    return file.good();
}

inline void ZodSaveSoundPercent(int sound)
{
    ZodSavedVolumes values = ZodLoadSavedVolumes();
    values.sound = ZodClampPercent(sound);
    ZodSaveVolumes(values);
}

inline void ZodSaveMusicPercent(int music)
{
    ZodSavedVolumes values = ZodLoadSavedVolumes();
    values.music = ZodClampPercent(music);
    ZodSaveVolumes(values);
}

#endif
