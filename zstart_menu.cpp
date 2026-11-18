#include "zstart_menu.h"
#include "zod_action_button.h"
#include "zod_volume_settings.h"
#include "zod_game_speed_settings.h"
#include "zstart_editor.h"
#include "zstart_select.h"
#include "zstart_multiplayer.h"
#include "zstart_replay.h"
#include "common.h"
#include "constants.h"

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_mixer.h>
#include <stdio.h>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <ctype.h>
#include <string.h>

using namespace COMMON;

// Main menu buttons
static const char* menu_items[] =
{
    "Start Game",
    "Replay Level",
    "Select Map",
    "Multiplayer",
    "Map Editor",
    "Quit Game"
};

static const int MENU_COUNT = 6;

static void Fill(
    SDL_Surface* screen,
    int x, int y, int w, int h,
    Uint8 r, Uint8 g, Uint8 b)
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

static void DrawText(
    SDL_Surface* screen,
    TTF_Font* font,
    const char* text,
    int x, int y,
    Uint8 r, Uint8 g, Uint8 b)
{
    if(!font) return;

    SDL_Color color = {r, g, b, 0};

    SDL_Surface* text_surface =
        TTF_RenderUTF8_Blended(font, text, color);

    if(!text_surface) return;

    SDL_Rect pos;
    pos.x = x;
    pos.y = y;

    SDL_BlitSurface(text_surface, NULL, screen, &pos);

    SDL_FreeSurface(text_surface);
}

// One user-controlled name shared by the menu and game startup.
// The small text file is stored alongside the game executable/assets.
static const char* ZOD_PLAYER_NAME_FILE = "player_name.txt";
static std::string zod_player_name;

const std::string& ZStartMenuPlayerName()
{
    return zod_player_name;
}

static std::string LoadZodPlayerName()
{
    std::ifstream input(ZOD_PLAYER_NAME_FILE, std::ios::in | std::ios::binary);
    if(!input) return "";

    std::string name;
    std::getline(input, name);
    if(!name.empty() && name[name.size() - 1] == '\r')
        name.erase(name.size() - 1);

    // Ignore corrupt/oversized settings instead of drawing outside the box.
    if(name.size() > 80) return "";
    for(size_t i = 0; i < name.size(); ++i)
        if(static_cast<unsigned char>(name[i]) < 32)
            return "";
    return name;
}

static void SaveZodPlayerName()
{
    std::ofstream output(ZOD_PLAYER_NAME_FILE,
                         std::ios::out | std::ios::binary | std::ios::trunc);
    if(output)
        output << zod_player_name << "\n";
    else
        fprintf(stderr, "ZOD Name: cannot save %s\n", ZOD_PLAYER_NAME_FILE);
}

static bool NameFieldAt(int x, int y)
{
    // The existing Name text box in the 1280x720 background PNG.
    return x >= 822 && x < 1041 && y >= 200 && y < 246;
}

static std::string UTF8FromSDL(Uint16 ch)
{
    std::string utf8;
    if(ch < 32 || ch == 127 || (ch >= 0xD800 && ch <= 0xDFFF))
        return utf8;
    if(ch < 0x80)
        utf8 += static_cast<char>(ch);
    else if(ch < 0x800)
    {
        utf8 += static_cast<char>(0xC0 | (ch >> 6));
        utf8 += static_cast<char>(0x80 | (ch & 0x3F));
    }
    else
    {
        utf8 += static_cast<char>(0xE0 | (ch >> 12));
        utf8 += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        utf8 += static_cast<char>(0x80 | (ch & 0x3F));
    }
    return utf8;
}

