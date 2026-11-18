#include "zpath_finding.h"
#include "constants.h"
#include "common.h"
#include <SDL/SDL_timer.h>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>

using namespace COMMON;

std::atomic<int> ZPath_Finding_Response::existing_responses(0);

void ZPath_Finding_Bresenham::Init(int stx_, int sty_, int etx_, int ety_, int w, int h)
{
	inited = false;

	if(stx_ < 0) return;
	if(sty_ < 0) return;
	if(stx_ >= w) return;
	if(sty_ >= h) return;
	if(etx_ < 0) return;
	if(ety_ < 0) return;
	if(etx_ >= w) return;
	if(ety_ >= h) return;

	stx = stx_;
	sty = sty_;
	etx = etx_;
	ety = ety_;

	nx = stx;
	ny = sty;

	dx = etx - stx;
	dy = ety - sty;

	if(dx < 0) step_x = -1;
	else step_x = 1;

	if(dy < 0) step_y = -1;
	else step_y = 1;

	dx = abs(dx*2);
	dy = abs(dy*2);

	if(dy>dx)
		fraction = dx * 2 - dy;
	else
		fraction = dy * 2 - dx;

	inited = true;
}

bool ZPath_Finding_Bresenham::GetNext(int &x, int &y)
{
	if(!inited) return false;

	if(dy>dx)
	{
		if(ny == ety) return false;

		if(fraction >= 0)
		{
			nx += step_x;
			fraction -= dy;
		}

		ny += step_y;
		fraction += dx;

		x = nx;
		y = ny;
	}
	else
	{
		if(nx == etx) return false;

		if(fraction >= 0)
		{
			ny += step_y;
			fraction -= dx;
		}

		nx += step_x;
		fraction += dy;

		x = nx;
		y = ny;
	}

	return true;
}

ZPath_Finding_RegionInfo::ZPath_Finding_RegionInfo()
{
	allocated = false;
	w = h = 0;
	robot_region = NULL;
	vehicle_region = NULL;
	ffQ = NULL;
}

void ZPath_Finding_RegionInfo::Init(int w_, int h_)
{
	int i, j;

	//first make sure its deleted
	Delete();

	//set our w,h
	w = w_;
	h = h_;

	//alloc ffQ
	ffQ = (ffnode *)malloc(w * h * sizeof(ffnode));
	//printf("ffq nodes:%d\n", w * h);

	//alloc regions
	robot_region = (int **)malloc(w * sizeof(int *));
	vehicle_region = (int **)malloc(w * sizeof(int *));
	for(i=0;i<w;i++)
	{
		robot_region[i] = (int *)malloc(h * sizeof(int));
		vehicle_region[i] = (int *)malloc(h * sizeof(int));

		for(j=0;j<h;j++)
		{
			robot_region[i][j] = 0;
			vehicle_region[i][j] = 0;
		}
	}

	allocated = true;
}

void ZPath_Finding_RegionInfo::Delete()
{
	if(!allocated) return;

	//free ffQ
	free(ffQ);

	//free regions
	for(int i=0;i<w;i++)
	{
		free(robot_region[i]);
		free(vehicle_region[i]);
	}
	free(robot_region);
	free(vehicle_region);

	ffQ = NULL;
	robot_region = NULL;
	vehicle_region = NULL;

	allocated = false;
}

void ZPath_Finding_RegionInfo::FloodFill_Recursive(int **region, int x, int y, int cur_region)
{
	//bounds?
	if(x < 0) return;
	if(y < 0) return;
	if(x >= w) return;
	if(y >= h) return;

	//this can be added?
	if(region[x][y] != -1) return;

	//add it
	region[x][y] = cur_region;

	//fill some more
	FloodFill_Recursive(region, x-1, y, cur_region); //left
	FloodFill_Recursive(region, x+1, y, cur_region); //right
	FloodFill_Recursive(region, x, y-1, cur_region); //up
	FloodFill_Recursive(region, x, y+1, cur_region); //down
}

void ZPath_Finding_RegionInfo::FloodFill_AddQ(int **region, int &i, int x, int y, int cur_region)
{
	if(x < 0) return;
	if(y < 0) return;
	if(x >= w) return;
	if(y >= h) return;

	//not what we want?
	if(region[x][y] != -1) return;

	//add it
	region[x][y] = cur_region;

	ffQ[i].x = x;
	ffQ[i].y = y;

	i++;
}

void ZPath_Finding_RegionInfo::FloodFill(int **region, int x, int y, int cur_region)
{
	int i;
	int ffQ_size;

	//clear ffQ
	//memset(ffQ, 0, w * h * sizeof(ffnode));

	ffQ_size = 0;
	FloodFill_AddQ(region, ffQ_size, x, y, cur_region);

	for(i=0;i<ffQ_size;i++)
	{
		ffnode n = ffQ[i];

		//push some more on!
		FloodFill_AddQ(region, ffQ_size, n.x-1, n.y, cur_region); //left
		FloodFill_AddQ(region, ffQ_size, n.x+1, n.y, cur_region); //right
		FloodFill_AddQ(region, ffQ_size, n.x, n.y-1, cur_region); //up
		FloodFill_AddQ(region, ffQ_size, n.x, n.y+1, cur_region); //down
	}

	//printf("Region:%d Size:%d\n", cur_region, ffQ_size);
}

