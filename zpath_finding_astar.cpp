#include <SDL/SDL_timer.h>
#include "constants.h"
#include <algorithm>
#include "zpath_finding_astar.h"
#include "common.h"

using namespace COMMON;

namespace ZPath_Finding_AStar
{
	inline int gf(int &cx, int &cy, int &nx, int &ny, ZPath_Finding_Response *response);
	inline int hf(int sx, int sy, int fx, int fy)
    {
        // Admissible octile lower bound in the same units as edge costs.
        // The old Manhattan heuristic used 1 vs. edge weights of 100-400.
        const int dx=abs(sx-fx), dy=abs(sy-fy);
        const int diagonal=std::min(dx,dy);
        return diagonal*(int)(141.4/ROAD_SPEED) +
            (std::max(dx,dy)-diagonal)*(int)(100.0/ROAD_SPEED);
    }
	inline void add_to_open_list(pf_point &cp, pf_point snp, pf_point_array &open_list, pf_point_array &closed_list, ZPath_Finding_Response *response);
	inline bool tile_ok(int &sx, int &sy, int &ex, int &ey, ZPath_Finding_Response *response);
	inline void push_in_neighbors(pf_point &cp, pf_point_array &open_list, pf_point_array &closed_list, ZPath_Finding_Response *response);
	inline void remove_from_open(pf_point &tp, pf_point_array &open_list, pf_point_array &closed_list);
	inline pf_point lowest_f_cost(pf_point_array &open_list);

	inline int gf(int &cx, int &cy, int &nx, int &ny, ZPath_Finding_Response *response)
	{
        map_pathfinding_info_tile **terrain = response->terrain_tile_info;
        const bool diagonal = cx!=nx && cy!=ny;
        int cost;
        if(!terrain)
            cost = diagonal ? response->tile_info[nx][ny].diag_weight
                            : response->tile_info[nx][ny].side_weight;
        else if(response->is_robot)
        {
            // Robot centres lie at tile centres: half the leg in each tile.
            const map_pathfinding_info_tile &a=terrain[cx][cy], &b=terrain[nx][ny];
            cost=diagonal ? (a.diag_weight+b.diag_weight)/2 : (a.side_weight+b.side_weight)/2;
        }
        else
        {
            // Vehicle centres are at (tile*16+16), not the tile index of
            // their top-left collision box. The centre of an A* edge lies
            // in one terrain tile, even on diagonal edges. Keep footprint
            // checks in tile_ok exactly as before, but price actual speed.
            const int tx=(cx+nx+2)/2, ty=(cy+ny+2)/2;
            const map_pathfinding_info_tile &t=terrain[tx][ty];
            cost=diagonal ? t.diag_weight : t.side_weight;
        }
        // Charge the estimated time for each newly encountered rock, not
        // the overlap of consecutive footprints. Non-destructible cells are
        // still rejected by tile_ok before this function is called.
        if(response->allows_demolition)
        {
            const int extent=response->is_robot ? 1 : 2;
            for(int x=nx; x<nx+extent; ++x)
                for(int y=ny; y<ny+extent; ++y)
                {
                    if(x>=cx && x<cx+extent && y>=cy && y<cy+extent) continue;
                    if(!response->path_finder->TilePassable(x,y,response->is_robot))
                        cost+=response->demolition_penalty;
                }
            // tile_ok also tests two swept corner cells on diagonals.
            // For a voluntary demolition comparison those rocks must be
            // priced as well, otherwise a diagonal can look free.
            // Special orders retain V1's original fallback cost model.
            if(diagonal && response->compare_demolition)
            {
                const int ax=nx>cx ? nx+extent-1 : nx;
                const int ay=ny>cy ? cy : cy+extent-1;
                const int bx=nx>cx ? cx : cx+extent-1;
                const int by=ny>cy ? ny+extent-1 : ny;
                if(!response->path_finder->TilePassable(ax,ay,response->is_robot))
                    cost+=response->demolition_penalty;
                if(!response->path_finder->TilePassable(bx,by,response->is_robot))
                    cost+=response->demolition_penalty;
            }
        }
        return cost;
	}

