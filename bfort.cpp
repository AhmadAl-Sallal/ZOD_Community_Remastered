#include "bfort.h"
#include "gwproduction.h"

ZSDL_Surface BFort::base_front[MAX_PLANET_TYPES];
ZSDL_Surface BFort::base_back[MAX_PLANET_TYPES];
ZSDL_Surface BFort::base_front_destroyed[MAX_PLANET_TYPES];
ZSDL_Surface BFort::base_back_destroyed[MAX_PLANET_TYPES];
ZSDL_Surface BFort::base_front_destroyed_overlay[MAX_PLANET_TYPES];
ZSDL_Surface BFort::base_back_destroyed_overlay[MAX_PLANET_TYPES];
ZSDL_Surface BFort::flag[MAX_TEAM_TYPES][4];
ZSDL_Surface BFort::destroyed_overlay;

BFort::BFort(ZTime *ztime_, ZSettings *zsettings_, planet_type palette_, bool is_front_) : ZBuilding(ztime_, zsettings_, palette_)
{
	object_name = "fort";
	palette = palette_;
	//is_front = is_front_;
	next_flag_time = 0;
	flag_i = 0;
	destroyed_fade = 0;
	last_fade_time = 0;
	fade_dir = 100;

	//max_health = FORT_BUILDING_HEALTH;
	//health = max_health;
	
	SetIsFront(is_front_);

	InitTypeId(object_type, object_id);

	//if(is_front)
	//{
	//	object_id = FORT_FRONT;
	//	width = 10;
	//	height = 12;
	//	width_pix = 10 * 16;
	//	height_pix = 12 * 16;
	//	unit_create_x = 80;
	//	unit_create_y = 128;
	//	unit_move_x = 80;
	//	unit_move_y = 192 + 16;
	//}
	//else
	//{
	//	object_id = FORT_BACK;
	//	width = 10;
	//	height = 11;
	//	width_pix = 10 * 16;
	//	height_pix = 11 * 16;
	//	unit_create_x = 80;
	//	unit_create_y = 32;
	//	unit_move_x = 80;
	//	unit_move_y = -16;
	//}

	//fire effects
	// Match the 200x210 fort artwork instead of the old 160px footprint.
	effects_box.x = 16;
	effects_box.y = 14;
	effects_box.w = 168;
	effects_box.h = 166;
	max_effects = 20 + (rand() % 8);
}

