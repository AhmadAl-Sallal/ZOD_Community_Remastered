#include "zstart_editor.h"
#include "common.h"
#include "constants.h"

#include <dirent.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cctype>

using namespace std;
using namespace COMMON;

static const char* MAP_FOLDER = "maps/other/";

static string pending_editor_command;

bool ZMapEditorLaunchPending()
{
    return !pending_editor_command.empty();
}

const char* ZMapEditorCommand()
{
    return pending_editor_command.c_str();
}


static void Box(SDL_Surface* s, int x, int y,
                int w, int h, int r, int g, int b)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;

    SDL_FillRect(s, &rect,
        SDL_MapRGB(s->format, r, g, b));
}

static void Text(SDL_Surface* s, TTF_Font* font,
                 const string& text, int x, int y)
{
    if(!font) return;

    SDL_Color color = {220, 205, 175, 0};

    SDL_Surface* t =
        TTF_RenderUTF8_Blended(font, text.c_str(), color);

    if(!t) return;

    SDL_Rect pos;
    pos.x = x;
    pos.y = y;

    SDL_BlitSurface(t, NULL, s, &pos);
    SDL_FreeSurface(t);
}

static void Button(SDL_Surface* s, TTF_Font* font,
                   const string& text,
                   int x, int y, int w, int h,
                   bool active = false)
{
    Box(s, x - 2, y - 2, w + 4, h + 4,
        133, 119, 96);

    Box(s, x, y, w, h,
        active ? 112 : 75,
        active ? 99 : 68,
        active ? 78 : 55);

    Text(s, font, text, x + 15,
         y + (h - TTF_FontHeight(font)) / 2);
}

static bool Inside(int mx, int my,
                   int x, int y, int w, int h)
{
    return mx >= x && mx < x + w &&
           my >= y && my < y + h;
}

static void Background(SDL_Surface* s)
{
    SDL_FillRect(s, NULL,
        SDL_MapRGB(s->format, 49, 44, 39));

    Box(s, 30, 25, s->w - 60, s->h - 50,
        103, 94, 81);

    Box(s, 36, 31, s->w - 72, s->h - 62,
        58, 53, 47);
}

static bool ValidFilename(const string& name)
{
    if(name.empty() || name[0] == '.')
        return false;

    for(size_t i = 0; i < name.size(); i++)
    {
        unsigned char c = (unsigned char)name[i];

        if(!isalnum(c) &&
           c != '_' && c != '-' &&
           c != '.' && c != ' ')
            return false;
    }

    return true;
}

static bool Exists(const string& filename)
{
    FILE* fp = fopen(filename.c_str(), "rb");

    if(!fp) return false;

    fclose(fp);
    return true;
}

static vector<string> FindMaps()
{
    vector<string> maps;

    DIR* dir = opendir(MAP_FOLDER);
    if(!dir) return maps;

    struct dirent* entry;

    while((entry = readdir(dir)) != NULL)
    {
        string name = entry->d_name;

        if(!ValidFilename(name))
            continue;

        if(name.length() < 5)
            continue;

        string ext = name.substr(name.length() - 4);

        for(size_t i = 0; i < ext.length(); i++)
            ext[i] = (char)tolower((unsigned char)ext[i]);

        if(ext != ".map")
            continue;

        if(!Exists(string(MAP_FOLDER) + name))
            continue;

        maps.push_back(name);
    }

    closedir(dir);

    sort(maps.begin(), maps.end());

    return maps;
}

static bool LaunchEditor(const string& filename,
                         const string& arguments,
                         string& status)
{
    const char* locations[] =
    {
        "src\\zod_map_editor.exe",
        "zod_map_editor.exe"
    };

    string executable;

    for(int i = 0; i < 2; i++)
    {
        if(Exists(locations[i]))
        {
            executable = locations[i];
            break;
        }
    }

    if(executable.empty())
    {
        status = "Map Editor executable not found";
        return false;
    }

    string command =
        executable + " -f \"" + filename + "\"" + arguments;

    pending_editor_command = command;
    status = "Opening Map Editor";
    return true;
}

static bool ValidNewName(const string& name)
{
    if(name.empty() || name.length() > 32)
        return false;

    for(size_t i = 0; i < name.length(); i++)
    {
        unsigned char c = (unsigned char)name[i];

        if(!isalnum(c) && c != '_' && c != '-')
            return false;
    }

    return true;
}

