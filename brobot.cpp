#include "brobot.h"
#include "gwproduction.h"

ZSDL_Surface BRobot::base[MAX_PLANET_TYPES][MAX_TEAM_TYPES];
ZSDL_Surface BRobot::base_destroyed[MAX_PLANET_TYPES][MAX_TEAM_TYPES];

ZSDL_Surface BRobot::spin[8];
ZSDL_Surface BRobot::green_box[6];
ZSDL_Surface BRobot::robot[2];

BRobot::BRobot(ZTime *ztime_, ZSettings *zsettings_, planet_type palette_) : ZBuilding(ztime_, zsettings_, palette_)
{
	// Sprite is 90x110 px; the enclosing tile footprint is 6x7 (16px tiles).
	width = 6;
	height = 7;
	width_pix = 90;
	height_pix = 110;
	object_name = "robot_factory";
	object_id = ROBOT_FACTORY;
	palette = palette_;

	InitTypeId(object_type, object_id);

	//max_health = ROBOT_BUILDING_HEALTH;
	//health = max_health;
	
	spin_i = 0;
	green_box_i = 0;
	robot_i = 0;
	exhaust_i = 0;

	// Robot doorway center is around x=56 (marked opening x=46..67).
	// Spawn inside the visible doorway, then move straight out below the sprite.
	unit_create_x = 56;
	unit_create_y = 100;
	unit_move_x = unit_create_x;
	unit_move_y = 128;

	//fire effects
	effects_box.x = 8;
	effects_box.y = 8;
	effects_box.w = width_pix - (16 + 8);
	effects_box.h = height_pix - (16 + 8);
	max_effects = 8 + (rand() % 4);
}

// SDL_Surface *BRobot::GetRender()
// {
// 	if(destroyed)
// 	{
// 		return base_destroyed[palette][owner];
// 	}
// 	else
// 	{
// 		return base[palette][owner];
// 	}
// }

