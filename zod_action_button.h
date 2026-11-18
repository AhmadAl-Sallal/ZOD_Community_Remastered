#ifndef ZOD_ACTION_BUTTON_H
#define ZOD_ACTION_BUTTON_H

// UI time, deliberately independent of the paused simulation clock.
struct ZodActionButton
{
    int held, pending;
    unsigned int queued_at;
    bool sent;
    ZodActionButton() { Reset(); }
    void Reset() { held = pending = 0; queued_at = 0; sent = false; }
    void Press(int id) { if(!pending) held = id; }
    void CancelHold() { held = 0; }
    bool Queue(int id, unsigned int now) {
        if(!id || pending) return false;
        held = 0; pending = id; queued_at = now; sent = false; return true;
    }
    bool Release(int id, unsigned int now) {
        const int before = held; held = 0;
        return before && before == id && Queue(id, now);
    }
    bool Ready(unsigned int now, unsigned int delay) const {
        return pending && !sent && now - queued_at >= delay;
    }
    int Visual(int id, int hover) const {
        if(!id) return 0;
        if(pending) return pending == id && !sent ? 2 : 0;
        if(hover != id) return 0;
        return held == id ? 2 : 1;
    }
};
#endif
