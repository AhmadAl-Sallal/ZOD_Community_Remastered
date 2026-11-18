#include <stdio.h>

#ifdef _WIN32

//xgetopt stuff
//this is only needed for some compilers
#include "xgetopt.h"
int optind, opterr;
TCHAR *optarg;
//end xgetopt

#else
#include <unistd.h>
#endif


#include "main.h"
#include "common.h"
#include "constants.h"
#include "zsdl.h"
#include "zplayer.h"
#include "zbot.h"
#include "zserver.h"
#include "zsound_engine.h"
#include "zmusic_engine.h"
#include "ztray.h"
#include "zstart_menu.h"
#include "zod_game_speed_settings.h"
#include "zstart_replay.h"
#include "zstart_multiplayer.h"
#include "zlan_lobby.h"
#include "zstart_editor.h"
#include "zod_editor_embedded.h"
#include "zstart_select.h"
#include <stdlib.h>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void display_help(char *shell_command);
void display_version();
int run_server_thread(void *nothing);
int run_bot_thread(void *nothing);
void run_player_thread();
void run_tray_app();

input_options starting_conditions;
static bool start_campaign_from_menu = false;
static bool start_replay_from_menu = false;
static bool start_multiplayer_host_from_menu = false;
static bool start_multiplayer_join_from_menu = false;
static std::string replay_map_path;
static std::atomic<int> local_server_startup(0); // 0 pending, 1 ready, -1 failed

char bot_bypass_data[MAX_BOT_BYPASS_SIZE];
int bot_bypass_size;

// All worker threads must be stopped before shared audio resources are released.
static void ReleaseMatchAudio()
{
    ZMusicEngine::Shutdown();
    ZSoundEngine::Shutdown();
    Mix_CloseAudio();
}

// The menu builds a quoted command using a validated, local map path.
// Interpret its arguments directly instead of spawning an external process.
// Quotes preserve filenames containing spaces (e.g. old custom maps).
static std::vector<std::string> ZodSplitEditorCommand(const char* command)
{
    std::vector<std::string> words;
    if(!command) return words;
    const char* p = command;
    while(*p)
    {
        while(*p && (*p == ' ' || *p == '\t')) ++p;
        if(!*p) break;
        bool quoted = false;
        std::string word;
        for(; *p; ++p)
        {
            if(*p == '"') { quoted = !quoted; continue; }
            if(!quoted && (*p == ' ' || *p == '\t')) break;
            word.push_back(*p);
        }
        if(quoted || word.empty()) return std::vector<std::string>();
        words.push_back(word);
    }
    return words;
}

