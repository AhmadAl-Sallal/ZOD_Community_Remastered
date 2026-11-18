#ifndef ZOD_CANNON_GEOMETRY_H
#define ZOD_CANNON_GEOMETRY_H

// Mister 008: measured from the 2026-11-14 cannon PNGs.
// Visual coordinates are relative to the map object's unchanged tile origin.
// One stable box per cannon encloses all intact passive/empty rotations, not
// muzzle flash or wrecks. Logical 2x2 placement and combat footprint stay 32x32.
// ID order: GATLING, GUN, HOWITZER, MISSILE_CANNON (constants.h).
struct ZODCannonRect { int x, y, w, h; };
struct ZODCannonPoint { int x, y; };

inline bool ZODCannonVisualRect(int id, ZODCannonRect &out)
{
    static const ZODCannonRect rects[4] = {
        {-8, -10, 42, 35},
        {-6, -6, 46, 35},
        {-9, -16, 48, 43},
        {-7, -11, 43, 38}
    };
    if(id < 0 || id >= 4) return false;
    out = rects[id];
    return true;
}

// Barrel tips in the current artwork (visual estimate at pixel level).
// Direction order: 000, 045, 090, 135, 180, 225, 270, 315.
inline ZODCannonPoint ZODCannonMuzzle(int id, int direction, bool second = false)
{
    static const ZODCannonPoint first[4][8] = {
        {{33, 3}, {24, -9}, {13, -8}, {2, -8}, {-8, 3}, {-2, 16}, {12, 21}, {27, 15}},
        {{38, 10}, {30, 0}, {16, -2}, {2, 0}, {-6, 10}, {4, 19}, {16, 24}, {29, 21}},
        {{38, 5}, {30, -11}, {15, -15}, {0, -12}, {-9, 5}, {-4, 17}, {15, 23}, {34, 17}},
        {{34, 1}, {17, -8}, {6, -9}, {3, -3}, {-5, 1}, {1, 12}, {6, 18}, {18, 19}}
    };
    static const ZODCannonPoint missile_second[8] =
        {{34, 10}, {27, -2}, {22, -9}, {13, -8}, {-5, 10}, {11, 20}, {21, 18}, {29, 10}};
    ZODCannonPoint fallback = {16, 16};
    if(id < 0 || id >= 4 || direction < 0 || direction >= 8) return fallback;
    return second && id == 3 ? missile_second[direction] : first[id][direction];
}

#endif
