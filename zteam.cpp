#include "zteam.h"
//#include "zgfile.h"

ZTeam_Palette ZTeam::team_palette[MAX_TEAM_TYPES];

SDL_Color team_color[MAX_TEAM_TYPES];

bool ZTeam_Palette::LoadSurfacePalette(SDL_Surface *src)
{
	//dimensions ok?
	if(src->w != 2)
	{
		printf("ZTeam_Palette::LoadSurfacePalette:palette width not 2\n");
		return false;
	}

	if(src->h != ZTEAM_PALETTE_MAX)
	{
		printf("ZTeam_Palette::LoadSurfacePalette:palette height not %d\n", ZTEAM_PALETTE_MAX);
		return false;
	}

	//clear out
	//base_color.clear();
	//replace_color.clear();

	//load in
	for(int j=0;j<src->h;j++)
	{
		Uint8 r, g, b, a;
		Uint32 pixel;
		SDL_Color nc;

		//base color
		{
			pixel = *(Uint32*)((Uint8*)src->pixels + j * src->pitch + 0 * src->format->BytesPerPixel);
			SDL_GetRGBA(pixel, src->format, &r, &g, &b, &a);

			nc.r = r;
			nc.g = g;
			nc.b = b;

			//base_color.push_back(nc);
			if(j<ZTEAM_PALETTE_MAX) base_color[j] = nc;
		}

		//replace color
		{
			pixel = *(Uint32*)((Uint8*)src->pixels + j * src->pitch + 1 * src->format->BytesPerPixel);
			SDL_GetRGBA(pixel, src->format, &r, &g, &b, &a);

			nc.r = r;
			nc.g = g;
			nc.b = b;

			//replace_color.push_back(nc);
			if(j<ZTEAM_PALETTE_MAX) replace_color[j] = nc;
		}
	}
	
	return true;
}

bool ZTeam_Palette::SaveSurfacePalette(string filename)
{
	printf("ZTeam_Palette::SaveSurfacePalette: this function requires color arrays be vectors\n");
	return false;
	/*
	SDL_Surface *src;

	if(!base_color.size())
	{
		printf("ZTeam_Palette::SaveSurfacePalette:palette not loaded to save to '%s'\n", filename.c_str());
		return false;
	}

	src = ZSDL_NewSurface(2, base_color.size());

	if(!src)
	{
		printf("ZTeam_Palette::SaveSurfacePalette:could not make surface to save to '%s'\n", filename.c_str());
		return false;
	}

	//load surface
	for(int i=0;i<src->h;i++)
	{
		SDL_Rect point_rect;
		SDL_Color c;

		//base color
		{
			c = base_color[i];

			point_rect.x = 0;
			point_rect.y = i;
			point_rect.w = 1;
			point_rect.h = 1;

			SDL_FillRect(src, &point_rect, SDL_MapRGB(src->format, c.r, c.g, c.b));
		}

		//replace color
		{
			c = replace_color[i];

			point_rect.x = 1;
			point_rect.y = i;
			point_rect.w = 1;
			point_rect.h = 1;

			SDL_FillRect(src, &point_rect, SDL_MapRGB(src->format, c.r, c.g, c.b));
		}
	}

	//save surface
	SDL_SaveBMP(src, filename.c_str());

	//free surface
	SDL_FreeSurface(src);

	//good
	return true;
	*/
}

bool ZTeam_Palette::AddColor(SDL_Color &bc, SDL_Color &rc)
{
	printf("ZTeam_Palette::AddColor: this function requires color arrays be vectors\n");
	return false;
	/*
	//this color already in the list?
	for(vector<SDL_Color>::iterator i=base_color.begin(); i!=base_color.end(); i++)
		if(bc.r == i->r && bc.g == i->g && bc.b == i->b)
			return false;

	//add it
	base_color.push_back(bc);
	replace_color.push_back(rc);

	return true;
	*/
}