void ZPath_Finding_RegionInfo::BuildRegions(map_pathfinding_info_tile **tile_info, int **region)
{
	int x, y;
	int cur_region;

	if(!allocated) 
	{
		printf("ZPath_Finding_RegionInfo::BuildRegions: not allocated\n");
		return;
	}

	//clear out the regions
	for(x=0;x<w;x++)
		for(y=0;y<h;y++)
		{
			if(tile_info[x][y].passable)
				region[x][y] = -1;
			else
				region[x][y] = -2;
		}

	//first region num
	cur_region = 0;

	//fill the regions
	for(x=0;x<w;x++)
		for(y=0;y<h;y++)
			if(region[x][y] == -1)
				FloodFill(region, x, y, cur_region++);
}

bool ZPath_Finding_RegionInfo::InSameRegion(int sx, int sy, int ex, int ey, bool is_robot)
{
	if(!allocated) 
	{
		printf("ZPath_Finding_RegionInfo::InSameRegion: not allocated\n");
		return true;
	}

	//divide all by 16
	sx >>= 4;
	sy >>= 4;
	ex >>= 4;
	ey >>= 4;

	if(sx < 0) return true;
	if(sy < 0) return true;
	if(ex < 0) return true;
	if(ey < 0) return true;
	if(sx >= w) return true;
	if(sy >= h) return true;
	if(ex >= w) return true;
	if(ey >= h) return true;

	if(is_robot)
	{
		//printf("R %d vs %d\n", robot_region[sx][sy], robot_region[ex][ey]);
		return robot_region[sx][sy] == robot_region[ex][ey];
	}
	else
		return vehicle_region[sx][sy] == vehicle_region[ex][ey];
}

ZPath_Finding_Response::ZPath_Finding_Response()
{
	path_finder = NULL;
	thread_id = 0;
	obj_ref_id = -1;
	tile_info = NULL;
    demolition_tile_info = NULL;
    terrain_tile_info = NULL;
    demolition_direct_possible = false;
    allows_demolition = false;
    demolition_penalty = 600; // original V1 fallback for special orders
    compare_demolition = false;
    route_cost = -1;
    cost_limit = -1;
    direct_travel_cost = -1.0;
	w = h = 0;
	start_x = start_y = 0;
	end_x = end_y = 0;
	kill_thread = false;

	existing_responses++;
}

ZPath_Finding_Response::~ZPath_Finding_Response()
{
	/*
	if(tile_info)
	{
		for(int i=0;i<w;i++)
			free(tile_info[i]);
		free(tile_info);
	}
	*/

	existing_responses--;
}

ZPath_Finding_Engine::ZPath_Finding_Engine()
{
	list_mutex = SDL_CreateMutex();
	path_robot_tile = NULL;
	path_vehicle_tile = NULL;
	path_robot_norocks_tile = NULL;
	path_vehicle_norocks_tile = NULL;
	next_thread_id = 0;
    destroyable_cell_count = 0;
	w = h = width_pix = height_pix = 0;
}

ZPath_Finding_Engine::~ZPath_Finding_Engine()
{
    // Workers can publish a response until joined. Never destroy their mutex first.
    DeleteAllTileInfo();
    SDL_DestroyMutex(list_mutex);
    list_mutex = NULL;
}

void ZPath_Finding_Engine::Push_Response(ZPath_Finding_Response *response)
{
	if(!response) return;

	SDL_LockMutex(list_mutex);
	respone_list.push_back(response);
	SDL_UnlockMutex(list_mutex);
}

void ZPath_Finding_Engine::DeleteTileInfo(map_pathfinding_info_tile ***the_list)
{
	map_pathfinding_info_tile **delete_list = *the_list;
	
	if(delete_list)
	{
		for(int i=0;i<w;i++)
			delete[] delete_list[i];
		delete[] delete_list;
	}

	*the_list = NULL;
}

void ZPath_Finding_Engine::DeleteAllTileInfo()
{
	//we must first stop all path finding threads
	Clear_Thread_List();
	Clear_Response_List(); // includes failures that have no worker

	DeleteTileInfo(&path_robot_tile);
	DeleteTileInfo(&path_robot_norocks_tile);
	DeleteTileInfo(&path_vehicle_tile);
	DeleteTileInfo(&path_vehicle_norocks_tile);
	region_info.Delete();
    road_prefix.clear();
    destroyable_cell_count = 0;
	w = h = width_pix = height_pix = 0;
}

