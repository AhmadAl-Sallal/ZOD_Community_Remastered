#include "bradar.h"

ZSDL_Surface BRadar::base[MAX_PLANET_TYPES][MAX_TEAM_TYPES];
ZSDL_Surface BRadar::base_destroyed[MAX_PLANET_TYPES][MAX_TEAM_TYPES];

ZSDL_Surface BRadar::dish[16];
ZSDL_Surface BRadar::front_light[2];
ZSDL_Surface BRadar::side_light[2];

BRadar::BRadar(ZTime *ztime_, ZSettings *zsettings_, planet_type palette_) : ZBuilding(ztime_, zsettings_, palette_)
{
	// Current radar base sprites are 80 x 65 px (16 px map tiles).
	width = 5;
	height = 5;
	width_pix = 80;
	height_pix = 65;
	object_name = "radar";
	object_id = RADAR;
	palette = palette_;

	InitTypeId(object_type, object_id);

	//max_health = RADAR_BUILDING_HEALTH;
	//health = max_health;
	
	front_light_i = 0;
	side_light_i = 0;
	dish_i = 0;

	//fire effects
	effects_box.x = 1;
	effects_box.y = 6;
	effects_box.w = 44;
	effects_box.h = 30;
	max_effects = 6 + (rand() % 3);
}

// SDL_Surface *BRadar::GetRender()
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

void BRadar::Init()
{
	int i, j;
	string filename;
	char filename_c[500];
	
	// Automatic radar team tint: same working neutral+mask method as the fort flags.
	// Both images use the real 96x72 asset coordinates. No separate team PNG files.
	ZSDL_Surface tint_mask;
	ZSDL_Surface tint_mask_destroyed;
	tint_mask.LoadBaseImage("assets/buildings/radar/radar_tintmask.png");
	tint_mask_destroyed.LoadBaseImage("assets/buildings/radar/radar_tintmask_destroyed.png");

	for(i=0; i<MAX_PLANET_TYPES; ++i)
	{
		// The neutral variant (NULL_TEAM) is always the original gray building.
		base[i][NULL_TEAM].LoadBaseImage("assets/buildings/radar/base_radar.png");
		base_destroyed[i][NULL_TEAM].LoadBaseImage("assets/buildings/radar/base_radar_destroyed.png");

		for(j=1; j<MAX_TEAM_TYPES; ++j)
		{
			SDL_Surface *colored = ZTeam::MakeNeutralTint(
				j, base[i][NULL_TEAM].GetBaseSurface(), tint_mask.GetBaseSurface());
			if(colored)
				base[i][j].LoadBaseImage(colored);
			else
				base[i][j] = base[i][NULL_TEAM].GetBaseSurface(); // graceful gray fallback

			SDL_Surface *colored_destroyed = ZTeam::MakeNeutralTint(
				j, base_destroyed[i][NULL_TEAM].GetBaseSurface(),
				tint_mask_destroyed.GetBaseSurface());
			if(colored_destroyed)
				base_destroyed[i][j].LoadBaseImage(colored_destroyed);
			else
				base_destroyed[i][j] = base_destroyed[i][NULL_TEAM].GetBaseSurface();
		}
	}

	//now load effect images
	for(i=0;i<16;i++)
	{
		sprintf(filename_c, "assets/buildings/radar/dish_%d.png", i);
		dish[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
	}
	
	for(i=0;i<2;i++)
	{
		sprintf(filename_c, "assets/buildings/radar/front_light_%d.png", i);
		front_light[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
		
		sprintf(filename_c, "assets/buildings/radar/side_light_%d.png", i);
		side_light[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
	}
}

int BRadar::Process()
{
    double &the_time = ztime->ztime;
    const double min_interval_time = 0.125;

    ProcessBuildingsEffects(the_time);

    if(the_time - last_process_time >= min_interval_time)
    {
        last_process_time = the_time;

        // Radar: 16 billeder, 22.5 grader pr. billede
        dish_i++;
        if(dish_i >= 16) dish_i = 0;

        // Behold oprindelig hastighed på øvrige animationer
        if(dish_i % 2 == 0)
        {
            front_light_i++;
            if(front_light_i >= 2) front_light_i = 0;

            side_light_i++;
            if(side_light_i >= 2) side_light_i = 0;

        }
    }

    return 1;
}
void BRadar::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
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
			{
				base_surface = &base_destroyed[palette][owner];
			}
			else
			{
				base_surface = &base[palette][owner];
			}

			if(the_map.PermStamp(x, y, base_surface))
				do_base_rerender = false;
		}
	}
	else
	{
		if(IsDestroyed())
		{
			base_surface = &base_destroyed[palette][owner];
		}
		else
		{
			base_surface = &base[palette][owner];
		}
		
		the_map.RenderZSurface(base_surface, x, y);
		//if(the_map.GetBlitInfo(base_surface, x, y, from_rect, to_rect))
		//{
		//	to_rect.x += shift_x;
		//	to_rect.y += shift_y;

		//	SDL_BlitSurface( base_surface, &from_rect, dest, &to_rect);
		//}
	}

}