void BFort::Init()
{
	string filename;
	char filename_c[500];
	int i, j;

	destroyed_overlay.LoadBaseImage("assets/buildings/fort/destroyed_overlay.png");// = IMG_Load_Error("assets/buildings/fort/destroyed_overlay.png");

	// Final fort colors: generate team flag frames from neutral images and masks.
	// ZTeam::Make stays available elsewhere for existing unit artwork.
	for(j=0;j<4;j++)
	{
		sprintf(filename_c, "assets/buildings/fort/flag_null_n%02d.png", j);
		flag[NULL_TEAM][j].LoadBaseImage(filename_c);

		ZSDL_Surface tint_mask;
		sprintf(filename_c, "assets/buildings/fort/flag_tintmask_n%02d.png", j);
		tint_mask.LoadBaseImage(filename_c);

		for(i=1;i<MAX_TEAM_TYPES;i++)
		{
			SDL_Surface *generated = ZTeam::MakeNeutralTint(
				i, flag[NULL_TEAM][j].GetBaseSurface(), tint_mask.GetBaseSurface());
			if(generated)
			{
				flag[i][j].LoadBaseImage(generated);
			}
			else
			{
				// The historical per-team PNGs are not shipped. Retain neutral art.
				fprintf(stderr, "ZOD Fort: failed flag tint (team %d, frame %d).\n", i, j);
				flag[i][j] = flag[NULL_TEAM][j].GetBaseSurface();
			}
		}
	}
	
	//load images pertaining to a fort
	for(i=0;i<MAX_PLANET_TYPES;i++)
	{
		filename = "assets/buildings/fort/fort_front.png";
		base_front[i].LoadBaseImage(filename);// = IMG_Load_Error(filename);
		
		filename = "assets/buildings/fort/fort_back.png";
		base_back[i].LoadBaseImage(filename);// = IMG_Load_Error(filename);
		
		filename = "assets/buildings/fort/fort_front_destroyed.png";
		base_front_destroyed[i].LoadBaseImage(filename);// = IMG_Load_Error(filename);
		
		filename = "assets/buildings/fort/fort_back_destroyed.png";
		base_back_destroyed[i].LoadBaseImage(filename);// = IMG_Load_Error(filename);

		//copies
		//if(destroyed_overlay && base_front_destroyed[i])
		{
			//base_front_destroyed_overlay[i] = SDL_DisplayFormat(base_front_destroyed[i]);//CopyImage(base_front_destroyed[i]);
			//SDL_BlitSurface( destroyed_overlay, NULL, base_front_destroyed_overlay[i], NULL);
			base_front_destroyed_overlay[i] = base_front_destroyed[i].GetBaseSurface();
			destroyed_overlay.BlitSurface(NULL, NULL, &base_front_destroyed_overlay[i]);

			base_front_destroyed_overlay[i].UseDisplayFormat();
		}

		//if(destroyed_overlay && base_back_destroyed[i])
		{
			//base_back_destroyed_overlay[i] = SDL_DisplayFormat(base_back_destroyed[i]);//CopyImage(base_back_destroyed[i]);
			//SDL_BlitSurface( destroyed_overlay, NULL, base_back_destroyed_overlay[i], NULL);
			base_back_destroyed_overlay[i] = base_back_destroyed[i].GetBaseSurface();
			destroyed_overlay.BlitSurface(NULL, NULL, &base_back_destroyed_overlay[i]);

			base_back_destroyed_overlay[i].UseDisplayFormat();
		}
	}
}

// SDL_Surface *BFort::GetRender()
// {
// 	if(is_front)
// 	{
// 		if(destroyed)
// 			return base_front_destroyed[palette];
// 		else
// 			return base_front[palette];
// 	}
// 	else
// 	{
// 		if(destroyed)
// 			return base_back_destroyed[palette];
// 		else
// 			return base_back[palette];
// 	}
// }

void BFort::SetIsFront(bool is_front_)
{
	is_front = is_front_;
	object_id = is_front ? FORT_FRONT : FORT_BACK;

	// Fort sprites are 200 x 210 pixels (both front and back).
	// The map footprint is tile-aligned; the selection/render footprint
	// uses the exact sprite size, so clicks and centers match the artwork.
	width = 13;    // ceil(200 / 16)
	height = 14;   // ceil(210 / 16)
	width_pix = 200;
	height_pix = 210;

	// Spawn from the visible doorway; move just outside the sprite.
	// Front leaves south; back leaves north. Keep X in the middle.
	unit_create_x = 102;
	unit_move_x = unit_create_x;
	if(is_front)
	{
		unit_create_y = 170;
		unit_move_y = 232;
	}
	else
	{
		unit_create_y = 76;
		unit_move_y = -24;
	}

	center_x = loc.x + (width_pix >> 1);
	center_y = loc.y + (height_pix >> 1);
}

int BFort::Process()
{
	double &the_time = ztime->ztime;

	ProcessBuildingsEffects(the_time);

	if(the_time > next_flag_time)
	{
		flag_i++;
		if(flag_i >= 4) flag_i = 0;

		next_flag_time = the_time + 0.2;
	}


	if(build_state != BUILDING_SELECT)
		ResetShowTime((int)ProductionTimeLeft(the_time));
	else
		ResetShowTime(-1);

	//fade
	destroyed_fade += (the_time - last_fade_time) * fade_dir;
	last_fade_time = the_time;
	if(destroyed_fade > 254)
	{
		destroyed_fade = 254;
		fade_dir *= -1;
	}
	else if(destroyed_fade < 1)
	{
		destroyed_fade = 1;
		fade_dir *= -1;
	}

	return 1;
}

