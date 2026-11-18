#ifndef _ZMINIMAP_H_
#define _ZMINIMAP_H_

#include <vector>
#include "zsdl.h"
#include "zmap.h"
#include "zobject.h"

using namespace std;

#define MINIMAP_W_MAX (647 - 555)
#define MINIMAP_H_MAX (388 - 299)

class ZMiniMap
{
public:
	ZMiniMap();

	void Setup(ZMap *zmap_, vector<ZObject*> *object_list_);
	void Setup_Boundaries();
	// Optional per-instance size. Other minimaps retain their original defaults.
	void SetSize(int width, int height);
	int GetWidth() const { return max_width; }
	int GetHeight() const { return max_height; }
	void DoRender(SDL_Surface *dest, int x, int y);
	bool ClickedMap(int x, int y, int &map_x, int &map_y);
	void SetShowTerrain(bool show_terrain_) { show_terrain = show_terrain_; }
	void ToggleShowTerrain() { show_terrain = !show_terrain; }
private:
	ZMap *zmap;
	vector<ZObject*> *object_list;

	SDL_Rect render_area;
	int max_width, max_height;
	double render_ratio;

	double show_terrain;
};

#endif
