#ifndef _VCRANE_H_
#define _VCRANE_H_

#include "zvehicle.h"

class VCrane : public ZVehicle
{
public:
    VCrane(ZTime *ztime_, ZSettings *zsettings_ = NULL);
    ~VCrane();

    static void Init();
    int Process();
    void DoRender(ZMap &the_map, SDL_Surface *dest,
                  int shift_x = 0, int shift_y = 0);
    void DoDeathEffect(bool do_fire_death, bool do_missile_death);
    void DoCraneAnim(bool on_, ZObject *rep_obj = NULL);

private:
    static ZSDL_Surface base[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
    static ZSDL_Surface empty;
    static ZSDL_Surface damaged[MAX_TEAM_TYPES];
    static ZSDL_Surface hook[16];
    static ZSDL_Surface wasted;

    int hook_i;
    double next_hook_time;
    bool anim_on;
    ECraneConco *conco_anim;
};

#endif
