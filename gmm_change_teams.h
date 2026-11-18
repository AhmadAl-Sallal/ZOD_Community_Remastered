#ifndef _ZGMM_CHANGE_TEAMS_H_
#define _ZGMM_CHANGE_TEAMS_H_
#include "zgui_main_menu_base.h"
class GMMChangeTeams : public ZGuiMainMenuBase
{
public:
    GMMChangeTeams();
    virtual bool RenderAtNativePixels() const { return true; }
    virtual void DoRender(ZMap &m, SDL_Surface *s);
    virtual void Process();
private:
    GMMWButton team_button[MAX_TEAM_TYPES];
    ZSDL_Surface art, selected_art;
    void HandleWidgetEvent(int type, ZGMMWidget *widget);
};
#endif
