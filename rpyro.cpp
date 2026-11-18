#include "rpyro.h"
#include "zod_robot_geometry.h"

ZSDL_Surface RPyro::fire[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];

RPyro::RPyro(ZTime *ztime_, ZSettings *zsettings_) : ZRobot(ztime_, zsettings_)
{
	hover_name = "Pyros";
	object_name = "pyro";

	can_snipe = true;

	InitTypeId(object_type, PYRO);

	//object_id = PYRO;
	//attack_radius = PYRO_ATTACK_RADIUS;
	//max_health = PYRO_MAX_HEALTH;
	//health = max_health;

	//damage = PYRO_DAMAGE;
	//damage_chance = PYRO_DAMAGE_CHANCE;
	//damage_int_time = PYRO_ATTACK_SPEED;
}

void RPyro::Init()
{
	int i, j, k;
	char filename_c[500];
	
	for(i=1;i<MAX_TEAM_TYPES;i++)
		for(j=0;j<MAX_ANGLE_TYPES;j++)
			for(k=0;k<3;k++)
	{
		sprintf(filename_c, "assets/units/robots/pyro/fire_pyro_r%03d_%02d.png", ROTATION[j], k+1);
		ZTeam::LoadRobotTintFrame(i, fire[i][j][k], filename_c);
	}
}

void RPyro::PlaySelectedWav()
{
	//int ch;

	//if(rand()%2)
	//{
	//	ch = rand() % 6;

	//	ZMix_PlayChannel(-1, selected_wav[ch], 0);
	//}
	//else
	//	ZMix_PlayChannel(-1, selected_robot_wav[PYRO], 0);
}

void RPyro::PlaySelectedAnim(ZPortrait &portrait)
{
	if(rand()%2)
	{
		switch(rand() % 4)
		{
		case 0: portrait.StartAnim(YES_SIR_ANIM); break;
		case 1: portrait.StartAnim(YES_SIR3_ANIM); break;
		case 2: portrait.StartAnim(UNIT_REPORTING1_ANIM); break;
		case 3: portrait.StartAnim(UNIT_REPORTING2_ANIM); break;
		}
	}
	else
		portrait.StartAnim(PYROS_REPORTING_ANIM);
}

// SDL_Surface *RPyro::GetRender()
// {
// 	if(owner != NULL_TEAM)
// 	{
// 		switch(mode)
// 		{
// 			case R_STANDING: return stand[owner][direction];
// 			case R_BEER: return beer[owner][action_i];
// 			case R_CIGARETTE: return cigarette[owner][action_i];
// 			case R_FULLSCAN: return full_area_scan[owner][action_i];
// 			case R_HEADSTRETCH: return head_stretch[owner][action_i];
// 		}
// 	}
// 	else
// 		return null_img;
// }

void RPyro::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	ZSDL_Surface *base_surface;
	
	if(owner != NULL_TEAM)
	{
		switch(mode)
		{
			case R_WALKING: base_surface = &walk[owner][direction][move_i]; break;
			case R_STANDING: base_surface = &stand[owner][direction]; break;
			case R_BEER: base_surface = &beer[owner][action_i]; break;
			case R_CIGARETTE: base_surface = &cigarette[owner][action_i]; break;
			case R_FULLSCAN: base_surface = &full_area_scan[owner][action_i]; break;
			case R_HEADSTRETCH: base_surface = &head_stretch[owner][action_i]; break;
			case R_PICKUP_UP_GRENADES: base_surface = &pickup_up[owner][action_i]; break;
			case R_PICKUP_DOWN_GRENADES: base_surface = &pickup_down[owner][action_i]; break;
			case R_ATTACKING: 
				if(this->CanThrowGrenades() || (attack_object && attack_object->AttackedOnlyByExplosives()))
					base_surface = &throw_something[owner][direction][grenade_i];
				else
					base_surface = &fire[owner][direction][action_i];
				break;
			default: base_surface = &null_img;
		}
	}
	else
		base_surface = &null_img;
	
	if(!base_surface) return;

	RenderRobotSurface(the_map, base_surface, shift_x, shift_y);
}

void RPyro::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	
}

int RPyro::Process()
{
	double &the_time = ztime->ztime;
	int &x = loc.x;
	int &y = loc.y;

	Common_Process(the_time);

	if(this->CanThrowGrenades() || (attack_object && attack_object->AttackedOnlyByExplosives())) return 1;

	if(mode == R_ATTACKING && the_time >= next_attack_time)
	{
		action_i++;
		if(action_i>=3) action_i = 0;

		switch(action_i)
		{
			case 0:
				next_attack_time = the_time + 0.07 + ((rand() % 100) * 0.03 * 0.01);
				break;
			case 1:
			case 2:
				next_attack_time = the_time + 0.05 + ((rand() % 100) * 0.03 * 0.01);
				break;
		}

		//just switched to firing?
		if(action_i == 2 && attack_object)
		{
			int tx, ty;
			int sx, sy;
			int w, h;

			attack_object->GetCords(tx, ty);
			attack_object->GetDimensionsPixel(w,h);

			tx += (rand() % w);
			ty += (rand() % h);

			ZODRobotMuzzle(PYRO, direction, x, y, sx, sy);
			if(effect_list) effect_list->push_back((ZEffect*)(new EFlame(ztime, sx, sy, tx, ty)));
			ZSoundEngine::PlayWavRestricted(PYRO_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
		}
	}

	return 1;
}
