#include "zobject.h"
#include "common.h"
#include "zfont_engine.h"
#include "zod_cannon_geometry.h"
#include "zod_robot_geometry.h"
#include "zod_collision_recovery.h"
#include <map>
#include <algorithm>
#include <cmath>


using namespace COMMON;

// Fase 6H: an explicit attack on a building must not freeze at the starting
// position just because every valid firing slot is already reserved, or the
// chosen firing slot repeatedly produces an empty A* result.
// Reversible A/B switch. No changes to A*, ScuffleUnits or unit hitboxes.
static const bool zod_attack_staging_enabled = true;

// A* results arrive asynchronously. Remember ONLY the failed explicit attack
// (not a failed ordinary move/repair) until its next normal retry. External
// state keeps ZObject/ZServer layouts and multiplayer packet structures intact.
struct ZODAttackRouteFailure { int target_ref; double when; };
static std::map<int, ZODAttackRouteFailure> zod_attack_route_failure;
static void ZODRememberFailedAttackRoute(int unit_id, int target_ref,
    double game_time)
{
    ZODAttackRouteFailure failure = {target_ref, game_time};
    zod_attack_route_failure[unit_id] = failure;
    // Do not accumulate stale failures for destroyed units in long matches.
    if(zod_attack_route_failure.size() > 512)
        for(std::map<int, ZODAttackRouteFailure>::iterator it =
            zod_attack_route_failure.begin(); it != zod_attack_route_failure.end(); )
        {
            if(game_time - it->second.when > 10.0)
                zod_attack_route_failure.erase(it++); // C++98-compatible
            else
                ++it;
        }
}


// Fase 6I-B (A/B switch): distribute VEHICLE A* travel lanes in wide,
// obstacle-free sections. This never changes A*, the destination, other units,
// collision boxes, special Repair/Crane/Fort paths or the transport helper.
// Set false to compare against the verified Fase 6H + 6I-A baseline.
static const bool zod_wide_route_lanes_enabled = true;

// Check the ENTIRE detour with the same real body rectangle as ProcessMove().
// A per-pixel sweep matches the actual body collision test in ProcessMove().
// If a wall, corner or narrow corridor is anywhere on the detour, we keep
// the original, proven A* path exactly as returned.
static bool ZODWideLaneSegmentClear(ZMap &tmap, int ax, int ay, int bx, int by,
    int width_pix, int height_pix)
{
    if(width_pix <= 2 || height_pix <= 2) return false;
    const int dx=bx-ax, dy=by-ay;
    const int distance=(abs(dx)>abs(dy)) ? abs(dx) : abs(dy);
    const int steps=distance;
    int stop_x=0, stop_y=0;
    for(int i=0; i<=steps; ++i)
    {
        const int cx=steps ? ax+(dx*i)/steps : ax;
        const int cy=steps ? ay+(dy*i)/steps : ay;
        if(tmap.WithinImpassable(cx-(width_pix>>1)+1,
            cy-(height_pix>>1)+1, width_pix-2, height_pix-2,
            stop_x, stop_y, false)) return false;
    }
    return true;
}

// Existing wide-lane spreading may move a tank off a road by 32-64px.
// Leave the scattering rules intact on open land; only reject a lane that
// would lose the time advantage of a road segment. Still allow multiple
// lanes on broad roads and keep the existing full-body collision checks.
static double ZODLaneTravelCost(ZMap &tmap, int ax, int ay, int bx, int by,
    bool &uses_road)
{
    const double dx=(double)bx-ax, dy=(double)by-ay;
    const double length=sqrt(dx*dx+dy*dy);
    if(length<=0.0) return 0.0;
    const int samples=(int)ceil(length/4.0);
    double sum=0.0;
    for(int i=0;i<samples;++i)
    {
        const double t=(i+0.5)/samples;
        const double speed=tmap.GetTileWalkSpeed((int)floor(ax+dx*t),(int)floor(ay+dy*t));
        if(speed>1.0) uses_road=true;
        sum += speed>0.0 ? 1.0/speed : 1.0;
    }
    return length*sum/samples;
}

static bool ZODRoadLaneAcceptable(ZMap &tmap,
    const ZPath_Finding_AStar::pf_point *before, int before_size,
    const ZPath_Finding_AStar::pf_point *after, int after_size)
{
    bool road=false;
    double old_cost=0.0;
    for(int i=1;i<before_size;++i)
        old_cost+=ZODLaneTravelCost(tmap,before[i-1].x,before[i-1].y,before[i].x,before[i].y,road);
    if(!road) return true;
    double new_cost=0.0;
    for(int i=1;i<after_size;++i)
        new_cost+=ZODLaneTravelCost(tmap,after[i-1].x,after[i-1].y,after[i].x,after[i].y,road);
    return new_cost<=old_cost*1.05;
}

// Post-process only the travel segments from an accepted A* route. A group
// receives a stable per-vehicle lane (-64/-32/0/+32/+64px), so it does not
// zigzag randomly when successive waypoints arrive. Two intermediate nodes
// create a parallel middle stretch, then safely rejoin the original route.
// The original final endpoint is NEVER changed; narrow sections stay single-file.
// The change is entirely local to this response's list of path points.
static int ZODSpreadVehicleRoute(ZMap &tmap,
    vector<ZPath_Finding_AStar::pf_point> &route,
    int unit_ref_id, int width_pix, int height_pix)
{
    if(!zod_wide_route_lanes_enabled || unit_ref_id < 0 ||
       route.size() < 2 || width_pix <= 2 || height_pix <= 2) return 0;

    const int lane = (unit_ref_id % 5) - 2;
    if(!lane) return 0; // center lane retains the unmodified A* route
    const int side = lane < 0 ? -1 : 1;
    const int requested_offset = abs(lane) * 32;

    // Fase 6Q: the old lane spreading only handled straight segments of
    // 150px or more. ALL vehicles still passed through the exact same corner
    // node, recreating a single-file queue every time the path turned.
    // Offset a CORNER only when both adjacent full-body travel segments are
    // physically clear. Narrow bends retain the original A* node untouched.
    vector<ZPath_Finding_AStar::pf_point> corners = route;
    int spread_corners = 0;
    for(size_t i=1; i+1<corners.size() && spread_corners<6; ++i)
    {
        const ZPath_Finding_AStar::pf_point &a=corners[i-1];
        const ZPath_Finding_AStar::pf_point &b=corners[i];
        const ZPath_Finding_AStar::pf_point &c=corners[i+1];
        const double in_x=b.x-a.x, in_y=b.y-a.y;
        const double out_x=c.x-b.x, out_y=c.y-b.y;
        const double in_len=sqrt(in_x*in_x+in_y*in_y);
        const double out_len=sqrt(out_x*out_x+out_y*out_y);
        if(in_len<64.0 || out_len<64.0) continue;
        const double ix=in_x/in_len, iy=in_y/in_len;
        const double ox=out_x/out_len, oy=out_y/out_len;
        const double cross=ix*oy-iy*ox;
        const double dot=ix*ox+iy*oy;
        // Ignore tiny course adjustments and near-U-turns.
        if(fabs(cross)<0.25 || dot< -0.70) continue;
        double nx=-iy-oy, ny=ix+ox;
        const double normal_len=sqrt(nx*nx+ny*ny);
        if(normal_len<0.25) continue;
        nx/=normal_len;
        ny/=normal_len;

        // Keep the outer bend lane gentler than the open straight lanes.
        const int corner_offset = requested_offset > 48 ? 48 : requested_offset;
        for(int offset=corner_offset; offset>=16; offset-=16)
        {
            if(in_len<2.0*offset || out_len<2.0*offset) continue;
            const int px=b.x+(int)floor(side*nx*offset+0.5);
            const int py=b.y+(int)floor(side*ny*offset+0.5);
            if(!ZODWideLaneSegmentClear(tmap,a.x,a.y,px,py,width_pix,height_pix) ||
               !ZODWideLaneSegmentClear(tmap,px,py,c.x,c.y,width_pix,height_pix))
                continue;
            const ZPath_Finding_AStar::pf_point before[] = {a,b,c};
            const ZPath_Finding_AStar::pf_point after[] = {a,ZPath_Finding_AStar::pf_point(px,py),c};
            if(!ZODRoadLaneAcceptable(tmap,before,3,after,3)) continue;
            corners[i]=ZPath_Finding_AStar::pf_point(px,py);
            ++spread_corners;
            break;
        }
    }

    vector<ZPath_Finding_AStar::pf_point> spread;
    spread.reserve(corners.size() + 12);
    spread.push_back(corners.front());
    int added_segments = 0;
    for(size_t i=1; i<corners.size(); ++i)
    {
        const ZPath_Finding_AStar::pf_point &a=corners[i-1];
        const ZPath_Finding_AStar::pf_point &b=corners[i];
        const double dx=(double)b.x-a.x, dy=(double)b.y-a.y;
        const double len=sqrt(dx*dx+dy*dy);

        // One long, open stretch is required. Do not introduce extra bends
        // in a bottleneck or turn every tiny A* segment into a detour.
        if(added_segments < 4 && len >= 150.0 && len <= 1400.0)
        {
            for(int offset=requested_offset; offset>=32; offset-=32)
            {
                // Keep the dogleg shallow enough not to create new queues.
                if(len < (double)(3*offset+60)) continue;
                const double transverse=(double)(side*offset)/len;
                const int x1=(int)floor(a.x + dx*0.25 - dy*transverse + 0.5);
                const int y1=(int)floor(a.y + dy*0.25 + dx*transverse + 0.5);
                const int x2=(int)floor(a.x + dx*0.75 - dy*transverse + 0.5);
                const int y2=(int)floor(a.y + dy*0.75 + dx*transverse + 0.5);

                if(!ZODWideLaneSegmentClear(tmap,a.x,a.y,x1,y1,width_pix,height_pix) ||
                   !ZODWideLaneSegmentClear(tmap,x1,y1,x2,y2,width_pix,height_pix) ||
                   !ZODWideLaneSegmentClear(tmap,x2,y2,b.x,b.y,width_pix,height_pix))
                    continue;

                const ZPath_Finding_AStar::pf_point before[] = {a,b};
                const ZPath_Finding_AStar::pf_point after[] = {a,ZPath_Finding_AStar::pf_point(x1,y1),ZPath_Finding_AStar::pf_point(x2,y2),b};
                if(!ZODRoadLaneAcceptable(tmap,before,2,after,4)) continue;
                spread.push_back(ZPath_Finding_AStar::pf_point(x1,y1));
                spread.push_back(ZPath_Finding_AStar::pf_point(x2,y2));
                ++added_segments;
                break;
            }
        }
        spread.push_back(b);
    }
    if(added_segments || spread_corners) route.swap(spread);
    return added_segments + spread_corners;
}

// Repair-only approach check: the generic A* footprint/direct-line tests can
// reject a vehicle already standing safely in front of the repair entrance.
// Use exactly the same physical rectangle as ZObject::ProcessMove(), checking
// every intermediate pixel, before allowing the existing repair waypoint to
// make its final short approach. Never cross an impassable tile.
// No global pathfinding, building geometry or unit collision settings change.
static bool RepairEntranceClearFromCurrentPosition(ZMap &tmap,
    int loc_x, int loc_y, int center_x, int center_y,
    int entrance_x, int entrance_y, int width_pix, int height_pix,
    bool is_robot)
{
    if(width_pix <= 2 || height_pix <= 2) return false;

    const int dx = entrance_x - center_x;
    const int dy = entrance_y - center_y;
    const int distance = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);

    // Already at the exact existing doorway: allow the original WAIT/ENTER
    // repair stages to run even if a nearby wall fools the route precheck.
    if(abs(dx) <= 4 && abs(dy) <= 4) return true;

    // Only a LOCAL repair entrance approach may bypass asynchronous A*.
    // Distant paths, including ones around walls, still require A*.
    if(distance > 96) return false;

    int stop_x = 0, stop_y = 0;
    for(int i = 0; i <= distance; ++i)
    {
        const int px = loc_x + (dx * i) / distance;
        const int py = loc_y + (dy * i) / distance;
        if(tmap.WithinImpassable(px + 1, py + 1,
             width_pix - 2, height_pix - 2, stop_x, stop_y, is_robot))
            return false;
    }
    return true;
}


// Select a passable A* approach that can safely finish at the EXISTING
// Repair doorway. The old hardcoded 'doorway + 16px south' target can land
// on a blocked tile (confirmed by the player's real-map repair debug log).
// The candidate itself, the A* last tile centre and the last leg are all
// checked against the actual unit footprint; no global geometry is changed.
static bool RepairStagingPointSafe(ZMap &tmap, int staging_x, int staging_y,
    int doorway_x, int doorway_y, int width_pix, int height_pix, bool is_robot)
{
    const int tx = staging_x >> 4;
    const int ty = staging_y >> 4;
    ZPath_Finding_Engine &pf = tmap.GetPathFinder();
    if(!pf.TilePassable(tx, ty, is_robot)) return false;
    if(!is_robot &&
        (!pf.TilePassable(tx+1, ty, false) ||
         !pf.TilePassable(tx, ty+1, false) ||
         !pf.TilePassable(tx+1, ty+1, false))) return false;

    // The last A* node is rendered at tile*16+16 for vehicles, +8 for robots;
    // the requested final pixel may differ by a few pixels. Check both.
    const int tile_center_offset = is_robot ? 8 : 16;
    const int path_final_x = (tx << 4) + tile_center_offset;
    const int path_final_y = (ty << 4) + tile_center_offset;
    if(!RepairEntranceClearFromCurrentPosition(tmap,
        path_final_x - (width_pix >> 1), path_final_y - (height_pix >> 1),
        path_final_x, path_final_y, staging_x, staging_y,
        width_pix, height_pix, is_robot)) return false;

    return RepairEntranceClearFromCurrentPosition(tmap,
        staging_x - (width_pix >> 1), staging_y - (height_pix >> 1),
        staging_x, staging_y, doorway_x, doorway_y,
        width_pix, height_pix, is_robot);
}

static bool RepairChooseStagingPoint(ZMap &tmap,
    int doorway_x, int doorway_y, int width_pix, int height_pix, bool is_robot,
    int &staging_x, int &staging_y)
{
    // Prefer the open approach immediately ABOVE the existing doorway, not
    // the assumed-open ground BELOW it. The -17 avoids a tile-boundary
    // mismatch: A* floors requested coordinates to 16px tiles.
    static const int offsets[][2] = {
        { 0,-17}, { 0,-33}, {-16,-17}, {16,-17},
        {-16,-33}, {16,-33}, { 0,-1}, {-16,-1}, {16,-1},
        {-32,-17}, {32,-17}, {-32,-33}, {32,-33},
        { 0,16}, {-16,16}, {16,16}
    };
    for(unsigned int i=0; i<sizeof(offsets)/sizeof(offsets[0]); ++i)
    {
        const int cx = doorway_x + offsets[i][0];
        const int cy = doorway_y + offsets[i][1];
        if(RepairStagingPointSafe(tmap, cx, cy, doorway_x, doorway_y,
            width_pix, height_pix, is_robot))
        {
            staging_x = cx;
            staging_y = cy;
            return true;
        }
    }
    return false;
}

// Crane work duration is independent of building size and driving distance.
// Adjust these values to balance each repair type without touching geometry.
static double CraneRepairWorkSeconds(unsigned char building_id)
{
    switch(building_id)
    {
    case RADAR:           return 3.0;
    case REPAIR:          return 4.0;
    case ROBOT_FACTORY:   return 4.5;
    case VEHICLE_FACTORY: return 5.0;
    case BRIDGE_VERT:
    case BRIDGE_HORZ:     return 5.0;
    case FORT_FRONT:
    case FORT_BACK:       return 6.0;
    default:              return 4.0;
    }
}

ZSettings ZObject::default_zsettings;

ZSDL_Surface ZObject::group_tag[10];

//vector<ZEffect*> *ZObject::effect_list = NULL;
vector<damage_missile> *ZObject::damage_missile_list = NULL;

ZObject::ZObject(ZTime *ztime_, ZSettings *zsettings_)
{
	if(!zsettings_)
		zsettings = &default_zsettings;
	else
		zsettings = zsettings_;

	ztime = ztime_;

	zmap = NULL;
	effect_list = NULL;

	double &the_time = ztime->ztime;

	object_name = "object";
	owner = NULL_TEAM;
	last_process_time = the_time;
	width = 0;
	height = 0;
	width_pix = 0;
	height_pix = 0;
	mode = NULL_MODE;
	destroyed = false;
	direction = 4;
	selectable = false;
	ref_id = -1;
	waypoint_i = 0;
	attack_i = 0;
	next_waypoint_time = 0;
    next_path_retry_time = 0;
    force_next_full_path = false;
    collision_recovery_active = false;
    collision_recovery_credit = collision_recovery_retry = 0.0;
	render_death_time = 0;
	show_waypoints = false;
	loc.x = loc.y = 0;
	loc.dx = loc.dy = 0;
	center_x = center_y = 0;
	move_speed = 0;
	real_move_speed = 0;
	transport_speed_factor = 1.0;
	parking_valid = false;
	parking_order_x = parking_order_y = 0;
	parking_target_x = parking_target_y = 0;
	next_auto_unstack_time = 0;
	xover = yover = 0;
	last_process_server_time = last_process_time;
	last_radius_time = last_process_time;
	last_loc_set_time = last_process_time;
	waypoint_cursor.SetTeam(NULL_TEAM);
	group_num = -1;
	//hover_name_img = NULL;
	//hover_name_star_img = NULL;
	attack_radius = 0;
	radius_i = 0;
	max_health = 1;
	health = max_health;
	last_wp.mode = -1;
	next_check_passive_attack_time = 0;
	watched_auto_target_ref_id = -1;
	watched_auto_target_x = watched_auto_target_y = 0;
	watched_auto_target_last_progress = 0;
	buildlist = NULL;
	connected_zone = NULL;
	unit_limit_reached = NULL;
	dont_stamp = false;
	do_auto_repair = false;
	can_be_destroyed = true;
	has_explosives = false;
	attacked_by_explosives = false;
	do_hit_effect = false;
	do_driver_hit_effect = false;
	can_snipe = false;
	has_lid = false;
	processed_death = false;
	can_be_sniped = false;
	last_damaged_by_fire_time = 0;
	last_damaged_by_missile_time = 0;
	just_left_cannon = false;
	initial_health_percent = 100;
	next_drop_track_time = 0;
	max_stamina = 0;
	stamina = max_stamina;

	driver_type = GRUNT;

	//to make sure loc updates from the server are not over frequent
	loc_update_int = 0.8 + (0.001 * (rand() % 25));
	next_loc_update_time = 0;

	//attack stuff
	attack_object = NULL;
	next_damage_time = 0;
	damage_int_time = 0;
	damage_chance = 0;
	damage = 0;
	damage_is_missile = false;
	damage_radius = 1;
	snipe_chance = 0;

	//needed for when an object needs to be removed after some time
	killme = false;
	killme_time = 0;

	ClearGroupInfo();
}

ZObject::~ZObject()
{
	//kill any pathfinding threads
	if(zmap) cur_wp_info.clear_and_kill(*zmap);

	//alittle mem mangement
	//if(hover_name_img) SDL_FreeSurface(hover_name_img);
	//if(hover_name_star_img) SDL_FreeSurface(hover_name_star_img);
}

void ZObject::InitTypeId(unsigned char ot, unsigned char oid)
{
	object_type = ot;
	object_id = oid;

	if(!zsettings)
	{
		printf("ZObject::InitTypeId:zsettings not set\n");
		return;
	}

	if(ot == BUILDING_OBJECT)
	{
		switch(oid)
		{
		case FORT_FRONT:
		case FORT_BACK:
			max_health = zsettings->fort_building_health * MAX_UNIT_HEALTH;
			break;
		case RADAR:
			max_health = zsettings->radar_building_health * MAX_UNIT_HEALTH;
			break;
		case REPAIR:
			max_health = zsettings->repair_building_health * MAX_UNIT_HEALTH;
			break;
		case ROBOT_FACTORY:
			max_health = zsettings->robot_building_health * MAX_UNIT_HEALTH;
			break;
		case VEHICLE_FACTORY:
			max_health = zsettings->vehicle_building_health * MAX_UNIT_HEALTH;
			break;
		case BRIDGE_VERT:
		case BRIDGE_HORZ:
			max_health = zsettings->bridge_building_health * MAX_UNIT_HEALTH;
			break;
		}
	}
	else if (ot == MAP_ITEM_OBJECT)
	{
		switch(oid)
		{
		case ROCK_ITEM:
			max_health = zsettings->rock_item_health * MAX_UNIT_HEALTH;
			break;
		case GRENADES_ITEM:
			max_health = zsettings->grenades_item_health * MAX_UNIT_HEALTH;
			break;
		case ROCKETS_ITEM:
			max_health = zsettings->rockets_item_health * MAX_UNIT_HEALTH;
			break;
		case HUT_ITEM:
			max_health = zsettings->hut_item_health * MAX_UNIT_HEALTH;
			break;
		}

		if(oid >= MAP0_ITEM && oid < MAP0_ITEM + MAP_ITEMS_AMOUNT)
			max_health = zsettings->map_item_health * MAX_UNIT_HEALTH;
	}
	else
	{
		ZUnit_Settings &unit_settings = zsettings->GetUnitSettings(object_type, object_id);

		move_speed = unit_settings.move_speed;
		attack_radius = unit_settings.attack_radius;
		damage = unit_settings.attack_damage * MAX_UNIT_HEALTH;
		damage_chance = unit_settings.attack_damage_chance;
		damage_radius = unit_settings.attack_damage_radius;
		missile_speed = unit_settings.attack_missile_speed;
		snipe_chance = unit_settings.attack_snipe_chance;
		damage_int_time = unit_settings.attack_speed;
		max_health = unit_settings.health * MAX_UNIT_HEALTH;
		max_stamina = unit_settings.max_run_time;
	}

	//set health and stamina to full
	health = max_health;
	stamina = max_stamina;
}

void ZObject::Init(TTF_Font *ttf_font)
{
	int i;
	char filename_c[500];
	SDL_Color textcolor;

	for(i=0;i<10;i++)
	{
		textcolor.r = 200;
		textcolor.g = 200;
		textcolor.b = 200;

		sprintf(filename_c, "%d", i);
		//group_tag[i] = TTF_RenderText_Solid(ttf_font, filename_c, textcolor);
		group_tag[i].LoadBaseImage(TTF_RenderText_Solid(ttf_font, filename_c, textcolor));
	}

	//for(i=0;i<6;i++)
	//{
	//	sprintf(filename_c, "assets/sounds/selected_%02d.wav", i);
	//	selected_wav[i] = MIX_Load_Error(filename_c);
	//}

	//for(i=0;i<MAX_ROBOT_TYPES;i++)
	//{
	//	sprintf(filename_c, "assets/sounds/selected_%s.wav", robot_type_string[i].c_str());
	//	selected_robot_wav[i] = MIX_Load_Error(filename_c);
	//}

	//for(i=0;i<12;i++)
	//{
	//	sprintf(filename_c, "assets/sounds/acknowledge_%02d.wav", i);
	//	acknowledge_wav[i] = MIX_Load_Error(filename_c);
	//}
}

void ZObject::InitRealMoveSpeed(ZMap &tmap)
{
	int &x = loc.x;
	int &y = loc.y;

	//real_move_speed = move_speed * tmap.GetTileWalkSpeed(x + (width_pix >> 1), y + (height_pix >> 1));
	real_move_speed = move_speed * tmap.GetTileWalkSpeed(center_x, center_y);
}

void ZObject::SetMap(ZMap *zmap_)
{
	zmap = zmap_;
}

void ZObject::DontStamp(bool dont_stamp_)
{
	dont_stamp = dont_stamp_;
}

void ZObject::DoDeathEffect(bool do_fire_death, bool do_missile_death)
{

}

void ZObject::DoReviveEffect()
{

}

void ZObject::FireMissile(int x_, int y_)
{

}

bool ZObject::KillMe(double the_time)
{
	return (killme && the_time >= killme_time);
}

void ZObject::DoKillMe(double killtime)
{
	//they were already set to die
	//make sure they die at their original time
	if(killme) return;

	killme = true;
	killme_time = killtime;
}

bool ZObject::IsDestroyed()
{
	return (health <= 0 && max_health > 0);
}

int ZObject::GetHealth()
{
	return health;
}

int ZObject::GetMaxHealth()
{
	return max_health;
}

void ZObject::SetHealthPercent(int health_percent, ZMap &tmap)
{
	if(health_percent > 100) health_percent = 100;
	if(health_percent < 0) health_percent = 0;

	//for the map editor
	initial_health_percent = health_percent;

	SetHealth(health_percent * max_health / 100, tmap);
}

void ZObject::SetHealth(int new_health, ZMap &tmap)
{
	bool was_destroyed;

	was_destroyed = IsDestroyed();

	health = new_health;

	if(health < 0) health = 0;
	if(health > max_health) health = max_health;

	if(was_destroyed && !IsDestroyed())
	{
		DoReviveEffect();
		UnSetDestroyMapImpassables(tmap);
	}
	else if(!was_destroyed && IsDestroyed())
	{
		SetDestroyMapImpassables(tmap);
		ProcessKillObject();
	}

	//if(!health) DoDeathEffect();
}

int ZObject::DamageDriverHealth(int damage_amount)
{
	if(!driver_info.size()) return 0;

	driver_info_s &the_driver = *driver_info.begin();

	if(the_driver.driver_health <= 0) return 0;

	the_driver.driver_health -= damage_amount;

	if(the_driver.driver_health <= 0)
	{
		//we killed the driver

		//clear drivers
		ClearDrivers();

		//go to NULL_TEAM
		SetOwner(NULL_TEAM);

		//disengage, clear waypoints, and stop movement
		//done at the server level
	}
	
	return 1;
}

int ZObject::DamageHealth(int damage_amount, ZMap &tmap)
{
	//already dead foo
	if(health <= 0) return health;

	SetHealth(health - damage_amount, tmap);

	//health -= damage_amount;

	//if(health <= 0)
	//{
	//	health = 0;
	//	ProcessKillObject(current_time());
	//}
	//if(health > max_health) health = max_health;

	//got to recalc the build time if this is a building
	RecalcBuildTime();

	return health;
}

void ZObject::SetGroup(int group_num_)
{
	group_num = group_num_;
}

void ZObject::PlaySelectedWav()
{
	//int ch;

	//ch = rand() % 6;

	//ZMix_PlayChannel(-1, selected_wav[ch], 0);
}