static void RemoveLastUTF8Character(std::string& text)
{
    if(text.empty()) return;
    size_t pos = text.size() - 1;
    while(pos > 0 &&
          (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
        --pos;
    text.erase(pos);
}

// Preserve the original Name label and input window on the clean background.
// This is a same-coordinate crop from the actual artwork, not a fake cover panel.
// It is harmless if the clean PNG already retained the Name field.
static void ZodRestoreNameArtwork(SDL_Surface* screen, SDL_Surface* original)
{
    if(!screen || !original) return;
    SDL_Rect source = {706, 187, 340, 64};
    SDL_Rect destination = {706, 187, 0, 0};
    SDL_BlitSurface(original, &source, screen, &destination);
}

static void DrawZodPlayerName(SDL_Surface* screen, TTF_Font* font, bool editing)
{
    // Only draw inside the artwork's existing input box.
    const int x = 834;
    const int y = 222 - TTF_FontHeight(font) / 2;
    int text_w = 0;
    int text_h = 0;

    SDL_Rect previous_clip;
    SDL_GetClipRect(screen, &previous_clip);
    SDL_Rect clip = {828, 205, 207, 35};
    SDL_SetClipRect(screen, &clip);

    if(!zod_player_name.empty())
    {
        DrawText(screen, font, zod_player_name.c_str(),
                 x, y, 225, 229, 237);
        TTF_SizeUTF8(font, zod_player_name.c_str(), &text_w, &text_h);
    }
    if(editing)
    {
        // Subtle caret; typing never requires Enter or a Save button.
        if((SDL_GetTicks() / 500) % 2 == 0)
            Fill(screen, x + text_w + 2, 209, 2, 25, 74, 199, 255);
    }
    SDL_SetClipRect(screen, &previous_clip);
}

// ZOD main-menu volume sliders (the original image remains unchanged).
// Coordinates use the game's existing 1280x720 menu artwork.
static bool ZodVolumeHit(int x, int y, bool music)
{
    const int center_y = music ? 410 : 354;
    return x >= 805 && x <= 1033 && y >= center_y - 22 && y <= center_y + 22;
}

static int ZodVolumePercentAt(int x)
{
    const int left = 833;
    const int right = 1017;
    if(x <= left) return 0;
    if(x >= right) return 100;
    return ((x - left) * 100 + (right - left) / 2) / (right - left);
}

static void DrawZodVolumeSlider(SDL_Surface* screen, int center_y, int percent)
{
    // The PNG has a fixed illustrated handle. Cover only its rail's interior,
    // then recreate the electric-blue rail and movable handle dynamically.
    // Neither the Sound/Music labels nor their metal panel borders change.
    const int top = center_y - 16;
    Fill(screen, 807, top, 226, 33, 4, 10, 17);
    Fill(screen, 809, top + 2, 222, 29, 3, 9, 15);

    // Neon rail: a dim halo, a bright center, and subtle 25% tick marks.
    Fill(screen, 827, center_y - 3, 201, 7, 2, 34, 62);
    Fill(screen, 828, center_y - 1, 199, 4, 3, 95, 151);
    Fill(screen, 828, center_y,     199, 2, 17, 198, 253);
    Fill(screen, 828, center_y + 1, 199, 1, 92, 234, 255);
    for(int i = 0; i <= 4; ++i)
    {
        int tick_x = 833 + (i * 184 + 2) / 4;
        Fill(screen, tick_x, center_y - 7, 1, 15, 3, 82, 152);
        Fill(screen, tick_x, center_y - 5, 1, 11, 24, 172, 242);
    }

    const int center_x = 833 + (ZodClampPercent(percent) * 184 + 50) / 100;
    const int x = center_x - 8;
    // Metallic blue handle and restrained glow, matching the existing artwork.
    Fill(screen, x - 3, top + 1, 22, 31, 2, 45, 91);
    Fill(screen, x - 1, top + 1, 18, 31, 11, 125, 206);
    Fill(screen, x, top + 2, 16, 29, 42, 219, 255);
    Fill(screen, x + 2, top + 4, 12, 24, 11, 137, 218);
    Fill(screen, x + 3, top + 5, 10, 22, 20, 184, 241);
    Fill(screen, x + 2, top + 2, 12, 2, 156, 244, 255);
    Fill(screen, x + 1, top + 29, 14, 2, 0, 66, 170);
}

// ZOD_MAIN_MENU_GAME_SPEED: reuse the seven original in-game speed presets.
// The original illustration remains untouched; only the speed rail is drawn live.
static bool ZodGameSpeedHit(int x, int y)
{
    return x >= 850 && x <= 1033 && y >= 447 && y <= 491;
}

static int ZodGameSpeedPresetAt(int x)
{
    const int left = 877;
    const int right = 1017;
    if(x <= left) return 0;
    if(x >= right) return 6;
    return ((x - left) * 6 + (right - left) / 2) / (right - left);
}

static void DrawZodGameSpeedSlider(SDL_Surface* screen, int preset)
{
    // Same restrained industrial/electric-blue style as Sound and Music.
    // Only cover the existing illustrated speed rail, not the "Game Speed" text.
    const int center_y = 469;
    const int top = center_y - 16;
    Fill(screen, 849, top, 184, 33, 4, 10, 17);
    Fill(screen, 851, top + 2, 180, 29, 3, 9, 15);

    Fill(screen, 865, center_y - 3, 163, 7, 2, 34, 62);
    Fill(screen, 866, center_y - 1, 161, 4, 3, 95, 151);
    Fill(screen, 866, center_y,     161, 2, 17, 198, 253);
    Fill(screen, 866, center_y + 1, 161, 1, 92, 234, 255);

    for(int i = 0; i < ZOD_GAME_SPEED_PRESETS; ++i)
    {
        const int tick_x = 877 + (i * 140 + 3) / 6;
        Fill(screen, tick_x, center_y - 7, 1, 15, 3, 82, 152);
        Fill(screen, tick_x, center_y - 5, 1, 11, 24, 172, 242);
    }

    if(preset < 0) preset = 0;
    if(preset >= ZOD_GAME_SPEED_PRESETS) preset = ZOD_GAME_SPEED_PRESETS - 1;
    const int center_x = 877 + (preset * 140 + 3) / 6;
    const int x = center_x - 8;
    Fill(screen, x - 3, top + 1, 22, 31, 2, 45, 91);
    Fill(screen, x - 1, top + 1, 18, 31, 11, 125, 206);
    Fill(screen, x, top + 2, 16, 29, 42, 219, 255);
    Fill(screen, x + 2, top + 4, 12, 24, 11, 137, 218);
    Fill(screen, x + 3, top + 5, 10, 22, 20, 184, 241);
    Fill(screen, x + 2, top + 2, 12, 2, 156, 244, 255);
    Fill(screen, x + 1, top + 29, 14, 2, 0, 66, 170);
}


// ZOD_MAP_EDITOR_INLINE_BEGIN
// Only user-editable maps in other/ and multiplayer/ are ever listed or opened.
// No changes to the existing standalone Map Editor itself.
static std::string zod_inline_editor_command;
const char* ZStartMenuEditorCommand()
{
    return zod_inline_editor_command.c_str();
}

static bool ZodEditorHit(int x, int y, int bx, int by, int w, int h)
{
    return x >= bx && x < bx + w && y >= by && y < by + h;
}

static bool ZodEditorSafeFilename(const std::string& name)
{
    if(name.empty() || name.size() > 96 || name.find("..") != std::string::npos)
        return false;
    for(size_t i = 0; i < name.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if(!(isalnum(c) || c == '_' || c == '-' || c == ' ' || c == '.'))
            return false;
    }
    return true;
}

static bool ZodEditorValidNewName(const std::string& name)
{
    if(name.empty() || name.size() > 32) return false;
    for(size_t i=0; i<name.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if(!(isalnum(c) || c=='_' || c=='-')) return false;
    }
    return true;
}

static bool ZodEditorFileExists(const std::string& path)
{
    FILE* file = fopen(path.c_str(), "rb");
    if(!file) return false;
    fclose(file);
    return true;
}

static std::string ZodEditorBasename(const std::string& path)
{
    size_t sep = path.find_last_of("/\\");
    return sep == std::string::npos ? path : path.substr(sep+1);
}

static bool ZodEditorSortByName(const std::string& first, const std::string& second)
{
    const std::string a = ZodEditorBasename(first);
    const std::string b = ZodEditorBasename(second);
    size_t i=0;
    for(;i<a.size() && i<b.size(); ++i)
    {
        int ac = tolower(static_cast<unsigned char>(a[i]));
        int bc = tolower(static_cast<unsigned char>(b[i]));
        if(ac != bc) return ac < bc;
    }
    if(a.size() != b.size()) return a.size() < b.size();
    return first < second;
}

static std::vector<std::string> ZodEditorEditableMaps()
{
    std::vector<std::string> maps;
    const char* directories[] = {"maps/other", "maps/multiplayer"};
    for(int folder=0; folder<2; ++folder)
    {
        DIR* dir = opendir(directories[folder]);
        if(!dir) continue;
        struct dirent* entry;
        while((entry=readdir(dir)) != NULL)
        {
            std::string name = entry->d_name;
            if(name.size() < 5 || !ZodEditorSafeFilename(name)) continue;
            std::string extension = name.substr(name.size()-4);
            for(size_t i=0;i<extension.size();++i)
                extension[i] = static_cast<char>(tolower(static_cast<unsigned char>(extension[i])));
            if(extension != ".map") continue;
            std::string full = std::string(directories[folder]) + "/" + name;
            if(ZodEditorFileExists(full)) maps.push_back(full);
        }
        closedir(dir);
    }
    std::sort(maps.begin(), maps.end(), ZodEditorSortByName);
    return maps;
}

static bool ZodEditorQueueLaunch(const std::string& path, bool creating,
                                 const std::string& name, int size_index,
                                 int terrain_index, std::string& error)
{
    const bool other = path.compare(0, 11, "maps/other/")==0;
    const bool multi = path.compare(0, 17, "maps/multiplayer/")==0;
    const std::string file = ZodEditorBasename(path);
    std::string extension = file.size() >= 4 ? file.substr(file.size()-4) : "";
    for(size_t i=0;i<extension.size();++i)
        extension[i]=static_cast<char>(tolower(static_cast<unsigned char>(extension[i])));
    const std::string allowed_prefix = other ? "maps/other/" : "maps/multiplayer/";
    if((!other && !multi) || !ZodEditorSafeFilename(file) ||
       path != allowed_prefix + file || file.size() < 5 || extension != ".map")
    {
        error = "Invalid map path";
        return false;
    }
    if(creating)
    {
        if(!ZodEditorValidNewName(name))
        {
            error = "Use letters, numbers, _ or -";
            return false;
        }
        if(ZodEditorFileExists(path))
        {
            error = "This map already exists";
            return false;
        }
        if(terrain_index < 0 || terrain_index >= MAX_PLANET_TYPES ||
           size_index < 0 || size_index >= 4)
        {
            error = "Invalid map settings";
            return false;
        }
    }
    else if(!ZodEditorFileExists(path))
    {
        error = "Map file no longer exists";
        return false;
    }

    const char* locations[] = {"src\\zod_map_editor.exe", "zod_map_editor.exe"};
    std::string exe;
    for(int i=0;i<2;i++)
        if(ZodEditorFileExists(locations[i])) {exe = locations[i];break;}
    // The main ZOD EXE now contains an embedded copy of the editor.
    // Keep the original command shape for parsing, but the separate EXE
    // is no longer required when opening an editor from the main menu.
    if(exe.empty()) exe = "zod_map_editor.exe";
    zod_inline_editor_command = "\"" + exe + "\" -f \"" + path + "\"";
    if(creating)
    {
        const int dimensions[] = {64,96,128,160};
        char dimension[32];
        snprintf(dimension,sizeof(dimension),"%dx%d", dimensions[size_index], dimensions[size_index]);
        zod_inline_editor_command += " -n -d ";
        zod_inline_editor_command += dimension;
        zod_inline_editor_command += " -p \"";
        zod_inline_editor_command += planet_type_string[terrain_index];
        zod_inline_editor_command += "\" -m \"" + name + "\"";
    }
    return true;
}

enum MenuAction { MA_PLAY=7, MA_HOST, MA_JOIN, MA_NEW, MA_OPEN,
                  MA_BACK, MA_CREATE, MA_SIZE_L, MA_SIZE_R, MA_TERRAIN_L,
                  MA_TERRAIN_R, MA_SAVE_L, MA_SAVE_R };
struct MenuFeedback {
    const ZodActionButton& button;
    int hover;
    int State(int id) const { return button.Visual(id,hover); }
};

static void ZodEditorDrawButton(SDL_Surface* s, TTF_Font* font,
                                const char* caption, int x, int y, int w,
                                bool enabled, int h=39, int state=0)
{
    const bool hover=enabled && state==1, pressed=enabled && state==2;
    Fill(s,x,y,w,h,enabled?(hover?120:pressed?48:80):59,
         enabled?(hover?200:pressed?95:144):70,enabled?(hover?240:pressed?135:179):82);
    Fill(s,x+2,y+2,w-4,h-4,enabled?(hover?28:pressed?9:18):28,
         enabled?(hover?75:pressed?30:50):35,enabled?(hover?104:pressed?48:74):40);
    int tw=0,th=0;
    TTF_SizeUTF8(font,caption,&tw,&th);
    DrawText(s,font,caption,x+(w-tw)/2+(pressed?1:0),y+(h-th)/2+(pressed?2:0),
             enabled?(hover?170:109):106,enabled?(hover?243:217):113,enabled?255:120);
}

// NEW MAP: three fixed, two-direction controls. The center is display-only.
// The same geometry is shared by the mouse hit-testing below.
static const int ZOD_EDITOR_SELECTOR_X = 861;
static const int ZOD_EDITOR_SELECTOR_W = 176;
static const int ZOD_EDITOR_SELECTOR_H = 34;
static const int ZOD_EDITOR_ARROW_W = 38;

static int ZodEditorSelectorDirection(int x)
{
    if(x < ZOD_EDITOR_SELECTOR_X + ZOD_EDITOR_ARROW_W) return -1;
    if(x >= ZOD_EDITOR_SELECTOR_X + ZOD_EDITOR_SELECTOR_W - ZOD_EDITOR_ARROW_W)
        return 1;
    return 0; // Clicking the displayed value does not alter the selection.
}

static void ZodEditorDrawSelector(SDL_Surface* screen, TTF_Font* font,
                                  const char* value, int y, int left_state, int right_state)
{
    const int x = ZOD_EDITOR_SELECTOR_X;
    const int w = ZOD_EDITOR_SELECTOR_W;
    const int h = ZOD_EDITOR_SELECTOR_H;
    // Reuse the existing industrial-blue button, with no additional PNGs.
    ZodEditorDrawButton(screen,font,"",x,y,w,true,h);

    // Subtle fixed dividers reserve the left/right 38px for the arrow controls.
    Fill(screen,x+ZOD_EDITOR_ARROW_W,y+3,1,h-6,34,99,137);
    Fill(screen,x+w-ZOD_EDITOR_ARROW_W-1,y+3,1,h-6,34,99,137);

    int tw=0,th=0;
    for(int i=0;i<2;++i)
    {
        const int state=i?right_state:left_state;
        const int ax=i?x+w-ZOD_EDITOR_ARROW_W:x;
        const char* label=i?">":"<";
        if(state) ZodEditorDrawButton(screen,font,label,ax,y,ZOD_EDITOR_ARROW_W,true,h,state);
        else {
            TTF_SizeUTF8(font,label,&tw,&th);
            DrawText(screen,font,label,ax+(ZOD_EDITOR_ARROW_W-tw)/2,y+(h-th)/2,109,217,255);
        }
    }

    // Keep the selected value readable and centered between the two arrows.
    std::string visible = value;
    const int available = w-2*ZOD_EDITOR_ARROW_W-12;
    TTF_SizeUTF8(font,visible.c_str(),&tw,&th);
    while(tw > available && visible.size() > 1)
    {
        visible.erase(visible.size()-1);
        TTF_SizeUTF8(font,visible.c_str(),&tw,&th);
    }
    DrawText(screen,font,visible.c_str(),x+(w-tw)/2,y+(h-th)/2,
             109,217,255);
}

static void DrawZodNewMapPanel(SDL_Surface* screen, TTF_Font* font,
                               TTF_Font* small_font, TTF_Font* hint_font,
                               const std::string& name, bool editing,
                               int size_index, int terrain_index,
                               int save_index, const std::string& message,
                               const MenuFeedback& feedback)
{
    // The back arrow, title and explanatory line follow the established lobby spacing.
    const int back=feedback.State(MA_BACK);
    DrawText(screen,font,"<",721+(back==2),264+(back==2?2:0),back==1?170:91,back==1?243:207,252);
    DrawText(screen,font,"NEW MAP",760,264,91,207,252);
    if(message.empty())
        DrawText(screen,small_font,"Create a new custom map",724,292,168,179,187);
    else
        DrawText(screen,hint_font,message.c_str(),724,294,241,160,143);

    Fill(screen,723,320,318,37,73,83,92);
    Fill(screen,725,322,314,33,8,14,21);
    SDL_Rect old_clip;
    SDL_GetClipRect(screen,&old_clip);
    SDL_Rect input_clip = {733,325,294,28};
    SDL_SetClipRect(screen,&input_clip);
    std::string visible_name = name;
    int text_width=0,unused_height=0;
    while(!visible_name.empty() &&
          TTF_SizeUTF8(small_font,visible_name.c_str(),&text_width,&unused_height)==0 &&
          text_width > 277)
        visible_name.erase(0,1);
    if(visible_name.empty()) visible_name="Type map name...";
    DrawText(screen,small_font,visible_name.c_str(),733,328,
             name.empty()?111:215,name.empty()?123:229,name.empty()?140:240);
    if(editing && (SDL_GetTicks()/500)%2==0)
    {
        if(name.empty()) text_width=0;
        else TTF_SizeUTF8(small_font,visible_name.c_str(),&text_width,&unused_height);
        Fill(screen,735+text_width,326,2,24,74,199,255);
    }
    SDL_SetClipRect(screen,&old_clip);

    const int dimensions[] = {64,96,128,160};
    char dimensions_text[32];
    snprintf(dimensions_text,sizeof(dimensions_text),"%d x %d", dimensions[size_index],dimensions[size_index]);
    DrawText(screen,small_font,"Map Size",725,375,180,194,207);
    ZodEditorDrawSelector(screen,small_font,dimensions_text,367,feedback.State(MA_SIZE_L),feedback.State(MA_SIZE_R));

    DrawText(screen,small_font,"Terrain",725,415,180,194,207);
    std::string terrain_text = planet_type_string[terrain_index];
    if(terrain_text.size()>13) terrain_text=terrain_text.substr(0,13);
    ZodEditorDrawSelector(screen,small_font,terrain_text.c_str(),407,feedback.State(MA_TERRAIN_L),feedback.State(MA_TERRAIN_R));

    DrawText(screen,small_font,"Save To",725,455,180,194,207);
    ZodEditorDrawSelector(screen,small_font,save_index==0?"Other":"Multiplayer",447,feedback.State(MA_SAVE_L),feedback.State(MA_SAVE_R));

    ZodEditorDrawButton(screen,font,"Create Map",796,510,178,true,39,feedback.State(MA_CREATE));
}
// ZOD_MAP_EDITOR_INLINE_END

// ZOD_SHARED_LIST_PANEL_BEGIN
// Shared right-hand list layout: later reused by Select Map and Map Editor.
// The artwork, Name area and all left-hand buttons remain unchanged.
static const int ZOD_REPLAY_ROWS = 6;
static const int ZOD_REPLAY_LIST_X = 722;
static const int ZOD_REPLAY_LIST_Y = 323;
static const int ZOD_REPLAY_ROW_H = 29;
static const int ZOD_REPLAY_SCROLL_X = 1025;
static const int ZOD_REPLAY_PLAY_X = 796;
static const int ZOD_REPLAY_PLAY_Y = 510;

static int ZodReplayClampScroll(int scroll, int count)
{
    int maximum = count - ZOD_REPLAY_ROWS;
    if(maximum < 0) maximum = 0;
    if(scroll < 0) return 0;
    if(scroll > maximum) return maximum;
    return scroll;
}

static bool ZodReplayPanelAt(int x, int y)
{
    return x >= 709 && x < 1058 && y >= 255 && y < 555;
}

static int ZodReplayRowAt(int x, int y)
{
    if(x < ZOD_REPLAY_LIST_X || x >= ZOD_REPLAY_SCROLL_X - 6 ||
       y < ZOD_REPLAY_LIST_Y ||
       y >= ZOD_REPLAY_LIST_Y + ZOD_REPLAY_ROWS * ZOD_REPLAY_ROW_H)
        return -1;
    return (y - ZOD_REPLAY_LIST_Y) / ZOD_REPLAY_ROW_H;
}

static bool ZodReplayScrollAt(int x, int y)
{
    return x >= ZOD_REPLAY_SCROLL_X && x <= 1047 &&
           y >= ZOD_REPLAY_LIST_Y &&
           y < ZOD_REPLAY_LIST_Y + ZOD_REPLAY_ROWS * ZOD_REPLAY_ROW_H;
}

static int ZodReplayScrollFromY(int y, int count)
{
    const int max_scroll = ZodReplayClampScroll(count, count);
    if(max_scroll <= 0) return 0;
    const int travel = ZOD_REPLAY_ROWS * ZOD_REPLAY_ROW_H - 24;
    int offset = y - ZOD_REPLAY_LIST_Y - 12;
    if(offset < 0) offset = 0;
    if(offset > travel) offset = travel;
    return (offset * max_scroll + travel / 2) / travel;
}

static std::string ZodReplayDisplayName(const std::string& path, int index)
{
    size_t slash = path.find_last_of("/\\");
    std::string label = slash == std::string::npos ? path : path.substr(slash + 1);
    if(label.size() >= 4 && label.substr(label.size() - 4) == ".map")
        label.erase(label.size() - 4);
    for(size_t i = 0; i < label.size(); ++i)
        if(label[i] == '_') label[i] = ' ';
    // Old or custom map names still get the proper completed-level number.
    if(label.size() < 3 || label[0] < '0' || label[0] > '9' ||
       label[1] < '0' || label[1] > '9')
    {
        char prefix[24];
        snprintf(prefix, sizeof(prefix), "%02d  ", index + 1);
        label = std::string(prefix) + label;
    }
    return label;
}

static std::string ZodSelectDisplayName(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string label = slash == std::string::npos ? path : path.substr(slash + 1);
    if(label.size() >= 4)
    {
        const std::string suffix = label.substr(label.size() - 4);
        if(suffix == ".map" || suffix == ".MAP") label.erase(label.size() - 4);
    }
    for(size_t i = 0; i < label.size(); ++i)
        if(label[i] == '_') label[i] = ' ';
    return label;
}

static void ZodReplayKeepVisible(int& scroll, int selected, int count)
{
    if(selected < 0) return;
    if(selected < scroll) scroll = selected;
    if(selected >= scroll + ZOD_REPLAY_ROWS)
        scroll = selected - ZOD_REPLAY_ROWS + 1;
    scroll = ZodReplayClampScroll(scroll, count);
}

// The selected row should never be hidden after dragging the scrollbar.
static void ZodReplaySelectVisible(int& selected, int scroll, int count)
{
    if(count <= 0) { selected = -1; return; }
    if(selected < scroll) selected = scroll;
    if(selected >= scroll + ZOD_REPLAY_ROWS)
        selected = scroll + ZOD_REPLAY_ROWS - 1;
    if(selected >= count) selected = count - 1;
}

static void DrawZodReplayPanel(SDL_Surface* screen, TTF_Font* font,
                               TTF_Font* small_font, TTF_Font* hint_font,
                               const std::vector<std::string>& maps,
                               int selected_level, int scroll,
                               int panel_mode,
                               const std::string& editor_message, const MenuFeedback& feedback)
{
    const bool select_map_mode = (panel_mode == 2);
    const bool multiplayer_mode = (panel_mode == 3);
    const bool editor_mode = (panel_mode == 4);
    // The approved clean PNG provides the metal background; draw controls on it.

    DrawText(screen, font, editor_mode ? "MAP EDITOR" :
             (multiplayer_mode ? "MULTIPLAYER" :
             (select_map_mode ? "SELECT MAP" : "REPLAY LEVEL")),
             724, 264, 91, 207, 252);
    char progress[40];
    if(select_map_mode || multiplayer_mode || editor_mode)
        snprintf(progress, sizeof(progress), "%d maps", (int)maps.size());
    else
        snprintf(progress, sizeof(progress), "%d / 35", (int)maps.size());
    DrawText(screen, small_font, progress,
             select_map_mode ? 973 : 979, 270, 198, 204, 211);
    DrawText(screen, editor_mode ? hint_font : small_font,
             (editor_mode && !editor_message.empty()) ? editor_message.c_str() :
             (editor_mode ? "Select a map to edit or create a new one" :
             (multiplayer_mode ? "Select a map to host" :
             (select_map_mode ? "Custom maps - maps/other" : "Completed levels"))),
             724, editor_mode ? 294 : 292,
             (editor_mode && !editor_message.empty()) ? 241 : 168,
             (editor_mode && !editor_message.empty()) ? 160 : 179,
             (editor_mode && !editor_message.empty()) ? 143 : 187);

    // Six visible entries; a real scrollbar covers the rest of the 35 missions.
    Fill(screen, 718, 320, 333, 179, 73, 83, 92);
    Fill(screen, 720, 322, 329, 175, 8, 14, 21);
    for(int row = 0; row < ZOD_REPLAY_ROWS; ++row)
    {
        const int index = scroll + row;
        if(index >= (int)maps.size()) break;
        const int y = ZOD_REPLAY_LIST_Y + row * ZOD_REPLAY_ROW_H;
        if(index == selected_level)
        {
            Fill(screen, 723, y, 296, ZOD_REPLAY_ROW_H - 1, 24, 81, 124);
            Fill(screen, 723, y, 3, ZOD_REPLAY_ROW_H - 1, 73, 204, 255);
        }
        else
            Fill(screen, 723, y, 296, ZOD_REPLAY_ROW_H - 1,
                 (row % 2) ? 16 : 12, (row % 2) ? 23 : 20, (row % 2) ? 31 : 28);
        // Prevent long map names from spilling into the scrollbar area.
        SDL_Rect old_clip;
        SDL_GetClipRect(screen, &old_clip);
        SDL_Rect clip = {729, Sint16(y + 1), 285, Uint16(ZOD_REPLAY_ROW_H - 2)};
        SDL_SetClipRect(screen, &clip);
        std::string label = (select_map_mode || multiplayer_mode || editor_mode)
            ? ZodSelectDisplayName(maps[index])
            : ZodReplayDisplayName(maps[index], index);
        if(editor_mode)
        {
            // Disambiguate equal filenames across the two editable folders.
            for(size_t k=0;k<maps.size();++k)
            {
                if(k!=(size_t)index &&
                   ZodEditorBasename(maps[k])==ZodEditorBasename(maps[index]))
                {
                    label += maps[index].compare(0,17,"maps/multiplayer/")==0 ? " [MP]" : " [Other]";
                    break;
                }
            }
        }
        DrawText(screen, small_font, label.c_str(), 733, y + 4,
                 index == selected_level ? 223 : 188,
                 index == selected_level ? 240 : 201,
                 index == selected_level ? 255 : 216);
        SDL_SetClipRect(screen, &old_clip);
    }
    if(maps.empty())
    {
        DrawText(screen, small_font,
                 editor_mode ? "No editable maps found" :
                 (multiplayer_mode ? "No multiplayer maps" :
                 (select_map_mode ? "No custom maps found" : "No completed levels yet")),
                 733, 362, 194, 199, 205);
        DrawText(screen, small_font,
                 editor_mode ? "Use New Map to create one" :
                 (multiplayer_mode ? "Check maps/multiplayer/" :
                 (select_map_mode ? "Check maps/other/" : "Finish Level 01 first")),
                 733, 388, 145, 154, 165);
    }

    Fill(screen, 1024, 322, 24, 175, 59, 69, 78);
    Fill(screen, 1026, 324, 20, 171, 5, 12, 19);
    const int maximum = ZodReplayClampScroll((int)maps.size(), (int)maps.size());
    int thumb_h = 171;
    if((int)maps.size() > ZOD_REPLAY_ROWS)
        thumb_h = 171 * ZOD_REPLAY_ROWS / (int)maps.size();
    if(thumb_h < 24) thumb_h = 24;
    int thumb_y = 324;
    if(maximum > 0)
        thumb_y += (171 - thumb_h) * scroll / maximum;
    Fill(screen, 1029, thumb_y, 14, thumb_h, 8, 80, 137);
    Fill(screen, 1031, thumb_y + 2, 10, thumb_h - 4, 38, 175, 238);

    if(editor_mode)
    {
        const bool can_open = selected_level >= 0 && selected_level < (int)maps.size();
        ZodEditorDrawButton(screen,font,"New Map",727,510,143,true,39,feedback.State(MA_NEW));
        ZodEditorDrawButton(screen,font,"Open Map",886,510,151,can_open,39,feedback.State(MA_OPEN));
        return;
    }
    // Inline multiplayer: choose a map for Host, or search a LAN lobby via Join.
    if(multiplayer_mode)
    {
        // Two action buttons instead of a Back button. Main navigation stays visible.
        const bool can_host = selected_level >= 0 && selected_level < (int)maps.size();
        ZodEditorDrawButton(screen,font,"Host Game",727,510,143,can_host,39,feedback.State(MA_HOST));
        ZodEditorDrawButton(screen,font,"Join Game",886,510,151,true,39,feedback.State(MA_JOIN));
        return;
    }
    // Play uses the selected completed level (no extra Back button).
    const bool playable = selected_level >= 0 && selected_level < (int)maps.size();
    const int play_state = feedback.State(MA_PLAY);
    const bool pressed = playable && play_state == 2;
    const bool hover = playable && play_state == 1;
    Fill(screen, ZOD_REPLAY_PLAY_X, ZOD_REPLAY_PLAY_Y, 178, 39,
         playable ? (hover ? 120 : pressed ? 48 : 80) : 59,
         playable ? (hover ? 200 : pressed ? 95 : 144) : 70,
         playable ? (hover ? 240 : pressed ? 135 : 179) : 82);
    Fill(screen, ZOD_REPLAY_PLAY_X + 2, ZOD_REPLAY_PLAY_Y + 2, 174, 35,
         playable ? (hover ? 28 : pressed ? 9 : 18) : 28,
         playable ? (hover ? 75 : pressed ? 30 : 50) : 35,
         playable ? (hover ? 104 : pressed ? 48 : 74) : 40);
    int tw = 0, th = 0;
    const char* play_caption = select_map_mode ? "Play Map" : "Play Level";
    TTF_SizeUTF8(font, play_caption, &tw, &th);
    DrawText(screen, font, play_caption,
             ZOD_REPLAY_PLAY_X + (178 - tw) / 2 + (pressed ? 1 : 0),
             ZOD_REPLAY_PLAY_Y + (39 - th) / 2 + (pressed ? 2 : 0),
             playable ? (hover ? 170 : 109) : 106,
             playable ? (hover ? 243 : 217) : 113, playable ? 255 : 120);
}
// ZOD_SHARED_LIST_PANEL_END

static SDL_Rect GetButtonRect(SDL_Surface* screen, int i)
{
    static const int coords[MENU_COUNT][4] =
    {
        {230, 198, 275, 45},  // Start Game
        {230, 247, 275, 45},  // Replay Level
        {230, 296, 275, 45},  // Select Map
        {230, 345, 275, 45},  // Multiplayer
        {230, 394, 275, 45},  // Map Editor
        {330, 528, 205, 54}   // Quit Game
    };

    SDL_Rect rect;

    rect.x = coords[i][0];
    rect.y = coords[i][1];
    rect.w = coords[i][2];
    rect.h = coords[i][3];

    return rect;
}
static int ButtonAt(SDL_Surface* screen, int x, int y)
{
    for(int i = 0; i < MENU_COUNT; i++)
    {
        SDL_Rect r = GetButtonRect(screen, i);

        if(x >= r.x && x < r.x + r.w &&
           y >= r.y && y < r.y + r.h)
            return i;
    }

    return -1;
}

// One-shot navigation from the still-running LAN lobby.
// main.cpp already calls RunZStartMenu() again when a lobby is left.
static int MenuActionAt(SDL_Surface* screen,int x,int y,int mode,bool new_map,bool valid_map)
{
    const int nav=ButtonAt(screen,x,y);
    if(nav>=0) return nav+1;
    if(mode==4 && new_map) {
        if(ZodEditorHit(x,y,712,259,43,31)) return MA_BACK;
        if(ZodEditorHit(x,y,796,510,178,39)) return MA_CREATE;
        for(int row=0;row<3;++row) {
            if(ZodEditorHit(x,y,ZOD_EDITOR_SELECTOR_X,367+40*row,ZOD_EDITOR_SELECTOR_W,ZOD_EDITOR_SELECTOR_H)) {
                const int direction=ZodEditorSelectorDirection(x);
                if(direction) return MA_SIZE_L+row*2+(direction>0);
            }
        }
    } else if(mode==3 || mode==4) {
        if(ZodEditorHit(x,y,727,510,143,39)) return mode==4?MA_NEW:(valid_map?MA_HOST:0);
        if(ZodEditorHit(x,y,886,510,151,39)) return mode==3?MA_JOIN:(valid_map?MA_OPEN:0);
    } else if((mode==1 || mode==2) && valid_map && ZodEditorHit(x,y,796,510,178,39)) return MA_PLAY;
    return 0;
}

// This lets every existing left-side button work without touching main.cpp.
static int zod_lobby_return_button = -1;
void ZStartMenuQueueNavigationFromLobby(int button_index)
{
    if(button_index >= 0 && button_index < MENU_COUNT)
        zod_lobby_return_button = button_index;
}

int RunZStartMenu()
{
    int lobby_nav = zod_lobby_return_button;
    zod_lobby_return_button = -1;
    // Start Game / Quit Game should act immediately, not flash another menu.
    if(lobby_nav == 0 || lobby_nav == 5)
    {
        // Keep SDL video alive when handing control to the game.
        // The player will switch this surface to its OpenGL game mode.
        if(lobby_nav == 5) SDL_Quit();
        return lobby_nav == 0 ? ZSTART_GAME : ZSTART_QUIT;
    }

    // If the LAN lobby returned without starting a game, its display stays
    // alive. Reuse it: no destroy/recreate flicker on the way back.
    if(!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
    {
        if(SDL_Init(SDL_INIT_VIDEO) < 0)
            return ZSTART_QUIT;
    }

    SDL_Surface* screen = SDL_GetVideoSurface();
    // The just-finished game used OpenGL. Switch back to the menu's
    // software surface even when both resolutions are 1280x720.
    // Reuse the existing fullscreen window without SDL_Quit().
    if(!screen || screen->w != 1280 || screen->h != 720 ||
       (screen->flags & SDL_OPENGL) || !(screen->flags & SDL_FULLSCREEN))
        screen = SDL_SetVideoMode(1280, 720, 32, SDL_SWSURFACE | SDL_FULLSCREEN);

    if(!screen)
    {
        SDL_Quit();
        return ZSTART_QUIT;
    }

    // Show the startup artwork ONCE, before the main menu is initialized.
    // Returning from a mission, editor or LAN lobby never repeats it.
    static bool startup_splash_shown = false;
    if(!startup_splash_shown)
    {
        startup_splash_shown = true;
        SDL_Surface* splash = SDL_LoadBMP("assets/other/splash.bmp");
        if(splash)
        {
            if(splash->w == 1280 && splash->h == 720)
            {
                SDL_WM_SetCaption("ZOD", "ZOD");
                SDL_ShowCursor(SDL_DISABLE);
                SDL_BlitSurface(splash, NULL, screen, NULL);
                SDL_Flip(screen);

                // Display for up to 2.5 seconds; any click or key skips it.
                // Consume splash input so it cannot activate a menu button.
                const Uint32 started = SDL_GetTicks();
                bool dismiss = false;
                bool quit = false;
                while(!dismiss && SDL_GetTicks() - started < 2500)
                {
                    SDL_Event splash_event;
                    while(SDL_PollEvent(&splash_event))
                    {
                        if(splash_event.type == SDL_QUIT)
                        {
                            quit = true;
                            dismiss = true;
                            break;
                        }
                        if(splash_event.type == SDL_KEYDOWN ||
                           splash_event.type == SDL_MOUSEBUTTONDOWN)
                        {
                            dismiss = true;
                            break;
                        }
                    }
                    if(!dismiss) SDL_Delay(16);
                }
                // Drop any remaining key/button releases from the skip action.
                SDL_Event trailing_event;
                while(SDL_PollEvent(&trailing_event))
                    if(trailing_event.type == SDL_QUIT) quit = true;
                SDL_FreeSurface(splash);
                if(quit)
                {
                    SDL_Quit();
                    return ZSTART_QUIT;
                }
            }
            else
            {
                fprintf(stderr, "ZOD Splash: expected 1280x720 BMP.\n");
                SDL_FreeSurface(splash);
            }
        }
        else
        {
            // Optional asset: missing artwork must never block the menu.
            fprintf(stderr, "ZOD Splash: %s\n", SDL_GetError());
        }
    }

    SDL_WM_SetCaption("ZOD - Main Menu", "ZOD");
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);

    if(TTF_Init() < 0)
    {
        SDL_Quit();
        return ZSTART_QUIT;
    }

    TTF_Font* title_font =
        TTF_OpenFont("assets/Arimo-Regular.ttf", 64);

    TTF_Font* button_font =
        TTF_OpenFont("assets/menu/Oxanium-Bold.ttf", 28);

    TTF_Font* small_font =
        TTF_OpenFont("assets/Arimo-Regular.ttf", 18);

    TTF_Font* name_font =
        TTF_OpenFont("assets/menu/Oxanium-Bold.ttf", 23);

    TTF_Font* hint_font = TTF_OpenFont("assets/Arimo-Regular.ttf", 14);
    if(!hint_font) hint_font = small_font;

    if(!title_font || !button_font || !small_font || !name_font)
    {
        fprintf(stderr, "ZOD Main Menu: could not load font\n");

        if(title_font) TTF_CloseFont(title_font);
        if(button_font) TTF_CloseFont(button_font);
        if(small_font) TTF_CloseFont(small_font);
        if(name_font) TTF_CloseFont(name_font);
        if(hint_font && hint_font != small_font) TTF_CloseFont(hint_font);

        TTF_Quit();
        SDL_Quit();

        return ZSTART_QUIT;
    }

    zod_player_name = LoadZodPlayerName();
    ZodSavedVolumes volume_settings = ZodLoadSavedVolumes();
    int sound_percent = volume_settings.sound;
    int music_percent = volume_settings.music;
    int game_speed_preset = ZodLoadSavedGameSpeedPreset();
    bool dragging_sound = false;
    bool dragging_music = false;
    bool dragging_game_speed = false;
    SDL_EnableUNICODE(1); // SDL 1.2: enable translated characters (also ae/oe/aa).

    bool running = true;
    bool name_editing = false;
    // 0=settings; 1=Replay; 2=Select; 3=Multiplayer; 4=Map Editor.
    // The New Map form lives inside mode 4, without changing the main menu.
    int list_panel_mode = 0;
    bool editor_new_map = false;
    bool editor_map_name_editing = false;
    std::string editor_map_name;
    std::string editor_message;
    int editor_size_index = 2; // 128x128 default, same choices as the old editor menu
    int editor_terrain_index = 0;
    int editor_save_index = 0; // 0 = other, 1 = multiplayer
    zod_inline_editor_command.clear();
    bool replay_drag_scroll = false;
    bool replay_list_focus = false;
    std::vector<std::string> replay_maps;
    int replay_selected = -1;
    int replay_scroll = 0;
    int result = ZSTART_QUIT;
    // Keyboard navigation focus is separate from the active submenu.
    // Mouse hover must never become a sticky menu selection.
    int selected = (lobby_nav >= 0 && lobby_nav < MENU_COUNT) ? lobby_nav : 0;
    bool keyboard_navigation = false;

    // Return directly to the requested inline panel after a lobby exit.
    if(lobby_nav >= 1 && lobby_nav <= 4)
    {
        list_panel_mode = lobby_nav;
        if(lobby_nav == 1) replay_maps = ZStartReplayCompletedMaps();
        else if(lobby_nav == 2) replay_maps = ZSelectMapPlayableMaps();
        else if(lobby_nav == 4) replay_maps = ZodEditorEditableMaps();
        else
        {
            replay_maps = ZMultiplayerPlayableMaps();
            const std::string& previous_map = ZMultiplayerSelectedMap();
            for(size_t i=0; i<replay_maps.size(); ++i)
                if(replay_maps[i] == previous_map) replay_selected = (int)i;
        }
        if(replay_selected < 0 && !replay_maps.empty()) replay_selected = 0;
        ZodReplayKeepVisible(replay_scroll, replay_selected, (int)replay_maps.size());
    }

    SDL_Surface* menu_background = IMG_Load("assets/menu/zod_main_menu.png");
    SDL_Surface* clean_background = IMG_Load("assets/menu/zod_main_menu_clean.png");
    // Both approved menu artworks are required; do not render obsolete backup UI.
    if(clean_background && (clean_background->w != 1280 || clean_background->h != 720))
    {
        fprintf(stderr, "ZOD: clean menu PNG must be 1280x720.\n");
        SDL_FreeSurface(clean_background);
        clean_background = NULL;
    }
    if(!menu_background || !clean_background)
    {
        fprintf(stderr, "ZOD: required menu artwork missing; aborting menu safely.\n");
        if(menu_background) SDL_FreeSurface(menu_background);
        if(clean_background) SDL_FreeSurface(clean_background);
        if(title_font) TTF_CloseFont(title_font);
        if(button_font) TTF_CloseFont(button_font);
        if(small_font) TTF_CloseFont(small_font);
        if(name_font) TTF_CloseFont(name_font);
        if(hint_font && hint_font != small_font) TTF_CloseFont(hint_font);
        TTF_Quit();
        SDL_Quit();
        return ZSTART_QUIT;
    }
    // ZOD_QUIT_HOVER_BEGIN
    SDL_Surface* quit_hover = IMG_Load("assets/menu/zod_quit_hover.png");
    if(!quit_hover)
        fprintf(stderr, "ZOD Quit Hover: %s\n", IMG_GetError());
    // ZOD_QUIT_HOVER_END
    // Approved menu artwork has already been validated above.

    const char* status =
        "Select an option";

    // The menu owns its audio device independently of each match.
    const bool menu_audio = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 &&
                            Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 4096) == 0;
    Mix_Music *menu_music = NULL;
    Mix_Chunk *menu_click = NULL;
    ZodActionButton action_button;
    SDL_Event deferred_click = {};
    if(menu_audio)
    {
        menu_click = Mix_LoadWAV("assets/sounds/CLICK1L.wav");
        if(menu_click) Mix_VolumeChunk(menu_click, 75);
        menu_music = Mix_LoadMUS("assets/sounds/Music_options.ogg");
        Mix_VolumeMusic((80 * music_percent + 50)/100);
        if(menu_music) Mix_PlayMusic(menu_music, -1);
        else fprintf(stderr,"ZOD Menu music: %s\n",Mix_GetError());
    }


    // Local controls respond quickly; transitions allow the click to finish before teardown.
    const auto action_hit = [&](int x,int y) -> int {
        return MenuActionAt(screen,x,y,list_panel_mode,editor_new_map,
            replay_selected>=0 && replay_selected<(int)replay_maps.size());
    };
    const auto play_click = [&]() {
        if(menu_click && sound_percent>0) {
            Mix_VolumeChunk(menu_click,(75*sound_percent+50)/100);
            Mix_PlayChannel(-1,menu_click,0);
        }
    };
    const auto action_delay = [&](int id) -> unsigned int {
        const bool leaves=id==1 || id==6 || id==MA_PLAY || id==MA_HOST || id==MA_JOIN || id==MA_OPEN || id==MA_CREATE;
        return leaves && menu_click && sound_percent>0 ? 350 : 110;
    };
    const auto queue_at = [&](int x,int y) {
        const int id=action_hit(x,y);
        if(action_button.Queue(id,SDL_GetTicks())) {
            deferred_click=SDL_Event();
            deferred_click.type=SDL_MOUSEBUTTONUP;
            deferred_click.button.button=SDL_BUTTON_LEFT;
            deferred_click.button.x=x; deferred_click.button.y=y;
            play_click();
        }
    };
    while(running)
    {
        SDL_Event event;
        bool dispatch=action_button.Ready(SDL_GetTicks(),action_delay(action_button.pending));
        while(dispatch || SDL_PollEvent(&event))
        {
            const bool committed=dispatch;
            if(committed) {
                event=deferred_click;
                dispatch=false;
                const int id=action_button.pending;
                action_button.Reset();
                if(action_hit(event.button.x,event.button.y)!=id) continue;
            }
            // Any mouse click takes over from keyboard-only navigation focus.
            if(!committed && event.type == SDL_MOUSEBUTTONDOWN &&
               event.button.button == SDL_BUTTON_LEFT)
                keyboard_navigation = false;
            if(event.type == SDL_QUIT)
            {
                running = false;
                result = ZSTART_QUIT;
            }
            else if(!running || action_button.pending) continue;
            else if(event.type == SDL_ACTIVEEVENT && !event.active.gain &&
                    (event.active.state & (SDL_APPINPUTFOCUS | SDL_APPMOUSEFOCUS)))
                action_button.CancelHold();
            else if(!committed && event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    !dragging_sound && !dragging_music && !dragging_game_speed && !replay_drag_scroll &&
                    action_hit(event.button.x,event.button.y))
            {
                action_button.Press(action_hit(event.button.x,event.button.y));
                name_editing = false;
            }
            else if(!committed && event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && action_button.held)
            {
                const int before=action_button.held;
                action_button.CancelHold();
                if(before==action_hit(event.button.x,event.button.y)) queue_at(event.button.x,event.button.y);
            }
            else if(!committed && event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
                    !dragging_sound && !dragging_music && !dragging_game_speed && !replay_drag_scroll &&
                    action_hit(event.button.x,event.button.y)) continue;
            else if(event.type == SDL_MOUSEMOTION)
            {
                keyboard_navigation = false;
                if(replay_drag_scroll && (list_panel_mode != 0) && !editor_new_map)
                {
                    replay_scroll = ZodReplayScrollFromY(event.motion.y, (int)replay_maps.size());
                    ZodReplaySelectVisible(replay_selected, replay_scroll, (int)replay_maps.size());
                    continue;
                }
                if(dragging_sound)
                {
                    sound_percent = ZodVolumePercentAt(event.motion.x);
                    continue;
                }
                if(dragging_music)
                {
                    music_percent = ZodVolumePercentAt(event.motion.x);
                    continue;
                }
                if(dragging_game_speed)
                {
                    game_speed_preset = ZodGameSpeedPresetAt(event.motion.x);
                    continue;
                }
                // Mouse hover is drawn live from SDL_GetMouseState below.
                // Do not save it into 'selected', or an old hover will stay
                // blue even after the pointer has left that menu item.
            }

            else if(event.type == SDL_MOUSEBUTTONDOWN &&
                    (event.button.button == SDL_BUTTON_WHEELUP ||
                     event.button.button == SDL_BUTTON_WHEELDOWN))
            {
                if((list_panel_mode != 0) && !editor_new_map &&
                   ZodReplayPanelAt(event.button.x, event.button.y))
                {
                    int delta = event.button.button == SDL_BUTTON_WHEELUP ? -1 : 1;
                    replay_scroll = ZodReplayClampScroll(replay_scroll + delta, (int)replay_maps.size());
                    ZodReplaySelectVisible(replay_selected, replay_scroll, (int)replay_maps.size());
                    continue;
                }
            }
            else if(event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT)
            {
                if((list_panel_mode != 0))
                {
                    if(!editor_new_map && ZodReplayScrollAt(event.button.x, event.button.y))
                    {
                        name_editing = false;
                        replay_list_focus = true;
                        replay_drag_scroll = true;
                        replay_scroll = ZodReplayScrollFromY(event.button.y, (int)replay_maps.size());
                        ZodReplaySelectVisible(replay_selected, replay_scroll, (int)replay_maps.size());
                        continue;
                    }
                    // Settings are hidden when either inline list is open.
                    if(ZodReplayPanelAt(event.button.x, event.button.y))
                        continue;
                }
                if(list_panel_mode != 0) continue;
                if(ZodVolumeHit(event.button.x, event.button.y, false))
                {
                    name_editing = false;
                    dragging_sound = true;
                    sound_percent = ZodVolumePercentAt(event.button.x);
                    continue;
                }
                if(ZodVolumeHit(event.button.x, event.button.y, true))
                {
                    name_editing = false;
                    dragging_music = true;
                    music_percent = ZodVolumePercentAt(event.button.x);
                    continue;
                }
                if(ZodGameSpeedHit(event.button.x, event.button.y))
                {
                    name_editing = false;
                    dragging_game_speed = true;
                    game_speed_preset = ZodGameSpeedPresetAt(event.button.x);
                    continue;
                }
            }

            else if(event.type == SDL_MOUSEBUTTONUP &&
                    event.button.button == SDL_BUTTON_LEFT)
            {
                if(replay_drag_scroll)
                {
                    replay_drag_scroll = false;
                    replay_scroll = ZodReplayScrollFromY(event.button.y, (int)replay_maps.size());
                    ZodReplaySelectVisible(replay_selected, replay_scroll, (int)replay_maps.size());
                    continue;
                }
                if(dragging_sound)
                {
                    dragging_sound = false;
                    sound_percent = ZodVolumePercentAt(event.button.x);
                    volume_settings.sound = sound_percent;
                    volume_settings.music = music_percent;
                    ZodSaveVolumes(volume_settings);
                    continue;
                }
                if(dragging_music)
                {
                    dragging_music = false;
                    music_percent = ZodVolumePercentAt(event.button.x);
                    volume_settings.sound = sound_percent;
                    volume_settings.music = music_percent;
                    ZodSaveVolumes(volume_settings);
                    continue;
                }
                if(dragging_game_speed)
                {
                    dragging_game_speed = false;
                    game_speed_preset = ZodGameSpeedPresetAt(event.button.x);
                    ZodSaveGameSpeedPreset(game_speed_preset);
                    continue;
                }

                if(NameFieldAt(event.button.x, event.button.y))
                {
                    name_editing = true;
                    replay_list_focus = false;
                    editor_map_name_editing = false;
                    continue;
                }
                name_editing = false;

                if(list_panel_mode == 4 && editor_new_map &&
                   ZodReplayPanelAt(event.button.x, event.button.y))
                {
                    const int x=event.button.x, y=event.button.y;
                    if(ZodEditorHit(x,y,712,259,43,31))
                    {
                        editor_new_map = false;
                        editor_message.clear();
                        editor_map_name_editing = false;
                    }
                    else if(ZodEditorHit(x,y,723,320,318,37))
                    {
                        editor_map_name_editing = true;
                    }
                    else if(ZodEditorHit(x,y,ZOD_EDITOR_SELECTOR_X,367,
                                         ZOD_EDITOR_SELECTOR_W,ZOD_EDITOR_SELECTOR_H))
                    {
                        const int direction = ZodEditorSelectorDirection(x);
                        if(direction)
                            editor_size_index = (editor_size_index + 4 + direction) % 4;
                        editor_message.clear();
                        editor_map_name_editing = false;
                    }
                    else if(ZodEditorHit(x,y,ZOD_EDITOR_SELECTOR_X,407,
                                         ZOD_EDITOR_SELECTOR_W,ZOD_EDITOR_SELECTOR_H))
                    {
                        const int direction = ZodEditorSelectorDirection(x);
                        if(direction)
                            editor_terrain_index = (editor_terrain_index + MAX_PLANET_TYPES + direction)
                                                   % MAX_PLANET_TYPES;
                        editor_message.clear();
                        editor_map_name_editing = false;
                    }
                    else if(ZodEditorHit(x,y,ZOD_EDITOR_SELECTOR_X,447,
                                         ZOD_EDITOR_SELECTOR_W,ZOD_EDITOR_SELECTOR_H))
                    {
                        const int direction = ZodEditorSelectorDirection(x);
                        if(direction)
                            editor_save_index = (editor_save_index + 2 + direction) % 2;
                        editor_message.clear();
                        editor_map_name_editing = false;
                    }
                    else if(ZodEditorHit(x,y,796,510,178,39))
                    {
                        if(!ZodEditorValidNewName(editor_map_name))
                            editor_message = "Use letters, numbers, _ or -";
                        else
                        {
                            const std::string folder = editor_save_index ? "maps/multiplayer/" : "maps/other/";
                            const std::string path = folder + editor_map_name + ".map";
                            if(ZodEditorQueueLaunch(path,true,editor_map_name,
                                                    editor_size_index,editor_terrain_index,
                                                    editor_message))
                            {
                                ZStartMenuQueueNavigationFromLobby(4);
                                result = ZSTART_MAP_EDITOR;
                                running = false;
                            }
                        }
                    }
                    else editor_map_name_editing = false;
                    continue;
                }

                if((list_panel_mode != 0) && ZodReplayPanelAt(event.button.x, event.button.y))
                {
                    const int row = ZodReplayRowAt(event.button.x, event.button.y);
                    if(row >= 0)
                    {
                        int index = replay_scroll + row;
                        if(index < (int)replay_maps.size())
                        {
                            replay_selected = index;
                            replay_list_focus = true;
                            if(list_panel_mode == 3)
                                ZMultiplayerSelectMap(replay_maps[replay_selected]);
                        }
                    }
                    else if(list_panel_mode == 4 &&
                            event.button.y >= 510 && event.button.y < 549)
                    {
                        if(event.button.x >= 727 && event.button.x < 870)
                        {
                            editor_new_map = true;
                            editor_map_name_editing = true;
                            editor_map_name.clear();
                            editor_message.clear();
                            editor_size_index = 2;
                            editor_terrain_index = 0;
                            editor_save_index = 0;
                            replay_list_focus = false;
                        }
                        else if(event.button.x >= 886 && event.button.x < 1037 &&
                                replay_selected >= 0 && replay_selected < (int)replay_maps.size())
                        {
                            if(ZodEditorQueueLaunch(replay_maps[replay_selected],false,"",0,0,editor_message))
                            {
                                ZStartMenuQueueNavigationFromLobby(4);
                                result = ZSTART_MAP_EDITOR;
                                running = false;
                            }
                        }
                    }
                    else if(list_panel_mode == 3 &&
                            event.button.y >= 510 && event.button.y < 549)
                    {
                        if(event.button.x >= 727 && event.button.x < 870 &&
                           replay_selected >= 0 && replay_selected < (int)replay_maps.size() &&
                           ZMultiplayerSelectMap(replay_maps[replay_selected]))
                        {
                            result = ZSTART_MULTIPLAYER_HOST;
                            running = false;
                        }
                        else if(event.button.x >= 886 && event.button.x < 1037)
                        {
                            result = ZSTART_MULTIPLAYER_JOIN;
                            running = false;
                        }
                    }
                    else if(committed && (list_panel_mode==1 || list_panel_mode==2) &&
                            action_hit(event.button.x,event.button.y)==MA_PLAY)
                    {
                        const bool queued=list_panel_mode==1?ZStartReplayQueueMap(replay_maps[replay_selected]):ZSelectMapQueueMap(replay_maps[replay_selected]);
                        if(queued) {result=list_panel_mode==1?ZSTART_REPLAY:ZSTART_SELECT_MAP;running=false;}
                    }
                    continue;
                }

                int clicked = ButtonAt(
                    screen,
                    event.button.x,
                    event.button.y
                );

                if(clicked >= 0)
                {
                    selected = clicked;
                    keyboard_navigation = false;
                    replay_list_focus = false;
                    if(clicked != 1 && clicked != 2 && clicked != 3 && clicked != 4) list_panel_mode = 0;
                    editor_new_map = false;
                    editor_map_name_editing = false;

                    if(clicked == 0)
                    {
                        result = ZSTART_GAME;
                        running = false;
                    }
                    else if(clicked == 2)
                    {
                        // Toggle Select Map inside the same right-side metal panel.
                        list_panel_mode = (list_panel_mode == 2) ? 0 : 2;
                        if(list_panel_mode == 2)
                        {
                            replay_maps = ZSelectMapPlayableMaps();
                            replay_selected = replay_maps.empty() ? -1 : 0;
                            replay_scroll = 0;
                        }
                    }
                    else if(clicked == 1)
                    {
                        list_panel_mode = (list_panel_mode == 1) ? 0 : 1;
                        if(list_panel_mode == 1)
                        {
                            replay_maps = ZStartReplayCompletedMaps();
                            replay_selected = replay_maps.empty() ? -1 : 0;
                            replay_scroll = 0;
                        }
                    }
                    else if(clicked == 3)
                    {
                        // Open/toggle Multiplayer in the same right-hand panel.
                        list_panel_mode = (list_panel_mode == 3) ? 0 : 3;
                        if(list_panel_mode == 3)
                        {
                            replay_maps = ZMultiplayerPlayableMaps();
                            replay_selected = replay_maps.empty() ? -1 : 0;
                            const std::string& last_map = ZMultiplayerSelectedMap();
                            for(size_t i=0; i<replay_maps.size(); ++i)
                                if(replay_maps[i] == last_map) replay_selected = (int)i;
                            replay_scroll = 0;
                            ZodReplayKeepVisible(replay_scroll, replay_selected, (int)replay_maps.size());
                        }
                    }
                    else if(clicked == 4)
                    {
                        list_panel_mode = (list_panel_mode == 4) ? 0 : 4;
                        if(list_panel_mode == 4)
                        {
                            replay_maps = ZodEditorEditableMaps();
                            replay_selected = replay_maps.empty() ? -1 : 0;
                            replay_scroll = 0;
                            editor_message.clear();
                        }
                    }
                    else if(clicked == 5)
                    {
                        result = ZSTART_QUIT;
                        running = false;
                    }
                    else
                    {
                        status = "This feature will be connected next";
                    }
                }
            }

            else if(event.type == SDL_KEYDOWN)
            {
                SDLKey key = event.key.keysym.sym;

                if(name_editing)
                {
                    if(key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                       key == SDLK_ESCAPE || key == SDLK_TAB)
                    {
                        name_editing = false;
                    }
                    else if(key == SDLK_BACKSPACE)
                    {
                        if(!zod_player_name.empty())
                        {
                            RemoveLastUTF8Character(zod_player_name);
                            SaveZodPlayerName();
                        }
                    }
                    else
                    {
                        std::string character = UTF8FromSDL(event.key.keysym.unicode);
                        if(!character.empty())
                        {
                            std::string candidate = zod_player_name + character;
                            int width = 0, height = 0;
                            if(candidate.size() <= 80 &&
                               TTF_SizeUTF8(name_font, candidate.c_str(),
                                            &width, &height) == 0 &&
                               width <= 188)
                            {
                                zod_player_name = candidate;
                                SaveZodPlayerName();
                            }
                        }
                    }
                    continue; // Do not trigger menu shortcuts while typing.
                }

                if(list_panel_mode == 4 && editor_new_map)
                {
                    if(key == SDLK_ESCAPE) queue_at(730,275);
                    else if(key == SDLK_BACKSPACE)
                    {
                        if(!editor_map_name.empty()) editor_map_name.erase(editor_map_name.size()-1);
                        editor_message.clear();
                    }
                    else if(key == SDLK_RETURN || key == SDLK_KP_ENTER) queue_at(800,520);
                    else if(key == SDLK_TAB) editor_map_name_editing = !editor_map_name_editing;
                    else if(editor_map_name_editing && editor_map_name.size() < 32)
                    {
                        Uint16 ch=event.key.keysym.unicode;
                        if(ch<128 && (isalnum(static_cast<unsigned char>(ch)) || ch=='_' || ch=='-'))
                        {
                            editor_map_name += static_cast<char>(ch);
                            editor_message.clear();
                        }
                    }
                    continue;
                }

                if((list_panel_mode != 0) && replay_list_focus &&
                   (key == SDLK_UP || key == SDLK_DOWN ||
                    key == SDLK_PAGEUP || key == SDLK_PAGEDOWN))
                {
                    if(!replay_maps.empty())
                    {
                        if(key == SDLK_UP) --replay_selected;
                        if(key == SDLK_DOWN) ++replay_selected;
                        if(key == SDLK_PAGEUP) replay_selected -= ZOD_REPLAY_ROWS;
                        if(key == SDLK_PAGEDOWN) replay_selected += ZOD_REPLAY_ROWS;
                        if(replay_selected < 0) replay_selected = 0;
                        if(replay_selected >= (int)replay_maps.size())
                            replay_selected = (int)replay_maps.size() - 1;
                        ZodReplayKeepVisible(replay_scroll, replay_selected, (int)replay_maps.size());
                        if(list_panel_mode == 3)
                            ZMultiplayerSelectMap(replay_maps[replay_selected]);
                    }
                    continue;
                }
                if(list_panel_mode == 3 && !name_editing)
                {
                    if(key == SDLK_h && action_hit(730,520)==MA_HOST) {queue_at(730,520);continue;}
                    if(key == SDLK_j) {queue_at(890,520);continue;}
                }
                if(key == SDLK_DOWN)
                {
                    selected = (selected + 1) % MENU_COUNT;
                    keyboard_navigation = true;
                }
                else if(key == SDLK_UP)
                {
                    selected = (selected + MENU_COUNT - 1) % MENU_COUNT;
                    keyboard_navigation = true;
                }

                else if(key == SDLK_RETURN ||
                        key == SDLK_KP_ENTER)
                {
                    if((list_panel_mode != 0) && replay_list_focus &&
                       replay_selected >= 0 && replay_selected < (int)replay_maps.size())
                    {
                        if(list_panel_mode == 3)
                        {
                            ZMultiplayerSelectMap(replay_maps[replay_selected]);
                        }
                        else if(list_panel_mode == 4)
                        {
                            queue_at(890,520);
                        }
                        else
                        {
                            queue_at(800,520);
                        }
                        continue;
                    }
                    const SDL_Rect r=GetButtonRect(screen,selected);
                    queue_at(r.x+r.w/2,r.y+r.h/2);
                    keyboard_navigation = false;
                }

                else if(key == SDLK_ESCAPE)
                {
                    if((list_panel_mode != 0))
                    {
                        list_panel_mode = 0;
                        replay_list_focus = false;
                    }
                    else
                    {
                        result = ZSTART_QUIT;
                        running = false;
                    }
                }
            }
        }

        if(!running) break;
        int mx=0,my=0; SDL_GetMouseState(&mx,&my);
        const MenuFeedback feedback={action_button,action_hit(mx,my)};
        if(menu_music) Mix_VolumeMusic((80 * music_percent + 50)/100);

        if(menu_background)
        {
            // ZOD_DYNAMIC_MENU_TEXT_BEGIN
// Settings use the original image. Lists use the CLEAN metal-panel image.
// Both are rendered into the same SDL display, so switching never flashes
// Windows or alters any multiplayer/navigation state.
SDL_Surface* active_background =
    (list_panel_mode != 0) ? clean_background : menu_background;
SDL_BlitSurface(active_background, NULL, screen, NULL);
if(list_panel_mode != 0)
    ZodRestoreNameArtwork(screen, menu_background);

// Quit has hover/keyboard focus only, not a persistent submenu state.
if(((keyboard_navigation && selected == 5) || feedback.State(6)) && quit_hover)
{
    SDL_Rect quit_pos;
    quit_pos.x = 310;
    quit_pos.y = 506;
    quit_pos.w = 0;
    quit_pos.h = 0;
    SDL_BlitSurface(quit_hover, NULL, screen, &quit_pos);
    if(feedback.State(6)==2) {
        Fill(screen,330,528,205,2,13,55,92);
        Fill(screen,330,528,2,54,13,55,92);
    }
}

// Samme font, stoerrelse og placering for alle fem punkter.
static const int text_y[5] = {203, 252, 301, 350, 399};
const int text_x = 280;
for(int i = 0; i < 5; i++)
{
    const int state=feedback.State(i+1);
    const int dx=state==2?1:0, dy=state==2?2:0;
    DrawText(screen,button_font,menu_items[i],text_x+1+dx,text_y[i]+2+dy,5,7,12);
    // A clicked submenu stays blue; all other items are blue only while
    // hovering/pressing them (or navigating with the keyboard arrows).
    const bool active_tab = (list_panel_mode == i && i >= 1 && i <= 4);
    if(active_tab || (keyboard_navigation && selected == i) || state) {
        DrawText(screen,button_font,menu_items[i],text_x-1+dx,text_y[i]+dy,0,65,145);
        DrawText(screen,button_font,menu_items[i],text_x+1+dx,text_y[i]+dy,0,65,145);
        DrawText(screen,button_font,menu_items[i],text_x+dx,text_y[i]+dy,state==1?110:65,state==1?225:195,255);
    } else DrawText(screen,button_font,menu_items[i],text_x,text_y[i],215,216,222);
}
// ZOD_DYNAMIC_MENU_TEXT_END
            DrawZodPlayerName(screen, name_font, name_editing);
            if((list_panel_mode != 0))
            {
                if(list_panel_mode == 4 && editor_new_map)
                    DrawZodNewMapPanel(screen,name_font,small_font,hint_font,
                                       editor_map_name,editor_map_name_editing,
                                       editor_size_index,editor_terrain_index,
                                       editor_save_index,editor_message,feedback);
                else
                    DrawZodReplayPanel(screen, name_font, small_font, hint_font,
                                       replay_maps, replay_selected, replay_scroll,
                                       list_panel_mode,
                                       editor_message, feedback);
            }
            else
            {
            DrawZodVolumeSlider(screen, 354, sound_percent);
            DrawZodVolumeSlider(screen, 410, music_percent);
            DrawZodGameSpeedSlider(screen, game_speed_preset);
            // ZOD_GAME_SPEED_PERCENT_DISPLAY_BEGIN
            // Display the selected value in the empty metal box below the slider.
            // Updated every frame, including while the handle is being dragged.
            char speed_percent_text[16];
            snprintf(speed_percent_text, sizeof(speed_percent_text), "%d%%",
                     ZodGameSpeedPercent(game_speed_preset));
            int speed_percent_width = 0;
            int speed_percent_height = 0;
            if(TTF_SizeUTF8(name_font, speed_percent_text,
                            &speed_percent_width, &speed_percent_height) == 0)
            {
                // Right box now matches the left box: x=884..1030, center=957.
                const int speed_percent_x = 969 - speed_percent_width / 2; // Right readout shifted 12 px with artwork.
                const int speed_percent_y = 528 - speed_percent_height / 2;
                DrawText(screen, name_font, speed_percent_text,
                         speed_percent_x + 1, speed_percent_y + 1, 3, 12, 22);
                DrawText(screen, name_font, speed_percent_text,
                         speed_percent_x, speed_percent_y, 90, 211, 255);
            }
            // ZOD_GAME_SPEED_PERCENT_DISPLAY_END
            } // End settings / shared Replay, Select Map and Multiplayer panels.
            SDL_Flip(screen);
            SDL_Delay(16);
            continue;
        }
        // Temporary graphics while original Z design is prepared
        SDL_FillRect(
            screen,
            NULL,
            SDL_MapRGB(screen->format, 49, 44, 39)
        );

        // Main metal frame
        Fill(screen, 30, 25,
             screen->w - 60, screen->h - 50,
             103, 94, 81);

        Fill(screen, 36, 31,
             screen->w - 72, screen->h - 62,
             58, 53, 47);

        DrawText(
            screen, title_font,
            "ZOD",
            screen->w / 2 - 80, 65,
            214, 192, 148
        );

        // Menu buttons
        for(int i = 0; i < MENU_COUNT; i++)
        {
            SDL_Rect r = GetButtonRect(screen, i);

            int visual=feedback.State(i+1);
            bool active = ((list_panel_mode == i && i >= 1 && i <= 4) ||
                           (keyboard_navigation && i == selected) || visual);

            Fill(screen, r.x - 3, r.y - 3,
                 r.w + 6, r.h + 6,
                 125, 113, 94);

            Fill(screen, r.x, r.y, r.w, r.h,
                 active ? 108 : 75,
                 active ? 97 : 69,
                 active ? 77 : 58);

            DrawText(
                screen,
                button_font,
                menu_items[i],
                r.x + 20 + (visual==2),
                r.y + (r.h - TTF_FontHeight(button_font)) / 2 + (visual==2?2:0),
                active ? 255 : 210,
                active ? 231 : 193,
                active ? 178 : 150
            );
        }

        DrawText(
            screen, small_font,
            status,
            90, screen->h - 85,
            190, 180, 160
        );

        DrawZodPlayerName(screen, name_font, name_editing);
        SDL_Flip(screen);
        SDL_Delay(16);
    }

    if(dragging_sound || dragging_music)
    {
        volume_settings.sound = sound_percent;
        volume_settings.music = music_percent;
        ZodSaveVolumes(volume_settings);
    }
    if(menu_audio)
    {
        Mix_HaltMusic();
        if(menu_music) Mix_FreeMusic(menu_music);
        Mix_HaltChannel(-1);
        if(menu_click) Mix_FreeChunk(menu_click);
        Mix_CloseAudio();
    }
    SDL_EnableUNICODE(0);
    if(quit_hover) SDL_FreeSurface(quit_hover);
    if(menu_background) SDL_FreeSurface(menu_background);
    if(clean_background) SDL_FreeSurface(clean_background);
    TTF_CloseFont(title_font);
    TTF_CloseFont(button_font);
    TTF_CloseFont(small_font);
    TTF_CloseFont(name_font);
    if(hint_font && hint_font != small_font) TTF_CloseFont(hint_font);

    TTF_Quit();
    // The editor now runs inside this process and reuses the menu's existing
    // fullscreen SDL surface. Do not SDL_Quit here: that causes the mode flash.
    if(result == ZSTART_QUIT)
        SDL_Quit();

    return result;
}