#include "DrawTracer.hpp"

namespace DrawTracer
{
namespace
{
// Hard cap on per-frame events. With section markers (~30) plus flush
// events (~50 in a busy frame) this leaves plenty of headroom while
// preventing a runaway loop from allocating gigabytes.
constexpr size_t kMaxEvents = 50000;

bool s_Enabled = false;
std::vector<Event> s_LiveEvents;
std::vector<Event> s_LastEvents;
}  // namespace

void SetEnabled(bool enabled)
{
    s_Enabled = enabled;
    if (!enabled)
    {
        // Free memory when disabled - capture is opt-in for debugging.
        std::vector<Event>().swap(s_LiveEvents);
        std::vector<Event>().swap(s_LastEvents);
    }
}

bool IsEnabled()
{
    return s_Enabled;
}

void BeginFrame()
{
    if (!s_Enabled)
        return;
    s_LastEvents = std::move(s_LiveEvents);
    s_LiveEvents.clear();
    // Reuse the previous frame's capacity to avoid per-frame reallocations.
    s_LiveEvents.reserve(s_LastEvents.size());
}

void Mark(std::string_view label, int currentDrawCount)
{
    if (!s_Enabled)
        return;
    if (s_LiveEvents.size() >= kMaxEvents)
        return;
    s_LiveEvents.push_back({currentDrawCount, std::string(label)});
}

const std::vector<Event>& LastFrameEvents()
{
    return s_LastEvents;
}

void Clear()
{
    s_LiveEvents.clear();
    s_LastEvents.clear();
}
}  // namespace DrawTracer
