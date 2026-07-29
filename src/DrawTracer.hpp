#pragma once

#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Per-frame draw-call tracing for renderer debugging.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Rendering
 *
 * Captures a chronological log of "events" within a single frame:
 *  - **Section markers** emitted by the Game render partials (Game.cpp and
 *    GameMenus.cpp) labelling what is about to be drawn ("Sky", "Particles",
 *    "UI overlays", ...). No subsystem marks its own span; the orchestrator
 *    labels it on the subsystem's behalf.
 *  - **Flush events** added by renderer backends when they actually
 *    submit a batch to the GPU (texture change, batch full, end of pass,
 *    projection swap, ...). The label encodes the reason and any extra
 *    context (vertex count, batch type).
 *
 * Each event records the renderer's cumulative draw-call count at the
 * moment of capture, so the delta between consecutive events shows how
 * many GPU submissions occurred in that span. Combined with the section
 * markers, this gives a "where was this draw call spent" trace.
 *
 * Capture is **opt-in** (off by default to avoid the per-frame allocation
 * cost). Toggle via the `renderer.trace` console command.
 *
 * @par Thread safety
 * Single-threaded; safe for the render thread only. Backends and game
 * code must not call from worker threads.
 *
 * @par Backend coverage
 * The OpenGL backend records flush events in all five of its batch flushers
 * (FlushBatch, FlushBatch3D, FlushRectBatch, FlushParticleBatch, FlushTextBatch)
 * plus the individual Draw* entry points. The Vulkan backend currently records section
 * markers only (Game.cpp threads them through both backends). Adding flush
 * instrumentation to VulkanRenderer is a future enhancement.
 */
namespace DrawTracer
{
/**
 * @struct Event
 * @brief One captured point in the frame log: a section marker or a flush.
 * @ingroup Rendering
 *
 * Ordering is capture order, so the @ref drawCount delta between two adjacent
 * events is the number of GPU submissions attributable to the span between them.
 */
struct Event
{
    int drawCount;      ///< Renderer's cumulative draw-call count at capture time.
    std::string label;  ///< Free-form description (section name or flush reason).
};

/**
 * @brief Toggle frame capture. When disabled, all Mark/BeginFrame calls
 * are no-ops and storage is freed.
 *
 * Disabling also discards the last completed frame's events, so read them
 * before turning capture off.
 */
void SetEnabled(bool enabled);

/// @brief Whether capture is on. Call sites guard label formatting with this so
/// the snprintf cost disappears when tracing is off.
bool IsEnabled();

/**
 * @brief Swap the live event list to "last completed frame" and clear
 * the live buffer for the next frame's events. Safe to call when
 * disabled (no-op).
 */
void BeginFrame();

/**
 * @brief Append an event to the live frame log.
 *
 * Caller passes the renderer's current draw-call count so events are
 * time-stamped with the GPU-work counter (rather than wall-clock time).
 *
 * Bounded: past 50,000 events in one frame further events are dropped silently,
 * with no diagnostic, so a pathological frame (e.g. an infinite loop) cannot eat
 * unbounded memory.
 *
 * @param label            Section name or flush reason. Copied into the event, so
 *                         a stack buffer or temporary is safe.
 * @param currentDrawCount Renderer's cumulative draw-call count at this point.
 */
void Mark(std::string_view label, int currentDrawCount);

/**
 * @brief Read the events captured during the most recently completed
 * frame. Returns an empty vector before the first BeginFrame swap.
 */
const std::vector<Event>& LastFrameEvents();

/// @brief Drop any captured events without changing the enabled state.
void Clear();
}  // namespace DrawTracer
