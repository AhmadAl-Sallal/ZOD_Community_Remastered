#ifdef _WIN32
// Embedded editor shares ZOD's getopt implementation and its global state.
// Defining xgetopt a second time inside the same EXE causes duplicate symbols.
#ifdef ZOD_EDITOR_EMBEDDED
extern int optind, opterr;
extern char *optarg;
int getopt(int argc, char **argv, char *optstring);
#else
#include "xgetopt.h"
int optind, opterr;
TCHAR *optarg;
#endif
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <time.h>
#include "zmap.h"
#include "zsdl.h"
#include "common.h"
#include "zmini_map.h"

#include "zrobot.h"

#include "bfort.h"
#include "brepair.h"
#include "bradar.h"
#include "brobot.h"
#include "bvehicle.h"
#include "bbridge.h"

#include "cgatling.h"
#include "cgun.h"
#include "chowitzer.h"
#include "cmissilecannon.h"

#include "vjeep.h"
#include "vlight.h"
#include "vmedium.h"
#include "vheavy.h"
#include "vapc.h"
#include "vmissilelauncher.h"
#include "vcrane.h"

#include "rgrunt.h"
#include "rpsycho.h"
#include "rsniper.h"
#include "rtough.h"
#include "rpyro.h"
#include "rlaser.h"

#include "oflag.h"
#include "orock.h"
#include "ogrenades.h"
#include "orockets.h"
#include "ohut.h"
#include "omapobject.h"

#include <SDL/SDL_ttf.h>

#define SEP_SHIFT_X 320
#define SEP_WIDTH 16
#define MAP_SHIFT_X (SEP_SHIFT_X + SEP_WIDTH)
// Editor-only sidebar geometry; game minimaps keep their original size.
#define SIDEBAR_WIDTH 320
#define PALETTE_BOTTOM 384
#define MINIMAP_X 11
#define MINIMAP_Y 395
// Leave room for the always-visible Health/Level/Bridge links readout.
#define EDITOR_MINIMAP_W 190
#define EDITOR_STATUS_H 70
#define EDITOR_CONTROLS_H 133
#define EDITOR_CONTROLS_MAX_EXTRA_H 26
#define EDITOR_BOTTOM_MARGIN 6
#define EDITOR_GAP 8

using namespace COMMON;
using namespace std;

//editor modes
enum map_editor_mode
{
	PLACE_TILE_MODE, PLACE_BUILDING_MODE, PLACE_CANNON_MODE,
	PLACE_VEHICLE_MODE, PLACE_ROBOT_MODE,
	PLACE_ITEM_MODE, PLACE_ZONE_MODE, REMOVE_ZONE_MODE,
	REMOVE_OBJECT_MODE, MAX_EDITOR_MODES
};

enum map_ruler_mode
{
	NO_RULER, SIMPLE_RULER, FULL_RULER,
	MAX_RULER_MODES
};

const string map_editor_mode_string[MAX_EDITOR_MODES] =
{
	"place_tile", "place_building", "place_cannon", "place_vehicle", "place_robot", "place_item", "place_zone", "remove_zone", "remove_object"
};

struct map_event
{
	int mode;
	int x, y;
	int w, h;
	int mtile;
	int ptile;
	int team;
	int object;
	int blevel;
	int health_percent;
	int extra_links;
	int ref_id;
};

//the map
ZMap edit_map;

//the minimap
ZMiniMap edit_minimap;

//the time
ZTime ztime;

//our object list
vector<ZObject*> object_list;

//objects required for rendering purposes
BFort *bfort_front;
BFort *bfort_back;
BRepair *brepair;
BRadar *bradar;
BRobot *brobot;
BVehicle *bvehicle;
BBridge *bbridge_vert;
BBridge *bbridge_horz;

OFlag *oflag;
ORock *orock;
OGrenades *ogrenades;
ORockets *orockets;
OHut *ohut;
OMapObject *omapobject;

CGatling *cgatling;
CGun *cgun;
CHowitzer *chowitzer;
CMissileCannon *cmissilecannon;

VJeep *vjeep;
VLight *vlight;
VMedium *vmedium;
VHeavy *vheavy;
VAPC *vapc;
VMissileLauncher *vmissilelauncher;
VCrane *vcrane;

RGrunt *rgrunt;
RPsycho *rpsycho;
RSniper *rsniper;
RTough *rtough;
RPyro *rpyro;
RLaser *rlaser;

//sdl stuff
SDL_Surface *screen;
TTF_Font *ttf_font;
TTF_Font *editor_font;
TTF_Font *editor_small_font;
// Larger fonts for the four lines by the minimap and the three central values.
TTF_Font *editor_status_font;
TTF_Font *editor_info_font;
int mouse_x, mouse_y;

//other
int current_mode;
int current_team;
int current_object;
//int current_ptile;
int current_blevel;
int current_extra_links;
int current_health_percent;
int hover_ptile;
int hover_mtile;
bool changes_made;
// Fullscreen mode includes its own always-visible exit control, since the
// native title-bar X is absent when SDL_FULLSCREEN is active.
// Keep the ORIGINAL 1280x720 logical editor layout in both modes: the map
// and the 320px sidebar are designed for these coordinates.
bool editor_fullscreen;
bool editor_exit_prompt;
bool editor_exit_save_failed;
ZObject *remove_object;
ZObject *hover_object;
SDL_Rect new_zone;
bool right_down, left_down, up_down, down_down;
double last_vert_scroll_time, last_horz_scroll_time;
double vert_scroll_over, horz_scroll_over;
bool click_down;
bool rctrl_down;
bool lctrl_down;
bool lshift_down;
bool rshift_down;
vector<int> current_ptile_list;
int next_ref_id;
vector<map_event> undo_list;
vector<map_event> redo_list;
int current_map_ruler;

//args
bool is_new;
int width;
int height;
planet_type palette;
string filename;
string mapname;

//prototypes
void init_main_objects();
void getargs(int argc, char **argv);
int checkargs(char *exec_command);
void display_proper_init(char *exec_command);
void blit_message(const char *message, int x, int y, int r = 255, int g = 255, int b = 255);
void draw_everything();
void draw_editor_window_controls();
void draw_editor_exit_prompt();
void editor_overlay_text(const char *message, int x, int y, int r, int g, int b);
void update_editor_video_surface();
void toggle_editor_fullscreen();
bool editor_close_button_hit(int x, int y);
bool request_editor_exit();
bool process_editor_exit_action(int action);
int editor_exit_prompt_click_action(int x, int y);
void draw_palette(bool flip = true);
void draw_map(bool flip = true);
void draw_seperator(bool flip = true);
void draw_info(bool flip = true);
void layout_editor_minimap();
int editor_status_top();
int editor_controls_top();
void ui_fill(int x, int y, int w, int h, int r, int g, int b);
void ui_outline(int x, int y, int w, int h, int r, int g, int b);
void ui_text(TTF_Font *font, const char *message, int x, int y, int r, int g, int b);
void draw_editor_shortcuts(int panel_top);
void draw_editor_shortcut(int x, int y, const char *key, const char *label, int key_width);
void draw_zones();
void draw_map_ruler();
void process_mouse_movement(int x, int y);
void process_mouse_click(int x, int y, bool motion_click = false);
void process_mouse_right_click(int x, int y);
ZObject *object_at_map_screen_position(int x, int y);
void process_mouse_wheel_up(int x, int y);
void process_mouse_wheel_down(int x, int y);
void process_button_pressed(int c);
void process_button_unpressed(int c);
void process_event(map_event the_event, bool push_into_undo = false);
map_event reverse_event(map_event the_event);
void draw_selection_box(int tile);
void draw_selection_box(int x, int y, int w, int h);
void draw_x_marker(int tile);
void set_placement_objects_team(enum team_type team);
void set_placement_objects_stamp();
void set_placement_objects_blevel(int blevel);
bool object_exists_at(int x, int y);
void place_object(map_object new_object);
void load_object(map_object new_object);
int check_hover_object();
void draw_hover_building_info();
void ProcessScroll(double the_time);
void do_print_screen();
void increase_object();
void decrease_object();
bool within_minimap(int x, int y);
bool ctrl_down() { return rctrl_down || lctrl_down; }
bool shift_down() { return lshift_down || rshift_down; }
void do_redo();
void do_undo();
void store_map_event(map_event the_event, vector<map_event> &the_list);

#ifdef ZOD_EDITOR_EMBEDDED
// This is called by the main menu in the SAME process. Both the menu and
// editor share the existing 1280x720 SDL software fullscreen surface.
// In particular, neither side calls SDL_Quit on the handoff.
static bool zod_editor_images_loaded = false;

