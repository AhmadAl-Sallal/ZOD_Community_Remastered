#include "zstart_replay.h"
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <string>
#include <vector>
#include <fstream>
#include <stdio.h>

using namespace std;

namespace {
struct Rect { int x, y, w, h; };

static bool Hit(const Rect &r, int x, int y)
{
    return x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h;
}

static void Box(SDL_Surface *s, const Rect &r, int red, int green, int blue)
{
    SDL_Rect d = { Sint16(r.x), Sint16(r.y), Uint16(r.w), Uint16(r.h) };
    SDL_FillRect(s, &d, SDL_MapRGB(s->format, red, green, blue));
}

static void Label(SDL_Surface *s, TTF_Font *font, const string &value,
                  int x, int y, int r=232, int g=222, int b=196)
{
    SDL_Color c = { Uint8(r), Uint8(g), Uint8(b), 0 };
    SDL_Surface *text = TTF_RenderUTF8_Blended(font, value.c_str(), c);
    if(!text) return;
    SDL_Rect d = { Sint16(x), Sint16(y), 0, 0 };
    SDL_BlitSurface(text, NULL, s, &d);
    SDL_FreeSurface(text);
}

static void Button(SDL_Surface *s, TTF_Font *font, const Rect &r,
                   const string &caption)
{
    Box(s, r, 111, 101, 85);
    Box(s, Rect{r.x+4, r.y+4, r.w-8, r.h-8}, 58, 53, 47);
    Label(s, font, caption, r.x+16, r.y+13);
}

static string queued_replay_map;

static vector<string> CompletedCampaignMaps()
{
    vector<string> result;
    int highest_unlocked = 1;
    ifstream saved("saves/campaign.sav");
    if(!(saved >> highest_unlocked)) highest_unlocked = 1;
    if(highest_unlocked < 1) highest_unlocked = 1;
    int completed = highest_unlocked - 1;
    if(completed > 35) completed = 35;
    if(completed <= 0) return result;

    ifstream list("map_list.txt");
    string line;
    if(!getline(list, line)) return result;
    if(!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
    if(line != "0") return result;

    for(int i=0; i<completed && getline(list, line); ++i)
    {
        if(!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
        // Refuse paths outside the campaign directory.
        if(line.compare(0,14,"maps/campaign/") != 0) break;
        if(line.size() < 18 || line.substr(line.size()-4) != ".map") break;
        ifstream existing(line.c_str(), ios::binary);
        if(!existing) break;
        result.push_back(line);
    }
    return result;
}
}

vector<string> ZStartReplayCompletedMaps()
{
    return CompletedCampaignMaps();
}

bool ZStartReplayQueueMap(const string &map_path)
{
    // Refuse an arbitrary file path: only already-completed campaign maps.
    vector<string> maps = CompletedCampaignMaps();
    for(size_t i = 0; i < maps.size(); ++i)
    {
        if(maps[i] == map_path)
        {
            queued_replay_map = map_path;
            return true;
        }
    }
    return false;
}

bool RunZStartReplay(string &map_path)
{
    // The inline Replay Level panel already performed map selection.
    // Keep main.cpp unchanged by consuming that selection here.
    if(!queued_replay_map.empty())
    {
        map_path = queued_replay_map;
        queued_replay_map.clear();
        return true;
    }
    vector<string> maps = CompletedCampaignMaps();
    if(SDL_Init(SDL_INIT_VIDEO) < 0) return false;
    SDL_Surface *screen = SDL_SetVideoMode(1280, 720, 32, SDL_SWSURFACE);
    if(!screen) { SDL_Quit(); return false; }
    if(TTF_Init() < 0) { SDL_Quit(); return false; }
    TTF_Font *title = TTF_OpenFont("assets/Arimo-Regular.ttf", 38);
    TTF_Font *font = TTF_OpenFont("assets/Arimo-Regular.ttf", 23);
    if(!title || !font)
    {
        if(title) TTF_CloseFont(title);
        if(font) TTF_CloseFont(font);
        TTF_Quit(); SDL_Quit(); return false;
    }

    SDL_WM_SetCaption("ZOD - Replay Level", "ZOD");
    SDL_ShowCursor(SDL_ENABLE);
    SDL_EnableKeyRepeat(220,70);
    const Rect playBtn = {100, 583, 210, 55};
    const Rect backBtn = {335, 583, 210, 55};
    const int rows = 10;
    const int rowY = 203;
    const int rowH = 34;
    int selected = maps.empty() ? -1 : 0;
    int page = 0;
    Uint32 lastWheel = 0;
    bool running = true;
    bool played = false;

    while(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT) { running = false; break; }
            if(event.type == SDL_MOUSEBUTTONDOWN)
            {
                const int x = event.button.x, y = event.button.y;
                if(event.button.button == SDL_BUTTON_WHEELUP ||
                   event.button.button == SDL_BUTTON_WHEELDOWN)
                {
                    const Uint32 now = SDL_GetTicks();
                    // Replay-only wheel throttle = 20 ms; Select Map is unchanged.
                    if(now-lastWheel >= 20)
                    {
                        lastWheel = now;
                        if(event.button.button == SDL_BUTTON_WHEELUP && selected > 0)
                            --selected;
                        else if(event.button.button == SDL_BUTTON_WHEELDOWN &&
                                selected+1 < (int)maps.size())
                            ++selected;
                    }
                }
                else if(event.button.button == SDL_BUTTON_LEFT)
                {
                    if(x>=109 && x<789 && y>=rowY && y<rowY+rows*rowH)
                    {
                        const int index = page + (y-rowY)/rowH;
                        if(index < (int)maps.size()) selected = index;
                    }
                    else if(Hit(playBtn, x, y) && selected >= 0)
                    {
                        map_path = maps[selected];
                        played = true; running = false;
                    }
                    else if(Hit(backBtn, x, y)) running = false;
                }
            }
            if(event.type == SDL_KEYDOWN)
            {
                const SDLKey key = event.key.keysym.sym;
                if(key == SDLK_ESCAPE) running = false;
                else if(key == SDLK_UP && selected > 0) --selected;
                else if(key == SDLK_DOWN && selected+1 < (int)maps.size()) ++selected;
                else if((key == SDLK_RETURN || key == SDLK_KP_ENTER) && selected >= 0)
                {
                    map_path = maps[selected];
                    played = true; running = false;
                }
            }
            if(selected >= 0)
            {
                if(selected < page) page = selected;
                if(selected >= page + rows) page = selected - rows + 1;
            }
        }
        if(!running) break;

        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 48,44,39));
        Box(screen, Rect{45,35,1190,650}, 111,101,85);
        Box(screen, Rect{53,43,1174,634}, 61,55,48);
        Label(screen, title, "ZOD - REPLAY LEVEL", 100,86,233,212,170);
        Label(screen, font, "Select a completed campaign level", 100,160);
        Box(screen, Rect{100,199,700,354}, 100,92,80);
        Box(screen, Rect{104,202,692,347}, 37,34,30);
        if(maps.empty())
            Label(screen, font, "No completed levels yet - finish Level 01 first", 122,233);
        for(int i=0; i<rows && page+i<(int)maps.size(); ++i)
        {
            const int n = page+i;
            if(n == selected)
                Box(screen, Rect{109,rowY+i*rowH,680,rowH-1}, 94,80,61);
            char label[80];
            sprintf(label, "Level %02d", n+1);
            Label(screen, font, label, 122,rowY+3+i*rowH);
        }
        char count[100];
        sprintf(count, "Completed: %d / 35", (int)maps.size());
        Label(screen, font, count, 839,212);
        Label(screen, font, "RED player vs BLUE bot", 839,251);
        Button(screen, font, playBtn, "Play Level");
        Button(screen, font, backBtn, "Back");
        SDL_Flip(screen);
        SDL_Delay(16);
    }
    TTF_CloseFont(font);
    TTF_CloseFont(title);
    TTF_Quit(); SDL_Quit();
    return played;
}