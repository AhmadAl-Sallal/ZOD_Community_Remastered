#include "vapc.h"
#include "zteam.h"

using namespace COMMON;

const double turrent_time_int = 0.2;

ZSDL_Surface VAPC::base[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VAPC::top[MAX_ANGLE_TYPES];
ZSDL_Surface VAPC::wasted[MAX_TEAM_TYPES];

VAPC::VAPC(ZTime *ztime_, ZSettings *zsettings_) : ZVehicle(ztime_, zsettings_)
{
	double &the_time = ztime->ztime;

	width = 2;
	height = 2;
	width_pix = 32;
	height_pix = 32;
	hover_name = "APC";
	object_name = "apc";

	InitTypeId(object_type, APC);

	//object_id = APC;
	//move_speed = APC_SPEED;
	//max_health = APC_MAX_HEALTH;
	//health = max_health;
	
	t_direction = direction;
	move_i = rand() % 3;
	
	next_turrent_time = the_time;
	next_turrent_time += turrent_time_int;
}

void VAPC::Init()
{
	int i, j, k;
	char filename_c[500];
	SDL_Surface *temp_surface;

	// The neutral/unowned APC keeps using the original empty sprite.
	strcpy(filename_c, "assets/units/vehicles/apc/empty.png");
	temp_surface = IMG_Load_Error(filename_c);

	for(j=0;j<MAX_ANGLE_TYPES;j++)
		for(k=0;k<3;k++)
			base[0][j][k] = temp_surface;

	ZSDL_FreeSurface(temp_surface);

	// Team-colored chassis: 8 directions x 3 movement frames, with one
	// neutral image and its own tintmask per frame (same method as Heavy Tank).
	for(j=0;j<MAX_ANGLE_TYPES;j++)
		for(k=0;k<3;k++)
		{
			char neutral_filename[500];
			char mask_filename[500];
			sprintf(neutral_filename, "assets/units/vehicles/apc/base_APC_r%03d_%02d.png", ROTATION[j], k + 1);
			sprintf(mask_filename, "assets/units/vehicles/apc/base_APC_r%03d_%02d_tintmask.png", ROTATION[j], k + 1);

			ZSDL_Surface neutral;
			ZSDL_Surface tintmask;
			neutral.LoadBaseImage(neutral_filename);
			tintmask.LoadBaseImage(mask_filename);

			for(i=1;i<MAX_TEAM_TYPES;i++)
			{
				SDL_Surface *generated = ZTeam::MakeNeutralTint(
					i, neutral.GetBaseSurface(), tintmask.GetBaseSurface());
				if(generated)
					base[i][j][k].LoadBaseImage(generated);
				else
					// On a mask error, show the neutral body rather than a missing sprite.
					base[i][j][k].LoadBaseImage(neutral_filename);
			}
		}

	// The rotating top has no team-colored areas and stays unchanged.
	for(j=0;j<MAX_ANGLE_TYPES;j++)
	{
		sprintf(filename_c, "assets/units/vehicles/apc/top_r%03d.png", ROTATION[j]);
		top[j].LoadBaseImage(filename_c);
	}

	// Destroyed APC is deliberately neutral: one wasted.png for every team.
	wasted[0].LoadBaseImage("assets/units/vehicles/apc/wasted.png");
	for(i=1;i<MAX_TEAM_TYPES;i++)
		wasted[i] = wasted[0];
}

// SDL_Surface *VAPC::GetRender()
// {
// 	if(destroyed)
// 	{
// 		return wasted[owner];
// 	}
// 	else
// 	{
// 		return base[owner][direction][base_i];
// 	}
// }

int VAPC::Process()
{
	double &the_time = ztime->ztime;
	int &x = loc.x;
	int &y = loc.y;

	if(moving && the_time >= next_move_time)
	{
		move_i++;
		if(move_i >= 3) move_i = 0;

		next_move_time = the_time + (VEHICLE_MOVE_ANIM_SPEED * SpeedOffsetPercentInv());
	}

	if(the_time >= next_turrent_time)
	{
		next_turrent_time = the_time + turrent_time_int;
		
		t_direction++;
		if(t_direction >= MAX_ANGLE_TYPES) t_direction = 0;
	}

	//attacking
	if(attack_object && driver_info.size() && !damage_is_missile)
	{
		int tx, ty;
		int sx, sy;
		int w, h;

		attack_object->GetDimensionsPixel(w,h);

		//determine direction
		sx = 0;
		sy = 0;

		for(vector<driver_info_s>::iterator i=driver_info.begin(); i!=driver_info.end() ;i++)
		{
			if(the_time < i->next_attack_time) continue;

			//set next time
			i->next_attack_time = the_time + damage_int_time + (0.012 * (rand() % 10));

			//get attack location
			attack_object->GetCords(tx, ty);
			
			tx += (rand() % w);
			ty += (rand() % h);

			//do the effect
			switch(driver_type)
			{
			case GRUNT:
			case PSYCHO:
			case SNIPER:
				if(effect_list) effect_list->push_back((ZEffect*)(new EBullet(ztime, owner, x+16, y+16, tx, ty)));
				ZSoundEngine::PlayWavRestricted(RIFLE_FIRE_SND, x, y, width_pix, height_pix);
				break;
			case PYRO:
				if(effect_list) effect_list->push_back((ZEffect*)(new EFlame(ztime, x+16+sx, y+16+sy, tx, ty)));
				ZSoundEngine::PlayWavRestricted(PYRO_FIRE_SND, x, y, width_pix, height_pix);
				break;
			case LASER:
				if(effect_list) effect_list->push_back((ZEffect*)(new ELaser(ztime, x+16+sx, y+16+sy, tx, ty)));
				ZSoundEngine::PlayWavRestricted(LASER_FIRE_SND, x, y, width_pix, height_pix);
				break;
			}
		}
	}
	
	return 1;
}

void VAPC::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
        int &x = loc.x;
        int &y = loc.y;
        const int turrent_x[MAX_ANGLE_TYPES] = {1, 5, 9, 13, 15, 11, 8, 5};
        const int turrent_y[MAX_ANGLE_TYPES] = {5, 8, 5, 8, 5, 3, 3, 4};
        ZSDL_Surface *base_surface;
        int lx, ly;
        int render_x, render_y;

        if(destroyed)
                base_surface = &wasted[owner];
        else
                base_surface = &base[owner][direction][move_i];

        render_x = x;
        render_y = y;
        AdjustRenderPositionForScale(base_surface, render_x, render_y);
        the_map.RenderZSurface(base_surface, render_x, render_y, do_hit_effect);

        if(owner != NULL_TEAM && !destroyed)
        {
                lx = x + turrent_x[direction];
                ly = y + turrent_y[direction];

                render_x = lx;
                render_y = ly;
                AdjustRenderPositionForScale(&top[t_direction], render_x, render_y);
                the_map.RenderZSurface(&top[t_direction], render_x, render_y, do_hit_effect);
        }

        do_hit_effect = false;
}

