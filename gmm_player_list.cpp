#include "gmm_player_list.h"
#include <stdio.h>
GMMPlayerList::GMMPlayerList() : ZGuiMainMenuBase()
{
    menu_type=GMM_PLAYER_LIST;
    title="Player List";
    w=354;h=366;offset=0;sharp_font=NULL;
    art.LoadBaseImage("assets/other/zod_facit_ui/submenu_player_list.png");
    if(!TTF_WasInit()) TTF_Init();
    if(TTF_WasInit())
    {
        sharp_font=TTF_OpenFont("assets/Arimo-Regular.ttf",14);
    }
    UpdateDimensions();close_button.SetCoords(20,75); close_button.SetDimensions(47,38);
}
GMMPlayerList::~GMMPlayerList(){if(sharp_font) TTF_CloseFont(sharp_font);}
void GMMPlayerList::RefreshText(ZSDL_Surface &surface,const string &value,string &cache)
{
    if(value==cache) return;
    cache=value;
    if(!sharp_font || value.empty()){ surface.Unload();return; }
    SDL_Color white={236,237,233,0};
    SDL_Surface *s=TTF_RenderUTF8_Blended(sharp_font,value.c_str(),white);
    if(s) surface.LoadBaseImage(s);
}
void GMMPlayerList::Process()
{
    names.clear();
    if(player_info)
        for(vector<p_info>::const_iterator i=player_info->begin();i!=player_info->end();++i)
            if(i->mode==PLAYER_MODE && i->team>=0 && i->team<MAX_TEAM_TYPES)
                names.push_back(team_type_string[i->team]+": "+i->name);
    std::sort(names.begin(),names.end());
    if(offset>(int)names.size()-8)offset=(int)names.size()-8;
    if(offset<0)offset=0;
    char count[48];snprintf(count,sizeof(count),"%d",(int)names.size());
    RefreshText(count_text,count,cached_count);
    for(int n=0;n<8;n++)
    {
        const int idx=n+offset;
        RefreshText(row_text[n],idx<(int)names.size()?names[idx]:string(),cached_lines[n]);
    }
    ProcessWidgets();
}
void GMMPlayerList::DoRender(ZMap &m,SDL_Surface *s)
{
    if(art.GetBaseSurface())art.BlitSurface(NULL,x,y);
    if(count_text.GetBaseSurface())count_text.BlitSurface(NULL,x+209,y+143);
    for(int i=0;i<8;i++)
        if(row_text[i].GetBaseSurface())
        {
            SDL_Rect src={0,0,(Uint16)std::min((int)row_text[i].GetBaseSurface()->w,256),(Uint16)row_text[i].GetBaseSurface()->h};
            SDL_Rect dst={(Sint16)(x+46),(Sint16)(y+173+i*18),src.w,src.h};
            row_text[i].BlitSurface(&src,&dst,NULL);
        }
}
bool GMMPlayerList::WheelUpButton(){if(offset>0) --offset;return true;}
bool GMMPlayerList::WheelDownButton(){if(offset+8<(int)names.size())++offset;return true;}
void GMMPlayerList::HandleWidgetEvent(int type,ZGMMWidget *widget){}