void ZPath_Finding_Engine::AllocAllTileInfo()
{
    // Atomic cells are C++ objects: construct/destruct them, never malloc/memset.
    // Zero-initialized row pointers also make partial allocation safe to unwind.
    try
    {
        path_vehicle_tile = new map_pathfinding_info_tile*[w]();
        path_robot_tile = new map_pathfinding_info_tile*[w]();
        path_robot_norocks_tile = new map_pathfinding_info_tile*[w]();
        path_vehicle_norocks_tile = new map_pathfinding_info_tile*[w]();
        for(int i=0; i<w; ++i)
        {
            path_vehicle_tile[i] = new map_pathfinding_info_tile[h];
            path_robot_tile[i] = new map_pathfinding_info_tile[h];
            path_robot_norocks_tile[i] = new map_pathfinding_info_tile[h];
            path_vehicle_norocks_tile[i] = new map_pathfinding_info_tile[h];
        }
    }
    catch(...)
    {
        DeleteTileInfo(&path_vehicle_tile);
        DeleteTileInfo(&path_robot_tile);
        DeleteTileInfo(&path_robot_norocks_tile);
        DeleteTileInfo(&path_vehicle_norocks_tile);
        throw;
    }
}

void ZPath_Finding_Engine::ResetTileInfo(int w_, int h_)
{
	DeleteAllTileInfo();

	w = w_;
	h = h_;
	width_pix = w * 16;
	height_pix = h * 16;

	AllocAllTileInfo();

	if(!path_robot_tile) printf("ResetTileInfo::path_robot_tile not setup\n");
	if(!path_vehicle_tile) printf("ResetTileInfo::path_vehicle_tile not setup\n");
	if(!path_robot_norocks_tile) printf("ResetTileInfo::path_robot_norocks_tile not setup\n");
	if(!path_vehicle_norocks_tile) printf("ResetTileInfo::path_vehicle_norocks_tile not setup\n");

	//do region too
	region_info.Init(w, h);
}

void ZPath_Finding_Engine::SetTileInfo(int x, int y, int tile_type)
{
	float tile_speed;

	if(x < 0) return;
	if(y < 0) return;
	if(x >= w) return;
	if(y >= h) return;
	if(!path_robot_tile) return;
	if(!path_vehicle_tile) return;
	if(!path_robot_norocks_tile) return;
	if(!path_vehicle_norocks_tile) return;

	switch(tile_type)
	{
	case PF_NORMAL: 
		tile_speed = 1.0; 
		path_robot_tile[x][y].passable = true;
		path_vehicle_tile[x][y].passable = true;
		break;
	case PF_IMPASSABLE: 
		tile_speed = 1.0;
		path_robot_tile[x][y].passable = false;
		path_vehicle_tile[x][y].passable = false;
		break;
	case PF_WATER: 
		tile_speed = 1.0 / WATER_SPEED;
		path_robot_tile[x][y].passable = true;
		path_vehicle_tile[x][y].passable = false;
		break;
	case PF_ROAD: 
		tile_speed = 1.0 / ROAD_SPEED; // same speed used by ZMap::GetTileWalkSpeed
		path_robot_tile[x][y].passable = true;
		path_vehicle_tile[x][y].passable = true;
		break;
	default:
		printf("ZPath_Finding_Engine::SetTileInfo:error invalid tile_type:%d\n", tile_type);
		break;
	}

	path_robot_tile[x][y].side_weight = 100 * tile_speed;
	path_robot_tile[x][y].diag_weight = (1.414 * 100) * tile_speed;

	path_vehicle_tile[x][y].side_weight = path_robot_tile[x][y].side_weight;
	path_vehicle_tile[x][y].diag_weight = path_robot_tile[x][y].diag_weight;

	path_robot_norocks_tile[x][y].side_weight = path_robot_tile[x][y].side_weight;
	path_robot_norocks_tile[x][y].diag_weight = path_robot_tile[x][y].diag_weight;

	path_vehicle_norocks_tile[x][y].side_weight = path_robot_tile[x][y].side_weight;
	path_vehicle_norocks_tile[x][y].diag_weight = path_robot_tile[x][y].diag_weight;

	path_robot_norocks_tile[x][y].passable = path_robot_tile[x][y].passable.load();
	path_vehicle_norocks_tile[x][y].passable = path_vehicle_tile[x][y].passable.load();
}