struct ZodEmbeddedEditorCleanup
{
    ~ZodEmbeddedEditorCleanup()
    {
        // A subsequent invocation of the editor must not keep the previous
        // map's objects, previews, or undo history alive.
        for(size_t i = 0; i < object_list.size(); ++i) delete object_list[i];
        object_list.clear();
        edit_map.ClearMap();
        undo_list.clear();
        redo_list.clear();
        current_ptile_list.clear();
        remove_object = NULL;
        hover_object = NULL;

        delete bfort_front; bfort_front = NULL;
        delete bfort_back; bfort_back = NULL;
        delete brepair; brepair = NULL;
        delete bradar; bradar = NULL;
        delete brobot; brobot = NULL;
        delete bvehicle; bvehicle = NULL;
        delete bbridge_vert; bbridge_vert = NULL;
        delete bbridge_horz; bbridge_horz = NULL;
        delete oflag; oflag = NULL;
        delete orock; orock = NULL;
        delete ogrenades; ogrenades = NULL;
        delete orockets; orockets = NULL;
        delete ohut; ohut = NULL;
        delete omapobject; omapobject = NULL;
        delete cgatling; cgatling = NULL;
        delete cgun; cgun = NULL;
        delete chowitzer; chowitzer = NULL;
        delete cmissilecannon; cmissilecannon = NULL;
        delete vjeep; vjeep = NULL;
        delete vlight; vlight = NULL;
        delete vmedium; vmedium = NULL;
        delete vheavy; vheavy = NULL;
        delete vapc; vapc = NULL;
        delete vmissilelauncher; vmissilelauncher = NULL;
        delete vcrane; vcrane = NULL;
        delete rgrunt; rgrunt = NULL;
        delete rpsycho; rpsycho = NULL;
        delete rsniper; rsniper = NULL;
        delete rtough; rtough = NULL;
        delete rpyro; rpyro = NULL;
        delete rlaser; rlaser = NULL;

        if(ttf_font) TTF_CloseFont(ttf_font); ttf_font = NULL;
        if(editor_font) TTF_CloseFont(editor_font); editor_font = NULL;
        if(editor_small_font) TTF_CloseFont(editor_small_font); editor_small_font = NULL;
        if(editor_status_font) TTF_CloseFont(editor_status_font); editor_status_font = NULL;
        if(editor_info_font) TTF_CloseFont(editor_info_font); editor_info_font = NULL;
        TTF_Quit();
        // Intentionally keep SDL video/screen alive for the main menu.
    }
};
int RunZodMapEditorEmbedded(int argc, char **argv)
#else
#undef main
int main(int argc, char **argv)
#endif
{
#ifdef ZOD_EDITOR_EMBEDDED
    ZodEmbeddedEditorCleanup cleanup;
    // xgetopt resets its internal next-option pointer when optind == 0.
    optind = 0;
    is_new = false;
    width = height = 0;
    filename.clear();
    mapname.clear();
    palette = DESERT;
#endif
	//init others
	//current_ptile = -1;
	current_ptile_list.clear();
	hover_ptile = -1;
	hover_mtile = -1;
	current_mode = 0;
	current_team = 0;
	current_blevel = 0;
	current_extra_links = 0;
	current_health_percent = 100;
	changes_made = false;
	editor_fullscreen = false;
	editor_exit_prompt = false;
	editor_exit_save_failed = false;
	remove_object = NULL;
	hover_object = NULL;
	new_zone.x = -1;
	right_down = false;
	left_down = false;
	up_down = false;
	down_down = false;
	click_down = false;
	rctrl_down = false;
	lctrl_down = false;
	lshift_down = false;
	rshift_down = false;
	next_ref_id = 0;
	current_map_ruler = NO_RULER;
	
	//seed
	srand(time(0));
	
	getargs(argc, argv);
	
	//check if args ok
	if(!checkargs(argv[0])) return 0;

	// Both menu and editor use the SAME fixed 1280x720 fullscreen mode.
	// In-process launch reuses the already-active SDL surface, avoiding the
	// desktop/fullscreen/desktop handoff and its repeated display flashes.
#ifdef ZOD_EDITOR_EMBEDDED
    screen = SDL_GetVideoSurface();
    if(!screen || screen->w != 1280 || screen->h != 720 ||
       !(screen->flags & SDL_FULLSCREEN) || (screen->flags & SDL_OPENGL))
    {
        fprintf(stderr, "Map Editor: unexpected menu video mode; trying 1280x720 fullscreen.\n");
        screen = SDL_SetVideoMode(1280, 720, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
    }
#else
    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
    screen = SDL_SetVideoMode(1280, 720, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
#endif
    if(screen)
        editor_fullscreen = (screen->flags & SDL_FULLSCREEN) != 0;
    else
    {
        fprintf(stderr, "Map Editor fullscreen unavailable: %s\n", SDL_GetError());
        screen = SDL_SetVideoMode(1280, 720, 32, SDL_SWSURFACE | SDL_RESIZABLE);
    }
    if(!screen)
    {
        fprintf(stderr, "Map Editor video init failed: %s\n", SDL_GetError());
#ifndef ZOD_EDITOR_EMBEDDED
        SDL_Quit();
#endif
        return 1;
    }
    mouse_x = 0;
    mouse_y = 0;
    SDL_WM_SetCaption("Zod Map Editor", "Zod Map Editor");
#ifndef ZOD_EDITOR_EMBEDDED
    atexit(SDL_Quit);
#endif
	//SDL_EnableKeyRepeat(1,1);

	//just force software mode
	ZSDL_Surface::SetUseOpenGL(false);
	ZSDL_Surface::SetScreenDimensions(screen->w, screen->h);
	ZSDL_Surface::SetMainSoftwareSurface(screen);
	ZSDL_Surface::SetMapPlace(MAP_SHIFT_X, 0);
	ZSDL_Surface::SetHasHud(false);
	
	//TTF
	TTF_Init();
	ttf_font = TTF_OpenFont("assets/Arimo-Regular.ttf",10);
	editor_font = TTF_OpenFont("assets/Arimo-Regular.ttf",13);
	editor_small_font = TTF_OpenFont("assets/Arimo-Regular.ttf",11);
	editor_status_font = TTF_OpenFont("assets/Arimo-Regular.ttf",14);
	editor_info_font = TTF_OpenFont("assets/Arimo-Regular.ttf",15);
	if (!ttf_font) printf("could not load Arimo-Regular.ttf\n");
	
    // Shared static graphics are initialized once in the embedded build.
    // Calling ZMap::Init repeatedly appends palette lists on every map open.
#ifdef ZOD_EDITOR_EMBEDDED
    if(!zod_editor_images_loaded)
    {
#endif
	//init stuff
	ZTeam::Init();
	ZMap::Init();
	ZBuilding::Init();
	BFort::Init();
	BRepair::Init();
	BRadar::Init();
	BRobot::Init();
	BVehicle::Init();
	BBridge::Init();
	OFlag::Init();
	ORock::Init();
	OGrenades::Init();
	ORockets::Init();
	OHut::Init();
	OMapObject::Init();
	ZCannon::Init();
	CGatling::Init();
	CGun::Init();
	CHowitzer::Init();
	CMissileCannon::Init();
	ZVehicle::Init();
	VJeep::Init();
	VLight::Init();
	VMedium::Init();
	VHeavy::Init();
	VAPC::Init();
	VMissileLauncher::Init();
	VCrane::Init();
	ZRobot::Init();
#ifdef ZOD_EDITOR_EMBEDDED
        zod_editor_images_loaded = true;
    }
#endif

	init_main_objects();
	
	//do "load"
	if(is_new)
		edit_map.MakeNewMap(mapname.c_str(), palette, width, height);
	else if(!edit_map.Read(filename.c_str()))
	{
		printf("could not read map '%s'\n", filename.c_str());
		return 0;
	}

	//testing purposes
	//edit_map.ReplaceUnusableTiles();
	
	//load objects
	for(vector<map_object>::iterator i=edit_map.GetObjectList().begin(); i!=edit_map.GetObjectList().end(); i++)
		load_object(*i);

	//setup rock renders
	ORock::SetupRockRenders(edit_map, object_list);
	
	//set these
	bfort_front->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	bfort_back->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	brepair->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	bradar->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	brobot->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	bvehicle->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	bbridge_vert->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	bbridge_horz->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	orock->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);
	ohut->ChangePalette((planet_type)edit_map.GetMapBasics().terrain_type);

	//has to be set after palette
	orock->SetDefaultRender();

	//no stamping
	set_placement_objects_stamp();

	//levels
	set_placement_objects_blevel(0);

	//unique
	vjeep->SetDirection(6);
	
	//set this
	edit_map.SetViewingDimensions(screen->w - MAP_SHIFT_X, screen->h);
	
	edit_map.DebugMapInfo();

	//minimap
	edit_minimap.Setup(&edit_map, &object_list);
	layout_editor_minimap();
	//edit_minimap.SetShowTerrain(true);

	//draw it all.
	draw_everything();
	
	SDL_Event event;
	while(1)
	{
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_QUIT:
					// Windows' native X and Alt+F4 use the same safe exit path.
					if(request_editor_exit()) return 0;
					break;
				case SDL_VIDEORESIZE:
				{
					if(editor_fullscreen) break;
					SDL_Surface *resized = SDL_SetVideoMode(event.resize.w, event.resize.h,
						32, SDL_SWSURFACE | SDL_RESIZABLE);
					if(resized)
					{
						screen = resized;
						update_editor_video_surface();
					}
					break;
				}
				case SDL_MOUSEMOTION:
					if(!editor_exit_prompt)
						process_mouse_movement(event.motion.x, event.motion.y);
					break;
				case SDL_MOUSEBUTTONDOWN:
				{
					if(editor_exit_prompt)
					{
						if(event.button.button == SDL_BUTTON_LEFT &&
							process_editor_exit_action(editor_exit_prompt_click_action(
								event.button.x, event.button.y))) return 0;
						break;
					}
					if(editor_close_button_hit(event.button.x, event.button.y))
					{
						// Neither mouse button paints/erases through the CLOSE button.
						if(event.button.button == SDL_BUTTON_LEFT && request_editor_exit()) return 0;
						break;
					}
					switch(event.button.button)
					{
						case SDL_BUTTON_RIGHT:
							// Keep the approved V1 right-click object eraser.
							process_mouse_right_click(event.button.x, event.button.y);
							break;
						case SDL_BUTTON_WHEELUP:
							process_mouse_wheel_up(event.button.x, event.button.y);
							break;
						case SDL_BUTTON_WHEELDOWN:
							process_mouse_wheel_down(event.button.x, event.button.y);
							break;
						default:
							click_down = true;
							process_mouse_click(event.button.x, event.button.y);
							break;
					}
					break;
				}
				case SDL_MOUSEBUTTONUP:
					if(!editor_exit_prompt && event.button.button != SDL_BUTTON_RIGHT)
						click_down = false;
					break;
				case SDL_KEYDOWN:
					if(editor_exit_prompt)
					{
						int action = 0;
						switch(event.key.keysym.sym)
						{
							case SDLK_s: case SDLK_RETURN: case SDLK_KP_ENTER: action = 1; break;
							case SDLK_d: action = 2; break;
							case SDLK_ESCAPE: action = 3; break;
						}
						if(process_editor_exit_action(action)) return 0;
						}
					else if(event.key.keysym.sym == SDLK_F11)
						toggle_editor_fullscreen();
					else
						process_button_pressed(event.key.keysym.sym);
					break;
				case SDL_KEYUP:
					if(!editor_exit_prompt) process_button_unpressed(event.key.keysym.sym);
					break;
			}
		}
		
// 		if(edit_map.DoEffects(current_time())) draw_map();
		double the_time = current_time();
		ztime.UpdateTime();

		if(!editor_exit_prompt) ProcessScroll(the_time);

		// The camera or the object list may change without a mouse-motion event.
		if(!editor_exit_prompt && check_hover_object()) draw_info(false);

		//edit_map.DoEffects(the_time);
		
		for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
			(*i)->Process();
		
		draw_map();
		draw_editor_window_controls();
		if(editor_exit_prompt) draw_editor_exit_prompt();

		if(editor_status_top() > MINIMAP_Y + 18)
			edit_minimap.DoRender(screen, MINIMAP_X, MINIMAP_Y);

		SDL_Flip(screen);
		
		uni_pause(10);
	}
	
	return 0;
}

void do_print_screen()
{
	SDL_Surface *print_surface;
	string bmp_filename;

	if(!edit_map.GetRender().GetBaseSurface()) return;

	print_surface = CopyImage(edit_map.GetRender().GetBaseSurface());

	if(!print_surface) return;

	//trick system
	{
		int map_w, map_h;

		map_w = edit_map.GetMapBasics().width*16;
		map_h = edit_map.GetMapBasics().height*16;

		edit_map.SetViewingDimensions(map_w, map_h);
		ZSDL_Surface::SetScreenDimensions(map_w, map_h);
		ZSDL_Surface::SetMainSoftwareSurface(print_surface);
		ZSDL_Surface::SetMapPlace(0, 0);
	}

	//render screenshot
	{
		edit_map.DoRender(print_surface);

		//draw zones
		edit_map.DoZoneEffects(current_time(), print_surface);
	
		//draw objects
		for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
			(*i)->DoRender(edit_map, print_surface);
	
		//draw after effects
		for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
			(*i)->DoAfterEffects(edit_map, print_surface);
	}

	//untrick system
	{
		ZSDL_Surface::SetScreenDimensions(screen->w, screen->h);
		ZSDL_Surface::SetMainSoftwareSurface(screen);
		ZSDL_Surface::SetMapPlace(MAP_SHIFT_X, 0);
		edit_map.SetViewingDimensions(screen->w-MAP_SHIFT_X, screen->h);
	}


	//save the map
	{
		bmp_filename = filename + ".bmp";

		printf("saving map screenshot: '%s'\n", bmp_filename.c_str());

		SDL_SaveBMP(print_surface, bmp_filename.c_str());
	}

	SDL_FreeSurface(print_surface);
}

void init_main_objects()
{
	bfort_front = new BFort(&ztime, NULL, DESERT, true);
	bfort_back = new BFort(&ztime, NULL, DESERT, false);
	brepair = new BRepair(&ztime);
	bradar = new BRadar(&ztime);
	brobot = new BRobot(&ztime);
	bvehicle = new BVehicle(&ztime);
	bbridge_vert = new BBridge(&ztime, NULL, DESERT, true);
	bbridge_horz = new BBridge(&ztime, NULL, DESERT, false);

	oflag = new OFlag(&ztime);
	orock = new ORock(&ztime);
	ogrenades = new OGrenades(&ztime);
	orockets = new ORockets(&ztime);
	ohut = new OHut(&ztime);
	omapobject = new OMapObject(&ztime);

	cgatling = new CGatling(&ztime);
	cgun = new CGun(&ztime);
	chowitzer = new CHowitzer(&ztime);
	cmissilecannon = new CMissileCannon(&ztime);

	vjeep = new VJeep(&ztime);
	vlight = new VLight(&ztime);
	vmedium = new VMedium(&ztime);
	vheavy = new VHeavy(&ztime);
	vapc = new VAPC(&ztime);
	vmissilelauncher = new VMissileLauncher(&ztime);
	vcrane = new VCrane(&ztime);

	rgrunt = new RGrunt(&ztime);
	rpsycho = new RPsycho(&ztime);
	rsniper = new RSniper(&ztime);
	rtough = new RTough(&ztime);
	rpyro = new RPyro(&ztime);
	rlaser = new RLaser(&ztime);
}

void display_proper_init(char *exec_command)
{
	printf("Welcome to the Zod Map Editor\n\n");
	
	printf("========================================================\n");
	printf("Command list...\n");
	printf("-f filename              - filename to be loaded / saved\n");
	printf("-d dimensions            - dimensions of a new map \n");
	printf("-p palette               - planet palette of a new map\n");
	printf("-m mapname               - mapname of a new map\n");
	printf("-n                       - create map instead of load\n");
	printf("\nEample usage...\n");
	printf("%s -n -f filename.map -d 20x30 -p desert -m virgin_soldiers\n", exec_command);
	printf("%s -f filename.map\n", exec_command);
	printf("========================================================\n");
}

int checkargs(char *exec_command)
{
	if(!filename.length()) 
	{
		display_proper_init(exec_command);
		return 0;
	}
	
	if(is_new)
	{
		if(width <= 0 || height <= 0 || width > ZOD_MAX_MAP_SIDE || height > ZOD_MAX_MAP_SIDE || !mapname.length() || mapname.length() >= 50)
		{
			display_proper_init(exec_command);
			return 0;
		}
	}
	
	return 1;
}

void getargs(int argc, char **argv)
{
	int c;
	int temp_int;
	string temp_str;
	extern char *optarg;
	extern int optind, optopt;
	
	char optstring[] = "f:d:p:m:n";
	while ((c = getopt(argc, argv, optstring)) != -1) 
	{
		switch(c) 
		{
			case 'f':
				if(!optarg) return;
				filename = optarg;
				break;
			case 'd':
				if(!optarg) return;
				temp_str = optarg;
				temp_int = temp_str.find('x');
				
				if(temp_int != string::npos)
				{
					width = atoi(temp_str.substr(0, temp_int).c_str());
					height = atoi(temp_str.substr(temp_int+1, 10).c_str());
				}
				break;
			case 'p':
				if(!optarg) return;
				palette = DESERT;
				for(int i=0;i<MAX_PLANET_TYPES;i++)
					if(planet_type_string[i] == optarg)
						palette = (planet_type)i;
				
				break;
			case 'm':
				if(!optarg) return;
				mapname = optarg;
				break;
			case 'n':
				is_new = true;
				break;
		}
	}
}

