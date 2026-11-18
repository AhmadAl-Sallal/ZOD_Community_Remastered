#include "zgui_main_menu_widgets.h"

bool GMMWList::finished_init = false;
GMMWList::GMMWList() : ZGMMWidget()
{
	widget_type = MMLIST_WIDGET;

	view_i = 0;
	SetVisibleEntries(MMLIST_MIN_ENTRIES);
	up_button_state = MMLIST_NORMAL;
	down_button_state = MMLIST_NORMAL;
	button_down_time = 0;
}

void GMMWList::Init()
{
    finished_init = true;
}

void GMMWList::Process()
{
	const int clicks_per_second = 30;
	int clicks_to_go;
	double time_dif;
	double the_time;

	if(up_button_state==MMLIST_PRESSED)
	{
		the_time = current_time();
		time_dif = the_time - button_down_time;
		if(time_dif < 0) return;
		clicks_to_go = time_dif * clicks_per_second;

		if(clicks_to_go)
		{
			view_i -= clicks_to_go;
			if(view_i < 0) view_i = 0;
			button_down_time = the_time;
		}
	}
	
	if(down_button_state==MMLIST_PRESSED)
	{
		the_time = current_time();
		time_dif = the_time - button_down_time;
		if(time_dif < 0) return;
		clicks_to_go = time_dif * clicks_per_second;

		if(clicks_to_go)
		{
			view_i += clicks_to_go;
			CheckViewI();
			button_down_time = the_time;
		}
	}
}

void GMMWList::SetVisibleEntries(int visible_entries_)
{
	visible_entries = visible_entries_;

	if(visible_entries < MMLIST_MIN_ENTRIES) visible_entries = MMLIST_MIN_ENTRIES;

	SetHeight();
}

void GMMWList::SetHeight()
{
	h = 3 + (visible_entries * MMLIST_ENTRY_HEIGHT) + 2;
}

int GMMWList::GetFirstSelected()
{
	for(int i=0;i<entry_list.size();i++)
		if(entry_list[i].state == MMLIST_PRESSED)
			return i;

	return -1;
}

void GMMWList::UnSelectAll(int except_entry)
{
	for(int i=0;i<entry_list.size();i++)
		if(i != except_entry)
			entry_list[i].state = MMLIST_NORMAL;
}

void GMMWList::CheckViewI()
{
	int available_slots;

	available_slots = entry_list.size() - visible_entries;

	if(view_i > available_slots) view_i = available_slots;
	if(view_i < 0) view_i = 0;
}

bool GMMWList::Click(int x_, int y_)
{
	int entry_clicked;

	the_flags.clear();

	x_-=x;
	y_-=y;

	//buttons?
	if(WithinUpButton(x_, y_)) 
	{ 
		up_button_state=MMLIST_PRESSED; 
		button_down_time=current_time()+0.2;
		return true; 
	}
	if(WithinDownButton(x_, y_)) 
	{ 
		down_button_state=MMLIST_PRESSED; 
		button_down_time=current_time()+0.2;
		return true; 
	}

	//entry?
	entry_clicked = WithinEntry(x_, y_);
	if(entry_clicked != -1 && entry_clicked < entry_list.size())
	{
		if(entry_list[entry_clicked].state == MMLIST_NORMAL)
			entry_list[entry_clicked].state = MMLIST_PRESSED;
		else
			entry_list[entry_clicked].state = MMLIST_NORMAL;

		//flag stuff
		the_flags.mmlist_entry_selected = entry_clicked;

		return true;
	}

	return false;
}

bool GMMWList::UnClick(int x_, int y_)
{
	int prev_up_button_state;
	int prev_down_button_state;

	x_-=x;
	y_-=y;

	prev_up_button_state = up_button_state;
	prev_down_button_state = down_button_state;

	up_button_state = MMLIST_NORMAL;
	down_button_state = MMLIST_NORMAL;

	if(prev_up_button_state==MMLIST_PRESSED && WithinUpButton(x_, y_)) { return MoveUp(); }
	if(prev_down_button_state==MMLIST_PRESSED && WithinDownButton(x_, y_)) { return MoveDown(); }

	return false;
}

int GMMWList::WithinEntry(int x_, int y_)
{
    // Content spans the inner area; reserve 13 pixels on the right for scroll.
    if(x_ < 3 || x_ > w - 15) return -1;
    if(y_ < 3 || y_ >= 3 + visible_entries * MMLIST_ENTRY_HEIGHT) return -1;
    int entry_found = view_i + (y_ - 3) / MMLIST_ENTRY_HEIGHT;
    if(entry_found < 0 || entry_found >= (int)entry_list.size()) return -1;
    return entry_found;
}

