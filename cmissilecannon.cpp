#include "cmissilecannon.h"
#include "zod_cannon_geometry.h"
#include "emissilecrockets.h"

ZSDL_Surface CMissileCannon::passive[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface CMissileCannon::place[MAX_TEAM_TYPES][4];
ZSDL_Surface CMissileCannon::wasted;

CMissileCannon::CMissileCannon(ZTime *ztime_, ZSettings *zsettings_, bool just_placed) : ZCannon(ztime_, zsettings_)
{
	hover_name = "Missile";
	object_name = "missilecannon";

	InitTypeId(object_type, MISSILE_CANNON);



	render_fire = false;
	has_explosives = true;
	damage_is_missile = true;
	
	mode = ROTATING_MODE;
	
	place_i = 0;
	if(just_placed) mode = JUST_PLACED_MODE;
}

SDL_Surface *CMissileCannon::GetPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

SDL_Surface *CMissileCannon::GetNPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

void CMissileCannon::Init()
{
    int i, j;
    char filename_c[500];
    char mask_filename[500];

    // Destroyed cannon is identical for all teams.
    wasted.LoadBaseImage("assets/units/cannons/missile_cannon/wasted.png");

    // The neutral / unowned cannon keeps its original placeholder sprite.
    ZSDL_Surface neutral_empty;
    neutral_empty.LoadBaseImage("assets/units/cannons/missile_cannon/empty_null.png");
    for(j = 0; j < MAX_ANGLE_TYPES; ++j)
        passive[NULL_TEAM][j] = neutral_empty;

    for(j = 0; j < 4; ++j)
        place[NULL_TEAM][j] = passive[NULL_TEAM][4];

    // Each rotation has a single neutral sprite + the user's tint mask.
    for(j = 0; j < MAX_ANGLE_TYPES; ++j)
    {
        sprintf(filename_c,
            "assets/units/cannons/missile_cannon/equiped_missile_cannon_r%03d_01.png",
            ROTATION[j]);
        sprintf(mask_filename,
            "assets/units/cannons/missile_cannon/equiped_missile_cannon_r%03d_01_tintmask.png",
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

    // The initial three placement frames are still ZCannon::init_place.
    // Of the four per-cannon frames, 01 is unmasked, then 02/03/04
    // apply the user's alpha-controlled 30/67/100 percent tint masks.
    for(j = 0; j < 4; ++j)
    {
        sprintf(filename_c,
            "assets/units/cannons/missile_cannon/place_missile_cannon_%02d.png", j + 1);

        if(j == 0)
        {
            for(i = 1; i < MAX_TEAM_TYPES; ++i)
                place[i][j].LoadBaseImage(filename_c);
            continue;
        }

        sprintf(mask_filename,
            "assets/units/cannons/missile_cannon/place_missile_cannon_%02d_tintmask.png", j + 1);

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

void CMissileCannon::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	(void)dest;
	(void)shift_x;
	(void)shift_y;
	const int unit_x = 0;
	const int unit_y = -8;
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
				// Both states previously displayed the same sprite.
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


int CMissileCannon::Process()
{
	double &the_time = ztime->ztime;

	if(render_fire && the_time >= end_render_fire_time)
		render_fire = false;

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

void CMissileCannon::FireMissile(int x_, int y_)
{
	double &the_time = ztime->ztime;
	const ZODCannonPoint muzzle = ZODCannonMuzzle(MISSILE_CANNON, direction);
	const ZODCannonPoint second = ZODCannonMuzzle(MISSILE_CANNON, direction, true);

	int &x = loc.x;
	int &y = loc.y;

	end_render_fire_time = the_time + 0.05 + ((rand() % 100) * 0.03 * 0.01);
	if(effect_list) effect_list->push_back((ZEffect*)(new EMissileCRockets(ztime, x+muzzle.x, y+muzzle.y, x+second.x, y+second.y, x_, y_)));
	ZSoundEngine::PlayWavRestricted(MOMISSILE_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
}

void CMissileCannon::SetAttackObject(ZObject *obj)
{
	attack_object = obj;

	if(!obj) render_fire = false;
	else
	{
		int tx, ty;
		int new_dir;

		attack_object->GetCenterCords(tx, ty);
		new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);
		if(new_dir != -1) direction = new_dir;
	}
}

void CMissileCannon::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(owner == NULL_TEAM) return;

	//effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(loc.x, loc.y, ECANNONDEATH_MISSILE)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

//bool CMissileCannon::ServerFireTurrentMissile(int &x_, int &y_, int &damage, int &radius, double &offset_time)
//{
//	x_ = (loc.x + 16) + (30 - (rand() % 60));
//	y_ = (loc.y + 16) + (30 - (rand() % 60));
//	damage = 40;
//	radius = 40;
//	offset_time = 7 + (0.01 * (rand() % 300));
//	return true;
//}

vector<fire_missile_info> CMissileCannon::ServerFireTurrentMissile(int &damage, int &radius)
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

void CMissileCannon::FireTurrentMissile(int x_, int y_, double offset_time)
{
	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(ztime, loc.x, loc.y, ECANNONDEATH_MISSILE, x_, y_, offset_time)));
	//effect_list->push_back((ZEffect*)(new ETurrentMissile(loc.x + 8, loc.y + 8, x_, y_, offset_time, ETURRENTMISSILE_LIGHT)));
}
