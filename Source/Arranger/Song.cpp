#include "Song.h"

namespace cadenza::arranger
{
const SongEvent* Song::eventForBar(int bar) const noexcept
{
    const SongEvent* best = nullptr;
    for (const auto& e : events) {
        if (e.bar <= bar && (best == nullptr || e.bar > best->bar)) {
            best = &e;
        }
    }
    return best;
}
}