void ZObject::PlaySelectedAnim(ZPortrait &portrait)
{
	switch(rand() % 4)
	{
	case 0: portrait.StartAnim(YES_SIR_ANIM); break;
	case 1: portrait.StartAnim(YES_SIR3_ANIM); break;
	case 2: portrait.StartAnim(UNIT_REPORTING1_ANIM); break;
	case 3: portrait.StartAnim(UNIT_REPORTING2_ANIM); break;
	}
}

void ZObject::PlayAcknowledgeWav()
{
	//int ch;

	//ch = rand() % 12;

	//ZMix_PlayChannel(-1, acknowledge_wav[ch], 0);
}

void ZObject::PlayAcknowledgeAnim(ZPortrait &portrait, bool no_way)
{
	if(no_way)
	{
		switch(rand() % 3)
		{
		case 0: portrait.StartAnim(FORGET_IT_ANIM); break;
		case 1: portrait.StartAnim(GET_OUTTA_HERE_ANIM); break;
		case 2: portrait.StartAnim(NO_WAY_ANIM); break;
		}
	}
	else
	{
		switch(rand() % 12)
		{
		case 0: portrait.StartAnim(WERE_ON_OUR_WAY_ANIM); break;
		case 1: portrait.StartAnim(HERE_WE_GO_ANIM); break;
		case 2: portrait.StartAnim(YOUVE_GOT_IT_ANIM); break;
		case 3: portrait.StartAnim(MOVING_IN_ANIM); break;
		case 4: portrait.StartAnim(OKAY_ANIM); break;
		case 5: portrait.StartAnim(ALRIGHT_ANIM); break;
		case 6: portrait.StartAnim(NO_PROBLEM_ANIM); break;
		case 7: portrait.StartAnim(OVER_N_OUT_ANIM); break;
		case 8: portrait.StartAnim(AFFIRMATIVE_ANIM); break;
		case 9: portrait.StartAnim(GOING_IN_ANIM); break;
		case 10: portrait.StartAnim(LETS_DO_IT_ANIM); break;
		case 11: portrait.StartAnim(LETS_GET_EM_ANIM); break;
		}
	}
}

vector<waypoint> &ZObject::GetWayPointList()
{
	return waypoint_list;
}

vector<waypoint> &ZObject::GetWayPointDevList()
{
	return waypoint_dev_list;
}

void ZObject::SetRefID(int id)
{
	ref_id = id;
}

int ZObject::GetRefID()
{
	return ref_id;
}

ZSDL_Surface &ZObject::GetHoverNameImg()
{
	if(hover_name_img.GetBaseSurface()) return hover_name_img;

	//make it?
	if(hover_name.size())
	{
		hover_name_img.Unload();
		hover_name_star_img.Unload();

		hover_name_img.LoadBaseImage(ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(hover_name.c_str()));
		hover_name_star_img.LoadBaseImage(ZFontEngine::GetFont(SMALL_WHITE_FONT).Render((hover_name + " *").c_str()));

		if(hover_name_img.GetBaseSurface())
			hover_name_x_shift = (width_pix - hover_name_img.GetBaseSurface()->w) >> 1;

		if(hover_name_star_img.GetBaseSurface())
			hover_name_star_x_shift = (width_pix - hover_name_star_img.GetBaseSurface()->w) >> 1;
	}

	return hover_name_img;
}

ZSDL_Surface &ZObject::GetHoverNameImgStatic(unsigned char ot, unsigned char oid)
{
	const int max_units_in_type = 7;
	static ZSDL_Surface static_hover_name_img[MAX_MAP_OBJECT_TYPES][max_units_in_type];
	static ZSDL_Surface null_return;

	if(ot < 0) return null_return;
	if(ot >= MAX_MAP_OBJECT_TYPES) return null_return;
	if(oid < 0) return null_return;
	if(oid >= max_units_in_type) return null_return;

	if(!static_hover_name_img[ot][oid].GetBaseSurface())
	{
		string render_str;

		render_str = GetHoverName(ot, oid);
		if(render_str.length())
			static_hover_name_img[ot][oid].LoadBaseImage(ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(render_str.c_str()));
	}

	return static_hover_name_img[ot][oid];
}

string ZObject::GetHoverName(unsigned char ot, unsigned char oid)
{
	switch(ot)
	{
		case CANNON_OBJECT:
			switch(oid)
			{
				case GATLING: return "Gatling"; break;
				case GUN: return "Gun"; break;
				case HOWITZER: return "Howitzer"; break;
				case MISSILE_CANNON: return "Missile"; break;
			}
			break;
		case VEHICLE_OBJECT:
			switch(oid)
			{
				case JEEP: return "Jeep"; break;
				case LIGHT: return "Light"; break;
				case MEDIUM: return "Medium"; break;
				case HEAVY: return "Heavy"; break;
				case APC: return "APC"; break;
				case MISSILE_LAUNCHER: return "M Missile"; break;
				case CRANE: return "Crane"; break;
			}
			break;
		case ROBOT_OBJECT:
			switch(oid)
			{
				case GRUNT: return "Grunt"; break;
				case PSYCHO: return "Psychos"; break;
				case SNIPER: return "Sniper"; break;
				case TOUGH: return "Tough"; break;
				case PYRO: return "Pyros"; break;
				case LASER: return "Laser"; break;
			}
			break;
	}

	return "";
}

void ZObject::RenderHover(ZMap &zmap, SDL_Surface *dest, team_type viewers_team)
{
	int &x = loc.x;
	int &y = loc.y;
	SDL_Rect from_rect, to_rect;
	int hover_name_y_shift = -19;
    int hover_center_shift = 0;
    if(object_type == ROBOT_OBJECT)
    {
        int vx, vy, vw, vh;
        GetVisualBounds(vx, vy, vw, vh);
        hover_name_y_shift = vy - y - 19;
        hover_center_shift = vx + vw/2 - x - width_pix/2;
    }
    if(object_type == CANNON_OBJECT)
    {
        ZODCannonRect rect;
        if(ZODCannonVisualRect(object_id, rect))
        {
            hover_name_y_shift = rect.y - 19;
            hover_center_shift = rect.x + rect.w/2 - width_pix/2;
        }
    }

	if(hover_name_img.GetBaseSurface())
	{
		if(viewers_team == owner && viewers_team != NULL_TEAM)
		{
			zmap.RenderZSurface(&hover_name_star_img, x + hover_name_star_x_shift + hover_center_shift, y + hover_name_y_shift);
			//if(zmap.GetBlitInfo(hover_name_star_img, x + hover_name_star_x_shift + hover_center_shift, y + hover_name_y_shift, from_rect, to_rect))
			//	SDL_BlitSurface( hover_name_star_img, &from_rect, dest, &to_rect);
		}
		else
		{
			zmap.RenderZSurface(&hover_name_star_img, x + hover_name_x_shift + hover_center_shift, y + hover_name_y_shift);
			//if(zmap.GetBlitInfo(hover_name_img, x + hover_name_x_shift + hover_center_shift, y + hover_name_y_shift, from_rect, to_rect))
			//	SDL_BlitSurface( hover_name_img, &from_rect, dest, &to_rect);
		}
	}
	else if(hover_name.size())
	{
		hover_name_img.Unload();
		hover_name_star_img.Unload();

		hover_name_img.LoadBaseImage( ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(hover_name.c_str()));
		hover_name_star_img.LoadBaseImage( ZFontEngine::GetFont(SMALL_WHITE_FONT).Render((hover_name + " *").c_str()));

		if(hover_name_img.GetBaseSurface())
			hover_name_x_shift = (width_pix - hover_name_img.GetBaseSurface()->w) >> 1;

		if(hover_name_star_img.GetBaseSurface())
			hover_name_star_x_shift = (width_pix - hover_name_star_img.GetBaseSurface()->w) >> 1;

		if(viewers_team == owner && viewers_team != NULL_TEAM)
		{
			if(hover_name_star_img.GetBaseSurface())
				zmap.RenderZSurface(&hover_name_star_img, x + hover_name_star_x_shift + hover_center_shift, y + hover_name_y_shift);
			//if(zmap.GetBlitInfo(hover_name_star_img, x + hover_name_star_x_shift + hover_center_shift, y + hover_name_y_shift, from_rect, to_rect))
			//	SDL_BlitSurface( hover_name_star_img, &from_rect, dest, &to_rect);
		}
		else
		{
			if(hover_name_img.GetBaseSurface())
				zmap.RenderZSurface(&hover_name_img, x + hover_name_x_shift + hover_center_shift, y + hover_name_y_shift);
			//if(zmap.GetBlitInfo(hover_name_img, x + hover_name_x_shift + hover_center_shift, y + hover_name_y_shift, from_rect, to_rect))
			//	SDL_BlitSurface( hover_name_img, &from_rect, dest, &to_rect);
		}
	}

	//if you are rendering the hover name, then you are also rendering the health
	RenderHealth(zmap, dest);
}

void ZObject::RenderHealth(ZMap &zmap, SDL_Surface *dest)
{
	int &x = loc.x;
	int &y = loc.y;
	SDL_Rect the_box;
	SDL_Rect from_rect, to_rect;
	//int shift_x, shift_y, view_w, view_h;
	const char g_r = 82, g_g = 190, g_b = 33;
	const char y_r = 247, y_g = 203, y_b = 107;
	const char b_r = 0, b_g = 0, b_b = 0;
	//int green_map = SDL_MapRGB(dest->format, 82, 190, 33);
	//int yellow_map = SDL_MapRGB(dest->format, 247, 203, 107);
	//int black_map = SDL_MapRGB(dest->format, 0, 0, 0);
	int bar_x_shift = -3;
	int bar_y_shift = -8;
	const int max_dist = 30 + 6;
    if(object_type == CANNON_OBJECT)
    {
        ZODCannonRect rect;
        if(ZODCannonVisualRect(object_id, rect))
        {
            bar_x_shift = rect.x + (rect.w - (max_dist + 2))/2;
            bar_y_shift = rect.y - 6;
        }
    }
	int green_dist, yellow_dist, total_dist;


	if(!hover_name.size()) return;

	//calculate
	green_dist = max_dist * (1.0 * health / MAX_UNIT_HEALTH);
	yellow_dist = max_dist * (1.0 * max_health / MAX_UNIT_HEALTH);

	if(green_dist <= 0) green_dist = 1;
	if(yellow_dist <= 0) yellow_dist = 1;
	total_dist = yellow_dist + 2;
    if(object_type == ROBOT_OBJECT)
    {
        int vx, vy, vw, vh;
        GetVisualBounds(vx, vy, vw, vh);
        bar_x_shift = vx - x + (vw - total_dist)/2;
        bar_y_shift = vy - y - 6;
    }

	//zmap.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

	//draw black
	//the_box.x = (x - shift_x) + bar_x_shift;
	//the_box.y = (y - shift_y) + bar_y_shift;
	//the_box.w = total_dist;
	//the_box.h = 4;

	//SDL_FillRect(dest, &the_box, black_map);

	if(zmap.GetBlitInfo(x + bar_x_shift, y + bar_y_shift, total_dist, 4, from_rect, to_rect))
	{
		the_box.x = to_rect.x;
		the_box.y = to_rect.y;
		the_box.w = from_rect.w;
		the_box.h = from_rect.h;

		//SDL_FillRect(dest, &the_box, black_map);
		ZSDL_FillRect(&the_box, b_r, b_g, b_b);
	}

	//draw green
	//the_box.x = (x - shift_x) + bar_x_shift + 1;
	//the_box.y = (y - shift_y) + bar_y_shift + 1;
	//the_box.w = green_dist;
	//the_box.h = 2;

	//SDL_FillRect(dest, &the_box, green_map);

	if(zmap.GetBlitInfo(x + bar_x_shift + 1, y + bar_y_shift + 1, green_dist, 2, from_rect, to_rect))
	{
		the_box.x = to_rect.x;
		the_box.y = to_rect.y;
		the_box.w = from_rect.w;
		the_box.h = from_rect.h;

		//SDL_FillRect(dest, &the_box, green_map);
		ZSDL_FillRect(&the_box, g_r, g_g, g_b);
	}

	//draw yellow
	//the_box.x = (x - shift_x) + bar_x_shift + 1 + green_dist;
	//the_box.y = (y - shift_y) + bar_y_shift + 1;
	//the_box.w = yellow_dist - green_dist;
	//the_box.h = 2;

	//if(the_box.w)
	//	SDL_FillRect(dest, &the_box, yellow_map);

	if(yellow_dist - green_dist)
	if(zmap.GetBlitInfo(x + bar_x_shift + 1 + green_dist, y + bar_y_shift + 1, yellow_dist - green_dist, 2, from_rect, to_rect))
	{
		the_box.x = to_rect.x;
		the_box.y = to_rect.y;
		the_box.w = from_rect.w;
		the_box.h = from_rect.h;

		//SDL_FillRect(dest, &the_box, yellow_map);
		ZSDL_FillRect(&the_box, y_r, y_g, y_b);
	}
}

void ZObject::RenderAttackRadius(ZMap &zmap, SDL_Surface *dest, vector<ZObject*> &avoid_list)
{
	int &x = loc.x;
	int &y = loc.y;
	int cx, cy;
	int mx, my;
	int shift_x, shift_y, view_w, view_h;
	SDL_Rect to_rect;
	//int rgb_map;
	int i;
	const int dots = 10;
	const double PI_shift = (PI / 2) / dots;
	double deg;

	if(!attack_radius) return;

	zmap.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

	//find the center
	cx = (x - shift_x) + (width_pix >> 1);
	cy = (y - shift_y) + (height_pix >> 1);

	//is any of this even on the map?
	if(cx + attack_radius < 0) return;
	if(cy + attack_radius < 0) return;
	if(cx - attack_radius > view_w) return;
	if(cy - attack_radius > view_h) return;

	//init stuff
	//team_type &t = owner;
	//rgb_map = SDL_MapRGB(dest->format, team_color[t].r, team_color[t].g, team_color[t].b); 
	to_rect.w = 3;
	to_rect.h = 3;

	deg = radius_i;
	for(i=0;i<dots && deg <= PI / 2;i++,deg+=PI_shift)
	{
		mx = (attack_radius + 3) * sin(deg);
		my = (attack_radius + 3) * cos(deg);

		to_rect.x = mx + cx;
		to_rect.y = my + cy;
		to_rect.w = 2;
		to_rect.h = 2;
		if((to_rect.x >= 0 && to_rect.x + 2 < view_w) && (to_rect.y >= 0 && to_rect.y + 2 < view_h))
			if(!WithinAttackRadiusOf(avoid_list, to_rect.x + shift_x, to_rect.y + shift_y))
				ZSDL_FillRect(&to_rect, team_color[owner].r, team_color[owner].g, team_color[owner].b);
				//SDL_FillRect(dest, &to_rect, rgb_map);

		to_rect.x = -mx + cx;
		to_rect.y = -my + cy;
		to_rect.w = 2;
		to_rect.h = 2;
		if((to_rect.x >= 0 && to_rect.x + 2 < view_w) && (to_rect.y >= 0 && to_rect.y + 2 < view_h))
			if(!WithinAttackRadiusOf(avoid_list, to_rect.x + shift_x, to_rect.y + shift_y))
				ZSDL_FillRect(&to_rect, team_color[owner].r, team_color[owner].g, team_color[owner].b);
				//SDL_FillRect(dest, &to_rect, rgb_map);

		to_rect.x = -mx + cx;
		to_rect.y = my + cy;
		to_rect.w = 2;
		to_rect.h = 2;
		if((to_rect.x >= 0 && to_rect.x + 2 < view_w) && (to_rect.y >= 0 && to_rect.y + 2 < view_h))
			if(!WithinAttackRadiusOf(avoid_list, to_rect.x + shift_x, to_rect.y + shift_y))
				ZSDL_FillRect(&to_rect, team_color[owner].r, team_color[owner].g, team_color[owner].b);
				//SDL_FillRect(dest, &to_rect, rgb_map);

		to_rect.x = mx + cx;
		to_rect.y = -my + cy;
		to_rect.w = 2;
		to_rect.h = 2;
		if((to_rect.x >= 0 && to_rect.x + 2 < view_w) && (to_rect.y >= 0 && to_rect.y + 2 < view_h))
			if(!WithinAttackRadiusOf(avoid_list, to_rect.x + shift_x, to_rect.y + shift_y))
				ZSDL_FillRect(&to_rect, team_color[owner].r, team_color[owner].g, team_color[owner].b);
				//SDL_FillRect(dest, &to_rect, rgb_map);
	}
}

// Visual selection dimensions measured from the resized unit graphics.
// IMPORTANT: logical width_pix/height_pix remain unchanged for movement, collision,
// pathfinding, factory entrances and ScuffleUnits until those systems
// are addressed together. These values affect ONLY UI selection bounds.
void ZObject::GetVisualDimensionsPixel(int &w_pix, int &h_pix)
{
    w_pix = width_pix;
    h_pix = height_pix;

    if(object_type == CANNON_OBJECT)
    {
        ZODCannonRect rect;
        if(ZODCannonVisualRect(object_id, rect))
        {
            w_pix = rect.w;
            h_pix = rect.h;
        }
        return;
    }
    if(object_type == ROBOT_OBJECT)
    {
        w_pix = ZOD_ROBOT_W; h_pix = ZOD_ROBOT_H;
        return;
    }
    if(object_type != VEHICLE_OBJECT) return;

    switch(object_id)
    {
    case APC:              w_pix = 32; h_pix = 35; break;
    case CRANE:            w_pix = 28; h_pix = 33; break;
    case HEAVY:            w_pix = 35; h_pix = 33; break;
    case JEEP:             w_pix = 30; h_pix = 30; break;
    case LIGHT:            w_pix = 32; h_pix = 29; break;
    case MEDIUM:           w_pix = 34; h_pix = 32; break;
    case MISSILE_LAUNCHER: w_pix = 32; h_pix = 33; break;
    }
}

void ZObject::GetVisualBounds(int &x, int &y, int &w, int &h)
{
    GetVisualDimensionsPixel(w, h);
    if(object_type == ROBOT_OBJECT)
    {
        ZODRobotVisualBounds(loc.x, loc.y, x, y, w, h);
        return;
    }

    // Cannon sprites extend above and to the sides of their 2x2 map tile.
    if(object_type == CANNON_OBJECT)
    {
        ZODCannonRect rect;
        if(ZODCannonVisualRect(object_id, rect))
        {
            x = loc.x + rect.x;
            y = loc.y + rect.y;
            return;
        }
    }

    // Keep selection for other non-vehicle objects exactly as before.
    if(object_type != VEHICLE_OBJECT)
    {
        x = center_x - (w >> 1);
        y = center_y - (h >> 1);
        return;
    }

    // Actual PNG canvas dimensions (not the smaller visible pixel bounds).
    // Vehicle sprites are drawn relative to loc.x/loc.y, NOT center_x/center_y.
    // Their render position also gets shifted by
    // ZVehicle::AdjustRenderPositionForScale().  Using the logical 32x32
    // center for our red frame made it appear too far RIGHT and DOWN.
    int canvas_w = 40;
    int canvas_h = 40;
    switch(object_id)
    {
    case APC:              canvas_h = 38; break;
    case CRANE:            canvas_h = 40; break;
    case HEAVY:            canvas_h = 40; break;
    case JEEP:             canvas_h = 39; break;
    case LIGHT:            canvas_h = 36; break;
    case MEDIUM:           canvas_h = 30; break;
    case MISSILE_LAUNCHER: canvas_h = 38; break;
    }

    // Mirror the anchor adjustment in ZVehicle::AdjustRenderPositionForScale
    // (uses 1.5x scaling), but ONLY for the visual UI frame.
    const int original_w = (2 * canvas_w + 1) / 3;
    const int original_h = (2 * canvas_h + 1) / 3;
    const int shift_w = (canvas_w - original_w + 1) / 2;
    const int shift_h = (canvas_h - original_h + 1) / 2;

    // Center the measured visible footprint on the sprite's rendered canvas.
    // Transparent padding may differ by direction; any residual subpixel
    // mismatch can be handled later without changing the underlying image.
    x = loc.x - shift_w + (canvas_w - w) / 2;
    y = loc.y - shift_h + (canvas_h - h) / 2;
}

bool ZObject::WithinVisualSelection(int &map_left, int &map_right, int &map_top, int &map_bottom)
{
    // Preserve special selection rules for forts and every other object type.
    if(object_type != VEHICLE_OBJECT && object_type != CANNON_OBJECT && object_type != ROBOT_OBJECT)
        return WithinSelection(map_left, map_right, map_top, map_bottom);

    int x, y, w, h;
    GetVisualBounds(x, y, w, h);

    if(map_left >= x + w) return false;
    if(map_right <= x) return false;
    if(map_top >= y + h) return false;
    if(map_bottom <= y) return false;
    return true;
}

void ZObject::RenderSelection(ZMap &zmap, SDL_Surface *dest)
{
	int &x = loc.x;
	int &y = loc.y;
	int shift_x, shift_y, view_w, view_h;
	SDL_Rect dim;

	zmap.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

	int visual_x, visual_y, visual_w, visual_h;
	GetVisualBounds(visual_x, visual_y, visual_w, visual_h);
	dim.x = visual_x - shift_x;
	dim.y = visual_y - shift_y;
	dim.w = visual_w;
	dim.h = visual_h;

	draw_selection_box(dest, dim, team_color[owner], view_w, view_h);

	//draw group number
	if(group_num != -1)
	{
        if(object_type == CANNON_OBJECT || object_type == ROBOT_OBJECT)
            zmap.RenderZSurface(&group_tag[group_num], visual_x-2, visual_y-3);
        else
		    zmap.RenderZSurface(&group_tag[group_num], x-2, y-3);
		//dim.x -= 2;
		//dim.y -= 3;

		//SDL_BlitSurface(group_tag[group_num], NULL, dest, &dim);
	}

	//render health too
	RenderHealth(zmap, dest);
}

bool ZObject::WithinAutoEnterRadius(int ox, int oy)
{
	int ae_radius = zsettings->auto_grab_vehicle_distance;
	int &x = center_x;
	int &y = center_y;

	return points_within_distance(x, y, ox, oy, ae_radius);
}

bool ZObject::WithinAutoGrabFlagRadius(int ox, int oy)
{
	int agf_radius = zsettings->auto_grab_flag_distance;
	int &x = center_x;
	int &y = center_y;

	return points_within_distance(x, y, ox, oy, agf_radius);
}

bool ZObject::WithinAgroRadius(ZObject *obj)
{
	if(!obj) return false;

	int &x = center_x;
	int &y = center_y;
	int &ox = obj->center_x;
	int &oy = obj->center_y;

	if(!WithinAgroRadius(ox, oy)) return false;

	if(!obj->IsDestroyableImpass() && zmap && zmap->EngageBarrierBetweenCoords(x,y,ox,oy)) return false;

	return true;
}

bool ZObject::WithinAgroRadius(int ox, int oy)
{
	int agro_radius = attack_radius + zsettings->agro_distance;
	int &x = center_x;
	int &y = center_y;

	return points_within_distance(x, y, ox, oy, agro_radius);
}

bool ZObject::WithinAttackRadius(ZObject *obj)
{
	if(!obj) return false;

	int &x = center_x;
	int &y = center_y;
	int &ox = obj->center_x;
	int &oy = obj->center_y;

	if(!WithinAttackRadius(ox, oy)) return false;

	if(!obj->IsDestroyableImpass() && zmap && zmap->EngageBarrierBetweenCoords(x,y,ox,oy)) return false;

	return true;
}

bool ZObject::WithinAttackRadius(int ox, int oy)
{
	int &x = center_x;
	int &y = center_y;

	return points_within_distance(x, y, ox, oy, attack_radius);
}

bool ZObject::WithinAttackRadiusOf(vector<ZObject*> &avoid_list, int ox, int oy)
{
	vector<ZObject*>::iterator i;

	for(i=avoid_list.begin();i!=avoid_list.end();i++)
		if(*i != this && (*i)->WithinAttackRadius(ox, oy))
			return true;

	return false;
}

bool ZObject::Selectable()
{
	//minions are not selectable
	if(leader_obj) return false;

	return selectable;
}

bool ZObject::UnderCursor(int &map_x, int &map_y)
{
	int &x = loc.x;
	int &y = loc.y;
	if(map_x < x) return false;
	if(map_y < y) return false;
	if(map_x > x + width_pix) return false;
	if(map_y > y + height_pix) return false;

	return true;
}

double ZObject::DistanceFromCoords(int x, int y)
{
	int dx, dy;

	dx = loc.x - x;
	dy = loc.y - y;

	return sqrt((double)((dx * dx) + (dy * dy)));
}

double ZObject::DistanceFromObject(ZObject &obj)
{
	int dx, dy;

	dx = loc.x - obj.loc.x;
	dy = loc.y - obj.loc.y;

	return sqrt((double)((dx * dx) + (dy * dy)));
}

bool ZObject::IntersectsObject(ZObject &obj)
{
	int &x = loc.x;
	int &y = loc.y;
	if(obj.loc.x >= x + width_pix) return false;
	if(obj.loc.x + obj.width_pix <= x) return false;
	if(obj.loc.y >= y + height_pix) return false;
	if(obj.loc.y + obj.height_pix <= y) return false;

	return true;
}

bool ZObject::WithinSelection(int &map_left, int &map_right, int &map_top, int &map_bottom)
{
	int &x = loc.x;
	int &y = loc.y;

	if(map_left >= x + width_pix) return false;
	if(map_right <= x) return false;
	if(map_top >= y + height_pix) return false;
	if(map_bottom <= y) return false;

	return true;
}

bool ZObject::CannonNotPlacable(int &map_left, int &map_right, int &map_top, int &map_bottom)
{
	return WithinSelection(map_left, map_right, map_top, map_bottom);
}

string ZObject::GetObjectName()
{
	return object_name;
}

void ZObject::SetDirection(int direction_)
{
	direction = direction_;
}

void ZObject::SetCords(int x_, int y_)
{
	int &x = loc.x;
	int &y = loc.y;
	x = x_;
	y = y_;

	//set centers
	center_x = x + (width_pix >> 1);
	center_y = y + (height_pix >> 1);
}

//void ZObject::GetCords(int &x_, int &y_)
//{
//	x_ = loc.x;
//	y_ = loc.y;
//}

//void ZObject::GetCenterCords(int &x_, int &y_)
//{
//	//int &x = loc.x;
//	//int &y = loc.y;
//	//x_ = x + (width_pix >> 1);
//	//y_ = y + (height_pix >> 1);
//	x_ = center_x;
//	y_ = center_y;
//}

