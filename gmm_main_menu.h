#ifndef _ZGMM_MAIN_MENU_H_
#define _ZGMM_MAIN_MENU_H_

#include "zgui_main_menu_base.h"

enum gmm_main_menu_button
{
    GMMMM_PAUSE_GAME_BUTTON,
    GMMMM_CHANGE_TEAMS_BUTTON,
    GMMMM_MANAGE_BOTS_BUTTON,
    GMMMM_ALLIANCES_BUTTON,
    GMMMM_PLAYER_LIST_BUTTON,
    GMMMM_RESET_MAP_BUTTON,
    GMMMM_QUIT_GAME_BUTTON,
    MAX_GMMMM_BUTTONS
};

class GMMMainMenu : public ZGuiMainMenuBase
{
public:
    GMMMainMenu(int play_mode, bool is_host);
    virtual bool RenderAtNativePixels() const { return true; }
    virtual void DoRender(ZMap &the_map, SDL_Surface *dest);
    virtual bool Motion(int mouse_x, int mouse_y);
private:
    GMMWButton menu_button[MAX_GMMMM_BUTTONS];
    ZSDL_Surface facit_art;
    ZSDL_Surface hover_art[MAX_GMMMM_BUTTONS];
    int visible_button_ids[MAX_GMMMM_BUTTONS];
    int visible_count;
    int hovered_row;
    void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);
};

#endif