void ProcessScroll(double the_time)
{
	const double shift_speed = 800;
	double time_diff;
	double the_shift;

	if(up_down && !down_down)
	{
		time_diff = the_time - last_vert_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += vert_scroll_over;

		if(the_shift >= 1)
		{
			last_vert_scroll_time = the_time;
			vert_scroll_over = the_shift - (int)the_shift;
			edit_map.ShiftViewUp((int)the_shift);
		}
	}
	else if(!up_down && down_down)
	{
		time_diff = the_time - last_vert_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += vert_scroll_over;

		if(the_shift >= 1)
		{
			last_vert_scroll_time = the_time;
			vert_scroll_over = the_shift - (int)the_shift;
			edit_map.ShiftViewDown((int)the_shift);
		}
	}

	if(right_down && !left_down)
	{
		time_diff = the_time - last_horz_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += horz_scroll_over;

		if(the_shift >= 1)
		{
			last_horz_scroll_time = the_time;
			horz_scroll_over = the_shift - (int)the_shift;
			edit_map.ShiftViewRight((int)the_shift);
		}
	}
	else if(!right_down && left_down)
	{
		time_diff = the_time - last_horz_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += horz_scroll_over;

		if(the_shift >= 1)
		{
			last_horz_scroll_time = the_time;
			horz_scroll_over = the_shift - (int)the_shift;
			edit_map.ShiftViewLeft((int)the_shift);
		}
	}
}

void process_button_unpressed(int c)
{
	switch(c)
	{
		case 273: //up
			up_down = false;
			break;
		case 274: //down
			down_down = false;
			break;
		case 275: //right
			right_down = false;
			break;
		case 276: //left
			left_down = false;
			break;
		case SDLK_LCTRL: lctrl_down = false; break;
		case SDLK_RCTRL: rctrl_down = false; break;
		case SDLK_RSHIFT: rshift_down = false; break;
		case SDLK_LSHIFT: lshift_down = false; break;
	}
}

void process_button_pressed(int c)
{
	switch(c)
	{
		case 273: //up
			up_down = true;
			vert_scroll_over = 0;
			last_vert_scroll_time = current_time();
			//edit_map.ShiftViewUp();
			//draw_map();
			break;
		case 274: //down
			down_down = true;
			vert_scroll_over = 0;
			last_vert_scroll_time = current_time();
			//edit_map.ShiftViewDown();
			//draw_map();
			break;
		case 275: //right
			right_down = true;
			horz_scroll_over = 0;
			last_horz_scroll_time = current_time();
			//edit_map.ShiftViewRight();
			//draw_map();
			break;
		case 276: //left
			left_down = true;
			horz_scroll_over = 0;
			last_horz_scroll_time = current_time();
			//edit_map.ShiftViewLeft();
			//draw_map();
			break;
		case SDLK_PRINT:
			do_print_screen();
			break;
		case SDLK_LCTRL: lctrl_down = true; break;
		case SDLK_RCTRL: rctrl_down = true; break;
		case SDLK_RSHIFT: rshift_down = true; break;
		case SDLK_LSHIFT: lshift_down = true; break;
		case 'r':
			current_map_ruler++;
			if(current_map_ruler >= MAX_RULER_MODES) current_map_ruler = 0;
			break;
		case 'z':
			if(ctrl_down() && shift_down())
				do_redo();
			else if(ctrl_down() && !shift_down())
				do_undo();
			break;
		case 's':
			edit_map.Write(filename.c_str());
			if(changes_made)
			{
				changes_made = false;
				draw_info();
			}
			break;
		case ';':
			current_health_percent--;
			if(current_health_percent < 0) current_health_percent = 100;
			switch(current_mode)
			{
			case PLACE_BUILDING_MODE:
			case PLACE_CANNON_MODE:
			case PLACE_VEHICLE_MODE:
			case PLACE_ROBOT_MODE:
			case PLACE_ITEM_MODE:
				draw_info();
				break;
			}
			break;
		case '\'':
			current_health_percent++;
			if(current_health_percent > 100) current_health_percent = 0;
			switch(current_mode)
			{
			case PLACE_BUILDING_MODE:
			case PLACE_CANNON_MODE:
			case PLACE_VEHICLE_MODE:
			case PLACE_ROBOT_MODE:
			case PLACE_ITEM_MODE:
				draw_info();
				break;
			}
			break;
		case ',': //down the links
			if(current_mode == PLACE_BUILDING_MODE)
			{
				current_extra_links--;

				if(current_extra_links<0) current_extra_links = 0;
				bbridge_vert->SetExtraLinks(current_extra_links);
				bbridge_horz->SetExtraLinks(current_extra_links);

				draw_info();
			}
			break;
		case '.': //down the links
			if(current_mode == PLACE_BUILDING_MODE)
			{
				current_extra_links++;

				bbridge_vert->SetExtraLinks(current_extra_links);
				bbridge_horz->SetExtraLinks(current_extra_links);

				draw_info();
			}
			break;
		case 'm': // next editor mode
		case 'n': // previous editor mode
			current_mode += (c == 'm') ? 1 : -1;
			if(current_mode >= MAX_EDITOR_MODES) current_mode = 0;
			if(current_mode < 0) current_mode = MAX_EDITOR_MODES - 1;

			current_object = 0;
			//current_ptile = -1;
			remove_object = NULL;
			current_health_percent = 100;

			if(current_mode == PLACE_BUILDING_MODE)
			{
				current_extra_links = 0;
				bbridge_vert->SetExtraLinks(current_extra_links);
				bbridge_horz->SetExtraLinks(current_extra_links);
			}
			
			draw_everything();
			break;
		case 'o': //change object
			increase_object();
			break;
		case 't': //change team
			current_team++;
			if(current_team >= MAX_TEAM_TYPES) current_team = 0;
			
			set_placement_objects_team((team_type)current_team);
			
			draw_info(false);
			switch(current_mode)
			{
				case PLACE_ROBOT_MODE:
				case PLACE_VEHICLE_MODE:
					if(mouse_x > MAP_SHIFT_X) draw_map(false);
					break;
			}
			SDL_Flip(screen);
			break;
		case 'l':
			current_blevel++;
			if(current_blevel >= MAX_BUILDING_LEVELS) current_blevel = 0;

			set_placement_objects_blevel(current_blevel);

			draw_info(false);
			if(current_mode == PLACE_BUILDING_MODE)
					if(mouse_x > MAP_SHIFT_X) draw_map(false);

			SDL_Flip(screen);
	}
}

void increase_object()
{
	current_health_percent = 100;

	current_object++;

	switch(current_mode)
	{
	default:
		//nevermind
		current_object--;
		return;
		break;
	case PLACE_BUILDING_MODE:
		if(current_object>=MAX_BUILDING_TYPES) current_object = 0;
		break;
	case PLACE_CANNON_MODE:
		if(current_object>=MAX_CANNON_TYPES) current_object = 0;
		break;
	case PLACE_VEHICLE_MODE:
		if(current_object>=MAX_VEHICLE_TYPES) current_object = 0;
		break;
	case PLACE_ROBOT_MODE:
		if(current_object>=MAX_ROBOT_TYPES) current_object = 0;
		break;
	case PLACE_ITEM_MODE:
		if(current_object>=MAX_ITEM_TYPES) current_object = 0;
		omapobject->SetType(current_object);
		break;
	}

	draw_info();
}

void decrease_object()
{
	current_health_percent = 100;

	current_object--;

	switch(current_mode)
	{
	default:
		//nevermind
		current_object++;
		return;
		break;
	case PLACE_BUILDING_MODE:
		if(current_object < 0) current_object = MAX_BUILDING_TYPES-1;
		break;
	case PLACE_CANNON_MODE:
		if(current_object < 0) current_object = MAX_CANNON_TYPES-1;
		break;
	case PLACE_VEHICLE_MODE:
		if(current_object < 0) current_object = MAX_VEHICLE_TYPES-1;
		break;
	case PLACE_ROBOT_MODE:
		if(current_object < 0) current_object = MAX_ROBOT_TYPES-1;
		break;
	case PLACE_ITEM_MODE:
		if(current_object < 0) current_object = MAX_ITEM_TYPES-1;
		omapobject->SetType(current_object);
		break;
	}

	draw_info();
}

void process_mouse_wheel_up(int x, int y)
{
	increase_object();
}

void process_mouse_wheel_down(int x, int y)
{
	decrease_object();
}

bool within_minimap(int x, int y)
{
	return (editor_status_top() > MINIMAP_Y + 18) &&
		(x >= MINIMAP_X && x < MINIMAP_X + edit_minimap.GetWidth()) &&
		(y >= MINIMAP_Y && y < MINIMAP_Y + edit_minimap.GetHeight());
}

void do_redo()
{
	map_event the_event;

	if(!redo_list.size()) return;

	the_event = redo_list.back();
	redo_list.pop_back();

	store_map_event(reverse_event(the_event), undo_list);

	process_event(the_event);
}

void do_undo()
{
	map_event the_event;

	if(!undo_list.size()) return;

	the_event = undo_list.back();
	undo_list.pop_back();

	store_map_event(reverse_event(the_event), redo_list);

	process_event(the_event);
}

void store_map_event(map_event the_event, vector<map_event> &the_list)
{
	const int max_list_size = 5000;

	if(the_event.mode == -1) return;

	the_list.push_back(the_event);

	while(the_list.size() > max_list_size)
		the_list.erase(the_list.begin());
}

map_event reverse_event(map_event the_event)
{
	map_event the_reverse_event;
	int mtile_x, mtile_y;
	map_zone *the_map_zone;
	ZObject *robj;
	unsigned char rot, roid;

	mtile_x = the_event.x;
	mtile_y = the_event.y;

	the_reverse_event.mode = -1;

	switch(the_event.mode)
	{
		case PLACE_TILE_MODE:
			the_reverse_event.mode = PLACE_TILE_MODE;
			the_reverse_event.mtile = the_event.mtile;
			the_reverse_event.ptile = edit_map.GetTile(the_event.mtile).tile;
			break;
		case PLACE_BUILDING_MODE:
		case PLACE_CANNON_MODE:
		case PLACE_ROBOT_MODE:
		case PLACE_VEHICLE_MODE:
		case PLACE_ITEM_MODE:
			the_reverse_event.mode = REMOVE_OBJECT_MODE;
			the_reverse_event.ref_id = next_ref_id;
			break;
		case REMOVE_OBJECT_MODE:
			robj = ZObject::GetObjectFromID(the_event.ref_id,object_list);

			if(!robj) break;

			robj->GetObjectID(rot, roid);

			switch(rot)
			{
			case BUILDING_OBJECT: the_reverse_event.mode = PLACE_BUILDING_MODE; break;
			case CANNON_OBJECT: the_reverse_event.mode = PLACE_CANNON_MODE; break;
			case ROBOT_OBJECT: the_reverse_event.mode = PLACE_ROBOT_MODE; break;
			case VEHICLE_OBJECT: the_reverse_event.mode = PLACE_VEHICLE_MODE; break;
			case MAP_ITEM_OBJECT: the_reverse_event.mode = PLACE_ITEM_MODE; break;
			}

			robj->GetCords(the_reverse_event.x, the_reverse_event.y);
			the_reverse_event.x >>= 4;
			the_reverse_event.y >>= 4;
			the_reverse_event.team = robj->GetOwner();
			the_reverse_event.object = roid;
			the_reverse_event.blevel = robj->GetLevel();
			the_reverse_event.extra_links = robj->GetExtraLinks();
			the_reverse_event.health_percent = robj->GetInitialHealthPercent();

			break;
		case PLACE_ZONE_MODE:
			the_reverse_event.mode = REMOVE_ZONE_MODE;
			the_reverse_event.x = the_event.x;
			the_reverse_event.y = the_event.y;
			break;
		case REMOVE_ZONE_MODE:
			the_map_zone = edit_map.GetZoneExact(the_event.x, the_event.y);

			if(the_map_zone)
			{
				the_reverse_event.mode = PLACE_ZONE_MODE;
				the_reverse_event.x = the_map_zone->x;
				the_reverse_event.y = the_map_zone->y;
				the_reverse_event.w = the_map_zone->w;
				the_reverse_event.h = the_map_zone->h;
			}
			break;
	}

	return the_reverse_event;
}