void ZObject::SetOwner(team_type owner_)
{
	owner = owner_;
}

// SDL_Surface *ZObject::GetRender()
// {
// 	return NULL;
// }

//void ZObject::GetDimensionsPixel(int &w_pix, int &h_pix)
//{
//	w_pix = width_pix;
//	h_pix = height_pix;
//}

//void ZObject::GetDimensions(int &w, int &h)
//{
//	w = width;
//	h = height;
//}

int ZObject::Process()
{
	printf("ZObject::Process:%s\n", object_name.c_str());
	return 0;
}

int ZObject::ProcessObject()
{
	double &the_time = ztime->ztime;
	double time_dif;

	//smooth the walk because of lag
	SmoothMove(the_time);

	//attack radius
	{
		const int dots = 10;
		const double PI_shift = (PI / 2) / dots;

		time_dif = the_time - last_radius_time;
		last_radius_time = the_time;

		radius_i += (PI_shift) * time_dif;

		//pull it back down
		while(radius_i > PI_shift)
			radius_i -= PI_shift;
	}

	if(the_time >= next_waypoint_time)
	{
		waypoint_i++;
		if(waypoint_i >= 4) waypoint_i = 0;

		next_waypoint_time = the_time + 0.1;
	}

	if(show_waypoints)
	{
		waypoint_cursor.Process(the_time);

		if(the_time >= render_death_time)
			show_waypoints = false;
	}

	TryDropTracks();

	return 1;
}

void ZObject::ShowWaypoints()
{
	double &the_time = ztime->ztime;
	vector<waypoint>::iterator i;

	render_death_time = the_time + 3.0;
	show_waypoints = true;

	//set cursor
	if(waypoint_list.size())
	{
		i=waypoint_list.end();
		i--;

		switch(i->mode)
		{
		case MOVE_WP:
		case FORCE_MOVE_WP:
		case ENTER_FORT_WP:
		case DODGE_WP:
			waypoint_cursor.SetCursor(PLACED_C);
			break;
		case PICKUP_GRENADES_WP:
			waypoint_cursor.SetCursor(GRABBED_C);
			break;
		case ENTER_WP:
			waypoint_cursor.SetCursor(ENTERED_C);
			break;
		case ATTACK_WP:
		case AGRO_WP:
			waypoint_cursor.SetCursor(ATTACKED_C);
			break;
		case CRANE_REPAIR_WP:
		case UNIT_REPAIR_WP:
			waypoint_cursor.SetCursor(REPAIRED_C);
			break;
		default:
			waypoint_cursor.SetCursor(PLACED_C);
			break;
		}
	}
	else
		waypoint_cursor.SetCursor(PLACED_C);
}

void ZObject::DoRenderWaypoints(ZMap &the_map, SDL_Surface *dest, vector<ZObject*> &object_list, bool is_rally_points, int shift_x, int shift_y)
{
	int x, y;
	int nx, ny;
	const int glvl = 170;
	SDL_Rect the_box;
	vector<waypoint>::iterator i;
	//Uint32 mappedrgb = SDL_MapRGB(dest->format, glvl, glvl, glvl);
	double tx,ty;
	double txp, typ;
	double dx, dy;
	double dist;
	int ox, oy;
	int maxj;
	int j;
	int mshift_x, mshift_y;
	int view_h, view_w;
	ZObject *target_object;
	vector<waypoint> *render_waypoint_list;

	if(!is_rally_points && !show_waypoints) return;

	if(is_rally_points)
	{
		if(!GetBuildingCreationPoint(x, y))
		{
			x = center_x;
			y = center_y;
		}
		render_waypoint_list = &rallypoint_list;
	}
	else
	{
		x = center_x;
		y = center_y;
		render_waypoint_list = &waypoint_list;
	}

	//do we even have waypoints to render?
	if(!render_waypoint_list->size()) return;

	the_map.GetViewShiftFull(mshift_x, mshift_y, view_w, view_h);

	view_w -= 2;
	view_h -= 2;

	tx = x - mshift_x;
	ty = y - mshift_y;

	//center
	//tx += width_pix >> 1;
	//ty += height_pix >> 1;

	//draw from inside
	if(is_rally_points && GetBuildingCreationMovePoint(nx, ny))
	{
		dx = nx - x;
		dy = ny - y;

		dist = sqrt((dx * dx) + (dy * dy));

		txp = 4 * dx / dist;
		typ = 4 * dy / dist;

		tx += txp * (waypoint_i / 4.0);
		ty += typ * (waypoint_i / 4.0);

		maxj = dist / 4;
		maxj++;

		if(waypoint_i) maxj--;

		//draw this segments points
		for(j=0;j<maxj;j++)
		{
			if((int)tx < view_w && (int)ty < view_h)
			{
				the_box.x = (int)tx;
				the_box.y = (int)ty;
				the_box.w = 2;
				the_box.h = 2;

				//SDL_FillRect(dest, &the_box, mappedrgb);
				ZSDL_FillRect(&the_box, glvl, glvl, glvl);
			}

			tx += txp;
			ty += typ;
		}

		//setup for next round
		tx = (nx - mshift_x);
		ty = (ny - mshift_y);
	}
	
	//for(i=waypoint_list.begin();i!=waypoint_list.end();i++)
	for(i=render_waypoint_list->begin();i!=render_waypoint_list->end();i++)
	{
		switch(i->mode)
		{
		case MOVE_WP:
		case FORCE_MOVE_WP:
		case ENTER_WP:
		case CRANE_REPAIR_WP:
		case UNIT_REPAIR_WP:
		case ENTER_FORT_WP:
		case DODGE_WP:
		case PICKUP_GRENADES_WP:
			//dx = (i->x - mshift_x) - tx;
			//dy = (i->y - mshift_y) - ty;
			nx = (i->x - mshift_x);
			ny = (i->y - mshift_y);
			break;
		
			//dx = (i->x - mshift_x) - tx;
			//dy = (i->y - mshift_y) - ty;
			//break;
		case ATTACK_OWN_WP:
		case ATTACK_WP:
		case AGRO_WP:
			target_object = GetObjectFromID(i->ref_id, object_list);

			if(target_object)
			{
				target_object->GetCenterCords(ox, oy);

				//dx = (ox - mshift_x) - tx;
				//dy = (oy - mshift_y) - ty;
				nx = (ox - mshift_x);
				ny = (oy - mshift_y);
			}
			else
			{
				//dx = (i->x - mshift_x) - tx;
				//dy = (i->y - mshift_y) - ty;
				nx = (i->x - mshift_x);
				ny = (i->y - mshift_y);
			}
			
			break;
		}

		//render
		RenderWaypointLine(tx, ty, nx, ny, view_h, view_w);

		//set old to new
		tx = nx;
		ty = ny;

		/*
		//dx = (i->x - mshift_x) - tx;
		//dy = (i->y - mshift_y) - ty;
		dist = sqrt((dx * dx) + (dy * dy));

		txp = 4 * dx / dist;
		typ = 4 * dy / dist;

		tx += txp * (waypoint_i / 4.0);
		ty += typ * (waypoint_i / 4.0);

		maxj = dist / 4;
		maxj++;

		if(waypoint_i) maxj--;

		//draw this segments points
		for(j=0;j<maxj;j++)
		{
			if((int)tx < view_w && (int)ty < view_h)
			{
				the_box.x = (int)tx;
				the_box.y = (int)ty;
				the_box.w = 2;
				the_box.h = 2;

				//SDL_FillRect(dest, &the_box, mappedrgb);
				ZSDL_FillRect(&the_box, glvl, glvl, glvl);
			}

			tx += txp;
			ty += typ;
		}

		//setup for next round
		tx = (i->x - mshift_x);
		ty = (i->y - mshift_y);
		*/
	}

	//render cursor
	//if(waypoint_list.size())
	if(render_waypoint_list->size())
	{
		int kx, ky;
		//i=waypoint_list.end();
		i=render_waypoint_list->end();
		i--;

		switch(i->mode)
		{
		case MOVE_WP:
		case FORCE_MOVE_WP:
		case ENTER_FORT_WP:
		case DODGE_WP:
			kx = (i->x);// - mshift_x;
			ky = (i->y);// - mshift_y;
			waypoint_cursor.SetCursor(PLACED_C);
			break;
		case ENTER_WP:
			kx = (i->x);// - mshift_x;
			ky = (i->y);// - mshift_y;
			waypoint_cursor.SetCursor(ENTERED_C);
			break;
		case PICKUP_GRENADES_WP:
			kx = (i->x);// - mshift_x;
			ky = (i->y);// - mshift_y;
			waypoint_cursor.SetCursor(GRABBED_C);
			break;
		case CRANE_REPAIR_WP:
		case UNIT_REPAIR_WP:
			kx = (i->x);// - mshift_x;
			ky = (i->y);// - mshift_y;
			waypoint_cursor.SetCursor(REPAIRED_C);
			break;
		case ATTACK_OWN_WP:
		case ATTACK_WP:
		case AGRO_WP:
			target_object = GetObjectFromID(i->ref_id, object_list);

			if(target_object)
			{
				target_object->GetCenterCords(ox, oy);

				kx = ox;// - mshift_x;
				ky = oy;// - mshift_y;

				waypoint_cursor.SetCursor(ATTACKED_C);
			}
			else
			{
				kx = (i->x);// - mshift_x;
				ky = (i->y);// - mshift_y;
				waypoint_cursor.SetCursor(PLACED_C);
			}
			
			break;
		}

		waypoint_cursor.Render(the_map, dest, kx, ky, true);
	}
}

void ZObject::RenderWaypointLine(int sx, int sy, int ex, int ey, int view_h, int view_w)
{
	const int glvl = 170;
	double tx, ty;
	double txp, typ;
	double dx, dy;
	double dist;
	int maxj;
	SDL_Rect the_box;

	tx = sx;
	ty = sy;

	dx = ex - sx;
	dy = ey - sy;

	dist = sqrt((dx * dx) + (dy * dy));

	txp = 4 * dx / dist;
	typ = 4 * dy / dist;

	tx += txp * (waypoint_i / 4.0);
	ty += typ * (waypoint_i / 4.0);

	maxj = dist / 4;
	maxj++;

	if(waypoint_i) maxj--;

	//draw this segments points
	for(int j=0;j<maxj;j++)
	{
		if((int)tx < view_w && (int)ty < view_h)
		{
			the_box.x = (int)tx;
			the_box.y = (int)ty;
			the_box.w = 2;
			the_box.h = 2;

			//SDL_FillRect(dest, &the_box, mappedrgb);
			ZSDL_FillRect(&the_box, glvl, glvl, glvl);
		}

		tx += txp;
		ty += typ;
	}
}

void ZObject::DoRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
	printf("ZObject::DoRender:%s\n", object_name.c_str());
}