bool GMMWList::WithinUpButton(int x_, int y_)
{
	const int iw = 11;
	const int ih = 8;
	int ix, iy;

	ix = (w - MMLIST_UP_BUTTON_FROM_RIGHT);
	iy = MMLIST_UP_BUTTON_FROM_TOP;
	
	if(x_ < ix) return false;
	if(y_ < iy) return false;
	if(x_ > ix + iw) return false;
	if(y_ > iy + ih) return false;

	return true;
}

bool GMMWList::WithinDownButton(int x_, int y_)
{
	const int iw = 11;
	const int ih = 8;
	int ix, iy;

	ix = (w - MMLIST_DOWN_BUTTON_FROM_RIGHT);
	iy = h - MMLIST_DOWN_BUTTON_FROM_BOTTOM;
	
	if(x_ < ix) return false;
	if(y_ < iy) return false;
	if(x_ > ix + iw) return false;
	if(y_ > iy + ih) return false;

	return true;
}

bool GMMWList::MoveUp()
{
	view_i--;

	if(view_i<0)
	{
		view_i = 0;
		return false;
	}

	return true;
}

bool GMMWList::MoveDown()
{
	int available_slots;

	view_i++;

	available_slots =  entry_list.size() - visible_entries;

	if(view_i > available_slots)
	{
		view_i = available_slots;

		if(view_i<0) view_i = 0;

		return false;
	}

	return true;
}

void GMMWList::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
	if(!finished_init) return;
	if(!active) return;

	tx += x;
	ty += y;

	RenderBackround(the_map, dest, tx, ty);
	RenderEntries(the_map, dest, tx, ty);
	RenderControls(the_map, dest, tx, ty);
}

void GMMWList::RenderBackround(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    FacitFrame(tx, ty, w, h);
    FacitFill(tx+2, ty+2, w-16, h-4, 24, 27, 27);
}

void GMMWList::RenderEntries(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    int content_x = tx + 3;
    int content_y = ty + 3;
    int content_w = w - 18;
    for(int i=0, j=view_i; i<visible_entries; ++i, ++j)
    {
        ZSDL_Surface text_img;
        if(j < (int)entry_list.size())
        {
            text_img.LoadBaseImage(ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(entry_list[j].text.c_str()));
            RenderEntry(the_map, dest, content_x, content_y + i*MMLIST_ENTRY_HEIGHT,
                        content_w, &text_img, entry_list[j].state);
        }
        else
            RenderEntry(the_map, dest, content_x, content_y + i*MMLIST_ENTRY_HEIGHT,
                        content_w, NULL, MMLIST_NORMAL);
    }
}

void GMMWList::RenderEntry(ZMap &the_map, SDL_Surface *dest,
                            int tx, int ty, int tw, ZSDL_Surface *label, int state)
{
    FacitFill(tx, ty, tw, MMLIST_ENTRY_HEIGHT,
              state == MMLIST_PRESSED ? 87 : 41,
              state == MMLIST_PRESSED ? 73 : 46,
              state == MMLIST_PRESSED ? 42 : 46);
    FacitFill(tx, ty+MMLIST_ENTRY_HEIGHT-1, tw, 1, 18, 20, 21);
    if(label && label->GetBaseSurface())
        label->BlitSurface(0, 0, tw-6, label->GetBaseSurface()->h,
                           NULL, tx+3, ty+2);
}

void GMMWList::RenderControls(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    int x = tx + (w - MMLIST_UP_BUTTON_FROM_RIGHT);
    int up_y = ty + MMLIST_UP_BUTTON_FROM_TOP;
    int down_y = ty + (h - MMLIST_DOWN_BUTTON_FROM_BOTTOM);
    FacitFrame(x, up_y, 11, 8, up_button_state == MMLIST_PRESSED);
    FacitFrame(x, down_y, 11, 8, down_button_state == MMLIST_PRESSED);
    // Visible scroll markers, preserving the original up/down hitboxes.
    FacitFill(x+4, up_y+2, 3, 2, 200, 164, 66);
    FacitFill(x+4, down_y+4, 3, 2, 200, 164, 66);
    int slots = (int)entry_list.size() - visible_entries;
    if(slots > 0)
    {
        int track_h = h - 27;
        int scroll_y = up_y + 10 + ((track_h - 5) * view_i / slots);
        FacitFill(x+4, scroll_y, 3, 5, 170, 145, 70);
    }
}

bool sort_mmlist_entry_func (const mmlist_entry &a, const mmlist_entry &b)
{
	return a.sort_number < b.sort_number;
}
