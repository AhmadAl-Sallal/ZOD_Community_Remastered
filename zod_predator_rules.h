#ifndef ZOD_PREDATOR_RULES_H
#define ZOD_PREDATOR_RULES_H

#include <random>
#include <cstddef>

namespace ZODPredators
{
// Reserved negative killer ref: normal object refs are >= 0, -1 is unknown.
// Reuses the existing death message without changing packet layout/IDs.
constexpr int killer_ref = -2;
constexpr double check_seconds = 1.0;
constexpr double cooldown_seconds = 4.0;
constexpr unsigned chance_percent = 1;

class Clock
{
    double next = -1.0;
public:
    void Reset() { next = -1.0; }
    bool Due(double now, bool active)
    {
        if(!active) return false;
        if(next < 0.0) { next = now + check_seconds; return false; }
        if(now < next) return false;
        // Do not catch up missed ticks after a stall, pause or map transition.
        next = now + check_seconds;
        return true;
    }
    void Cooldown(double now)
    {
        if(next < now + cooldown_seconds) next = now + cooldown_seconds;
    }
};

// A separate random engine avoids changing combat/AI's existing rand() stream.
class Lottery
{
    std::mt19937 random;
public:
    explicit Lottery(unsigned seed = std::random_device{}()) : random(seed) {}
    bool Hit() { return std::uniform_int_distribution<unsigned>(0,99)(random) < chance_percent; }
    // Reservoir sampling: order/team does not bias selection when several hit.
    bool Replace(std::size_t hits)
    {
        return std::uniform_int_distribution<std::size_t>(1,hits)(random) == 1;
    }
};
}
#endif
