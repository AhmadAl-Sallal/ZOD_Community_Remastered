#include "zrobot.h"
#include "zod_robot_geometry.h"

using namespace COMMON;

#define GRENADE_TIME_INT 0.15

ZSDL_Surface ZRobot::null_img;
ZSDL_Surface ZRobot::stand[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface ZRobot::throw_something[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][4];
ZSDL_Surface ZRobot::walk[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][4];
ZSDL_Surface ZRobot::beer[MAX_TEAM_TYPES][10];
ZSDL_Surface ZRobot::cigarette[MAX_TEAM_TYPES][11];
ZSDL_Surface ZRobot::full_area_scan[MAX_TEAM_TYPES][12];
ZSDL_Surface ZRobot::head_stretch[MAX_TEAM_TYPES][11];
ZSDL_Surface ZRobot::pickup_down[MAX_TEAM_TYPES][4];
ZSDL_Surface ZRobot::pickup_up[MAX_TEAM_TYPES][4];

ZRobot::ZRobot(ZTime *ztime_, ZSettings *zsettings_) : ZObject(ztime_, zsettings_)
{
	double &the_time = ztime->ztime;

	width = 1;
	height = 1;
	width_pix = 16;
	height_pix = 16;
	object_name = "robot";
	object_type = ROBOT_OBJECT;
	selectable = true;
	
	action_i = 0;
	grenade_i = 0;
	throw_grenade = false;
	direction = 6;
	mode = R_STANDING;
	
	process_time_int = 0.3;
	next_process_time = the_time + process_time_int;

	grenade_amount = 0;

	//move_speed = ROBOT_SPEED;
}

void ZRobot::RenderRobotSurface(ZMap &the_map, ZSDL_Surface *surface, int shift_x, int shift_y)
{
        if(!surface)
        {
                do_hit_effect = false;
                return;
        }

        SDL_Surface *image_surface = surface->GetBaseSurface();

        if(!image_surface)
        {
                do_hit_effect = false;
                return;
        }

        // Calculate water submersion.
        submerge_amount = the_map.SubmergeAmount(loc.x + 8, loc.y + 8);

        if(submerge_amount < 0)
                submerge_amount = 0;

        if(submerge_amount > image_surface->h)
                submerge_amount = image_surface->h;

        // Keep the robot centered in its 16x16 map tile.
        // The bottom of the image remains anchored at loc.y + 16.
        const int render_x = ZODRobotRenderX(loc.x, image_surface->w);
        const int render_y = ZODRobotRenderY(loc.y, image_surface->h) + submerge_amount;

        const int render_height = image_surface->h - submerge_amount;

        if(render_height > 0)
        {
                SDL_Rect from_rect, to_rect;

                if(the_map.GetBlitInfo(
                        render_x,
                        render_y,
                        image_surface->w,
                        render_height,
                        from_rect,
                        to_rect))
                {
                        to_rect.x += shift_x;
                        to_rect.y += shift_y;

                        surface->BlitHitSurface(&from_rect, &to_rect, NULL, do_hit_effect);
                }
        }

        do_hit_effect = false;
}
void ZRobot::Common_Process(double the_time)
{
	int tx, ty;
	int new_dir;

	if(throw_grenade && the_time >= next_grenade_time)
	{
		next_grenade_time = the_time + GRENADE_TIME_INT;

		grenade_i++;
		if(grenade_i >= 4)
		{
			grenade_i = 0;
			throw_grenade = false;
		}
	}

	if(the_time < next_process_time) return;
	
	switch(mode)
	{
		case R_WALKING:
			move_i++;
			if(move_i >= 4) move_i = 0;

			break;
		case R_STANDING:
			//one in 10 that we do something
			if(rand() % 10) break;
			
			//1 in 3 that something is just turn
			if(rand() % 3)
				direction = rand() % MAX_ANGLE_TYPES;
			else
			{
				//which random other mode is it then?
				int choice = rand() % 4;
				
				action_i = 0;
				direction = 6;
				
				//printf("action_i robot:%d\n", action_i);
				
				switch(choice)
				{
					case 0:
						mode = R_CIGARETTE;
						break;
					case 1:
						mode = R_BEER;
						break;
					case 2:
						mode = R_FULLSCAN;
						break;
					case 3:
						mode = R_HEADSTRETCH;
						break;
				}
			}
			break;
		case R_CIGARETTE:
			action_i++;
			if(action_i >= 11) mode = R_STANDING;
			break;
		case R_BEER:
			action_i++;
			if(action_i >= 10) mode = R_STANDING;
			break;
		case R_FULLSCAN:
			action_i++;
			if(action_i >= 12) mode = R_STANDING;
			break;
		case R_HEADSTRETCH:
			action_i++;
			if(action_i >= 11) mode = R_STANDING;
			break;
		case R_PICKUP_UP_GRENADES:
		case R_PICKUP_DOWN_GRENADES:
			action_i++;
			if(action_i >= 4) mode = R_STANDING;
			break;
		case R_ATTACKING:
			if(!attack_object) break;
			attack_object->GetCenterCords(tx, ty);
			new_dir = DirectionFromLoc(tx - center_x, ty - center_y);
			if(new_dir != -1) direction = new_dir;
			break;
	}
	
	next_process_time = the_time + (process_time_int * SpeedOffsetPercentInv());
}

void ZRobot::DoPickupGrenadeAnim()
{
	//dont bother
	if(!CanHaveGrenades()) return;

	if(mode == R_ATTACKING) return;

	if(direction < 4)
		mode = R_PICKUP_UP_GRENADES;
	else
		mode = R_PICKUP_DOWN_GRENADES;
	action_i = 0;
}

void ZRobot::Init()
{
    int i, j, k;
    char filename_c[500];
    null_img.LoadBaseImage("assets/units/robots/null.png");
    for(j=0; j<MAX_ANGLE_TYPES; ++j)
        stand[NULL_TEAM][j] = null_img;

    for(i=1; i<MAX_TEAM_TYPES; ++i)
    {
        for(j=0; j<MAX_ANGLE_TYPES; ++j)
        {
            sprintf(filename_c, "assets/units/robots/stand_robot_r%03d_01.png", ROTATION[j]);
            ZTeam::LoadRobotTintFrame(i, stand[i][j], filename_c);
            for(k=0; k<4; ++k)
            {
                sprintf(filename_c, "assets/units/robots/walk_robot_r%03d_%02d.png", ROTATION[j], k+1);
                ZTeam::LoadRobotTintFrame(i, walk[i][j][k], filename_c);
                sprintf(filename_c, "assets/units/robots/throw_robot_r%03d_%02d.png", ROTATION[j], k+1);
                ZTeam::LoadRobotTintFrame(i, throw_something[i][j][k], filename_c);
            }
        }
        for(k=0; k<10; ++k)
        {
            sprintf(filename_c, "assets/units/robots/beer_robot_%02d.png", k+1);
            ZTeam::LoadRobotTintFrame(i, beer[i][k], filename_c);
        }
        for(k=0; k<11; ++k)
        {
            sprintf(filename_c, "assets/units/robots/cigarette_robot_%02d.png", k+1);
            ZTeam::LoadRobotTintFrame(i, cigarette[i][k], filename_c);
        }
        for(k=0; k<12; ++k)
        {
            sprintf(filename_c, "assets/units/robots/full_area_scan_robot_%02d.png", k+1);
            ZTeam::LoadRobotTintFrame(i, full_area_scan[i][k], filename_c);
        }
        for(k=0; k<11; ++k)
        {
            sprintf(filename_c, "assets/units/robots/head_stretch_robot_%02d.png", k+1);
            ZTeam::LoadRobotTintFrame(i, head_stretch[i][k], filename_c);
        }
        for(k=0; k<4; ++k)
        {
            sprintf(filename_c, "assets/units/robots/pickup-down_robot_%02d.png", k+1);
            ZTeam::LoadRobotTintFrame(i, pickup_down[i][k], filename_c);
        }
        for(k=0; k<4; ++k)
        {
            sprintf(filename_c, "assets/units/robots/pickup-up_robot_%02d.png", k+1);
            ZTeam::LoadRobotTintFrame(i, pickup_up[i][k], filename_c);
        }
    }
}

void ZRobot::RecalcDirection()
{
	int new_dir;

	new_dir = DirectionFromLoc(loc.dx, loc.dy);

	if(new_dir != -1) 
	{
		if(mode != R_WALKING)
			move_i = 0;

		mode = R_WALKING;
		direction = new_dir;
		//printf("mode:walking\n");
	}
	else
	{
		mode = R_STANDING;
		//printf("mode:standing\n");
	}
}

void ZRobot::SetAttackObject(ZObject *obj)
{
	double &the_time = ztime->ztime;

	attack_object = obj;

	if(attack_object)
	{
		mode = R_ATTACKING;
		action_i = 0;
		next_attack_time = the_time + 0.1;
	}
	else
	{
		if(mode != R_WALKING && mode != R_STANDING)
			mode = R_STANDING;
	}
}

void ZRobot::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(!effect_list) return;

	if(do_missile_death)
		effect_list->push_back((ZEffect*)(new ERobotTurrent(ztime, center_x, center_y, owner)));
	else
		effect_list->insert(effect_list->begin(), (ZEffect*)(new ERobotDeath(ztime, loc.x, loc.y, owner, do_fire_death)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

void ZRobot::FireMissile(int x_, int y_)
{
	double &the_time = ztime->ztime;
	int &x = center_x;
	int &y = center_y;

	double dist;
	double dist_time;
	int dx = x - x_;
	int dy = y - y_;

	dist = sqrt((float)((dx * dx) + (dy * dy)));
	dist_time = dist / zsettings->grenade_missile_speed;

	if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, x + 2, y + 2, x_, y_, dist_time, ETURRENTMISSILE_GRENADE)));	
	//if(effect_list) effect_list->push_back((ZEffect*)(new ELightRocket(ztime, x+17+sx[t_direction], y+14+sy[t_direction], x_, y_, missile_speed, 0, 1, 1)));

	ZSoundEngine::PlayWavRestricted(THROW_GRENADE_SND, loc.x, loc.y, width_pix, height_pix);

	grenade_i = 0;
	throw_grenade = true;
	next_grenade_time = the_time + GRENADE_TIME_INT;
}

bool ZRobot::CanThrowGrenades()
{
	return GetGrenadeAmount() || (GetGroupLeader() && GetGroupLeader()->GetGrenadeAmount());
}

void ZRobot::SetGrenadeAmount(int grenade_amount_) 
{ 
	grenade_amount = grenade_amount_;

	if(grenade_amount < 0 || grenade_amount > 99)
	{
		printf("ZRobot::SetGrenadeAmount: had to adjust bad value:%d\n", grenade_amount);
		grenade_amount = 0;
	}
}