void ZPath_Finding_Engine::SetTileWideWeights()
{
	int i,j;
	int **wide_side_weight;
	int **wide_diag_weight;

	if(w <= 0 || h <= 0 || !path_vehicle_tile || !path_vehicle_norocks_tile)
	{
		printf("SetTileWideWeights::not setup");	
		return;
	}

    // Build once during map initialization. Terrain weights do not change
    // when rocks or buildings change their passability.
    const int stride = h + 1;
    road_prefix.assign((size_t)(w+1)*stride, 0);
    for(int x=1; x<=w; ++x)
        for(int y=1; y<=h; ++y)
            road_prefix[x*stride+y] =
                road_prefix[(x-1)*stride+y] + road_prefix[x*stride+y-1]
                - road_prefix[(x-1)*stride+y-1]
                + (path_robot_tile[x-1][y-1].side_weight < 100 ? 1 : 0);

	//alloc
	wide_side_weight = (int**)malloc(w * sizeof(int*));
	wide_diag_weight = (int**)malloc(w * sizeof(int*));
	for(i=0;i<w;i++) 
	{
		wide_side_weight[i] = (int*)malloc(h * sizeof(int));
		wide_diag_weight[i] = (int*)malloc(h * sizeof(int));
	}

	//fill wide_weight table
	for(i=0;i<w;i++)
		for(j=0;j<h;j++)
		{
			int the_side_weight;
			int the_diag_weight;

			the_side_weight = path_vehicle_tile[i][j].side_weight;
			the_diag_weight = path_vehicle_tile[i][j].diag_weight;

			if(i+1<w)
			{
				the_side_weight += path_vehicle_tile[i+1][j].side_weight;
				the_diag_weight += path_vehicle_tile[i+1][j].diag_weight;
			}

			if(j+1<h)
			{
				the_side_weight += path_vehicle_tile[i][j+1].side_weight;
				the_diag_weight += path_vehicle_tile[i][j+1].diag_weight;
			}

			if(i+1<w && j+1<h)
			{
				the_side_weight += path_vehicle_tile[i+1][j+1].side_weight;
				the_diag_weight += path_vehicle_tile[i+1][j+1].diag_weight;
			}

			wide_side_weight[i][j] = the_side_weight;
			wide_diag_weight[i][j] = the_diag_weight;
		}

	//move wide_weight table over to the vehicle's weight tables
	for(i=0;i<w;i++)
		for(j=0;j<h;j++)
		{
			path_vehicle_tile[i][j].side_weight = wide_side_weight[i][j];
			path_vehicle_tile[i][j].diag_weight = wide_diag_weight[i][j];

			path_vehicle_norocks_tile[i][j].side_weight = path_vehicle_tile[i][j].side_weight;
			path_vehicle_norocks_tile[i][j].diag_weight = path_vehicle_tile[i][j].diag_weight;
		}

	//dealloc
	for(i=0;i<w;i++)
	{
		free(wide_side_weight[i]);
		free(wide_diag_weight[i]);
	}
	free(wide_side_weight);
	free(wide_diag_weight);

}

bool ZPath_Finding_Engine::TileOnMap(int tx, int ty)
{
	if(tx < 0) return false;
	if(ty < 0) return false;
	if(tx >= w) return false;
	if(ty >= h) return false;

	return true;
}

bool ZPath_Finding_Engine::TilePassable(int tx, int ty, bool is_robot)
{
	if(!TileOnMap(tx, ty)) return false;

	if(is_robot)
		return path_robot_tile[tx][ty].passable;
	else
		return path_vehicle_tile[tx][ty].passable;
}

bool ZPath_Finding_Engine::ShouldBeAbleToMoveTo(int sx, int sy, int ex, int ey, bool is_robot)
{
	//same region?
	if(!InSameRegion(sx, sy, ex, ey, is_robot)) return false;

	//end target ok?
	{
		int tx, ty;
		int cx, cy;

		tx = ex >> 4;
		ty = ey >> 4;

		if(is_robot)
		{
			if(!TilePassable(tx, ty, is_robot)) return false;
		}
		else
		{
			if(!TilePassable(tx, ty, is_robot)) return false; //top left
			if(!TilePassable(tx+1, ty, is_robot)) return false; //top right
			if(!TilePassable(tx, ty+1, is_robot)) return false; //bottom left
			if(!TilePassable(tx+1, ty+1, is_robot)) return false; //bottom right
		}
	}

	return true;
}

bool ZPath_Finding_Engine::InSameRegion(int sx, int sy, int ex, int ey, bool is_robot)
{
	return region_info.InSameRegion(sx, sy, ex, ey, is_robot);
}

void ZPath_Finding_Engine::RebuildRegions()
{
	region_info.BuildRegions(path_robot_tile, region_info.robot_region);
	region_info.BuildRegions(path_vehicle_tile, region_info.vehicle_region);
}

void ZPath_Finding_Engine::SetImpassable(int x, int y, bool impassable, bool destroyable)
{
	if(x < 0) return;
	if(y < 0) return;
	if(x >= w) return;
	if(y >= h) return;
	if(!path_robot_tile) return;
	if(!path_vehicle_tile) return;

    const bool was_destroyable = path_robot_norocks_tile[x][y].passable &&
        !path_robot_tile[x][y].passable;
    const bool now_destroyable = impassable && destroyable;
    destroyable_cell_count += (int)now_destroyable - (int)was_destroyable;
	path_robot_tile[x][y].passable = !impassable;
	path_vehicle_tile[x][y].passable = !impassable;

	path_robot_norocks_tile[x][y].passable = !impassable || destroyable;
	path_vehicle_norocks_tile[x][y].passable = !impassable || destroyable;

	//slow place for it
	//would be best placed after the point in which this is called
	//as this function is normally called in blocks
	//RebuildRegions();
}

