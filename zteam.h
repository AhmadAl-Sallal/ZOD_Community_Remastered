#ifndef _ZTEAM_H_
#define _ZTEAM_H_

#include "constants.h"
#include "zsdl.h"
#include "zsdl_opengl.h"
#include <vector>
#include <string>

using namespace std;

#define ZTEAM_BASE_TEAM RED_TEAM

extern SDL_Color team_color[MAX_TEAM_TYPES];

#define ZTEAM_PALETTE_MAX 16

class ZTeam_Palette
{
public:
	bool LoadSurfacePalette(SDL_Surface *surface);
	bool SaveSurfacePalette(string filename);
	bool AddColor(SDL_Color &bc, SDL_Color &rc);
	bool GetReplacement(Uint8 &r, Uint8 &g, Uint8 &b, SDL_Color &c);

	//vector<SDL_Color> base_color;
	//vector<SDL_Color> replace_color;
	SDL_Color base_color[ZTEAM_PALETTE_MAX];
	SDL_Color replace_color[ZTEAM_PALETTE_MAX];
};

class ZTeam
{
public:
	static void Init();
	static void Setup_team_color();

	static void LoadPalette(int team);
	static void SavePalette(int team);
	static void SaveAllPalettes();
	static void AppendPalette(int team, SDL_Surface *base_version, SDL_Surface *this_version);
	static void LoadZSurface(int team, ZSDL_Surface &bv, ZSDL_Surface &rv, string filename);

	static SDL_Surface *Make(int team, SDL_Surface *base_version);

    // New opt-in path for neutral RGBA sprites with an explicit alpha mask.
    // Only masked pixels are tinted; everything else (pole, shadow, etc.) stays intact.
    // Caller owns the returned SDL_Surface. Legacy Make() is unchanged.
    static SDL_Surface *MakeNeutralTint(int team, SDL_Surface *neutral, SDL_Surface *mask);

    // Robot sprites can have red source artwork or a neutral source. Masks
    // are mandatory for living soldiers, optional for colourless death frames.
    static void LoadRobotTintFrame(int team, ZSDL_Surface &out, string filename,
                                   bool mask_required = true);
    static void LoadRobotEffectFrame(ZSDL_Surface &out, const char *unit,
                                     const char *effect, int frame);
private:
    static SDL_Surface *MakeMaskedTint(int team, SDL_Surface *base,
                                      SDL_Surface *mask, bool robot_source);
	static ZTeam_Palette team_palette[MAX_TEAM_TYPES];
};

#endif
