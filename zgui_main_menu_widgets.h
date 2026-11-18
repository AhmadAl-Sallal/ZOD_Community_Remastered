#ifndef _ZGUIMAINMENUWIDGETS_H_
#define _ZGUIMAINMENUWIDGETS_H_

#include <algorithm>
#include "zsdl.h"
#include "zsdl_opengl.h"
#include "zmap.h"
#include "zfont_engine.h"
#include "common.h"

using namespace COMMON;

// Small Facit-compatible utility controls for legacy map/network dialogs.
// The approved in-game Facit PNG panels render themselves independently.
// No removed legacy menu textures or newly named graphics are required.
inline void FacitFill(int x, int y, int w, int h, int red, int green, int blue)
{
    if(w <= 0 || h <= 0) return;
    SDL_Rect rect = { (Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h };
    ZSDL_Surface::ZSDL_FillRect(&rect, (char)red, (char)green, (char)blue);
}
inline void FacitFrame(int x, int y, int w, int h, bool selected = false)
{
    if(w <= 1 || h <= 1) return;
    FacitFill(x, y, w, h, 13, 17, 19);
    FacitFill(x+1, y+1, w-2, h-2, selected ? 72 : 54,
              selected ? 66 : 57, selected ? 48 : 55);
    if(w > 4 && h > 4)
        FacitFill(x+2, y+2, w-4, h-4,
                  selected ? 67 : 33, selected ? 59 : 37, selected ? 41 : 37);
    FacitFill(x+1, y+1, w-2, 1, selected ? 191 : 119,
              selected ? 154 : 113, selected ? 74 : 99);
}

class gmmw_flag
{
public:
	gmmw_flag() { clear(); }

	void clear()
	{
		mmlist_entry_selected = -1;
		mmradio_si_selected = -1;
	}

	int mmlist_entry_selected;
	int mmradio_si_selected;
};

enum mmwidget_type
{
	MMUNKNOWN_WIDGET, MMBUTTON_WIDGET, MMLABEL_WIDGET,
	MMLIST_WIDGET, MMRADIO_WIDGET, MMTEAM_COLOR_WIDGET,
	MMTEXT_BOX_WIDGET, 
	MAX_MMWIDGETS
};

const string mmwidget_type_string[MAX_MMWIDGETS] =
{
	"unknown", "button", "label", "list", "radio", 
	"team_color", "text_box"
};

class ZGMMWidget
{
public:
	ZGMMWidget();

	virtual void Process() {}
	virtual void DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty) {}

	void SetActive(bool active_) { active = active_; }
	void ToggleActive() { active = !active; }
	bool IsActive() { return active; }
	int GetRefID() { return ref_id; }
	int GetWidgetType() { return widget_type; }
	int GetWidth() { return w; }
	int GetHeight() { return h; }
	gmmw_flag &GetGMMWFlags() { return the_flags; }

	void SetDimensions(int w_, int h_) { w=w_; h=h_; }
	void SetCoords(int x_, int y_) { x=x_; y=y_; }
	bool WithinDimensions(int x_, int y_);

	virtual bool Click(int x_, int y_) { return false; }
	virtual bool UnClick(int x_, int y_) { return false; }
	virtual bool Motion(int x_, int y_) { return false; }
	virtual bool KeyPress(int c) { return false; }
	virtual bool WheelUpButton() { return false; }
	virtual bool WheelDownButton() { return false; }
protected:
	int x,y,w,h;
	bool active;
	int ref_id;
	int widget_type;
	gmmw_flag the_flags;

private:
	static int next_ref_id;
};

enum mmbutton_type
{
	MMGENERIC_BUTTON, MMCLOSE_BUTTON, MAX_MMBUTTON_TYPES
};

const string mmbutton_type_string[MAX_MMBUTTON_TYPES] = 
{
	"", "close"
};

enum mmbutton_state
{
	MMBUTTON_NORMAL, MMBUTTON_PRESSED, MMBUTTON_GREEN, MAX_MMBUTTON_STATES
};

#define GMMWBUTTON_HEIGHT 15

class GMMWButton : public ZGMMWidget
{
public:
	GMMWButton();

	static void Init();

	void DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty);

	bool Click(int x_, int y_);
	bool UnClick(int x_, int y_);

	void SetText(string text_) { text = text_; rerender_text = true; }
	void SetType(int type_) { type = type_; DetermineDimensions(); }
	void SetGreen(bool is_green_) { is_green = is_green_; }
private:
	static bool finished_init;


	void RenderGeneric(ZMap &the_map, SDL_Surface *dest, int tx, int ty);

	void MakeTextImage();
	void DetermineDimensions();

	string text;
	ZSDL_Surface text_img;
	bool rerender_text;

	int state;
	int type;
	bool is_green;
	
};

#define MMLABEL_HEIGHT 10

enum mmlabel_justify_type
{
	MMLABEL_NORMAL, MMLABEL_CENTER, MMLABEL_RIGHT, MAX_MMLABEL_JUSTIFIES
};

class GMMWLabel : public ZGMMWidget
{
public:
	GMMWLabel();

