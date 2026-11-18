#include "vlight.h"
#include "zteam.h"

using namespace COMMON;

const double turrent_time_int = 1.0;

ZSDL_Surface VLight::base[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VLight::base_damaged[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][3];
ZSDL_Surface VLight::top[MAX_ANGLE_TYPES];

VLight::VLight(ZTime *ztime_, ZSettings *zsettings_) : ZVehicle(ztime_, zsettings_)
{
	double &the_time = ztime->ztime;

	width = 2;
	height = 2;
	width_pix = 32;
	height_pix = 32;
	hover_name = "Light";
	object_name = "light";

	can_be_sniped = true;

	InitTypeId(object_type, LIGHT);

	//object_id = LIGHT;
	//move_speed = LIGHT_SPEED;
	//attack_radius = LIGHT_ATTACK_RADIUS;
	//max_health = LIGHT_MAX_HEALTH;
	//health = max_health;

	//damage = LIGHT_DAMAGE;
	//damage_int_time = LIGHT_ATTACK_SPEED;
	//damage_radius = LIGHT_DAMAGE_RADIUS;
	//missile_speed = LIGHT_MISSILE_SPEED;

	has_explosives = true;
	damage_is_missile = true;
	has_lid = true;
	
	t_direction = direction;
	move_i = rand() % 3;
	
	next_turrent_time = the_time;
	next_turrent_time += turrent_time_int;
}

void VLight::Init()
{
    int i, j, k;
    char filename_c[500];
    SDL_Surface *temp_surface;

    // A light tank with no owner keeps its original neutral empty sprite.
    strcpy(filename_c, "assets/units/vehicles/light/empty.png");
    temp_surface = IMG_Load_Error(filename_c);
    for(j=0; j<MAX_ANGLE_TYPES; ++j)
        for(k=0; k<3; ++k)
            base_damaged[NULL_TEAM][j][k] = base[NULL_TEAM][j][k] = temp_surface;
    ZSDL_FreeSurface(temp_surface);

    // The original Light Tank only has four unique hull angles:
    // 000, 045, 090, 315. Opposite angles reuse the matching original
    // sprite with the movement-frame order reversed (2-k).
    const int body_directions[4] = {0, 1, 2, 7};
    for(int d=0; d<4; ++d)
    {
        const int body_dir = body_directions[d];
        const int opposite_dir = (body_dir == 7) ? 3 : body_dir + 4;

        for(k=0; k<3; ++k)
        {
            char neutral_filename[500];
            char mask_filename[500];
            char damaged_filename[500];
            sprintf(neutral_filename,
                "assets/units/vehicles/light/base_light_r%03d_%02d.png",
                ROTATION[body_dir], k+1);
            sprintf(mask_filename,
                "assets/units/vehicles/light/base_light_r%03d_%02d_tintmask.png",
                ROTATION[body_dir], k+1);
            sprintf(damaged_filename,
                "assets/units/vehicles/light/base_damaged_light_r%03d_%02d.png",
                ROTATION[body_dir], k+1);

            // Normal hull: neutral sprite + its existing separate color mask.
            ZSDL_Surface neutral;
            ZSDL_Surface tintmask;
            neutral.LoadBaseImage(neutral_filename);
            tintmask.LoadBaseImage(mask_filename);
            for(i=1; i<MAX_TEAM_TYPES; ++i)
            {
                SDL_Surface *generated = ZTeam::MakeNeutralTint(
                    i, neutral.GetBaseSurface(), tintmask.GetBaseSurface());
                if(generated)
                    base[i][body_dir][k].LoadBaseImage(generated);
                else
                    base[i][body_dir][k].LoadBaseImage(neutral_filename);

                base[i][opposite_dir][2-k] = base[i][body_dir][k];
            }

            // Damaged hulls have NO tintmasks in the supplied sprite pack.
            // Use the same neutral damaged drawing for every owning team,
            // without making four duplicate PNG files.
            base_damaged[RED_TEAM][body_dir][k].LoadBaseImage(damaged_filename);
            for(i=1; i<MAX_TEAM_TYPES; ++i)
            {
                if(i != RED_TEAM)
                    base_damaged[i][body_dir][k] = base_damaged[RED_TEAM][body_dir][k];
                base_damaged[i][opposite_dir][2-k] = base_damaged[i][body_dir][k];
            }
        }
    }

    // The existing turret sprites stay neutral and rotate independently.
    // No top tintmask files were supplied or introduced.
    for(j=0; j<MAX_ANGLE_TYPES; ++j)
    {
        sprintf(filename_c,
            "assets/units/vehicles/light/top_r%03d.png", ROTATION[j]);
        top[j].LoadBaseImage(filename_c);
    }
}

// SDL_Surface *VLight::GetRender()
// {
// 	return base[owner][direction][base_i];
// }

int VLight::Process()
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

void VLight::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
        int &x = loc.x;
        int &y = loc.y;
        const int turrent_x[8] = {2, 0, -2, 0, 2, 0, -2, 0};
        const int turrent_y[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        const int turrent_shift_x[8] = {0, 0, 0, -1, 0, 0, 0, 1};
        const int turrent_shift_y[8] = {-2, -2, -1, 0, 0, 0, 1, -2};
        const int lid_shift_x[8] = {11, 11, 12, 12, 12, 12, 12, 11};
        const int lid_shift_y[8] = {3, 4, 5, 4, 3, 3, 4, 3};
        int damaged_shift_y = 0;
        int damaged_shift_x = 0;
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
        {
                //for some reason the damaged version of this image is pushed around
                base_surface = &base_damaged[owner][direction][move_i];

                switch(direction)
                {
                case 2:
                case 6:
                        damaged_shift_x = 1;
                        damaged_shift_y = 3;
                        break;
                case 1:
                case 5:
                        damaged_shift_x = 2;
                        break;
                }
        }

        render_x = x + damaged_shift_x;
        render_y = y + damaged_shift_y;
        AdjustRenderPositionForScale(base_surface, render_x, render_y);
        the_map.RenderZSurface(base_surface, render_x, render_y, do_hit_effect);

        if(owner != NULL_TEAM)
        {
                lx = x + turrent_x[direction] + turrent_shift_x[t_direction];
                ly = y + turrent_y[direction] + turrent_shift_y[t_direction];

                render_x = lx;
                render_y = ly;
                AdjustRenderPositionForScale(&top[t_direction], render_x, render_y);
                the_map.RenderZSurface(&top[t_direction], render_x, render_y, do_hit_effect);

                lx = x + turrent_x[direction] + lid_shift_x[t_direction];
                ly = y + turrent_y[direction] + lid_shift_y[t_direction];

                RenderLid(the_map, dest, lx, ly, shift_x, shift_y);
        }

        do_hit_effect = false;
}

void VLight::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	//int &x = loc.x;
	//int &y = loc.y;
	//SDL_Rect from_rect, to_rect;
	//int lx, ly;
}

