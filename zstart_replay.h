#ifndef ZOD_ZSTART_REPLAY_H
#define ZOD_ZSTART_REPLAY_H

#include <string>
#include <vector>

// Returns true only when the user selected a completed campaign level.
// The returned path is loaded as a one-off map, not a campaign session.
bool RunZStartReplay(std::string &map_path);

// Same completed-level list as the original replay screen.
std::vector<std::string> ZStartReplayCompletedMaps();

// Queue one valid completed level for the existing main.cpp replay launch path.
bool ZStartReplayQueueMap(const std::string &map_path);

#endif