	inline void add_to_open_list(pf_point &cp, pf_point np, pf_point_array &open_list, pf_point_array &closed_list, ZPath_Finding_Response *response)
	{
		if(!tile_ok(cp.x, cp.y, np.x, np.y, response)) return;

		const int new_g = cp.g + gf(cp.x, cp.y, np.x, np.y, response);
		const int estimate = hf(np.x, np.y, response->end_x / 16, response->end_y / 16);
        if(response->cost_limit >= 0 && new_g + estimate >= response->cost_limit) return;
		const int open_i = open_list.point_index[np.x][np.y];

		// A cheaper route to an existing open node must update its parent/cost.
		if(open_i != -1)
		{
			pf_point &existing = open_list.list[open_i];
			if(new_g >= existing.g) return;

			existing.g = new_g;
			existing.f = existing.g + existing.h;
			existing.px = cp.x;
			existing.py = cp.y;
			return;
		}

		// Reopen a closed node if a cheaper path is discovered.
		// In particular, use the actual list index, not the boolean value of != -1.
		const int closed_i = closed_list.point_index[np.x][np.y];
		if(closed_i != -1)
		{
			const pf_point &existing = closed_list.list[closed_i];
			if(new_g >= existing.g) return;

			closed_list.RemovePoint(np.x, np.y);
		}

		np.g = new_g;
		np.h = estimate;
		np.f = np.g + np.h;
		np.px = cp.x;
		np.py = cp.y;
		open_list.AddPoint(np);
	}

	inline bool tile_ok(int &sx, int &sy, int &ex, int &ey, ZPath_Finding_Response *response)
	{
		map_pathfinding_info_tile **tile_info = response->tile_info;

		if(response->is_robot)
		{
			if(ex < 0) return false;
			if(ey < 0) return false;
			if(ex >= response->w) return false;
			if(ey >= response->h) return false;

			if(!tile_info[ex][ey].passable) return false;

			//stop here if this is a side move
			if(ex == sx || ey == sy) return true;

			//do diag checks
			if(!tile_info[sx+(ex-sx)][sy].passable) return false;
			if(!tile_info[sx][sy+(ey-sy)].passable) return false;
		}
		else
		{
			if(ex < 0) return false;
			if(ey < 0) return false;
			if(ex+1 >= response->w) return false;
			if(ey+1 >= response->h) return false;

			if(!tile_info[ex][ey].passable) return false;
			if(!tile_info[ex+1][ey].passable) return false;
			if(!tile_info[ex][ey+1].passable) return false;
			if(!tile_info[ex+1][ey+1].passable) return false;

			//stop here if this is a side move
			if(ex == sx || ey == sy) return true;

			//what kind of diag is it?
			if(ex > sx && ey < sy) //upper right
			{
				if(!tile_info[sx][sy-1].passable) return false;
				if(!tile_info[sx+2][sy+1].passable) return false;
			}
			else if(ex < sx && ey < sy) //upper left
			{
				if(!tile_info[sx+1][sy-1].passable) return false;
				if(!tile_info[sx-1][sy+1].passable) return false;
			}
			else if(ex > sx && ey > sy) //lower right
			{
				if(!tile_info[sx+2][sy].passable) return false;
				if(!tile_info[sx][sy+2].passable) return false;
			}
			else //lower left
			{
				if(!tile_info[sx-1][sy].passable) return false;
				if(!tile_info[sx+1][sy+2].passable) return false;
			}
		}

		return true;
	}

	inline void push_in_neighbors(pf_point &cp, pf_point_array &open_list, pf_point_array &closed_list, ZPath_Finding_Response *response)
	{
		int &x = cp.x;
		int &y = cp.y;

		add_to_open_list(cp, pf_point(x-1, y-1), open_list, closed_list, response);
		add_to_open_list(cp, pf_point(x-1, y), open_list, closed_list, response);
		add_to_open_list(cp, pf_point(x-1, y+1), open_list, closed_list, response);

		add_to_open_list(cp, pf_point(x, y-1), open_list, closed_list, response);
		add_to_open_list(cp, pf_point(x, y+1), open_list, closed_list, response);

		add_to_open_list(cp, pf_point(x+1, y-1), open_list, closed_list, response);
		add_to_open_list(cp, pf_point(x+1, y), open_list, closed_list, response);
		add_to_open_list(cp, pf_point(x+1, y+1), open_list, closed_list, response);
	}