bool ZPath_Finding_Engine::WithinImpassable(int x, int y, int w, int h, int &stop_x, int &stop_y, bool is_robot)
{
	map_pathfinding_info_tile **tile_check;
	int tx, ty, tex, tey, tw, th;
	int i, j;
	int i_pix, j_pix;

	//
	if(!path_robot_tile) return false;
	if(!path_vehicle_tile) return false;

	//simple map restrictions
	if(x < 0) return true;
	if(y < 0) return true;
	if(x + w >= width_pix) return true;
	if(y + h >= height_pix) return true;

	if(is_robot)
		tile_check = path_robot_tile;
	else
		tile_check = path_vehicle_tile;

	//set stuff
	//tx = x / 16;
	//ty = y / 16;
	//tw = w / 16;
	//th = h / 16;
	tx = x >> 4;
	ty = y >> 4;
	tw = w >> 4;
	th = h >> 4;

	if(w % 16) tw += 1;
	if(h % 16) th += 1;

	tex = tx + tw;
	tey = ty + th;

	//int max_tex = (width_pix / 16) - 1;
	//int max_tey = (height_pix / 16) - 1;
	int max_tex = (width_pix >> 4) - 1;
	int max_tey = (height_pix >> 4) - 1;
	if(tex > max_tex) tex = max_tex;
	if(tey > max_tey) tey = max_tey;

	//printf("WithinImpassable:(%d, %d)-(%d, %d)\n", tx, ty, tex, tey);

	//ok it is on the map, so what tiles might it intersect with?
	//for(i=tx, i_pix=tx*16; i<=tex; i++, i_pix+=16)
	//	for(j=ty, j_pix=ty*16; j<=tey; j++, j_pix+=16)
	for(i=tx, i_pix=tx<<4; i<=tex; i++, i_pix+=16)
		for(j=ty, j_pix=ty<<4; j<=tey; j++, j_pix+=16)
			if(!tile_check[i][j].passable)
			{
				//it is impassable, do we overlap it?
				if(x >= i_pix + 16) continue;
				if(y >= j_pix + 16) continue;
				if(x + w <= i_pix) continue;
				if(y + h <= j_pix) continue;

				stop_x = i << 4;
				stop_y = j << 4;

				//printf("caught at (%d, %d)\n", i, j);

				return true;
			}

	return false;
}

bool ZPath_Finding_Engine::HasDestroyableBarrier(int x, int y)
{
	if(!path_robot_tile) return false;
	if(!path_robot_norocks_tile) return false;

	if(x < 0) return false;
	if(y < 0) return false;
	if(x >= w) return false;
	if(y >= h) return false;

	return path_robot_norocks_tile[x][y].passable && !path_robot_tile[x][y].passable;
}

// Normal consumption: response has already been published; no list lock held.
// The worker never touches the list again after publishing it, so joining is safe.
void ZPath_Finding_Engine::Clear_Thread_Id(int thread_id)
{
    for(size_t i=0; i<thread_list.size(); ++i)
        if(thread_list[i].thread_id == thread_id)
        {
            if(thread_list[i].the_thread) SDL_WaitThread(thread_list[i].the_thread, NULL);
            thread_list.erase(thread_list.begin()+i);
            return;
        }
}

void ZPath_Finding_Engine::Take_Responses(vector<ZPath_Finding_Response*> &completed)
{
    // Only the engine's owner thread drains or cancels results. Workers publish
    // under list_mutex. Do not execute callbacks or join with that mutex held.
    SDL_LockMutex(list_mutex);
    completed.swap(respone_list); // caller supplies an empty vector
    SDL_UnlockMutex(list_mutex);
    for(size_t i=0; i<completed.size(); ++i)
        Clear_Thread_Id(completed[i]->thread_id);
}

void ZPath_Finding_Engine::Clear_Response_List()
{
    vector<ZPath_Finding_Response*> completed;
    Take_Responses(completed);
    for(size_t i=0; i<completed.size(); ++i) delete completed[i];
}

void ZPath_Finding_Engine::Clear_Thread_List()
{
    // Request cancellation together; do not hold list_mutex while waiting.
    for(size_t i=0; i<thread_list.size(); ++i)
        thread_list[i].response->kill_thread = true;
    for(size_t i=0; i<thread_list.size(); ++i)
        if(thread_list[i].the_thread) SDL_WaitThread(thread_list[i].the_thread, NULL);
    SDL_LockMutex(list_mutex);
    for(size_t i=0; i<thread_list.size(); ++i)
    {
        ZPath_Finding_Response *r = thread_list[i].response;
        respone_list.erase(std::remove(respone_list.begin(), respone_list.end(), r), respone_list.end());
        delete r;
    }
    thread_list.clear();
    SDL_UnlockMutex(list_mutex);
}