bool ZTeam_Palette::GetReplacement(Uint8 &r, Uint8 &g, Uint8 &b, SDL_Color &c)
{
	for(int i=0;i<ZTEAM_PALETTE_MAX;i++)
	{
		SDL_Color &bi = base_color[i];

		if(r == bi.r && g == bi.g && b == bi.b)
		{
			SDL_Color &ri = replace_color[i];

			c.r = ri.r;
			c.g = ri.g;
			c.b = ri.b;

			return true;
		}
	}

	return false;

	/*
	vector<SDL_Color>::iterator bi;
	vector<SDL_Color>::iterator ri;

	if(!r && !g && !b) return false;

	bi=base_color.begin();
	ri=replace_color.begin();

	for(; bi!=base_color.end(); bi++, ri++)
		if(r == bi->r && g == bi->g && b == bi->b)
		{
			c.r = ri->r;
			c.g = ri->g;
			c.b = ri->b;

			return true;
		}

	return false;
	*/
}

void ZTeam::Init()
{
	int i;

	for(i=0;i<MAX_TEAM_TYPES;i++)
		LoadPalette(i);

	Setup_team_color();
}

/*
{
	{115, 115, 115, 0},
	{233, 0, 0, 0},
	{19, 55, 251, 0},
	{23, 143, 19, 0},
	{203, 99, 47, 0}
};*/

void ZTeam::Setup_team_color()
{
	int i;

	for(i=0;i<MAX_TEAM_TYPES;i++)
	{
		team_color[i].r = 115;
		team_color[i].g = 115;
		team_color[i].b = 115;
	}

	team_color[NULL_TEAM].r = 115;
	team_color[NULL_TEAM].g = 115;
	team_color[NULL_TEAM].b = 115;

	team_color[RED_TEAM].r = 223;
	team_color[RED_TEAM].g = 0;
	team_color[RED_TEAM].b = 0;

	team_color[BLUE_TEAM].r = 19;
	team_color[BLUE_TEAM].g = 55;
	team_color[BLUE_TEAM].b = 251;

#ifdef GREEN_TEAM
	team_color[GREEN_TEAM].r = 23;
	team_color[GREEN_TEAM].g = 143;
	team_color[GREEN_TEAM].b = 19;
#endif

#ifdef YELLOW_TEAM
	team_color[YELLOW_TEAM].r = 203;
	team_color[YELLOW_TEAM].g = 99;
	team_color[YELLOW_TEAM].b = 47;
#endif

	for(i=BLUE_TEAM;i<MAX_TEAM_TYPES;i++)
	{
		SDL_Color c;
		Uint8 r, g, b;

		r = 223;
		g = 0;
		b = 0;

		if(team_palette[i].GetReplacement(r, g, b, c))
		{
			team_color[i].r = c.r;
			team_color[i].g = c.g;
			team_color[i].b = c.b;
		}
	}
}

void ZTeam::LoadPalette(int team)
{
	string filename;
	SDL_Surface *surface;

	// The neutral team does not use a recoloring palette.
	if(team == NULL_TEAM || team == ZTEAM_BASE_TEAM) return;

	filename = "assets/teams/" + team_type_string[team] + "_palette.bmp";
	surface = IMG_Load(filename.c_str());

	if(!surface)
	{
		printf("ZTeam::Could not load palette for the %s team:'%s'\n", team_type_string[team].c_str(), filename.c_str());
		return;
	}

	//convert it to 32 bits
	surface = ZSDL_ConvertImage(surface);

	//load it and free it
	team_palette[team].LoadSurfacePalette(surface);
	SDL_FreeSurface(surface);
}

void ZTeam::SavePalette(int team)
{
	if(team == ZTEAM_BASE_TEAM)
	{
		printf("ZTeam::SavePalette:You can not save the base palette (%s)\n", team_type_string[ZTEAM_BASE_TEAM].c_str());
		return;
	}

	string filename;

	filename = "assets/teams/" + team_type_string[team] + "_palette.bmp";

	team_palette[team].SaveSurfacePalette(filename);

	//SDL_SaveBMP(team_palette[team], filename.c_str());
}