void process_event(map_event the_event, bool push_into_undo)
{
	map_zone new_formal_zone;
	map_object new_object = {};
	int mtile_x, mtile_y;
	int temp_x, temp_y;
	map_tile new_tile;
	bool do_it = false;
	ZObject *robj;

	mtile_x = the_event.x;
	mtile_y = the_event.y;

	switch(the_event.mode)
	{
		case PLACE_TILE_MODE:
			if(push_into_undo)
			{
				redo_list.clear();
				store_map_event(reverse_event(the_event), undo_list);
			}

			new_tile.tile = the_event.ptile;
			edit_map.ChangeTile(the_event.mtile, new_tile);
			changes_made = true;
			draw_info();
			break;
		case PLACE_BUILDING_MODE:
			switch(the_event.object)
			{
				case FORT_FRONT:
					bfort_front->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
				case FORT_BACK:
					bfort_back->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
				case RADAR:
					bradar->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
				case REPAIR:
					brepair->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
				case ROBOT_FACTORY:
					brobot->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
				case VEHICLE_FACTORY:
					bvehicle->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
				case BRIDGE_VERT:
					bbridge_vert->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
				case BRIDGE_HORZ:
					bbridge_horz->GetDimensions(width, height);
					if(width + mtile_x > edit_map.GetMapBasics().width) break;
					if(height + mtile_y > edit_map.GetMapBasics().height) break;
					do_it = true;
					break;
			}
					
			//common code
			if(do_it)
			{
				if(push_into_undo)
				{
					redo_list.clear();
					store_map_event(reverse_event(the_event), undo_list);
				}

				new_object.x = mtile_x;
				new_object.y = mtile_y;
				new_object.owner = the_event.team;
				new_object.object_type = BUILDING_OBJECT;
				new_object.object_id = the_event.object;
				new_object.blevel = the_event.blevel;
				new_object.extra_links = the_event.extra_links;
				new_object.health_percent = the_event.health_percent;
				place_object(new_object);
				changes_made = 1;
				draw_info();
			}
			break;
		case PLACE_CANNON_MODE:
			switch(the_event.object)
			{
				case GATLING:
					cgatling->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case GUN:
					cgun->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case HOWITZER:
					cgatling->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case MISSILE_CANNON:
					cgatling->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
			}
					
			//common code if allowed
			if(do_it)
			{
				if(push_into_undo)
				{
					redo_list.clear();
					store_map_event(reverse_event(the_event), undo_list);
				}

				new_object.x = mtile_x;
				new_object.y = mtile_y;
				new_object.owner = the_event.team;
				new_object.object_type = CANNON_OBJECT;
				new_object.object_id = the_event.object;
				new_object.health_percent = the_event.health_percent;
				place_object(new_object);
				changes_made = 1;
				draw_info();
			}
		case PLACE_ROBOT_MODE:
			if(the_event.team == NULL_TEAM) break;

			if(push_into_undo)
			{
				redo_list.clear();
				store_map_event(reverse_event(the_event), undo_list);
			}
					
			new_object.x = mtile_x;
			new_object.y = mtile_y;
			new_object.owner = the_event.team;
			new_object.object_type = ROBOT_OBJECT;
			new_object.object_id = the_event.object;
			new_object.health_percent = the_event.health_percent;
			place_object(new_object);
			changes_made = 1;
			draw_info();
			break;
		case PLACE_VEHICLE_MODE:
			switch(the_event.object)
			{
				case JEEP:
					vjeep->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case LIGHT:
					vlight->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case MEDIUM:
					vmedium->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case HEAVY:
					vheavy->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case APC:
					vapc->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case MISSILE_LAUNCHER:
					vmissilelauncher->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
				case CRANE:
					vcrane->GetDimensionsPixel(width, height);
					if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
					if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
					do_it = true;
					break;
			}
					
			//common code if allowed
			if(do_it)
			{
				if(push_into_undo)
				{
					redo_list.clear();
					store_map_event(reverse_event(the_event), undo_list);
				}

				new_object.x = mtile_x;
				new_object.y = mtile_y;
				new_object.owner = the_event.team;
				new_object.object_type = VEHICLE_OBJECT;
				new_object.object_id = the_event.object;
				new_object.health_percent = the_event.health_percent;
				place_object(new_object);
				changes_made = 1;
				draw_info();
			}
			else push_into_undo = false;
			break;
		case PLACE_ITEM_MODE:
			switch(the_event.object)
			{
				case FLAG_ITEM:
					do_it = true;
					break;
				case ROCK_ITEM:
					do_it = true;
					break;
				case GRENADES_ITEM:
					do_it = true;
					break;
				case ROCKETS_ITEM:
					do_it = true;
					break;
				case HUT_ITEM:
					do_it = true;
					break;
			}

			if(the_event.object >= MAP0_ITEM && the_event.object <= MAP21_ITEM)
				do_it = true;
					
			//common code
			if(do_it)
			{
				if(push_into_undo)
				{
					redo_list.clear();
					store_map_event(reverse_event(the_event), undo_list);
				}

				new_object.x = mtile_x;
				new_object.y = mtile_y;
				new_object.owner = the_event.team;
				new_object.object_type = MAP_ITEM_OBJECT;
				new_object.object_id = the_event.object;
				new_object.health_percent = the_event.health_percent;
				place_object(new_object);
				changes_made = 1;
				draw_info();
			}
			break;
		case PLACE_ZONE_MODE:			
			//add it
			new_formal_zone.x = the_event.x;
			new_formal_zone.y = the_event.y;
			new_formal_zone.w = the_event.w;
			new_formal_zone.h = the_event.h;
			if(edit_map.AddZone(new_formal_zone))
			{
				if(push_into_undo)
				{
					redo_list.clear();
					store_map_event(reverse_event(the_event), undo_list);
				}

				changes_made = 1;
				draw_info(false);
			}
			break;
		case REMOVE_ZONE_MODE:
			if(push_into_undo)
			{
				redo_list.clear();
				store_map_event(reverse_event(the_event), undo_list);
			}

			if(edit_map.RemoveZone(mtile_x, mtile_y))
			{
				changes_made = 1;
				draw_info();
			}
			break;
		case REMOVE_OBJECT_MODE:
			robj = ZObject::GetObjectFromID(the_event.ref_id,object_list);

			if(!robj) break;

			if(push_into_undo)
			{
				redo_list.clear();
				store_map_event(reverse_event(the_event), undo_list);
			}

			robj->GetCords(temp_x, temp_y);
					
			//find in the map list
			for(vector<map_object>::iterator i=edit_map.GetObjectList().begin();i!=edit_map.GetObjectList().end(); i++)
				if(temp_x == i->x * 16 && temp_y == i->y * 16)
			{
				edit_map.GetObjectList().erase(i);
				break;
			}
					
			//find in the object list
			for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
				if(robj == *i)
			{
				delete *i;
				object_list.erase(i);
				break;
			}

			//elseware
			if(hover_object == robj) hover_object = NULL;
			if(remove_object == robj) remove_object = NULL;

			//just do it, the client can be more critical if it is needed
			ORock::SetupRockRenders(edit_map, object_list);
					
			changes_made = true;
			draw_info();
			break;
			break;
	}
}

// Right click is a one-shot object eraser, not an alternative left click.
// Reuse the existing remove-object event so undo/redo, the saved map list,
// rock rendering and changes_made all behave exactly as in Remove Object mode.
void process_mouse_right_click(int x, int y)
{
	ZObject *clicked_object = object_at_map_screen_position(x, y);
	if(!clicked_object) return;

	map_event the_event = {};
	the_event.mode = REMOVE_OBJECT_MODE;
	the_event.ref_id = clicked_object->GetRefID();
	process_event(the_event, true);
}

void process_mouse_click(int x, int y, bool motion_click)
{
	map_object new_object = {};
	int mtile_x, mtile_y;
	int temp_x, temp_y;
	bool do_it = false;
	map_event the_event;
	static int last_placed_tile_mtile = -1;

	//within minimap?
	if(within_minimap(x,y))
	{
		int new_screen_x, new_screen_y;

		if(edit_minimap.ClickedMap(x - MINIMAP_X, y - MINIMAP_Y, new_screen_x, new_screen_y))
		{
			int shift_x, shift_y, view_w, view_h;

			edit_map.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

			//set desired location
			new_screen_x -= (view_w >> 1);
			new_screen_y -= (view_h >> 1);

			edit_map.SetViewShift(new_screen_x, new_screen_y);
		}
	}
	
	//in palette area?
	if(x < 320 && y < 384)
	{
		int new_tile;
		
		new_tile = ZMap::GetPaletteTile(x,y);
		
		if(new_tile != -1)
		{
			if(ctrl_down())
			{
				if(!current_ptile_list.size())
					current_ptile_list.push_back(new_tile);
				else
				{
					vector<int>::iterator it;

					it = find (current_ptile_list.begin(), current_ptile_list.end(), new_tile);

					if(it == current_ptile_list.end())
						current_ptile_list.push_back(new_tile);
					else
						current_ptile_list.erase(it);
				}
			}
			else
			{
				current_ptile_list.clear();
				current_ptile_list.push_back(new_tile);
			}

			draw_palette();
		}
	}
	
	//in map area?
	if(x >= MAP_SHIFT_X)
	{
		//are we over a tile?
		if(hover_mtile != -1)
		{
			mtile_x = hover_mtile % edit_map.GetMapBasics().width;
			mtile_y = hover_mtile / edit_map.GetMapBasics().width;

			switch(current_mode)
			{
				case PLACE_TILE_MODE:
					//if(current_ptile != -1)
					if(current_ptile_list.size())
					{
						//don't abuse the same tile
						if(motion_click && last_placed_tile_mtile == hover_mtile)
							break;

						last_placed_tile_mtile = hover_mtile;

						the_event.mode = current_mode;
						the_event.mtile = hover_mtile;
						the_event.ptile = current_ptile_list[rand()%current_ptile_list.size()];
						process_event(the_event, true);

						//map_tile new_tile;
						//new_tile.tile = current_ptile_list[rand()%current_ptile_list.size()];
						//edit_map.ChangeTile(hover_mtile, new_tile);
						//changes_made = true;
						//draw_info();
					}
					break;
				case PLACE_ROBOT_MODE:
					if(current_team == NULL_TEAM) break;
				case PLACE_BUILDING_MODE:
				case PLACE_ITEM_MODE:
				case PLACE_CANNON_MODE:
				case PLACE_VEHICLE_MODE:
					if(object_exists_at(mtile_x, mtile_y)) break;

					the_event.mode = current_mode;
					the_event.x = mtile_x;
					the_event.y = mtile_y;
					the_event.team = current_team;
					the_event.object = current_object;
					the_event.blevel = current_blevel;
					the_event.extra_links = current_extra_links;
					the_event.health_percent = current_health_percent;
					process_event(the_event, true);
					break;
				case PLACE_ZONE_MODE:
					if(new_zone.x == -1)
					{
						new_zone.x = mtile_x;
						new_zone.y = mtile_y;
						new_zone.w = 1;
						new_zone.h = 1;
					}
					else
					{
						the_event.mode = current_mode;

						the_event.x = new_zone.x;
						the_event.y = new_zone.y;
						the_event.w = new_zone.w;
						the_event.h = new_zone.h;

						process_event(the_event, true);

						//clear
						new_zone.x = -1;
					}
					break;
				case REMOVE_ZONE_MODE:
					//mtile_x = hover_mtile % edit_map.GetMapBasics().width;
					//mtile_y = hover_mtile / edit_map.GetMapBasics().width;

					the_event.mode = current_mode;
					the_event.x = mtile_x;
					the_event.y = mtile_y;
					process_event(the_event, true);
					
					//if(edit_map.RemoveZone(mtile_x, mtile_y))
					//{
					//	changes_made = 1;
					//	draw_info();
					//}
					
					break;
				case REMOVE_OBJECT_MODE:
					if(!remove_object) break;

					the_event.mode = current_mode;
					the_event.ref_id = remove_object->GetRefID();
					process_event(the_event, true);

					break;
					
					//remove_object->GetCords(temp_x, temp_y);
					//
					////find in the map list
					//for(vector<map_object>::iterator i=edit_map.GetObjectList().begin();i!=edit_map.GetObjectList().end(); i++)
					//	if(temp_x == i->x * 16 && temp_y == i->y * 16)
					//{
					//	edit_map.GetObjectList().erase(i);
					//	break;
					//}
					//
					////find in the object list
					//for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
					//	if(remove_object == *i)
					//{
					//	delete *i;
					//	object_list.erase(i);
					//	break;
					//}

					////hover
					//if(hover_object == remove_object) hover_object = NULL;
					//remove_object = NULL;

					////just do it, the client can be more critical if it is needed
					//ORock::SetupRockRenders(edit_map, object_list);
					//
					//changes_made = true;
					//draw_info();
					//break;
			}
		}
	}
}

void process_mouse_movement(int x, int y)
{
	int mtile_x, mtile_y;
	
	mouse_x = x;
	mouse_y = y;
	
	//in palette area?
	if(x < 320 && y < 384)
	{
		int new_tile;
		
		new_tile = ZMap::GetPaletteTile(x,y);
		
		if(hover_ptile != new_tile)
		{
			hover_ptile = new_tile;
			draw_palette();
// 			SDL_Flip(screen);
		}
	}
	
	//in map area?
	if(x >= MAP_SHIFT_X)
	{
		int new_tile;
		
		//what tile are we over?
		new_tile = edit_map.GetTileIndex(x-MAP_SHIFT_X,y, true);
		
		if(hover_mtile != new_tile) hover_mtile = new_tile;
		if(check_hover_object()) draw_info();
		
		switch(current_mode)
		{
			case PLACE_TILE_MODE:
			case PLACE_BUILDING_MODE:
			case PLACE_CANNON_MODE:
			case PLACE_VEHICLE_MODE:
			case PLACE_ROBOT_MODE:
			case PLACE_ITEM_MODE:
				if(hover_mtile == -1) break;
				hover_mtile = new_tile;
// 				draw_map();
// 				SDL_Flip(screen);
				break;
			case PLACE_ZONE_MODE:
				if(hover_mtile == -1) break;
				mtile_x = hover_mtile % edit_map.GetMapBasics().width;
				mtile_y = hover_mtile / edit_map.GetMapBasics().width;
				
				new_zone.w = (mtile_x - new_zone.x) + 1;
				new_zone.h = (mtile_y - new_zone.y) + 1;
				
				if(new_zone.w > edit_map.GetMapBasics().width) new_zone.w = 1;
				if(new_zone.h > edit_map.GetMapBasics().height) new_zone.h = 1;
				if(new_zone.w < 1) new_zone.w = 1;
				if(new_zone.h < 1) new_zone.h = 1;
				
				break;
			case REMOVE_OBJECT_MODE:
				if(hover_mtile == -1) break;
				remove_object = NULL;
				for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
				{
					int ox, oy;
					int sx, sy;
					int w_pix, h_pix;
					(*i)->GetCords(ox,oy);
					(*i)->GetDimensionsPixel(w_pix,h_pix);
					ox += MAP_SHIFT_X;
					edit_map.GetViewShift(sx, sy);
					ox -= sx;
					oy -= sy;
					if(x < ox) continue;
					if(y < oy) continue;
					if(x >= ox + w_pix) continue;
					if(y >= oy + h_pix) continue;
					
					remove_object = (*i);
					
					break;
				}
				break;
		}

		//mouse down so we click too?
		if(click_down && current_mode != PLACE_ZONE_MODE) process_mouse_click(x, y, true);
	}	

	if(x < MAP_SHIFT_X && check_hover_object()) draw_info();

	if(click_down && within_minimap(x,y)) process_mouse_click(x, y, true);
}