void ZObject::DoAfterEffects(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{
// 	printf("ZObject::DoAfterEffects:%s\n", object_name.c_str());
}

void ZObject::SetDestroyed(bool is_destroyed)
{
	destroyed = is_destroyed;
}

void ZObject::GetObjectID(unsigned char &object_type_, unsigned char &object_id_)
{
   object_type_ = object_type;
   object_id_ = object_id;
}

//team_type ZObject::GetOwner()
//{
//   return owner;
//}

int ZObject::ProcessServer(ZMap &tmap, ZOLists &ols)
{
	double &the_time = ztime->ztime;
	double time_dif;
	vector<waypoint>::iterator wp;
	bool is_new_waypoint;
	bool attack_player_given = false;

	time_dif = the_time - last_process_server_time;
	last_process_server_time = the_time;

	sflags.clear();

	//it being repaired?
	sflags.auto_repair = DoAutoRepair(tmap, ols);

	//increase / decrease stamina
	ProcessRunStamina(time_dif);

	if(IsDestroyed()) return 0;

	//was a unit created?
	sflags.build_unit = BuildUnit(the_time, sflags.bot, sflags.boid);

	//repair unit
	sflags.repair_unit = RepairUnit(the_time, sflags.rot, sflags.roid, sflags.rdriver_type, sflags.rdriver_info, sflags.rwaypoint_list);

	//lid business
	ProcessServerLid();

	// Emergency exit runs before A*, including idle units whose last order
	// was already completed. Scripted building movements are excluded.
	if(ProcessCollisionRecovery(time_dif, tmap)) return 1;

	//go through waypoints
	if(waypoint_list.size())
	{
		wp = waypoint_list.begin();

		if(*wp != last_wp)
		{
			is_new_waypoint = true;
			// A route retry calls last_wp.clear() for the SAME MOVE_WP.
			// Reuse its already-chosen parking spot rather than spending CPU
			// searching all the candidates again (and jumping to a new spot).
			// A genuinely different order still invalidates the old slot.
			if(!parking_valid || wp->mode != MOVE_WP ||
			   wp->x != parking_order_x || wp->y != parking_order_y)
				parking_valid = false;
			next_path_retry_time = 0;
			last_wp = *wp;
					xover = yover = 0;

			cur_wp_info.clear_and_kill(tmap);

			//because all waypoints use cur_wp_info.x+y for movement
			//we set it to our current location to set ourselves to
			//an initial "stop"
			//cur_wp_info.x = loc.x + (width_pix >> 1);
			//cur_wp_info.y = loc.y + (height_pix >> 1);
			//cur_wp_info.x = center_x;
			//cur_wp_info.y = center_y;
			SetTarget(center_x, center_y);

			//stop running
			is_running = false;
		}
		else
			is_new_waypoint = false;

		// An unreachable route is a temporary navigation failure, not a
		// successful direct path and not a reason to delete a repair/attack order.
		// Retry the same order after a short pause without busy-looping A*.
		if(cur_wp_info.path_failed)
		{
			// The player's movement order remains underneath auto ATTACK_WP.
			// A failed A* route to an automatic target must not hold the whole
			// unit hostage to endless identical retry requests.
			if(AbandonUnreachableAutoTarget())
			{
				// Reconsider the saved movement order on the next tick.
			}
			else if(the_time >= next_path_retry_time)
			{
				next_path_retry_time = 0;
				cur_wp_info.clear_and_kill(tmap);
				last_wp.clear(); // next server tick will initialize the same order
			}
		}
		else switch(wp->mode)
		{
		case MOVE_WP:
			ProcessMoveWP(wp, time_dif, is_new_waypoint, ols, tmap, true);
			break;
		case FORCE_MOVE_WP:
			ProcessMoveWP(wp, time_dif, is_new_waypoint, ols, tmap, false);
			break;
		case DODGE_WP:
			ProcessDodgeWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		case ENTER_WP:
			ProcessEnterWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		case ATTACK_OWN_WP:
		case ATTACK_WP:
			attack_player_given = wp->player_given;
			ProcessAttackWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		case AGRO_WP:
			ProcessAgroWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		case CRANE_REPAIR_WP:
			ProcessCraneRepairWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		case UNIT_REPAIR_WP:
			ProcessUnitRepairWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		case ENTER_FORT_WP:
			ProcessEnterFortWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		case PICKUP_GRENADES_WP:
			ProcessPickupWP(wp, time_dif, is_new_waypoint, ols, tmap);
			break;
		default:
			printf("ZObject::ProcessServer::killing unknown waypoint:%d\n", wp->mode);
			KillWP(wp);
			break;
		}
	}
	else
	{
		//!waypoint_list.size()
		//StopMove();
	}

	//see if they want to attack someone nearby
	CheckPassiveEngage(the_time, ols);

	//let them do their damage
	ProcessAttackDamage(tmap, attack_player_given);

	//update location only happens in intervals
	//update velocity always happens
	if(sflags.updated_location)
	{
		if(the_time >= next_loc_update_time)
			next_loc_update_time = the_time + loc_update_int;
		else
			sflags.updated_location = false;
	}

	return 1;
}

void ZObject::Engage(ZObject *attack_object_)
{
	//are we really disengaging by chance?
	if(!attack_object_)
	{
		Disengage();
		return;
	}

	//attack then
	if(attack_object != attack_object_)
	{
		attack_object = attack_object_;
		sflags.updated_attack_object = true;

		//if we can be sniped by the target, we should open our lid!
		if(attack_object->CanSnipe())
			SignalLidShouldOpen();
	}
}

bool ZObject::Disengage()
{
	if(attack_object)
	{
		attack_object = NULL;
		sflags.updated_attack_object = true;

		//we only open our lid while we're engaged.
		SignalLidShouldClose();

		return true;
	}

	return false;
}

bool ZObject::IsMoving()
{
	const double z = 0.00001;

	return !((loc.dx > -z && loc.dx < z) && (loc.dy > -z && loc.dy < z));
}

// This assist applies to normal travel only. No force moves, factory exits,
// repair entrances, dodging, attack orders, or stationary/parked units.
bool ZObject::GetTransportMotion(double &vx, double &vy)
{
	vx = vy = 0.0;
	if(IsDestroyed() || waypoint_list.empty() ||
		waypoint_list.front().mode != MOVE_WP ||
		cur_wp_info.path_failed || cur_wp_info.path_finding_id || !IsMoving())
		return false;

	// Fase 6B: keep the proven braking near arrival, too. Distinct parking
	// slots now replace the old rule that allowed stacking at the endpoint.

	vx = loc.dx;
	vy = loc.dy;
	return true;
}

void ZObject::SetTransportSpeedFactor(double factor)
{
	// ScuffleUnits never repositions a unit; it can only temporarily change
	// the unit's OWN velocity while a normal MOVE_WP is active.
	if(IsDestroyed() || waypoint_list.empty() ||
		waypoint_list.front().mode != MOVE_WP)
		factor = 1.0;
	if(factor < 0.45) factor = 0.45;
	if(factor > 1.0) factor = 1.0;
	if(fabs(factor - transport_speed_factor) < 0.0001) return;

	const double ratio = factor / transport_speed_factor;
	transport_speed_factor = factor;
	if(IsMoving())
	{
		loc.dx *= ratio;
		loc.dy *= ratio;
		sflags.updated_velocity = true;
	}
}


// Fase 6B A/B switch: turn off ONLY new destination spreading; keep Fase6A.
static const bool scuffle_parking_slots_enabled = true;

// Fase 6B: parking is local server state, NOT a change to a player order,
// packet, attack target, or the physical object position.
bool ZObject::GetReservedMoveParking(int &x, int &y) const
{
    if(!parking_valid || waypoint_list.empty() ||
       waypoint_list.front().mode != MOVE_WP ||
       waypoint_list.front() != last_wp ||
       waypoint_list.front().x != parking_order_x ||
       waypoint_list.front().y != parking_order_y)
        return false;
    x = parking_target_x;
    y = parking_target_y;
    return true;
}

// Preserve targeting before moving an already-engaged unit out of a pile.
// A successful candidate remains in range and does not shoot through walls.
bool ZObject::DestinationSafeForCombat(int x, int y, ZMap &tmap)
{
    if(!attack_object || attack_object->IsDestroyed()) return true;
    if(attack_radius <= 8) return false;
    int tx, ty;
    attack_object->GetCenterCords(tx, ty);
    const long dx = x - tx, dy = y - ty;
    const long safe_range = attack_radius - 6;
    if(dx*dx + dy*dy > safe_range*safe_range) return false;
    if(!attack_object->IsDestroyableImpass() &&
       tmap.EngageBarrierBetweenCoords(x, y, tx, ty)) return false;
    return true;
}

// Precompute parking occupancy ONCE per search. The old code repeatedly
// traversed the full object list for each 16px candidate. During large army
// orders that meant thousands of virtual calls on the main server thread.
// C++98 requires vector element types to have external linkage.
struct ZParkingOccupant { int x, y; double clearance_x, clearance_y; };

// Choose a goal ONCE when MOVE_WP starts, before asking A* for a route.
// Existing idle units and previously reserved movement goals are obstacles
// only to PARKING, not to travelling: units may still pass each other.
// The initial requested destination is always tried first.
bool ZObject::FindFreeParkingSpot(int gx, int gy, ZOLists &ols, ZMap &tmap,
    int &out_x, int &out_y, int rings, bool auto_unstack)
{
    if(!ols.object_list || width_pix <= 2 || height_pix <= 2) return false;
    const bool is_robot = object_type == ROBOT_OBJECT;
    const int step = 16;
    bool found = false;
    long best_score = 0;
    vector<ZParkingOccupant> occupied;
    // The occupancy list is immutable throughout one destination search;
    // the server runs movement/waypoint updates sequentially.
    for(vector<ZObject*>::iterator it=ols.object_list->begin();
        it!=ols.object_list->end(); ++it)
    {
        ZObject *other = *it;
        if(!other || other == this || other->IsDestroyed()) continue;
        if(other->object_type != VEHICLE_OBJECT && other->object_type != ROBOT_OBJECT) continue;
        if(other->owner == NULL_TEAM) continue;
        int ox=0, oy=0;
        bool reserved = other->GetReservedMoveParking(ox, oy);
        if(!reserved && !other->waypoint_list.empty() &&
           other->waypoint_list.front().mode == MOVE_WP &&
           other->waypoint_list.front().player_given == false &&
           !other->IsMoving())
        {
            ox = other->waypoint_list.front().x;
            oy = other->waypoint_list.front().y;
            reserved = true;
        }
        if(!reserved && other->IsMoving()) continue;
        if(!reserved) other->GetCenterCords(ox, oy);
        ZParkingOccupant o = {ox, oy,
            (width_pix + other->width_pix)*0.44 + 2.0,
            (height_pix + other->height_pix)*0.44 + 2.0};
        occupied.push_back(o);
    }

    for(int ring=0; ring<=rings; ++ring)
    {
    for(int iy=-ring; iy<=ring; ++iy)
    for(int ix=-ring; ix<=ring; ++ix)
    {
        if(ring && abs(ix)!=ring && abs(iy)!=ring) continue;
        if(auto_unstack && !ring) continue;
        const int px = gx + ix*step;
        const int py = gy + iy*step;
        int stop_x = 0, stop_y = 0;
        if(tmap.WithinImpassable(px - (width_pix >> 1) + 1,
            py - (height_pix >> 1) + 1, width_pix-2, height_pix-2,
            stop_x, stop_y, is_robot)) continue;
        bool taken = false;
        for(vector<ZParkingOccupant>::const_iterator it=occupied.begin();
            it!=occupied.end(); ++it)
            if(fabs((double)px-it->x) < it->clearance_x &&
               fabs((double)py-it->y) < it->clearance_y)
            { taken = true; break; }
        if(taken) continue;
        // Checking reservations first avoids costly path/region validation
        // for occupied spots in a large mixed army. Same valid candidates.
        if(!tmap.GetPathFinder().ShouldBeAbleToMoveTo(loc.x, loc.y,
             px, py, is_robot)) continue;
        if(auto_unstack && !DestinationSafeForCombat(px, py, tmap)) continue;
        // Favour a tight, stable formation near the selected destination.
        // Among equally close rings, favour the unit's own approach side.
        const long ax = px-center_x, ay = py-center_y;
        const long score = (ax*ax + ay*ay);
        if(!found || score < best_score)
        {
            found = true;
            best_score = score;
            out_x = px; out_y = py;
        }
    }
    // The first usable ring has enough clearance. Never let a far-away
    // candidate win over a nearby one, and avoid scanning all 625 samples.
    if(found) break;
    }
    return found;
}

bool ZObject::CanAutoUnstack()
{
    // Do not issue a parking order while the local emergency exit is active.
    if(collision_recovery_active) return false;
    if(IsDestroyed() || owner == NULL_TEAM || !CanMove() || IsMoving() ||
       ztime->ztime < next_auto_unstack_time) return false;
    if(object_type != ROBOT_OBJECT && object_type != VEHICLE_OBJECT) return false;
    if(waypoint_list.empty()) return true;
    const int mode = waypoint_list.front().mode;
    // No interruption of MOVE, FORCE_MOVE, Repair, Enter, Dodge or a
    // moving/blocked attack. Relocate only units ALREADY firing in place.
    if(mode != ATTACK_WP && mode != ATTACK_OWN_WP && mode != AGRO_WP)
        return false;
    return attack_object && WithinAttackRadius(attack_object);
}

bool ZObject::RequestAutoUnstack(ZOLists &ols, ZMap &tmap)
{
    if(!CanAutoUnstack()) return false;
    int x = 0, y = 0;
    if(!FindFreeParkingSpot(center_x, center_y, ols, tmap,
        x, y, 7, true))
    {
        // Prevent repeated expensive searches if terrain is too tight.
        next_auto_unstack_time = ztime->ztime + 2.0;
        return false;
    }
    waypoint move;
    move.mode = MOVE_WP;
    move.x = x;
    move.y = y;
    move.player_given = false;
    move.attack_to = false;
    waypoint_list.insert(waypoint_list.begin(), move);
    next_auto_unstack_time = ztime->ztime + 4.0;
    sflags.updated_waypoints = true;
    // ProcessServer on the next tick will plan a regular A* MOVE_WP.
    // No SetCords, no forced velocities and no global collision changes.
    return true;
}

void ZObject::CheckPassiveEngage(double &the_time, ZOLists &ols)
{
        // Explicit own-attack order takes priority
        if(!waypoint_list.empty() && waypoint_list.front().mode == ATTACK_OWN_WP)
        {
                if(attack_object && (attack_object->GetRefID() != waypoint_list.front().ref_id ||
                                     !WithinAttackRadius(attack_object)))
                        Disengage();
                return;
        }
	//is it time, since this is a "tiny bit intensive"
	if(the_time < next_check_passive_attack_time) return;

	//set for next time
	next_check_passive_attack_time = the_time + 0.5;

	//can we even attack
	if(!CanAttack()) return;
	if(owner == NULL_TEAM) return;
	if(!(object_type == CANNON_OBJECT || object_type == VEHICLE_OBJECT || object_type == ROBOT_OBJECT)) return;
	if(object_type == ROBOT_OBJECT && IsMoving()) return;

	//are we already attacking someone?
	if(attack_object) 
	{
		int ox, oy;

		//is it still within range?
		attack_object->GetCenterCords(ox, oy);

		//if(!WithinAttackRadius(ox, oy))
		if(!WithinAttackRadius(attack_object))
			Disengage();
		
		return;
	}

	vector<ZObject*>::iterator obj;

	vector<ZObject*> agro_choices;
	vector<ZObject*> enter_vehicle_choices;
	vector<ZObject*> grab_flag_choices;
	vector<ZObject*> grab_grenades_choices;

	//lets see if there are any enemies to attack..
	if(CanAttack())
	for(obj=ols.passive_engagable_olist.begin();obj!=ols.passive_engagable_olist.end();obj++)
	{
		int ox, oy;
		unsigned char ot, oid;

		(*obj)->GetObjectID(ot, oid);
		(*obj)->GetCenterCords(ox, oy);

		if((*obj)->GetOwner() != NULL_TEAM && (*obj)->GetOwner() != owner && CanAttackObject((*obj)))
		{
			//do not auto attack buildings
			if(!(ot == CANNON_OBJECT || ot == VEHICLE_OBJECT || ot == ROBOT_OBJECT)) continue;

			//if(WithinAttackRadius(ox, oy))
			if(WithinAttackRadius(*obj))
			{
				Engage(*obj);
				//attack_object = *obj;
				//sflags.updated_attack_object = true;
				return;
			}

			//start an agro waypoint?
			//if we have no waypoints and are "not a cannon" / "able to move"
			if(!waypoint_list.size() && object_type != CANNON_OBJECT)
			{
				//if(WithinAgroRadius(ox, oy))
				if(WithinAgroRadius(*obj) && !IsAutoTargetAvoided((*obj)->GetRefID()))
					agro_choices.push_back(*obj);
			}
		}

		//add to the auto enter or grab lists?
		if(!waypoint_list.size() && object_type != CANNON_OBJECT)
		{
			if(!IsMinion() && !agro_choices.size())
			{
				//auto enter vehicle (not apc though)?
				if(object_type == ROBOT_OBJECT && (*obj)->CanBeEntered() && WithinAutoEnterRadius(ox, oy) && !(ot == VEHICLE_OBJECT && oid == APC) && !(this->just_left_cannon && ot == CANNON_OBJECT))
					enter_vehicle_choices.push_back(*obj);
				//else if((ot == MAP_ITEM_OBJECT && oid == FLAG_ITEM) && (*obj)->GetOwner() != owner && WithinAutoGrabFlagRadius(ox, oy))
				//	grab_flag_choices.push_back(*obj);
				//else if((ot == MAP_ITEM_OBJECT && oid == GRENADES_ITEM) && CanPickupGrenades() && WithinAutoEnterRadius(ox, oy))
				//	grab_grenades_choices.push_back(*obj);
			}
		}
	}

	if(!IsMinion() && !agro_choices.size() && !waypoint_list.size())
	{
		//collect grabbable flags
		if(CanMove())
		for(obj=ols.flag_olist.begin();obj!=ols.flag_olist.end();obj++)
		{
			int ox, oy;
			unsigned char ot, oid;

			(*obj)->GetObjectID(ot, oid);
			(*obj)->GetCenterCords(ox, oy);

			if(ot != MAP_ITEM_OBJECT) continue;
			if(oid != FLAG_ITEM) continue;
			if((*obj)->GetOwner() == owner) continue;
			
			team_type flag_owner = (*obj)->GetOwner();

			if(alliance_matrix &&
   			owner > NULL_TEAM && owner < MAX_TEAM_TYPES &&
   			flag_owner > NULL_TEAM && flag_owner < MAX_TEAM_TYPES &&
   			alliance_matrix[owner][flag_owner])
    			continue;

			if(!WithinAutoGrabFlagRadius(ox, oy)) continue;

			grab_flag_choices.push_back(*obj);
		}

		//collect grenades list
		if(CanPickupGrenades())
		for(obj=ols.grenades_olist.begin();obj!=ols.grenades_olist.end();obj++)
		{
			int ox, oy;
			unsigned char ot, oid;

			(*obj)->GetObjectID(ot, oid);
			(*obj)->GetCenterCords(ox, oy);

			//if((ot == MAP_ITEM_OBJECT && oid == GRENADES_ITEM) && CanPickupGrenades() && WithinAutoEnterRadius(ox, oy))
			if(ot != MAP_ITEM_OBJECT) continue;
			if(oid != GRENADES_ITEM) continue;
			if(!WithinAutoEnterRadius(ox, oy)) continue;

			grab_grenades_choices.push_back(*obj);
		}
	}

	if(agro_choices.size())
	{
		ZObject *agro_choice;
		int ox, oy;
		double least_distance;

		//find choice (closest)
		agro_choice = agro_choices[0];
		least_distance = DistanceFromObject(*agro_choice);
		for(obj=agro_choices.begin()++;obj!=agro_choices.end();obj++)
		{
			double this_distance = DistanceFromObject(**obj);

			if(this_distance < least_distance)
			{
				least_distance = this_distance;
				agro_choice = *obj;
			}
		}
		

		agro_choice->GetCenterCords(ox, oy);

		waypoint new_movepoint;

		new_movepoint.mode = AGRO_WP;
		new_movepoint.ref_id = agro_choice->GetRefID();
		new_movepoint.x = ox;
		new_movepoint.y = oy;

		waypoint_list.push_back(new_movepoint);
	}
	else
	{
		vector<ZObject*> *obj_choices = NULL;
		int waypoint_mode = MOVE_WP;
		bool do_auto_enter = false;
		bool do_auto_grab_flag = false;
		bool do_auto_grab_grenades = false;

		if(enter_vehicle_choices.size() && grab_flag_choices.size() && grab_grenades_choices.size())
		{
			switch(rand() % 3)
			{
				case 0: do_auto_enter = true; break;
				case 1: do_auto_grab_flag = true; break;
				case 2: do_auto_grab_grenades = true; break;
			}
		}
		else if(enter_vehicle_choices.size() && grab_flag_choices.size())
		{
			if(rand() % 2) do_auto_enter = true;
			else do_auto_grab_flag = true;
		}
		else if(enter_vehicle_choices.size() && grab_grenades_choices.size())
		{
			if(rand() % 2) do_auto_enter = true;
			else do_auto_grab_grenades = true;
		}
		else if(grab_flag_choices.size() && grab_grenades_choices.size())
		{
			if(rand() % 2) do_auto_grab_flag = true;
			else do_auto_grab_grenades = true;
		}
		else if(enter_vehicle_choices.size())
			do_auto_enter = true;
		else if(grab_flag_choices.size())
			do_auto_grab_flag = true;
		else if(grab_grenades_choices.size())
			do_auto_grab_grenades = true;

		if(do_auto_enter)
		{
			obj_choices = &enter_vehicle_choices;
			waypoint_mode = ENTER_WP;
		}
		else if(do_auto_grab_flag)
		{
			obj_choices = &grab_flag_choices;
			waypoint_mode = MOVE_WP;
		}
		else if(do_auto_grab_grenades)
		{
			obj_choices = &grab_grenades_choices;
			waypoint_mode = PICKUP_GRENADES_WP;
		}

		if(obj_choices && obj_choices->size())
		{
			//vector<ZObject*> &obj_choices = enter_vehicle_choices;
			ZObject *obj_choice;
			int ox, oy;
			double least_distance;

			//find choice (closest)
			obj_choice = *obj_choices->begin();
			least_distance = DistanceFromObject(*obj_choice);
			for(obj=obj_choices->begin()++;obj!=obj_choices->end();obj++)
			{
				double this_distance = DistanceFromObject(**obj);

				if(this_distance < least_distance)
				{
					least_distance = this_distance;
					obj_choice = *obj;
				}
			}

			obj_choice->GetCenterCords(ox, oy);

			waypoint new_movepoint;

			new_movepoint.mode = waypoint_mode;
			new_movepoint.ref_id = obj_choice->GetRefID();
			new_movepoint.x = ox;
			new_movepoint.y = oy;
			new_movepoint.attack_to = true;

			waypoint_list.push_back(new_movepoint);

			//doesn't hurt to call this if we don't have minions
			//but needed if we do
			CloneMinionWayPoints();
		}
	}
}

bool ZObject::EstimateMissileTarget(ZObject *target, int &tx, int &ty)
{
	//some checks
	if(missile_speed <= 0) return false;

	//don't need to estimate if
	//the target isn't moving
	if(isz(target->loc.dx) && isz(target->loc.dy)) return false;

	double dx, dy;
	double dx2, dy2;
	int ixo, iyo;
	double xo, yo;
	double x2o, y2o;
	double Cu, Cd;

	dx = target->loc.dx;
	dy = target->loc.dy;
	target->GetCenterCords(ixo, iyo);
	xo = ixo;
	yo = iyo;

	x2o = center_x;
	y2o = center_y;

	Cu = yo - y2o;
	Cd = xo - x2o;

	double a, b, c, d;

	a = (Cu * Cu) + (Cd * Cd);
	b = (2 * Cu * Cd * dy) - (2 * Cu * Cu * dx);
	c = (Cd * Cd * dy * dy) - (2 * Cu * Cd * dx) + (Cu * Cu * dx * dx) - (Cd * Cd * missile_speed * missile_speed);
	d = (b * b) - (4 * a * c);

	if(d <= 0.00001) 
	{
		printf("EstimateMissileTarget::not solvable\n");
		return false;
	}
	if(isz(a))
	{
		printf("EstimateMissileTarget::a is zero?\n");
		return false;
	}

	dx2 = (-1 * b - sqrt(d)) / (2 * a);

	double dy2_guts;

	dy2_guts = (double)(missile_speed * missile_speed) - (dx2 * dx2);

	//printf("dy2_guts:m:%d dx2_1:%lf\n", missile_speed, (-1 * b - sqrt(d)) / (2 * a));
	//printf("dy2_guts:m:%d dx2_2:%lf\n", missile_speed, (-1 * b + sqrt(d)) / (2 * a));

	if(dy2_guts <= 0.00001) 
	{
		//printf("EstimateMissileTarget::trying other dx2\n");

		dx2 = (-1 * b + sqrt(d)) / (2 * a);

		dy2_guts = (missile_speed * missile_speed) - (dx2 * dx2);

		if(dy2_guts <= 0.00001) 
		{
			printf("EstimateMissileTarget::bad dy2_guts value\n");
			return false;
		}
	}

	dy2 = sqrt(dy2_guts);

	double t;
	double dd;

	//dd = dx - dx2;
	if(!isz(dd = dx - dx2))
		t = -1 * Cd / dd;
	else if(!isz(dd = dy - dy2))
		t = -1 * Cu / dd;
	else
	{
		printf("EstimateMissileTarget::bad dd value\n");
		return false;
	}

	//printf("t:%lf\n", t);

	if(t < 0)
	{
		//printf("EstimateMissileTarget::2nd\n");

		dx2 = (-1 * b + sqrt(d)) / (2 * a);

		double dy2_guts;

		dy2_guts = (double)(missile_speed * missile_speed) - (dx2 * dx2);

		if(dy2_guts <= 0.00001) 
		{
			printf("EstimateMissileTarget::bad dy2_guts value 2nd\n");
			return false;
		}

		dy2 = sqrt(dy2_guts);

		//dd = dx - dx2;
		if(!isz(dd = dx - dx2))
			t = -1 * Cd / dd;
		else if(!isz(dd = dy - dy2))
			t = -1 * Cu / dd;
		else
		{
			printf("EstimateMissileTarget::bad dd value\n");
			return false;
		}

		if(t < 0)
		{
			printf("EstimateMissileTarget::bad t value\n");
			return false;
		}
	}

	tx = dx * t + xo;
	ty = dy * t + yo;

	return true;
}

bool ZObject::NearestAttackLoc(int sx, int sy, int &ex, int &ey, int aa_radius, bool is_robot, ZMap &tmap)
{
	int &x = loc.x;
	int &y = loc.y;
	int &cx = center_x;
	int &cy = center_y;

	//line test
	

	//normal checks
	{
		//center ok?
		ex = cx; ey = cy;
		if(tmap.GetPathFinder().ShouldBeAbleToMoveTo(sx, sy, ex, ey, is_robot)) return true;

		//+8 ok?
		ex = x+8; ey = y+8;
		if((ex != cx && ey != cy) && tmap.GetPathFinder().ShouldBeAbleToMoveTo(sx, sy, ex, ey, is_robot)) return true;
	}

	//quick cannon fort checks
	{
		//up 2 tiles ok?
		ex = x+8; ey = (y+8)-32;
		if((ex != cx && ey != cy) && tmap.GetPathFinder().ShouldBeAbleToMoveTo(sx, sy, ex, ey, is_robot)) return true;

		//right 3 tiles ok?
		ex = x+8+48; ey = y+8;
		if((ex != cx && ey != cy) && tmap.GetPathFinder().ShouldBeAbleToMoveTo(sx, sy, ex, ey, is_robot)) return true;

		//left 3 tiles ok?
		ex = (x+8)-48; ey = y+8;
		if((ex != cx && ey != cy) && tmap.GetPathFinder().ShouldBeAbleToMoveTo(sx, sy, ex, ey, is_robot)) return true;
	}

	//full direct line check
	{
		ZPath_Finding_Bresenham ls;
		int lx, ly;

		ls.Init(cx>>4, cy>>4, sx>>4, sy>>4, tmap.GetMapBasics().width, tmap.GetMapBasics().height);

		while(ls.GetNext(lx, ly))
		{
			ex = (lx<<4)+8;
			ey = (ly<<4)+8;

			if(!points_within_distance(cx, cy, ex, ey, aa_radius)) break;

			if(tmap.GetPathFinder().ShouldBeAbleToMoveTo(sx, sy, ex, ey, is_robot)) return true;
		}
	}

	return false;
}

double ZObject::DamagedSpeed()
{
	if(ShowPartiallyDamaged()) return zsettings->partially_damaged_unit_speed;
	if(ShowDamaged()) return zsettings->damaged_unit_speed;

	return 1.0;
}

double ZObject::RunSpeed() 
{
	if(leader_obj)
		return leader_obj->is_running && !ShowDamaged() ? zsettings->run_unit_speed : 1.0; 
	else
		return is_running && !ShowDamaged() ? zsettings->run_unit_speed : 1.0; 
}

bool ZObject::CanReachTargetRunning(int x, int y)
{
	double distance_runnable;

	//an estimate because the real speed can change with terrain
	distance_runnable = move_speed * stamina;

	return points_within_distance(center_x, center_y, x, y, distance_runnable);
}

void ZObject::ProcessRunStamina(double time_dif)
{
	if(is_running)
	{
		stamina -= time_dif;

		if(stamina < 0)
		{
			stamina = 0;
			is_running = false;
		}
	}
	else
	{
		stamina += time_dif * zsettings->run_recharge_rate;

		if(stamina > max_stamina) stamina = max_stamina;
	}
}

void ZObject::AttemptStartRun()
{
	const double min_stamina = 0.3;

	if(is_running) return;

	//one in five we don't run
	if(!(rand() % 5)) return;

	if(stamina < min_stamina) return;

	is_running = true;
}

void ZObject::ProcessAttackDamage(ZMap &tmap, bool attack_player_given)
{
	double &the_time = ztime->ztime;
	if(!attack_object || !damage) return;
	if(the_time < next_damage_time) return;

	next_damage_time = the_time + damage_int_time;

	bool attack_own = false;

	if(!waypoint_list.empty())
	{
	    const waypoint &current_wp = waypoint_list.front();

	    if(current_wp.mode == ATTACK_OWN_WP &&
	       current_wp.player_given &&
	       current_wp.ref_id == attack_object->GetRefID() &&
	       attack_object != this &&
	       !attack_object->IsDestroyed() &&
	       CanAttack() &&
	       (object_type == ROBOT_OBJECT || object_type == VEHICLE_OBJECT ||
            object_type == CANNON_OBJECT) &&
	       owner != NULL_TEAM &&
	       attack_object->GetOwner() == owner &&
	       (HasExplosives() || !attack_object->AttackedOnlyByExplosives()))
	    {
	        unsigned char target_type, target_id;
	        attack_object->GetObjectID(target_type, target_id);

	        attack_own =
	            (target_type == ROBOT_OBJECT || target_type == VEHICLE_OBJECT ||
             target_type == CANNON_OBJECT);
	    }
	}

	if(!attack_own && !this->CanAttackObject(attack_object))
	{
		Disengage();
		return;
	}

	bool can_attack_with_grenades;

	can_attack_with_grenades = (GetGrenadeAmount() || (GetGroupLeader() && GetGroupLeader()->GetGrenadeAmount()));

	if(damage_is_missile || can_attack_with_grenades)
	{
		int tx, ty;
		//int w, h;
		damage_missile new_missile;

		if(!EstimateMissileTarget(attack_object, tx, ty))
			attack_object->GetCenterCords(tx, ty);

		if(can_attack_with_grenades)
		{
			tx += (rand() % 48) - 24;
			ty += (rand() % 48) - 24;

			new_missile.x = tx;
			new_missile.y = ty;
			new_missile.damage = zsettings->grenade_damage * MAX_UNIT_HEALTH;
			new_missile.radius = zsettings->grenade_damage_radius;
			new_missile.team = owner;
			new_missile.attacker_ref_id = ref_id;
			new_missile.attack_player_given = attack_player_given;
			new_missile.target_ref_id = attack_object->GetRefID();
			new_missile.CalcExplodeTimeTo(center_x, center_y, zsettings->grenade_missile_speed, the_time);

			//remove a grenade
			if(GetGrenadeAmount())
			{
				SetGrenadeAmount(GetGrenadeAmount()-1);
				sflags.updated_grenade_amount = true;
			}
			else if(GetGroupLeader() && GetGroupLeader()->GetGrenadeAmount())
			{
				GetGroupLeader()->SetGrenadeAmount(GetGroupLeader()->GetGrenadeAmount()-1);
				sflags.updated_leader_grenade_amount = true;
			}

			//set a different next-time
			next_damage_time = the_time + zsettings->grenade_attack_speed;
		}
		else
		{
			tx += (rand() % 32) - 16;
			ty += (rand() % 32) - 16;

			new_missile.x = tx;
			new_missile.y = ty;
			new_missile.damage = damage;
			new_missile.radius = damage_radius;
			new_missile.team = owner;
			new_missile.attacker_ref_id = ref_id;
			new_missile.attack_player_given = attack_player_given;
			new_missile.target_ref_id = attack_object->GetRefID();
			//new_missile.CalcExplodeTimeTo(loc.x + (width_pix >> 1), loc.y + (height_pix >> 1), missile_speed);
			new_missile.CalcExplodeTimeTo(center_x, center_y, missile_speed, the_time);
		}

		new_missile.attack_own = attack_own;

		damage_missile_list->push_back(new_missile);

		sflags.fired_missile = true;
		sflags.missile_x = tx;
		sflags.missile_y = ty;

		//tell the unit to dodge
		attack_object->DodgeMissile(tx, ty, new_missile.explode_time - the_time);
	}
	else
	{
		if((rand() % 10000) / 10000.0 > damage_chance) return;

		//attack vehicle or driver?
		if(can_snipe && attack_object->CanBeSniped() && ((rand() % 10000) / 10000.0 <= snipe_chance))
		{
			attack_object->DamageDriverHealth(damage);
			sflags.updated_attack_object_driver_health = true;
		}
		else
		{
			attack_object->DamageHealth(damage, tmap);
			sflags.updated_attack_object_health = true;
		}

		//are we a pyro? have to set victom's last hurt by fire time 
		//(so that we can do the melt death effect)
		if(object_type == ROBOT_OBJECT && object_id == PYRO)
			attack_object->SetDamagedByFireTime(the_time);

		//if destroyed give a target destroyed
		if(attack_object->IsDestroyed() && attack_player_given)
		{
			sflags.portrait_anim_ref_id = ref_id;
			sflags.portrait_anim_value = TARGET_DESTROYED_ANIM;
		}
	}
}

void ZObject::ProcessKillObject()
{
    // Buildings no longer repair themselves automatically.
    // Crane repair remains available.

    do_auto_repair = false;

    // Stop production?
    if(ProducesUnits()) StopBuildingProduction();
}

void ZObject::ProcessAgroWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	int ox, oy;
	ZObject *target_object;

	if(is_new)
	{
		//set the agro center
		//cur_wp_info.agro_center_x = x + (width_pix >> 1);
		//cur_wp_info.agro_center_y = y + (height_pix >> 1);
		cur_wp_info.agro_center_x = center_x;
		cur_wp_info.agro_center_y = center_y;
		cur_wp_info.stage = ATTACK_AWS;
	}

	target_object = GetObjectFromID(wp->ref_id, *ols.object_list);

	//we still attacking this target?
	//if(!target_object || 
	//	target_object->IsDestroyed() || 
	//	owner == target_object->GetOwner() ||
	//	(!HasExplosives() && target_object->AttackedOnlyByExplosives()))
	if(!CanAttackObject(target_object))
	{
		KillWP(wp);
		return;
	}

	target_object->GetCenterCords(ox, oy);

	if(!points_within_distance(ox, oy, cur_wp_info.agro_center_x, cur_wp_info.agro_center_y, attack_radius + zsettings->agro_distance))
	{
		//go back to center
		//if we aren't already
		if(cur_wp_info.stage != RETURN_URWS)
		{
			cur_wp_info.stage = RETURN_URWS;
			//cur_wp_info.x = cur_wp_info.agro_center_x;
			//cur_wp_info.y = cur_wp_info.agro_center_y;
			SetTarget(cur_wp_info.agro_center_x, cur_wp_info.agro_center_y);
			SetVelocity();
		}
	}
	else
	{
		//we are within distance
		//so continue the attack if we are not already
		if(cur_wp_info.stage != ATTACK_AWS)
		{
			cur_wp_info.stage = ATTACK_AWS;
			//cur_wp_info.x = ox;
			//cur_wp_info.y = oy;
			SetTarget(ox, oy);
			SetVelocity();
		}
	}

	if(cur_wp_info.stage == ATTACK_AWS)
	{
	//we're there
		//if(WithinAttackRadius(ox, oy))
		if(WithinAttackRadius(target_object))
		{
			StopMove();

			//attack!
			Engage(target_object);
		}
		else
		{
			//hunt down the bastard

			//move there
			//cur_wp_info.x = ox;
			//cur_wp_info.y = oy;
			SetTarget(ox, oy);
			SetVelocity();

			// The legacy AGRO approach moves straight at the target.  If
			// terrain blocks it, cooldown this particular target and let
			// passive target selection choose another instead of reacquiring
			// the same inaccessible cannon over and over.
			int stop_x = 0, stop_y = 0;
			if(!ProcessMove(time_dif, tmap, stop_x, stop_y))
			{
				if(DoAttackImpassableAtCoords(ols, stop_x, stop_y)) return;
				if(!AbandonUnreachableAutoTarget()) KillWP(wp);
				return;
			}
			if(AutoTargetHasStalled(wp->ref_id, 5.0))
			{
				if(!AbandonUnreachableAutoTarget()) KillWP(wp);
				return;
			}
		}
	}
	else //if(cur_wp_info.stage == RETURN_URWS)
	{
		if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols)) return;

		if(!ReachedTarget()) return;

		KillWP(wp);
		return;
	}
}

void ZObject::DeferCurrentPath(double seconds, bool blocked)
{
    if(zod_attack_staging_enabled && blocked && !waypoint_list.empty() &&
       waypoint_list.front().mode == ATTACK_WP &&
       waypoint_list.front().player_given)
    {
        ZODRememberFailedAttackRoute(ref_id,
            waypoint_list.front().ref_id, ztime->ztime);
    }
    if(!waypoint_list.empty() &&
       (waypoint_list.front().mode == ATTACK_WP ||
        waypoint_list.front().mode == ATTACK_OWN_WP ||
        waypoint_list.front().mode == MOVE_WP))
    {
    }
	StopMove();
	cur_wp_info.path_finding_id = 0;
	cur_wp_info.pf_point_list.clear();
	cur_wp_info.got_pf_response = false;
	cur_wp_info.path_failed = true;
	next_path_retry_time = ztime->ztime + seconds;
	if(blocked) force_next_full_path = true;
}

// These checks are shared by ALL mobile vehicle/robot classes using ZObject.
// An automatic attack is either passive AGRO_WP or a temporary ATTACK_WP
// inserted in front of the player's attack_to movement. An explicit enemy
// click (player_given) and bot orders are NOT auto-retargeted here.
bool ZObject::IsAutoAttackWaypoint(const waypoint &wp) const
{
	if(wp.mode == AGRO_WP) return true;
	if(wp.mode != ATTACK_WP || wp.player_given) return false;
	return waypoint_list.size() > 1 &&
		waypoint_list.front().mode == ATTACK_WP &&
		waypoint_list.front().ref_id == wp.ref_id &&
		waypoint_list[1].attack_to;
}

bool ZObject::IsAutoTargetAvoided(int target_ref_id)
{
	const double now = ztime->ztime;
	for(vector< pair<int, double> >::iterator it=auto_target_avoid_until.begin();
		it!=auto_target_avoid_until.end(); )
	{
		if(it->second <= now)
			it = auto_target_avoid_until.erase(it);
		else
		{
			if(it->first == target_ref_id) return true;
			++it;
		}
	}
	return false;
}

void ZObject::AvoidAutoTarget(int target_ref_id)
{
	if(target_ref_id < 0) return;
	const double until = ztime->ztime + 10.0;
	for(vector< pair<int, double> >::iterator it=auto_target_avoid_until.begin();
		it!=auto_target_avoid_until.end(); ++it)
	{
		if(it->first == target_ref_id)
		{
			it->second = until;
			return;
		}
	}
	// Limit per-unit target tracking, including in very large battles.
	if(auto_target_avoid_until.size() >= 8)
		auto_target_avoid_until.erase(auto_target_avoid_until.begin());
	auto_target_avoid_until.push_back(make_pair(target_ref_id, until));
}

bool ZObject::AutoTargetHasStalled(int target_ref_id, double seconds)
{
	// This watch is for automatically selected ATTACK and AGRO waypoints
	// only, never explicit attacks, repair trips or ordinary movement.
	if(waypoint_list.empty() || !IsAutoAttackWaypoint(waypoint_list.front()))
		return false;
	const double now = ztime->ztime;
	if(watched_auto_target_ref_id != target_ref_id)
	{
		watched_auto_target_ref_id = target_ref_id;
		watched_auto_target_x = center_x;
		watched_auto_target_y = center_y;
		watched_auto_target_last_progress = now;
		return false;
	}
	if(abs(center_x - watched_auto_target_x) +
		abs(center_y - watched_auto_target_y) >= 12)
	{
		watched_auto_target_x = center_x;
		watched_auto_target_y = center_y;
		watched_auto_target_last_progress = now;
		return false;
	}
	return now - watched_auto_target_last_progress >= seconds;
}

