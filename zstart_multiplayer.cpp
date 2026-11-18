#include "zstart_multiplayer.h"

#include <algorithm>
#include <string>
#include <vector>
#include <stdio.h>
#include <ctype.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

static std::string selected_mp_map;
static std::string join_mp_address = "127.0.0.1";

const std::string& ZMultiplayerSelectedMap() { return selected_mp_map; }
const std::string& ZMultiplayerJoinAddress() { return join_mp_address; }

static std::vector<std::string> FindMultiplayerMaps()
{
    std::vector<std::string> result;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE handle = FindFirstFileA("maps/multiplayer/*.map", &fd);
    if(handle != INVALID_HANDLE_VALUE)
    {
        do {
            if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                result.push_back(std::string("maps/multiplayer/") + fd.cFileName);
        } while(FindNextFileA(handle, &fd));
        FindClose(handle);
    }
#else
    DIR* dir = opendir("maps/multiplayer");
    if(dir)
    {
        struct dirent* entry;
        while((entry = readdir(dir)) != NULL)
        {
            std::string file(entry->d_name);
            if(file.size() > 4 && file.substr(file.size()-4) == ".map")
                result.push_back(std::string("maps/multiplayer/") + file);
        }
        closedir(dir);
    }
#endif
    std::sort(result.begin(), result.end());
    return result;
}

// Reuse exactly the existing multiplayer map folder and map selection.
std::vector<std::string> ZMultiplayerPlayableMaps()
{
    return FindMultiplayerMaps();
}

bool ZMultiplayerSelectMap(const std::string& path)
{
    const std::vector<std::string> maps = FindMultiplayerMaps();
    if(std::find(maps.begin(), maps.end(), path) == maps.end()) return false;
    selected_mp_map = path;
    return true;
}

static void Box(SDL_Surface* screen, int x, int y, int w, int h,
                Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Rect rect;
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_FillRect(screen, &rect, SDL_MapRGB(screen->format, r, g, b));
}

static void Text(SDL_Surface* screen, TTF_Font* font, const std::string& str,
                 int x, int y, Uint8 r=222, Uint8 g=203, Uint8 b=164)
{
    if(!font || str.empty()) return;
    SDL_Color color = {r,g,b,0};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, str.c_str(), color);
    if(!surface) return;
    SDL_Rect position;
    position.x = x; position.y = y;
    SDL_BlitSurface(surface, NULL, screen, &position);
    SDL_FreeSurface(surface);
}

static std::string FileLabel(const std::string& path)
{
    std::string::size_type slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if(name.size() > 47) name = name.substr(0,44) + "...";
    return name;
}