void blit_message(const char *message, int x, int y, int r, int g, int b)
{
	if(!ttf_font) return;
	SDL_Color textcolor;
	textcolor.r = r;
	textcolor.g = g;
	textcolor.b = b;
	textcolor.unused = 0;
	SDL_Surface *text = TTF_RenderText_Solid(ttf_font, message, textcolor);
	if(!text) return;
	SDL_Rect location;
	location.x = x;
	location.y = y;
	SDL_BlitSurface(text, NULL, screen, &location);
	SDL_FreeSurface(text);
}

void draw_everything()
{
	draw_palette(false);
	draw_map(false);
	draw_info(false);
	draw_editor_window_controls();
	if(editor_exit_prompt) draw_editor_exit_prompt();

	SDL_Flip(screen);
}

// SDL 1.2 fullscreen has no OS title bar. Keep one CLOSE button at the
// center of the screen's top edge, away from the map's upper-right corner.
// F11 remains available for window/fullscreen toggling.
void editor_overlay_text(const char *message, int x, int y, int r, int g, int b)
{
	TTF_Font *font = editor_font ? editor_font : ttf_font;
	if(!screen || !font || !message) return;
	SDL_Color color = { (Uint8)r, (Uint8)g, (Uint8)b, 0 };
	SDL_Surface *text_surface = TTF_RenderText_Solid(font, message, color);
	if(!text_surface) return;
	SDL_Rect at;
	at.x = x;
	at.y = y;
	SDL_BlitSurface(text_surface, NULL, screen, &at);
	SDL_FreeSurface(text_surface);
}

// The CLOSE button and its hitbox always share the same centered geometry.
// At 1280x720 this is x=595..684, y=9..38 (entirely on the map).
const int EDITOR_CLOSE_W = 90;
const int EDITOR_CLOSE_Y = 9;
const int EDITOR_CLOSE_H = 30;

bool editor_close_button_hit(int x, int y)
{
	if(!screen || screen->w < MAP_SHIFT_X + EDITOR_CLOSE_W) return false;
	const int close_x = (screen->w - EDITOR_CLOSE_W) / 2;
	return x >= close_x && x < close_x + EDITOR_CLOSE_W &&
	       y >= EDITOR_CLOSE_Y && y < EDITOR_CLOSE_Y + EDITOR_CLOSE_H;
}

void editor_overlay_text_centered(const char *message, int left, int top,
                                  int box_w, int box_h, int r, int g, int b)
{
	TTF_Font *font = editor_font ? editor_font : ttf_font;
	if(!font || !message) return;
	int w = 0, h = 0;
	if(TTF_SizeText(font, message, &w, &h)) return;
	editor_overlay_text(message, left + (box_w - w) / 2,
	                    top + (box_h - h) / 2, r, g, b);
}

void draw_editor_window_controls()
{
	if(!screen || screen->w < MAP_SHIFT_X + EDITOR_CLOSE_W) return;
	const int close_x = (screen->w - EDITOR_CLOSE_W) / 2;
	ui_fill(close_x, EDITOR_CLOSE_Y, EDITOR_CLOSE_W, EDITOR_CLOSE_H, 114, 31, 36);
	ui_outline(close_x, EDITOR_CLOSE_Y, EDITOR_CLOSE_W, EDITOR_CLOSE_H, 255, 157, 157);
	editor_overlay_text_centered("X  CLOSE", close_x, EDITOR_CLOSE_Y,
	                             EDITOR_CLOSE_W, EDITOR_CLOSE_H, 255, 255, 255);
}

void update_editor_video_surface()
{
	if(!screen) return;
	ZSDL_Surface::SetScreenDimensions(screen->w, screen->h);
	ZSDL_Surface::SetMainSoftwareSurface(screen);
	edit_map.SetViewingDimensions(screen->w - MAP_SHIFT_X, screen->h);
	layout_editor_minimap();
	draw_everything();
}

void toggle_editor_fullscreen()
{
	if(!screen) return;
	const bool target_fullscreen = !editor_fullscreen;
	int w = 1280, h = 720;
	Uint32 flags = SDL_SWSURFACE | SDL_RESIZABLE;
	if(target_fullscreen)
	{
		// Match the fixed UI layout, even on high-resolution desktop displays.
		w = 1280;
		h = 720;
		flags = SDL_SWSURFACE | SDL_FULLSCREEN;
	}
	SDL_Surface *updated = SDL_SetVideoMode(w, h, 32, flags);
	if(!updated)
	{
		fprintf(stderr, "Map Editor screen-mode change failed: %s\n", SDL_GetError());
		return;
	}
	screen = updated;
	editor_fullscreen = target_fullscreen;
	update_editor_video_surface();
}

bool request_editor_exit()
{
	if(!changes_made) return true;
	editor_exit_prompt = true;
	editor_exit_save_failed = false;
	// Ignore any held paint/scroll inputs while the modal is open.
	click_down = left_down = right_down = up_down = down_down = false;
	return false;
}

// 1 Save & close, 2 Close without saving, 3 Cancel.
bool process_editor_exit_action(int action)
{
	if(!editor_exit_prompt) return false;
	switch(action)
	{
		case 1:
			if(edit_map.Write(filename.c_str())) return true;
			editor_exit_save_failed = true;
			return false;
		case 2:
			return true;
		case 3:
			editor_exit_prompt = false;
			editor_exit_save_failed = false;
			draw_everything();
			return false;
	}
	return false;
}

void editor_exit_prompt_geometry(int &x, int &y)
{
	x = (MAP_SHIFT_X + screen->w - 450) / 2;
	y = (screen->h - 164) / 2;
}

int editor_exit_prompt_click_action(int x, int y)
{
	if(!screen) return 0;
	int bx, by;
	editor_exit_prompt_geometry(bx, by);
	if(y < by + 108 || y >= by + 146) return 0;
	if(x >= bx + 18 && x < bx + 145) return 1;
	if(x >= bx + 155 && x < bx + 294) return 2;
	if(x >= bx + 304 && x < bx + 432) return 3;
	return 0;
}

void draw_editor_exit_prompt()
{
	if(!screen || !editor_exit_prompt) return;
	int x, y;
	editor_exit_prompt_geometry(x, y);
	ui_fill(x, y, 450, 164, 18, 24, 30);
	ui_outline(x, y, 450, 164, 231, 184, 49);
	ui_fill(x + 2, y + 2, 446, 3, 231, 184, 49);
	editor_overlay_text("UNSAVED MAP CHANGES", x + 18, y + 17, 255, 215, 87);
	editor_overlay_text(editor_exit_save_failed ?
		"Could not save map. Check the file/path." :
		"Save your changes before closing?", x + 18, y + 51, 245, 247, 249);
	editor_overlay_text("S / Enter = save    D = discard    Esc = cancel",
		x + 18, y + 79, 183, 190, 199);
	ui_fill(x + 18, y + 108, 127, 38, 32, 95, 56);
	ui_outline(x + 18, y + 108, 127, 38, 109, 210, 142);
	editor_overlay_text_centered("SAVE & EXIT", x + 18, y + 108, 127, 38, 255, 255, 255);
	ui_fill(x + 155, y + 108, 139, 38, 119, 46, 43);
	ui_outline(x + 155, y + 108, 139, 38, 226, 143, 130);
	editor_overlay_text("DISCARD", x + 192, y + 118, 255, 255, 255);
	ui_fill(x + 304, y + 108, 128, 38, 48, 57, 69);
	ui_outline(x + 304, y + 108, 128, 38, 169, 185, 200);
	editor_overlay_text("CANCEL", x + 344, y + 118, 255, 255, 255);
}

void draw_palette(bool flip)
{
	// Preserve the original tile palette and all its selection markers.
	ZMap::GetMapPalette((planet_type)edit_map.GetMapBasics().terrain_type).BlitSurface(NULL, NULL);
	draw_selection_box(hover_ptile);
	for(vector<int>::iterator i=current_ptile_list.begin(); i!=current_ptile_list.end(); i++)
		draw_x_marker(*i);

	// Decorative frame and separator stay LEFT of MAP_SHIFT_X (336).
	ui_outline(2, 2, 316, 380, 111, 124, 135);
	ui_fill(320, 0, SEP_WIDTH, screen->h, 8, 11, 15);
	ui_fill(320, 0, 2, screen->h, 237, 181, 31);
	ui_fill(324, 0, 1, screen->h, 67, 75, 84);
}

void draw_seperator(bool flip)
{
	
	if(flip) SDL_Flip(screen);
}

void draw_map_ruler()
{
	int shift_x, shift_y, view_w, view_h;
	int x, y;
	int colorkey;
	int r, g, b;
	SDL_Rect line_rect;
	int offset_x, offset_y;
	int xi, yi;
	char message[550];

	if(edit_map.GetMapBasics().terrain_type == ARCTIC)
	{
		r = 255;
		g = 0;
		b = 0;
	}
	else
	{
		r = 255;
		g = 255;
		b = 255;
	}

	colorkey = SDL_MapRGB(screen->format, r, g, b);

	//even drawing the map?
	if(screen->w < MAP_SHIFT_X) return;
	
	edit_map.GetViewShift(shift_x, shift_y);
	edit_map.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

	//top and bottom
	offset_x = -shift_x % 16;
	offset_x += 16;
	xi = shift_x / 16; 
	xi++;
	for(x=offset_x;x<view_w;x+=16, xi++)
	{
		{
			line_rect.x = x + MAP_SHIFT_X;
			line_rect.y = 0;
			line_rect.w = 1;
			line_rect.h = 4;
        
			SDL_FillRect(screen, &line_rect, colorkey );

			if(!(xi%5))
			{
				sprintf(message, "%d", xi);
				blit_message(message, line_rect.x + 2, line_rect.y+6, r, g, b);

				if(current_map_ruler == FULL_RULER)
				{
					line_rect.x = x + MAP_SHIFT_X;
					line_rect.y = 0;
					line_rect.w = 1;
					line_rect.h = screen->h;
        
					SDL_FillRect(screen, &line_rect, colorkey );
				}
			}
		}

		line_rect.x = x + MAP_SHIFT_X;
		line_rect.y = screen->h - 4;
		line_rect.w = 1;
		line_rect.h = 4;
        
		SDL_FillRect(screen, &line_rect, colorkey );
	}

	//left and right
	offset_y = -shift_y % 16;
	offset_y += 16;
	yi = shift_y / 16;
	yi++;
	for(y=offset_y;y<view_h;y+=16, yi++)
	{
		{
			line_rect.x = MAP_SHIFT_X;
			line_rect.y = y;
			line_rect.w = 4;
			line_rect.h = 1;
        
			SDL_FillRect(screen, &line_rect, colorkey );

			if(!(yi%5))
			{
				sprintf(message, "%d", yi);
				blit_message(message, line_rect.x+6, line_rect.y, r, g, b);

				if(current_map_ruler == FULL_RULER)
				{
					line_rect.x = MAP_SHIFT_X;
					line_rect.y = y;
					line_rect.w = screen->w - MAP_SHIFT_X;
					line_rect.h = 1;
        
					SDL_FillRect(screen, &line_rect, colorkey );
				}
			}
		}

		line_rect.x = screen->w - 4;
		line_rect.y = y;
		line_rect.w = 4;
		line_rect.h = 1;
        
		SDL_FillRect(screen, &line_rect, colorkey );
	}
}