	void DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty);

	void SetText(string text_);
	void SetJustification(int justify_) { justify = justify_; }
	void SetFont(int font_) { font=font_; }
private:
	void MakeTextImage();

	int justify;
	string text;
	ZSDL_Surface text_img;
	string rendered_text;
	bool rerender_text;
	int font;
};

#define MMLIST_ENTRY_HEIGHT 13
#define MMLIST_MIN_ENTRIES 4

#define MMLIST_UP_BUTTON_FROM_TOP 3
#define MMLIST_UP_BUTTON_FROM_RIGHT 12
#define MMLIST_DOWN_BUTTON_FROM_BOTTOM 11
#define MMLIST_DOWN_BUTTON_FROM_RIGHT 12
#define MMLIST_SCROLLER_FROM_RIGHT 9

enum mmlist_state
{
	MMLIST_NORMAL, MMLIST_PRESSED, MAX_MMLIST_STATES
};

class mmlist_entry
{
public:
	mmlist_entry() { clear(); }
	mmlist_entry(string text_, int ref_id_, int sort_number_) 
	{ 
		text = text_; 
		ref_id = ref_id_; 
		sort_number= sort_number_; 
		state = MMLIST_NORMAL;
	}

	void clear()
	{
		text.clear();
		ref_id = -1;
		sort_number = -1;
		state = MMLIST_NORMAL;
	}

	string text;
	int ref_id;
	int sort_number;
	int state;

	friend bool sort_mmlist_entry_func (const mmlist_entry &a, const mmlist_entry &b);
};

extern bool sort_mmlist_entry_func (const mmlist_entry &a, const mmlist_entry &b);

class GMMWList : public ZGMMWidget
{
public:
	GMMWList();

	static void Init();

	void Process();
	void DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty);

	bool Click(int x_, int y_);
	bool UnClick(int x_, int y_);
	bool WheelUpButton() { return MoveUp(); }
	bool WheelDownButton() { return MoveDown(); }

	void SetVisibleEntries(int visible_entries_);
	vector<mmlist_entry> &GetEntryList() { return entry_list; }
	void CheckViewI();
	void UnSelectAll(int except_entry = -1);
	int GetFirstSelected();
private:
	static bool finished_init;


	void RenderBackround(ZMap &the_map, SDL_Surface *dest, int tx, int ty);
	void RenderEntries(ZMap &the_map, SDL_Surface *dest, int tx, int ty);
	void RenderControls(ZMap &the_map, SDL_Surface *dest, int tx, int ty);
	void RenderEntry(ZMap &the_map, SDL_Surface *dest, int ix, int iy, int iw, ZSDL_Surface *text, int state);

	void SetHeight();
	bool MoveUp();
	bool MoveDown();
	bool WithinUpButton(int x_, int y_);
	bool WithinDownButton(int x_, int y_);
	int WithinEntry(int x_, int y_);

	int visible_entries;
	int view_i;

	int up_button_state;
	int down_button_state;

	double button_down_time;

	vector<mmlist_entry> entry_list;
};

#define MMRADIO_HEIGHT 9
#define MMRADIO_LEFT_WIDTH 16
#define MMRADIO_CENTER_WIDTH 13
#define MMRADIO_RIGHT_WIDTH 15
#define MMRADIO_MIN_SELECTIONS 2

class GMMWRadio : public ZGMMWidget
{
public:
	GMMWRadio();

	static void Init();

	void DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty);

	bool Click(int x_, int y_);

	void SetMaxSelections(int selections_);
	void SetSelected(int si_) { si=si_; CheckSI(); }
	int GetSelected() { return si; }

private:
	static bool finished_init;


	void CheckSI();

	int selections;
	int si;
};

#define MMTEAM_COLOR_HEIGHT 12
#define MMTEAM_COLOR_WIDTH 19

class GMMWTeamColor : public ZGMMWidget
{
public:
	GMMWTeamColor();

	static void Init();

	void DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty);

	void SetTeam(int team_);
private:
	static bool finished_init;

	static ZSDL_Surface team_color_img[MAX_TEAM_TYPES];

	int team;
};

#define MMTEXT_BOX_HEIGHT 14


class GMMWTextBox : public ZGMMWidget
{
public:
	GMMWTextBox();

	static void Init();

	void DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty);
	void Process();

	bool Click(int x_, int y_);
	bool KeyPress(int c);

	void SetText(string text_) { text=text_; do_rerender = true; }
	void SetSelected(bool selected_) { selected=selected_; do_rerender = true; }
	void SetMaxText(int max_text_) { max_text=max_text_; }
	void SetGoodCharsOnly(bool good_chars_only_) { good_chars_only=good_chars_only_; do_rerender = true; }
	void SetPassworded(bool passworded_) { passworded=passworded_; do_rerender = true; }
private:
	static bool finished_init;


	void MakeTextImage();

	bool selected;
	string text;
	int max_text;
	bool good_chars_only;
	bool passworded;
	ZSDL_Surface text_img;

	bool do_rerender;
};

#endif
