#ifndef _ZGMM_MANAGE_BOTS_H_
#define _ZGMM_MANAGE_BOTS_H_
#include "zgui_main_menu_base.h"
class GMMManageBots : public ZGuiMainMenuBase
{
public:
    GMMManageBots();
    virtual bool RenderAtNativePixels() const { return true; }
    virtual void DoRender(ZMap &m, SDL_Surface *s);
    virtual void Process();
private:
    GMMWButton start_button[MAX_TEAM_TYPES];
    GMMWButton stop_button[MAX_TEAM_TYPES];
    ZSDL_Surface art, select_art;
    void HandleWidgetEvent(int type, ZGMMWidget *widget);
};
#endif