void ZPath_Finding_Engine::Kill_Thread_Id(int thread_id)
{
    ZPath_Finding_Response *cancelled = NULL;
    for(size_t i=0; i<thread_list.size(); ++i)
        if(thread_list[i].thread_id == thread_id)
        {
            cancelled = thread_list[i].response;
            cancelled->kill_thread = true;
            if(thread_list[i].the_thread) SDL_WaitThread(thread_list[i].the_thread, NULL);
            thread_list.erase(thread_list.begin()+i);
            break;
        }
    SDL_LockMutex(list_mutex);
    for(size_t i=0; i<respone_list.size(); ++i)
        if(respone_list[i]->thread_id == thread_id)
        {
            if(!cancelled) cancelled = respone_list[i];
            respone_list.erase(respone_list.begin()+i);
            break;
        }
    SDL_UnlockMutex(list_mutex);
    delete cancelled;
}

// Estimate ordinary travel time in equivalent normal-terrain pixels. Sample
// the unit centre, just as the actual movement code does. This also allows
// a clear straight route to beat an unnecessarily long grid route.
static double RouteSegmentCost(map_pathfinding_info_tile **terrain, int w, int h,
    int sx, int sy, int ex, int ey)
{
    const double dx = (double)ex-sx, dy = (double)ey-sy;
    const double length = sqrt(dx*dx + dy*dy);
    if(length == 0.0) return 0.0;
    if(!terrain) return length;
    const int samples = (int)ceil(length/4.0);
    double weight = 0.0;
    for(int i=0; i<samples; ++i)
    {
        const double t = (i+0.5)/samples;
        const int tx = (int)floor((sx+dx*t)/16.0);
        const int ty = (int)floor((sy+dy*t)/16.0);
        weight += (tx>=0 && ty>=0 && tx<w && ty<h)
            ? terrain[tx][ty].side_weight : 100;
    }
    return length*weight/(100.0*samples);
}

static double ReturnedRouteCost(const ZPath_Finding_Response *r)
{
    int x = r->start_x + (r->is_robot ? 0 : 8);
    int y = r->start_y + (r->is_robot ? 0 : 8);
    double cost = 0.0;
    for(size_t i=0; i<r->pf_point_list.size(); ++i)
    {
        const ZPath_Finding_AStar::pf_point &p = r->pf_point_list[i];
        cost += RouteSegmentCost(r->terrain_tile_info,r->w,r->h,x,y,p.x,p.y);
        x=p.x; y=p.y;
    }
    return cost;
}

bool ZPath_Finding_Engine::RoadCouldImprove(int sx, int sy, int ex, int ey,
    double direct_cost) const
{
    const double dx=(double)ex-sx, dy=(double)ey-sy;
    const double length=sqrt(dx*dx+dy*dy);
    // Local moves and routes already on the fastest terrain remain immediate.
    if(length < 32.0 || direct_cost <= length*((int)(100.0/ROAD_SPEED)/100.0)*1.02)
        return false;
    // A robot crossing water can also benefit from a dry-land detour.
    if(direct_cost > length*1.02) return true;
    if(road_prefix.empty()) return false;
    const double max_length = direct_cost / ((int)(100.0/ROAD_SPEED)/100.0);
    // Every possible time-saving road lies inside this ellipse's bounding box.
    // Pad by one tile to include any corner/edge intersection.
    const double max_sq=max_length*max_length;
    const double rx=0.5*sqrt(std::max(0.0,max_sq-dy*dy));
    const double ry=0.5*sqrt(std::max(0.0,max_sq-dx*dx));
    const double mx=(sx+(double)ex)*0.5, my=(sy+(double)ey)*0.5;
    const int x0=std::max(0,(int)floor((mx-rx)/16.0)-1);
    const int y0=std::max(0,(int)floor((my-ry)/16.0)-1);
    const int x1=std::min(w,(int)floor((mx+rx)/16.0)+2);
    const int y1=std::min(h,(int)floor((my+ry)/16.0)+2);
    if(x0>=x1 || y0>=y1) return false;
    const int stride=h+1;
    return road_prefix[x1*stride+y1]-road_prefix[x0*stride+y1]
        -road_prefix[x1*stride+y0]+road_prefix[x0*stride+y0] > 0;
}