void BFort::DoPreRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	int &x = loc.x;
	int &y = loc.y;
	SDL_Rect from_rect, to_rect;
	ZSDL_Surface *destroyed_overlay_img;

	if(IsDestroyed())
	{
		if(is_front)
			destroyed_overlay_img = &base_front_destroyed_overlay[palette];
		else
			destroyed_overlay_img = &base_back_destroyed_overlay[palette];

		destroyed_overlay_img->SetAlpha(destroyed_fade);
		the_map.RenderZSurface(destroyed_overlay_img, x, y);
		//if(the_map.GetBlitInfo(destroyed_overlay_img, x, y, from_rect, to_rect))
		//{
		//	to_rect.x += shift_x;
		//	to_rect.y += shift_y;

		//	//SDL_SetAlpha(destroyed_overlay_img, SDL_RLEACCEL | SDL_SRCALPHA,(Uint8)destroyed_fade);
		//	//SDL_BlitSurface( destroyed_overlay_img, &from_rect, dest, &to_rect);
		//}
	}
}

void BFort::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
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
			if(is_front)
			{
				if(IsDestroyed())
					base_surface = &base_front_destroyed[palette];
				else
					base_surface = &base_front[palette];
			}
			else
			{
				if(IsDestroyed())
					base_surface = &base_back_destroyed[palette];
				else
					base_surface = &base_back[palette];
			}

			if(the_map.PermStamp(x, y, base_surface))
				do_base_rerender = false;
			
			
		}
	}
	else
	{
		if(is_front)
		{
			if(IsDestroyed())
				base_surface = &base_front_destroyed[palette];
			else
				base_surface = &base_front[palette];
		}
		else
		{
			if(IsDestroyed())
				base_surface = &base_back_destroyed[palette];
			else
				base_surface = &base_back[palette];
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

void BFort::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	int &x = loc.x;
	int &y = loc.y;
	SDL_Rect from_rect, to_rect;
	int lx, ly;

	//unit creation cover
	RenderUnitCover(the_map, dest, shift_x, shift_y);

	//render only alive stuff
	if(!IsDestroyed())
	{
		//render time
		// Keep the live production countdown centered over the doorway.
		SDL_Surface *time_surface = show_time_img.GetBaseSurface();

		if(is_front)
	{
    		// FORT FRONT
    		lx = x + 102 - (time_surface ? (time_surface->w >> 1) : 0);
    		ly = y + 139;
	}
	else
	{
    		// FORT BACK
    		lx = x + 102 - (time_surface ? (time_surface->w >> 1) : 0);
    		ly = y + 141;
	}
		// Move the timer down onto the panel just above the doorway.

		the_map.RenderZSurface(&show_time_img, lx, ly);
		//if(show_time_img && the_map.GetBlitInfo(show_time_img, x + 74, y + 90, from_rect, to_rect))
		//{
		//	to_rect.x += shift_x;
		//	to_rect.y += shift_y;

		//	SDL_BlitSurface( show_time_img, &from_rect, dest, &to_rect);
		//}


		//render flag
		// Place the 48x36 team flag in the cyan-striped roof square.
		// Keep team-tint animation and the existing flag assets unchanged.
		lx = x + 94;
		ly = y + 46;

		the_map.RenderZSurface(&flag[owner][flag_i], lx, ly);
		//if(the_map.GetBlitInfo(flag[owner][flag_i], lx, ly, from_rect, to_rect))
		//{
		//	to_rect.x += shift_x;
		//	to_rect.y += shift_y;

		//	SDL_BlitSurface( flag[owner][flag_i], &from_rect, dest, &to_rect);
		//}
	}

	//render effects
	for(vector<EStandard*>::iterator i=extra_effects.begin(); i!=extra_effects.end(); i++)
		(*i)->DoRender(the_map, dest);
}

void BFort::RenderUnitCover(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	int &x = loc.x;
	int &y = loc.y;
	SDL_Rect from_rect, to_rect;
	int lx, ly;

	//unit creation cover
	if(is_front)
	{
		// Cover the front gate as units are created behind it.
		if(the_map.GetBlitInfo(x + 70, y + 158, 60, 52, from_rect, to_rect))
		{
			from_rect.x += 70;
			from_rect.y += 158;

			to_rect.x += shift_x;
			to_rect.y += shift_y;

			if(IsDestroyed())
			{
				base_front_destroyed[palette].BlitSurface(&from_rect, &to_rect);
				base_front_destroyed_overlay[palette].BlitSurface(&from_rect, &to_rect);
			}
			else
				base_front[palette].BlitSurface(&from_rect, &to_rect);

			//base_surface->BlitSurface(&from_rect, &to_rect);
			//SDL_BlitSurface( base_front[palette], &from_rect, dest, &to_rect);
		}
	}
	else
	{
		// Cover the central top doorway on back-facing forts.
		if(the_map.GetBlitInfo(x + 70, y + 30, 60, 76, from_rect, to_rect))
		{
			from_rect.x += 70;
			from_rect.y += 30;

			to_rect.x += shift_x;
			to_rect.y += shift_y;

			if(IsDestroyed())
			{
				base_back_destroyed[palette].BlitSurface(&from_rect, &to_rect);
				base_back_destroyed_overlay[palette].BlitSurface(&from_rect, &to_rect);
			}
			else
				base_back[palette].BlitSurface(&from_rect, &to_rect);

			//base_surface->BlitSurface(&from_rect, &to_rect);
			//SDL_BlitSurface( base_front[palette], &from_rect, dest, &to_rect);
		}
	}
}

ZGuiWindow *BFort::MakeGuiWindow()
{
	GWProduction *ret;

	if(IsDestroyed()) return NULL;

	ret = new GWProduction(ztime);

	ret->SetMap(zmap);
	ret->SetType(GWPROD_FORT);
	ret->SetCords(loc.x, loc.y);
	ret->SetBuildingObj(this);

	return (ZGuiWindow *)ret;
}

void BFort::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	int sx, sy;
	int ex, ey;
	double offset_time;
	int peices;
	int fireballs;
	int i;

	//fireballs
	{
		fireballs = 12 + (rand() % 6);

		for(i=0;i<fireballs;i++)
		{
			sx = loc.x + effects_box.x + (rand() % effects_box.w);
			sy = loc.y + effects_box.y + (rand() % effects_box.h);

			if(effect_list) effect_list->push_back((ZEffect*)(new ESideExplosion(ztime, sx, sy, 1.3)));
		}
	}

	//turrents
	{
		peices = 16 + (rand() % 6);

		for(i=0;i<peices;i++)
		{
			sx = loc.x + effects_box.x + (rand() % effects_box.w);
			sy = loc.y + effects_box.y + (rand() % effects_box.h);
			ex = loc.x + (width_pix >> 1) + (200 - (rand() % 400));
			ey = loc.y + (height_pix >> 1) + (200 - (rand() % 400));

			offset_time = 3 + (0.01 * (rand() % 200));

			switch(rand()%5)
			{
			case 0:
				if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, sx, sy, ex, ey, offset_time, ETURRENTMISSILE_FORT_BUILDING_PEICE0)));
				break;
			case 1:
				if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, sx, sy, ex, ey, offset_time, ETURRENTMISSILE_FORT_BUILDING_PEICE1)));
				break;
			case 2:
				if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, sx, sy, ex, ey, offset_time, ETURRENTMISSILE_FORT_BUILDING_PEICE2)));
				break;
			case 3:
				if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, sx, sy, ex, ey, offset_time, ETURRENTMISSILE_FORT_BUILDING_PEICE3)));
				break;
			case 4:
				if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, sx, sy, ex, ey, offset_time, ETURRENTMISSILE_FORT_BUILDING_PEICE4)));
				break;
			}
		}
	}

	ZSoundEngine::PlayWavRestricted(RANDOM_EXPLOSION_SND, loc.x, loc.y, width_pix, height_pix);
	ZSoundEngine::PlayWavRestricted(RANDOM_EXPLOSION_SND, loc.x, loc.y, width_pix, height_pix);
	ZSoundEngine::PlayWavRestricted(RANDOM_EXPLOSION_SND, loc.x, loc.y, width_pix, height_pix);
	ZSoundEngine::PlayWavRestricted(RANDOM_EXPLOSION_SND, loc.x, loc.y, width_pix, height_pix);

	do_base_rerender = true;
}

