#include "gmm_manage_bots.h"

namespace {
const int TEAM_COLUMN_OFFSET = 329;
int TeamRow(int team) { return (team-RED_TEAM)%4; }
int TeamColumn(int team) { return team>=PURPLE_TEAM ? 1 : 0; }
}

GMMManageBots::GMMManageBots() : ZGuiMainMenuBase()
{
    menu_type = GMM_MANAGE_BOTS;
    title="Manage Bots";
    w=684; h=366;
    art.LoadBaseImage("assets/other/zod_facit_ui/submenu_manage_bots_8teams.png");
    select_art.LoadBaseImage("assets/other/zod_facit_ui/submenu_toggle_highlight.png");
    for(int team=RED_TEAM; team<MAX_TEAM_TYPES; ++team)
    {
        int py=145+TeamRow(team)*43;
        int px=TeamColumn(team)*TEAM_COLUMN_OFFSET;
        start_button[team].SetType(MMGENERIC_BUTTON);
        start_button[team].SetText("On");
        start_button[team].SetCoords(209+px,py+3);
        start_button[team].SetDimensions(48,25);
        AddWidget(&start_button[team]);
        stop_button[team].SetType(MMGENERIC_BUTTON);
        stop_button[team].SetText("Off");
        stop_button[team].SetCoords(265+px,py+3);
        stop_button[team].SetDimensions(48,25);
        AddWidget(&stop_button[team]);
    }
    UpdateDimensions();
    close_button.SetCoords(20,75); close_button.SetDimensions(47,38);
}
void GMMManageBots::Process() { ProcessWidgets(); }
void GMMManageBots::DoRender(ZMap &m, SDL_Surface *s)
{
    if(art.GetBaseSurface()) art.BlitSurface(NULL,x,y);
    if(!select_art.GetBaseSurface() || !player_info) return;
    for(int team=RED_TEAM; team<MAX_TEAM_TYPES; ++team)
    {
        bool hasbot=false;
        for(std::vector<p_info>::const_iterator i=player_info->begin(); i!=player_info->end(); ++i)
            if(i->team==team && i->mode==BOT_MODE && !i->ignored) { hasbot=true;break; }
        select_art.BlitSurface(NULL,x+(hasbot?209:265)+TeamColumn(team)*TEAM_COLUMN_OFFSET,
                               y+145+TeamRow(team)*43+3);
    }
}
void GMMManageBots::HandleWidgetEvent(int type, ZGMMWidget *widget)
{
    if(type!=GMM_UNCLICK_EVENT || !widget) return;
    for(int team=RED_TEAM; team<MAX_TEAM_TYPES; ++team)
    {
        if(widget->GetRefID()==start_button[team].GetRefID())
        { gmm_flags.start_bot=true;gmm_flags.start_bot_team=team;return; }
        if(widget->GetRefID()==stop_button[team].GetRefID())
        { gmm_flags.stop_bot=true;gmm_flags.stop_bot_team=team;return; }
    }
}
