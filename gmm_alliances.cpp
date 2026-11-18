#include "gmm_alliances.h"

namespace {
const int TEAM_COLUMN_OFFSET = 329;
int TeamRow(int team) { return (team-RED_TEAM)%4; }
int TeamColumn(int team) { return team>=PURPLE_TEAM ? 1 : 0; }
}

GMMAlliances::GMMAlliances() : ZGuiMainMenuBase()
{
    menu_type=GMM_ALLIANCES;
    title="Alliances";
    w=684;h=366;
    art.LoadBaseImage("assets/other/zod_facit_ui/submenu_alliances_8teams.png");
    select_art.LoadBaseImage("assets/other/zod_facit_ui/submenu_toggle_highlight.png");
    for(int team=RED_TEAM; team<MAX_TEAM_TYPES; ++team)
    {
        int py=133+TeamRow(team)*38;
        int px=TeamColumn(team)*TEAM_COLUMN_OFFSET;
        ally_button[team].SetType(MMGENERIC_BUTTON); ally_button[team].SetText("On");
        ally_button[team].SetCoords(209+px,py+3); ally_button[team].SetDimensions(48,25);
        AddWidget(&ally_button[team]);
        enemy_button[team].SetType(MMGENERIC_BUTTON); enemy_button[team].SetText("Off");
        enemy_button[team].SetCoords(265+px,py+3); enemy_button[team].SetDimensions(48,25);
        AddWidget(&enemy_button[team]);
    }
    all_bots_on_button.SetType(MMGENERIC_BUTTON); all_bots_on_button.SetText("On");
    all_bots_on_button.SetCoords(209,133+4*38+3);
    all_bots_on_button.SetDimensions(48,25); AddWidget(&all_bots_on_button);
    all_bots_off_button.SetType(MMGENERIC_BUTTON); all_bots_off_button.SetText("Off");
    all_bots_off_button.SetCoords(265,133+4*38+3);
    all_bots_off_button.SetDimensions(48,25); AddWidget(&all_bots_off_button);
    UpdateDimensions();close_button.SetCoords(20,75); close_button.SetDimensions(47,38);
}
void GMMAlliances::Process(){ ProcessWidgets(); }
void GMMAlliances::DoRender(ZMap &m, SDL_Surface *s)
{
    if(art.GetBaseSurface()) art.BlitSurface(NULL,x,y);
    if(!select_art.GetBaseSurface()) return;
    if(player_team && alliance_requests && *player_team>NULL_TEAM && *player_team<MAX_TEAM_TYPES)
    {
        for(int team=RED_TEAM;team<MAX_TEAM_TYPES;++team)
        {
            bool ally=alliance_requests[*player_team][team];
            select_art.BlitSurface(NULL,x+(ally?209:265)+TeamColumn(team)*TEAM_COLUMN_OFFSET,
                                   y+133+TeamRow(team)*38+3);
        }
    }
    if(all_bots_allied)
        select_art.BlitSurface(NULL,x+(*all_bots_allied?209:265),y+133+4*38+3);
}
void GMMAlliances::HandleWidgetEvent(int type, ZGMMWidget *widget)
{
    if(type!=GMM_UNCLICK_EVENT || !widget) return;
    for(int team=RED_TEAM;team<MAX_TEAM_TYPES;++team)
    {
        if(widget->GetRefID()==ally_button[team].GetRefID())
        {gmm_flags.set_alliance=true;gmm_flags.set_alliance_team=team;gmm_flags.set_alliance_value=true;return;}
        if(widget->GetRefID()==enemy_button[team].GetRefID())
        {gmm_flags.set_alliance=true;gmm_flags.set_alliance_team=team;gmm_flags.set_alliance_value=false;return;}
    }
    if(widget->GetRefID()==all_bots_on_button.GetRefID())
    {gmm_flags.set_all_bots_allied=true;gmm_flags.set_all_bots_allied_value=true;}
    else if(widget->GetRefID()==all_bots_off_button.GetRefID())
    {gmm_flags.set_all_bots_allied=true;gmm_flags.set_all_bots_allied_value=false;}
}