bool ZObject::AbandonUnreachableAutoTarget()
{
	if(waypoint_list.empty() || !IsAutoAttackWaypoint(waypoint_list.front()))
		return false;
	const int bad_ref_id = waypoint_list.front().ref_id;
	AvoidAutoTarget(bad_ref_id);
	// No async route needs cancelling when called after a matching result
	// or a blocked movement step. Invalidate any old local route state.
	cur_wp_info.clear();
	next_path_retry_time = 0;
	force_next_full_path = false;
	watched_auto_target_ref_id = -1;
	vector<waypoint>::iterator current_wp = waypoint_list.begin();
	KillWP(current_wp);
	return true;
}

// Fase 6Q: independent, dimension-aware firing positions. The older Fase 5
// code required a full MAX(width,height) + 8px empty rectangle around EVERY
// attacker and sampled only a 16px grid. That disproportionately excluded
// soldiers (16px bodies) and especially mixed robot/vehicle armies.
// Physical collision and A* have NOT been changed: this only allocates
// non-overlapping final ATTACK_WP destinations for buildings and cannons.
struct ZAttackPositionSpot { int x, y, clearance_x, clearance_y; };

bool ZObject::FindSpacedStationaryAttackLoc(ZObject *target, ZOLists &ols,
    ZMap &tmap, int &ex, int &ey)
{
    if(!target || !ols.object_list) return false;
    if(target->object_type != BUILDING_OBJECT && target->object_type != CANNON_OBJECT) return false;
    if(object_type != ROBOT_OBJECT && object_type != VEHICLE_OBJECT) return false;
    if(attack_radius < 48) return false;

    // An 8px sample grid permits soldiers to stand ~24px apart instead of
    // being forced to 32px by the old 16px sampling. For tanks, 8px sampling
    // permits ~40px rather than ~48px. Keep >4px safety for arrival snapping.
    const int safe_radius = attack_radius - 8;
    const long safe_radius_sq = (long)safe_radius * safe_radius;
    const long min_radius_sq = 24L * 24L;
    const bool is_robot = (object_type == ROBOT_OBJECT);
    vector<ZAttackPositionSpot> occupied;
    for(vector<ZObject*>::iterator it=ols.object_list->begin();
        it!=ols.object_list->end(); ++it)
    {
        ZObject *peer = *it;
        if(!peer || peer == this || peer->IsDestroyed()) continue;
        if(peer->object_type != ROBOT_OBJECT && peer->object_type != VEHICLE_OBJECT) continue;
        bool friendly = (peer->owner == owner);
        if(!friendly && alliance_matrix &&
           owner > NULL_TEAM && owner < MAX_TEAM_TYPES &&
           peer->owner > NULL_TEAM && peer->owner < MAX_TEAM_TYPES)
            friendly = alliance_matrix[owner][peer->owner];
        if(!friendly) continue;

        // Actual, separate body dimensions: half my body + half theirs,
        // plus a small visual gap. Tanks retain at least 4px extra vertical
        // room; tiny robots are no longer spaced like heavy vehicles.
        const int clearance_x = (width_pix + peer->width_pix + 1)/2 + 3;
        const int clearance_y = (height_pix + peer->height_pix + 1)/2 + 4;
        const int nearby = safe_radius +
            (clearance_x > clearance_y ? clearance_x : clearance_y) + 16;
        const bool standing_near_target =
            !peer->IsMoving() &&
            abs(peer->center_x - target->center_x) <= nearby &&
            abs(peer->center_y - target->center_y) <= nearby;
        if(standing_near_target)
        {
            ZAttackPositionSpot spot = {peer->center_x, peer->center_y,
                clearance_x, clearance_y};
            occupied.push_back(spot);
        }

        // Reserve valid in-flight attack goals to avoid simultaneous attackers
        // asking A* to deliver them to the same firing point. Do not treat
        // stale failed paths or ordinary MOVE goals as firing reservations.
        if(!peer->waypoint_list.empty())
        {
            const waypoint &pwp = peer->waypoint_list.front();
            if((pwp.mode == ATTACK_WP || pwp.mode == ATTACK_OWN_WP) &&
               pwp.ref_id == target->ref_id &&
               !peer->cur_wp_info.path_failed &&
               (peer->cur_wp_info.path_finding_id ||
                peer->cur_wp_info.got_pf_response) &&
               // A shooter stops AS SOON as it reaches weapon range, not
               // necessarily at its planned goal. Reserving both its body
               // and that unused goal permanently wastes firing positions.
               !(standing_near_target && peer->WithinAttackRadius(target)))
            {
                ZAttackPositionSpot spot = {peer->cur_wp_info.init_attack_x,
                    peer->cur_wp_info.init_attack_y, clearance_x, clearance_y};
                occupied.push_back(spot);
            }
        }
    }

    bool found = false;
    long best_score = 0;
    // Offset 4 leaves 8px resolution on BOTH sides of a 16px tile center.
    const int first_x = ((target->center_x - safe_radius) >> 3) * 8 + 4;
    const int last_x  = target->center_x + safe_radius;
    const int first_y = ((target->center_y - safe_radius) >> 3) * 8 + 4;
    const int last_y  = target->center_y + safe_radius;

    // Mark the reserved 8px cells ONCE. Without this, scanning each candidate
    // against every soldier is O(attackers * positions * attackers) and could
    // itself cause a visible pause when ordering 30-100 soldiers to attack.
    const int cells_x = (last_x-first_x)/8 + 1;
    const int cells_y = (last_y-first_y)/8 + 1;
    vector<unsigned char> reserved(cells_x*cells_y,0);
    for(vector<ZAttackPositionSpot>::const_iterator it=occupied.begin();
        it!=occupied.end(); ++it)
    {
        int min_x = (it->x-it->clearance_x-first_x)/8 - 1;
        int max_x = (it->x+it->clearance_x-first_x)/8 + 1;
        int min_y = (it->y-it->clearance_y-first_y)/8 - 1;
        int max_y = (it->y+it->clearance_y-first_y)/8 + 1;
        if(min_x<0) min_x=0;
        if(min_y<0) min_y=0;
        if(max_x>=cells_x) max_x=cells_x-1;
        if(max_y>=cells_y) max_y=cells_y-1;
        for(int iy=min_y; iy<=max_y; ++iy)
        for(int ix=min_x; ix<=max_x; ++ix)
            if(abs(first_x+ix*8-it->x)<it->clearance_x &&
               abs(first_y+iy*8-it->y)<it->clearance_y)
                reserved[iy*cells_x+ix]=1;
    }

    int stop_x=0, stop_y=0;
    for(int iy=0; iy<cells_y; ++iy)
    for(int ix=0; ix<cells_x; ++ix)
    {
        const int px=first_x+ix*8;
        const int py=first_y+iy*8;
        const long rx=(long)px-target->center_x;
        const long ry=(long)py-target->center_y;
        const long dist_sq=rx*rx+ry*ry;
        if(dist_sq>safe_radius_sq || dist_sq<min_radius_sq) continue;

        if(reserved[iy*cells_x+ix]) continue;

        const long dx=(long)px-center_x;
        const long dy=(long)py-center_y;
        const long score=dx*dx+dy*dy;
        if(found && score>=best_score) continue;

        // Unlike the old endpoint check, also check ProcessMove's EXACT
        // physical footprint. A* must not select a point in the fort's wall
        // and then repeatedly fail on the last pixels of the approach.
        if(tmap.WithinImpassable(px-(width_pix>>1)+1,
            py-(height_pix>>1)+1, width_pix-2, height_pix-2,
            stop_x, stop_y, is_robot)) continue;
        if(!tmap.GetPathFinder().ShouldBeAbleToMoveTo(loc.x,loc.y,
            px,py,is_robot)) continue;
        // A* reconstructs robot nodes at tile+8 and vehicle nodes at tile+16.
        // An 8px-sampled goal may differ from that last lattice node. Require
        // the short final leg to be clear for the SAME physical rectangle.
        const int canonical_offset = is_robot ? 8 : 16;
        const int path_x = (px & ~15) + canonical_offset;
        const int path_y = (py & ~15) + canonical_offset;
        if(!ZODWideLaneSegmentClear(tmap,path_x,path_y,px,py,
            width_pix,height_pix)) continue;
        if(!target->IsDestroyableImpass() &&
           tmap.EngageBarrierBetweenCoords(px,py,
               target->center_x,target->center_y)) continue;

        found=true;
        best_score=score;
        ex=px;
        ey=py;
    }
    return found;
}

// Fase 6C: An explicit order to attack a building should bring the ENTIRE
// selected army. A unit with no weapon / explosives cannot shoot that building,
// but can follow the rest of the troops to a passable area OUTSIDE its footprint.
// Called from ZCore::CheckWaypoint during server-side waypoint validation.
// This is never used by automatic target acquisition, nor for ATTACK_OWN_WP.
// No physical pushing, no special-case pathfinder, no fabricated force-move.
bool ZObject::ConvertUnsupportedBuildingAttackToMove(waypoint &wp,
    ZObject *target, ZOLists &ols, ZMap &tmap)
{
    // Only a deliberately ordered attack on a static building (or a hut).
    // Neutral buildings are valid player-selected targets in ZOD as well;
    // Fase 6C accidentally rejected them with target->owner <= NULL_TEAM.
    // Keep own/allied targets out, and never change cannon/vehicle combat.
    if(!target || target->IsDestroyed() || !CanMove() ||
       (object_type != ROBOT_OBJECT && object_type != VEHICLE_OBJECT) ||
       (target->object_type != BUILDING_OBJECT &&
        !(target->object_type == MAP_ITEM_OBJECT && target->object_id == HUT_ITEM)) ||
       owner <= NULL_TEAM || owner >= MAX_TEAM_TYPES ||
       target->owner == owner)
        return false;
    if(target->owner != NULL_TEAM && alliance_matrix &&
       target->owner < MAX_TEAM_TYPES &&
       alliance_matrix[owner][target->owner]) return false;

    int tx, ty, tw, th;
    target->GetCenterCords(tx, ty);
    target->GetDimensionsPixel(tw, th);
    if(tw < 16) tw = 16;
    if(th < 16) th = 16;

    // Margin keeps the helper unit *outside* the target building; the
    // existing Fase 6B parking selection then allocates distinct positions.
    const int outer_x = (tw + width_pix) / 2 + 24;
    const int outer_y = (th + height_pix) / 2 + 24;
    const int dx = center_x - tx;
    const int dy = center_y - ty;
    const int clamped_x = (dx < -tw/2 ? -tw/2 : dx > tw/2 ? tw/2 : dx);
    const int clamped_y = (dy < -th/2 ? -th/2 : dy > th/2 ? th/2 : dy);

    // Try the natural approach side first, then the other sides if a cliff,
    // wall or blocked region prevents getting there. 5 rings = <=80px local
    // parking search per side, avoiding expensive whole-map target scans.
    int side_x[4] = {tx + outer_x, tx - outer_x, tx + clamped_x, tx + clamped_x};
    int side_y[4] = {ty + clamped_y, ty + clamped_y, ty + outer_y, ty - outer_y};
    int sides[4] = {0,1,2,3};
    for(int i=0; i<4; ++i)
        for(int j=i+1; j<4; ++j)
        {
            long di = (long)(side_x[sides[i]]-center_x)*(side_x[sides[i]]-center_x) +
                      (long)(side_y[sides[i]]-center_y)*(side_y[sides[i]]-center_y);
            long dj = (long)(side_x[sides[j]]-center_x)*(side_x[sides[j]]-center_x) +
                      (long)(side_y[sides[j]]-center_y)*(side_y[sides[j]]-center_y);
            if(dj < di) { int temp=sides[i]; sides[i]=sides[j]; sides[j]=temp; }
        }

    int px=0, py=0;
    bool found=false;
    for(int i=0; i<4 && !found; ++i)
        found = FindFreeParkingSpot(side_x[sides[i]], side_y[sides[i]],
            ols, tmap, px, py, 5, false);

    // A bigger army may occupy all of the first few parking rings. Do not
    // silently DROP its support order: search a wider assembly perimeter.
    // The existing footprint/terrain/path check still decides each candidate.
    for(int i=0; i<4 && !found; ++i)
        found = FindFreeParkingSpot(side_x[sides[i]], side_y[sides[i]],
            ols, tmap, px, py, 11, false);
    if(!found)
    {
        return false; // never manufacture a route through a solid building
    }

    wp.mode = MOVE_WP;
    wp.ref_id = -1;
    wp.x = px;
    wp.y = py;
    wp.player_given = false; // internal helper: pre-arrival slot reservation
    wp.attack_to = true;     // still free to respond to OTHER valid enemies
    return true;
}

void ZObject::ProcessAttackWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	int ox, oy;
	ZObject *target_object;

	target_object = GetObjectFromID(wp->ref_id, *ols.object_list);

    // Fase 6H: the only new movement is our temporary building-approach
    // MOVE_WP. It inherits the normal MOVE auto-engagement, but a secondary
    // target must not drag that army across the map. An auto ATTACK_WP made
    // while on the temporary MOVE is at [0], the MOVE at [1], and the
    // original, explicit building attack at [2]. The old waypoint x/y is
    // where that secondary enemy was when first discovered.
    if(zod_attack_staging_enabled && target_object &&
       wp->mode == ATTACK_WP && !wp->player_given &&
       waypoint_list.size() > 2 &&
       waypoint_list[1].mode == MOVE_WP &&
       !waypoint_list[1].player_given && waypoint_list[1].attack_to &&
       waypoint_list[1].ref_id >= 0 &&
       waypoint_list[2].mode == ATTACK_WP &&
       waypoint_list[2].player_given &&
       waypoint_list[2].ref_id == waypoint_list[1].ref_id)
    {
        int secondary_x=0, secondary_y=0;
        target_object->GetCenterCords(secondary_x, secondary_y);
        const int leash = attack_radius + zsettings->agro_distance + 48;
        if(!points_within_distance(wp->x, wp->y,
            secondary_x, secondary_y, leash))
        {
            // An automatic secondary attack may already have an async A*
            // request. Cancel it before removing the temporary waypoint.
            cur_wp_info.clear_and_kill(tmap);
            if(AbandonUnreachableAutoTarget()) return;
        }
    }

	//we still attacking this target?
	//if(!target_object || 
	//	target_object->IsDestroyed() || 
	//	owner == target_object->GetOwner() ||
	//	(!HasExplosives() && target_object->AttackedOnlyByExplosives()))
	bool allowed_attack = false;

	if(wp->mode == ATTACK_OWN_WP)
	{
	    unsigned char target_type, target_id;

	    if(target_object &&
	       target_object != this &&
	       !target_object->IsDestroyed() &&
	       wp->player_given &&
	       CanAttack() &&
	       (object_type == ROBOT_OBJECT || object_type == VEHICLE_OBJECT ||
            object_type == CANNON_OBJECT) &&
	       owner != NULL_TEAM &&
	       target_object->GetOwner() == owner)
	    {
	        target_object->GetObjectID(target_type, target_id);

	        allowed_attack =
	            (target_type == ROBOT_OBJECT || target_type == VEHICLE_OBJECT ||
             target_type == CANNON_OBJECT) &&
	            (HasExplosives() || !target_object->AttackedOnlyByExplosives());
	    }
	}
	else
	    allowed_attack = CanAttackObject(target_object);

	if(!allowed_attack)
	{
        // Fase 6D: robot MINIONS copy their leader's ATTACK_WP directly.
        // They do not go through ZCore::CheckWaypoint individually, so 6C's
        // order-time conversion cannot help them. Handle it here as well.
        // This also lets a unit keep following if it loses its last grenade.
        // Never change automatic attacks or the explicit ATTACK_OWN_WP mode.
        if(wp->mode == ATTACK_WP && wp->player_given &&
           ConvertUnsupportedBuildingAttackToMove(*wp, target_object,
               ols, tmap))
        {
            StopMove();
            cur_wp_info.clear_and_kill(tmap);
            last_wp.clear();    // initialize the new MOVE_WP on the next tick
            sflags.updated_waypoints = true; // relay minion/leader order
            return;
        }
		KillWP(wp);
		return;
	}

	target_object->GetCenterCords(ox, oy);

    // Explicit friendly-fire order on a fixed cannon: do not try to pathfind
    // to an out-of-range target (or continue firing once it leaves range).
    // The order remains queued, so it resumes when the target moves close.
    if(wp->mode == ATTACK_OWN_WP && object_type == CANNON_OBJECT &&
       !WithinAttackRadius(target_object))
    {
        StopMove();
        Disengage();
        return;
    }

	//we're there
	//if(WithinAttackRadius(ox, oy))
	if(WithinAttackRadius(target_object))
	{
		StopMove();
        // Fase 6Q: the firing position was only an APPROACH goal. When
        // weapon range is reached early, release the unused goal and its
        // pending A* request, or subsequent attackers will see two occupied
        // slots for one stationary shooter. Keep the player ATTACK_WP itself.
        if(cur_wp_info.path_finding_id || cur_wp_info.got_pf_response)
            cur_wp_info.clear_and_kill(tmap);

		//attack!
		Engage(target_object);
	}
	else
	{
		//hunt down the bastard

		//are we waiting for a path finding response?
		if(cur_wp_info.path_finding_id)
			return;
		else if(!cur_wp_info.got_pf_response)
		{
			//we are not waiting for waypoints
			//so start doing that
			const bool spaced_stationary_attack =
				(target_object->object_type == BUILDING_OBJECT ||
				 target_object->object_type == CANNON_OBJECT) &&
				(object_type == ROBOT_OBJECT || object_type == VEHICLE_OBJECT) &&
				attack_radius >= 48;
			bool found_attack_loc = false;
			if(spaced_stationary_attack)
				found_attack_loc = FindSpacedStationaryAttackLoc(target_object,
					ols, tmap, cur_wp_info.x, cur_wp_info.y);
			else
				found_attack_loc = target_object->NearestAttackLoc(x, y,
					cur_wp_info.x, cur_wp_info.y, attack_radius,
					(object_type == ROBOT_OBJECT), tmap);

            // Fase 6H/6Q: attack on a STATIC target has two independent jobs:
            //   1) reach the battlefield, 2) claim a free firing position.
            // Previously, failure of (2), or one failed A* route to (2),
            // prevented (1), even on open terrain far from the building.
            // Advance via a normal MOVE_WP and KEEP the player's ATTACK_WP
            // underneath. MOVE_WP already has proven anti-stacking, A*, and
            // optional engagement of nearby enemy robots/vehicles/cannons.
            // No chase of other buildings, no force move, no teleportation.
            bool failed_attack_route = false;
            std::map<int, ZODAttackRouteFailure>::iterator failed_it =
                zod_attack_route_failure.find(ref_id);
            if(failed_it != zod_attack_route_failure.end())
            {
                failed_attack_route =
                    failed_it->second.target_ref == wp->ref_id &&
                    ztime->ztime >= failed_it->second.when &&
                    ztime->ztime - failed_it->second.when <= 6.0;
                zod_attack_route_failure.erase(failed_it);
            }

            if(zod_attack_staging_enabled && wp->mode == ATTACK_WP &&
               wp->player_given && spaced_stationary_attack &&
               (!found_attack_loc || failed_attack_route))
            {
                const double from_x = (double)center_x - ox;
                const double from_y = (double)center_y - oy;
                const double distance = sqrt(from_x*from_x + from_y*from_y);
                // Wait near the battlefield, not 64+ pixels behind the range.
                // Cannons on forts need this exactly like standalone buildings.
                // Keep a 48px deadband to avoid repeat rally/attack oscillation.
                const int stand_off = attack_radius + 24;
                if(distance > (double)stand_off + 48.0)
                {
                    const double ux = from_x / distance;
                    const double uy = from_y / distance;
                    const double diagonal = 0.7071067811865475;
                    // Approach side first, then nearby flanks. The opposite
                    // sides are last-resort paths around impassable terrain.
                    const double dirs[8][2] = {
                        {ux, uy},
                        {(ux+uy)*diagonal, (uy-ux)*diagonal},
                        {(ux-uy)*diagonal, (uy+ux)*diagonal},
                        {uy, -ux}, {-uy, ux},
                        {(-ux+uy)*diagonal, (-uy-ux)*diagonal},
                        {(-ux-uy)*diagonal, (-uy+ux)*diagonal},
                        {-ux, -uy}
                    };
                    int rally_x=0, rally_y=0;
                    bool have_rally=false;
                    for(int side=0; side<8 && !have_rally; ++side)
                    {
                        const int sx = ox + (int)(dirs[side][0] * stand_off);
                        const int sy = oy + (int)(dirs[side][1] * stand_off);
                        int rx=0, ry=0;
                        if(!FindFreeParkingSpot(sx, sy, ols, tmap,
                            rx, ry, 7, false)) continue;
                        const long rdx=(long)rx-ox, rdy=(long)ry-oy;
                        const long min_wait=(long)attack_radius+12;
                        if(rdx*rdx+rdy*rdy < min_wait*min_wait) continue;
                        if(abs(rx-center_x) + abs(ry-center_y) < 48) continue;
                        rally_x=rx; rally_y=ry;
                        have_rally=true;
                    }
                    if(have_rally)
                    {
                        waypoint rally;
                        rally.mode = MOVE_WP;
                        // Keep a reference to the original building so this
                        // temporary move can be cancelled if it is destroyed.
                        rally.ref_id = wp->ref_id;
                        rally.x = rally_x;
                        rally.y = rally_y;
                        rally.player_given = false;
                        rally.attack_to = true;
                        // Original attack remains directly behind this
                        // temporary movement. Secondary auto-attacks use
                        // existing MOVE_WP logic (units/guns, no buildings).
                        waypoint_list.insert(waypoint_list.begin(), rally);
                        StopMove();
                        cur_wp_info.clear_and_kill(tmap);
                        last_wp.clear();
                        parking_valid = false;
                        sflags.updated_waypoints = true;
                        return;
                    }
                }
            }

			if(!found_attack_loc && spaced_stationary_attack)
			{
				// All reachable approach samples are blocked/reserved. Do not
				// send several tanks to the same point inside the target.
				// Keep an explicit attack order alive; auto-targets may retarget.
				if(AbandonUnreachableAutoTarget()) return;
				DeferCurrentPath(1.5);
				return;
			}
			if(!found_attack_loc)
			{
				// Don't chase the centre of an unreachable automatically
				// selected cannon. Preserve direct player ATTACK_WP behavior.
				if(AbandonUnreachableAutoTarget()) return;
				target_object->GetCords(ox, oy);
				//cur_wp_info.x = ox + 8;
				//cur_wp_info.y = oy + 8;
				SetTarget(ox+8, oy+8);
			}
			else
				SetTarget();

			//needed to check if we should recalc a path
			cur_wp_info.init_attack_x = cur_wp_info.x;
			cur_wp_info.init_attack_y = cur_wp_info.y;

			//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + (width_pix >> 1), y + (height_pix >> 1), cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
			//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
			cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + 8, y + 8, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), HasExplosives(), ref_id, force_next_full_path, EstimateRockDemolitionCost());

			//don't wait for thread if it wasn't created
			if(cur_wp_info.path_finding_id)
				StopMove();
			else
			{
				cur_wp_info.got_pf_response = true;
				SetVelocity();
			}

			return;
		}

		//redo path finding?
		{
			int adx, ady;

			adx = ox - cur_wp_info.init_attack_x;
			ady = oy - cur_wp_info.init_attack_y;

			//has it moved too far from our current destination?
			if(abs(adx) > attack_radius || abs(ady) > attack_radius)
			{
				//reset the waypoint
				StopMove();
				cur_wp_info.clear();

				return;
			}
		}

		//move there
		//cur_wp_info.x = ox;
		//cur_wp_info.y = oy;
		SetVelocity();
		// A blocked ATTACK_WP must keep its target/order. Recompute a route
		// instead of deleting the order at the first wall or building.
		int stop_x = 0, stop_y = 0;
		if(!ProcessMove(time_dif, tmap, stop_x, stop_y))
		{
			if(DoAttackImpassableAtCoords(ols, stop_x, stop_y)) return;
			if(AbandonUnreachableAutoTarget()) return;
			DeferCurrentPath(0.9, true);
			return;
		}

		// Even when A* returns a nonempty route, an unreachable firing
		// position can produce an endless stop/replan loop. Only abandon
		// automatic targets after several seconds without real progress.
		if(!cur_wp_info.path_finding_id && AutoTargetHasStalled(wp->ref_id, 5.0))
		{
			if(AbandonUnreachableAutoTarget()) return;
		}

		{
			if(!ReachedTarget()) return;

			//go to the next pf_point, or kill this waypoint?
			if(cur_wp_info.pf_point_list.size())
			{
				//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
				//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
				SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
				SetVelocity();

				cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
			}
			else
			{
				//we are out of waypoints

				//reset the waypoint
				StopMove();
				cur_wp_info.clear();

				/*
				//find new route to target
				if(!target_object->NearestAttackLoc(x, y, cur_wp_info.x, cur_wp_info.y, attack_radius, (object_type == ROBOT_OBJECT), tmap))
				{
					target_object->GetCords(ox, oy);
					//cur_wp_info.x = ox + 8;
					//cur_wp_info.y = oy + 8;
					SetTarget(ox+8,oy+8);
				}
				else
					SetTarget();

				cur_wp_info.got_pf_response = false;
				//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + (width_pix >> 1), y + (height_pix >> 1), cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
				//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
				cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + 8, y + 8, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);

				//don't wait for thread if it wasn't created
				if(cur_wp_info.path_finding_id)
					StopMove();
				else
				{
					cur_wp_info.got_pf_response = true;
					SetVelocity();
				}
				*/
			}
		}
	}
}