void draw_map(bool flip)
{
	static int nz_i, nz_io = 0;
	static double last_nz_time = 0;;
	int x, y;
	int shift_x, shift_y;
	int width, height;
	int mtile_x, mtile_y;
	SDL_Rect to_rect;
	SDL_Rect from_rect;
	
	edit_map.GetViewShift(shift_x, shift_y);
	from_rect.x = shift_x;
	from_rect.y = shift_y;
	from_rect.w = screen->w - MAP_SHIFT_X;
	from_rect.h = screen->h;
	
	to_rect.x = MAP_SHIFT_X;
	to_rect.y = 0;
	//SDL_BlitSurface(edit_map.GetRender(), &from_rect, screen, &to_rect);
	edit_map.DoRender(screen, MAP_SHIFT_X);
	
	//do map effects
	//edit_map.DoEffects(current_time(), screen, MAP_SHIFT_X);

	//draw zones
	edit_map.DoZoneEffects(current_time(), screen, MAP_SHIFT_X);
	
	//draw objects
	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
		(*i)->DoRender(edit_map, screen, MAP_SHIFT_X);
	
	//draw after effects
	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
		(*i)->DoAfterEffects(edit_map, screen, MAP_SHIFT_X);
	
	if(remove_object)
	{
		int ox, oy, ow_pix, oh_pix;
		
		remove_object->GetCords(ox, oy);
		remove_object->GetDimensionsPixel(ow_pix, oh_pix);
		
		ox -= shift_x;
		oy -= shift_y;
		
		draw_selection_box(ox + MAP_SHIFT_X, oy, ow_pix, oh_pix);
	}
	
	//is the mouse in the area?
	if(mouse_x >= MAP_SHIFT_X && hover_mtile != -1)
	{
		//where we going to?
		edit_map.GetTile(hover_mtile,x,y, true);
		to_rect.x = x+MAP_SHIFT_X;
		to_rect.y = y;
		
		mtile_x = hover_mtile % edit_map.GetMapBasics().width;
		mtile_y = hover_mtile / edit_map.GetMapBasics().width;

		switch(current_mode)
		{
			case PLACE_TILE_MODE:
				//selected tile ok?
				//if(current_ptile != -1)
				if(current_ptile_list.size())
				{
					//where we coming from?
					//ZMap::GetPaletteTile(current_ptile,x,y);
					ZMap::GetPaletteTile(*current_ptile_list.begin(),x,y);
					from_rect.x = x;
					from_rect.y = y;
					from_rect.w = 16;
					from_rect.h = 16;
					
					//blit!
					//SDL_BlitSurface(ZMap::GetMapPalette((planet_type)edit_map.GetMapBasics().terrain_type), &from_rect, screen, &to_rect);
					ZMap::GetMapPalette((planet_type)edit_map.GetMapBasics().terrain_type).BlitSurface(&from_rect, &to_rect);
				}
				break;
			case PLACE_BUILDING_MODE:
				switch(current_object)
				{
					case FORT_FRONT:
						bfort_front->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						bfort_front->SetCords(mtile_x*16,mtile_y*16);
						bfort_front->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(bfort_front.GetRender(), NULL, screen, &to_rect);
						break;
					case FORT_BACK:
						bfort_back->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						bfort_back->SetCords(mtile_x*16,mtile_y*16);
						bfort_back->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(bfort_back.GetRender(), NULL, screen, &to_rect);
						break;
					case RADAR:
						bradar->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						bradar->SetCords(mtile_x*16,mtile_y*16);
						bradar->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(bradar.GetRender(), NULL, screen, &to_rect);
						break;
					case REPAIR:
						brepair->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						brepair->SetCords(mtile_x*16,mtile_y*16);
						brepair->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(brepair.GetRender(), NULL, screen, &to_rect);
						break;
					case ROBOT_FACTORY:
						brobot->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						brobot->SetCords(mtile_x*16,mtile_y*16);
						brobot->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(brobot.GetRender(), NULL, screen, &to_rect);
						break;
					case VEHICLE_FACTORY:
						bvehicle->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						bvehicle->SetCords(mtile_x*16,mtile_y*16);
						bvehicle->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(bvehicle.GetRender(), NULL, screen, &to_rect);
						break;
					case BRIDGE_VERT:
						bbridge_vert->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						bbridge_vert->SetCords(mtile_x*16,mtile_y*16);
						bbridge_vert->DoRender(edit_map, screen, MAP_SHIFT_X);
						break;
					case BRIDGE_HORZ:
						bbridge_horz->GetDimensions(width, height);
						if(width + mtile_x > edit_map.GetMapBasics().width) break;
						if(height + mtile_y > edit_map.GetMapBasics().height) break;
						
						bbridge_horz->SetCords(mtile_x*16,mtile_y*16);
						bbridge_horz->DoRender(edit_map, screen, MAP_SHIFT_X);
						break;
				}
				break;
			case PLACE_CANNON_MODE:
				switch(current_object)
				{
					case GATLING:
						cgatling->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						cgatling->SetCords(mtile_x*16,mtile_y*16);
						cgatling->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(cgatling.GetRender(), NULL, screen, &to_rect);
						break;
					case GUN:
						cgun->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						cgun->SetCords(mtile_x*16,mtile_y*16);
						cgun->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(cgun.GetRender(), NULL, screen, &to_rect);
						break;
					case HOWITZER:
						chowitzer->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						chowitzer->SetCords(mtile_x*16,mtile_y*16);
						chowitzer->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(chowitzer.GetRender(), NULL, screen, &to_rect);
						break;
					case MISSILE_CANNON:
						cmissilecannon->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						cmissilecannon->SetCords(mtile_x*16,mtile_y*16);
						cmissilecannon->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(cmissilecannon.GetRender(), NULL, screen, &to_rect);
						break;
				}
				break;
			case PLACE_VEHICLE_MODE:
				switch(current_object)
				{
					case JEEP:
						vjeep->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						vjeep->SetCords(mtile_x*16,mtile_y*16);
						vjeep->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(vjeep.GetRender(), NULL, screen, &to_rect);
						break;
					case LIGHT:
						vlight->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						vlight->SetCords(mtile_x*16,mtile_y*16);
						vlight->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(vlight.GetRender(), NULL, screen, &to_rect);
						break;
					case MEDIUM:
						vmedium->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						vmedium->SetCords(mtile_x*16,mtile_y*16);
						vmedium->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(vmedium.GetRender(), NULL, screen, &to_rect);
						break;
					case HEAVY:
						vheavy->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						vheavy->SetCords(mtile_x*16,mtile_y*16);
						vheavy->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(vheavy.GetRender(), NULL, screen, &to_rect);
						break;
					case APC:
						vapc->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						vapc->SetCords(mtile_x*16,mtile_y*16);
						vapc->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(vapc.GetRender(), NULL, screen, &to_rect);
						break;
					case MISSILE_LAUNCHER:
						vmissilelauncher->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						vmissilelauncher->SetCords(mtile_x*16,mtile_y*16);
						vmissilelauncher->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(vmissilelauncher.GetRender(), NULL, screen, &to_rect);
						break;
					case CRANE:
						vcrane->GetDimensionsPixel(width, height);
						if(width + (mtile_x * 16) > edit_map.GetMapBasics().width * 16) break;
						if(height + (mtile_y * 16) > edit_map.GetMapBasics().height * 16) break;
						vcrane->SetCords(mtile_x*16,mtile_y*16);
						vcrane->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(vcrane.GetRender(), NULL, screen, &to_rect);
						break;
				}
				break;
			case PLACE_ROBOT_MODE:
				switch(current_object)
				{
// 					case GRUNT:
					default: //they are all the same
						rgrunt->SetCords(mtile_x*16,mtile_y*16);
						rgrunt->DoRender(edit_map, screen, MAP_SHIFT_X);
// 						SDL_BlitSurface(rgrunt.GetRender(), NULL, screen, &to_rect);
						break;
				}
				break;
			case PLACE_ITEM_MODE:
				switch(current_object)
				{
					case FLAG_ITEM:
						oflag->SetCords(mtile_x*16,mtile_y*16);
						oflag->DoRender(edit_map, screen, MAP_SHIFT_X);
						break;
					case ROCK_ITEM:
						orock->SetCords(mtile_x*16,mtile_y*16);
						orock->DoRender(edit_map, screen, MAP_SHIFT_X);
						break;
					case GRENADES_ITEM:
						ogrenades->SetCords(mtile_x*16,mtile_y*16);
						ogrenades->DoRender(edit_map, screen, MAP_SHIFT_X);
						break;
					case ROCKETS_ITEM:
						orockets->SetCords(mtile_x*16,mtile_y*16);
						orockets->DoRender(edit_map, screen, MAP_SHIFT_X);
						break;
					case HUT_ITEM:
						ohut->SetCords(mtile_x*16,mtile_y*16);
						ohut->DoRender(edit_map, screen, MAP_SHIFT_X);
						break;
				}

				if(current_object >= MAP0_ITEM && current_object <= MAP21_ITEM)
				{
					omapobject->SetCords(mtile_x*16,mtile_y*16);
					omapobject->DoRender(edit_map, screen, MAP_SHIFT_X);
				}

				break;
			case PLACE_ZONE_MODE:
// 				printf("%d %d %d %d\n", new_zone.x, new_zone.y, new_zone.w, new_zone.h);
				if(new_zone.x == -1) break;
				
				if(current_time() - last_nz_time > 0.3)
				{
					last_nz_time = current_time();
					nz_io++;
					if(nz_io >= MAX_TEAM_TYPES) nz_io = 0;
				}
				
				nz_i = nz_io;
				
				for(int i=0;i<new_zone.w;i++, nz_i++)
				{
					if(nz_i >= MAX_TEAM_TYPES) nz_i = 0;

					x = (new_zone.x + i) * 16 + 6;
					y = new_zone.y * 16 + 6;

					edit_map.RenderZSurface(&ZMap::GetZoneMarkers()[nz_i], x, y);
					//if(edit_map.GetBlitInfo(ZMap::GetZoneMarkers()[nz_i], x, y, from_rect, to_rect))
					//{
					//	to_rect.x += MAP_SHIFT_X;
					//	SDL_BlitSurface( ZMap::GetZoneMarkers()[nz_i], &from_rect, screen, &to_rect);
					//}

					x = (new_zone.x + i) * 16 + 6;
					y = (new_zone.y + (new_zone.h - 1)) * 16 + 6;

					edit_map.RenderZSurface(&ZMap::GetZoneMarkers()[nz_i], x, y);
					//if(edit_map.GetBlitInfo(ZMap::GetZoneMarkers()[nz_i], x, y, from_rect, to_rect))
					//{
					//	to_rect.x += MAP_SHIFT_X;
					//	SDL_BlitSurface( ZMap::GetZoneMarkers()[nz_i], &from_rect, screen, &to_rect);
					//}
					
					//to_rect.x = (new_zone.x + i) * 16 + MAP_SHIFT_X + 6;
					//to_rect.y = new_zone.y * 16 + 6;
					//
					//SDL_BlitSurface(ZMap::GetZoneMarkers()[nz_i], NULL, screen, &to_rect);
					//
					//to_rect.x = (new_zone.x + i) * 16 + MAP_SHIFT_X + 6;
					//to_rect.y = (new_zone.y + (new_zone.h - 1)) * 16 + 6;
					//
					//SDL_BlitSurface(ZMap::GetZoneMarkers()[nz_i], NULL, screen, &to_rect);
				}
				for(int i=0;i<new_zone.h;i++, nz_i++) 
				{
					if(nz_i >= MAX_TEAM_TYPES) nz_i = 0;
					
					x = new_zone.x * 16 + 6;
					y = (new_zone.y + i) * 16 + 6;

					edit_map.RenderZSurface(&ZMap::GetZoneMarkers()[nz_i], x, y);
					//if(edit_map.GetBlitInfo(ZMap::GetZoneMarkers()[nz_i], x, y, from_rect, to_rect))
					//{
					//	to_rect.x += MAP_SHIFT_X;
					//	SDL_BlitSurface( ZMap::GetZoneMarkers()[nz_i], &from_rect, screen, &to_rect);
					//}

					x = (new_zone.x + (new_zone.w - 1)) * 16 + 6;
					y = (new_zone.y + i) * 16 + 6;

					edit_map.RenderZSurface(&ZMap::GetZoneMarkers()[nz_i], x, y);
					//if(edit_map.GetBlitInfo(ZMap::GetZoneMarkers()[nz_i], x, y, from_rect, to_rect))
					//{
					//	to_rect.x += MAP_SHIFT_X;
					//	SDL_BlitSurface( ZMap::GetZoneMarkers()[nz_i], &from_rect, screen, &to_rect);
					//}

					//to_rect.x = new_zone.x * 16 + MAP_SHIFT_X + 6;
					//to_rect.y = (new_zone.y + i) * 16 + 6;
					//
					//SDL_BlitSurface(ZMap::GetZoneMarkers()[nz_i], NULL, screen, &to_rect);
					//
					//to_rect.x = (new_zone.x + (new_zone.w - 1)) * 16 + MAP_SHIFT_X + 6;
					//to_rect.y = (new_zone.y + i) * 16 + 6;
					//
					//SDL_BlitSurface(ZMap::GetZoneMarkers()[nz_i], NULL, screen, &to_rect);
				}
				
				break;
		}
	}

	//draw zones if we should
	if(current_mode == REMOVE_ZONE_MODE)
		draw_zones();

	if(current_map_ruler != NO_RULER) draw_map_ruler();

	draw_hover_building_info();
	
	//if(flip) SDL_Flip(screen);
}

