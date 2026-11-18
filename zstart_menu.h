#ifndef _ZSTART_MENU_H_
#define _ZSTART_MENU_H_

#include <string>

enum ZStartMenuResult
{
    ZSTART_GAME, ZSTART_REPLAY,
    ZSTART_QUIT,
    ZSTART_MAP_EDITOR,
    ZSTART_SELECT_MAP, ZSTART_MULTIPLAYER_HOST, ZSTART_MULTIPLAYER_JOIN
};

int RunZStartMenu();
// Integrated right-side Map Editor panel.
const char* ZStartMenuEditorCommand();
const std::string& ZStartMenuPlayerName();

#endif