void ZTeam::SaveAllPalettes()
{
	int i;

	for(i=0;i<MAX_TEAM_TYPES;i++)
		SavePalette(i);
}

void ZTeam::AppendPalette(int team, SDL_Surface *bv, SDL_Surface *rv)
{
	if(team == ZTEAM_BASE_TEAM)
	{
		printf("ZTeam::AppendPalette:You can not append to the base palette (%s)\n", team_type_string[ZTEAM_BASE_TEAM].c_str());
		return;
	}

	//surfaces ok?
	if(!bv || !rv) return;

	if(!bv->w || !bv->h) return;
	if(!rv->w || !rv->h) return;

	if(bv->w != rv->w) return;
	if(bv->h != rv->h) return;

	//get list of differences
	int i, j;

	for(i=0;i<bv->w;i++)
		for(j=0;j<bv->h;j++)
		{
			Uint8 br, bg, bb, ba;
			Uint8 rr, rg, rb, ra;
			Uint32 pixel;

			pixel = *(Uint32*)((Uint8*)bv->pixels + j * bv->pitch + i * bv->format->BytesPerPixel);
			SDL_GetRGBA(pixel, bv->format, &br, &bg, &bb, &ba);

			pixel = *(Uint32*)((Uint8*)rv->pixels + j * rv->pitch + i * rv->format->BytesPerPixel);
			SDL_GetRGBA(pixel, rv->format, &rr, &rg, &rb, &ra);

			//add to the list?
			if(br != rr || bg != rg || bb != rb)
			{
				SDL_Color bc, rc;

				bc.r = br;
				bc.g = bg;
				bc.b = bb;

				rc.r = rr;
				rc.g = rg;
				rc.b = rb;

				team_palette[team].AddColor(bc, rc);
			}
		}
}

//#define COLLECT_TEAM_COLORS

void ZTeam::LoadZSurface(int team, ZSDL_Surface &bv, ZSDL_Surface &rv, string filename)
{
#ifdef USE_TEAM_COLORS
	#ifdef COLLECT_TEAM_COLORS
			rv.LoadBaseImage(filename);
			if(team != NULL_TEAM && team != ZTEAM_BASE_TEAM)
				ZTeam::AppendPalette(team, bv.GetBaseSurface(), rv.GetBaseSurface());
	#else
			if(team == NULL_TEAM || team == ZTEAM_BASE_TEAM) rv.LoadBaseImage(filename);
			else rv.LoadBaseImage(ZTeam::Make(team, bv.GetBaseSurface())); 
	#endif
#else
	rv.LoadBaseImage(filename);
#endif
}

SDL_Surface *ZTeam::Make(int team, SDL_Surface *base_version)
{
	if(team == ZTEAM_BASE_TEAM)
	{
		printf("ZTeam::Make:You can not make from the base palette (%s)\n", team_type_string[ZTEAM_BASE_TEAM].c_str());
		return NULL;
	}

	if(!base_version) return NULL;

	SDL_Surface *nw;

	nw = CopyImage(base_version);

	if(!nw) return NULL;

	Uint32* pixel_ptr;
	pixel_ptr = (Uint32*)nw->pixels;

	for(int j=0;j<nw->h;j++)
		for(int i=0;i<nw->w;i++)
		{
			Uint8 r, g, b, a;
			Uint32 pixel;
			SDL_Color c;

			//pixel = *(Uint32*)((Uint8*)nw->pixels + j * nw->pitch + i * nw->format->BytesPerPixel);
			pixel = *pixel_ptr;
			pixel_ptr++;
			SDL_GetRGBA(pixel, nw->format, &r, &g, &b, &a);

			//bypass invisible
			if(!a) continue;

			if(team_palette[team].GetReplacement(r,g,b,c))
			{
				SDL_Rect point_rect;

				point_rect.x = i;
				point_rect.y = j;
				point_rect.w = 1;
				point_rect.h = 1;

				SDL_FillRect(nw, &point_rect, SDL_MapRGB(nw->format, c.r, c.g, c.b));
			}
		}

	return nw;
}


