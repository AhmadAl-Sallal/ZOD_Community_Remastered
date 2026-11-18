#include "zgui_main_menu_widgets.h"

bool GMMWButton::finished_init = false;
GMMWButton::GMMWButton() : ZGMMWidget()
{
	widget_type = MMBUTTON_WIDGET;

	rerender_text = false;
	state = MMBUTTON_NORMAL;
	type = MMGENERIC_BUTTON;
	is_green = false;
}

void GMMWButton::Init()
{
    // Buttons are drawn from the Facit utility palette, not old Steel PNGs.
    finished_init = true;
}

void GMMWButton::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    if(!finished_init || !active) return;
    tx += x;
    ty += y;
    if(type == MMGENERIC_BUTTON)
        RenderGeneric(the_map, dest, tx, ty);
    else
        FacitFrame(tx, ty, w, h, state == MMBUTTON_PRESSED);

    MakeTextImage();
    if(text_img.GetBaseSurface())
    {
        int shift = state == MMBUTTON_PRESSED ? 1 : 0;
        int text_x = tx + (w - text_img.GetBaseSurface()->w) / 2;
        int text_y = ty + (h - text_img.GetBaseSurface()->h) / 2 + shift;
        text_img.BlitSurface(NULL, text_x, text_y);
    }
}

void GMMWButton::RenderGeneric(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    FacitFrame(tx, ty, w, h,
               state == MMBUTTON_PRESSED || (state == MMBUTTON_NORMAL && is_green));
}

void GMMWButton::MakeTextImage()
{
	if(!rerender_text) return;

	if(text.length())
		text_img.LoadBaseImage(ZFontEngine::GetFont(type == MMCLOSE_BUTTON ? SMALL_WHITE_FONT : YELLOW_MENU_FONT).Render(text.c_str()));
	else
		text_img.Unload();

	rerender_text = false;
}

void GMMWButton::DetermineDimensions()
{
	switch(type)
	{
	case MMCLOSE_BUTTON:
		w=12;
		h=12;
		break;
	}
}

bool GMMWButton::Click(int x_, int y_)
{
	if(!active) return false;
	if(!WithinDimensions(x_, y_)) return false;

	state = MMBUTTON_PRESSED;

	return true;
}

bool GMMWButton::UnClick(int x_, int y_)
{
	int previous_state = state;

	state = MMBUTTON_NORMAL;

	if(!active) return false;
	if(!WithinDimensions(x_, y_)) return false;

	//call an unclick event if we were previously pressed
	//and the unclick is over the button
	return (previous_state == MMBUTTON_PRESSED);
}