	inline void remove_from_open(pf_point &tp, pf_point_array &open_list, pf_point_array &closed_list)
	{
		//for(vector<pf_point>::iterator i=open_list.begin();i!=open_list.end();)
		open_list.RemovePoint(tp.x, tp.y);
		//for(int i=0;i<open_list.size;)
		//{
		//	pf_point &ip = open_list.list[i];

		//	if(ip.x == tp.x && ip.y == tp.y)
		//	{
		//		//i = open_list.erase(i);
		//		open_list.size--;
		//		open_list.list[i] = open_list.list[open_list.size];
		//	}
		//	else
		//		i++;
		//}

		//closed_list.push_back(tp);
		closed_list.AddPoint(tp);
		//if(closed_list.size >= closed_list.alloc_size)
		//{
		//	closed_list.alloc_size += 1000;
		//	closed_list.list = (pf_point*)realloc(closed_list.list, closed_list.alloc_size * sizeof(pf_point));
		//}
		//closed_list.list[closed_list.size++] = tp;
		
	}

	inline pf_point lowest_f_cost(pf_point_array &open_list)
	{
		pf_point lowest_found;

		//lowest_found = *open_list.begin();
		lowest_found = *open_list.list;

		//for(vector<pf_point>::iterator i=open_list.begin();i!=open_list.end();i++)
		for(int i=0;i<open_list.size;i++)
		{
			pf_point &ip = open_list.list[i];

			if(lowest_found.f > ip.f || (lowest_found.f == ip.f && lowest_found.h > ip.h))
				lowest_found = ip;
		}
			
		//not found?
		return lowest_found;
	}

	int the_dir(pf_point &lp, pf_point &cp)
	{
		if(cp.x > lp.x)
		{
			if(cp.y == lp.y)
				return 0;
			else if(cp.y < lp.y)
				return 1;
			else
				return 7;
		}
		else if(cp.x == lp.x)
		{
			if(cp.y > lp.y)
				return 2;
			else
				return 6;
		}
		else
		{
			if(cp.y < lp.y)
				return 3;
			else if(cp.y > lp.y)
				return 5;
			else
				return 4;
		}

		printf("ZPath_Finding_AStar::dir = -1?\n");
		return -1;
	}