// ---------------------------------------------------------------------------
// Opt-in team tinting for new gray-base artwork. Does NOT change legacy Make().
// 16 luminance stops per team, fitted to the approved fort flags for the first
// four colors. Remaining colors are safe initial ramps for later visual testing.
// The RGB of the mask is ignored; its alpha determines which pixels to tint.
// ---------------------------------------------------------------------------
namespace
{
static const Uint8 kTeamTintRamps[8][16][3] = {
    // red
    {
        { 29,  9, 10},
        { 44,  7,  9},
        { 60,  9, 11},
        { 89, 11, 14},
        {112, 11, 16},
        {126, 12, 17},
        {139, 15, 20},
        {152, 20, 25},
        {165, 27, 31},
        {178, 32, 36},
        {192, 39, 42},
        {206, 45, 48},
        {220, 52, 54},
        {233, 58, 60},
        {242, 63, 64},
        {250, 67, 68}
    },
    // blue
    {
        {  9, 15, 32},
        {  8, 21, 48},
        { 10, 28, 65},
        { 12, 40, 96},
        { 13, 48,120},
        { 13, 54,134},
        { 15, 60,147},
        { 20, 68,159},
        { 29, 78,170},
        { 38, 88,183},
        { 49,100,196},
        { 60,113,210},
        { 73,126,223},
        { 84,138,234},
        { 93,147,244},
        {100,155,250}
    },
    // green
    {
        {  9, 27, 10},
        {  8, 41, 12},
        {  9, 57, 15},
        { 12, 84, 20},
        { 12,105, 23},
        { 13,119, 26},
        { 19,131, 31},
        { 29,144, 39},
        { 41,155, 48},
        { 51,168, 55},
        { 61,181, 64},
        { 72,194, 72},
        { 84,208, 81},
        { 95,220, 90},
        {104,230, 97},
        {111,239,102}
    },
    // yellow
    {
        { 31, 25,  8},
        { 46, 37,  6},
        { 63, 50,  6},
        { 93, 74,  7},
        {117, 92,  6},
        {131,104,  6},
        {144,115,  8},
        {157,128, 14},
        {168,142, 23},
        {181,156, 31},
        {195,170, 39},
        {208,185, 48},
        {222,200, 58},
        {234,214, 67},
        {243,225, 74},
        {250,235, 79}
    },
    // purple
    {
        { 22,  9, 28},
        { 33, 14, 41},
        { 44, 18, 54},
        { 63, 27, 78},
        { 77, 33, 96},
        { 86, 37,107},
        { 94, 40,117},
        {103, 43,127},
        {111, 47,137},
        {119, 50,147},
        {127, 54,158},
        {136, 58,168},
        {144, 61,179},
        {152, 64,189},
        {157, 67,195},
        {162, 69,201}
    },
    // teal
    {
        {  8, 22, 22},
        { 11, 33, 33},
        { 15, 44, 44},
        { 22, 63, 63},
        { 27, 77, 77},
        { 30, 86, 86},
        { 33, 94, 94},
        { 35,103,103},
        { 38,111,111},
        { 41,119,119},
        { 44,127,127},
        { 47,136,136},
        { 50,144,144},
        { 52,152,152},
        { 54,157,157},
        { 56,162,162}
    },
    // white
    {
        { 30, 30, 30},
        { 45, 45, 45},
        { 59, 59, 59},
        { 85, 85, 85},
        {106,106,106},
        {118,118,118},
        {129,129,129},
        {140,140,140},
        {151,151,151},
        {162,162,162},
        {173,173,173},
        {185,185,185},
        {196,196,196},
        {207,207,207},
        {214,214,214},
        {221,221,221}
    },
    // black
    {
        {  8,  8,  8},
        { 12, 12, 12},
        { 16, 16, 16},
        { 24, 24, 24},
        { 29, 29, 29},
        { 32, 32, 32},
        { 35, 35, 35},
        { 39, 39, 39},
        { 42, 42, 42},
        { 45, 45, 45},
        { 48, 48, 48},
        { 51, 51, 51},
        { 54, 54, 54},
        { 57, 57, 57},
        { 59, 59, 59},
        { 61, 61, 61}
    },
};


int NeutralTintRampIndex(int team)
{
    if(team < 0 || team >= MAX_TEAM_TYPES) return -1;
    const string &name = team_type_string[team];
    if(name == "red")    return 0;
    if(name == "blue")   return 1;
    if(name == "green")  return 2;
    if(name == "yellow") return 3;
    if(name == "purple") return 4;
    if(name == "teal")   return 5;
    if(name == "white")  return 6;
    if(name == "black")  return 7;
    return -1;
}

Uint32 Read32(const SDL_Surface *surf, int x, int y)
{
    const Uint8 *row = static_cast<const Uint8*>(surf->pixels) + y * surf->pitch;
    return *(reinterpret_cast<const Uint32*>(row + x * 4));
}

void Write32(SDL_Surface *surf, int x, int y, Uint32 pixel)
{
    Uint8 *row = static_cast<Uint8*>(surf->pixels) + y * surf->pitch;
    *(reinterpret_cast<Uint32*>(row + x * 4)) = pixel;
}
}

