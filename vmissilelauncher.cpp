#include "vmissilelauncher.h"

using namespace COMMON;

const double turrent_time_int = 1.0;

ZSDL_Surface VMissileLauncher::base[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VMissileLauncher::top[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface VMissileLauncher::wasted;

VMissileLauncher::VMissileLauncher(ZTime *ztime_, ZSettings *zsettings_) : ZVehicle(ztime_, zsettings_)
{
	double &the_time = ztime->ztime;

	width = 2;
	height = 2;
	width_pix = 32;
	height_pix = 32;
	hover_name = "M Missile";
	object_name = "missilelauncher";

	InitTypeId(object_type, MISSILE_LAUNCHER);

	//object_id = MISSILE_LAUNCHER;
	//move_speed = MISSILE_LAUNCHER_SPEED;
	//attack_radius = MMISSILE_ATTACK_RADIUS;
	//max_health = MMISSILE_MAX_HEALTH;
	//health = max_health;

	//damage = MMISSILE_DAMAGE;
	//damage_int_time = MMISSILE_ATTACK_SPEED;
	//damage_radius = MMISSILE_DAMAGE_RADIUS;
	//missile_speed = MMISSILE_MISSILE_SPEED;

	has_explosives = true;
	damage_is_missile = true;
	
	t_direction = direction;
	move_i = rand() % 3;
	
	next_turrent_time = the_time;
	next_turrent_time += turrent_time_int;
}

void VMissileLauncher::Init()
{
    int i, j, k;
    char filename_c[500];
    SDL_Surface *temp_surface;

    // Unowned missile launcher: one neutral sprite, no team tint.
    strcpy(filename_c, "assets/units/vehicles/missile_launcher/empty_missile.png");
    temp_surface = IMG_Load_Error(filename_c);
    for(j=0; j<MAX_ANGLE_TYPES; ++j)
        for(k=0; k<3; ++k)
            base[NULL_TEAM][j][k] = temp_surface;
    ZSDL_FreeSurface(temp_surface);

    // Eight original movement directions, with three frames each.
    // Only the supplied individual masks decide which pixels change team color.
    // Do not expand the masks or recolor the hand-adjusted unmasked red pixels.
    for(j=0; j<MAX_ANGLE_TYPES; ++j)
        for(k=0; k<3; ++k)
        {
            char neutral_filename[500];
            char mask_filename[500];
            sprintf(neutral_filename,
                "assets/units/vehicles/missile_launcher/base_missile_r%03d_%02d.png",
                ROTATION[j], k+1);
            sprintf(mask_filename,
                "assets/units/vehicles/missile_launcher/base_missile_r%03d_%02d_tintmask.png",
                ROTATION[j], k+1);

            ZSDL_Surface neutral, mask;
            neutral.LoadBaseImage(neutral_filename);
            mask.LoadBaseImage(mask_filename);
            for(i=1; i<MAX_TEAM_TYPES; ++i)
            {
                SDL_Surface *made = ZTeam::MakeNeutralTint(
                    i, neutral.GetBaseSurface(), mask.GetBaseSurface());
                if(made) base[i][j][k].LoadBaseImage(made);
                else base[i][j][k].LoadBaseImage(neutral_filename);
            }
        }

    // The launcher rotates independently: eight turret drawings and masks.
    for(j=0; j<MAX_ANGLE_TYPES; ++j)
    {
        char neutral_filename[500];
        char mask_filename[500];
        sprintf(neutral_filename,
            "assets/units/vehicles/missile_launcher/top_missile_r%03d.png",
            ROTATION[j]);
        sprintf(mask_filename,
            "assets/units/vehicles/missile_launcher/top_missile_r%03d_tintmask.png",
            ROTATION[j]);

        ZSDL_Surface neutral, mask;
        neutral.LoadBaseImage(neutral_filename);
        mask.LoadBaseImage(mask_filename);
        for(i=1; i<MAX_TEAM_TYPES; ++i)
        {
            SDL_Surface *made = ZTeam::MakeNeutralTint(
                i, neutral.GetBaseSurface(), mask.GetBaseSurface());
            if(made) top[i][j].LoadBaseImage(made);
            else top[i][j].LoadBaseImage(neutral_filename);
        }
    }

    // Destroyed = fully dead. One common neutral wreck, no tintmask.
    wasted.LoadBaseImage("assets/units/vehicles/missile_launcher/wasted_missile.png");
}


int VMissileLauncher::Process()
{
	double &the_time = ztime->ztime;

	if(moving && the_time >= next_move_time)
	{
		move_i++;
		if(move_i >= 3) move_i = 0;

		next_move_time = the_time + (VEHICLE_MOVE_ANIM_SPEED * SpeedOffsetPercentInv());
	}

	if(the_time >= next_turrent_time)
	{
		if(attack_object)
		{
			int tx, ty;
			int new_dir;

			attack_object->GetCenterCords(tx, ty);
			new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);
			if(new_dir != -1) t_direction = new_dir;
		}
		else
		{
			next_turrent_time = the_time + turrent_time_int;
			
			t_direction++;
			if(t_direction >= MAX_ANGLE_TYPES) t_direction = 0;
		}
	}
	
	return 1;
}

void VMissileLauncher::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
        int &x = loc.x;
        int &y = loc.y;
        const int turrent_x[MAX_ANGLE_TYPES] = {0, 2, 3, 8, 9, 7, 2, 0};
        const int turrent_y[MAX_ANGLE_TYPES] = {0, 3, 0, 4, 0, -2, -3, -3};
        const int turrent_shift_x[MAX_ANGLE_TYPES] = {2, 0, 0, 0, 0, -2, 0, 0};
        const int turrent_shift_y[MAX_ANGLE_TYPES] = {0, 0, 0, -2, -2, 0, 2, -2};
        int lx, ly;
        int render_x, render_y;

        if(IsDestroyed())
        {
                render_x = x;
                render_y = y;
                AdjustRenderPositionForScale(&wasted, render_x, render_y);
                the_map.RenderZSurface(&wasted, render_x, render_y);

                return;
        }

        render_x = x;
        render_y = y;
        AdjustRenderPositionForScale(&base[owner][direction][move_i], render_x, render_y);
        the_map.RenderZSurface(&base[owner][direction][move_i], render_x, render_y, do_hit_effect);

        if(owner != NULL_TEAM && !destroyed)
        {
                lx = x + turrent_x[direction] + turrent_shift_x[t_direction];
                ly = y + turrent_y[direction] + turrent_shift_y[t_direction];

                render_x = lx;
                render_y = ly;
                AdjustRenderPositionForScale(&top[owner][t_direction], render_x, render_y);
                the_map.RenderZSurface(&top[owner][t_direction], render_x, render_y, do_hit_effect);
        }

        do_hit_effect = false;
}

void VMissileLauncher::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	//int &x = loc.x;
	//int &y = loc.y;
	//SDL_Rect from_rect, to_rect;
	//int lx, ly;
}

void VMissileLauncher::FireMissile(int x_, int y_)
{
	int &x = loc.x;
	int &y = loc.y;

	const int sx[8] = {20, 12, 0, -12, -20, -12, 0, 12};
	const int sy[8] = {0, -12, -20, -12, 0, 12, 20, 12};

	if(effect_list) effect_list->push_back((ZEffect*)(new EMoMissileRockets(ztime, x+17+sx[t_direction], y+14+sy[t_direction], x_, y_)));
	ZSoundEngine::PlayWavRestricted(MOMISSILE_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
}

void VMissileLauncher::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new EDeath(ztime, loc.x, loc.y, EDEATH_MOMISSILE)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}
