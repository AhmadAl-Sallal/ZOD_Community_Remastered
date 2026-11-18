#ifndef ZSTART_SELECT_H
#define ZSTART_SELECT_H

#include "SDL.h"
#include "SDL_ttf.h"
#include <string>
#include <vector>

int RunZSelectMapMenu(
    SDL_Surface* screen,
    TTF_Font* font,
    TTF_Font* small_font
);

// For the shared main-menu panel. Returns paths in maps/other/.
std::vector<std::string> ZSelectMapPlayableMaps();
// Accept only an existing map from that directory, then use normal startup.
bool ZSelectMapQueueMap(const std::string& path);

bool ZHasSelectedMap();
const char* ZSelectedMapPath();

#endif