SDL_Surface *ZTeam::MakeNeutralTint(int team, SDL_Surface *neutral, SDL_Surface *mask)
{
    return MakeMaskedTint(team, neutral, mask, false);
}

SDL_Surface *ZTeam::MakeMaskedTint(int team, SDL_Surface *neutral, SDL_Surface *mask,
                                   bool robot_source)
{
    if(!neutral || !mask || neutral->w != mask->w || neutral->h != mask->h ||
       neutral->format->BytesPerPixel != 4 || mask->format->BytesPerPixel != 4)
    {
        printf("ZTeam::MakeNeutralTint: missing/invalid neutral or tint mask\n");
        return NULL;
    }

    const int ramp_index = NeutralTintRampIndex(team);
    if(ramp_index == -1) return NULL;

    SDL_Surface *out = robot_source ? SDL_DisplayFormatAlpha(neutral) : CopyImage(neutral);
    if(!out) return NULL;
    if(out->format->BytesPerPixel != 4)
    {
        SDL_FreeSurface(out);
        return NULL;
    }

    bool lock_neutral = SDL_MUSTLOCK(neutral);
    bool lock_mask = SDL_MUSTLOCK(mask);
    bool lock_out = SDL_MUSTLOCK(out);

    if(lock_neutral && SDL_LockSurface(neutral) < 0)
    {
        SDL_FreeSurface(out);
        return NULL;
    }
    if(lock_mask && SDL_LockSurface(mask) < 0)
    {
        if(lock_neutral) SDL_UnlockSurface(neutral);
        SDL_FreeSurface(out);
        return NULL;
    }
    if(lock_out && SDL_LockSurface(out) < 0)
    {
        if(lock_mask) SDL_UnlockSurface(mask);
        if(lock_neutral) SDL_UnlockSurface(neutral);
        SDL_FreeSurface(out);
        return NULL;
    }

    for(int y = 0; y < neutral->h; ++y)
    {
        for(int x = 0; x < neutral->w; ++x)
        {
            Uint8 r, g, b, a, mr, mg, mb, ma;
            SDL_GetRGBA(Read32(neutral, x, y), neutral->format, &r, &g, &b, &a);
            SDL_GetRGBA(Read32(mask, x, y), mask->format, &mr, &mg, &mb, &ma);
            if(a == 0 || ma == 0) continue;

            // For gray assets, this equals gray brightness; minor RGB differences
            // from antialiasing do not turn the pole or ribbon into team colors.
            // The approved troop masks identify shades of red in the supplied
            // artwork. Red intensity retains their original shade ordering;
            // luminance would make pure red uniforms almost black. Neutral
            // (grayscale) sprites still use the standard tint luminance.
            const int brightness = robot_source && r > g && r > b
                ? r : (77 * r + 150 * g + 29 * b + 128) >> 8;
            const int position = brightness * 15;
            const int low = position / 255;
            const int high = low < 15 ? low + 1 : 15;
            const int fraction = position % 255;

            const Uint8 *c0 = kTeamTintRamps[ramp_index][low];
            const Uint8 *c1 = kTeamTintRamps[ramp_index][high];
            const int tr = (c0[0] * (255 - fraction) + c1[0] * fraction + 127) / 255;
            const int tg = (c0[1] * (255 - fraction) + c1[1] * fraction + 127) / 255;
            const int tb = (c0[2] * (255 - fraction) + c1[2] * fraction + 127) / 255;

            // Blend by explicit mask alpha, preserve original sprite alpha exactly.
            const Uint8 nr = (r * (255 - ma) + tr * ma + 127) / 255;
            const Uint8 ng = (g * (255 - ma) + tg * ma + 127) / 255;
            const Uint8 nb = (b * (255 - ma) + tb * ma + 127) / 255;
            Write32(out, x, y, SDL_MapRGBA(out->format, nr, ng, nb, a));
        }
    }

    if(lock_out) SDL_UnlockSurface(out);
    if(lock_mask) SDL_UnlockSurface(mask);
    if(lock_neutral) SDL_UnlockSurface(neutral);
    return out;
}