int Find_Path_Thread(void *response_)
{
	ZPath_Finding_Response *response;

	//a check
	if(!response_) return 0;

	response = (ZPath_Finding_Response *)response_;

	// Diagnostics only: set to true when measuring long A* pauses.
	// SDL tick timing measures actual wall-clock A* computation, not
	// subsequent server retry cooldowns. No movement rules change.
	static const bool zod_diagnose_slow_paths = false;
	const Uint32 zod_astar_start_ms = zod_diagnose_slow_paths ? SDL_GetTicks() : 0;
	//process
    ZPath_Finding_AStar::Do_Astar(response);
    if(!response->kill_thread)
    {
        if(response->direct_travel_cost >= 0.0)
        {
            // Do not replace a safe straight route unless the weighted route
            // saves at least 5% (avoid pointless turns and tile-grid detours).
            if(response->pf_point_list.empty() ||
                ReturnedRouteCost(response) >= response->direct_travel_cost*0.95)
            {
                response->pf_point_list.clear();
                response->pf_point_list.push_back(ZPath_Finding_AStar::pf_point(
                    response->end_x,response->end_y));
            }
        }
        else if(response->demolition_tile_info &&
                (response->pf_point_list.empty() || response->compare_demolition))
        {
            // V2: if demolition is optional, keep the intact route unless
            // the alternative (travel + estimated shooting time) saves 5%.
            // A bounded second search uses the same worker and heuristic.
            const bool have_intact = !response->pf_point_list.empty();
            const int intact_cost = response->route_cost;
            int limit = -1;
            if(have_intact)
                limit = (int)(intact_cost * 0.95);

            // The cheapest possible movement plus one rock is a lower
            // bound on any useful demolition shortcut. Avoid a second
            // search when even this optimistic estimate cannot win.
            const int dx=abs(response->start_x/16-response->end_x/16);
            const int dy=abs(response->start_y/16-response->end_y/16);
            const int diagonal=std::min(dx,dy);
            const int min_travel=diagonal*(int)(141.4/ROAD_SPEED) +
                (std::max(dx,dy)-diagonal)*(int)(100.0/ROAD_SPEED);
            if(!have_intact || limit > min_travel + response->demolition_penalty)
            {
                vector<ZPath_Finding_AStar::pf_point> intact_route;
                intact_route.swap(response->pf_point_list);
                map_pathfinding_info_tile **intact_grid = response->tile_info;
                response->tile_info = response->demolition_tile_info;
                response->allows_demolition = true;
                response->cost_limit = limit;
                ZPath_Finding_AStar::Do_Astar(response);
                response->cost_limit = -1;
                response->tile_info = intact_grid;
                if(!response->kill_thread)
                {
                    if(have_intact && (response->pf_point_list.empty() ||
                        response->route_cost < 0 || response->route_cost >= limit))
                    {
                        response->pf_point_list.swap(intact_route);
                        response->route_cost = intact_cost;
                        response->allows_demolition = false;
                    }
                    else if(!have_intact && response->pf_point_list.empty() &&
                            response->demolition_direct_possible)
                    {
                        // Preserve V1's last-resort direct demolition route.
                        response->pf_point_list.push_back(ZPath_Finding_AStar::pf_point(
                            response->end_x,response->end_y));
                    }
                }
            }
        }
    }
	if(zod_diagnose_slow_paths)
	{
		const Uint32 elapsed_ms = SDL_GetTicks() - zod_astar_start_ms;
		if(elapsed_ms >= 300)
			printf("[ZOD A*] %u ms unit=%d from=(%d,%d) to=(%d,%d) path=%u\n",
				(unsigned int)elapsed_ms, response->obj_ref_id,
				response->start_x, response->start_y,
				response->end_x, response->end_y,
				(unsigned int)response->pf_point_list.size());
	}

	//push for server to collect
	if(!response->kill_thread)
		response->path_finder->Push_Response(response);
	// Owner joins and frees the response, including cancelled workers.

	return 1;
}