// Return: -1 = quit ZOD, 0 = return to map list
static int NewMapMenu(SDL_Surface* screen,
                      TTF_Font* font,
                      TTF_Font* small_font)
{
    string name;
    string status = "Type a map name";

    const int sizes[] = {64, 96, 128, 160};
    int size_index = 0;
    int terrain = DESERT;

    SDL_EnableUNICODE(SDL_ENABLE);

    while(true)
    {
        SDL_Event event;

        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
                return -1;

            if(event.type == SDL_KEYDOWN)
            {
                SDLKey key = event.key.keysym.sym;

                if(key == SDLK_ESCAPE)
                    return 0;

                if(key == SDLK_BACKSPACE)
                {
                    if(!name.empty())
                        name.erase(name.length() - 1);
                }
                else
                {
                    unsigned int c = event.key.keysym.unicode;

                    if(c < 128 &&
                       name.length() < 32 &&
                       (isalnum((unsigned char)c) ||
                        c == '_' || c == '-'))
                    {
                        name += (char)c;
                    }
                }
            }

            if(event.type == SDL_MOUSEBUTTONUP &&
               event.button.button == SDL_BUTTON_LEFT)
            {
                int x = event.button.x;
                int y = event.button.y;

                if(Inside(x,y,250,275,780,50))
                {
                    size_index = (size_index + 1) % 4;
                }
                else if(Inside(x,y,250,365,780,50))
                {
                    terrain = (terrain + 1) % MAX_PLANET_TYPES;
                }
                else if(Inside(x,y,250,540,360,55))
                {
                    if(!ValidNewName(name))
                    {
                        status = "Use letters, numbers, _ or -";
                    }
                    else
                    {
                        string filename =
                            string(MAP_FOLDER) + name + ".map";

                        if(Exists(filename))
                        {
                            status = "A map with this name already exists";
                        }
                        else
                        {
                            char dimensions[64];
                            sprintf(dimensions, "%dx%d",
                                sizes[size_index], sizes[size_index]);

                            string args = " -n -d ";
                            args += dimensions;
                            args += " -p \"";
                            args += planet_type_string[terrain];
                            args += "\" -m \"";
                            args += name;
                            args += "\"";

                            if(LaunchEditor(filename, args, status)) return 1;
                        }
                    }
                }
                else if(Inside(x,y,670,540,360,55))
                {
                    return 0;
                }
            }
        }

        Background(screen);

        Text(screen,font,"CREATE NEW MAP",400,80);

        Text(screen,small_font,
             "Map name (type on keyboard):",250,165);

        Box(screen,250,195,780,48,35,32,28);
        Text(screen,font,name + "_",265,200);

        Button(screen,font,
               string("Map Size: ") +
               (sizes[size_index] == 64 ? "64x64" :
                sizes[size_index] == 96 ? "96x96" :
                sizes[size_index] == 128 ? "128x128" : "160x160"),
               250,275,780,50);

        Button(screen,font,
               string("Terrain: ") + planet_type_string[terrain],
               250,365,780,50);

        Button(screen,font,"Create Map",250,540,360,55);
        Button(screen,font,"Back",670,540,360,55);

        Text(screen,small_font,status,250,635);
        Text(screen,small_font,
             "Click size or terrain to change selection",250,665);

        SDL_Flip(screen);
        SDL_Delay(16);
    }
}

bool RunZMapEditorMenu(SDL_Surface* screen,
                       TTF_Font* font,
                       TTF_Font* small_font)
{
    pending_editor_command.clear();
    vector<string> maps = FindMaps();

    int selected = maps.empty() ? -1 : 0;
    int scroll = 0;

    const int visible = 8;
    const int row_y = 148;
    const int row_h = 40;

    string status = "Select a map to edit";

    while(true)
    {
        SDL_Event event;

        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
                return false;

            if(event.type == SDL_KEYDOWN)
            {
                SDLKey key = event.key.keysym.sym;

                if(key == SDLK_ESCAPE)
                    return true;

                if(key == SDLK_DOWN &&
                   selected + 1 < (int)maps.size())
                    selected++;

                if(key == SDLK_UP && selected > 0)
                    selected--;

                if(selected >= scroll + visible)
                    scroll = selected - visible + 1;

                if(selected >= 0 && selected < scroll)
                    scroll = selected;
            }

            if(event.type == SDL_MOUSEBUTTONUP &&
               event.button.button == SDL_BUTTON_LEFT)
            {
                int x = event.button.x;
                int y = event.button.y;

                if(Inside(x,y,235,row_y,810,visible * row_h))
                {
                    int index = scroll + (y - row_y) / row_h;

                    if(index >= 0 && index < (int)maps.size())
                        selected = index;
                }

                else if(Inside(x,y,350,510,580,48))
                {
                    if(selected < 0 ||
                       selected >= (int)maps.size())
                    {
                        status = "Please select a map";
                    }
                    else
                    {
                        string filename =
                            string(MAP_FOLDER) + maps[selected];

                        if(LaunchEditor(filename,"",status)) return true;
                    }
                }

                else if(Inside(x,y,350,565,580,48))
                {
                    int new_map_result = NewMapMenu(screen,font,small_font);

                    if(new_map_result == -1)
                        return false;

                    if(new_map_result == 1)
                        return true;

                    maps = FindMaps();

                    selected = maps.empty() ? -1 : 0;
                    scroll = 0;

                    status = "Select a map to edit";
                }

                else if(Inside(x,y,350,620,580,48))
                {
                    return true;
                }
            }

            if(event.type == SDL_MOUSEBUTTONDOWN)
            {
                if(event.button.button == SDL_BUTTON_WHEELDOWN)
                {
                    if(scroll + visible < (int)maps.size())
                        scroll++;
                }
                else if(event.button.button == SDL_BUTTON_WHEELUP)
                {
                    if(scroll > 0)
                        scroll--;
                }
            }
        }

        Background(screen);

        Text(screen,font,"ZOD MAP EDITOR",395,55);
        Text(screen,small_font,"Select Map to Edit",235,112);

        Box(screen,230,143,820,328,35,32,28);

        if(maps.empty())
            Text(screen,small_font,
                 "No maps found in maps/other/",255,175);

        for(int i = 0; i < visible; i++)
        {
            int index = scroll + i;

            if(index >= (int)maps.size())
                break;

            Button(screen,small_font,maps[index],
                   235,row_y + i * row_h,810,35,
                   index == selected);
        }

        Button(screen,font,"Open Map",350,510,580,48);
        Button(screen,font,"New Map",350,565,580,48);
        Button(screen,font,"Back",350,620,580,48);

        Text(screen,small_font,status,235,685);

        SDL_Flip(screen);
        SDL_Delay(16);
    }
}