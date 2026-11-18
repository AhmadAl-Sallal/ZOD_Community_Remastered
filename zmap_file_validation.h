#ifndef _ZMAP_FILE_VALIDATION_H_
#define _ZMAP_FILE_VALIDATION_H_
#include "constants.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <new>
#include <cstddef>

enum map_object_type
{
	ROCK_OBJECT,
	BRIDGE_OBJECT,
	BUILDING_OBJECT,
	CANNON_OBJECT,
	VEHICLE_OBJECT,
	ROBOT_OBJECT,
	ANIMAL_OBJECT,
	MAP_ITEM_OBJECT,
	MAX_MAP_OBJECT_TYPES
};

const string map_object_type_string[MAX_MAP_OBJECT_TYPES] =
{
	"rock", "bridge", "building", "cannon", "vehicle", "robot", "animal", "map_item"
};

class map_basics
{
public:
	map_basics() { clear(); }

	void clear()
	{
		width=0;
		height=0;
		map_name[0] = 0;
		player_count = 0;
		object_count = 0;
		terrain_type = 0;
		zone_count = 0;
	}

	unsigned short width;
	unsigned short height;
	char map_name[50];
	unsigned char player_count;
	unsigned short object_count;
	unsigned char terrain_type;
	unsigned short zone_count;
};

struct map_zone
{
	unsigned short x, y, w, h; //in tile form
};

struct map_object_old
{
	unsigned short x,y; //this is in tile form, not pixel
	char owner;
	unsigned char object_type;
	unsigned char object_id;
	char blevel;
};

struct map_object
{
	unsigned short x,y; //this is in tile form, not pixel
	char owner;
	unsigned char object_type;
	unsigned char object_id;
	char blevel;
	unsigned short extra_links;
	int health_percent;
};

struct map_tile
{
	unsigned short tile;
};

// Original native little-endian .map layout, unchanged.
static_assert(sizeof(map_basics)==62 && sizeof(map_zone)==8 &&
              sizeof(map_object)==16 && sizeof(map_tile)==2,
              "Unexpected ZOD map file layout");
static const int ZOD_MAX_MAP_SIDE = 512;
static const int ZOD_MAX_MAP_OBJECTS = 16384;
static const int ZOD_MAX_MAP_ZONES = 1024;
static const int ZOD_MAX_MAP_BYTES = 4 * 1024 * 1024;