#undef main
int main(int argc, char **argv)
{
	SDL_Thread *server_thread;
	
	printf("Welcome to the Zod Engine\n");

	if(argc<=1) starting_conditions.setdefaults();
	
	//read in the arguments
	starting_conditions.getoptions(argc, argv);
	
	//make sure there is nothing conflicting, 
	//like we are trying to make a dedicated server that is supposed to connect to another server
	starting_conditions.checkoptions();
	
	//init this for the bots
	ZCore::CreateRandomBotBypassData(bot_bypass_data, bot_bypass_size);

	//now see what we have
	if(starting_conditions.read_display_version) display_version();
	if(starting_conditions.read_display_help) display_help(argv[0]);
	
	//now what do we really run?
	else if(starting_conditions.read_run_tray)
	{
		run_tray_app();
	}
	else if(starting_conditions.read_is_dedicated)
	{
		//run only a server
		//server_thread = SDL_CreateThread(run_server_thread, NULL);
		run_server_thread(NULL);
	}
	else if(starting_conditions.read_connect_address)
	{
		//connect to a server
		run_player_thread();
		ReleaseMatchAudio();
	}
	else
	{
		ZOD_LAN_GAME_MENU:
		while(true)
                {
                    int menu_result = RunZStartMenu();
                    // The editable menu name is the single source of truth.
                    // Empty means the user has not chosen a name yet.
                    starting_conditions.player_name = ZStartMenuPlayerName();
                    starting_conditions.read_player_name =
                        !starting_conditions.player_name.empty();
                // Replay Level: select from completed campaign maps only.
                if(menu_result == ZSTART_REPLAY)
                {
                    std::string picked_map;
                    if(RunZStartReplay(picked_map))
                    {
                        replay_map_path = picked_map;
                        start_replay_from_menu = true;
                        start_campaign_from_menu = false;
                        break; // Launch the selected replay mission.
                    }
                    continue; // Back to the main menu.
                }

                    if(menu_result == ZSTART_MULTIPLAYER_HOST || menu_result == ZSTART_MULTIPLAYER_JOIN)
                    {
                        const bool lan_host = (menu_result == ZSTART_MULTIPLAYER_HOST);
                        std::string lan_name = starting_conditions.read_player_name
                            ? std::string(starting_conditions.player_name)
                            : std::string(lan_host ? "Host" : "Player");
                        ZLanLobbyResult lan_choice;
                        if(!RunZLanLobby(lan_host, ZMultiplayerSelectedMap(), lan_name, lan_choice))
                            continue; // Cancelled lobby: return to main menu.
                        start_multiplayer_host_from_menu = lan_host;
                        start_multiplayer_join_from_menu = !lan_host;
                        start_campaign_from_menu = false;
                        start_replay_from_menu = false;
                        for(int team=0; team<MAX_TEAM_TYPES; ++team)
                            starting_conditions.read_start_bot[team] = false;
                        starting_conditions.read_player_team = true;
                        starting_conditions.team = lan_choice.team;
                        starting_conditions.read_player_name = true;
                        starting_conditions.player_name = lan_name;
                        starting_conditions.read_connect_address = !lan_host;
                        if(!lan_host)
                            starting_conditions.connect_address = lan_choice.host_ip;
                        break;
                    }
                    if(menu_result == ZSTART_QUIT)
                        return 0;

                    if(menu_result == ZSTART_MAP_EDITOR)
                    {
                        const char* command = ZStartMenuEditorCommand();

                        if(command && command[0])
                        {
                            printf("Starting Map Editor: %s\n", command);
                            const char* tempDir = getenv("TEMP");

                            char logPath[2048];

                            snprintf(
                                logPath,
                                sizeof(logPath),
                                "%s\\ZOD_Editor_Log.txt",
                                tempDir ? tempDir : "."
                            );

                            FILE* log = fopen(logPath, "w");

                            if(log)
                            {
                                fprintf(log, "COMMAND: [%s]\n", command);
                                fflush(log);
                            }

                            // No child process: hand the live 1280x720 fullscreen
                            // SDL surface directly to the embedded editor.
                            // This avoids SDL_Quit/Windows mode restore/re-enter.
                            std::vector<std::string> editor_arguments =
                                ZodSplitEditorCommand(command);
                            int editor_status = -1;
                            if(editor_arguments.size() >= 3)
                            {
                                std::vector<char*> editor_argv;
                                for(size_t i = 0; i < editor_arguments.size(); ++i)
                                    editor_argv.push_back(&editor_arguments[i][0]);
                                editor_status = RunZodMapEditorEmbedded(
                                    (int)editor_argv.size(), &editor_argv[0]);
                            }
                            else
                            {
                                fprintf(stderr, "Map Editor: invalid launch command\n");
                            }

                            if(log)
                            {
                                fprintf(log, "RETURN CODE: %d\n", editor_status);
                                fclose(log);
                            }
                            printf("Map Editor returned: %d\n", editor_status);
                        }

                        continue;
                    }

                    // Start Game = campaign
                    start_campaign_from_menu = (menu_result == ZSTART_GAME);
                    break;
                }
                //run a server, then connect to it
if(start_multiplayer_join_from_menu)
                {
                    // Join existing server: do not start a second server locally.
                    SDL_Delay(3000); // LAN lobby: let host start TCP server first.
                    run_player_thread();
                    // Return from the guest game (quit or lost host connection).
                    ReleaseMatchAudio();
                    // Keep SDL video alive when returning to the main menu.
                    // RunZStartMenu() converts the OpenGL surface back to 2D.
                    // SDL_Quit() here would tear down fullscreen and cause flicker.
                    start_multiplayer_join_from_menu = false;
                    starting_conditions.read_connect_address = false;
                    starting_conditions.read_player_team = true;
                    starting_conditions.team = RED_TEAM;
                    goto ZOD_LAN_GAME_MENU;
                }
                else
                {
                    ZServer::ResetStopRequest();
                    local_server_startup.store(0);
                    server_thread = SDL_CreateThread(run_server_thread, NULL);
                    if(server_thread)
                    {
                        while(local_server_startup.load()==0) SDL_Delay(10);
                        if(local_server_startup.load()==1) run_player_thread();
                        else
                        {
#ifdef _WIN32
                            MessageBoxA(NULL,"Could not load the map or start the local server (port 8000).","ZOD - Startup Error", MB_OK|MB_ICONERROR);
#else
                            fprintf(stderr,"Could not load map or start local server.\n");
#endif
                        }
                        // Disconnect and stop bots/server BEFORE SDL is shut down.
                        ZServer::RequestStop();
                        SDL_WaitThread(server_thread, NULL);
                        ReleaseMatchAudio();
                        // Keep SDL video alive when returning to the main menu.
                        // RunZStartMenu() converts the OpenGL surface back to 2D.
                        // SDL_Quit() here would tear down fullscreen and cause flicker.
                        start_campaign_from_menu = false;
                        start_replay_from_menu = false;
                        start_multiplayer_host_from_menu = false;
                        starting_conditions.read_player_team = true;
                        starting_conditions.team = RED_TEAM;
                        goto ZOD_LAN_GAME_MENU;
                    }
                    else fprintf(stderr,"Could not start server thread: %s\n", SDL_GetError());
                }
	}

	return 1;
}

