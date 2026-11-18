#include "zstart_select.h"

#include <dirent.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace std;

static const char* SELECT_MAP_FOLDER = "maps/other/";

static string selected_map_path;

bool ZHasSelectedMap()
{
    return !selected_map_path.empty();
}

const char* ZSelectedMapPath()
{
    return selected_map_path.c_str();
}

static vector<string> FindPlayableMaps()
{
    vector<string> maps;

    DIR* dir = opendir(SELECT_MAP_FOLDER);

    if(!dir)
        return maps;

    struct dirent* entry;

    while((entry = readdir(dir)) != NULL)
    {
        string name = entry->d_name;

        if(name.length() < 5)
            continue;

        string extension = name.substr(name.length() - 4);

        if(extension == ".map" || extension == ".MAP")
            maps.push_back(name);
    }

    closedir(dir);

    sort(maps.begin(), maps.end());

    return maps;
}

// Same playable-map scan as the original Select Map screen.
vector<string> ZSelectMapPlayableMaps()
{
    vector<string> names = FindPlayableMaps();
    vector<string> paths;
    for(size_t i = 0; i < names.size(); ++i)
        paths.push_back(string(SELECT_MAP_FOLDER) + names[i]);
    return paths;
}

bool ZSelectMapQueueMap(const string& path)
{
    // Never accept an arbitrary path from the UI.
    vector<string> valid = ZSelectMapPlayableMaps();
    for(size_t i = 0; i < valid.size(); ++i)
    {
        if(valid[i] == path)
        {
            selected_map_path = path;
            return true;
        }
    }
    return false;
}

static void SelectBox(
    SDL_Surface* screen,
    int x, int y, int w, int h,
    int r, int g, int b
)
{
    SDL_Rect rect;

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;

    SDL_FillRect(
        screen,
        &rect,
        SDL_MapRGB(screen->format, r, g, b)
    );
}

static void SelectText(
    SDL_Surface* screen,
    TTF_Font* font,
    const string& value,
    int x, int y
)
{
    if(!font)
        return;

    SDL_Color color = {230, 215, 190, 0};

    SDL_Surface* text =
        TTF_RenderUTF8_Blended(
            font,
            value.c_str(),
            color
        );

    if(!text)
        return;

    SDL_Rect position;

    position.x = x;
    position.y = y;
    position.w = 0;
    position.h = 0;

    SDL_BlitSurface(
        text,
        NULL,
        screen,
        &position
    );

    SDL_FreeSurface(text);
}

static bool SelectInside(
    int x, int y,
    int bx, int by,
    int bw, int bh
)
{
    return x >= bx &&
           x < bx + bw &&
           y >= by &&
           y < by + bh;
}

// Return:
// -1 = Quit ZOD
//  0 = Back
//  1 = Play selected map

int RunZSelectMapMenu(
    SDL_Surface* screen,
    TTF_Font* font,
    TTF_Font* small_font
)
{
    // Enable smooth arrow-key navigation.
    // Automatically restore normal SDL keyboard behavior on exit.
    struct SelectMapKeyRepeat
    {
        SelectMapKeyRepeat()
        {
            SDL_EnableKeyRepeat(220, 70);
        }

        ~SelectMapKeyRepeat()
        {
            SDL_EnableKeyRepeat(0, 0);
        }
    } key_repeat;

    selected_map_path.clear();

    vector<string> maps = FindPlayableMaps();

    int selected = maps.empty() ? -1 : 0;
    int scroll = 0;
    Uint32 last_wheel_move = SDL_GetTicks() - 100;

    const int visible = 8;
    const int row_height = 43;
    const int row_start = 150;

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

                if(key == SDLK_DOWN && !maps.empty())
                {
                    if(selected < (int)maps.size() - 1)
                        selected++;
                }

                if(key == SDLK_UP && !maps.empty())
                {
                    if(selected > 0)
                        selected--;
                }

                if(key == SDLK_RETURN ||
                   key == SDLK_KP_ENTER)
                {
                    if(selected >= 0)
                    {
                        selected_map_path =
                            string(SELECT_MAP_FOLDER) +
                            maps[selected];

                        return 1;
                    }
                }
            }

            if(event.type == SDL_MOUSEBUTTONDOWN)
            {
                int x = event.button.x;
                int y = event.button.y;

                // Limit mouse wheel speed to one map per 100 ms
                if(event.button.button == SDL_BUTTON_WHEELDOWN ||
                   event.button.button == SDL_BUTTON_WHEELUP)
                {
                    Uint32 now = SDL_GetTicks();

                    if(now - last_wheel_move < 20)
                        continue;

                    last_wheel_move = now;
                }

                if(event.button.button == SDL_BUTTON_WHEELDOWN)
                {
                    if(selected >= 0 && selected + 1 < (int)maps.size())
                        selected++;

                    continue;
                }

                if(event.button.button == SDL_BUTTON_WHEELUP)
                {
                    if(selected > 0)
                        selected--;

                    continue;
                }

                if(event.button.button != SDL_BUTTON_LEFT)
                    continue;

                if(SelectInside(
                    x, y, 275, row_start,
                    730, visible * row_height
                ))
                {
                    int row = (y - row_start) / row_height;

                    int index = scroll + row;

                    if(index >= 0 && index < (int)maps.size())
                        selected = index;
                }

                // PLAY MAP
                if(SelectInside(x,y,275,565,320,60))
                {
                    if(selected >= 0)
                    {
                        selected_map_path =
                            string(SELECT_MAP_FOLDER) +
                            maps[selected];

                        return 1;
                    }
                }

                // BACK
                if(SelectInside(x,y,680,565,320,60))
                    return 0;
            }
        }

        if(selected >= 0)
        {
            if(selected < scroll)
                scroll = selected;

            if(selected >= scroll + visible)
                scroll = selected - visible + 1;
        }

        // Background
        SelectBox(screen,0,0,1280,720,25,28,30);

        SelectText(
            screen,font,
            "SELECT MAP",
            430,55
        );

        SelectText(
            screen,small_font,
            "Choose a custom map from maps/other",
            275,110
        );

        // Map list background
        SelectBox(
            screen,265,140,
            750,365,
            45,43,39
        );

        for(int row = 0; row < visible; row++)
        {
            int index = scroll + row;

            if(index >= (int)maps.size())
                break;

            int y = row_start + row * row_height;

            if(index == selected)
            {
                SelectBox(
                    screen,
                    275,y,
                    730,40,
                    105,84,52
                );
            }
            else
            {
                SelectBox(
                    screen,
                    275,y,
                    730,40,
                    56,55,51
                );
            }

            SelectText(
                screen,
                small_font,
                maps[index],
                292,y+8
            );
        }

        if(maps.empty())
        {
            SelectText(
                screen,small_font,
                "No maps found in maps/other/",
                315,220
            );
        }

        if(selected >= 0)
        {
            SelectText(
                screen,small_font,
                string("Selected: ") + maps[selected],
                275,520
            );
        }

        // Play button
        SelectBox(
            screen,
            275,565,
            320,60,
            80,105,60
        );

        SelectText(
            screen,font,
            "PLAY MAP",
            355,577
        );

        // Back button
        SelectBox(
            screen,
            680,565,
            320,60,
            105,65,55
        );

        SelectText(
            screen,font,
            "BACK",
            775,577
        );

        SelectText(
            screen,small_font,
            "Arrow keys / mouse wheel: scroll     Enter: play     ESC: back",
            275,665
        );

        SDL_Flip(screen);
        SDL_Delay(16);
    }
}