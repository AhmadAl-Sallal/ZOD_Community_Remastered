#ifndef _ZGUIMAINMENUBASE_H_
#define _ZGUIMAINMENUBASE_H_

#include "constants.h"
#include "zsdl.h"
#include "zsdl_opengl.h"
#include "zmap.h"
#include "common.h"
#include "zbuildlist.h"
#include "ztime.h"
#include "zfont_engine.h"
#include "zobject.h"
#include "zcore.h"
#include "ztime.h"

#include "zgui_main_menu_widgets.h"

#define GMM_SIDE_MARGIN 5
#define GMM_TOP_MARGIN 5
#define GMM_BOTTOM_MARGIN 5
#define GMM_TITLE_HEIGHT 18
#define GMM_TITLE_MARGIN (GMM_TITLE_HEIGHT + GMM_TOP_MARGIN)

enum zod_ingame_mode
{
    ZOD_MENU_CAMPAIGN,
    ZOD_MENU_SKIRMISH,
    ZOD_MENU_MULTIPLAYER
};

enum menu_type
{
	GMM_MAIN_MAIN, GMM_CHANGE_TEAMS, GMM_MANAGE_BOTS, 
	GMM_ALLIANCES,
	GMM_PLAYER_LIST, GMM_SELECT_MAP, GMM_OPTIONS, GMM_WARNING,
	GMM_MULTIPLAYER,
	MAX_MENU_TYPES
};

enum gmm_event_type
{
	GMM_UNKNOWN_EVENT, GMM_CLICK_EVENT, GMM_UNCLICK_EVENT, 
	GMM_MOTION_EVENT, GMM_KEYPRESS_EVENT, GMM_WHEELUP_EVENT,
	GMM_WHEELDOWN_EVENT, MAC_GMM_EVENTS
};

const string gmm_event_type_string[MAC_GMM_EVENTS] =
{
	"unknown", "click", "unclick", "motion", "keypress",
	"wheelup", "wheeldown"
};

class gmm_warning_flag
{
public:
	gmm_warning_flag() { clear(); }

	void clear()
	{
		text1.clear();
		text2.clear();
		quit_game = false;
		reset_map = false;
	}

	string text1;
	string text2;
	bool quit_game;
	bool reset_map;
};

class gmm_flag
{
public:
	gmm_flag() { clear(); }

	void clear()
	{
		open_main_menu = false;
		open_main_menu_type = -1;
		reshuffle_teams = false;
		change_team = false;
		change_team_type = -1;
		start_bot = false;
		stop_bot = false;
		start_bot_team = -1;
		stop_bot_team = -1;
		set_alliance = false;
		set_alliance_team = -1;
		set_alliance_value = false;
		set_all_bots_allied = false;
		set_all_bots_allied_value = false;
		change_map = false;
		change_map_number = -1;
		reset_map = false;
		quit_game = false;
		set_volume = false;
		set_volume_value = -1;
		set_music_volume = false;
		set_music_volume_value = -1;
		pause_game = false;
		set_game_speed = false;
		set_game_speed_value = 1.0;

		warning_flags.clear();
	}

	bool open_main_menu;
	int open_main_menu_type;
	bool reshuffle_teams;
	bool change_team;
	int change_team_type;
	bool start_bot;
	int start_bot_team;
	bool stop_bot;
	int stop_bot_team;
	bool set_alliance;
	int set_alliance_team;
	bool set_alliance_value;
	bool set_all_bots_allied;
	bool set_all_bots_allied_value;
	bool change_map;
	int change_map_number;
	bool reset_map;
	bool quit_game;
	bool set_volume;
	int set_volume_value;
	bool set_music_volume;
	int set_music_volume_value;
	bool pause_game;
	bool set_game_speed;
	float set_game_speed_value;

	gmm_warning_flag warning_flags;
};

class ZGuiMainMenuBase
{
public:
	ZGuiMainMenuBase();
	virtual ~ZGuiMainMenuBase() {}

	static void Init();

	virtual void Process();
	virtual void DoRender(ZMap &the_map, SDL_Surface *dest);
	// Display reference-derived artwork at native pixel size without double scaling.
	virtual bool RenderAtNativePixels() const { return false; }

	virtual bool Click(int x_, int y_);
	virtual bool UnClick(int x_, int y_);
	virtual bool Motion(int x_, int y_);
	virtual bool KeyPress(int c);
	virtual bool WheelUpButton();
	virtual bool WheelDownButton();

	bool KillMe() { return killme; }
	int GetMenuType() { return menu_type; }
	void SetCenterCoords(int cx_, int cy_);
	bool WithinDimensions(int x_, int y_);
	gmm_flag &GetGMMFlags() { return gmm_flags; }
	void GetCoords(int &x_, int &y_) { x_ = x; y_ = y; }
	void GetDimensions(int &w_, int &h_) { w_ = w; h_ = h; }
	bool IsOverHUD(int hud_left, int hud_top);
	void SetPlayerInfoList(vector<p_info> *player_info_) { player_info = player_info_; }
	void SetAllianceInfo(bool (*requests)[MAX_TEAM_TYPES],
        bool (*alliances_)[MAX_TEAM_TYPES],
        bool *all_bots_allied_)
	{
    	alliance_requests = requests;
    	alliances = alliances_;
    	all_bots_allied = all_bots_allied_;
	}
	void SetSelectableMapList(vector<string> *selectable_map_list_) { selectable_map_list = selectable_map_list_; }
	void SetPlayerTeam(int *player_team_) { player_team=player_team_; }
	void SetSoundSetting(int *sound_setting_) { sound_setting = sound_setting_; }
	void SetMusicSetting(int *music_setting_) { music_setting = music_setting_; }
	void SetZTime(ZTime *ztime_) { ztime=ztime_; }
	void Move(double px, double py);
	void SetWarningFlags(gmm_warning_flag warning_flags_) { warning_flags=warning_flags_; }

protected:
	static bool finished_init;

	void UpdateDimensions();
	void ProcessWidgets();

	void MakeTitle();
	void AddWidget(ZGMMWidget *new_widget) { widget_list.push_back(new_widget); }

	void RenderBase(ZMap &the_map, SDL_Surface *dest, int tx, int ty);
	void RenderWidgets(ZMap &the_map, SDL_Surface *dest);

	virtual void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);

	bool killme;
	int menu_type;
	int x, y, w, h;
	string title;
	ZSDL_Surface title_img;
	gmm_flag gmm_flags;
	vector<p_info> *player_info;
	bool (*alliance_requests)[MAX_TEAM_TYPES];
	bool (*alliances)[MAX_TEAM_TYPES];
	bool *all_bots_allied;
	vector<string> *selectable_map_list;
	int *player_team;
	int *sound_setting;
	int *music_setting;
	ZTime *ztime;
	gmm_warning_flag warning_flags;

	bool click_grabbed;
	int grab_x, grab_y;

	vector<ZGMMWidget*> widget_list;

	GMMWButton close_button;
};

#endif