void ZObject::ProcessPickupWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	const double z = 0.000001;
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	ZObject *target_object;

	target_object = GetObjectFromID(wp->ref_id, *ols.object_list);

	//can pickup grenades?
	if(!CanPickupGrenades())
	{
		KillWP(wp);
		return;
	}

	//grenades still exist?
	if(!target_object)
	{
		KillWP(wp);
		return;
	}

	//are they grenades?
	unsigned char ot, oid;
	target_object->GetObjectID(ot, oid);
	if(ot != MAP_ITEM_OBJECT || oid != GRENADES_ITEM)
	{
		KillWP(wp);
		return;
	}

	//are we there?
	if(target_object->UnderCursor(center_x, center_y))
	{
		KillWP(wp);

		if(!IsMinion()) 
		{
			sflags.do_pickup_grenade_anim = true;
			sflags.updated_grenade_amount = true;
			SetGrenadeAmount(GetGrenadeAmount() + target_object->GetGrenadeAmount());
			target_object->SetGrenadeAmount(0);
			target_object->SetHealth(0, tmap);

			sflags.delete_grenade_box_ref_id = target_object->GetRefID();
		}
		return;
	}

	//disengage if this is a robot, because they can not fire and walk
	//also setvelocity
	if(is_new)
	{
		//force move waypoints are expected to always go
		//straight to their targets
		//cur_wp_info.x = wp->x;
		//cur_wp_info.y = wp->y;
		SetTarget(wp->x, wp->y);

		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + (width_pix >> 1), y + (height_pix >> 1), wp->x, wp->y, (object_type == ROBOT_OBJECT), ref_id);
		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
		cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + 8, y + 8, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), HasExplosives(), ref_id, force_next_full_path, EstimateRockDemolitionCost());

		//don't wait for thread if it wasn't created
		if(cur_wp_info.path_finding_id)
			StopMove();
		else
		{
			cur_wp_info.got_pf_response = true;
			SetVelocity();
		}

		//run to pick things up
		AttemptStartRun(wp->x, wp->y);
	}

	//attack to
	if(CheckAttackTo(wp, ols)) return;

	if(!cur_wp_info.got_pf_response) return;

	//did we halt movement?
	if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols)) return;

	if(!ReachedTarget()) return;

	//go to the next pf_point, or kill this waypoint?
	if(cur_wp_info.pf_point_list.size())
	{
		//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
		//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
		SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
		SetVelocity();

		cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
	}
	else
		KillWP(wp);
}

void ZObject::ProcessEnterWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	const double z = 0.000001;
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	ZObject *target_object;

	if(is_new)
	{
		//cur_wp_info.x = wp->x;
		//cur_wp_info.y = wp->y;
		SetTarget(wp->x, wp->y);

		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + (width_pix >> 1), y + (height_pix >> 1), wp->x, wp->y, (object_type == ROBOT_OBJECT), ref_id);
		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
		cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + 8, y + 8, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), HasExplosives(), ref_id, force_next_full_path, EstimateRockDemolitionCost());

		//don't wait for thread if it wasn't created
		if(cur_wp_info.path_finding_id)
			StopMove();
		else
		{
			cur_wp_info.got_pf_response = true;
			SetVelocity();
		}

		//if(object_type == ROBOT_OBJECT) Disengage();

		//run to enter vehicles
		AttemptStartRun(wp->x, wp->y);
	}

	target_object = GetObjectFromID(wp->ref_id, *ols.object_list);

	if(!target_object || !target_object->CanBeEntered())//target_object->GetOwner() != NULL_TEAM || target_object->IsDestroyed())
	{
		KillWP(wp);
		return;
	}

	int &cx = center_x;
	int &cy = center_y;

	//cx = x + (width_pix >> 1);
	//cy = y + (height_pix >> 1);

	//are we at the target?
	if(target_object->UnderCursor(cx, cy))
	{
		KillWP(wp);

		if(!IsMinion())
		{
			sflags.entered_target_ref_id = target_object->GetRefID();
		}
		return;
	}

	//attack to
	if(CheckAttackTo(wp, ols)) return;

	//don't actually move without the pf_waypoints
	if(!cur_wp_info.got_pf_response) return;

	//did we halt movement?
	if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols)) return;

	if(!ReachedTarget()) return;

	//go to the next pf_point, or kill this waypoint?
	if(cur_wp_info.pf_point_list.size())
	{
		//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
		//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
		SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
		SetVelocity();

		cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
	}
	else
		KillWP(wp);
}

void ZObject::ProcessDodgeWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	const double z = 0.000001;
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;

	if(is_new)
	{
		//cur_wp_info.x = wp->x;
		//cur_wp_info.y = wp->y;
		SetTarget(wp->x, wp->y);

		SetVelocity();

		AttemptStartRun();
	}

	//did we halt movement?
	if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols)) return;

	if(!ReachedTarget()) return;

	KillWP(wp);
}

void ZObject::ProcessMoveWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap, bool stoppable)
{
	const double z = 0.000001;
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;

    // Fase 6H: only our tagged, temporary building-approach move has
    // a building ref_id and a matching explicit ATTACK_WP directly behind it.
    // Drop the approach if the building has already been destroyed. Do not
    // change player MOVE_WP, rally points, or ordinary auto-attack behavior.
    if(zod_attack_staging_enabled && wp->mode == MOVE_WP &&
       !wp->player_given && wp->attack_to && wp->ref_id >= 0 &&
       waypoint_list.size() > 1 &&
       waypoint_list[1].mode == ATTACK_WP &&
       waypoint_list[1].ref_id == wp->ref_id &&
       waypoint_list[1].player_given)
    {
        ZObject *primary = GetObjectFromID(wp->ref_id, *ols.object_list);
        if(!primary || primary->IsDestroyed())
        {
            KillWP(wp);
            return;
        }
    }

	//disengage if this is a robot, because they can not fire and walk
	//also setvelocity
	if(is_new)
	{
		//force move waypoints are expected to always go
		//straight to their targets
		if(stoppable)
		{
			int px = wp->x, py = wp->y;
            // Fase 6J: a MOVE aimed at a capturable flag is different from
            // ordinary parking.  The server captures a flag only when a
            // mobile unit ACTUALLY overlaps its 16x16 body.  With Fase 6B
            // spreading, a whole group can stop just OUTSIDE that body.
            // Reserve one real flag-crossing goal for the nearest ordered
            // unit; all other units retain the normal parking rules.
            // No changes to capture radius, A*, movement, or non-flag moves.
            ZObject *capture_flag = NULL;
            if(wp->ref_id >= 0 && ols.object_list &&
               (object_type == VEHICLE_OBJECT || object_type == ROBOT_OBJECT))
            {
                ZObject *candidate = GetObjectFromID(wp->ref_id, *ols.object_list);
                if(candidate && candidate->GetOwner() != owner)
                {
                    unsigned char flag_type, flag_id;
                    candidate->GetObjectID(flag_type, flag_id);
                    if(flag_type == MAP_ITEM_OBJECT && flag_id == FLAG_ITEM)
                    {
                        // Respect the same alliance rule as automatic flag grab.
                        const team_type flag_owner = candidate->GetOwner();
                        if(!(alliance_matrix &&
                             owner > NULL_TEAM && owner < MAX_TEAM_TYPES &&
                             flag_owner > NULL_TEAM && flag_owner < MAX_TEAM_TYPES &&
                             alliance_matrix[owner][flag_owner]))
                            capture_flag = candidate;
                    }
                }
            }

			// Plan a non-overlapping resting place BEFORE A* starts.
			// A RETRY of this exact order reuses its previous destination;
			// it is not a new player click. Validate the old slot still has
			// no stationary/reserved occupant and is still reachable/passable.
			// A zero-ring query is cheap and preserves Fase 6B anti-stacking.
			bool have_parking = false;

            if(scuffle_parking_slots_enabled && capture_flag)
            {
                int fx, fy;
                capture_flag->GetCenterCords(fx, fy);
                const double dx = (double)center_x - fx;
                const double dy = (double)center_y - fy;
                const double my_dist2 = dx*dx + dy*dy;
                bool primary = true;

                // No new state or packets: the closest same-team unit with
                // an active MOVE to THIS flag has capture priority.
                for(vector<ZObject*>::iterator i=ols.mobile_olist.begin();
                    i!=ols.mobile_olist.end(); ++i)
                {
                    ZObject *other = *i;
                    if(!other || other == this || other->IsDestroyed() ||
                       other->GetOwner() != owner) continue;
                    vector<waypoint> &other_wp = other->GetWayPointList();
                    if(other_wp.empty() || other_wp.front().mode != MOVE_WP ||
                       other_wp.front().ref_id != wp->ref_id) continue;
                    int ox, oy;
                    other->GetCenterCords(ox, oy);
                    const double odx = (double)ox - fx;
                    const double ody = (double)oy - fy;
                    const double other_dist2 = odx*odx + ody*ody;
                    if(other_dist2 < my_dist2 ||
                       (other_dist2 == my_dist2 && other->GetRefID() < ref_id))
                    {
                        primary = false;
                        break;
                    }
                }

                if(primary)
                {
                    // Every candidate overlaps the real 16px flag rectangle.
                    // Check the FULL unit footprint and reachable map region,
                    // so a flag near a wall cannot pull a tank through it.
                    static const int offsets[][2] = {
                        {0,0}, {0,-8}, {0,8}, {-8,0}, {8,0},
                        {-8,-8}, {8,-8}, {-8,8}, {8,8},
                        {0,-16}, {0,16}, {-16,0}, {16,0}
                    };
                    const bool is_robot = object_type == ROBOT_OBJECT;
                    for(unsigned int k=0; k<sizeof(offsets)/sizeof(offsets[0]); ++k)
                    {
                        if(abs(offsets[k][0]) >= (width_pix+16)/2 ||
                           abs(offsets[k][1]) >= (height_pix+16)/2)
                            continue;
                        const int tx = fx + offsets[k][0];
                        const int ty = fy + offsets[k][1];
                        int block_x=0, block_y=0;
                        if(tmap.WithinImpassable(tx-(width_pix>>1)+1,
                             ty-(height_pix>>1)+1, width_pix-2,
                             height_pix-2, block_x, block_y, is_robot))
                            continue;
                        if(!tmap.GetPathFinder().ShouldBeAbleToMoveTo(
                             loc.x, loc.y, tx, ty, is_robot))
                            continue;
                        px = tx; py = ty;
                        have_parking = true;
                        parking_valid = true;
                        parking_order_x = wp->x;
                        parking_order_y = wp->y;
                        parking_target_x = px;
                        parking_target_y = py;
                        break;
                    }
                }
            }

			if(scuffle_parking_slots_enabled && !have_parking && parking_valid &&
			   parking_order_x == wp->x && parking_order_y == wp->y)
			{
				px = parking_target_x; py = parking_target_y;
				int verify_x=0, verify_y=0;
				have_parking = FindFreeParkingSpot(px, py, ols, tmap,
					verify_x, verify_y, 0, false);
				if(!have_parking)
                {
                    parking_valid = false;
                    px = wp->x;
                    py = wp->y;
                }
			}
			if(scuffle_parking_slots_enabled && !have_parking &&
			   FindFreeParkingSpot(wp->x, wp->y, ols, tmap,
				px, py, 12, false))
			{
				parking_valid = true;
				parking_order_x = wp->x;
				parking_order_y = wp->y;
				parking_target_x = px;
				parking_target_y = py;
			}
			SetTarget(px, py);

			//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + (width_pix >> 1), y + (height_pix >> 1), wp->x, wp->y, (object_type == ROBOT_OBJECT), ref_id);
			//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
			cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + 8, y + 8, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), HasExplosives(), ref_id, force_next_full_path, EstimateRockDemolitionCost());


			//don't wait for thread if it wasn't created
			if(cur_wp_info.path_finding_id)
				StopMove();
			else
			{
				cur_wp_info.got_pf_response = true;
				SetVelocity();
			}
		}
		else
		{
			//cur_wp_info.x = wp->x;
			//cur_wp_info.y = wp->y;
			SetTarget(wp->x, wp->y);
			SetVelocity();
		}

		//if(object_type == ROBOT_OBJECT) Disengage();

		//run if we are going for a flag
		{
			for(vector<ZObject*>::iterator obj=ols.flag_olist.begin()++;obj!=ols.flag_olist.end();obj++)
				if((*obj)->DistanceFromCoords(wp->x, wp->y) <= 32)
				{
					AttemptStartRun(wp->x, wp->y);
					break;
				}
		}
	}

	        //attack to
        if(CheckAttackTo(wp, ols)) return;

        // Retry blocked MOVE_WP orders without deleting the waypoint.
        if(stoppable && next_path_retry_time > 0)
        {
                if(ztime->ztime < next_path_retry_time)
                        return;

                next_path_retry_time = 0;
                last_wp.clear();
                StopMove();
                return;
        }

        if(!cur_wp_info.got_pf_response && stoppable)
                return;

        if(stoppable)
        {
                int stop_x = 0;
                int stop_y = 0;

                if(!ProcessMove(time_dif, tmap, stop_x, stop_y, true))
                {
                        if(DoAttackImpassableAtCoords(ols, stop_x, stop_y))
                                return;

                        // stop_x/y is origin of blocked tile in map pixels.
                        StopMove();
                        cur_wp_info.clear_and_kill(tmap);
                        force_next_full_path = true;
                        next_path_retry_time = ztime->ztime + 0.5;
                        return;
                }
        }
        else
        {
                if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols, false))
                        return;
        }


	if(!ReachedTarget()) return;

	//go to the next pf_point, or kill this waypoint?
	if(cur_wp_info.pf_point_list.size())
	{
		//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
		//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
		SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
		SetVelocity();

		cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
	}
        else
        {
                KillWP(wp);
        }
}

void ZObject::ProcessEnterFortWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	ZObject *target_object;
	bool stoppable;

	target_object = GetObjectFromID(wp->ref_id, ols.building_olist);

	//target still exist?
	//target still need repaired?
	if(!target_object)
	{
		KillWP(wp);
		return;
	}

	if(is_new)
	{
		cur_wp_info.stage = GOTO_ENTRANCE_EFWS;

		//goto entrace
		if(target_object->GetBuildingCreationMovePoint(cur_wp_info.x, cur_wp_info.y))
		{
			//these will be our exit cords for the last stage
			cur_wp_info.fort_exit_x = cur_wp_info.x;
			cur_wp_info.fort_exit_y = cur_wp_info.y;

			SetTarget();

			//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
			cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + 8, y + 8, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), HasExplosives(), ref_id, force_next_full_path);

			//don't wait for thread if it wasn't created
			if(cur_wp_info.path_finding_id)
				StopMove();
			else
			{
				cur_wp_info.got_pf_response = true;
				SetVelocity();
			}
		}
		else
		{
			//didn't get the entrance points?
			KillWP(wp);
			return;
		}
	}

	if(!target_object->CanEnterFort(owner))
	{
		if(cur_wp_info.stage == GOTO_ENTRANCE_EFWS)
		{
			KillWP(wp);
			return;
		}
		else if(cur_wp_info.stage == ENTER_BUILDING_EFWS)
		{
			//if the fort gets destroyed while we are entering it
			//then execute the exit stage of this WP
			//cur_wp_info.x = cur_wp_info.fort_exit_x;
			//cur_wp_info.y = cur_wp_info.fort_exit_y;
			SetTarget(cur_wp_info.fort_exit_x, cur_wp_info.fort_exit_y);

			cur_wp_info.stage = EXIT_BUILDING_EFWS;
			SetVelocity();
		}
	}

	//attack to
	if(CheckAttackTo(wp, ols)) return;

	//don't move if we do not have a response
	if(!cur_wp_info.got_pf_response) return;

	switch(cur_wp_info.stage)
	{
		case GOTO_ENTRANCE_EFWS:
			stoppable = true;
			break;
		case ENTER_BUILDING_EFWS:
		case EXIT_BUILDING_EFWS:
			stoppable = false;
			break;
	}

	if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols, stoppable)) return;

	if(!ReachedTarget()) return;

	//so we reached our current target... now enter the next stage
	switch(cur_wp_info.stage)
	{
	case GOTO_ENTRANCE_EFWS:
		//go to the next pf_point, or next stage?
		if(cur_wp_info.pf_point_list.size())
		{
			//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
			//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
			SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
			SetVelocity();

			cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
		}
		else
		{
			if(!target_object->GetBuildingCreationPoint(cur_wp_info.x, cur_wp_info.y))
				KillWP(wp);
			else
			{
				SetTarget();
				SetVelocity();

				cur_wp_info.stage = ENTER_BUILDING_EFWS;
			}
		}
		break;
	case ENTER_BUILDING_EFWS:
		//cur_wp_info.x = cur_wp_info.fort_exit_x;
		//cur_wp_info.y = cur_wp_info.fort_exit_y;
		SetTarget(cur_wp_info.fort_exit_x, cur_wp_info.fort_exit_y);

		cur_wp_info.stage = EXIT_BUILDING_EFWS;

		//destroy the fort
		sflags.destroy_fort_building_ref_id = target_object->GetRefID();

		SetVelocity();
		break;
	case EXIT_BUILDING_EFWS:
		KillWP(wp);
		break;
	default:
		KillWP(wp);
		break;
	}
}

void ZObject::ProcessCraneRepairWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	const double z = 0.000001;
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	ZObject *target_object;
	bool stoppable;

	target_object = GetObjectFromID(wp->ref_id, ols.building_olist);

	//target still exist?
	//target still need repaired?
	if(!target_object)
	{
		KillWP(wp);
		return;
	}

    unsigned char repaired_type, repaired_id;
    target_object->GetObjectID(repaired_type, repaired_id);
    const bool is_fort = (repaired_type == BUILDING_OBJECT &&
        (repaired_id == FORT_FRONT || repaired_id == FORT_BACK));

	//begin movement towards the entrance
	if(is_new)
	{
		int ent_x, ent_y, ent_x2, ent_y2;

		cur_wp_info.stage = GOTO_ENTRANCE_CRWS;

		if(target_object->GetCraneEntrance(ent_x, ent_y, ent_x2, ent_y2))
		{
			if(ent_x == ent_x2 && ent_y == ent_y2)
			{
				//building has one entrance
				//cur_wp_info.x = ent_x;
				//cur_wp_info.y = ent_y;
				SetTarget(ent_x, ent_y);
			}
			else
			{
				//bridge has two entrances, which is closer?
				double d1, d2;

				d1 = sqrt(pow((double)(x - ent_x), 2) + pow((double)(y - ent_y), 2));
				d2 = sqrt(pow((double)(x - ent_x2), 2) + pow((double)(y - ent_y2), 2));

				if(d1 < d2)
				{
					//cur_wp_info.x = ent_x;
					//cur_wp_info.y = ent_y;
					SetTarget(ent_x, ent_y);
				}
				else
				{
					//cur_wp_info.x = ent_x2;
					//cur_wp_info.y = ent_y2;
					SetTarget(ent_x2, ent_y2);
				}
			}
		}
		else //could not get building entrance info?
		{
			KillWP(wp);
			return;
		}

		//these will be our exit cords for the last stage
		cur_wp_info.crane_exit_x = cur_wp_info.x;
		cur_wp_info.crane_exit_y = cur_wp_info.y;

		//SetVelocity();

		//find our way to the entrance
		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + (width_pix >> 1), y + (height_pix >> 1), cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
		cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + 8, y + 8, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), HasExplosives(), ref_id, force_next_full_path);

		//don't wait for thread if it wasn't created
		if(cur_wp_info.path_finding_id)
			StopMove();
		else
		{
			cur_wp_info.got_pf_response = true;
			SetVelocity();
		}
	}

	if(!target_object->CanBeRepairedByCrane(owner))
	{
		if(cur_wp_info.stage == GOTO_ENTRANCE_CRWS)
		{
			KillWP(wp);
			return;
		}
		else if(cur_wp_info.stage == ENTER_BUILDING_CRWS ||
                (is_fort && cur_wp_info.stage == WAIT_CRWS))
		{
			//if the building gets repaired while we are entering it
			//then execute the exit stage of this WP
			//cur_wp_info.x = cur_wp_info.crane_exit_x;
			//cur_wp_info.y = cur_wp_info.crane_exit_y;
			SetTarget(cur_wp_info.crane_exit_x, cur_wp_info.crane_exit_y);

			cur_wp_info.stage = EXIT_BUILDING_CRWS;
			SetVelocity();
		}
	}

    // Hold position for a configured amount of work time. The crew animation
    // continues independently during this pause; neither target footprint nor
    // entrance-to-center driving distance changes this duration.
    if(cur_wp_info.stage == WAIT_CRWS)
    {
        StopMove();
        if(ztime->ztime < cur_wp_info.crane_work_until) return;

        // The fort now uses the same controlled exit leg as other buildings.
        // Keep the existing work duration; repair only once we are outside.
        SetTarget(cur_wp_info.crane_exit_x, cur_wp_info.crane_exit_y);
        cur_wp_info.stage = EXIT_BUILDING_CRWS;
        SetVelocity();
        return;
    }

	//don't move if we do not have a response
	if(!cur_wp_info.got_pf_response) return;

	switch(cur_wp_info.stage)
	{
		case GOTO_ENTRANCE_CRWS:
			stoppable = true;
			break;
		case ENTER_BUILDING_CRWS:
		case EXIT_BUILDING_CRWS:
			stoppable = false;
			break;
	}

	if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols, stoppable)) return;

	if(!ReachedTarget()) return;

	//so we reached our current target... now enter the next stage
	switch(cur_wp_info.stage)
	{
	case GOTO_ENTRANCE_CRWS:
		//go to the next pf_point, or next stage?
		if(cur_wp_info.pf_point_list.size())
		{
			//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
			//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
			SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
			SetVelocity();

			cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
		}
        else
        {
            // A fort uses its real doorway/creation point. As for other
            // buildings, only this final entry/exit leg is scripted; the
            // approach still uses the normal collision-aware pathfinder.
            if(!target_object->GetCraneCenter(cur_wp_info.x, cur_wp_info.y))
                KillWP(wp);
            else
            {
                if(is_fort)
                {
                    // The normal scripted movement ignores the building's
                    // wall mask, but still gets its speed from the ground.
                    // On an unusually placed map, impassable ground under
                    // the doorway would stop the crane inside the fort.
                    // Retain the old outside repair in that specific case.
                    const int ex = cur_wp_info.x, ey = cur_wp_info.y;
                    const int dx = ex - center_x, dy = ey - center_y;
                    const int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                    for(int i = 0; i <= steps; ++i)
                    {
                        const int px = center_x + (steps ? dx*i/steps : 0);
                        const int py = center_y + (steps ? dy*i/steps : 0);
                        if(tmap.GetTileWalkSpeed(px, py) <= 0.0)
                        {
                            cur_wp_info.x = cur_wp_info.crane_exit_x;
                            cur_wp_info.y = cur_wp_info.crane_exit_y;
                            break;
                        }
                    }
                }
                cur_wp_info.stage = ENTER_BUILDING_CRWS;
                SetTarget();
                SetVelocity();
                sflags.set_crane_anim = true;
                sflags.crane_anim_on = true;
                sflags.crane_rep_ref_id = wp->ref_id;
            }
        }
		break;
    case ENTER_BUILDING_CRWS:
    {
        cur_wp_info.stage = WAIT_CRWS;
        cur_wp_info.crane_work_until = ztime->ztime +
            CraneRepairWorkSeconds(repaired_id);
        StopMove();
        break;
    }
	case EXIT_BUILDING_CRWS:
		//set the building to auto repair immediately...
        // If another crane repaired the fort (or its ownership changed)
        // during our visit, finish exiting without performing a second repair.
        if(!is_fort || target_object->CanBeRepairedByCrane(owner))
        {
            target_object->do_auto_repair = true;
            target_object->next_auto_repair_time = 0;
        }

		sflags.set_crane_anim = true;
		sflags.crane_anim_on = false;
		sflags.crane_rep_ref_id = wp->ref_id;
		KillWP(wp);
		break;
	default:
		KillWP(wp);
		break;
	}
}

