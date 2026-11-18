#ifndef _ZSTART_MULTIPLAYER_H_
#define _ZSTART_MULTIPLAYER_H_

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <string>
#include <vector>

// Returned by the multiplayer submenu.
enum ZMultiplayerAction {
    ZMP_QUIT = -1,
    ZMP_BACK = 0,
    ZMP_HOST = 1,
    ZMP_JOIN = 2
};

int RunZMultiplayerMenu(SDL_Surface* screen, TTF_Font* button_font, TTF_Font* small_font);
const std::string& ZMultiplayerSelectedMap();
const std::string& ZMultiplayerJoinAddress();

// Inline main-menu multiplayer map picker.
std::vector<std::string> ZMultiplayerPlayableMaps();
bool ZMultiplayerSelectMap(const std::string& map_path);

#endif