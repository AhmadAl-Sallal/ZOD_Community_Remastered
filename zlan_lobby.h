#ifndef _ZLAN_LOBBY_H_
#define _ZLAN_LOBBY_H_

#include <string>

struct ZLanLobbyResult
{
    std::string host_ip;
    int team;
};

// Pre-game LAN lobby. No ZServer/ZPlayer is launched until all players are ready
// and the host presses Start. Returns false for Back/Esc or network errors.
bool RunZLanLobby(bool host, const std::string& map_path,
                  const std::string& player_name, ZLanLobbyResult& result);

#endif