	void Do_Astar(ZPath_Finding_Response *response)
	{
		const int ticks_until_pause = 180;
		int total_ticks;
		int pause_ticks;
		double start_time, end_time;
		//vector<pf_point> open_list;
		//vector<pf_point> closed_list;
		pf_point_array open_list;
		pf_point_array closed_list;
		pf_point cp;

		//check
		if(!response) return;
        response->route_cost = -1;
        response->pf_point_list.clear();

		int start_x = response->start_x / 16;
		int start_y = response->start_y / 16;
		int end_x = response->end_x / 16;
		int end_y = response->end_y / 16;

		//start and end ok?
		if(!tile_ok(start_x, start_y, start_x, start_y, response) || 
			!tile_ok(end_x, end_y, end_x, end_y, response))
		{
			// An invalid footprint is not a route. An empty result means failure.
			response->pf_point_list.clear();
			return;
		}

		//printf("Do_Astar::start:[%d,%d]\n", response->start_x, response->start_y);
		//printf("Do_Astar::end:[%d,%d]\n", response->end_x, response->end_y);

		//init lists
		{
			int size;

			open_list.alloc_size = 1000;
			open_list.list = (pf_point*)malloc(open_list.alloc_size * sizeof(pf_point));
			open_list.size = 0;
			open_list.InitPointIndex(response->w, response->h);

			closed_list.alloc_size = 1000;
			closed_list.list = (pf_point*)malloc(closed_list.alloc_size * sizeof(pf_point));
			closed_list.size = 0;
			closed_list.InitPointIndex(response->w, response->h);
		}

		//open_list.push_back(pf_point(start_x, start_y));
		pf_point sp = pf_point(start_x, start_y);
        sp.h = hf(start_x,start_y,end_x,end_y);
        sp.f = sp.h;
        if(response->cost_limit >= 0 && sp.f >= response->cost_limit)
        {
            open_list.FreeList();
            open_list.FreePointIndex();
            closed_list.FreeList();
            closed_list.FreePointIndex();
            return;
        }
		sp.px = start_x;
		sp.py = start_y;
		open_list.AddPoint(sp);
		//open_list.list[open_list.size++] = pf_point(start_x, start_y);

		//start_time = current_time();
		total_ticks = 0;
		pause_ticks = 0;
		while(open_list.size)
		{
			cp = lowest_f_cost(open_list);

			if(response->kill_thread)
			{
				open_list.FreeList();
				open_list.FreePointIndex();
				closed_list.FreeList();
				closed_list.FreePointIndex();
				return;
			}

			if(cp.x == end_x && cp.y == end_y)
			{
				//printf("end found\n");
				break;
			}

			remove_from_open(cp, open_list, closed_list);

			if(response->kill_thread) 
			{
				open_list.FreeList();
				open_list.FreePointIndex();
				closed_list.FreeList();
				closed_list.FreePointIndex();
				return;
			}

			push_in_neighbors(cp, open_list, closed_list, response);

			//printf("current_point:[%d,%d]\n", cp.x, cp.y);

			if(response->kill_thread)
			{
				open_list.FreeList();
				open_list.FreePointIndex();
				closed_list.FreeList();
				closed_list.FreePointIndex();
				return;
			}

			total_ticks++;
			pause_ticks++;
			if(pause_ticks >= ticks_until_pause)
			{
				pause_ticks = 0;
				SDL_Delay(1);
			}

			if(response->kill_thread)
			{
				open_list.FreeList();
				open_list.FreePointIndex();
				closed_list.FreeList();
				closed_list.FreePointIndex();
				return;
			}
		}
		//end_time = current_time();


		vector<pf_point> final_path;
		if(cp.x == end_x && cp.y == end_y)
		while(1)
		{
			int i;

			final_path.insert(final_path.begin(), cp);

			//for(vector<pf_point>::iterator i=close_list.begin();;i++)
			// The start point has no parent; never read its uninitialized parent.
			if(cp.x == start_x && cp.y == start_y)
				break;

			// The index table avoids an out-of-bounds read and a linear search.
			if(cp.px < 0 || cp.py < 0 || cp.px >= response->w || cp.py >= response->h)
			{
				final_path.clear();
				break;
			}

			i = closed_list.point_index[cp.px][cp.py];
			if(i < 0 || i >= closed_list.size)
			{
				final_path.clear();
				break;
			}
			cp = closed_list.list[i];
		}

		//remove redundents
		int previous_dir = -1;
		int cur_dir;
		for(vector<pf_point>::iterator i=final_path.begin();i!=final_path.end();)
		{
			if(i!=final_path.begin() && i+1!=final_path.end())
			{
				previous_dir = the_dir(*(i-1), *(i));
				cur_dir = the_dir(*i, *(i+1));

				if(previous_dir == cur_dir)
					i = final_path.erase(i);
				else
					i++;
			}
			else
				i++;
		}


		//convert list to actual x,y cords
		for(vector<pf_point>::iterator i=final_path.begin();i!=final_path.end();i++)
		{
			i->x *= 16;
			i->y *= 16;

			if(response->is_robot)
			{
				i->x += 8;
				i->y += 8;
			}
			else
			{
				i->x += 16;
				i->y += 16;
			}
			//printf("Do_Astar::found point:[%d,%d]\n", i->x, i->y);
		}

		// When the open list is exhausted (or the parent chain is invalid),
		// never turn the requested endpoint into a fake one-point route.
		if(!final_path.empty())
        {
            response->route_cost = final_path.back().g;
			final_path.push_back(pf_point(response->end_x, response->end_y));
        }
		
		response->pf_point_list = final_path;
		//return final_path;

		//printf("Do_Astar::finished, ticks:%d time:%lf ... [%d,%d] to [%d,%d]\n", total_ticks, end_time - start_time, response->start_x, response->start_y, response->end_x, response->end_y);

		//dealloc
		{
			open_list.FreeList();
			open_list.FreePointIndex();
			closed_list.FreeList();
			closed_list.FreePointIndex();
		}
	}

}