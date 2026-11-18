#ifndef ZOD_ROBOT_GEOMETRY_H
#define ZOD_ROBOT_GEOMETRY_H
#include "constants.h"

// Measured actual canvas of all 339 supplied troop/driver sprites: 18 x 22.
// The transparent canvas is centred horizontally and bottom-anchored to the
// 16 x 16 movement tile, just as the renderer was before this patch.
enum { ZOD_ROBOT_TILE = 16, ZOD_ROBOT_W = 18, ZOD_ROBOT_H = 22 };
inline int ZODRobotRenderX(int x, int w) { return x + (ZOD_ROBOT_TILE-w)/2; }
inline int ZODRobotRenderY(int y, int h) { return y + ZOD_ROBOT_TILE-h; }
inline void ZODRobotVisualBounds(int lx, int ly, int &x, int &y, int &w, int &h)
{
    w = ZOD_ROBOT_W; h = ZOD_ROBOT_H;
    x = ZODRobotRenderX(lx, w); y = ZODRobotRenderY(ly, h);
}
// Approximate muzzle flash centres measured from the actual firing frames.
inline void ZODRobotMuzzle(int type, int dir, int lx, int ly, int &x, int &y)
{
    static const int rifle[8][2] = {{15,7},{14,3},{9,3},{4,4},{3,7},{4,15},{8,17},{15,13}};
    static const int psycho[8][2] = {{15,10},{15,7},{9,2},{4,8},{4,10},{7,15},{9,15},{15,15}};
    static const int laser[8][2] = {{15,12},{12,7},{9,3},{6,6},{5,12},{7,14},{9,14},{12,14}};
    static const int pyro[8][2] = {{12,12},{13,7},{9,6},{7,7},{6,12},{6,15},{9,16},{12,15}};
    static const int tough[8][2] = {{9,8},{9,8},{13,9},{10,8},{9,8},{9,8},{6,10},{11,8}};
    const int (*points)[2] = rifle;
    switch(type)
    {
        case PSYCHO: points = psycho; break;
        case LASER: points = laser; break;
        case PYRO: points = pyro; break;
        case TOUGH: points = tough; break;
        default: break;
    }
    if(dir < 0 || dir >= 8) dir = 0;
    x = ZODRobotRenderX(lx, ZOD_ROBOT_W) + points[dir][0];
    y = ZODRobotRenderY(ly, ZOD_ROBOT_H) + points[dir][1];
}
#endif
