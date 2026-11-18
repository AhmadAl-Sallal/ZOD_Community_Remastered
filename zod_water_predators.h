#ifndef ZOD_WATER_PREDATORS_H
#define ZOD_WATER_PREDATORS_H

#include "zobject.h"
#include "zod_predator_rules.h"

namespace ZODPredators
{
inline bool PlanetEnabled(int planet) { return planet == CITY || planet == JUNGLE; }

inline bool OnBridge(const vector<ZObject*> &buildings, int x, int y)
{
    for(ZObject *obj : buildings)
    {
        if(!obj || obj->IsDestroyed()) continue;
        unsigned char type, id;
        obj->GetObjectID(type, id);
        if(type != BUILDING_OBJECT || (id != BRIDGE_VERT && id != BRIDGE_HORZ)) continue;
        int bx,by,w,h;
        obj->GetCords(bx,by);
        obj->GetDimensionsPixel(w,h);
        if(x >= bx && y >= by && x-bx < w && y-by < h) return true;
    }
    return false;
}

inline bool OpenWaterAt(ZMap &map, const vector<ZObject*> &buildings, int x, int y)
{
    const int planet = map.GetMapBasics().terrain_type;
    if(!PlanetEnabled(planet) || !map.Loaded()) return false;
    const int index = map.GetTileIndex(x,y);
    if(index < 0) return false;
    const unsigned tile = map.GetTile(static_cast<unsigned>(index)).tile;
    if(tile >= MAX_PLANET_TILES) return false;
    const palette_tile_info &info = ZMap::GetMapPaletteTileInfo(static_cast<planet_type>(planet),tile);
    if(!info.is_water || !info.is_passable || info.is_road) return false;
    return !OnBridge(buildings,x,y);
}

inline bool RobotEligible(ZObject *obj, double now)
{
    if(!obj || obj->IsDestroyed() || obj->KillMe(now) || obj->GetOwner() == NULL_TEAM) return false;
    unsigned char type,id;
    obj->GetObjectID(type,id);
    return type == ROBOT_OBJECT;
}

// Hut animals currently roam on dry ground; allow a purely visual shoreline
// encounter instead of changing their pathfinding or making them swim.
inline bool FindShoreWater(ZMap &map, const vector<ZObject*> &buildings,
                           int x, int y, int &wx, int &wy)
{
    if(OnBridge(buildings,x,y)) return false;
    static const int offsets[][2] = {{0,0},{-6,0},{6,0},{0,-6},{0,6}};
    for(const auto &d : offsets)
        if(OpenWaterAt(map,buildings,x+d[0],y+d[1]))
        { wx=x+d[0]; wy=y+d[1]; return true; }
    return false;
}
}
#endif
