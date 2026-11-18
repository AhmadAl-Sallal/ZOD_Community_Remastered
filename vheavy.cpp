#include "vheavy.h"
#include "zteam.h"

using namespace COMMON;

const double turrent_time_int = 1.0;

ZSDL_Surface VHeavy::base[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VHeavy::base_damaged[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VHeavy::top[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];

VHeavy::VHeavy(ZTime *ztime_, ZSettings *zsettings_) : ZVehicle(ztime_, zsettings_)
{
	double &the_time = ztime->ztime;

	width = 2;
	height = 2;
	width_pix = 32;
	height_pix = 32;
	hover_name = "Heavy";
	object_name = "heavy";

	InitTypeId(object_type, HEAVY);

	//object_id = HEAVY;
	//move_speed = HEAVY_SPEED;
	//attack_radius = HEAVY_ATTACK_RADIUS;
	//max_health = HEAVY_MAX_HEALTH;
	//health = max_health;

	//damage = HEAVY_DAMAGE;
	//damage_int_time = HEAVY_ATTACK_SPEED;
	//damage_radius = HEAVY_DAMAGE_RADIUS;
	//missile_speed = HEAVY_MISSILE_SPEED;

	has_explosives = true;
	damage_is_missile = true;
	has_lid = true;
	can_be_sniped = true;
	
	t_direction = direction;
	move_i = rand() % 3;
	
	next_turrent_time = the_time;
	next_turrent_time += turrent_time_int;	
}

void VHeavy::Init()
{
	int i, j, k;
	char filename_c[500];
	SDL_Surface *temp_surface;
	
	// Use the existing refined neutral sprite during this initial team-color test.
	strcpy(filename_c, "assets/units/vehicles/heavy/empty.png");
	temp_surface = IMG_Load_Error(filename_c);
	
	for(j=0;j<MAX_ANGLE_TYPES;j++)
		for(k=0;k<3;k++)
			base_damaged[0][j][k] = base[0][j][k] = temp_surface;

	ZSDL_FreeSurface(temp_surface);
	
	// Normal heavy body: one neutral sprite and one tint mask per movement frame.
	// Keep the original four base directions and their opposite-direction aliases.
	for(j=0;j<4;j++)
	{
		// Original asset scheme stores direction 315 at index 7 (not index 3).
		const int body_dir = (j == 3) ? 7 : j;

		for(k=0;k<3;k++)
		{
            // One shared damaged sprite per frame (no separate team masks).
            char damaged_filename[500];
            sprintf(damaged_filename,
                "assets/units/vehicles/heavy/base_damaged_heavy_r%03d_%02d.png",
                ROTATION[body_dir], k + 1);
            base_damaged[RED_TEAM][body_dir][k].LoadBaseImage(damaged_filename);
            for(i=1; i<MAX_TEAM_TYPES; ++i)
            {
                if(i != RED_TEAM)
                    base_damaged[i][body_dir][k] = base_damaged[RED_TEAM][body_dir][k];
                if(body_dir != 7)
                    base_damaged[i][body_dir + 4][2-k] = base_damaged[i][body_dir][k];
                else
                    base_damaged[i][body_dir - 4][2-k] = base_damaged[i][body_dir][k];
            }

			char neutral_filename[500];
			char mask_filename[500];
			sprintf(neutral_filename, "assets/units/vehicles/heavy/base_heavy_r%03d_%02d.png", ROTATION[body_dir], k + 1);
			sprintf(mask_filename, "assets/units/vehicles/heavy/base_heavy_r%03d_%02d_tintmask.png", ROTATION[body_dir], k + 1);

			ZSDL_Surface neutral;
			ZSDL_Surface tintmask;
			neutral.LoadBaseImage(neutral_filename);
			tintmask.LoadBaseImage(mask_filename);

			for(i=1;i<MAX_TEAM_TYPES;i++)
			{
				SDL_Surface *generated = ZTeam::MakeNeutralTint(
					i, neutral.GetBaseSurface(), tintmask.GetBaseSurface());
				if(generated)
					base[i][body_dir][k].LoadBaseImage(generated);
				else
					// When a mask fails, keep the neutral original visible.
					base[i][body_dir][k].LoadBaseImage(neutral_filename);

				// Retain the original opposite-direction animation-frame reversal.
				if(body_dir != 7)
					base[i][body_dir + 4][2-k] = base[i][body_dir][k];
				else
					base[i][body_dir - 4][2-k] = base[i][body_dir][k];

			}
		}
	}

	// Independently rotating turret: eight neutral directions, each with its own mask.
	for(j=0;j<MAX_ANGLE_TYPES;j++)
	{
		char neutral_filename[500];
		char mask_filename[500];
		sprintf(neutral_filename, "assets/units/vehicles/heavy/top_heavy_r%03d.png", ROTATION[j]);
		sprintf(mask_filename, "assets/units/vehicles/heavy/top_heavy_r%03d_tintmask.png", ROTATION[j]);

		ZSDL_Surface neutral;
		ZSDL_Surface tintmask;
		neutral.LoadBaseImage(neutral_filename);
		tintmask.LoadBaseImage(mask_filename);

		for(i=1;i<MAX_TEAM_TYPES;i++)
		{
			SDL_Surface *generated = ZTeam::MakeNeutralTint(
				i, neutral.GetBaseSurface(), tintmask.GetBaseSurface());
			if(generated)
				top[i][j].LoadBaseImage(generated);
			else
				top[i][j].LoadBaseImage(neutral_filename);
		}
	}

}
		
// SDL_Surface *VHeavy::GetRender()
// {
// 	return base[owner][direction][base_i];
// }

int VHeavy::Process()
{
	double &the_time = ztime->ztime;
	int tx, ty;
	int new_dir;

	ProcessLid();

	if(moving && the_time >= next_move_time)
	{
		move_i--;
		if(move_i < 0) move_i = 2;

		next_move_time = the_time + (VEHICLE_MOVE_ANIM_SPEED * SpeedOffsetPercentInv());
	}

	if(the_time >= next_turrent_time)
	{
		if(attack_object)
		{
			attack_object->GetCenterCords(tx, ty);
			new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);
			if(new_dir != -1) t_direction = new_dir;
		}
		else
		{
			next_turrent_time = the_time + turrent_time_int;
			
			t_direction++;
			if(t_direction >= MAX_ANGLE_TYPES) t_direction = 0;
		}
	}
	
	return 1;
}

void VHeavy::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
        int &x = loc.x;
        int &y = loc.y;
        const int turrent_x[MAX_ANGLE_TYPES] = {4, 2, -1, -3, 4, 2, -1, -3};
        const int turrent_y[MAX_ANGLE_TYPES] = {0, -3, -5, -4, 0, -3, -5, -4};
        const int turrent_shift_x[MAX_ANGLE_TYPES] = {4, 0, 0, 0, -4, 0, 0, 0};
        const int turrent_shift_y[MAX_ANGLE_TYPES] = {0, -2, -2, -2, 0, 0, 0, 0};
        const int lid_shift_x[MAX_ANGLE_TYPES] = {8, 13, 16, 17, 16, 11, 7, 7};
        const int lid_shift_y[MAX_ANGLE_TYPES] = {9, 9, 7, 4, 3, 2, 4, 7};
        ZSDL_Surface *base_surface;
        int lx, ly;
        int render_x, render_y;

        if(IsDestroyed())
        {
                render_x = x;
                render_y = y;
                AdjustRenderPositionForScale(&base_damaged[owner][direction][move_i], render_x, render_y);
                the_map.RenderZSurface(&base_damaged[owner][direction][move_i], render_x, render_y);

                return;
        }

        if(!ShowDamaged())
                base_surface = &base[owner][direction][move_i];
        else
                base_surface = &base_damaged[owner][direction][move_i];

        render_x = x;
        render_y = y;
        AdjustRenderPositionForScale(base_surface, render_x, render_y);
        the_map.RenderZSurface(base_surface, render_x, render_y, do_hit_effect);

        if(owner != NULL_TEAM && !destroyed)
        {
                lx = x + turrent_x[direction] + turrent_shift_x[t_direction];
                ly = y + turrent_y[direction] + turrent_shift_y[t_direction];

                render_x = lx;
                render_y = ly;
                AdjustRenderPositionForScale(&top[owner][t_direction], render_x, render_y);
                the_map.RenderZSurface(&top[owner][t_direction], render_x, render_y, do_hit_effect);

                lx = x + turrent_x[direction] + lid_shift_x[t_direction];
                ly = y + turrent_y[direction] + lid_shift_y[t_direction];

                RenderLid(the_map, dest, lx, ly, shift_x, shift_y);
        }

        do_hit_effect = false;
}

