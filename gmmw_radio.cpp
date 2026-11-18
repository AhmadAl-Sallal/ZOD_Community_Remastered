#include "zgui_main_menu_widgets.h"

bool GMMWRadio::finished_init = false;
GMMWRadio::GMMWRadio() : ZGMMWidget()
{
	widget_type = MMRADIO_WIDGET;

	si = 0;
	SetMaxSelections(MMRADIO_MIN_SELECTIONS);
	h = MMRADIO_HEIGHT;
}

void GMMWRadio::Init()
{
    finished_init = true;
}

void GMMWRadio::SetMaxSelections(int selections_)
{
	selections = selections_;

	if(selections<MMRADIO_MIN_SELECTIONS) selections = MMRADIO_MIN_SELECTIONS;

	w = MMRADIO_LEFT_WIDTH + ((selections-2) * MMRADIO_CENTER_WIDTH) + MMRADIO_RIGHT_WIDTH;

	CheckSI();
}

void GMMWRadio::CheckSI()
{
	if(si<0 || si>=selections) 
	{
		printf("GMMWRadio::CheckSI:bad si:%d max_selections:%d\n", si, selections);
		si = 0;
	}
}

bool GMMWRadio::Click(int x_, int y_)
{
	the_flags.clear();

	if(x_ < x) return false;
	if(x_ > x + w) return false;
	if(y_ < y) return false;
	if(y_ > y + h) return false;

	x_ -= x;

	if(x_ < MMRADIO_LEFT_WIDTH)
		the_flags.mmradio_si_selected = 0;
	else if(x_ > w - MMRADIO_RIGHT_WIDTH)
		the_flags.mmradio_si_selected = selections - 1;
	else
		the_flags.mmradio_si_selected = 1 + ((x_ - MMRADIO_LEFT_WIDTH) / MMRADIO_CENTER_WIDTH);

	return true;
}

void GMMWRadio::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    if(!finished_init || !active) return;
    tx += x; ty += y;
    FacitFrame(tx, ty, w, h);
    for(int i=0; i<selections; ++i)
    {
        int left = i==0 ? 0 : MMRADIO_LEFT_WIDTH+(i-1)*MMRADIO_CENTER_WIDTH;
        int right = i==selections-1 ? w : MMRADIO_LEFT_WIDTH+i*MMRADIO_CENTER_WIDTH;
        if(right>left+2)
            FacitFrame(tx+left+1, ty+1, right-left-2, h-2, si==i);
    }
}