void draw_zones()
{
	int shift_x, shift_y;

	edit_map.GetViewShift(shift_x, shift_y);

	for(vector<map_zone_info>::iterator i=edit_map.GetZoneInfoList().begin(); i!=edit_map.GetZoneInfoList().end(); i++)
	{
		SDL_Rect the_box;
		SDL_Color the_color;

		the_color.r = 250;
		the_color.g = 0;
		the_color.b = 0;

		the_box.x = MAP_SHIFT_X + i->x - shift_x;
		the_box.y = i->y - shift_y;
		the_box.w = 16;
		the_box.h = 16;

		if(the_box.x < MAP_SHIFT_X) continue;

		draw_box(screen, the_box, the_color, MAP_SHIFT_X + screen->w, screen->h);
	}
}

// The lower sidebar is reflowed with the window's height; map view starts
// at MAP_SHIFT_X (336) and is never touched by these panel decorations.
// Make the shortcuts panel taller when the editor window has spare height.
// At 1280x720 retain the previous height so the minimap stays usable.
int editor_controls_height()
{
	int extra = (screen->h - 720) / 4;
	if(extra < 0) extra = 0;
	if(extra > EDITOR_CONTROLS_MAX_EXTRA_H) extra = EDITOR_CONTROLS_MAX_EXTRA_H;
	return EDITOR_CONTROLS_H + extra;
}

int editor_controls_top()
{
	return screen->h - EDITOR_BOTTOM_MARGIN - editor_controls_height();
}

int editor_status_top()
{
	// Fixed-height status panel: switching editor mode never shifts the boxes.
	return editor_controls_top() - EDITOR_STATUS_H - EDITOR_GAP;
}

void layout_editor_minimap()
{
	int mini_height = editor_status_top() - EDITOR_GAP - MINIMAP_Y - 4;
	if(mini_height < 8) mini_height = 8;
	edit_minimap.SetSize(EDITOR_MINIMAP_W, mini_height);
}

void ui_fill(int x, int y, int w, int h, int r, int g, int b)
{
	if(!screen || w <= 0 || h <= 0) return;
	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;
	SDL_FillRect(screen, &rect, SDL_MapRGB(screen->format, r, g, b));
}

void ui_outline(int x, int y, int w, int h, int r, int g, int b)
{
	if(w <= 1 || h <= 1) return;
	ui_fill(x, y, w, 1, r, g, b);
	ui_fill(x, y + h - 1, w, 1, r, g, b);
	ui_fill(x, y, 1, h, r, g, b);
	ui_fill(x + w - 1, y, 1, h, r, g, b);
}

void ui_text(TTF_Font *font, const char *message, int x, int y, int r, int g, int b)
{
	if(!screen || !message) return;
	if(!font) font = ttf_font;
	if(!font) return;

	SDL_Color color;
	color.r = r;
	color.g = g;
	color.b = b;
	color.unused = 0;
	SDL_Surface *text = TTF_RenderText_Solid(font, message, color);
	if(!text) return;

	// Long object names must not draw over the map or separator.
	SDL_Rect old_clip;
	SDL_GetClipRect(screen, &old_clip);
	SDL_Rect side_clip;
	side_clip.x = 11;
	side_clip.y = PALETTE_BOTTOM;
	side_clip.w = SIDEBAR_WIDTH - 23;
	side_clip.h = screen->h > PALETTE_BOTTOM ? screen->h - PALETTE_BOTTOM : 0;
	SDL_SetClipRect(screen, &side_clip);
	SDL_Rect destination;
	destination.x = x;
	destination.y = y;
	SDL_BlitSurface(text, NULL, screen, &destination);
	SDL_SetClipRect(screen, &old_clip);
	SDL_FreeSurface(text);
}

void draw_editor_shortcut(int x, int y, const char *key, const char *label, int key_width)
{
	ui_fill(x, y, key_width, 15, 43, 47, 53);
	ui_outline(x, y, key_width, 15, 134, 145, 156);
	ui_text(editor_small_font, key, x + 4, y, 255, 209, 43);
	// Only the white descriptions are larger and easier to read.
	ui_text(editor_font, label, x + key_width + 5, y - 1, 250, 251, 252);
}

void draw_editor_shortcuts(int panel_top)
{
	const int gold_r = 255, gold_g = 209, gold_b = 43;
	const int panel_bottom = screen->h - EDITOR_BOTTOM_MARGIN;
	const int panel_height = panel_bottom - panel_top;
	if(panel_height <= 4) return;

	ui_fill(9, panel_top, 302, panel_height, 18, 21, 26);
	ui_outline(9, panel_top, 302, panel_height, 141, 150, 157);
	// No heading. This is a fixed guide (not mode-specific).
	ui_fill(11, panel_top + 2, 298, 2, gold_r, gold_g, gold_b);

	// Seven rows, two columns. N and M are the back/forward mode keys.
	// The new O/S row preserves every previous shortcut, with Undo/Redo last.
	const int row0 = panel_top + 8;
	int row_gap = (panel_height - 30) / 6;
	if(row_gap < 16) row_gap = 16;
	for(int row = 0; row < 7; row++)
	{
		const int y = row0 + row * row_gap;
		if(y + 15 > panel_bottom) break;
		switch(row)
		{
			case 0:
				draw_editor_shortcut(18, y, "N", "Mode back", 20);
				draw_editor_shortcut(163, y, "M", "Mode forward", 20);
				break;
			case 1:
				draw_editor_shortcut(18, y, "O", "Object", 20);
				draw_editor_shortcut(163, y, "S", "Save", 20);
				break;
			case 2:
				draw_editor_shortcut(18, y, "T", "Team", 20);
				draw_editor_shortcut(163, y, "L", "Level", 20);
				break;
			case 3:
				draw_editor_shortcut(18, y, "R", "Grid/Ruler", 20);
				draw_editor_shortcut(163, y, "Arrows", "Pan", 43);
				break;
			case 4:
				// Danish keyboard users reported the health keys as nonfunctional:
				// do not advertise them until the input mapping is verified.
				draw_editor_shortcut(18, y, ",", "Shorter bridge", 20);
				draw_editor_shortcut(163, y, ".", "Longer bridge", 20);
				break;
			case 5:
				draw_editor_shortcut(18, y, "Left click", "Paint", 66);
				draw_editor_shortcut(163, y, "Right click", "Remove", 66);
				break;
			case 6:
				draw_editor_shortcut(18, y, "Ctrl+Z", "Undo", 47);
				draw_editor_shortcut(163, y, "Ctrl+Shift+Z", "Redo", 89);
				break;
		}
	}

}

// The four minimap readouts form one left-aligned column. Their left
// margin stays put regardless of the currently hovered object/value.
int editor_text_width(TTF_Font *font, const char *text)
{
	if(!font || !text) return 0;
	int width = 0, height = 0;
	if(TTF_SizeText(font, text, &width, &height)) return 0;
	return width;
}

// Use compact building names in the narrow sidebar only. The map tooltip
// retains the full name and the inspected building's saved level.
string editor_sidebar_object_name(unsigned char type, unsigned char id, const string &name)
{
	if(type == BUILDING_OBJECT)
	{
		switch(id)
		{
			case FORT_FRONT: return "Fort front";
			case FORT_BACK: return "Fort back";
			case VEHICLE_FACTORY: return "Vehicle";
			case ROBOT_FACTORY: return "Robot";
			case REPAIR: return "Repair";
			case RADAR: return "Radar";
			case BRIDGE_VERT: return "Bridge V";
			case BRIDGE_HORZ: return "Bridge H";
		}
	}
	if(type == CANNON_OBJECT && id == MISSILE_CANNON) return "Missile";
	if(type == VEHICLE_OBJECT && id == MISSILE_LAUNCHER) return "Missile";
	return name;
}

// Keep each label/value on one line, inside the readout column. Use the
// small font for the whole row if necessary, not just the value.
void editor_left_pair(TTF_Font *font, const char *label, const char *value,
                      int x, int y, int max_width)
{
	if(!font) font = editor_font ? editor_font : ttf_font;
	if(!font || max_width <= 0) return;
	const int gap = 3;
	if(editor_text_width(font, label) + gap + editor_text_width(font, value) > max_width && editor_small_font)
		font = editor_small_font;
	const int label_w = editor_text_width(font, label);
	if(label_w > max_width) return;
	ui_text(font, label, x, y, 255, 209, 43);
	const int available = max_width - label_w - gap;
	if(available <= 0) return;
	string visible = value;
	if(editor_text_width(font, visible.c_str()) > available)
	{
		if(editor_text_width(font, "...") > available) return;
		while(!visible.empty() && editor_text_width(font, (visible + "...").c_str()) > available)
			visible.erase(visible.size() - 1);
		visible += "...";
	}
	ui_text(font, visible.c_str(), x + label_w + gap, y, 241, 244, 245);
}

void draw_info(bool flip)
{
	if(!screen || screen->h <= PALETTE_BOTTOM) return;

	// No global minimap dimensions are changed: this panel has its own size.
	const int status_top = editor_status_top();
	const int mini_box_bottom = status_top - EDITOR_GAP;
	const int gold_r = 255, gold_g = 209, gold_b = 43;
	char message[500];
	char object_name[500];

	// Wipe ONLY the sidebar footer, never the map or the tile palette.
	ui_fill(0, PALETTE_BOTTOM, SIDEBAR_WIDTH, screen->h - PALETTE_BOTTOM, 8, 11, 15);
	ui_fill(2, PALETTE_BOTTOM + 1, 316, 2, 237, 181, 31);

	// Resolve the hovered map object once. Its row is always visible, even
	// when no object is under the cursor.
	snprintf(message, sizeof(message), "-");
	if(hover_object)
	{
		unsigned char object_type, object_id;
		hover_object->GetObjectID(object_type, object_id);
		snprintf(message, sizeof(message), "%s",
		         editor_sidebar_object_name(object_type, object_id, hover_object->GetObjectName()).c_str());
	}

	// Keep the editor minimap as a simple, clickable zone/navigation overview.
	if(status_top > MINIMAP_Y + 18)
	{
		ui_fill(8, 389, 304, mini_box_bottom - 389, 19, 23, 28);
		ui_outline(8, 389, 304, mini_box_bottom - 389, 124, 137, 148);
		ui_fill(10, 391, 300, 2, gold_r, gold_g, gold_b);
		edit_minimap.DoRender(screen, MINIMAP_X, MINIMAP_Y);

		// One fixed left edge for all four rows, slightly left of V13.
		// Keep the column centered as a group, never center individual labels.
		const int readout_left = 200;
		const int readout_width = 108; // 200..308, inside the sidebar
		TTF_Font *readout_font = editor_status_font ? editor_status_font : editor_font;

		// Equal vertical spacing between Object, Health, Level and Bridge links.
		// At 720p the minimap footer is shorter, so reduce ALL gaps equally.
		int row_gap = (mini_box_bottom - MINIMAP_Y - 33) / 3;
		if(row_gap > 28) row_gap = 28;
		if(row_gap < 19) row_gap = 19;
		const int block_height = 7 + 3 * row_gap + 17;
		int block_top = MINIMAP_Y + (edit_minimap.GetHeight() - block_height) / 2 - 11;
		if(block_top < MINIMAP_Y + 2) block_top = MINIMAP_Y + 2;
		if(block_top + block_height > mini_box_bottom - 3)
			block_top = mini_box_bottom - 3 - block_height;
		if(block_top < MINIMAP_Y + 2) block_top = MINIMAP_Y + 2;

		const int object_y = block_top + 7;
		const int health_y = object_y + row_gap;
		const int level_y = health_y + row_gap;
		const int bridge_y = level_y + row_gap;

		editor_left_pair(readout_font, "Object:", message, readout_left, object_y, readout_width);

		// These are placement settings, not the inspected object's attributes.
		// The latter remain available in the map tooltip (including level).
		char value[32];
		snprintf(value, sizeof(value), "%d%%", current_health_percent);
		editor_left_pair(readout_font, "Health:", value, readout_left, health_y, readout_width);
		snprintf(value, sizeof(value), "%d", current_blevel + 1);
		editor_left_pair(readout_font, "Level:", value, readout_left, level_y, readout_width);
		snprintf(value, sizeof(value), "%d", current_extra_links);
		editor_left_pair(readout_font, "Bridge links:", value, readout_left, bridge_y, readout_width);
	}

	// Compact status panel: mode, object and team only. The save shortcut
	// has moved into the taller controls panel below.
	const int status_box_height = EDITOR_STATUS_H;
	ui_fill(8, status_top, 304, status_box_height, 19, 23, 28);
	ui_outline(8, status_top, 304, status_box_height, 125, 137, 149);
	ui_fill(11, status_top + 2, 298, 2, gold_r, gold_g, gold_b);

	TTF_Font *middle_font = editor_info_font ? editor_info_font : editor_font;
	const int label_x = 17;
	const int value_x = 149;
	const int row_y[3] = {status_top + 7, status_top + 28,
	                      status_top + 49};
	ui_text(middle_font, "Current Mode:", label_x, row_y[0], gold_r, gold_g, gold_b);
	ui_text(middle_font, map_editor_mode_string[current_mode].c_str(), value_x, row_y[0], 241, 244, 245);

	// Initialize object text in zone/remove modes, which lack a selection.
	snprintf(object_name, sizeof(object_name), "none");
	switch(current_mode)
	{
		case PLACE_TILE_MODE: snprintf(object_name, sizeof(object_name), "Palette Tile of %d", (int)current_ptile_list.size()); break;
		case PLACE_VEHICLE_MODE: snprintf(object_name, sizeof(object_name), "%s", vehicle_type_string[current_object].c_str()); break;
		case PLACE_CANNON_MODE: snprintf(object_name, sizeof(object_name), "%s", cannon_type_string[current_object].c_str()); break;
		case PLACE_ROBOT_MODE: snprintf(object_name, sizeof(object_name), "%s", robot_type_string[current_object].c_str()); break;
		case PLACE_ITEM_MODE: snprintf(object_name, sizeof(object_name), "%s", item_type_string[current_object].c_str()); break;
		case PLACE_BUILDING_MODE: snprintf(object_name, sizeof(object_name), "%s", building_type_string[current_object].c_str()); break;
		default: break;
	}
	ui_text(middle_font, "Current Object:", label_x, row_y[1], gold_r, gold_g, gold_b);
	ui_text(middle_font, object_name, value_x, row_y[1], 241, 244, 245);
	ui_text(middle_font, "Current Team:", label_x, row_y[2], gold_r, gold_g, gold_b);
	ui_text(middle_font, team_type_string[current_team].c_str(), value_x, row_y[2], 241, 244, 245);


	// Equal 8-pixel gaps above and below the fixed-height status panel.
	const int controls_panel_top = editor_controls_top();
	// The shortcut guide never changes based on the current mode.
	draw_editor_shortcuts(controls_panel_top);
	// The surrounding caller controls SDL_Flip, as before.
}

