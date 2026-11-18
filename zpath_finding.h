#ifndef _ZPATH_FINDING_H_
#define _ZPATH_FINDING_H_

#include "zpath_finding_astar.h"

#include <SDL/SDL_thread.h>
#include <vector>
#include <atomic>

using namespace std;

enum pf_tile_types
{
	PF_NORMAL, PF_IMPASSABLE, PF_WATER, PF_ROAD, MAX_PF_TILE_TYPES
};

struct map_pathfinding_info_tile
{
	int side_weight;
	int diag_weight;
	// Terrain may change while an A* worker reads this cell.
	std::atomic<bool> passable;
	map_pathfinding_info_tile() : side_weight(0), diag_weight(0), passable(false) {}
};

class ZPath_Finding_Engine;

namespace ZPath_Finding_AStar
{
	class pf_point;
}

class ZPath_Finding_Response
{
public:
	ZPath_Finding_Response();
	~ZPath_Finding_Response();

	static std::atomic<int> existing_responses;

	ZPath_Finding_Engine *path_finder;
	int thread_id;

	int start_x, start_y;
	int end_x, end_y;
	int obj_ref_id;
	int w, h;
	bool is_robot;
	std::atomic<bool> kill_thread;
	map_pathfinding_info_tile **tile_info;
    // Compare the intact route with a paid demolition route for normal orders.
    map_pathfinding_info_tile **demolition_tile_info;
    map_pathfinding_info_tile **terrain_tile_info;
    bool demolition_direct_possible;
    bool allows_demolition;
    int demolition_penalty; // extra A* cost per newly encountered rock
    bool compare_demolition; // voluntary shortcut allowed for normal travel
    int route_cost; // final grid cost, -1 if no route
    int cost_limit; // exclusive search bound; -1 means unlimited
    double direct_travel_cost; // -1: no safe direct candidate (or forced A*)
	vector<ZPath_Finding_AStar::pf_point> pf_point_list;
};

class ZPath_Finding_Thread_Entry
{
public:
	ZPath_Finding_Thread_Entry(int thread_id_, SDL_Thread *the_thread_, ZPath_Finding_Response *response_)
		{ thread_id = thread_id_; the_thread = the_thread_; response = response_; }

	int thread_id;
	SDL_Thread *the_thread;
	ZPath_Finding_Response *response;
};

class ffnode
{
public:
	ffnode(int x_, int y_) { x=x_; y=y_; }
	int x, y;
};

class ZPath_Finding_RegionInfo
{
public:
	ZPath_Finding_RegionInfo();

	void Init(int w_, int h_);
	void Delete();
	void BuildRegions(map_pathfinding_info_tile **tile_info, int **region);
	void FloodFill_Recursive(int **region, int x, int y, int cur_region);
	void FloodFill(int **region, int x, int y, int cur_region);
	void FloodFill_AddQ(int **region, int &i, int x, int y, int cur_region);
	bool InSameRegion(int sx, int sy, int ex, int ey, bool is_robot);

	bool allocated;

	int w, h;

	int **robot_region;
	int **vehicle_region;
	ffnode *ffQ;
};

//from the AI for game developers book
class ZPath_Finding_Bresenham
{
public:
	ZPath_Finding_Bresenham() { inited = false; }

	void Init(int stx_, int sty_, int etx_, int ety_, int w, int h);
	bool GetNext(int &x, int &y);

private:
	void clear();

	bool inited;
	int stx, sty;
	int etx, ety;
	int nx, ny;
	int dx, dy;
	int step_x, step_y;
	int cx, cy;
	int fraction;
};

class ZPath_Finding_Engine
{
public:
	ZPath_Finding_Engine();
	~ZPath_Finding_Engine();

	void ResetTileInfo(int w_, int h_);
	void SetTileInfo(int x, int y, int tile_type);
	void SetTileWideWeights();
	void DeleteAllTileInfo();
	void DeleteTileInfo(map_pathfinding_info_tile ***delete_list);

	void Push_Response(ZPath_Finding_Response *response);
	void Clear_Thread_List();
	void Clear_Response_List();
	// Owner thread takes completed responses; caller owns and deletes each one.
	void Take_Responses(vector<ZPath_Finding_Response*> &completed);
	void Clear_Thread_Id(int thread_id);
	void Kill_Thread_Id(int thread_id);
	inline void Lock_List() { SDL_LockMutex(list_mutex); }
	inline void Unlock_List() { SDL_UnlockMutex(list_mutex); } 
	inline vector<ZPath_Finding_Response*> &GetList() { return respone_list; }
	
	int Find_Path(int sx, int sy, int ex, int ey, bool is_robot, bool has_explosives, int obj_ref_id, bool skip_direct_test = false, int demolition_penalty = 0);
	bool Direct_Path_Possible(int sx, int sy, int ex, int ey, bool is_robot, bool has_explosives);
	
	void SetImpassable(int x, int y, bool impassable = true, bool destroyable = false);
	bool WithinImpassable(int x, int y, int w, int h, int &stop_x, int &stop_y, bool is_robot);
	bool HasDestroyableBarrier(int x, int y);

	void RebuildRegions();
	bool InSameRegion(int sx, int sy, int ex, int ey, bool is_robot);

	bool ShouldBeAbleToMoveTo(int sx, int sy, int ex, int ey, bool is_robot);

	bool TileOnMap(int tx, int ty);
	bool TilePassable(int tx, int ty, bool is_robot);
	
private:
	void AllocAllTileInfo();
    bool RoadCouldImprove(int sx, int sy, int ex, int ey, double direct_cost) const;
    // Summed area table: a cheap test for roads within the possible detour area.
    vector<int> road_prefix;
    int destroyable_cell_count;

	SDL_mutex* list_mutex;
	vector<ZPath_Finding_Response*> respone_list;

	int next_thread_id;
	vector<ZPath_Finding_Thread_Entry> thread_list;

	int w, h;
	int width_pix, height_pix;

	map_pathfinding_info_tile **path_robot_tile;
	map_pathfinding_info_tile **path_robot_norocks_tile;
	map_pathfinding_info_tile **path_vehicle_tile;
	map_pathfinding_info_tile **path_vehicle_norocks_tile;

	ZPath_Finding_RegionInfo region_info;
};

#endif
