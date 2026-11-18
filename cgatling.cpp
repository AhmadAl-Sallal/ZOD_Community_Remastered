#include "cgatling.h"
#include "zod_cannon_geometry.h"

ZSDL_Surface CGatling::passive[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface CGatling::fire[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
ZSDL_Surface CGatling::place[MAX_TEAM_TYPES][4];
ZSDL_Surface CGatling::wasted;

CGatling::CGatling(ZTime *ztime_, ZSettings *zsettings_, bool just_placed) : ZCannon(ztime_, zsettings_)
{
	hover_name = "Gatling";
	object_name = "gatling";

	can_snipe = true;

	InitTypeId(object_type, GATLING);


	renderfire = false;
	
	mode = ROTATING_MODE;
	
	place_i = 0;
	if(just_placed) mode = JUST_PLACED_MODE;
}

SDL_Surface *CGatling::GetPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

SDL_Surface *CGatling::GetNPlaceImage(int team_)
{
	return passive[team_][4].GetBaseSurface();
}

void CGatling::Init()
{
    int i, j, k;
    char filename_c[500];
    char mask_filename[500];

    // Destroyed cannon and unowned rotations stay neutral. Do not tint effects.
    wasted.LoadBaseImage("assets/units/cannons/gatling/wasted.png");
    for(j=0; j<MAX_ANGLE_TYPES; j++)
    {
        sprintf(filename_c, "assets/units/cannons/gatling/empty_r%03d.png", ROTATION[j]);
        passive[NULL_TEAM][j].LoadBaseImage(filename_c);
        fire[NULL_TEAM][j] = passive[NULL_TEAM][j];
    }

    // All 8 angles x 2 firing phases share one neutral image per frame.
    // Only the gunner's uniform is marked in each tint mask; the gun,
    // muzzle flash and remaining details retain their original colors.
    for(j=0; j<MAX_ANGLE_TYPES; j++)
        for(k=0; k<2; k++)
        {
            sprintf(filename_c,
                "assets/units/cannons/gatling/fire_gatling_r%03d_%02d.png",
                ROTATION[j], k + 1);
            sprintf(mask_filename,
                "assets/units/cannons/gatling/fire_gatling_r%03d_%02d_tintmask.png",
                ROTATION[j], k + 1);

            ZSDL_Surface neutral;
            ZSDL_Surface tintmask;
            neutral.LoadBaseImage(filename_c);
            tintmask.LoadBaseImage(mask_filename);

            for(i=1; i<MAX_TEAM_TYPES; i++)
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

    // The four "place" frames are part of the placement animation.
    // Frame 0 is deliberately uncolored (no mask). The following
    // masks use 30%, 60%, and 100% strength through PNG alpha.
    for(j=0; j<4; j++)
    {
        sprintf(filename_c,
            "assets/units/cannons/gatling/place_gatling_%02d.png", j + 1);
        place[NULL_TEAM][j].LoadBaseImage(filename_c);

        if(j == 0)
        {
            for(i=1; i<MAX_TEAM_TYPES; i++)
                place[i][j].LoadBaseImage(filename_c);
            continue;
        }

        sprintf(mask_filename,
            "assets/units/cannons/gatling/place_gatling_%02d_tintmask.png", j + 1);
        ZSDL_Surface neutral;
        ZSDL_Surface tintmask;
        neutral.LoadBaseImage(filename_c);
        tintmask.LoadBaseImage(mask_filename);

        for(i=1; i<MAX_TEAM_TYPES; i++)
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

void CGatling::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	(void)dest;
	(void)shift_x;
	(void)shift_y;
	const int unit_x = 0;
	const int unit_y = -7;
	const int base_x[8] = {1, 0, 0, 0, -1, 0, 0, 0};
	int &x = loc.x;
	int &y = loc.y;
	ZSDL_Surface *base_surface;
	int lx, ly;
	
	if(IsDestroyed())
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
				if(renderfire)
					base_surface = &fire[owner][direction];
				else
					base_surface = &passive[owner][direction];
				break;
		}
	}

	lx = x + unit_x + base_x[direction];
	ly = y + unit_y;
	AdjustRenderPositionForScale(base_surface, lx, ly);
	
	the_map.RenderZSurface(base_surface, lx, ly, do_hit_effect);
	do_hit_effect = false;
}


int CGatling::Process()
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
			//are we really attacking though?
			if(attack_object)
			{
				int tx, ty;
				int new_dir;

				if(the_time < next_attack_time) break;

				if(renderfire) renderfire = false;
				else renderfire = true;

				next_attack_time = the_time + 0.07 + ((rand() % 100) * 0.03 * 0.01);

				attack_object->GetCenterCords(tx, ty);
				new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);
				if(new_dir != -1) direction = new_dir;

				//now do a bullet effect
				if(renderfire)
				{
                    const ZODCannonPoint muzzle = ZODCannonMuzzle(GATLING, direction);
					int lx, ly;
					int w, h;

					attack_object->GetCords(tx, ty);
					attack_object->GetDimensionsPixel(w,h);

					//lx = loc.x + (width_pix / 2);
					//ly = loc.y + (height_pix / 2);

					tx += (rand() % w);
					ty += (rand() % h);

					lx = loc.x + muzzle.x;
					ly = loc.y + muzzle.y;

					//effect_list->push_back((ZEffect*)(new EBullet(owner, lx, ly, tx, ty)));
					if(effect_list) effect_list->push_back((ZEffect*)(new EBullet(ztime, owner, lx, ly, tx, ty)));
					ZSoundEngine::PlayWavRestricted(GATLING_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
				}

				break;
			}

			if(the_time - last_process_time < 1.0) break;
			if(owner == NULL_TEAM) break;
			last_process_time = the_time;
			
			direction++;
			if(direction >= MAX_ANGLE_TYPES) direction = 0;
			
			break;
		case C_ATTACKING:

			break;
	}

	return 1;
}

void CGatling::SetAttackObject(ZObject *obj)
{
	attack_object = obj;

	if(!obj) renderfire = false;
	else
	{
		int tx, ty;
		int new_dir;

		attack_object->GetCenterCords(tx, ty);
		new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);
		if(new_dir != -1) direction = new_dir;
	}
}

void CGatling::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(owner == NULL_TEAM) return;

	//effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(loc.x, loc.y, ECANNONDEATH_GATLING)));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

//bool CGatling::ServerFireTurrentMissile(int &x_, int &y_, int &damage, int &radius, double &offset_time)
//{
//	x_ = (loc.x + 16) + (30 - (rand() % 60));
//	y_ = (loc.y + 16) + (30 - (rand() % 60));
//	damage = 40;
//	radius = 40;
//	offset_time = 7 + (0.01 * (rand() % 300));
//	return true;
//}

vector<fire_missile_info> CGatling::ServerFireTurrentMissile(int &damage, int &radius)
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

void CGatling::FireTurrentMissile(int x_, int y_, double offset_time)
{
	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new ECannonDeath(ztime, loc.x, loc.y, ECANNONDEATH_GATLING, x_, y_, offset_time)));
	//effect_list->push_back((ZEffect*)(new ETurrentMissile(loc.x + 8, loc.y + 8, x_, y_, offset_time, ETURRENTMISSILE_LIGHT)));
}