void BRobot::Init()
{
	int i, j;
	char filename_c[500];
	
	// Normal and destroyed Robot Factory use the existing neutral sprites
	// and their approved entrance tint masks. No old per-team PNG overlays.
	ZSDL_Surface entrance_tintmask;
	ZSDL_Surface entrance_destroyed_tintmask;
	entrance_tintmask.LoadBaseImage("assets/buildings/robot/entrance_tintmask.png");
	entrance_destroyed_tintmask.LoadBaseImage("assets/buildings/robot/entrance_destroyed_tintmask.png");

	for(i=0;i<MAX_PLANET_TYPES;i++)
	{
		base[i][NULL_TEAM].LoadBaseImage("assets/buildings/robot/base_robot.png");
		base_destroyed[i][NULL_TEAM].LoadBaseImage("assets/buildings/robot/base_robot_destroyed.png");

		for(j=1;j<MAX_TEAM_TYPES;j++)
		{
			SDL_Surface *colored = ZTeam::MakeNeutralTint(
				j, base[i][NULL_TEAM].GetBaseSurface(), entrance_tintmask.GetBaseSurface());
			if(colored)
				base[i][j].LoadBaseImage(colored);
			else
				base[i][j] = base[i][NULL_TEAM].GetBaseSurface();

			SDL_Surface *colored_destroyed = ZTeam::MakeNeutralTint(
				j, base_destroyed[i][NULL_TEAM].GetBaseSurface(),
				entrance_destroyed_tintmask.GetBaseSurface());
			if(colored_destroyed)
				base_destroyed[i][j].LoadBaseImage(colored_destroyed);
			else
				base_destroyed[i][j] = base_destroyed[i][NULL_TEAM].GetBaseSurface();
		}
	}

	//load effects
	for(i=0;i<8;i++)
	{
		sprintf(filename_c, "assets/buildings/robot/spin_%d.png", i);
		spin[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
	}
	
	for(i=0;i<6;i++)
	{
		sprintf(filename_c, "assets/buildings/robot/green_box_%d.png", i);
		green_box[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
	}
	
	for(i=0;i<2;i++)
	{
		sprintf(filename_c, "assets/buildings/robot/robot_%d.png", i);
		robot[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
	}
}

int BRobot::Process()
{
	double &the_time = ztime->ztime;
	const double min_interval_time = 0.25;

	ProcessBuildingsEffects(the_time);
	
	if(the_time - last_process_time >= min_interval_time)
	{
		last_process_time = the_time;
		
		spin_i++;
		if(spin_i >= 8) spin_i = 0;
		
		green_box_i++;
		if(green_box_i >= 6) green_box_i = 0;
		
		robot_i++;
		if(robot_i >= 2) robot_i = 0;
		
		exhaust_i++;
		if(exhaust_i >= 14) exhaust_i = 0;
		
	}

	if(build_state != BUILDING_SELECT)
		ResetShowTime((int)ProductionTimeLeft(the_time));
	else
		ResetShowTime(-1);

	return 1;
}

void BRobot::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	int &x = loc.x;
	int &y = loc.y;
	ZSDL_Surface *base_surface;
	SDL_Rect from_rect, to_rect;
	int lx, ly;
	
	if(!dont_stamp)
	{
		if(do_base_rerender)
		{
			if(IsDestroyed())
				base_surface = &base_destroyed[palette][owner];
			else
				base_surface = &base[palette][owner];
			
			if(the_map.PermStamp(x, y, base_surface))
				do_base_rerender = false;
		}
	}
	else
	{
		if(IsDestroyed())
			base_surface = &base_destroyed[palette][owner];
		else
			base_surface = &base[palette][owner];

		the_map.RenderZSurface(base_surface, x, y);
		//if(the_map.GetBlitInfo(base_surface, x, y, from_rect, to_rect))
		//{
		//	to_rect.x += shift_x;
		//	to_rect.y += shift_y;

		//	SDL_BlitSurface( base_surface, &from_rect, dest, &to_rect);
		//}
	}
}

void BRobot::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	int &x = loc.x;
	int &y = loc.y;
	const int spin_x = 15;
	const int spin_y = 3;
	const int robot_x = 52;
	const int robot_y = 57;
	const int level_x = 37;
	const int level_y = 83;
	const int green_box_x = 24;
	const int green_box_y = 44;
	const int exhaust_x = 45;
	const int exhaust_y = -25;
	
	SDL_Rect from_rect, to_rect;
	int lx, ly;
	
	//render cover
	if(!IsDestroyed())
	{
		if(owner != NULL_TEAM && build_state != BUILDING_SELECT)
		{
			if(the_map.GetBlitInfo(x + 47, y + 71, 36, 30, from_rect, to_rect))
			{
				from_rect.x += 47;
				from_rect.y += 71;

				to_rect.x += shift_x;
				to_rect.y += shift_y;

				base[palette][owner].BlitSurface(&from_rect, &to_rect);
				//SDL_BlitSurface( base[palette][owner], &from_rect, dest, &to_rect);
			}

			lx = x + exhaust_x;
			ly = y + exhaust_y;
			if(exhaust_i > 0)
				ly -= (exhaust_i - 1) * 3;
			the_map.RenderZSurface(&exhaust[exhaust_i], lx, ly);

			lx = x + green_box_x;
			ly = y + green_box_y;

			the_map.RenderZSurface(&green_box[green_box_i], lx, ly);
			//if(the_map.GetBlitInfo(green_box[green_box_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( green_box[green_box_i], &from_rect, dest, &to_rect);
			//}

			lx = x + robot_x;
			ly = y + robot_y;

			the_map.RenderZSurface(&robot[robot_i], lx, ly);
			//if(the_map.GetBlitInfo(robot[robot_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( robot[robot_i], &from_rect, dest, &to_rect);
			//}

			lx = x + spin_x;
			ly = y + spin_y;

			the_map.RenderZSurface(&spin[spin_i], lx, ly);
			//if(the_map.GetBlitInfo(spin[spin_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( spin[spin_i], &from_rect, dest, &to_rect);
			//}

			//render time
			lx = x + 15;
			ly = y + 80;

			the_map.RenderZSurface(&show_time_img, lx, ly);
			//if(show_time_img && the_map.GetBlitInfo(show_time_img, x + 35, y + 58, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( show_time_img, &from_rect, dest, &to_rect);
			//}
		}
		else
		{
			lx = x + robot_x;
			ly = y + robot_y;

			the_map.RenderZSurface(&robot[robot_i], lx, ly);
			//if(the_map.GetBlitInfo(robot[robot_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( robot[robot_i], &from_rect, dest, &to_rect);
			//}
		}
	}
	else
	{
		//IsDestroyed()
		const int x_plus = 29;
		const int y_plus = 12;

		if(the_map.GetBlitInfo(x + x_plus, y + y_plus, 48, 89, from_rect, to_rect))
		{
			from_rect.x += x_plus;
			from_rect.y += y_plus;

			to_rect.x += shift_x;
			to_rect.y += shift_y;

			base_destroyed[palette][owner].BlitSurface(&from_rect, &to_rect);
			//SDL_BlitSurface( base_destroyed[palette][owner], &from_rect, dest, &to_rect);
		}
	}

	//render level
	lx = x + level_x;
	ly = y + level_y;

	the_map.RenderZSurface(&level_img[level], lx, ly);
	//if(the_map.GetBlitInfo(level_img[level], lx, ly, from_rect, to_rect))
	//{
	//	to_rect.x += shift_x;
	//	to_rect.y += shift_y;

	//	SDL_BlitSurface( level_img[level], &from_rect, dest, &to_rect);
	//}

	//render effects
	for(vector<EStandard*>::iterator i=extra_effects.begin(); i!=extra_effects.end(); i++)
		(*i)->DoRender(the_map, dest);
}

ZGuiWindow *BRobot::MakeGuiWindow()
{
	GWProduction *ret;

	if(IsDestroyed()) return NULL;

	ret = new GWProduction(ztime);

	ret->SetMap(zmap);
	ret->SetType(GWPROD_ROBOT);
	ret->SetCords(loc.x, loc.y);
	ret->SetBuildingObj(this);

	return (ZGuiWindow *)ret;
}

void BRobot::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	int sx, sy;
	int ex, ey;
	double offset_time;
	int peices;
	int fireballs;
	int i;

	//fireballs
	{
		fireballs = 8 + (rand() % 3);

		for(i=0;i<fireballs;i++)
		{
			sx = loc.x + effects_box.x + (rand() % effects_box.w);
			sy = loc.y + effects_box.y + (rand() % effects_box.h);

			if(effect_list) effect_list->push_back((ZEffect*)(new ESideExplosion(ztime, sx, sy, 1.3)));
		}
	}

	peices = 6 + (rand() % 3);

	for(i=0;i<peices;i++)
	{
		sx = loc.x + effects_box.x + (rand() % effects_box.w);
		sy = loc.y + effects_box.y + (rand() % effects_box.h);
		ex = loc.x + (width_pix >> 1) + (200 - (rand() % 400));
		ey = loc.y + (height_pix >> 1) + (200 - (rand() % 400));

		offset_time = 1.5 + (0.01 * (rand() % 200));

		switch(rand()%2)
		{
		case 0:
			if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, sx, sy, ex, ey, offset_time, ETURRENTMISSILE_BUILDING_PEICE0)));
			break;
		case 1:
			if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, sx, sy, ex, ey, offset_time, ETURRENTMISSILE_BUILDING_PEICE1)));
			break;
		}
	}

	ZSoundEngine::PlayWavRestricted(RANDOM_EXPLOSION_SND, loc.x, loc.y, width_pix, height_pix);
	ZSoundEngine::PlayWavRestricted(RANDOM_EXPLOSION_SND, loc.x, loc.y, width_pix, height_pix);
	do_base_rerender = true;
}

void BRobot::SetMapImpassables(ZMap &tmap)
{
    const int tx = loc.x / 16;
    const int ty = loc.y / 16;

    // 90x110 sprite: block only its 6x7-tile footprint (not the old 8x8).
    for(int i = 0; i < width; ++i)
        for(int j = 0; j < height; ++j)
            tmap.SetImpassable(tx + i, ty + j);

    // Center doorway around x=56: tile 3 (x=48..63) leads inside,
    // and the wider lower opening allows a clean exit past the door frame.
    // The row below the building (j == 7) is deliberately left untouched.
    tmap.SetImpassable(tx + 3, ty + 5, false);
    for(int i = 2; i <= 4; ++i)
        tmap.SetImpassable(tx + i, ty + height - 1, false);
}

bool BRobot::GetCraneEntrance(int &x, int &y, int &x2, int &y2)
{
	// Align the crane approach with the robot doorway, outside the 110px sprite.
	x = x2 = loc.x + 56;
	y = y2 = loc.y + 128;
	return true;
}

bool BRobot::GetCraneCenter(int &x, int &y)
{
	// Crane service point just inside the same doorway (not geometric center).
	x = loc.x + 56;
	y = loc.y + 94;
	return true;
}
