#ifndef _ZSTART_EDITOR_H_
#define _ZSTART_EDITOR_H_

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

bool RunZMapEditorMenu(
    SDL_Surface* screen,
    TTF_Font* font,
    TTF_Font* small_font
);

bool ZMapEditorLaunchPending();
const char* ZMapEditorCommand();

#endif