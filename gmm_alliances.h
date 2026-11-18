#ifndef _ZGMM_ALLIANCES_H_
#define _ZGMM_ALLIANCES_H_
#include "zgui_main_menu_base.h"
class GMMAlliances : public ZGuiMainMenuBase
{
public:
    GMMAlliances();
    virtual bool RenderAtNativePixels() const { return true; }
    virtual void DoRender(ZMap &m, SDL_Surface *s);
    virtual void Process();
private:
    GMMWButton ally_button[MAX_TEAM_TYPES];
    GMMWButton enemy_button[MAX_TEAM_TYPES];
    GMMWButton all_bots_on_button, all_bots_off_button;
    ZSDL_Surface art, select_art;
    void HandleWidgetEvent(int type, ZGMMWidget *widget);
};
#endif
