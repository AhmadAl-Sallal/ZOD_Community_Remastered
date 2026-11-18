#ifndef ZOD_COLLISION_RECOVERY_H
#define ZOD_COLLISION_RECOVERY_H

#include <algorithm>
#include <vector>
#include <cstdlib>

// Local, pixel-resolution emergency exit. This is deliberately independent
// of A*: only the physical collision rectangle and the existing blocked tiles
// matter. A step may reduce (or preserve) existing penetration, but may never
// acquire a new blocked tile or increase penetration into an existing tile.
namespace ZODCollisionRecovery
{
    struct Point { int x, y; Point(int x_=0,int y_=0):x(x_),y(y_) {} };
    inline int Positive(int n) { return n>0?n:0; }
    inline int FloorTile(int n) { return n>=0?n/16:-((-n+15)/16); }
    inline int TileOverlap(int x,int y,int w,int h,int tx,int ty)
    {
        const int left=tx*16, top=ty*16;
        return Positive((std::min)(x+w,left+16)-(std::max)(x,left))*
               Positive((std::min)(y+h,top+16)-(std::max)(y,top));
    }

    // The caller supplies the same per-tile passability as WithinImpassable.
    // Right/bottom use its existing strict boundary convention.
    template<class Grid>
    bool SafeStep(Grid &grid,int ax,int ay,int bx,int by,int w,int h,int mw,int mh)
    {
        if(std::abs(bx-ax)+std::abs(by-ay)!=1) return false;
        if(Positive(-bx)>Positive(-ax) || Positive(-by)>Positive(-ay) ||
           Positive(bx+w-mw+1)>Positive(ax+w-mw+1) ||
           Positive(by+h-mh+1)>Positive(ay+h-mh+1)) return false;
        const int tx0=(std::max)(0,FloorTile(bx));
        const int ty0=(std::max)(0,FloorTile(by));
        const int tx1=(std::min)(mw/16-1,FloorTile(bx+w-1));
        const int ty1=(std::min)(mh/16-1,FloorTile(by+h-1));
        for(int tx=tx0;tx<=tx1;++tx)
            for(int ty=ty0;ty<=ty1;++ty)
                if(!grid.Passable(tx,ty) &&
                   TileOverlap(bx,by,w,h,tx,ty)>TileOverlap(ax,ay,w,h,tx,ty))
                    return false;
        return true;
    }

    // Bounded BFS handles corners and equal-overlap steps. Return just the
    // first pixel of the shortest safe escape; the next tick revalidates the
    // live map, so newly created/destroyed obstacles cannot invalidate a plan.
    template<class Grid>
    bool FindStep(Grid &grid,int x,int y,int w,int h,int mw,int mh,Point &step)
    {
        if(w<=0 || h<=0 || mw<=0 || mh<=0 || !grid.Blocked(x,y,w,h)) return false;
        const int radius=32, side=radius*2+1, origin=radius*side+radius;
        struct Node { int x,y,first; Node(int a,int b,int f):x(a),y(b),first(f){} };
        std::vector<unsigned char> visited(side*side,0);
        std::vector<Node> queue;
        queue.reserve(side*side);
        queue.push_back(Node(x,y,-1)); visited[origin]=1;
        const int dx[]={-1,1,0,0}, dy[]={0,0,-1,1};
        for(std::size_t i=0;i<queue.size();++i)
        {
            const Node n=queue[i];
            for(int dir=0;dir<4;++dir)
            {
                const int nx=n.x+dx[dir],ny=n.y+dy[dir];
                const int rx=nx-x,ry=ny-y;
                if(std::abs(rx)+std::abs(ry)>radius) continue;
                const int index=(ry+radius)*side+rx+radius;
                if(visited[index] || !SafeStep(grid,n.x,n.y,nx,ny,w,h,mw,mh)) continue;
                visited[index]=1;
                const int first=n.first<0?dir:n.first;
                if(!grid.Blocked(nx,ny,w,h))
                {
                    step=Point(x+dx[first],y+dy[first]);
                    return true;
                }
                queue.push_back(Node(nx,ny,first));
            }
        }
        return false;
    }
}
#endif