int run_server_thread(void *nothing)
{
	int i;
	vector<SDL_Thread*> bot_thread;
        ZServer zserver;
        zserver.SetMultiplayerMode(start_multiplayer_host_from_menu);
        if(start_multiplayer_host_from_menu)
            zserver.SetMultiplayerHostTeam(starting_conditions.team);
        // Campaign: BLUE bot is always enabled
        if(start_campaign_from_menu || start_replay_from_menu)
        {
            for(int team = 0; team < MAX_TEAM_TYPES; team++)
            {
                starting_conditions.read_start_bot[team] =
                    (team == BLUE_TEAM);
            }

            printf("Campaign: BLUE bot enabled\n");
        }

	for(i=0;i<MAX_TEAM_TYPES;i++)
		if(starting_conditions.read_start_bot[i])
		{
			zserver.InitBot(i);

			//int *team = new int;
			//printf("start bot for team %s\n", team_type_string[i].c_str());

			//*team = i;
			//bot_thread.push_back(SDL_CreateThread(run_bot_thread, (void*)team));
		}
	
        if(start_multiplayer_host_from_menu)
        {
            zserver.SetMapName(ZMultiplayerSelectedMap());
        }
        else if(start_replay_from_menu)
        {
            // Mister 007 V4: replay uses the same intro/results pipeline as
            // Start Game, but never changes the actual campaign save.
            zserver.SetCampaignMode(true);
            zserver.SetReplayMode(replay_map_path);
            zserver.SetMapList("map_list.txt");
        }
        else if(start_campaign_from_menu)
        {
                zserver.SetCampaignMode(true);
                zserver.SetMapList("map_list.txt");
        }
        else if(ZHasSelectedMap())
        {
                zserver.SetSkirmishResultsEnabled(true);
                zserver.SetMapName(ZSelectedMapPath());
        }
        else if(starting_conditions.read_map_name)
		zserver.SetMapName(starting_conditions.map_name);
	else if(starting_conditions.read_map_list)
		zserver.SetMapList(starting_conditions.map_list);

	if(starting_conditions.read_settings)
		zserver.SetSettingsFilename(starting_conditions.settings_filename);
	if(starting_conditions.read_p_settings)
		zserver.SetPerpetualSettingsFilename(starting_conditions.p_settings_filename);

	zserver.SetBotBypassData(bot_bypass_data, bot_bypass_size);

	zserver.Setup();
    if(!zserver.Ready()) { local_server_startup.store(-1); return 0; }
	// ZOD_INITIAL_GAME_SPEED: set the locally hosted match speed before play.
	// This includes campaign, replay, selected map and LAN host.
	// LAN guests do not run this local server and inherit the host speed.
	if(!starting_conditions.read_is_dedicated &&
	   !starting_conditions.read_connect_address)
	{
		const float saved_speed = ZodSavedGameSpeedMultiplier();
		if(saved_speed != 1.0f)
			zserver.ChangeGameSpeed(saved_speed);
	}
	local_server_startup.store(1);
	zserver.Run();

	return 1;
}

int run_bot_thread(void *nothing)
{
	int *team;
	ZBot zbot;

	team = (int*)nothing;
	
	zbot.SetDesiredTeam((team_type)*team);
	if(starting_conditions.read_connect_address)
		zbot.SetRemoteAddress(starting_conditions.connect_address);

	zbot.SetBotBypassData(bot_bypass_data, bot_bypass_size);

	zbot.Setup();
	zbot.Run();

	return 1;
}

