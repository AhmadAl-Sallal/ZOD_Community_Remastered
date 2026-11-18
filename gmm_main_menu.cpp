#include "gmm_main_menu.h"
#include <stdio.h>

GMMMainMenu::GMMMainMenu(int play_mode, bool is_host) : ZGuiMainMenuBase()
{
    menu_type = GMM_MAIN_MAIN;
    title = "Menu";
    // The facit panel has no separate close glyph; use Pause/Return buttons.
    close_button.SetActive(false);
    // Draw actual reference-derived PNGs in native screen pixels, no double scaling.
    // Source metal, rivets and black/yellow hazard stripes are preserved verbatim.
    const char *variant = "skirmish";
    if(play_mode == ZOD_MENU_CAMPAIGN) variant = "campaign";
    else if(play_mode == ZOD_MENU_MULTIPLAYER)
        variant = is_host ? "multi_host" : "multi_guest";

    w = 354;
    h = 366;
    visible_count = 0;
    hovered_row = -1;

    char file[180];
    snprintf(file, sizeof(file), "assets/other/zod_facit_ui/menu_%s.png", variant);
    facit_art.LoadBaseImage(file);
    if(!facit_art.GetBaseSurface())
        printf("ZOD FACIT UI: missing menu artwork %s\n", file);

    const char* labels[MAX_GMMMM_BUTTONS] = {
        "Pause Game", "Change Team", "Manage Bots", "Alliances",
        "Player List", "Reset Map", "Return to Main Menu"
    };

    int visible_buttons[MAX_GMMMM_BUTTONS];
    int count = 0;

    visible_buttons[count++] = GMMMM_PAUSE_GAME_BUTTON;

    if(play_mode == ZOD_MENU_SKIRMISH)
        visible_buttons[count++] = GMMMM_CHANGE_TEAMS_BUTTON;

    if(play_mode != ZOD_MENU_CAMPAIGN)
    {
        visible_buttons[count++] = GMMMM_MANAGE_BOTS_BUTTON;
        visible_buttons[count++] = GMMMM_ALLIANCES_BUTTON;
    }

    if(play_mode == ZOD_MENU_MULTIPLAYER)
        visible_buttons[count++] = GMMMM_PLAYER_LIST_BUTTON;

    // Guests cannot reset a shared multiplayer battle.
    if(play_mode != ZOD_MENU_MULTIPLAYER || is_host)
        visible_buttons[count++] = GMMMM_RESET_MAP_BUTTON;

    visible_buttons[count++] = GMMMM_QUIT_GAME_BUTTON;

    // Native-resolution artwork: these coordinates are shared with image generation.
    const bool compact = (play_mode == ZOD_MENU_CAMPAIGN);
    const int row_y_campaign[3] = {160, 207, 254};
    const int row_y_standard[6] = {116, 153, 190, 227, 264, 301};
    visible_count = count;
    for(int i=0; i<count; ++i)
    {
        const int b = visible_buttons[i];
        visible_button_ids[i] = b;
        // Transparent clickable rectangles precisely match engraved button positions.
        // DoRender does NOT paint old low-resolution font/buttons on top of the art.
        menu_button[b].SetType(MMGENERIC_BUTTON);
        menu_button[b].SetText(labels[b]);
        menu_button[b].SetCoords(30, compact ? row_y_campaign[i] : row_y_standard[i]);
        menu_button[b].SetDimensions(w - 60, 31);
        AddWidget(&menu_button[b]);
        snprintf(file, sizeof(file), "assets/other/zod_facit_ui/menu_%s_hover%d.png",variant,i);
        hover_art[i].LoadBaseImage(file);
    }

    UpdateDimensions();
}

void GMMMainMenu::HandleWidgetEvent(int event_type, ZGMMWidget *event_widget)
{
    if(!event_widget || event_type != GMM_UNCLICK_EVENT) return;
    const int id = event_widget->GetRefID();

    if(id == menu_button[GMMMM_PAUSE_GAME_BUTTON].GetRefID())
    {
        // Hide the menu so the existing centered Pause/Resume overlay
        // cannot be covered by it, including when the map is already paused.
        gmm_flags.pause_game = true;
        killme = true;
    }
    else if(id == menu_button[GMMMM_CHANGE_TEAMS_BUTTON].GetRefID())
    {
        gmm_flags.open_main_menu = true;
        gmm_flags.open_main_menu_type = GMM_CHANGE_TEAMS;
    }
    else if(id == menu_button[GMMMM_MANAGE_BOTS_BUTTON].GetRefID())
    {
        gmm_flags.open_main_menu = true;
        gmm_flags.open_main_menu_type = GMM_MANAGE_BOTS;
    }
    else if(id == menu_button[GMMMM_ALLIANCES_BUTTON].GetRefID())
    {
        gmm_flags.open_main_menu = true;
        gmm_flags.open_main_menu_type = GMM_ALLIANCES;
    }
    else if(id == menu_button[GMMMM_PLAYER_LIST_BUTTON].GetRefID())
    {
        gmm_flags.open_main_menu = true;
        gmm_flags.open_main_menu_type = GMM_PLAYER_LIST;
    }
    else if(id == menu_button[GMMMM_RESET_MAP_BUTTON].GetRefID())
    {
        gmm_flags.open_main_menu = true;
        gmm_flags.open_main_menu_type = GMM_WARNING;
        gmm_flags.warning_flags.text1 = "Restart this map?";
        gmm_flags.warning_flags.text2 = "Progress will be lost.";
        gmm_flags.warning_flags.reset_map = true;
    }
    else if(id == menu_button[GMMMM_QUIT_GAME_BUTTON].GetRefID())
    {
        gmm_flags.open_main_menu = true;
        gmm_flags.open_main_menu_type = GMM_WARNING;
        gmm_flags.warning_flags.text1 = "Leave this battle and";
        gmm_flags.warning_flags.text2 = "return to the main menu?";
        gmm_flags.warning_flags.quit_game = true;
    }
}

void GMMMainMenu::DoRender(ZMap &the_map, SDL_Surface *dest)
{
    if(hovered_row >= 0 && hovered_row < visible_count &&
       hover_art[hovered_row].GetBaseSurface())
        hover_art[hovered_row].BlitSurface(NULL, x, y);
    else
        facit_art.BlitSurface(NULL, x, y);
}

bool GMMMainMenu::Motion(int mouse_x, int mouse_y)
{
    // Hover feedback is burned into native-res metallic PNGs, never drawn with pixel font.
    hovered_row = -1;
    const int tx = mouse_x - x;
    const int ty = mouse_y - y;
    for(int i=0; i<visible_count; ++i)
    {
        if(menu_button[visible_button_ids[i]].WithinDimensions(tx, ty))
        {
            hovered_row = i;
            break;
        }
    }
    // Keep legacy dragging behavior and menu consumption working.
    return ZGuiMainMenuBase::Motion(mouse_x, mouse_y) || WithinDimensions(mouse_x, mouse_y);
}