void ZTeam::LoadRobotTintFrame(int team, ZSDL_Surface &out, string filename,
                               bool mask_required)
{
    ZSDL_Surface base, mask;
    base.LoadBaseImage(filename);
    if(!base.GetBaseSurface()) return;

    const string mask_filename = filename.substr(0, filename.size()-4) + "_tintmask.png";
    SDL_Surface *raw_mask = IMG_Load(mask_filename.c_str());
    if(!raw_mask)
    {
        if(mask_required && team == RED_TEAM)
            printf("ZOD robot: missing tint mask: %s\n", mask_filename.c_str());
        // Common, colourless death frames legitimately have no mask.
        out.LoadBaseImage(base.GetBaseSurface(), false);
        return;
    }
    mask.LoadBaseImage(raw_mask);
    SDL_Surface *tinted = MakeMaskedTint(team, base.GetBaseSurface(),
                                         mask.GetBaseSurface(), true);
    if(tinted) out.LoadBaseImage(tinted);
    else
    {
        if(team == RED_TEAM) printf("ZOD robot: invalid tint mask: %s\n", mask_filename.c_str());
        out.LoadBaseImage(base.GetBaseSurface(), false);
    }
}

void ZTeam::LoadRobotEffectFrame(ZSDL_Surface &out, const char *unit,
                                 const char *effect, int frame)
{
    // Prefer the cleaned one-based naming convention. Retain compatibility
    // with the original non-team projectile/effect assets.
    char path[500];
    sprintf(path, "assets/units/robots/%s/%s_%s_%02d.png", unit, effect, unit, frame+1);
    SDL_Surface *raw = IMG_Load(path);
    if(!raw)
    {
        sprintf(path, "assets/units/robots/%s/%s_%02d.png", unit, effect, frame+1);
        raw = IMG_Load(path);
    }
    if(!raw)
    {
        sprintf(path, "assets/units/robots/%s/%s_n%02d.png", unit, effect, frame);
        raw = IMG_Load(path);
    }
    if(!raw) printf("ZOD robot: missing effect %s/%s frame %d\n", unit, effect, frame+1);
    out.LoadBaseImage(raw);
}