void run_player_thread()
{
	ZPlayer zplayer;
        zplayer.SetLanMode(start_multiplayer_host_from_menu, start_multiplayer_join_from_menu);
        zplayer.SetIngameMode((start_campaign_from_menu || start_replay_from_menu)
            ? ZOD_MENU_CAMPAIGN
            : ((start_multiplayer_host_from_menu || start_multiplayer_join_from_menu ||
                starting_conditions.read_connect_address)
                ? ZOD_MENU_MULTIPLAYER : ZOD_MENU_SKIRMISH));
        zplayer.SetCampaignBriefingsEnabled(start_campaign_from_menu || start_replay_from_menu);
        // Both Start Game and Replay Level show the new planet INTRO;
        // Select Map and Multiplayer retain splash.bmp.
	
	zplayer.DisableCursor(starting_conditions.read_disable_zcursor);
	zplayer.SetSoundsOff(starting_conditions.read_sound_off);
	zplayer.SetMusicOff(starting_conditions.read_music_off);
	zplayer.SetWindowed(starting_conditions.read_is_windowed);
	zplayer.SetUseOpenGL(!starting_conditions.read_opengl_off);
	if(starting_conditions.read_player_team)
		zplayer.SetDesiredTeam((team_type)starting_conditions.team);
	if(starting_conditions.read_player_name)
		zplayer.SetPlayerName(starting_conditions.player_name);
	if(starting_conditions.read_loginname)
		zplayer.SetLoginName(starting_conditions.loginname);
	if(starting_conditions.read_password)
		zplayer.SetLoginPassword(starting_conditions.password);
	if(starting_conditions.read_connect_address)
		zplayer.SetRemoteAddress(starting_conditions.connect_address);
	if(starting_conditions.read_resolution)
		zplayer.SetDimensions(starting_conditions.resolution_width, starting_conditions.resolution_height);
	
	zplayer.Setup();
	zplayer.Run();
}

void run_tray_app()
{
	ZTray ztray;

	if(starting_conditions.read_connect_address)
		ztray.SetRemoteAddress(starting_conditions.connect_address);

	ztray.Setup();
	ztray.Run();
}
 
void display_help(char *shell_command)
{
	printf("\n==================================================================\n");
	printf("Command list...\n");
	printf("-c ip_address        - game host address\n");
	printf("-m filename          - map to be used\n");
	printf("-l filename          - map list to be used\n");
	printf("-z filename          - settings file to be used\n");
	printf("-e filename          - main server settings file to be used\n");
	printf("-n player_name       - your player name\n");
	printf("-g login_name        - your login name\n");
	printf("-i login_password    - your login password\n");
	printf("-t team              - your team\n");
	printf("-b team              - connect a bot player\n");
	printf("-w                   - run game in windowed mode\n");
	printf("-r resolution        - resolution to run the game at\n");
	printf("-d                   - run a dedicated server\n");
	printf("-h                   - display command help\n");
	printf("-s                   - no sound\n");
	printf("-u                   - no music\n");
	printf("-o                   - no opengl\n");
	printf("-k                   - use faster and blander cursor\n");
	printf("-v                   - display version and credits\n");
	printf("-a                   - run shell based tray app\n");
	
	printf("\nExample usage...\n");
	printf("%s -c localhost -r 800x600 -w\n", shell_command);
	printf("%s -m level1.map -b 1 -p 1\n", shell_command);
	printf("==================================================================\n");
}

void display_version()
{
#ifndef DISABLE_OPENGL
	printf("\nZod: A Zed Engine, Version Alpha\n");
#else
	printf("\nZod: A Zed Engine, Version Alpha (OpenGL Disabled)\n");
#endif
	printf("By Michael Bok\n");
	printf("Please visit http://zod.sourceforge.net/ and http://zzone.lewe.com/\n");
}

void input_options::setdefaults()
{
	printf("no arguments set, using local game defaults\n");

	// Use local map list
	read_map_list = true;
	map_list = "map_list.txt";

	// No fixed player name: the main menu loads the user's saved choice.
	read_player_name = false;
	player_name.clear();

	// Player team
	read_player_team = true;
	player_team_str = "red";
	team = RED_TEAM;

	// Resolution
	resolution = "1280x720";
	resolution_width = 1280;
	resolution_height = 720;
	read_resolution = true;

}
 
