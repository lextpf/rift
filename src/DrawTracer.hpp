#pragma once

#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Per-frame draw-call tracing for renderer debugging.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Captures a chronological log of "events" within a single frame:
 *  - **Section markers** added by render orchestrators (Game::Render,
 *    GameMenus::RenderTitleFrame, SkyRenderer, etc.) labelling what is
 *    about to be drawn ("Sky", "Background tiles", "Particles", ...).
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
 * The OpenGL backend records flush events in its three batch flushers.
 * The Vulkan backend currently records section markers only (Game.cpp
 * threads them through both backends). Adding flush instrumentation to
 * VulkanRenderer is a future enhancement.
 */
namespace DrawTracer
{
struct Event
{
    int drawCount;      ///< Renderer's cumulative draw-call count at capture time.
    std::string label;  ///< Free-form description (section name or flush reason).
};

/**
 * @brief Toggle frame capture. When disabled, all Mark/BeginFrame calls
 * are no-ops and storage is freed.
 */
void SetEnabled(bool enabled);

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
 * The label is copied; callers can construct it freely.
 *
 * Bounded: drops events past kMaxEvents to protect against pathological
 * frames (e.g. infinite loop) eating unbounded memory.
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