void draw_selection_box(int x, int y, int w, int h)
{
	
	SDL_Color marker_color;
	
	marker_color.r = 255;
	marker_color.g = 0;
	marker_color.b = 0;
	
	//draw the selection box
	for(int i=0;i<w;i++) if(x+i >= MAP_SHIFT_X)
	{
		put32pixel(screen, x+i, y, marker_color);
		put32pixel(screen, x+i, y+(h-1), marker_color);
	}
	for(int i=0;i<h;i++) if(x >= MAP_SHIFT_X) put32pixel(screen, x, y+i, marker_color);
	for(int i=0;i<h;i++) put32pixel(screen, x+(w-1), y+i, marker_color);
}

void draw_selection_box(int tile)
{
	int x, y;
	
	if(tile == -1) return;
	
	SDL_Color marker_color;
	
	marker_color.r = 0;
	marker_color.g = 255;
	marker_color.b = 255;
	
	ZMap::GetPaletteTile(tile,x,y);
	
	//draw the selection box
	for(int i=0;i<16;i++) put32pixel(screen, x+i, y, marker_color);
	for(int i=0;i<16;i++) put32pixel(screen, x, y+i, marker_color);
	for(int i=0;i<16;i++) put32pixel(screen, x+15, y+i, marker_color);
	for(int i=0;i<16;i++) put32pixel(screen, x+i, y+15, marker_color);
}

void draw_x_marker(int tile)
{
	int x,y;
	
	if(tile == -1) return;
	
	SDL_Color x_color;
		
	ZMap::GetPaletteTile(tile,x,y);
		
	x_color.r = 255;
	x_color.g = 0;
	x_color.b = 0;
		
	for(int i=0;i<16;i++)
	{
		put32pixel(screen, x+i, y+i, x_color);
		put32pixel(screen, x+15-i, y+i, x_color);
	}
}

void set_placement_objects_team(enum team_type team)
{
	bfort_front->SetOwner(team);
	bfort_back->SetOwner(team);
	brepair->SetOwner(team);
	bradar->SetOwner(team);
	brobot->SetOwner(team);
	bvehicle->SetOwner(team);
	bbridge_vert->SetOwner(team);
	bbridge_horz->SetOwner(team);
	oflag->SetOwner(team);
	cgatling->SetOwner(team);
	cgun->SetOwner(team);
	chowitzer->SetOwner(team);
	cmissilecannon->SetOwner(team);
	vjeep->SetOwner(team);
	vlight->SetOwner(team);
	vmedium->SetOwner(team);
	vheavy->SetOwner(team);
	vapc->SetOwner(team);
	vmissilelauncher->SetOwner(team);
	vcrane->SetOwner(team);
	rgrunt->SetOwner(team);
	rpsycho->SetOwner(team);
	rsniper->SetOwner(team);
	rtough->SetOwner(team);
	rpyro->SetOwner(team);
	rlaser->SetOwner(team);
}

void set_placement_objects_stamp()
{
	bfort_front->DontStamp(true);
	bfort_back->DontStamp(true);
	brepair->DontStamp(true);
	bradar->DontStamp(true);
	brobot->DontStamp(true);
	bvehicle->DontStamp(true);
	bbridge_vert->DontStamp(true);
	bbridge_horz->DontStamp(true);
}

void set_placement_objects_blevel(int blevel)
{
	bfort_front->SetLevel(blevel);
	bfort_back->SetLevel(blevel);
	brepair->SetLevel(blevel);
	bradar->SetLevel(blevel);
	brobot->SetLevel(blevel);
	bvehicle->SetLevel(blevel);
	bbridge_vert->SetLevel(blevel);
	bbridge_horz->SetLevel(blevel);
}

// Shared read-only hit-test for hover info and right-click deletion.
// Use the existing Remove Object rectangle rule (full pixel dimensions),
// not just the object's origin tile or the active placement mode.
ZObject *object_at_map_screen_position(int x, int y)
{
	if(!screen || x < MAP_SHIFT_X || x >= screen->w ||
	   y < 0 || y >= screen->h ||
	   edit_map.GetTileIndex(x - MAP_SHIFT_X, y, true) == -1)
		return NULL;

	int sx, sy;
	edit_map.GetViewShift(sx, sy);
	const int mx = x - MAP_SHIFT_X + sx;
	const int my = y + sy;
	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); ++i)
	{
		int ox, oy, w, h;
		(*i)->GetCords(ox, oy);
		(*i)->GetDimensionsPixel(w, h);
		if(mx >= ox && mx < ox + w && my >= oy && my < oy + h)
			return *i;
	}
	return NULL;
}

int check_hover_object()
{
	ZObject *next_hover = NULL;
	if(screen && (SDL_GetAppState() & SDL_APPMOUSEFOCUS))
		next_hover = object_at_map_screen_position(mouse_x, mouse_y);

	if(next_hover == hover_object) return 0;
	hover_object = next_hover;
	return 1;
}

// A building's saved level is distinct from current_blevel (the level of
// the NEXT building to be placed). Keep those placement controls intact
// and show the inspected building's own data in a small map tooltip.
void draw_hover_building_info()
{
	if(!screen || !hover_object) return;
	unsigned char type, id;
	hover_object->GetObjectID(type, id);
	if(type != BUILDING_OBJECT) return;

	TTF_Font *font = editor_status_font ? editor_status_font : ttf_font;
	if(!font) return;
	string name = hover_object->GetObjectName();
	if(id == FORT_FRONT) name = "Fort (front)";
	else if(id == FORT_BACK) name = "Fort (back)";
	else for(size_t n = 0; n < name.size(); ++n)
		if(name[n] == '_') name[n] = ' ';
	char text[256];
	snprintf(text, sizeof(text), "%s  |  Level: %d",
	         name.c_str(), hover_object->GetLevel() + 1);
	SDL_Color color = {241, 244, 245, 0};
	SDL_Surface *label = TTF_RenderText_Solid(font, text, color);
	if(!label) return;

	const int w = label->w + 16;
	const int h = label->h + 12;
	if(w > screen->w - MAP_SHIFT_X - 8 || h > screen->h - 8)
	{
		SDL_FreeSurface(label);
		return;
	}
	int x = mouse_x + 18;
	int y = mouse_y + 22;
	if(x + w > screen->w - 4) x = mouse_x - 12 - w;
	if(x < MAP_SHIFT_X + 4) x = MAP_SHIFT_X + 4;
	if(y + h > screen->h - 4) y = mouse_y - 12 - h;
	if(y < 4) y = 4;
	ui_fill(x, y, w, h, 19, 23, 28);
	ui_outline(x, y, w, h, 255, 209, 43);
	SDL_Rect dst;
	dst.x = x + 8;
	dst.y = y + 6;
	SDL_BlitSurface(label, NULL, screen, &dst);
	SDL_FreeSurface(label);
}

bool object_exists_at(int x, int y)
{
	for(vector<map_object>::iterator i=edit_map.GetObjectList().begin(); i!=edit_map.GetObjectList().end(); i++)
		if(i->x == x && i->y == y)
			return true;

	return false;
}

void place_object(map_object new_object)
{
	if(object_exists_at(new_object.x, new_object.y)) return;

	//for(vector<map_object>::iterator i=edit_map.GetObjectList().begin(); i!=edit_map.GetObjectList().end(); i++)
	//	if(i->x == new_object.x && i->y == new_object.y)
	//		return;
	
	edit_map.PlaceObject(new_object);
	load_object(new_object);

	//arg
	if(new_object.object_type == MAP_ITEM_OBJECT && new_object.object_id == ROCK_ITEM)
		ORock::SetupRockRenders(edit_map, object_list);
}

void load_object(map_object new_object)
{
	ZObject *new_object_ptr = NULL;
	
	switch(new_object.object_type)
	{
		case BUILDING_OBJECT:
			switch(new_object.object_id)
			{
				case FORT_FRONT:
					new_object_ptr = new BFort(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type, true);
					break;
				case FORT_BACK:
					new_object_ptr = new BFort(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type, false);
					break;
				case RADAR:
					new_object_ptr = new BRadar(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type);
					break;
				case REPAIR:
					new_object_ptr = new BRepair(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type);
					break;
				case ROBOT_FACTORY:
					new_object_ptr = new BRobot(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type);
					break;
				case VEHICLE_FACTORY:
					new_object_ptr = new BVehicle(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type);
					break;
				case BRIDGE_VERT:
					new_object_ptr = new BBridge(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type, true, new_object.extra_links);
					break;
				case BRIDGE_HORZ:
					new_object_ptr = new BBridge(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type, false, new_object.extra_links);
					break;
			}
			break;
		case CANNON_OBJECT:
			switch(new_object.object_id)
			{
				case GATLING:
					new_object_ptr = new CGatling(&ztime, NULL, new_object.owner!=NULL_TEAM);
					break;
				case GUN:
					new_object_ptr = new CGun(&ztime, NULL, new_object.owner!=NULL_TEAM);
					break;
				case HOWITZER:
					new_object_ptr = new CHowitzer(&ztime, NULL, new_object.owner!=NULL_TEAM);
					break;
				case MISSILE_CANNON:
					new_object_ptr = new CMissileCannon(&ztime, NULL, new_object.owner!=NULL_TEAM);
					break;
			}
			break;
		case VEHICLE_OBJECT:
			switch(new_object.object_id)
			{
				case JEEP:
					new_object_ptr = new VJeep(&ztime);
					break;
				case LIGHT:
					new_object_ptr = new VLight(&ztime);
					break;
				case MEDIUM:
					new_object_ptr = new VMedium(&ztime);
					break;
				case HEAVY:
					new_object_ptr = new VHeavy(&ztime);
					break;
				case APC:
					new_object_ptr = new VAPC(&ztime);
					break;
				case MISSILE_LAUNCHER:
					new_object_ptr = new VMissileLauncher(&ztime);
					break;
				case CRANE:
					new_object_ptr = new VCrane(&ztime);
					break;
			}
			
			//for fun
			if(new_object_ptr)
				new_object_ptr->SetDirection(rand()%MAX_ANGLE_TYPES);
			break;
		case ROBOT_OBJECT:
			switch(new_object.object_id)
			{
				case GRUNT:
					new_object_ptr = new RGrunt(&ztime);
					break;
				case PSYCHO:
					new_object_ptr = new RPsycho(&ztime);
					break;
				case SNIPER:
					new_object_ptr = new RSniper(&ztime);
					break;
				case TOUGH:
					new_object_ptr = new RTough(&ztime);
					break;
				case PYRO:
					new_object_ptr = new RPyro(&ztime);
					break;
				case LASER:
					new_object_ptr = new RLaser(&ztime);
					break;
			}
			break;
		case MAP_ITEM_OBJECT:
			switch(new_object.object_id)
			{
				case FLAG_ITEM:
					new_object_ptr = new OFlag(&ztime);
					break;
				case ROCK_ITEM:
					new_object_ptr = new ORock(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type);
					break;
				case GRENADES_ITEM:
					new_object_ptr = new OGrenades(&ztime);
					break;
				case ROCKETS_ITEM:
					new_object_ptr = new ORockets(&ztime);
					break;
				case HUT_ITEM:
					new_object_ptr = new OHut(&ztime, NULL, (planet_type)edit_map.GetMapBasics().terrain_type);
					break;
			}

			if(new_object.object_id >= MAP0_ITEM && new_object.object_id <= MAP21_ITEM)
				new_object_ptr = new OMapObject(&ztime, NULL, new_object.object_id);

			break;
	}
	
	if(new_object_ptr) 
	{
		new_object_ptr->SetRefID(next_ref_id++);
		new_object_ptr->SetOwner((team_type)new_object.owner);
		new_object_ptr->SetCords(new_object.x*16,new_object.y*16);
		new_object_ptr->SetLevel(new_object.blevel);
		new_object_ptr->SetHealthPercent(new_object.health_percent, edit_map);
		new_object_ptr->DontStamp(true);

		object_list.push_back(new_object_ptr);
	}
}
