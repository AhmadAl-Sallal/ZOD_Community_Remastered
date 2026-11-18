#include "zcannon.h"

ZSDL_Surface ZCannon::init_place[3];

ZCannon::ZCannon(ZTime *ztime_, ZSettings *zsettings_) : ZObject(ztime_, zsettings_)
{
	object_type = CANNON_OBJECT;
	selectable = true;
	can_be_sniped = true;
	ejectable = true;

	width = 2;
	height = 2;
	width_pix = 32;
	height_pix = 32;
}

void ZCannon::Init()
{
	int i;
	char filename_c[500];
	
	//load colors
	for(i=0;i<3;i++)
	{
		sprintf(filename_c, "assets/units/cannons/init-place_n%02d.png", i);
		init_place[i].LoadBaseImage(filename_c);// = IMG_Load_Error(filename_c);
	}
}

void ZCannon::AdjustRenderPositionForScale(ZSDL_Surface *surface, int &x, int &y)
{
        if(!surface) return;

        // Shared placement images are not enlarged.
        if(surface == &init_place[0] ||
           surface == &init_place[1] ||
           surface == &init_place[2])
                return;

        SDL_Surface *image_surface = surface->GetBaseSurface();
        if(!image_surface) return;

        // Calculate the original dimensions using the 1.5x scale.
        const int original_w = (2 * image_surface->w + 1) / 3;
        const int original_h = (2 * image_surface->h + 1) / 3;

        // Keep the original image center aligned.
        x -= (image_surface->w - original_w + 1) / 2;
        y -= (image_surface->h - original_h + 1) / 2;
}
void ZCannon::SetAttackObject(ZObject *obj)
{
	attack_object = obj;

	if(attack_object)
	{
		int tx, ty;
		int new_dir;

		attack_object->GetCenterCords(tx, ty);
		new_dir = DirectionFromLoc(tx - loc.x, ty - loc.y);
		if(new_dir != -1) direction = new_dir;
	}
}

bool ZCannon::CanBeSniped()
{
	return can_be_sniped && driver_info.size() && ejectable;
}

bool ZCannon::CanEjectDrivers()
{
	//return true;
	return ejectable;
}

void ZCannon::SetInitialDrivers()
{
	if(owner != NULL_TEAM)
	{
		driver_type = GRUNT;
		//AddDriver(GRUNT_MAX_HEALTH);
		AddDriver(zsettings->GetUnitSettings(ROBOT_OBJECT, GRUNT).health * MAX_UNIT_HEALTH);
	}
	else
	{
		driver_type = GRUNT;
		ClearDrivers();
	}
}

void ZCannon::SetEjectableCannon(bool ejectable_)
{
	ejectable = ejectable_;
}