bool BFort::CanEnterFort(int team)
{
	if(team == owner) return false;
	if(IsDestroyed()) return false;

	return true;
}

bool BFort::UnderCursorCanAttack(int &map_x, int &map_y)
{
	const int rx = map_x - loc.x;
	const int ry = map_y - loc.y;

	// Approximate the visible 200x210 fort silhouette, not the old
	// 160px click area. Avoid most transparent corner regions.
	if(points_within_area(rx, ry, 16, 0, 54, 112)) return true;  // left towers
	if(points_within_area(rx, ry, 130, 0, 54, 112)) return true; // right towers
	if(points_within_area(rx, ry, 30, 32, 140, 150)) return true; // central hull
	if(points_within_area(rx, ry, 0, 82, 200, 92)) return true; // side wings
	if(points_within_area(rx, ry, 32, 168, 42, 42)) return true; // left foot
	if(points_within_area(rx, ry, 126, 168, 42, 42)) return true; // right foot

	return false;
}

bool BFort::UnderCursorFortCanEnter(int &map_x, int &map_y)
{
	const int rx = map_x - loc.x;
	const int ry = map_y - loc.y;

	// Preserve easy center clicking while including the actual door.
	// FRONT gate is at the bottom; BACK gate is towards the top.
	if(is_front)
		return points_within_area(rx, ry, 64, 52, 72, 158);
	else
		return points_within_area(rx, ry, 64, 16, 72, 112);
}