int ZPath_Finding_Engine::Find_Path(int sx, int sy, int ex, int ey, bool is_robot, bool has_explosives, int obj_ref_id, bool skip_direct_test, int demolition_penalty)
{
    // Never treat a destructible barrier as empty during the direct test.
    // Straight travel is still immediate whenever no quicker road is plausible.
    double direct_cost = -1.0;
    if(!skip_direct_test && Direct_Path_Possible(sx, sy, ex, ey, is_robot, false))
    {
        const int cx=sx+(is_robot ? 0 : 8), cy=sy+(is_robot ? 0 : 8);
        direct_cost=RouteSegmentCost(path_robot_tile,w,h,cx,cy,ex,ey);
        if(!RoadCouldImprove(cx,cy,ex,ey,direct_cost)) return 0;
    }
	ZPath_Finding_Response *response;
	SDL_Thread *the_thread = NULL;
	map_pathfinding_info_tile **tile_check;

	// Do not use the cached region map as an authoritative failure signal:
	// destructible obstacles can change passability without rebuilding regions.
	// A* reports an empty route if the actual tile map is disconnected.

	//inc
	next_thread_id++;

	//don't let it be zero
	if(!next_thread_id) next_thread_id++;

	//make data
	response = new ZPath_Finding_Response;

	//populate
	response->path_finder = this;
	response->thread_id = next_thread_id;
	response->obj_ref_id = obj_ref_id;
	response->start_x = sx;
	response->start_y = sy;
	response->end_x = ex;
	response->end_y = ey;
	response->is_robot = is_robot;
	response->kill_thread = false;

    // A* first sees the real, solid collision map. For normal orders,
    // the alternative grid is compared in the same worker if worthwhile.
    tile_check = is_robot ? path_robot_tile : path_vehicle_tile;
    response->w = w;
    response->h = h;
    response->tile_info = tile_check;
    response->terrain_tile_info = path_robot_tile;
    response->direct_travel_cost = direct_cost;
    if(demolition_penalty > 0)
    {
        response->compare_demolition = true;
        response->demolition_penalty = demolition_penalty;
    }
    if(has_explosives && destroyable_cell_count > 0)
    {
        response->demolition_tile_info = is_robot ? path_robot_norocks_tile : path_vehicle_norocks_tile;
        response->demolition_direct_possible = !skip_direct_test && direct_cost < 0.0 &&
            Direct_Path_Possible(sx,sy,ex,ey,is_robot,true);
    }

	// No tile grid (e.g. a failed map load) must return a failed route.
	if(!response->tile_info)
	{
		Push_Response(response);
		return next_thread_id;
	}

	//start thread
	the_thread = SDL_CreateThread(Find_Path_Thread, (void*)response);

	// Thread creation failure must not mean "direct path" (which is 0).
	// Queue an empty failed result under this request ID; the object will
	// stop safely and retry instead of driving through blocked terrain.
	if(!the_thread)
	{
		printf("ZPath_Finding_Engine::Could not create Find_Path_Thread\n");
		Push_Response(response);
		return next_thread_id;
	}

	//printf("start thread:%d\n", next_thread_id);

	//add thread to thread list
	thread_list.push_back(ZPath_Finding_Thread_Entry(next_thread_id, the_thread, response));


	return next_thread_id;

}

bool ZPath_Finding_Engine::Direct_Path_Possible(int sx, int sy, int ex, int ey, bool is_robot, bool has_explosives)
{
	const int crawl_dist = 4;
	int A, B, C;
	double dx, dy;
	double x, y;
	int dist_left;
	double dist_check;
	double under;
	map_pathfinding_info_tile **tile_check;
	int bc;

	if(!path_robot_tile || !path_vehicle_tile ||
	   !path_robot_norocks_tile || !path_vehicle_norocks_tile) return false;
	//already there...?
	if(sx == ex && sy == ey) return true;

	x = sx;
	y = sy;
	dx = ex - sx;
	dy = ey - sy;
	dist_left = sqrt((dx * dx) + (dy * dy));
	
	double hyp;

	if(fabs(dx) > fabs(dy))
		hyp = 1 / cos(atan2(fabs(dy), fabs(dx)));
	else
		hyp = 1 / sin(atan2(fabs(dy), fabs(dx)));

	//hyp = 1.414;
	//hyp += 0.08;

	//if(hyp > 1.414) hyp = 1.414;

	//printf("1.41:%lf\n", hyp );

	if(is_robot)
	{
		if(has_explosives)
			tile_check = path_robot_norocks_tile;
		else
			tile_check = path_robot_tile;

		dist_check = (8 + 8 + 1) * hyp;
		bc = 1;
	}
	else
	{
		if(has_explosives)
			tile_check = path_vehicle_norocks_tile;
		else
			tile_check = path_vehicle_tile;

		dist_check = (16 + 8 + 1) * hyp;
		bc = 2;
	}

	//line equation
	A = -1 * (sy - ey);
	B = (sx - ex);
	C = -1 * (A * sx + B * sy);

	//distance from line equals...
	//|Ax + Bn + C| / (A^2 + B^2)^0.5
	under = sqrt((double)((A * A) + (B * B)));

	//set right
	dx /= dist_left;
	dy /= dist_left;
	dx *= crawl_dist;
	dy *= crawl_dist;
	dist_check *= under;

	//go along the line and check
	//all the sectors around it
	while(dist_left > 0)
	{
		int ctx, cty;
		int tx, ty;
		int etx, ety;

		ctx = x / 16;
		cty = y / 16;

		etx = ctx+bc;
		ety = cty+bc;

		for(tx=ctx-bc;tx<=etx;tx++)
			for(ty=cty-bc;ty<=ety;ty++)
			{
				int center_x, center_y;

				if(tx < 0) continue;
				if(ty < 0) continue;
				if(tx >= w) continue;
				if(ty >= h) continue;

				//it good?
				if(tile_check[tx][ty].passable) continue;

				//distance smaller then our minimal?
				center_x = (tx * 16) + 8;
				center_y = (ty * 16) + 8;
				if(dist_check >= abs(A * center_x + B * center_y + C))
				{
					//printf("Direct_Path_Possible::not possible...\n");
					return false;
				}
			}

		x += dx;
		y += dy;
		dist_left -= crawl_dist;
	}

	//didn't find a false so I guess its good
	//printf("Direct_Path_Possible::possible...\n");
	return true;
}
