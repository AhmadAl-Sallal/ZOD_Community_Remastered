#include "chowitzer.h"
#include "zod_cannon_geometry.h"
#include "elightrocket.h"

ZSDL_Surface CHowitzer::passive[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface CHowitzer::fire[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface CHowitzer::place[MAX_TEAM_TYPES][4];
ZSDL_Surface CHowitzer::wasted;

CHowitzer::CHowitzer(ZTime *ztime_, ZSettings *zsettings_, bool just_placed) : ZCannon(ztime_, zsettings_)
{
	hover_name = "Howitzer";
	object_name = "howitzer";

	InitTypeId(object_type, HOWITZER);



	render_fire = false;
	has_explosives = true;
	damage_is_missile = true;
	
	mode = ROTATING_MODE;
	
	place_i = 0;
	if(just_placed) mode = JUST_PLACED_MODE;
}

SDL_Surface *CHowitzer::GetPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

SDL_Surface *CHowitzer::GetNPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

void CHowitzer::Init()
{
    int i, j, k;
    char filename_c[500];
    char mask_filename[500];

    // One destroyed sprite and eight original empty-cannon directions.
    // Their original appearance is preserved; only the seated robot is tinted.
    wasted.LoadBaseImage("assets/units/cannons/howitzer/wasted.png");
    for(j = 0; j < MAX_ANGLE_TYPES; ++j)
    {
        sprintf(filename_c,
            "assets/units/cannons/howitzer/empty_r%03d.png", ROTATION[j]);
        passive[NULL_TEAM][j].LoadBaseImage(filename_c);
        fire[NULL_TEAM][j] = passive[NULL_TEAM][j];
    }

    // Eight rotations with two firing frames each. Use one neutral frame
    // and its alpha-controlled tint mask to generate all team colors.
    // Other cannon colors, light and muzzle effects are never team-tinted.
    for(j = 0; j < MAX_ANGLE_TYPES; ++j)
        for(k = 0; k < 2; ++k)
        {
            sprintf(filename_c,
                "assets/units/cannons/howitzer/fire_howitzer_r%03d_%02d.png",
                ROTATION[j], k + 1);
            sprintf(mask_filename,
                "assets/units/cannons/howitzer/fire_howitzer_r%03d_%02d_tintmask.png",
                ROTATION[j], k + 1);

            ZSDL_Surface neutral;
            ZSDL_Surface tintmask;
            neutral.LoadBaseImage(filename_c);
            tintmask.LoadBaseImage(mask_filename);

            for(i = 1; i < MAX_TEAM_TYPES; ++i)
            {
                ZSDL_Surface &target = (k == 0) ? passive[i][j] : fire[i][j];
                SDL_Surface *tinted = ZTeam::MakeNeutralTint(
                    i, neutral.GetBaseSurface(), tintmask.GetBaseSurface());
                if(tinted)
                    target.LoadBaseImage(tinted);
                else
                    target.LoadBaseImage(filename_c);
            }
        }

    // First three deployment images still belong to ZCannon::init_place.
    // The Howitzer's four following place images are 01 (no tint mask),
    // 02 (30%), 03 (about 67%) and 04 (100%) using original mask alpha.
    for(j = 0; j < 4; ++j)
    {
        sprintf(filename_c,
            "assets/units/cannons/howitzer/place_howitzer_%02d.png", j + 1);
        place[NULL_TEAM][j].LoadBaseImage(filename_c);

        if(j == 0)
        {
            for(i = 1; i < MAX_TEAM_TYPES; ++i)
                place[i][j].LoadBaseImage(filename_c);
            continue;
        }

        sprintf(mask_filename,
            "assets/units/cannons/howitzer/place_howitzer_%02d_tintmask.png",
            j + 1);
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

void CHowitzer::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	(void)dest;
	(void)shift_x;
	(void)shift_y;
	const int unit_x = -2;
	const int unit_y = -12;
	int &x = loc.x;
	int &y = loc.y;
	const int base_x[8] = {5, 2, 2, 2, 0, 2, 2, 2};
	const int base_y[8] = {0, 0, 0, 0, 0, 3, 3, 3};
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
				if(render_fire)
					base_surface = &fire[owner][direction];
				else
					base_surface = &passive[owner][direction];
				break;
		}
	}

	lx = x + base_x[direction] + unit_x;
	ly = y + base_y[direction] + unit_y;
	AdjustRenderPositionForScale(base_surface, lx, ly);
	
	the_map.RenderZSurface(base_surface, lx, ly, do_hit_effect);
	do_hit_effect = false;
}


int CHowitzer::Process()
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

void CHowitzer::FireMissile(int x_, int y_)
{
	double &the_time = ztime->ztime;
	const ZODCannonPoint muzzle = ZODCannonMuzzle(HOWITZER, direction);

	int &x = loc.x;
	int &y = loc.y;

	render_fire = true;
	end_render_fire_time = the_time + 0.05 + ((rand() % 100) * 0.03 * 0.01);
	if(effect_list) effect_list->push_back((ZEffect*)(new ELightRocket(ztime, x+muzzle.x, y+muzzle.y, x_, y_, missile_speed, 1, 1)));
	ZSoundEngine::PlayWavRestricted(HEAVY_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
}

void CHowitzer::SetAttackObject(ZObject *obj)
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

void CHowitzer::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(owner == NULL_TEAM) return;

	//effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(loc.x, loc.y, ECANNONDEATH_HOWITZER)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

//bool CHowitzer::ServerFireTurrentMissile(int &x_, int &y_, int &damage, int &radius, double &offset_time)
//{
//	x_ = (loc.x + 16) + (30 - (rand() % 60));
//	y_ = (loc.y + 16) + (30 - (rand() % 60));
//	damage = 40;
//	radius = 40;
//	offset_time = 7 + (0.01 * (rand() % 300));
//	return true;
//}

vector<fire_missile_info> CHowitzer::ServerFireTurrentMissile(int &damage, int &radius)
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

void CHowitzer::FireTurrentMissile(int x_, int y_, double offset_time)
{
	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(ztime, loc.x, loc.y, ECANNONDEATH_HOWITZER, x_, y_, offset_time)));
	//effect_list->push_back((ZEffect*)(new ETurrentMissile(loc.x + 8, loc.y + 8, x_, y_, offset_time, ETURRENTMISSILE_LIGHT)));
}
