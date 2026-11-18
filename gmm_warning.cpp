#include "gmm_warning.h"

GMMWarning::GMMWarning(gmm_warning_flag warning_flags_) : ZGuiMainMenuBase()
{
    warning_flags = warning_flags_;
    menu_type = GMM_WARNING;
    title = "Confirm";
    facit_dialog = (warning_flags.reset_map || warning_flags.quit_game);
    close_button.SetActive(false);

    if(facit_dialog)
    {
        w = 408;
        h = 246;
        const char *art = "assets/other/zod_facit_ui/dialog_reset.png";
        if(warning_flags.quit_game)
        {
            art = (warning_flags.text1.find("ALL other players") != string::npos ||
                   warning_flags.text2.find("ALL other players") != string::npos)
                ? "assets/other/zod_facit_ui/dialog_quit_host.png"
                : "assets/other/zod_facit_ui/dialog_quit.png";
        }
        dialog_art.LoadBaseImage(art);
        // Distinct click boxes map to the exact YES and CANCEL plate positions.
        ok_button.SetType(MMGENERIC_BUTTON);
        ok_button.SetCoords(22, 179);
        ok_button.SetDimensions(175, 36);
        AddWidget(&ok_button);
        cancel_button.SetType(MMGENERIC_BUTTON);
        cancel_button.SetCoords(212, 179);
        cancel_button.SetDimensions(175, 36);
        AddWidget(&cancel_button);
        UpdateDimensions();
    }
    else
    {
        w = 172;
        h = 72;
        SetupLayout1();
    }
}

void GMMWarning::SetupLayout1()
{
    cancel_button.SetType(MMGENERIC_BUTTON);
    cancel_button.SetText("Cancel");
    cancel_button.SetCoords(18, 49);
    cancel_button.SetDimensions(54, 15);
    AddWidget(&cancel_button);

    ok_button.SetType(MMGENERIC_BUTTON);
    ok_button.SetText("Ok");
    ok_button.SetCoords(w - 18 - 54, 49);
    ok_button.SetDimensions(54, 15);
    AddWidget(&ok_button);

    text1_label.SetFont(SMALL_WHITE_FONT);
    text1_label.SetText(warning_flags.text1);
    text1_label.SetCoords(8, 24);
    text1_label.SetDimensions(w-16, MMLABEL_HEIGHT);
    text1_label.SetJustification(MMLABEL_CENTER);
    AddWidget(&text1_label);

    text2_label.SetFont(SMALL_WHITE_FONT);
    text2_label.SetText(warning_flags.text2);
    text2_label.SetCoords(8, 24 + MMLABEL_HEIGHT + 2);
    text2_label.SetDimensions(w-16, MMLABEL_HEIGHT);
    text2_label.SetJustification(MMLABEL_CENTER);
    AddWidget(&text2_label);
    UpdateDimensions();
}

void GMMWarning::DoRender(ZMap &the_map, SDL_Surface *dest)
{
    if(facit_dialog && dialog_art.GetBaseSurface())
        dialog_art.BlitSurface(NULL, x, y);
    else
        ZGuiMainMenuBase::DoRender(the_map, dest);
}

void GMMWarning::HandleWidgetEvent(int event_type, ZGMMWidget *event_widget)
{
	int w_ref_id;

	if(!event_widget) return;

	w_ref_id = event_widget->GetRefID();

	switch(event_type)
	{
	case GMM_UNCLICK_EVENT:
		if(w_ref_id == ok_button.GetRefID())
		{
			killme = true;
			gmm_flags.quit_game = warning_flags.quit_game;
			gmm_flags.reset_map = warning_flags.reset_map;
		}
		else if(w_ref_id == cancel_button.GetRefID())
		{
			killme = true;
		}
		break;
	}
}