inline bool ZODValidateMapBytes(const char *data, std::size_t size, const char **why = NULL)
{
#define ZOD_MAP_REJECT(reason) do { if(why) *why = reason; return false; } while(0)
    if(why) *why = NULL;
    if(!data || size < sizeof(map_basics) || size > ZOD_MAX_MAP_BYTES)
        ZOD_MAP_REJECT("invalid file size");
    map_basics basic;
    memcpy(&basic, data, sizeof(basic));
    if(!basic.width || !basic.height || basic.width > ZOD_MAX_MAP_SIDE || basic.height > ZOD_MAX_MAP_SIDE)
        ZOD_MAP_REJECT("invalid map dimensions (maximum 512 x 512)");
    if(basic.terrain_type >= MAX_PLANET_TYPES || basic.player_count >= MAX_TEAM_TYPES)
        ZOD_MAP_REJECT("invalid terrain or player count");
    if(!memchr(basic.map_name, 0, sizeof(basic.map_name)))
        ZOD_MAP_REJECT("unterminated map name");
    if(basic.object_count > ZOD_MAX_MAP_OBJECTS || basic.zone_count > ZOD_MAX_MAP_ZONES)
        ZOD_MAP_REJECT("too many map objects or zones");
    const std::size_t cells = (std::size_t)basic.width * basic.height;
    const std::size_t expected = sizeof(basic) + (std::size_t)basic.zone_count * sizeof(map_zone)
        + (std::size_t)basic.object_count * sizeof(map_object) + cells * sizeof(map_tile);
    if(expected != size) ZOD_MAP_REJECT("file length does not match map header");
    const char *pos = data + sizeof(basic);
    for(unsigned int i=0; i<basic.zone_count; ++i, pos+=sizeof(map_zone))
    {
        map_zone z; memcpy(&z,pos,sizeof(z));
        if(!z.w || !z.h || (unsigned int)z.x+z.w > basic.width || (unsigned int)z.y+z.h > basic.height)
            ZOD_MAP_REJECT("zone outside map bounds");
    }
    for(unsigned int i=0; i<basic.object_count; ++i, pos+=sizeof(map_object))
    {
        map_object o; memcpy(&o,pos,sizeof(o));
        if(o.x >= basic.width || o.y >= basic.height || (unsigned char)o.owner >= MAX_TEAM_TYPES)
            ZOD_MAP_REJECT("object position or owner outside supported range");
        if(o.health_percent < 0 || o.health_percent > 100) ZOD_MAP_REJECT("invalid object health percent");
        switch(o.object_type)
        {
            case BUILDING_OBJECT:
                if(o.object_id >= MAX_BUILDING_TYPES || o.blevel < 0 || o.blevel >= MAX_BUILDING_LEVELS)
                    ZOD_MAP_REJECT("invalid building type or level");
                if(o.object_id == BRIDGE_VERT && ((unsigned int)o.x+4 > basic.width || (unsigned int)o.y+5+o.extra_links > basic.height))
                    ZOD_MAP_REJECT("vertical bridge outside map");
                if(o.object_id == BRIDGE_HORZ && ((unsigned int)o.x+5+o.extra_links > basic.width || (unsigned int)o.y+4 > basic.height))
                    ZOD_MAP_REJECT("horizontal bridge outside map");
                break;
            case CANNON_OBJECT: if(o.object_id >= MAX_CANNON_TYPES) ZOD_MAP_REJECT("invalid cannon type"); break;
            case VEHICLE_OBJECT: if(o.object_id >= MAX_VEHICLE_TYPES) ZOD_MAP_REJECT("invalid vehicle type"); break;
            case ROBOT_OBJECT: if(o.object_id >= MAX_ROBOT_TYPES) ZOD_MAP_REJECT("invalid robot type"); break;
            case MAP_ITEM_OBJECT: if(o.object_id >= MAX_ITEM_TYPES) ZOD_MAP_REJECT("invalid map item type"); break;
            default: ZOD_MAP_REJECT("unsupported map object type");
        }
    }
    for(std::size_t i=0; i<cells; ++i, pos+=sizeof(map_tile))
    {
        map_tile t; memcpy(&t,pos,sizeof(t));
        if(t.tile >= MAX_PLANET_TILES) ZOD_MAP_REJECT("tile index outside palette");
    }
#undef ZOD_MAP_REJECT
    return true;
}

inline bool ZODReadMapFileBytes(const char *filename, std::vector<char> &bytes)
{
    bytes.clear();
    if(!filename || !*filename) return false;
    FILE *fp = fopen(filename,"rb");
    if(!fp) { fprintf(stderr,"Cannot open map '%s'\n",filename); return false; }
    bool ok = fseek(fp,0,SEEK_END)==0;
    long length = ok ? ftell(fp) : -1;
    ok = length >= (long)sizeof(map_basics) && length <= ZOD_MAX_MAP_BYTES && fseek(fp,0,SEEK_SET)==0;
    if(ok)
    {
        try { bytes.resize((std::size_t)length); }
        catch(const std::bad_alloc &) { ok=false; }
    }
    if(ok) ok = fread(bytes.data(),1,bytes.size(),fp)==bytes.size() && fgetc(fp)==EOF && !ferror(fp);
    fclose(fp);
    const char *reason = "read/size error";
    if(ok) ok = ZODValidateMapBytes(bytes.data(),bytes.size(),&reason);
    if(!ok) { fprintf(stderr,"Invalid map '%s': %s\n",filename,reason?reason:"unknown error"); bytes.clear(); }
    return ok;
}
#endif
