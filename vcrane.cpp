#include "vcrane.h"
#include "zteam.h"

using namespace COMMON;

// The crane arm is already painted into every base_crane sprite.
// Only the independently animated hook and construction crew remain separate.
const double hook_time_int = 0.7;

ZSDL_Surface VCrane::base[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VCrane::empty;
ZSDL_Surface VCrane::damaged[MAX_TEAM_TYPES];
ZSDL_Surface VCrane::hook[16];
ZSDL_Surface VCrane::wasted;

VCrane::VCrane(ZTime *ztime_, ZSettings *zsettings_) : ZVehicle(ztime_, zsettings_)
{
    width = 2;
    height = 2;
    width_pix = 32;
    height_pix = 32;
    hover_name = "Crane";
    object_name = "crane";
    InitTypeId(object_type, CRANE);

    move_i = rand() % 3;
    hook_i = 0;
    anim_on = false;
    conco_anim = NULL;
    next_hook_time = ztime->ztime + hook_time_int;
}

VCrane::~VCrane()
{
    if(conco_anim) delete conco_anim;
}

void VCrane::Init()
{
    char neutral_name[500], mask_name[500];
    int i, j, k;

    // Only a Crane with no team owner is capturable and visually neutral.
    // An owned Crane is team-colored, even if temporarily driverless.
    empty.LoadBaseImage("assets/units/vehicles/crane/empty_crane.png");

    // All 24 combined crane-and-truck frames: 8 angles x 3 tread phases.
    // No separate blue/green/yellow PNGs and no separate crane_rXXX sprites.
    for(j=0; j<MAX_ANGLE_TYPES; ++j)
        for(k=0; k<3; ++k)
        {
            sprintf(neutral_name,
                "assets/units/vehicles/crane/base_crane_r%03d_%02d.png",
                ROTATION[j], k+1);
            sprintf(mask_name,
                "assets/units/vehicles/crane/base_crane_r%03d_%02d_tintmask.png",
                ROTATION[j], k+1);
            ZSDL_Surface neutral, mask;
            neutral.LoadBaseImage(neutral_name);
            mask.LoadBaseImage(mask_name);
            for(i=1; i<MAX_TEAM_TYPES; ++i)
            {
                SDL_Surface *made = ZTeam::MakeNeutralTint(
                    i, neutral.GetBaseSurface(), mask.GetBaseSurface());
                if(made) base[i][j][k].LoadBaseImage(made);
                else base[i][j][k].LoadBaseImage(neutral_name);
            }
        }

    // Hook sprites were moved to crane/effects; preserve original 16-step
    // forward/backward animation from the eight actual image files.
    for(j=0; j<8; ++j)
    {
        sprintf(neutral_name,
            "assets/units/vehicles/crane/effects/hook_n%02d.png", j);
        hook[j].LoadBaseImage(neutral_name);
        hook[15-j] = hook[j];
    }

    // Separate visual states:
    // destroyed_crane = damaged but still alive (ShowDamaged).
    // wasted_crane    = zero health / dead (IsDestroyed).
    ZSDL_Surface neutral_destroyed, mask_destroyed;
    neutral_destroyed.LoadBaseImage("assets/units/vehicles/crane/destroyed_crane.png");
    mask_destroyed.LoadBaseImage("assets/units/vehicles/crane/destroyed_crane_tintmask.png");
    for(i=1; i<MAX_TEAM_TYPES; ++i)
    {
        SDL_Surface *made = ZTeam::MakeNeutralTint(
            i, neutral_destroyed.GetBaseSurface(), mask_destroyed.GetBaseSurface());
        if(made) damaged[i].LoadBaseImage(made);
        else damaged[i].LoadBaseImage("assets/units/vehicles/crane/destroyed_crane.png");
    }

    // A dead Crane has one neutral wreck sprite regardless of former owner.
    // No per-team coloring and no wasted_crane_tintmask.png.
    wasted.LoadBaseImage("assets/units/vehicles/crane/wasted_crane.png");
}

int VCrane::Process()
{
    double &the_time = ztime->ztime;
    if(moving && the_time >= next_move_time)
    {
        move_i = (move_i + 1) % 3;
        next_move_time = the_time +
            (VEHICLE_MOVE_ANIM_SPEED * SpeedOffsetPercentInv());
    }

    if(anim_on && the_time >= next_hook_time)
    {
        next_hook_time = the_time + 0.01;
        hook_i = (hook_i + 1) % 16;
    }

    if(conco_anim)
    {
        if(conco_anim->KillMe())
        {
            delete conco_anim;
            conco_anim = NULL;
        }
        else
            conco_anim->Process();
    }
    return 1;
}

void VCrane::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
    int render_x = loc.x;
    int render_y = loc.y;

    if(IsDestroyed())
    {
        AdjustRenderPositionForScale(&wasted, render_x, render_y);
        the_map.RenderZSurface(&wasted, render_x, render_y);
        return;
    }

    // The gameplay capture rule uses owner == NULL_TEAM, not driver count.
    // That state is neutral gray; after capture it uses normal team color.
    // Damage is a separate, repairable appearance.
    const bool is_empty = (owner == NULL_TEAM);
    const bool is_damaged = !is_empty && ShowDamaged();
    ZSDL_Surface *body;
    if(is_empty) body = &empty;
    else if(is_damaged) body = &damaged[owner];
    else body = &base[owner][direction][move_i];
    AdjustRenderPositionForScale(body, render_x, render_y);
    the_map.RenderZSurface(body, render_x, render_y, do_hit_effect);

    // Do not draw the OLD independently rotating crane_rXXX graphics:
    // those crane arms are already permanently mounted in base_crane.
    // The original hook remains independently animated.
    // Empty already has its hanging hook in the 50x40 sprite. Likewise,
    // damaged_crane is the full damaged visual; do not double-draw a hook.
    if(!is_empty && !is_damaged)
    {
        static const int crane_x[8] = {-6, -3, 0, 3, 6, 1, 0, -2};
        static const int crane_y[8] = {-6, -4, -5, -4, -6, -8, -9, -8};
        static const int hook_x[8]  = { 0,  4,14,23,25,21,14,  5};
        static const int hook_y[8]  = {14, 20,23,20,14, 8, 5,  8};
        render_x = loc.x + crane_x[direction] + hook_x[direction];
        render_y = loc.y + crane_y[direction] + hook_y[direction];
        AdjustRenderPositionForScale(&hook[hook_i], render_x, render_y);
        the_map.RenderZSurface(&hook[hook_i], render_x, render_y, do_hit_effect);
    }

    // Keep work crew in the foreground so the truck cannot hide it.
    if(conco_anim && !conco_anim->KillMe())
        conco_anim->DoRender(the_map, dest);

    do_hit_effect = false;
}

void VCrane::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
    if(effect_list)
        effect_list->insert(effect_list->begin(),
            (ZEffect*)(new EDeath(ztime, loc.x, loc.y, EDEATH_CRANE)));
}

void VCrane::DoCraneAnim(bool on_, ZObject *rep_obj)
{
    if(on_)
    {
        next_hook_time = 0;
        if(rep_obj)
        {
            unsigned char ot, oid;
            rep_obj->GetObjectID(ot, oid);
            if(ot == BUILDING_OBJECT)
            {
                if(conco_anim)
                {
                    delete conco_anim;
                    conco_anim = NULL;
                }
                int bx, by, bw, bh;
                rep_obj->GetCords(bx, by);
                rep_obj->GetDimensionsPixel(bw, bh);
                const bool is_bridge = (oid == BRIDGE_VERT || oid == BRIDGE_HORZ);
                const bool is_back_fort = (oid == FORT_BACK);
                conco_anim = new ECraneConco(ztime, owner, loc.x, loc.y,
                                            bx, by, bw, bh, is_bridge, is_back_fort);
            }
        }
    }
    else
    {
        hook_i = 0;
        if(conco_anim)
            conco_anim->BeginDeath(loc.x, loc.y);
    }
    anim_on = on_;
}