void BRadar::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	int &x = loc.x;
	int &y = loc.y;
	const int front_light_x = 32;
	const int front_light_y = 33;
	const int side_light_x = 62;
	const int side_light_y = 0;
	
	const int dish_x[16] = {
    20, 20, 19, 20,
    20, 20, 20, 20,
    20, 21, 21, 21,
    21, 20, 21, 20
};

	const int dish_y[16] = {
    -10, -10, -10, -10,
    -11, -12, -13, -15,
    -17, -16, -15, -13,
    -11, -10, -10, -10
};
	
	SDL_Rect from_rect, to_rect;
	int lx, ly;
	
	if(!IsDestroyed())
	{
		if(owner != NULL_TEAM)
		{
			lx = x + front_light_x;
			ly = y + front_light_y;

			the_map.RenderZSurface(&front_light[front_light_i], lx, ly);
			//if(the_map.GetBlitInfo(front_light[front_light_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( front_light[front_light_i], &from_rect, dest, &to_rect);
			//}
			
			lx = x + side_light_x;
			ly = y + side_light_y;
			
			the_map.RenderZSurface(&side_light[side_light_i], lx, ly);
			//if(the_map.GetBlitInfo(side_light[side_light_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( side_light[side_light_i], &from_rect, dest, &to_rect);
			//}
			
			lx = x + dish_x[dish_i]-13;
			ly = y + dish_y[dish_i]-56;
			
			the_map.RenderZSurface(&dish[dish_i], lx, ly);

            		//if(the_map.GetBlitInfo(dish[dish_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( dish[dish_i], &from_rect, dest, &to_rect);
			//}
		}
		else
		{
			lx = x + front_light_x;
			ly = y + front_light_y;

			the_map.RenderZSurface(&front_light[front_light_i], lx, ly);
			//if(the_map.GetBlitInfo(front_light[front_light_i], lx, ly, from_rect, to_rect))
			//{
			//	to_rect.x += shift_x;
			//	to_rect.y += shift_y;

			//	SDL_BlitSurface( front_light[front_light_i], &from_rect, dest, &to_rect);
			//}
		}
	}
	else
	{
		//IsDestroyed()
		const int x_plus = 18;
		const int y_plus = 0;

		if(the_map.GetBlitInfo(x + x_plus, y + y_plus, 48, 48, from_rect, to_rect))
		{
			from_rect.x += x_plus;
			from_rect.y += y_plus;

			to_rect.x += shift_x;
			to_rect.y += shift_y;

			base_destroyed[palette][owner].BlitSurface(&from_rect, &to_rect);
			//SDL_BlitSurface( base_destroyed[palette][owner], &from_rect, dest, &to_rect);
		}
	}

	//render effects
	for(vector<EStandard*>::iterator i=extra_effects.begin(); i!=extra_effects.end(); i++)
		(*i)->DoRender(the_map, dest);
}

void BRadar::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	int sx, sy;
	int ex, ey;
	double offset_time;
	int peices;
	int fireballs;
	int i;

	//fireballs
	{
		fireballs = 4 + (rand() % 3);

		for(i=0;i<fireballs;i++)
		{
			sx = loc.x + effects_box.x + (rand() % effects_box.w);
			sy = loc.y + effects_box.y + (rand() % effects_box.h);

			if(effect_list) effect_list->push_back((ZEffect*)(new ESideExplosion(ztime, sx, sy, 1.3)));
		}
	}

	peices = 3 + (rand() % 3);

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

	do_base_rerender = true;
}

void BRadar::SetMapImpassables(ZMap &tmap)
{
    const int tx = loc.x / 16;
    const int ty = loc.y / 16;

    // The visible radar body fills approx. 80 x 64 px. The final image
    // row (y = 64) is nearly transparent, so do not block a fifth tile row.
    // This avoids the old invisible 96 x 80 px collision rectangle.
    for(int i = 0; i < 5; i++)
    {
        for(int j = 0; j < 4; j++)
        {
            tmap.SetImpassable(tx + i, ty + j);
        }
    }
}

bool BRadar::GetCraneEntrance(int &x, int &y, int &x2, int &y2)
{
	// Align the crane approach with the radar's central front opening.
	x = x2 = loc.x + 42;
	y = y2 = loc.y + height_pix + 32;
	return true;
}

bool BRadar::GetCraneCenter(int &x, int &y)
{
    x = loc.x + 42;
    y = loc.y + 36;
    return true;
}