static bool Hit(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void KeepVisible(int& top, int selected, int visible)
{
    if(selected < top) top = selected;
    if(selected >= top + visible) top = selected - visible + 1;
    if(top < 0) top = 0;
}

int RunZMultiplayerMenu(SDL_Surface* screen, TTF_Font* button_font, TTF_Font* small_font)
{
    if(!screen || !button_font || !small_font) return ZMP_BACK;

    // STATE_MENU, STATE_MAPS and STATE_JOIN are local to this submenu.
    enum ScreenState { STATE_MENU, STATE_MAPS, STATE_JOIN };
    ScreenState state = STATE_MENU;
    std::vector<std::string> maps = FindMultiplayerMaps();
    if(!maps.empty() && selected_mp_map.empty()) selected_mp_map = maps[0];

    int selected_menu = 0;
    int selected_map = 0;
    int first_map = 0;
    const int visible_rows = 9;
    const int map_start_y = 166;
    const int map_row_h = 46;
    const int button_x = screen->w / 6;
    const int button_w = screen->w / 2;
    const int button_y = 188;
    const int button_h = 58;
    const int button_gap = 77;
    std::string address = join_mp_address;
    std::string status = "Choose Host, Join or Select Map";
    Uint32 last_wheel = 0;
    int old_delay = 0, old_interval = 0;
    SDL_GetKeyRepeat(&old_delay, &old_interval);
    SDL_EnableKeyRepeat(220, 70);
    int old_unicode = SDL_EnableUNICODE(1);
    int result = ZMP_BACK;
    bool running = true;

    for(size_t i=0; i<maps.size(); ++i)
        if(maps[i] == selected_mp_map) { selected_map = (int)i; break; }
    KeepVisible(first_map, selected_map, visible_rows);

    while(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
            {
                result = ZMP_QUIT;
                running = false;
                break;
            }
            if(state == STATE_MENU)
            {
                if(event.type == SDL_MOUSEMOTION)
                {
                    for(int i=0;i<4;++i)
                        if(Hit(event.motion.x,event.motion.y,button_x,
                               button_y+i*button_gap,button_w,button_h)) selected_menu=i;
                }
                else if(event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
                {
                    for(int i=0;i<4;++i)
                    {
                        if(Hit(event.button.x,event.button.y,button_x,
                               button_y+i*button_gap,button_w,button_h))
                        {
                            selected_menu=i;
                            if(i==0)
                            {
                                if(selected_mp_map.empty()) status="Select a multiplayer map first";
                                else {result=ZMP_HOST;running=false;}
                            }
                            else if(i==1) {result=ZMP_JOIN;running=false;}
                            else if(i==2)
                            {
                                maps=FindMultiplayerMaps();
                                if(maps.empty()) status="No .map files in maps/multiplayer";
                                else {state=STATE_MAPS;first_map=0;selected_map=0;}
                            }
                            else {result=ZMP_BACK;running=false;}
                            break;
                        }
                    }
                }
                else if(event.type==SDL_KEYDOWN)
                {
                    SDLKey key=event.key.keysym.sym;
                    if(key==SDLK_ESCAPE) {result=ZMP_BACK;running=false;}
                    else if(key==SDLK_UP) selected_menu=(selected_menu+3)%4;
                    else if(key==SDLK_DOWN) selected_menu=(selected_menu+1)%4;
                    else if(key==SDLK_RETURN || key==SDLK_KP_ENTER)
                    {
                        if(selected_menu==0)
                        {
                            if(selected_mp_map.empty()) status="Select a multiplayer map first";
                            else {result=ZMP_HOST;running=false;}
                        }
                        else if(selected_menu==1) {result=ZMP_JOIN;running=false;}
                        else if(selected_menu==2)
                        {
                            maps=FindMultiplayerMaps();
                            if(maps.empty()) status="No .map files in maps/multiplayer";
                            else {state=STATE_MAPS;first_map=0;selected_map=0;}
                        }
                        else {result=ZMP_BACK;running=false;}
                    }
                }
            }
            else if(state == STATE_MAPS)
            {
                if(event.type==SDL_MOUSEBUTTONDOWN &&
                   (event.button.button==SDL_BUTTON_WHEELUP || event.button.button==SDL_BUTTON_WHEELDOWN))
                {
                    Uint32 now=SDL_GetTicks();
                    if(now-last_wheel >= 20)
                    {
                        int direction=(event.button.button==SDL_BUTTON_WHEELDOWN)?1:-1;
                        selected_map += direction;
                        if(selected_map<0) selected_map=0;
                        if(selected_map>=(int)maps.size()) selected_map=(int)maps.size()-1;
                        KeepVisible(first_map,selected_map,visible_rows);
                        last_wheel=now;
                    }
                }
                else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)
                {
                    int x=event.button.x;
                    int y=event.button.y;
                    for(int row=0;row<visible_rows && row+first_map<(int)maps.size();++row)
                    {
                        int ry=map_start_y+row*map_row_h;
                        if(Hit(x,y,button_x,ry,button_w,map_row_h-5))
                        {
                            selected_map=first_map+row;
                            if(event.type==SDL_MOUSEBUTTONUP) {
                                selected_mp_map=maps[selected_map]; state=STATE_MENU;
                                status="Map selected";
                            }
                            break;
                        }
                    }
                    if(event.type==SDL_MOUSEBUTTONUP && Hit(x,y,button_x,screen->h-91,button_w,42))
                    { state=STATE_MENU; }
                }
                else if(event.type==SDL_KEYDOWN)
                {
                    SDLKey key=event.key.keysym.sym;
                    if(key==SDLK_ESCAPE) state=STATE_MENU;
                    else if(key==SDLK_UP && selected_map>0) --selected_map;
                    else if(key==SDLK_DOWN && selected_map+1<(int)maps.size()) ++selected_map;
                    else if(key==SDLK_PAGEUP) selected_map=(selected_map>visible_rows)?(selected_map-visible_rows):0;
                    else if(key==SDLK_PAGEDOWN) selected_map=(selected_map+visible_rows<(int)maps.size())?(selected_map+visible_rows):((int)maps.size()-1);
                    else if(key==SDLK_RETURN || key==SDLK_KP_ENTER)
                    {
                        selected_mp_map=maps[selected_map]; state=STATE_MENU;
                        status="Map selected";
                    }
                    KeepVisible(first_map,selected_map,visible_rows);
                }
            }
            else // STATE_JOIN
            {
                if(event.type==SDL_KEYDOWN)
                {
                    SDLKey key=event.key.keysym.sym;
                    if(key==SDLK_ESCAPE) state=STATE_MENU;
                    else if(key==SDLK_BACKSPACE && !address.empty()) address.erase(address.size()-1);
                    else if(key==SDLK_RETURN || key==SDLK_KP_ENTER)
                    {
                        if(!address.empty())
                        {
                            join_mp_address=address;
                            result=ZMP_JOIN;
                            running=false;
                        }
                    }
                    else if(event.key.keysym.unicode>0 && event.key.keysym.unicode<128 && address.size()<127)
                    {
                        char c=(char)event.key.keysym.unicode;
                        if(isalnum((unsigned char)c) || c=='.' || c=='-' || c=='_') address.push_back(c);
                    }
                }
                else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)
                {
                    if(Hit(event.button.x,event.button.y,button_x,screen->h-122,button_w/2-8,48))
                    {
                        if(!address.empty()) {join_mp_address=address;result=ZMP_JOIN;running=false;}
                    }
                    else if(Hit(event.button.x,event.button.y,button_x+button_w/2+8,
                                screen->h-122,button_w/2-8,48)) state=STATE_MENU;
                }
            }
        }
        if(!running) break;

        SDL_FillRect(screen,NULL,SDL_MapRGB(screen->format,49,44,39));
        Box(screen,30,25,screen->w-60,screen->h-50,103,94,81);
        Box(screen,36,31,screen->w-72,screen->h-62,58,53,47);
        Text(screen,button_font,"MULTIPLAYER",screen->w/6,75,235,209,155);

        if(state==STATE_MENU)
        {
            static const char* options[4]={"Host Game","Join Game","Select Map","Back"};
            for(int i=0;i<4;++i)
            {
                int ry=button_y+i*button_gap;
                Box(screen,button_x-3,ry-3,button_w+6,button_h+6,125,113,94);
                Box(screen,button_x,ry,button_w,button_h,
                    (selected_menu==i)?108:75,(selected_menu==i)?97:69,(selected_menu==i)?77:58);
                Text(screen,button_font,options[i],button_x+17,ry+10,
                     (selected_menu==i)?255:210, (selected_menu==i)?231:193, (selected_menu==i)?178:150);
            }
            Text(screen,small_font,std::string("Map: ")+(selected_mp_map.empty()?"none":FileLabel(selected_mp_map)),button_x,screen->h-155);
            Text(screen,small_font,status,button_x,screen->h-117);
            Text(screen,small_font,"Host defaults to RED; joining defaults to BLUE. Teams/bots can be changed in-game.",button_x,screen->h-77,190,180,160);
        }
        else if(state==STATE_MAPS)
        {
            Text(screen,small_font,"Select a map (click or Enter). Mouse wheel and arrow keys to scroll.",button_x,122);
            for(int row=0;row<visible_rows && row+first_map<(int)maps.size();++row)
            {
                int idx=first_map+row, ry=map_start_y+row*map_row_h;
                bool active=(idx==selected_map);
                Box(screen,button_x,ry,button_w,map_row_h-5,
                    active?108:75,active?97:69,active?77:58);
                Text(screen,small_font,FileLabel(maps[idx]),button_x+14,ry+9,
                     active?255:210,active?231:193,active?178:150);
            }
            Box(screen,button_x,screen->h-91,button_w,42,75,69,58);
            Text(screen,small_font,"Back (Esc)",button_x+15,screen->h-81);
            char count[96];
            snprintf(count,sizeof(count),"%d multiplayer maps",(int)maps.size());
            Text(screen,small_font,count,button_x+button_w-220,122);
        }
        else
        {
            Text(screen,small_font,"Enter host IP address or hostname (e.g. 192.168.1.25 or localhost):",button_x,181);
            Box(screen,button_x,241,button_w,66,35,32,30);
            Text(screen,button_font,address+"_",button_x+12,252,255,231,178);
            Box(screen,button_x,screen->h-122,button_w/2-8,48,92,85,70);
            Text(screen,small_font,"Connect (Enter)",button_x+13,screen->h-110);
            Box(screen,button_x+button_w/2+8,screen->h-122,button_w/2-8,48,75,69,58);
            Text(screen,small_font,"Back (Esc)",button_x+button_w/2+21,screen->h-110);
            Text(screen,small_font,"The host must already be running. No bots are forced for multiplayer.",button_x,screen->h-63,190,180,160);
        }
        SDL_Flip(screen);
        SDL_Delay(16);
    }

    SDL_EnableUNICODE(old_unicode);
    SDL_EnableKeyRepeat(old_delay,old_interval);
    return result;
}