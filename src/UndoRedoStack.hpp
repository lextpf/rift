#pragma once

#include "EditorCommand.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Tilemap;

/**
 * @brief Bounded undo/redo stack of EditorCommand pointers.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Editor
 *
 * Standard command-pattern history with FIFO eviction at capacity. Holds two
 * deques: undo (most recent action at the back) and redo (cleared on every
 * new mutation). Default capacity is 100 entries.
 *
 * Two ways to add a command:
 *  - Execute(cmd, tm, npcs): runs cmd->Apply, pushes to undo. Use for single-
 *    shot actions that have not been applied yet (single click, paste, etc.).
 *  - Push(cmd): pushes to undo without calling Apply. Use for stroke commits
 *    where the Touch path already mutated the tilemap during the drag - re-
 *    Applying would double-mutate.
 *
 * Commands are owned by the stack (via unique_ptr) and evicted oldest-first
 * once the undo deque exceeds the capacity. Every mutating call is null-safe:
 * a null command is a no-op.
 */
class UndoRedoStack
{
public:
    /// Undo-history depth used when no capacity is passed to the constructor.
    static constexpr std::size_t DEFAULT_CAPACITY = 100;

    /// Construct a stack holding up to @ref DEFAULT_CAPACITY undo entries.
    UndoRedoStack() = default;

    /**
     * @brief Construct a stack with an explicit undo-history depth.
     * @param capacity Maximum number of undo entries retained before the
     *                 oldest is evicted.
     */
    explicit UndoRedoStack(std::size_t capacity)
        : m_Capacity(capacity)
    {
    }

    /**
     * @brief Apply the command immediately, then push it onto the undo stack.
     *
     * Clears the redo stack. Use for single-shot actions that have not been
     * applied yet (single click, paste, etc.). A null @p cmd is ignored.
     *
     * @param cmd     Command to apply and record (ownership transferred).
     * @param tilemap Target tilemap forwarded to EditorCommand::Apply.
     * @param npcs    NPC registry forwarded to EditorCommand::Apply.
     */
    void Execute(std::unique_ptr<EditorCommand> cmd, Tilemap& tilemap, ecs::registry& npcs)
    {
        if (!cmd)
            return;
        cmd->Apply(tilemap, npcs);
        Push(std::move(cmd));
    }

    /**
     * @brief Record an already-applied command onto the undo stack.
     *
     * Pushes @p cmd without calling Apply (e.g. a stroke accumulator that
     * already mutated the tilemap during the drag; re-applying would double-
     * mutate). Clears the redo stack and evicts the oldest entry once the undo
     * deque exceeds @ref Capacity. A null @p cmd is ignored.
     *
     * @param cmd Already-applied command to record (ownership transferred).
     */
    void Push(std::unique_ptr<EditorCommand> cmd)
    {
        if (!cmd)
            return;
        m_Redo.clear();
        m_Undo.push_back(std::move(cmd));
        while (m_Undo.size() > m_Capacity)
            m_Undo.pop_front();
    }

    /**
     * @brief Revert the most recent command and move it to the redo stack.
     * @param tilemap Target tilemap forwarded to EditorCommand::Revert.
     * @param npcs    NPC registry forwarded to EditorCommand::Revert.
     * @return `false` if the undo stack was empty (nothing to revert).
     */
    bool Undo(Tilemap& tilemap, ecs::registry& npcs)
    {
        if (m_Undo.empty())
            return false;
        auto cmd = std::move(m_Undo.back());
        m_Undo.pop_back();
        cmd->Revert(tilemap, npcs);
        m_Redo.push_back(std::move(cmd));
        return true;
    }

    /**
     * @brief Re-apply the most recently reverted command and move it back to undo.
     * @param tilemap Target tilemap forwarded to EditorCommand::Apply.
     * @param npcs    NPC registry forwarded to EditorCommand::Apply.
     * @return `false` if the redo stack was empty (nothing to re-apply).
     */
    bool Redo(Tilemap& tilemap, ecs::registry& npcs)
    {
        if (m_Redo.empty())
            return false;
        auto cmd = std::move(m_Redo.back());
        m_Redo.pop_back();
        cmd->Apply(tilemap, npcs);
        m_Undo.push_back(std::move(cmd));
        return true;
    }

    /// Drop all history, leaving both the undo and redo stacks empty.
    void Clear()
    {
        m_Undo.clear();
        m_Redo.clear();
    }

    [[nodiscard]] bool CanUndo() const { return !m_Undo.empty(); }  ///< Undo available.
    [[nodiscard]] bool CanRedo() const { return !m_Redo.empty(); }  ///< Redo available.

    [[nodiscard]] std::size_t UndoSize() const { return m_Undo.size(); }  ///< Undo entry count.
    [[nodiscard]] std::size_t RedoSize() const { return m_Redo.size(); }  ///< Redo entry count.
    [[nodiscard]] std::size_t Capacity() const { return m_Capacity; }     ///< Max undo entries.

    /// Label of the next-to-undo command, or empty string if none.
    [[nodiscard]] std::string UndoLabel() const
    {
        return m_Undo.empty() ? std::string{} : m_Undo.back()->DebugLabel();
    }

    /// Label of the next-to-redo command, or empty string if none.
    [[nodiscard]] std::string RedoLabel() const
    {
        return m_Redo.empty() ? std::string{} : m_Redo.back()->DebugLabel();
    }

private:
    std::deque<std::unique_ptr<EditorCommand>> m_Undo;  ///< Undo history; newest action at back.
    std::deque<std::unique_ptr<EditorCommand>> m_Redo;  ///< Reverted commands; cleared on mutation.
    std::size_t m_Capacity = DEFAULT_CAPACITY;  ///< Max undo entries before oldest eviction.
};
