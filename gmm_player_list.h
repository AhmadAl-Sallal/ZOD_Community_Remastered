#ifndef _ZGMM_PLAYER_LIST_H_
#define _ZGMM_PLAYER_LIST_H_
#include "zgui_main_menu_base.h"
class GMMPlayerList : public ZGuiMainMenuBase
{
public:
    GMMPlayerList();
    virtual ~GMMPlayerList();
    virtual bool RenderAtNativePixels() const { return true; }
    virtual void DoRender(ZMap &m, SDL_Surface *s);
    virtual void Process();
    virtual bool WheelUpButton();
    virtual bool WheelDownButton();
private:
    ZSDL_Surface art, row_text[8], count_text;
    string cached_lines[8], cached_count;
    TTF_Font *sharp_font;
    vector<string> names;
    int offset;
    void RefreshText(ZSDL_Surface &surface,const string &value,string &cache);
    void HandleWidgetEvent(int type,ZGMMWidget *widget);
};
#endif
