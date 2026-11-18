#include "cgun.h"
#include "zod_cannon_geometry.h"
#include "elightrocket.h"

ZSDL_Surface CGun::passive[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface CGun::place[MAX_TEAM_TYPES][4];
ZSDL_Surface CGun::wasted;

CGun::CGun(ZTime *ztime_, ZSettings *zsettings_, bool just_placed) : ZCannon(ztime_, zsettings_)
{
	hover_name = "Gun";
	object_name = "gun";

	InitTypeId(object_type, GUN);



	has_explosives = true;
	damage_is_missile = true;
	
	mode = ROTATING_MODE;
	
	place_i = 0;
	if(just_placed) mode = JUST_PLACED_MODE;
}

SDL_Surface *CGun::GetPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

SDL_Surface *CGun::GetNPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

void CGun::Init()
{
    int i, j;
    char filename_c[500];
    char mask_filename[500];

    // The destroyed cannon and unowned gun remain unchanged/team-neutral.
    wasted.LoadBaseImage("assets/units/cannons/gun/wasted.png");
    ZSDL_Surface neutral_empty;
    neutral_empty.LoadBaseImage("assets/units/cannons/gun/empty.png");

    for(j = 0; j < MAX_ANGLE_TYPES; ++j)
    {
        passive[NULL_TEAM][j] = neutral_empty;
    }

    // Single neutral equipped sprite for every rotation. Team colors are
    // generated using the user's tint masks, affecting ONLY the gunner.
    // The cannon itself is not recolored.
    for(j = 0; j < MAX_ANGLE_TYPES; ++j)
    {
        sprintf(filename_c,
            "assets/units/cannons/gun/equiped_gun_r%03d_01.png",
            ROTATION[j]);
        sprintf(mask_filename,
            "assets/units/cannons/gun/equiped_gun_r%03d_01_tintmask.png",
            ROTATION[j]);

        ZSDL_Surface neutral;
        ZSDL_Surface tintmask;
        neutral.LoadBaseImage(filename_c);
        tintmask.LoadBaseImage(mask_filename);

        for(i = 1; i < MAX_TEAM_TYPES; ++i)
        {
            SDL_Surface *tinted = ZTeam::MakeNeutralTint(
                i, neutral.GetBaseSurface(), tintmask.GetBaseSurface());
            if(tinted)
                passive[i][j].LoadBaseImage(tinted);
            else
                passive[i][j].LoadBaseImage(filename_c);
        }
    }

    // Place frames 01-04 preserve the original 7-step placement animation.
    // Frame 01 has no tint mask. Frames 02, 03 and 04 use user-supplied
    // tint masks with their own PNG alpha strengths (ca. 30/67/100%).
    for(j = 0; j < 4; ++j)
    {
        sprintf(filename_c,
            "assets/units/cannons/gun/place_gun_%02d.png", j+1);
        place[NULL_TEAM][j].LoadBaseImage(filename_c);

        if(j == 0)
        {
            for(i = 1; i < MAX_TEAM_TYPES; ++i)
                place[i][j].LoadBaseImage(filename_c);
            continue;
        }

        sprintf(mask_filename,
            "assets/units/cannons/gun/place_gun_%02d_tintmask.png", j+1);
        ZSDL_Surface neutral;
        ZSDL_Surface tintmask;
        neutral.LoadBaseImage(filename_c);
        tintmask.LoadBaseImage(mask_filename);

        for(i = 1; i < MAX_TEAM_TYPES; ++i)
        {
            SDL_Surface *tinted = ZTeam::MakeNeutralTint(
                i, neutral.GetBaseSurface(), tintmask.GetBaseSurface());
            if(tinted)
                place[i][j].LoadBaseImage(tinted);
            else
                place[i][j].LoadBaseImage(filename_c);
        }
    }
}

void CGun::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	(void)dest;
	(void)shift_x;
	(void)shift_y;
        const int unit_x = 0;
        const int unit_y = 0;
	int &x = loc.x;
	int &y = loc.y;
	ZSDL_Surface *base_surface;
	int lx, ly;
	
	if(destroyed)
	{
		base_surface = &wasted;
	}
	else
	{
		switch(mode)
		{
			case JUST_PLACED_MODE:
				if(place_i<3)
					base_surface = &init_place[place_i];
				else
					base_surface = &place[owner][place_i-3];
				break;
			default:
				base_surface = &passive[owner][direction];
				break;
		}
	}

        lx = x + unit_x;
        ly = y + unit_y;
        AdjustRenderPositionForScale(base_surface, lx, ly);
	
	the_map.RenderZSurface(base_surface, lx, ly, do_hit_effect);
	do_hit_effect = false;
}
		

int CGun::Process()
{
	double &the_time = ztime->ztime;

	switch(mode)
	{
		case JUST_PLACED_MODE:
			if(the_time - last_process_time < 0.1) break;
			last_process_time = the_time;
			
			place_i++;
			if(place_i >= 7)
			{
				//we are done with this mode, go to the next
				place_i = 0;
				mode = ROTATING_MODE;
			}
			break;
		case ROTATING_MODE:
			if(the_time - last_process_time < 1.0) break;
			if(owner == NULL_TEAM) break;
			last_process_time = the_time;
			
			if(attack_object)
			{
				int tx, ty;
				int new_dir;

				attack_object->GetCenterCords(tx, ty);
				new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);
				if(new_dir != -1) direction = new_dir;
			}
			else
			{
				direction++;
				if(direction >= MAX_ANGLE_TYPES) direction = 0;
			}
			
			break;
	}

	return 1;
}

void CGun::FireMissile(int x_, int y_)
{
	const ZODCannonPoint muzzle = ZODCannonMuzzle(GUN, direction);

	int &x = loc.x;
	int &y = loc.y;

	if(effect_list) effect_list->push_back((ZEffect*)(new ELightRocket(ztime, x+muzzle.x, y+muzzle.y, x_, y_, missile_speed, 0, 1)));
	ZSoundEngine::PlayWavRestricted(GUN_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
}

void CGun::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(owner == NULL_TEAM) return;

	//effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(loc.x, loc.y, ECANNONDEATH_GUN)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

//bool CGun::ServerFireTurrentMissile(int &x_, int &y_, int &damage, int &radius, double &offset_time)
//{
//	x_ = (loc.x + 16) + (30 - (rand() % 60));
//	y_ = (loc.y + 16) + (30 - (rand() % 60));
//	damage = 40;
//	radius = 40;
//	offset_time = 7 + (0.01 * (rand() % 300));
//	return true;
//}

vector<fire_missile_info> CGun::ServerFireTurrentMissile(int &damage, int &radius)
{
	vector<fire_missile_info> ret;
	fire_missile_info a_missile;
	int &max_horz = zsettings->max_turrent_horizontal_distance;
	int &max_vert = zsettings->max_turrent_vertical_distance;

	a_missile.missile_x = (loc.x + 16) + (max_horz - (rand() % (max_horz + max_horz)));
	a_missile.missile_y = (loc.y + 16) + (max_vert - (rand() % (max_vert + max_vert)));
	a_missile.missile_offset_time = 7 + (0.01 * (rand() % 300));

	damage = 40;
	radius = 40;

	ret.push_back(a_missile);

	return ret;
}

void CGun::FireTurrentMissile(int x_, int y_, double offset_time)
{
	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(ztime, loc.x, loc.y, ECANNONDEATH_GUN, x_, y_, offset_time)));
}