void VHeavy::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	//int &x = loc.x;
	//int &y = loc.y;
	//SDL_Rect from_rect, to_rect;
	//int lx, ly;
}

void VHeavy::FireMissile(int x_, int y_)
{
	int &x = loc.x;
	int &y = loc.y;

	const int sx[8] = {20, 12, 0, -12, -20, -12, 0, 12};
	const int sy[8] = {0, -12, -20, -12, 0, 12, 20, 12};

	//effect_list->push_back((ZEffect*)(new ELightRocket(x+17+sx[t_direction], y+14+sy[t_direction], x_, y_, HEAVY_MISSILE_SPEED, 0, 1, 1)));
	if(effect_list) effect_list->push_back((ZEffect*)(new ELightRocket(ztime, x+17+sx[t_direction], y+14+sy[t_direction], x_, y_, missile_speed, 0, 1, 1)));
	ZSoundEngine::PlayWavRestricted(HEAVY_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
}

void VHeavy::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(owner == NULL_TEAM) return;

	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new EDeath(ztime, loc.x, loc.y, EDEATH_TANK, &base_damaged[owner][direction][move_i])));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

//bool VHeavy::ServerFireTurrentMissile(int &x_, int &y_, int &damage, int &radius, double &offset_time)
//{
//	x_ = (loc.x + 16) + (30 - (rand() % 60));
//	y_ = (loc.y + 16) + (30 - (rand() % 60));
//	damage = 40;
//	radius = 40;
//	offset_time = 3 + (0.01 * (rand() % 100));
//	return true;
//}

vector<fire_missile_info> VHeavy::ServerFireTurrentMissile(int &damage, int &radius)
{
	vector<fire_missile_info> ret;
	fire_missile_info a_missile;
	int &max_horz = zsettings->max_turrent_horizontal_distance;
	int &max_vert = zsettings->max_turrent_vertical_distance;

	a_missile.missile_x = (loc.x + 16) + (max_horz - (rand() % (max_horz + max_horz)));
	a_missile.missile_y = (loc.y + 16) + (max_vert - (rand() % (max_vert + max_vert)));
	a_missile.missile_offset_time = 3 + (0.01 * (rand() % 100));

	damage = 40;
	radius = 40;

	ret.push_back(a_missile);

	return ret;
}

void VHeavy::FireTurrentMissile(int x_, int y_, double offset_time)
{
	if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, loc.x + 8, loc.y + 8, x_, y_, offset_time, ETURRENTMISSILE_HEAVY, owner)));
}