void ZObject::ProcessUnitRepairWP(vector<waypoint>::iterator &wp, double time_dif, bool is_new, ZOLists &ols, ZMap &tmap)
{
	const double z = 0.000001;
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	ZObject *target_object;
	bool stoppable;
	int ent_x, ent_y;

	target_object = GetObjectFromID(wp->ref_id, ols.building_olist);

	//target still exist?
	if(!target_object)
	{
		KillWP(wp);
		return;
	}

	//begin movement towards the entrance
	if(is_new)
	{
		cur_wp_info.stage = GOTO_ENTRANCE_URWS;

		if(target_object->GetRepairEntrance(ent_x, ent_y))
		{
			cur_wp_info.x = ent_x;
			cur_wp_info.y = ent_y;
			SetTarget(ent_x, ent_y);
		}
		else
		{
			KillWP(wp);
			return;
		}

		//SetVelocity();

		//find our way to the entrance
		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(x + (width_pix >> 1), y + (height_pix >> 1), cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
		//cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(center_x, center_y, cur_wp_info.x, cur_wp_info.y, (object_type == ROBOT_OBJECT), ref_id);
		// Repair-specific: if the EXISTING doorway is directly reachable with
		// the real unit collision rectangle, use the established repair stages.
		// This avoids an A* no-route response preventing units already at the
		// entrance from entering. All obstructed/long routes still use A*.
		if(RepairEntranceClearFromCurrentPosition(tmap, x, y, center_x, center_y,
			cur_wp_info.x, cur_wp_info.y, width_pix, height_pix,
			(object_type == ROBOT_OBJECT)))
		{
			cur_wp_info.path_finding_id = 0;
			cur_wp_info.got_pf_response = true;
			cur_wp_info.path_failed = false;
			force_next_full_path = false;
			SetVelocity();
		}
		else
		{
			// The player's real-map debug log shows that a fixed point 16px
			// SOUTH of Repair is blocked, causing every full A* route to fail.
			// Choose an A*-passable point with a collision-checked final leg
			// to the UNCHANGED doorway. Never drive through a wall on failure.
			int staging_x = 0, staging_y = 0;
			if(!RepairChooseStagingPoint(tmap, cur_wp_info.x, cur_wp_info.y,
				width_pix, height_pix, (object_type == ROBOT_OBJECT),
				staging_x, staging_y))
			{
				DeferCurrentPath(1.5);
				return;
			}
			cur_wp_info.path_finding_id = tmap.GetPathFinder().Find_Path(
				x + 8, y + 8, staging_x, staging_y,
				(object_type == ROBOT_OBJECT), HasExplosives(), ref_id,
				true);
			if(cur_wp_info.path_finding_id)
				StopMove();
			else
			{
				cur_wp_info.got_pf_response = true;
				SetVelocity();
			}
		}
	}

	//still ok to do this wp?
	if(!target_object->CanRepairUnit(owner) || !CanBeRepaired())
	{
		//if the building can not repair us anymore (got destroyed or changed teams?)
		//then if we are entering this building we must now leave it
		if(cur_wp_info.stage == ENTER_BUILDING_URWS)
		{
			if(target_object->GetRepairEntrance(ent_x, ent_y))
			{
				cur_wp_info.x = ent_x;
				cur_wp_info.y = ent_y;
				SetTarget(ent_x, ent_y);
			}
			else
			{
				KillWP(wp);
				return;
			}
			cur_wp_info.stage = EXIT_BUILDING_URWS;
			SetVelocity();
		}
		else
		{
			KillWP(wp);
			return;
		}
	}

	//attack to
	if(CheckAttackTo(wp, ols)) return;

	//don't move if we do not have a response
	if(!cur_wp_info.got_pf_response) return;

	//do we need to wait?
	if(target_object->RepairingAUnit())
	{
		//if the building can repair us but it is repairing someone else,
		//then we will wait if we are in the wait state
		//and we will leave the building if we are currently entering it
		switch(cur_wp_info.stage)
		{
			case ENTER_BUILDING_URWS:
				if(target_object->GetRepairEntrance(ent_x, ent_y))
				{
					//cur_wp_info.x = ent_x;
					//cur_wp_info.y = ent_y;
					SetTarget(ent_x, ent_y);
				}
				else
				{
					KillWP(wp);
					return;
				}
				cur_wp_info.stage = EXIT_BUILDING_URWS;
				SetVelocity();
				break;
			case WAIT_URWS:
				return;
				break;
		}
	}

	switch(cur_wp_info.stage)
	{
		case GOTO_ENTRANCE_URWS:
			stoppable = true;
			break;
		case ENTER_BUILDING_URWS:
		case EXIT_BUILDING_URWS:
			stoppable = false;
			break;
		default:
			stoppable = true;
			break;
	}

	if(cur_wp_info.stage == GOTO_ENTRANCE_URWS)
	{
		int stop_x = 0, stop_y = 0;
		if(!ProcessMove(time_dif, tmap, stop_x, stop_y, true))
		{
			if(DoAttackImpassableAtCoords(ols, stop_x, stop_y)) return;
			DeferCurrentPath(0.9, true);
			return;
		}
	}
	else if(!ProcessMoveOrKillWP(time_dif, tmap, wp, ols, stoppable))
		return;

	if(!ReachedTarget()) return;

	switch(cur_wp_info.stage)
	{
	case GOTO_ENTRANCE_URWS:
		//go to the next pf_point, or next stage?
		if(cur_wp_info.pf_point_list.size())
		{
			//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
			//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
			SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
			SetVelocity();

			cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
		}
		else
		{
			cur_wp_info.stage = WAIT_URWS;
			SetVelocity();
		}
		break;
	case EXIT_BUILDING_URWS:
		cur_wp_info.stage = WAIT_URWS;
		SetVelocity();
		break;
	case ENTER_BUILDING_URWS:
		sflags.entered_repair_building_ref_id = target_object->GetRefID();
		
		//set this to leave the building incase it isn't accepted
		//because of another unit entering at the exact same interval / "time"
		if(target_object->GetRepairEntrance(ent_x, ent_y))
		{
			//cur_wp_info.x = ent_x;
			//cur_wp_info.y = ent_y;
			SetTarget(ent_x, ent_y);
		}
		else
		{
			KillWP(wp);
			return;
		}
		cur_wp_info.stage = EXIT_BUILDING_URWS;
		SetVelocity();
		break;
	case WAIT_URWS:
		if(!target_object->GetRepairCenter(cur_wp_info.x, cur_wp_info.y))
			KillWP(wp);

		cur_wp_info.stage = ENTER_BUILDING_URWS;

		SetTarget();
		SetVelocity();
		break;
	default:
		KillWP(wp);
		break;
	}
}

bool ZObject::CheckAttackTo(vector<waypoint>::iterator &wp, ZOLists &ols)
{
	if(wp->attack_to)
	{
		if(!CanOverwriteWP()) return false;

		//look for attack choices
		vector<ZObject*> agro_choices;
		for(vector<ZObject*>::iterator obj=ols.passive_engagable_olist.begin();obj!=ols.passive_engagable_olist.end();obj++)
		{
			int ox, oy;
			unsigned char ot, oid;

			(*obj)->GetObjectID(ot, oid);
			(*obj)->GetCenterCords(ox, oy);

			if((*obj)->GetOwner() != NULL_TEAM && (*obj)->GetOwner() != owner && CanAttackObject(*obj))
			{
				//do not auto attack buildings
				if(!(ot == CANNON_OBJECT || ot == VEHICLE_OBJECT || ot == ROBOT_OBJECT)) continue;

                    // Fase 6Q: a temporary approach MOVE toward a cannon
                    // must not auto-attack the SAME cannon again. Otherwise
                    // it interrupts its own staging while all slots are full.
                    // Other nearby enemies are still handled normally.
                    if(zod_attack_staging_enabled && ot == CANNON_OBJECT &&
                       wp->mode == MOVE_WP && !wp->player_given &&
                       wp->attack_to && wp->ref_id == (*obj)->GetRefID() &&
                       waypoint_list.size() > 1 &&
                       waypoint_list[1].mode == ATTACK_WP &&
                       waypoint_list[1].player_given &&
                       waypoint_list[1].ref_id == wp->ref_id) continue;

				//push it?
				//if(WithinAgroRadius(ox, oy)) agro_choices.push_back(*obj);
				if(WithinAgroRadius(*obj) && !IsAutoTargetAvoided((*obj)->GetRefID()))
					agro_choices.push_back(*obj);
			}
		}

		//choose a random agro choice if we have one
		if(agro_choices.size())
		{
			ZObject *agro_choice;
			int ox, oy;

			//find choice (closest)
			agro_choice = agro_choices[rand() % agro_choices.size()];

			agro_choice->GetCenterCords(ox, oy);

			waypoint new_waypoint;

			new_waypoint.mode = ATTACK_WP;
			new_waypoint.ref_id = agro_choice->GetRefID();
			new_waypoint.x = ox;
			new_waypoint.y = oy;

			//push this attack waypoint to the front,
			//and exit this current waypoint
			waypoint_list.insert(waypoint_list.begin(), new_waypoint);

			return true;
		}
	}

	return false;
}

void ZObject::KillWP(vector<waypoint>::iterator &wp)
{
	sflags.updated_waypoints = true;
	waypoint_list.erase(wp);

	StopMove();
	//SetVelocity();
	//sflags.updated_location = true;

	//clean this
	last_wp.clear();
}

bool ZObject::CanOverwriteWP()
{
	vector<waypoint>::iterator wp;

	//check current wp
	if(waypoint_list.size())
	{
		wp = waypoint_list.begin();

		switch(wp->mode)
		{
		case FORCE_MOVE_WP:
			return false;
			break;
		case CRANE_REPAIR_WP:
			if(cur_wp_info.stage != GOTO_ENTRANCE_CRWS)
				return false;
			break;
		case UNIT_REPAIR_WP:
			if(cur_wp_info.stage != GOTO_ENTRANCE_URWS && cur_wp_info.stage != WAIT_URWS)
				return false;
			break;
		case ENTER_FORT_WP:
			if(cur_wp_info.stage != GOTO_ENTRANCE_EFWS)
				return false;
			break;
		}
	}

	return true;
}

bool ZObject::StopMove()
{
	float &dx = loc.dx;
	float &dy = loc.dy;
	//const float z = 0.00001;

	//if((dx < z && dx > -z) && (dy < z && dy > -z)) return false;
	if(!IsMoving()) return false;

	dx = 0;
	dy = 0;

	sflags.updated_velocity = true;

	return true;
}

void ZObject::SetTarget(int x, int y)
{
	//to
	cur_wp_info.x = x;
	cur_wp_info.y = y;

	//from
	cur_wp_info.sx = center_x;
	cur_wp_info.sy = center_y;

	//distance to go
	cur_wp_info.adx = abs(cur_wp_info.x - cur_wp_info.sx);
	cur_wp_info.ady = abs(cur_wp_info.y - cur_wp_info.sy);
}

// Do not interfere with scripted factory exits, crane/repair/fort entry,
// waiting or exit. Normal movement and combat use the strict map rectangle.
bool ZObject::CollisionRecoveryAllowed() const
{
    if(owner == NULL_TEAM || move_speed <= 0 || width_pix <= 2 || height_pix <= 2 ||
       (object_type != VEHICLE_OBJECT && object_type != ROBOT_OBJECT)) return false;
    if(waypoint_list.empty()) return true;
    switch(waypoint_list.front().mode)
    {
        case FORCE_MOVE_WP: case CRANE_REPAIR_WP: case UNIT_REPAIR_WP:
        case ENTER_FORT_WP: return false;
        default: return true;
    }
}

namespace {
struct ZODRecoveryGrid
{
    ZMap &map;
    bool robot;
    ZODRecoveryGrid(ZMap &m,bool r):map(m),robot(r) {}
    bool Passable(int tx,int ty) { return map.GetPathFinder().TilePassable(tx,ty,robot); }
    bool Blocked(int x,int y,int w,int h)
    {
        int sx=0,sy=0;
        return map.WithinImpassable(x,y,w,h,sx,sy,robot);
    }
};
}

bool ZObject::ProcessCollisionRecovery(double time_dif, ZMap &tmap)
{
    if(!CollisionRecoveryAllowed() || !tmap.Loaded())
    {
        collision_recovery_active=false;
        collision_recovery_credit=collision_recovery_retry=0.0;
        return false;
    }
    ZODRecoveryGrid grid(tmap,object_type==ROBOT_OBJECT);
    const int w=width_pix-2,h=height_pix-2;
    const int mw=tmap.GetMapBasics().width*16,mh=tmap.GetMapBasics().height*16;
    // Cheap check on the current footprint; no search for any healthy unit.
    if(!grid.Blocked(loc.x+1,loc.y+1,w,h))
    {
        if(collision_recovery_active)
            real_move_speed=move_speed*tmap.GetTileWalkSpeed(center_x,center_y)*DamagedSpeed()*RunSpeed();
        collision_recovery_active=false;
        collision_recovery_credit=collision_recovery_retry=0.0;
        return false;
    }
    if(time_dif<=0.0) return collision_recovery_active;
    if(ztime->ztime<collision_recovery_retry) return false;
    ZODCollisionRecovery::Point step;
    if(!ZODCollisionRecovery::FindStep(grid,loc.x+1,loc.y+1,w,h,mw,mh,step))
    {
        // No safe local exit: do not tunnel or lock out normal orders.
        // Explosive units may still clear a rock through the normal logic.
        collision_recovery_active=false;
        collision_recovery_credit=0.0;
        collision_recovery_retry=ztime->ztime+0.5;
        return false;
    }
    if(!collision_recovery_active)
    {
        collision_recovery_active=true;
        StopMove();
        is_running=false;
        xover=yover=0;
        cur_wp_info.clear_and_kill(tmap);
        last_wp.clear(); // leave the actual order queue untouched
        next_path_retry_time=0;
        force_next_full_path=true;
        parking_valid=false;
        if(object_type==ROBOT_OBJECT) Disengage();
        sflags.updated_velocity=true;
    }
    // Move at a bounded normal (non-sprint) pace, never jump to the result.
    // Explicit zero-velocity location updates prevent client extrapolation
    // across a wall while the server works through the one-pixel steps.
    collision_recovery_credit+=(std::min)(time_dif,0.2)*
        (std::max)(1.0,(std::min)(20.0,move_speed*DamagedSpeed()));
    while(collision_recovery_credit>=1.0)
    {
        collision_recovery_credit-=1.0;
        SetCords(step.x-1,step.y-1);
        sflags.updated_velocity=true; // RelayObjectLoc includes coordinates
        if(!grid.Blocked(loc.x+1,loc.y+1,w,h))
        {
            collision_recovery_active=false;
            collision_recovery_credit=0.0;
            real_move_speed=move_speed*tmap.GetTileWalkSpeed(center_x,center_y)*DamagedSpeed()*RunSpeed();
            break; // the untouched order restarts next server tick
        }
        if(collision_recovery_credit<1.0) break;
        if(!ZODCollisionRecovery::FindStep(grid,loc.x+1,loc.y+1,w,h,mw,mh,step))
        {
            collision_recovery_active=false;
            collision_recovery_credit=0.0;
            collision_recovery_retry=ztime->ztime+0.5;
            break;
        }
    }
    return true;
}

bool ZObject::ReachedTarget()
{
    int &x = loc.x;
    int &y = loc.y;
    int &cx = center_x;
    int &cy = center_y;

    int tx = cur_wp_info.x;
    int ty = cur_wp_info.y;

    // Arrival tolerance: maximum 4 pixels per axis.
    if(abs(cx - tx) <= 4 && abs(cy - ty) <= 4)
    {
        // Previously this snap bypassed ProcessMove's collision test. A
        // legal arrival could therefore put the hull inside a wall/corner.
        // Keep the existing 4px arrival tolerance, but not the illegal snap.
        if(zmap && CollisionRecoveryAllowed())
        {
            const int end_x=tx-(width_pix>>1),end_y=ty-(height_pix>>1);
            const int delta_x=end_x-x,delta_y=end_y-y;
            const int steps=(std::max)(abs(delta_x),abs(delta_y));
            for(int i=1;i<=steps;++i)
            {
                int sx=0,sy=0;
                if(zmap->WithinImpassable(x+delta_x*i/steps+1,
                    y+delta_y*i/steps+1,width_pix-2,height_pix-2,
                    sx,sy,object_type==ROBOT_OBJECT))
                {
                    // A deliberate demolition route must still report the
                    // blockage to ProcessMove so it can attack the rock.
                    if(cur_wp_info.path_allows_demolition) return false;
                    xover=yover=0;
                    return true;
                }
            }
        }
        x = tx - (width_pix >> 1);
        y = ty - (height_pix >> 1);

        center_x = tx;
        center_y = ty;

        xover = yover = 0;
        return true;
    }

    // Recalculate direction from current position.
    if(waypoint_list.size())
    {
        float vx = tx - cx;
        float vy = ty - cy;
        float mag = sqrt((vx * vx) + (vy * vy));

        if(mag > 0.001f)
        {
            const double speed_factor =
                waypoint_list.front().mode == MOVE_WP ? transport_speed_factor : 1.0;
            float ndx = (vx / mag) * real_move_speed * speed_factor;
            float ndy = (vy / mag) * real_move_speed * speed_factor;

            if(fabs(loc.dx - ndx) > 0.1f ||
               fabs(loc.dy - ndy) > 0.1f)
            {
                loc.dx = ndx;
                loc.dy = ndy;
                sflags.updated_velocity = true;
            }
        }
    }

    return false;
}
bool ZObject::ProcessMoveOrKillWP(double time_dif, ZMap &tmap, vector<waypoint>::iterator &wp, ZOLists &ols, bool stoppable)
{
	int stop_x, stop_y;
	if(!ProcessMove(time_dif, tmap, stop_x, stop_y, stoppable))
	{
		//attack our way through?
		if(DoAttackImpassableAtCoords(ols, stop_x, stop_y)) return false;

		//otherwise kill the move and leave

		KillWP(wp);
		return false;
	}

	return true;
}

bool ZObject::ProcessMove(double time_dif, ZMap &tmap, int &stop_x, int &stop_y, bool stoppable)
{
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;
	double nx, ny;
	int inx, iny;
	//const double z = 0.00001;

	//are we moving?
	//if((dx < z && dx > -z) && (dy < z && dy > -z))
	if(isz(dx) && isz(dy)) return true;

	nx = x + (dx * time_dif) + xover;
	ny = y + (dy * time_dif) + yover;

	inx = (int)floor(nx);
	iny = (int)floor(ny);

	// Minions must respect normal map collision
	// Minion collision bypass disabled

	//is the new x and y kosher?
	//if(stoppable && tmap.WithinImpassable((inx + (width_pix>>1)) - 7, (iny + (height_pix>>1)) - 7, 6, 6, object_type == ROBOT_OBJECT))
	if(stoppable && tmap.WithinImpassable(inx+1, iny+1, width_pix-2, height_pix-2, stop_x, stop_y, object_type == ROBOT_OBJECT))
	{
		if(ReachedTarget())
			return true;
		else
			return false;
	}

	//robots don't attack while moving
	if(object_type == ROBOT_OBJECT) Disengage();

	//capture the overflow loss from double to int conversion
	xover = nx - inx;
	yover = ny - iny;

	x = inx;
	y = iny;

	//set centers
	center_x = x + (width_pix >> 1);
	center_y = y + (height_pix >> 1);

	//set new real move speed
	//if(dx > z || dx < -z || dy > z || dy < -z)
	if(!isz(dx) || !isz(dy))
	{
		double previous_real_speed = real_move_speed;

		real_move_speed = move_speed * tmap.GetTileWalkSpeed(center_x, center_y) * DamagedSpeed() * RunSpeed();
		//real_move_speed = move_speed * tmap.GetTileWalkSpeed(x + (width_pix >> 1), y + (height_pix >> 1));
		//real_move_speed = move_speed * tmap.GetTileWalkSpeed(x + 8, y + 8);

		//update in speed?
		if(real_move_speed != previous_real_speed)
		{
			if(previous_real_speed > 0)
			{
				dx /= previous_real_speed;
				dy /= previous_real_speed;
			}
			else
			{
				printf("error:previous_real_speed(%lf) not above zero!\n", previous_real_speed);
			}
			dx *= real_move_speed;
			dy *= real_move_speed;

			sflags.updated_velocity = true;
		}
	}

	return true;
}

void ZObject::SetVelocity(ZObject *target_object)
{
	int &x = loc.x;
	int &y = loc.y;
	int ox, oy;
	float &dx = loc.dx;
	float &dy = loc.dy;
	vector<waypoint>::iterator wp;
	float mag;
	float old_dx = loc.dx;
	float old_dy = loc.dy;
	

	//do we have a waypoint?
	if(waypoint_list.size())
	{
		int &cx = center_x;
		int &cy = center_y;

		//cx = x + (width_pix >> 1);
		//cy = y + (height_pix >> 1);

		dx = cur_wp_info.x - cx;
		dy = cur_wp_info.y - cy;
		if(!isz(dx) || !isz(dy))
		{
			mag = sqrt((dx * dx) + (dy * dy));
			dx /= mag;
			dy /= mag;
			double velocity_factor =
				(waypoint_list.front().mode == MOVE_WP) ? transport_speed_factor : 1.0;
			dx *= real_move_speed * velocity_factor;
			dy *= real_move_speed * velocity_factor;
		}

		//wp = waypoint_list.begin();

		//switch(wp->mode)
		//{
		//case MOVE_WP:
		//case FORCE_MOVE_WP:
		//case ENTER_WP:
		//case CRANE_REPAIR_WP:
		//case UNIT_REPAIR_WP:
		//case ATTACK_WP:
		//	dx = cur_wp_info.x - cx;
		//	dy = cur_wp_info.y - cy;
		//	if(!isz(dx) || !isz(dy))
		//	{
		//		mag = sqrt((dx * dx) + (dy * dy));
		//		dx /= mag;
		//		dy /= mag;
		//		dx *= real_move_speed;
		//		dy *= real_move_speed;
		//	}
		//	break;
		////case ATTACK_WP:
		////	if(target_object)
		////	{
		////		target_object->GetCenterCords(ox, oy);
		////		dx = ox - cx;
		////		dy = oy - cy;
		////		if(!isz(dx) || !isz(dy))
		////		{
		////			mag = sqrt((dx * dx) + (dy * dy));
		////			dx /= mag;
		////			dy /= mag;
		////			dx *= real_move_speed;
		////			dy *= real_move_speed;
		////		}
		////		//garr
		////		//yover = xover = 0;
		////	}

		//	break;
		//}
	}
	else
	{
		//we aint moving if we have no waypoints
		StopMove();
	}

	if(fabs(dx - old_dx) < 0.1) dx = old_dx;
	if(fabs(dy - old_dy) < 0.1) dy = old_dy;

	//update velocity?
	if(dx != old_dx || dy != old_dy)
	{
		sflags.updated_velocity = true;
		//yover = xover = 0;
	}
}

bool ZObject::DodgeMissile(int tx, int ty, double time_till_explode)
{
	int nx, ny;

	//1 in 3 then don't do
	//if(!(rand() % 3)) return false;
	if(!CanOverwriteWP()) return false;
	if(move_speed <= 0) return false;
	if(real_move_speed <= 0) return false;
	if(object_type == ROBOT_OBJECT && attack_object) return false;
	if(owner == NULL_TEAM) return false;

	double dist; 
	if(time_till_explode > stamina) dist = time_till_explode * real_move_speed;
	else dist = time_till_explode * real_move_speed * zsettings->run_unit_speed;
	int m_move_dist = dist * 2.0 / 4.0;
	int f_move_dist = dist * 4.0 / 4.0;
	if(m_move_dist <= 0) m_move_dist = 1;
	int move_dist = f_move_dist + (rand() % m_move_dist);
	double theta = (2 * PI) * (1000.0 / (rand() % 1000));

	//move_dist = dist;

	nx = center_x + (move_dist * cos(theta));
	ny = center_y + (move_dist * sin(theta));

	//if(rand()%2)
	//	nx = center_x + (f_move_dist + (rand() % m_move_dist));
	//else
	//	nx = center_x - (f_move_dist + (rand() % m_move_dist));

	//if(rand()%2)
	//	ny = center_y + (f_move_dist + (rand() % m_move_dist));
	//else
	//	ny = center_y - (f_move_dist + (rand() % m_move_dist));

	//modify current dodge or make new?
	if(waypoint_list.size() && waypoint_list.begin()->mode == DODGE_WP)
	{
		//altering this should make it
		//recognize as a new waypoint later
		waypoint_list.begin()->x = nx;
		waypoint_list.begin()->y = ny;
	}
	else
	{
		waypoint dodge_waypoint;

		dodge_waypoint.mode = DODGE_WP;
		dodge_waypoint.ref_id = -1;
		dodge_waypoint.x = nx;
		dodge_waypoint.y = ny;

		waypoint_list.insert(waypoint_list.begin(), dodge_waypoint);
	}

	return true;
}

// Estimate a full-health rock's clearing time on the owner thread. Workers
// receive only an integer cost, never live unit/settings pointers. One
// 16-pixel normal-terrain edge is 100 A* cost units. Include splash loss,
// firing interval, projectile flight, and a small stop/restart allowance.
int ZObject::EstimateRockDemolitionCost()
{
    if(!zsettings || !CanAttack() || !HasExplosives() || move_speed <= 0) return 0;
    const bool grenade = GetGrenadeAmount() > 0 ||
        (GetGroupLeader() && GetGroupLeader()->GetGrenadeAmount() > 0);
    const double shot_damage = grenade ? zsettings->grenade_damage * MAX_UNIT_HEALTH : damage;
    const double interval = grenade ? zsettings->grenade_attack_speed : damage_int_time;
    const double radius = grenade ? zsettings->grenade_damage_radius : damage_radius;
    const double projectile_speed = grenade ? zsettings->grenade_missile_speed : missile_speed;
    if(shot_damage <= 0 || radius <= 0) return 0;
    const double splash_factor = (std::max)(0.25, 1.0 - (grenade ? 18.0 : 12.0)/radius);
    const double rock_health = (std::max)(1.0, zsettings->rock_item_health * MAX_UNIT_HEALTH);
    const double shots = (std::max)(1.0, ceil(rock_health/(shot_damage*splash_factor)));
    const double flight = projectile_speed > 0 ? 0.6*attack_radius/projectile_speed : 0.0;
    const double seconds = (std::max)(2.0, shots*(std::max)(0.2,interval) + flight + 0.8);
    const double normal_speed = (std::max)(1.0, move_speed*DamagedSpeed());
    return (int)(std::min)(100000.0, ceil(seconds*normal_speed*100.0/16.0));
}

bool ZObject::DoAttackImpassableAtCoords(ZOLists &ols, int x, int y)
{
	//all impasses require explosives to destroy
	if(!HasExplosives()) return false;

	for(vector<ZObject*>::iterator o=ols.object_list->begin(); o!=ols.object_list->end();++o)
	{
		if(!(*o)->IsDestroyableImpass()) continue;
		if(!(*o)->CausesImpassAtCoord(x, y)) continue;
		if(!CanAttackObject(*o)) continue;

        // Only a route explicitly allowed to demolish may break an obstacle.
        // A deliberate ATTACK on this exact rock still works as before.
        if(!waypoint_list.empty())
        {
            const waypoint &current=waypoint_list.front();
            const bool explicit_target=(current.mode==ATTACK_WP || current.mode==ATTACK_OWN_WP) &&
                current.ref_id==(*o)->GetRefID();
            if(!explicit_target && !cur_wp_info.path_allows_demolition &&
               (current.mode==MOVE_WP || current.mode==ATTACK_WP ||
                current.mode==ATTACK_OWN_WP || current.mode==AGRO_WP))
                return false;
        }

		//ok attack it
		{
			waypoint new_waypoint;

			new_waypoint.mode = ATTACK_WP;
			new_waypoint.ref_id = (*o)->GetRefID();
			(*o)->GetCenterCords(new_waypoint.x, new_waypoint.y);

			//push this attack waypoint to the front,
			//and exit this current waypoint
			waypoint_list.insert(waypoint_list.begin(), new_waypoint);

			//a kind of solution for the minions
			CloneMinionWayPoints();

			return true;
		}
	}

	return false;
}

server_flag &ZObject::GetSFlags()
{
	return sflags;
}

void ZObject::CreateLocationData(char *&data, int &size)
{
	size = 4 + sizeof(object_location);
	data = (char*)malloc(size);

	((int*)data)[0] = ref_id;
	memcpy(data+4, &loc, sizeof(object_location));
}

void ZObject::CreateAttackObjectData(char *&data, int &size)
{
	attack_object_packet send_data;

	size = sizeof(attack_object_packet);
	data = (char*)malloc(size);

	send_data.ref_id = ref_id;
	if(attack_object)
		send_data.attack_object_ref_id = attack_object->ref_id;
	else
		send_data.attack_object_ref_id = -1;

	memcpy(data, &send_data, sizeof(attack_object_packet));
}

void ZObject::SetLoc(object_location new_loc)
{
	double &the_time = ztime->ztime;
	int &x = loc.x;
	int &y = loc.y;

	if(new_loc.dx != loc.dx || new_loc.dy != loc.dy)
	{
		loc = new_loc;
		RecalcDirection();
	}
	else
		loc = new_loc;

	//for estimating where it really is
	last_loc = loc;
	last_loc_set_time = the_time;

	//set centers
	center_x = x + (width_pix >> 1);
	center_y = y + (height_pix >> 1);
}

void ZObject::SmoothMove(double &the_time)
{
	int &x = loc.x;
	int &y = loc.y;
	float &dx = loc.dx;
	float &dy = loc.dy;

	//move if it is moving
	if(!isz(dx)) loc.x = last_loc.x + floor(dx * (the_time - last_loc_set_time));
	if(!isz(dy)) loc.y = last_loc.y + floor(dy * (the_time - last_loc_set_time));

	//set centers
	center_x = x + (width_pix >> 1);
	center_y = y + (height_pix >> 1);
}

void ZObject::RecalcDirection()
{
	int new_dir;

	new_dir = DirectionFromLoc(loc.dx, loc.dy);

	printf("ZObject::RecalcDirection():%d\n", new_dir);

	if(new_dir != -1) direction = new_dir;
}

int ZObject::DirectionFromLoc(float dx, float dy)
{
	//const float z = 0.000001;
	float a;
	int dir;

	//are we going anywhere?
	//if((dx > -z && dx < z) && (dy > -z && dy < z))
	if(isz(dx) && isz(dy))
		return -1;

	a = atan2(dy,dx);

	//atan2 is kind of funky
	if(a < 0) a += PI + PI;
	a += PI * 1 / 8;

	if(a < PI / 4) dir = 0;
	else if(a < PI / 2) dir = 7;
	else if(a < PI * 3 / 4) dir = 6;
	else if(a < PI) dir = 5;
	else if(a < 5 * PI / 4) dir = 4;
	else if(a < 6 * PI / 4) dir = 3;
	else if(a < 7 * PI / 4) dir = 2;
	else if(a < 2 * PI) dir = 1;
	else dir = 0;

	//printf("DirectionFromLoc:a:%f dir:%d\n", a, dir);

	return dir;
}

ZObject* ZObject::GetObjectFromID_BS(int ref_id_, vector<ZObject*> &the_list)
{
	int low, high, midpoint;

	low = 0;
	high = the_list.size() - 1;
	midpoint = 0;

	//printf("ref id list: ");
	//for(vector<ZObject*>::iterator obj=the_list.begin(); obj!=the_list.end();obj++)
	//	printf("%d, ", (*obj)->GetRefID());
	//printf("\n");

	while (low <= high)
	{
		int tref_id;

		//midpoint = low + ((high - low) / 2);
		midpoint = low + ((high - low) >> 1);

		tref_id = the_list[midpoint]->GetRefID();

		if (ref_id_ == tref_id)
			return the_list[midpoint];
		else if (ref_id_ < tref_id)
			high = midpoint - 1;
		else
			low = midpoint + 1;
	}

	return NULL;
}

ZObject* ZObject::GetObjectFromID(int ref_id_, vector<ZObject*> &the_list)
{
	vector<ZObject*>::iterator obj;

	return GetObjectFromID_BS(ref_id_, the_list);

	//for(obj=the_list.begin(); obj!=the_list.end();obj++)
	//	if((*obj)->ref_id == ref_id_)
	//		return *obj;

	//return NULL;
}

void ZObject::SetAttackObject(ZObject *obj)
{
	attack_object = obj;
}

ZObject* ZObject::GetAttackObject()
{
	return attack_object;
}

ZObject* ZObject::NearestObjectFromList(vector<ZObject*> &the_list)
{
	ZObject *obj_choice;
	double least_distance;

	if(!the_list.size()) return NULL;

	obj_choice = the_list[0];
	least_distance = DistanceFromObject(*obj_choice);
	for(vector<ZObject*>::iterator obj=the_list.begin()++;obj!=the_list.end();obj++)
	{
		double this_distance = DistanceFromObject(**obj);

		if(this_distance < least_distance)
		{
			least_distance = this_distance;
			obj_choice = *obj;
		}
	}

	return obj_choice;
}

void ZObject::RemoveObjectFromList(ZObject* the_object, vector<ZObject*> &the_list)
{
	vector<ZObject*>::iterator i;

	for(i=the_list.begin();i!=the_list.end();)
	{
		if(*i == the_object)
			i = the_list.erase(i);
		else
			i++;
	}
}

ZObject* ZObject::NearestObjectToCoords(vector<ZObject*> &the_list, int x, int y)
{
	ZObject *obj_choice;
	double least_distance;

	if(!the_list.size()) return NULL;

	obj_choice = the_list[0];
	least_distance = obj_choice->DistanceFromCoords(x,y);
	for(vector<ZObject*>::iterator i=the_list.begin();i!=the_list.end();i++)
	{
		double this_distance;

		this_distance = (*i)->DistanceFromCoords(x,y);
		if(this_distance < least_distance)
		{
			least_distance = this_distance;
			obj_choice = *i;
		}
	}

	return obj_choice;
}

void ZObject::ClearAndDeleteList(vector<ZObject*> &the_list)
{
	for(vector<ZObject*>::iterator obj=the_list.begin(); obj!=the_list.end(); ++obj)
		if(*obj) delete *obj;

	the_list.clear();
}

void ZObject::ProcessList(vector<ZObject*> &the_list)
{
	for(vector<ZObject*>::iterator obj=the_list.begin(); obj!=the_list.end(); ++obj)
		if(*obj) (*obj)->Process();
}

ZObject* ZObject::NearestSelectableObject(vector<ZObject*> &the_list, int unit_type, int only_team, int x, int y)
{
	ZObject *obj_choice;
	double least_distance;

	if(!the_list.size()) return NULL;

	obj_choice = NULL;
	//obj_choice = the_list[0];
	//least_distance = obj_choice->DistanceFromCoords(x,y);
	for(vector<ZObject*>::iterator i=the_list.begin();i!=the_list.end();i++)
	{
		unsigned char ot, oid;
		double this_distance;

		if((*i)->GetOwner() != only_team) continue;
		if((*i)->IsMinion()) continue;

		(*i)->GetObjectID(ot, oid);
		if(ot != unit_type) continue;

		//this the first found?
		if(!obj_choice)
		{
			obj_choice = *i;
			least_distance = obj_choice->DistanceFromCoords(x,y);
			continue;
		}

		//is this one closer then the previous?
		this_distance = (*i)->DistanceFromCoords(x,y);
		if(this_distance < least_distance)
		{
			obj_choice = *i;
			least_distance = this_distance;
		}
	}

	return obj_choice;
}

ZObject* ZObject::NextSelectableObjectAboveID(vector<ZObject*> &the_list, int unit_type, int only_team, int min_ref_id)
{
	for(vector<ZObject*>::iterator i=the_list.begin();i!=the_list.end();i++)
	{
		unsigned char ot, oid;

		if((*i)->GetRefID() <= min_ref_id) continue;
		if((*i)->GetOwner() != only_team) continue;
		if((*i)->IsMinion()) continue;

		(*i)->GetObjectID(ot, oid);
		if(ot != unit_type) continue;
			
		return *i;
	}

	return NULL;
}

void ZObject::RemoveObject(ZObject *obj)
{
	if(attack_object == obj)
	{
		SetAttackObject(NULL);

		//only the server needs this
		//but it doesn't hurt to do it in the client
		SignalLidShouldClose();
	}

	//this is kind of crude but it needs to be done
	for(vector<ZObject*>::iterator i=minion_list.begin();i!=minion_list.end();i++)
		if(*i == obj) *i = NULL;

	//crude
	if(leader_obj == obj) leader_obj = NULL;
}

//bool ZObject::ServerFireTurrentMissile(int &x_, int &y_, int &damage, int &radius, double &offset_time)
//{
//	x_ = 0;
//	y_ = 0;
//	damage = 0;
//	radius = 0;
//	offset_time = 0;
//	return false;
//}

vector<fire_missile_info> ZObject::ServerFireTurrentMissile(int &damage, int &radius)
{
	vector<fire_missile_info> ret;

	damage = 0;
	radius = 0;

	return ret;
}

void ZObject::FireTurrentMissile(int x_, int y_, double offset_time)
{

}

ZGuiWindow *ZObject::MakeGuiWindow()
{
	return NULL;
}

void ZObject::ProcessSetBuiltCannonData(char *data, int size)
{

}

void ZObject::ProcessSetBuildingStateData(char *data, int size)
{

}

bool ZObject::GetBuildingCreationPoint(int &x, int &y)
{
	return false;
}

bool ZObject::GetBuildingCreationMovePoint(int &x, int &y)
{
	return false;
}

bool ZObject::GetBuildUnit(unsigned char &ot, unsigned char &oid)
{
	return false;
}

bool ZObject::BuildUnit(double &the_time, unsigned char &ot, unsigned char &oid)
{
	return false;
}

void ZObject::ResetProduction()
{

}

void ZObject::SetBuildList(ZBuildList *buildlist_)
{
	buildlist = buildlist_;
}

void ZObject::SetAllianceMatrix(const bool (*matrix)[MAX_TEAM_TYPES])
{
    alliance_matrix = matrix;
}

bool ZObject::StoreBuiltCannon(unsigned char oid)
{
	return false;
}

map_zone_info *ZObject::GetConnectedZone()
{
	return connected_zone;
}

void ZObject::SetConnectedZone(map_zone_info *connected_zone_)
{
	connected_zone = connected_zone_;
}

void ZObject::SetConnectedZone(ZMap &the_map)
{
	connected_zone = the_map.GetZone(loc.x, loc.y);
}

bool ZObject::RemoveStoredCannon(unsigned char oid)
{
	return false;
}

bool ZObject::HaveStoredCannon(unsigned char oid)
{
	return false;
}

int ZObject::CannonsInZone(ZOLists &ols)
{
	int cannons_found = 0;

	for(vector<ZObject*>::iterator i=ols.object_list->begin();i!=ols.object_list->end();i++)
		if(this != *i && connected_zone == (*i)->GetConnectedZone())
		{
			unsigned char ot, oid;

			(*i)->GetObjectID(ot, oid);

			if(ot != CANNON_OBJECT) continue;

			cannons_found++;
		}

	return cannons_found;
}

vector<unsigned char> &ZObject::GetBuiltCannonList()
{
	static vector<unsigned char> arg;

	return arg;
}

void ZObject::SetUnitLimitReachedList(bool *unit_limit_reached_)
{
	unit_limit_reached = unit_limit_reached_;
}

void ZObject::SetLevel(int level_)
{

}

int ZObject::GetLevel()
{
	return 0;
}

void ZObject::DoPreRender(ZMap &the_map, SDL_Surface *dest, int shift_x, int shift_y)
{

}

bool ZObject::HasDestroyedFortInZone(ZOLists &ols)
{
	if(!connected_zone) return false;

	for(vector<ZObject*>::iterator i=ols.building_olist.begin(); i!=ols.building_olist.end(); ++i)
	{
		unsigned char ot, oid;

		if(connected_zone != (*i)->GetConnectedZone()) continue;
		if(!(*i)->IsDestroyed()) continue;

		(*i)->GetObjectID(ot, oid);

		if(ot == BUILDING_OBJECT && (oid == FORT_FRONT || oid == FORT_BACK)) return true;
	}

	return false;
}

bool ZObject::DoAutoRepair(ZMap &tmap, ZOLists &ols)
{
	double &the_time = ztime->ztime;

	if(!do_auto_repair) return false;

	if(the_time >= next_auto_repair_time)
	{
		do_auto_repair = false;

        // A destroyed fort must not block its OWN deliberate Crane repair.
        // Preserve the existing zone restriction for other buildings.
        const bool self_is_fort = (object_type == BUILDING_OBJECT &&
            (object_id == FORT_FRONT || object_id == FORT_BACK));
        if(!self_is_fort && HasDestroyedFortInZone(ols)) return false;
		
		//do the repair
		//health = max_health;
		SetHealth(max_health, tmap);

		//let them know
		return true;
	}

	return false;
}

void ZObject::StopAutoRepair()
{
	do_auto_repair = false;
}

int ZObject::GetBuildState()
{
	return -1;
}

void ZObject::SetMapImpassables(ZMap &tmap)
{

}

void ZObject::UnSetMapImpassables(ZMap &tmap)
{

}

void ZObject::SetDestroyMapImpassables(ZMap &tmap)
{

}

void ZObject::UnSetDestroyMapImpassables(ZMap &tmap)
{

}

void ZObject::AddGroupMinion(ZObject *obj)
{
	if(!obj) return;
	if(obj == this) return;

	minion_list.push_back(obj);
}

void ZObject::RemoveGroupMinion(ZObject *obj)
{
	for(vector<ZObject*>::iterator i=minion_list.begin();i!=minion_list.end();)
	{
		if(!*i || *i == obj)
			i = minion_list.erase(i);
		else
			i++;
	}
}

vector<ZObject*> &ZObject::GetMinionList()
{
	return minion_list;
}

ZObject* ZObject::GetGroupLeader()
{
	return leader_obj;
}

void ZObject::SetGroupLeader(ZObject *obj)
{
	//cant be your own leader
	if(obj == this) return;

	//if(leader_obj) minion_list.clear();

	leader_obj = obj;
}

void ZObject::ClearGroupInfo()
{
	minion_list.clear();
	leader_obj = NULL;
}

bool ZObject::IsMinion()
{
	return leader_obj;
}

bool ZObject::IsApartOfAGroup()
{
	return leader_obj || minion_list.size();
}

void ZObject::CloneMinionWayPoints()
{
	for(vector<ZObject*>::iterator i=minion_list.begin();i!=minion_list.end();i++)
	{
		if(!*i) continue;

		(*i)->GetWayPointList() = waypoint_list;
		(*i)->SetVelocity();

		//just left cannon...
		(*i)->SetJustLeftCannon(just_left_cannon);
	}
}

void ZObject::CreateGroupInfoData(char *&data, int &size)
{
	//only robots can create groups so
	if(object_type != ROBOT_OBJECT)
	{
		data = NULL;
		size = 0;
		return;
	}

	//int ref_id;
	int leader_ref_id;
	int minions;

	if(leader_obj)
		leader_ref_id = leader_obj->GetRefID();
	else
		leader_ref_id = -1;

	minions = minion_list.size();

	size = 12 + (4 * minions);

	data = (char*)malloc(size);
	((int*)data)[0] = ref_id;
	((int*)data)[1] = leader_ref_id;
	((int*)data)[2] = minions;

	for(int i=0;i<minions;i++)
	{
		if(minion_list[i])
			((int*)data)[3+i] = minion_list[i]->GetRefID();
		else
			((int*)data)[3+i] = -1;
	}
}

void ZObject::CreateTeamData(char *&data, int &size)
{
	object_team_packet packet_header;

	size = sizeof(object_team_packet) + (driver_info.size() * sizeof(driver_info_s));
	data = (char*)malloc(size);

	packet_header.ref_id = ref_id;
	packet_header.owner = owner;
	packet_header.driver_type = driver_type;
	packet_header.driver_amount = driver_info.size();

	memcpy(data, &packet_header, sizeof(object_team_packet));

	int shift_amt = sizeof(object_team_packet);
	for(vector<driver_info_s>::iterator i=driver_info.begin();i!=driver_info.end();i++)
	{
		memcpy(data + shift_amt, &(*i), sizeof(driver_info_s));
		shift_amt += sizeof(driver_info_s);
	}
}

void ZObject::ProcessGroupInfoData(char *data, int size, vector<ZObject*> &object_list)
{
	int ref_id_;
	int leader_ref_id;
	int minions;

	//meet min requirements?
	if(size < 12) return;

	ref_id_ = ((int*)data)[0];
	leader_ref_id = ((int*)data)[1];
	minions = ((int*)data)[2];

	//other requirements?
	if(ref_id_ != ref_id) return;
	if(size != 12 + (4 * minions)) return;

	ClearGroupInfo();

	leader_obj = GetObjectFromID(leader_ref_id, object_list);

	//fill the minion list
	for(int i=0;i<minions;i++)
	{
		ZObject *new_minion;

		new_minion = GetObjectFromID(((int*)data)[3+i], object_list);

		if(new_minion)
		{
			minion_list.push_back(new_minion);
		}
		else
		{
			printf("ProcessGroupInfoData:could not find a minion\n");
		}
	}
}

void ZObject::SetupRockRender(bool **rock_list, int map_w, int map_h)
{

}

bool ZObject::CanBeDestroyed()
{
	return can_be_destroyed;
}

void ZObject::CreationMapEffects(ZMap &tmap)
{

}

void ZObject::DeathMapEffects(ZMap &tmap)
{

}

bool ZObject::ResetBuildTime(float zone_ownage)
{
	return false;
}

bool ZObject::RecalcBuildTime()
{
	return false;
}

double ZObject::SpeedOffsetPercent()
{
	if(!move_speed) return 1.0;
	if(!IsMoving()) return 1.0;

	return sqrt((loc.dx*loc.dx)+(loc.dy*loc.dy)) / move_speed;
}

bool ZObject::CanBeRepairedByCrane(int repairers_team)
{
	if(object_type != BUILDING_OBJECT) return false;

	if(owner != NULL_TEAM && repairers_team != owner) return false;
	if(!IsDestroyed()) return false;

	return true;
}

bool ZObject::CanAttack()
{
	return damage && !IsDestroyed();
}

bool ZObject::HasExplosives()
{
	if(has_explosives) return true;
	if(GetGrenadeAmount()) return true;
	if(GetGroupLeader() && GetGroupLeader()->GetGrenadeAmount()) return true;

	return false;
	//return has_explosives;
}

bool ZObject::AttackedOnlyByExplosives()
{
	return attacked_by_explosives;
}

void ZObject::SetDriverType(int driver_type_)
{
	driver_type = driver_type_;

	if(driver_type < 0) driver_type = 0;
	if(driver_type >= MAX_ROBOT_TYPES) driver_type = MAX_ROBOT_TYPES-1;

	//drivers can mess with damage info
	ResetDamageInfo();
}

void ZObject::AddDriver(int driver_health_)
{
	driver_info_s new_driver;

	new_driver.driver_health = driver_health_;

	AddDriver(new_driver);
}

void ZObject::AddDriver(driver_info_s new_driver)
{
	new_driver.next_attack_time = 0;

	driver_info.push_back(new_driver);

	//drivers can mess with damage info
	ResetDamageInfo();
}

vector<driver_info_s> &ZObject::GetDrivers()
{
	return driver_info;
}

void ZObject::ClearDrivers()
{
	driver_info.clear();

	//drivers can mess with damage info
	ResetDamageInfo();
}

int ZObject::GetDriverType()
{
	return driver_type;
}

int ZObject::GetDriverHealth()
{
	if(driver_info.size())
		return driver_info.begin()->driver_health;
	else
		return 0;
}

bool ZObject::CanEjectDrivers()
{
	return false;
}

void ZObject::SetInitialDrivers()
{
	driver_type = GRUNT;
	ClearDrivers();
}

void ZObject::SetEjectableCannon(bool ejectable_)
{

}

void ZObject::ResetDamageInfo()
{

}

unsigned short ZObject::GetExtraLinks()
{
	return 0;
}

void ZObject::DoHitEffect()
{
	do_hit_effect = true;
}

bool ZObject::GetCraneEntrance(int &x, int &y, int &x2, int &y2)
{
    // Stage outside the fort wall at the existing production exit.
    // The final entry and return are controlled by ProcessCraneRepairWP.
    if(object_type == BUILDING_OBJECT &&
        (object_id == FORT_FRONT || object_id == FORT_BACK))
    {
        if(!GetBuildingCreationMovePoint(x, y)) return false;
        x2 = x;
        y2 = y;
        return true;
    }
    return false;
}

bool ZObject::GetCraneCenter(int &x, int &y)
{
    if(object_type == BUILDING_OBJECT &&
        (object_id == FORT_FRONT || object_id == FORT_BACK))
        return GetBuildingCreationPoint(x, y);
    return false;
}

bool ZObject::GetRepairEntrance(int &x, int &y)
{
	return false;
}

bool ZObject::GetRepairCenter(int &x, int &y)
{
	return false;
}

void ZObject::DoCraneAnim(bool on_, ZObject *rep_obj)
{

}

bool ZObject::CanBeRepaired()
{
	return false;
}

bool ZObject::CanRepairUnit(int units_team)
{
	return false;
}

bool ZObject::RepairingAUnit()
{
	return false;
}

bool ZObject::SetRepairUnit(ZObject *unit_obj)
{
	return false;
}

bool ZObject::GetLidState()
{
	return false;
}

void ZObject::SetLidState(bool lid_open_)
{

}

void ZObject::DoRepairBuildingAnim(bool on_, double remaining_time_)
{

}

bool ZObject::RepairUnit(double &the_time, unsigned char &ot, unsigned char &oid, int &driver_type_, vector<driver_info_s> &driver_info_, vector<waypoint> &rwaypoint_list)
{
	return false;
}

void ZObject::ProcessServerLid()
{

}

void ZObject::SignalLidShouldOpen()
{

}

void ZObject::SignalLidShouldClose()
{

}

bool ZObject::CanBeSniped()
{
	return can_be_sniped && driver_info.size();
}

bool ZObject::CanSnipe()
{
	return can_snipe;
}

bool ZObject::HasLid()
{
	return has_lid;
}

bool ZObject::HasProcessedDeath()
{
	return processed_death;
}

void ZObject::SetHasProcessedDeath(bool processed_death_)
{
	processed_death = processed_death_;
}

void ZObject::DoDriverHitEffect()
{
	do_driver_hit_effect = true;
}

bool ZObject::CanBeEntered()
{
	if(owner != NULL_TEAM) return false;
	if(IsDestroyed()) return false;
	if(!(object_type == VEHICLE_OBJECT || object_type == CANNON_OBJECT)) return false;

	return true;
}

void ZObject::SetDamagedByFireTime(double the_time)
{
	last_damaged_by_fire_time = the_time;
}

void ZObject::SetDamagedByMissileTime(double the_time)
{
	last_damaged_by_missile_time = the_time;
}

double ZObject::GetDamagedByFireTime()
{
	return last_damaged_by_fire_time;
}

double ZObject::GetDamagedByMissileTime()
{
	return last_damaged_by_missile_time;
}

bool ZObject::CanEnterFort(int team)
{
	return false;
}

bool ZObject::CanSetWaypoints()
{
	return false;
}

bool ZObject::CanMove()
{
	return move_speed;
}

bool ZObject::CanAttackObject(ZObject *obj)
{
    if(!obj) return false;

    if(!CanAttack()) return false;
    if(obj->IsDestroyed()) return false;
    if(owner == obj->GetOwner()) return false;

    team_type target_owner = obj->GetOwner();

    if(alliance_matrix &&
       owner > NULL_TEAM && owner < MAX_TEAM_TYPES &&
       target_owner > NULL_TEAM && target_owner < MAX_TEAM_TYPES &&
       alliance_matrix[owner][target_owner])
        return false;

    if(!HasExplosives() && obj->AttackedOnlyByExplosives()) return false;

    return true;
}
void ZObject::PostPathFindingResult(ZPath_Finding_Response* response)
{
	if(!response) return;

	if(response->thread_id == cur_wp_info.path_finding_id)
	{
		//printf("got a good response - thread_id:%d\n", response->thread_id);
		cur_wp_info.path_finding_id = 0;
		if(response->pf_point_list.empty())
		{
            if(zod_attack_staging_enabled && !waypoint_list.empty() &&
               waypoint_list.front().mode == ATTACK_WP &&
               waypoint_list.front().player_given)
            {
                ZODRememberFailedAttackRoute(ref_id,
                    waypoint_list.front().ref_id, ztime->ztime);
            }
			// A* failure must never become a global 'drive through walls'
			// fallback. Exception only for a real, collision-checked SHORT
			// approach to a REPAIR doorway (same test as at order start).
			if(zmap && !waypoint_list.empty() &&
				waypoint_list.begin()->mode == UNIT_REPAIR_WP &&
				cur_wp_info.stage == GOTO_ENTRANCE_URWS &&
				RepairEntranceClearFromCurrentPosition(*zmap, loc.x, loc.y,
					center_x, center_y, cur_wp_info.x, cur_wp_info.y,
					width_pix, height_pix, (object_type == ROBOT_OBJECT)))
			{
				cur_wp_info.got_pf_response = true;
				cur_wp_info.path_failed = false;
				force_next_full_path = false;
				SetVelocity();
				return;
			}
			DeferCurrentPath(1.5);
			return;
		}

		// Repair is special: when coming from the SIDE or from BEHIND a
		// wall, finish the A* detour at the staging point in FRONT of the
		// garage and only then follow the existing doorway coordinates.
		// Validate the last short leg using the same collision rectangle as
		// ProcessMove; if anything blocks it, we must not drive through it.
		if(zmap && !waypoint_list.empty() &&
			waypoint_list.begin()->mode == UNIT_REPAIR_WP &&
			cur_wp_info.stage == GOTO_ENTRANCE_URWS)
		{
			const int staging_x = response->end_x;
			const int staging_y = response->end_y;
			if(!RepairStagingPointSafe(*zmap,
				staging_x, staging_y, cur_wp_info.x, cur_wp_info.y,
				width_pix, height_pix, (object_type == ROBOT_OBJECT)))
			{
				DeferCurrentPath(1.5);
				return;
			}
		}

		cur_wp_info.got_pf_response = true;
		cur_wp_info.path_failed = false;
		force_next_full_path = false;
		cur_wp_info.pf_point_list = response->pf_point_list;
        cur_wp_info.path_allows_demolition = response->allows_demolition;

        // Fase 6I-B: leave robots, attack auto-targets and ALL special paths
        // untouched. Apply only to normal vehicle MOVE and explicit ATTACK
        // travel, including Fase 6H's temporary building-approach MOVE.
        if(zmap && object_type == VEHICLE_OBJECT &&
           !waypoint_list.empty() &&
           (waypoint_list.front().mode == MOVE_WP ||
            (waypoint_list.front().mode == ATTACK_WP &&
             waypoint_list.front().player_given)))
        {
            ZODSpreadVehicleRoute(*zmap,
                cur_wp_info.pf_point_list,ref_id,width_pix,height_pix);
        }

		if(!waypoint_list.empty() &&
			waypoint_list.begin()->mode == UNIT_REPAIR_WP &&
			cur_wp_info.stage == GOTO_ENTRANCE_URWS)
			cur_wp_info.pf_point_list.push_back(
				ZPath_Finding_AStar::pf_point(cur_wp_info.x, cur_wp_info.y));

		if(cur_wp_info.pf_point_list.size())
		{
			//cur_wp_info.x = cur_wp_info.pf_point_list.begin()->x;
			//cur_wp_info.y = cur_wp_info.pf_point_list.begin()->y;
			SetTarget(cur_wp_info.pf_point_list.begin()->x, cur_wp_info.pf_point_list.begin()->y);
			SetVelocity();

			cur_wp_info.pf_point_list.erase(cur_wp_info.pf_point_list.begin());
		}
	}
}

bool sort_objects_func (ZObject *a, ZObject *b)
{
	return ((a->loc.y + a->height_pix) < (b->loc.y + b->height_pix));
}