void VLight::FireMissile(int x_, int y_)
{
	int &x = loc.x;
	int &y = loc.y;

	const int sx[8] = {20, 12, 0, -12, -20, -12, 0, 12};
	const int sy[8] = {0, -12, -20, -12, 0, 12, 20, 12};

	//effect_list->push_back((ZEffect*)(new ELightRocket(x+17+sx[t_direction], y+14+sy[t_direction], x_, y_)));
	if(effect_list) effect_list->push_back((ZEffect*)(new ELightRocket(ztime, x+17+sx[t_direction], y+14+sy[t_direction], x_, y_, missile_speed)));
	ZSoundEngine::PlayWavRestricted(LIGHT_FIRE_SND, loc.x, loc.y, width_pix, height_pix);
}

void VLight::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{
	if(owner == NULL_TEAM) return;

	if(effect_list) effect_list->insert(effect_list->begin(), (ZEffect*)(new EDeath(ztime, loc.x, loc.y, EDEATH_TANK, &base_damaged[owner][direction][move_i])));
	//effect_list->push_back((ZEffect*)(new ERobotDeath(loc.x, loc.y, owner)));
}

//bool VLight::ServerFireTurrentMissile(int &x_, int &y_, int &damage, int &radius, double &offset_time)
//{
//	x_ = (loc.x + 16) + (30 - (rand() % 60));
//	y_ = (loc.y + 16) + (30 - (rand() % 60));
//	damage = 40;
//	radius = 40;
//	offset_time = 3 + (0.01 * (rand() % 100));
//	return true;
//}

vector<fire_missile_info> VLight::ServerFireTurrentMissile(int &damage, int &radius)
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

void VLight::FireTurrentMissile(int x_, int y_, double offset_time)
{
	if(effect_list) effect_list->push_back((ZEffect*)(new ETurrentMissile(ztime, loc.x + 8, loc.y + 8, x_, y_, offset_time, ETURRENTMISSILE_LIGHT)));	
}