bool BFort::CannonNotPlacable(int &map_left, int &map_right, int &map_top, int &map_bottom)
{
	const int local_x = map_left - loc.x;
	const int local_y = map_top - loc.y;

	// Turret centers in the new sprite are approximately x=44/160,
	// y=24/92. Permit the corresponding 16px cannon tile origins.
	if((local_x == 32 || local_x == 144) &&
	   (local_y == 16 || local_y == 80)) return false;

	return WithinSelection(map_left, map_right, map_top, map_bottom);
}

void BFort::SetMapImpassables(ZMap &tmap)
{
	const int tx = loc.x / 16;
	const int ty = loc.y / 16;

	// The artwork covers 200x210px: at most 13x14 map tiles.
	// The previous 15x15 scaled wall mask blocked EMPTY space well
	// beyond the building, and its corridor was off-center.
	//
	// Block the visible main hull, turrets, wings and the two feet.
	// Leave transparent corners and the 14th bottom row untouched.
	// Open a two-tile corridor at the actual doorway, so newly
	// produced units can leave the fort without walking into a wall.
	for(int y = 0; y < 14; y++)
	{
		for(int x = 0; x < 13; x++)
		{
			const bool main_hull = y >= 2 && y <= 11 && x >= 1 && x <= 11;
			const bool top_towers = y <= 1 &&
				((x >= 1 && x <= 4) || (x >= 8 && x <= 11));
			const bool side_wings = y >= 5 && y <= 10 && (x == 0 || x == 12);
			const bool bottom_feet = y == 12 &&
				((x >= 2 && x <= 4) || (x >= 9 && x <= 10));
			const bool blocks = main_hull || top_towers || side_wings || bottom_feet;

			const bool doorway = (x == 5 || x == 6) &&
				(is_front ? (y >= 10) : (y <= 6));

			if(doorway)
				tmap.SetImpassable(tx + x, ty + y, false);
			else if(blocks)
				tmap.SetImpassable(tx + x, ty + y);
		}
	}
}
