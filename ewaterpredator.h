#ifndef ZOD_EWATERPREDATOR_H
#define ZOD_EWATERPREDATOR_H

#include "zeffect.h"
#include <cstdio>

// Client-only visual effect; all combat casualties are decided by the server.
// The temporary sprite set is separate from any original graphics.
class EWaterPredator : public ZEffect
{
    static constexpr int frame_count = 12;
    static constexpr int frame_ms = 100;
    int x,y,palette;
    double start;
    ZSDL_Surface *prey;
    int submerge;
    struct Frames
    {
        bool attempted = false;
        ZSDL_Surface images[frame_count];
        bool Load(int planet)
        {
            if(!attempted)
            {
                attempted = true;
                for(int i=0;i<frame_count;++i)
                {
                    char path[160];
                    std::snprintf(path,sizeof(path),"assets/other/predators/%s_%02d.png",
                        planet == CITY ? "city_monster" : "jungle_croc",i);
                    images[i].LoadBaseImage(path);
                }
            }
            for(int i=0;i<frame_count;++i) if(!images[i].GetBaseSurface()) return false;
            return true;
        }
    };
    static Frames &GetFrames(int planet)
    {
        static Frames city, jungle;
        return planet == CITY ? city : jungle;
    }
    int Frame() const
    {
        const double elapsed = ztime->ztime - start;
        return elapsed <= 0 ? 0 : static_cast<int>(elapsed * 1000.0 / frame_ms);
    }
public:
    static void Init() { GetFrames(CITY).Load(CITY); GetFrames(JUNGLE).Load(JUNGLE); }
    EWaterPredator(ZTime *time, int x_, int y_, int planet, ZSDL_Surface *prey_=NULL, int submerge_=0) : ZEffect(time),
        x(x_),y(y_),palette(planet),start(time->ztime),prey(prey_),submerge(submerge_)
    {
        if(planet != CITY && planet != JUNGLE) { killme=true; return; }
        if(!GetFrames(planet).Load(planet)) killme=true;
        // Sound works even if the optional art is missing; respects viewport,
        // battle audio gate and ordinary effects volume.
        ZSoundEngine::PlayWavRestricted(planet == CITY ? WATER_MONSTER_SND : WATER_CROC_SND,
                                        x-24,y-20,48,40);
    }
    void Process() override { if(Frame() >= frame_count) killme=true; }
    void DoRender(ZMap &map, SDL_Surface *) override
    {
        if(killme) return;
        const int i=Frame();
        if(i<0 || i>=frame_count) return;
        // Use the original tinted robot sprite while it is dragged down.
        // No pointer to the deleted unit is kept, only a static image.
        if(prey && i<6 && prey->GetBaseSurface())
        {
            SDL_Surface *surface=prey->GetBaseSurface();
            const int sink = submerge + i*i/2;
            const int h=surface->h-sink;
            SDL_Rect src,dst;
            if(h>0 && map.GetBlitInfo(x-surface->w/2,y+8-surface->h+sink,
                                      surface->w,h,src,dst))
                prey->BlitSurface(&src,&dst);
        }
        ZSDL_Surface &image = GetFrames(palette).images[i];
        if(image.GetBaseSurface()) map.RenderZSurface(&image,x-24,y-24);
    }
};
#endif
