#include "zgui_main_menu_widgets.h"

bool GMMWTextBox::finished_init = false;

GMMWTextBox::GMMWTextBox()
{
	widget_type = MMTEXT_BOX_WIDGET;

	selected = false;
	good_chars_only = false;
	passworded = false;
	do_rerender = false;
	max_text = -1;

	h = MMTEXT_BOX_HEIGHT;
}

void GMMWTextBox::Init()
{
    finished_init = true;
}

bool GMMWTextBox::Click(int x_, int y_)
{
	return WithinDimensions(x_, y_);
}

bool GMMWTextBox::KeyPress(int c)
{
	if(!selected) return false;

	if(c == 8) //delete key
	{
		if(text.length())
			text.erase(text.length()-1,1);
	}
	else
	{
		if(max_text != -1 && text.length() >= max_text) return true;
		if(good_chars_only && !good_user_char(c)) return true;

		//add it to the string
		text += c;
	}

	do_rerender = true;

	return true;
}

void GMMWTextBox::MakeTextImage()
{
	string render_str;

	if(passworded)
		render_str = string(text.length(), '*');
	else
		render_str = text;

	if(selected)
		render_str += "{";

	text_img.LoadBaseImage(ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(render_str.c_str()));

	do_rerender = false;
}

void GMMWTextBox::Process()
{
	if(do_rerender) MakeTextImage();
}

void GMMWTextBox::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    if(!finished_init || !active) return;
    tx += x; ty += y;
    FacitFrame(tx, ty, w, h, selected);
    if(text_img.GetBaseSurface())
    {
        int text_width = w - 8;
        if(text_width <= 0) return;
        int image_width = text_img.GetBaseSurface()->w;
        int from_x = image_width > text_width ? image_width-text_width : 0;
        int blit_width = image_width-from_x;
        if(blit_width > text_width) blit_width = text_width;
        text_img.BlitSurface(from_x, 0, blit_width,
                            text_img.GetBaseSurface()->h, NULL, tx+4, ty+2);
    }
}