int input_options::checkoptions()
{
	//nothing?
	if(!read_connect_address &&
		!read_map_name &&
		!read_is_windowed &&
		!read_resolution &&
		!read_is_dedicated &&
		!read_display_help &&
		!read_display_version &&
		!read_map_list)
	{
		read_display_help = true;
	}
	
	if(read_connect_address && read_is_dedicated)
	{
		printf("cannot be a dedicated server and have a connect address\n");
		return 0;
	}
	
	if(read_is_dedicated && read_resolution)
	{
		printf("cannot be a dedicated server and have a screen resolution\n");
		return 0;
	}
	
	if(read_is_dedicated && read_is_windowed)
	{
		printf("cannot be a dedicated server and be in windowed mode\n");
		return 0;
	}
	
	if(read_connect_address && (read_map_name || read_map_list))
	{
		printf("cannot have a connect address and set the map\n");
		return 0;
	}

	if(read_map_name && read_map_list)
	{
		printf("cannot a read map list and a specific map file\n");
		return 0;
	}

	if(read_run_tray && !read_connect_address)
	{
		printf("need a connect address to run the tray app\n");
		return 0;
	}
	
	return 1;
}
 
int input_options::getoptions(int argc, char **argv)
{
	int c;
	int i;
	int temp_int;
	extern char *optarg;
	extern int optind;

	char optstring[] = "c:m:l:n:t:b:z:e:g:i:wr:dhvksuoa";
	while ((c = getopt(argc, argv, optstring)) != -1) 
	{
		switch(c) 
		{
			case 'c':
				if(!optarg) return 0;
				read_connect_address = true;
				connect_address = optarg;
				break;
			case 'm':
				if(!optarg) return 0;
				read_map_name = true;
				map_name = optarg;
				break;

			case 'l':
				if(!optarg) return 0;
				read_map_list = true;
				map_list = optarg;
				break;
				
			case 'n':
				if(!optarg) return 0;
				read_player_name = true;
				player_name = optarg;
				break;

			case 'z':
				if(!optarg) return 0;
				read_settings = true;
				settings_filename = optarg;
				break;

			case 'e':
				if(!optarg) return 0;
				read_p_settings = true;
				p_settings_filename = optarg;
				break;

			case 'g':
				if(!optarg) return 0;
				read_loginname = true;
				loginname = optarg;
				break;

			case 'i':
				if(!optarg) return 0;
				read_password = true;
				password = optarg;
				break;
				
			case 't':
				if(!optarg) return 0;
				read_player_team = true;
				player_team_str = optarg;

				for(i=0;i<MAX_TEAM_TYPES;i++)
					if(team_type_string[i] == player_team_str)
						break;

				if(i!=MAX_TEAM_TYPES)
					team = i;
				else
					printf("could not find the team '%s', perhaps try lowercase?\n", player_team_str.c_str());
				break;
				
			case 'b':
				if(!optarg) return 0;
				//read_bot_count = true;
				//bot_count = atoi(optarg);
				if(!optarg) return 0;

				for(i=0;i<MAX_TEAM_TYPES;i++)
					if(team_type_string[i] == optarg)
						break;

				if(i!=MAX_TEAM_TYPES)
					read_start_bot[i] = true;
				else
					printf("could not find the team '%s', perhaps try lowercase?\n", optarg);
				break;
				
			case 'r':
				if(!optarg) return 0;
				resolution = optarg;
				
				temp_int = resolution.find('x');
				
				if(temp_int != string::npos)
				{
					resolution_width = atoi(resolution.substr(0, temp_int).c_str());
					resolution_height = atoi(resolution.substr(temp_int+1, 10).c_str());
					read_resolution = true;
				}
				else
					read_resolution = false;
				
				break;
				
			case 'w':
				read_is_windowed = true;
				break;
				
			case 'd':
				read_is_dedicated = true;
				break;
				
			case 'h':
				read_display_help = true;
				break;
				
			case 'v':
				read_display_version = true;
				break;

			case 's':
				read_sound_off = true;
				break;

			case 'u':
				read_music_off = true;
				break;

			case 'k':
				read_disable_zcursor = true;
				break;

			case 'o':
				read_opengl_off = true;
				break;

			case 'a':
				read_run_tray = true;
				break;
				
			case '?':
				printf("unrecognized option -%c\n", c);
				read_display_help = true;
				return 0;
				break;
				
				
		}
		
	}

	return 1;
}