void VAPC::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	//int &x = loc.x;
	//int &y = loc.y;
	//SDL_Rect from_rect, to_rect;
	//int lx, ly;
}

void VAPC::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new EDeath(ztime, loc.x, loc.y, EDEATH_APC)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

void VAPC::FireMissile(int x_, int y_)
{
	int &x = loc.x;
	int &y = loc.y;

	int sx, sy;
	int w, h;

	//just switched to firing?
	if(attack_object)
	{
		sx = 0;
		sy = 0;

		if(effect_list) effect_list->push_back((ZEffect*)(new EToughRocket(ztime, x+16+sx, y+16+sy, x_, y_)));
		ZSoundEngine::PlayWavRestricted(TOUGH_FIRE_SND, x, y, width_pix, height_pix);

		attack_object->GetDimensionsPixel(w,h);

		//add some effect for the extra toughs in the apc
		for(int i=0;i<driver_info.size()-1;i++)
		{
			int tx, ty;

			//get attack location
			attack_object->GetCords(tx, ty);
			
			tx += (rand() % w);
			ty += (rand() % h);

			if(effect_list) effect_list->push_back((ZEffect*)(new EToughRocket(ztime, x+16+sx, y+16+sy, tx, ty)));
			//ZSoundEngine::PlayWavRestricted(TOUGH_FIRE_SND, x, y, width_pix, height_pix);
		}
	}
}

bool VAPC::CanEjectDrivers()
{
	return true;
}

void VAPC::ResetDamageInfo()
{
	//clear the info
	attack_radius = 0;
	damage = 0;
	damage_chance = 0;
	damage_int_time = 0;
	damage_is_missile = false;
	damage_radius = 0;
	missile_speed = 0;
	has_explosives = false;

	//exit with still cleared?
	if(!driver_info.size()) return;

	ZUnit_Settings &unit_settings = zsettings->GetUnitSettings(ROBOT_OBJECT, driver_type);

	attack_radius = unit_settings.attack_radius;
	damage = unit_settings.attack_damage * MAX_UNIT_HEALTH;
	damage_chance = unit_settings.attack_damage_chance;
	damage_int_time = unit_settings.attack_speed;
	damage_radius = unit_settings.attack_damage_radius;
	missile_speed = unit_settings.attack_missile_speed;

	if(driver_type == TOUGH)
	{
		has_explosives = true;
		damage_is_missile = true;
	}

	//set base damage
	//switch(driver_type)
	//{
	//case GRUNT:
	//	attack_radius = GRUNT_ATTACK_RADIUS;
	//	damage = GRUNT_DAMAGE;
	//	damage_chance = GRUNT_DAMAGE_CHANCE;
	//	damage_int_time = GRUNT_ATTACK_SPEED;
	//	break;
	//case PSYCHO:
	//	attack_radius = PSYCHO_ATTACK_RADIUS;
	//	damage = PSYCHO_DAMAGE;
	//	damage_chance = PSYCHO_DAMAGE_CHANCE;
	//	damage_int_time = PSYCHO_ATTACK_SPEED;
	//	break;
	//case SNIPER:
	//	attack_radius = SNIPER_ATTACK_RADIUS;
	//	damage = SNIPER_DAMAGE;
	//	damage_chance = SNIPER_DAMAGE_CHANCE;
	//	damage_int_time = SNIPER_ATTACK_SPEED;
	//	break;
	//case TOUGH:
	//	attack_radius = TOUGH_ATTACK_RADIUS;
	//	has_explosives = true;
	//	damage = TOUGH_DAMAGE;
	//	damage_int_time = TOUGH_ATTACK_SPEED;
	//	damage_is_missile = true;
	//	damage_radius = TOUGH_DAMAGE_RADIUS;
	//	missile_speed = TOUGH_MISSILE_SPEED;
	//	break;
	//case PYRO:
	//	attack_radius = PYRO_ATTACK_RADIUS;
	//	damage = PYRO_DAMAGE;
	//	damage_chance = PYRO_DAMAGE_CHANCE;
	//	damage_int_time = PYRO_ATTACK_SPEED;
	//	break;
	//case LASER:
	//	attack_radius = LASER_ATTACK_RADIUS;
	//	damage = LASER_DAMAGE;
	//	damage_chance = LASER_DAMAGE_CHANCE;
	//	damage_int_time = LASER_ATTACK_SPEED;
	//	break;
	//}

	//multiplyer
	damage *= driver_info.size();
}
