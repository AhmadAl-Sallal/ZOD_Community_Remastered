#include "gmm_change_teams.h"

namespace {
// The approved 8-team panel has 1-4 on the left and 5-8 on the right.
// Neutral remains a separate fifth row in the left column.
const int TEAM_COLUMN_OFFSET = 329;
int TeamRow(int team) { return team == NULL_TEAM ? 4 : (team - RED_TEAM) % 4; }
int TeamColumn(int team) { return team >= PURPLE_TEAM ? 1 : 0; }
}

GMMChangeTeams::GMMChangeTeams() : ZGuiMainMenuBase()
{
    menu_type = GMM_CHANGE_TEAMS;
    title = "Change Teams";
    w=684; h=366;
    art.LoadBaseImage("assets/other/zod_facit_ui/submenu_change_teams_8teams.png");
    selected_art.LoadBaseImage("assets/other/zod_facit_ui/submenu_row_highlight.png");
    for(int team=NULL_TEAM; team<MAX_TEAM_TYPES; ++team)
    {
        team_button[team].SetType(MMGENERIC_BUTTON);
        team_button[team].SetText("Join");
        team_button[team].SetCoords(250+TeamColumn(team)*TEAM_COLUMN_OFFSET,136+TeamRow(team)*38);
        team_button[team].SetDimensions(67,25);
        AddWidget(&team_button[team]);
    }
    UpdateDimensions();
    close_button.SetCoords(20,75); close_button.SetDimensions(47,38);
}
void GMMChangeTeams::Process() { ProcessWidgets(); }
void GMMChangeTeams::DoRender(ZMap &m, SDL_Surface *s)
{
    if(art.GetBaseSurface()) art.BlitSurface(NULL,x,y);
    if(player_team && *player_team>=NULL_TEAM && *player_team<MAX_TEAM_TYPES && selected_art.GetBaseSurface())
    {
        const int team=*player_team;
        selected_art.BlitSurface(NULL,x+27+TeamColumn(team)*TEAM_COLUMN_OFFSET,y+133+TeamRow(team)*38);
    }
}
void GMMChangeTeams::HandleWidgetEvent(int type, ZGMMWidget *widget)
{
    if(type != GMM_UNCLICK_EVENT || !widget) return;
    for(int team=NULL_TEAM; team<MAX_TEAM_TYPES; ++team)
        if(widget->GetRefID() == team_button[team].GetRefID())
        {
            gmm_flags.change_team=true;
            gmm_flags.change_team_type=team;
            return;
        }
}
