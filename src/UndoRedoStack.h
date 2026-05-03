#pragma once

#include "EditorCommand.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Tilemap;
class NonPlayerCharacter;

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
 */
class UndoRedoStack
{
public:
    static constexpr std::size_t DEFAULT_CAPACITY = 100;

    UndoRedoStack() = default;
    explicit UndoRedoStack(std::size_t capacity)
        : m_Capacity(capacity)
    {
    }

    /// Apply the command immediately, then push it onto the undo stack.
    /// Clears the redo stack.
    void Execute(std::unique_ptr<EditorCommand> cmd,
                 Tilemap& tilemap,
                 std::vector<NonPlayerCharacter>& npcs)
    {
        if (!cmd)
            return;
        cmd->Apply(tilemap, npcs);
        Push(std::move(cmd));
    }

    /// Push a command that was already applied (e.g., by the stroke
    /// accumulator) onto the undo stack without calling Apply. Clears redo.
    void Push(std::unique_ptr<EditorCommand> cmd)
    {
        if (!cmd)
            return;
        m_Redo.clear();
        m_Undo.push_back(std::move(cmd));
        while (m_Undo.size() > m_Capacity)
            m_Undo.pop_front();
    }

    /// Pop the most recent command, revert it, move to the redo stack.
    /// Returns false if the undo stack is empty.
    bool Undo(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs)
    {
        if (m_Undo.empty())
            return false;
        auto cmd = std::move(m_Undo.back());
        m_Undo.pop_back();
        cmd->Revert(tilemap, npcs);
        m_Redo.push_back(std::move(cmd));
        return true;
    }

    /// Pop the most recent reverted command, re-apply it, move to undo.
    /// Returns false if the redo stack is empty.
    bool Redo(Tilemap& tilemap, std::vector<NonPlayerCharacter>& npcs)
    {
        if (m_Redo.empty())
            return false;
        auto cmd = std::move(m_Redo.back());
        m_Redo.pop_back();
        cmd->Apply(tilemap, npcs);
        m_Undo.push_back(std::move(cmd));
        return true;
    }

    void Clear()
    {
        m_Undo.clear();
        m_Redo.clear();
    }

    [[nodiscard]] bool CanUndo() const { return !m_Undo.empty(); }
    [[nodiscard]] bool CanRedo() const { return !m_Redo.empty(); }

    [[nodiscard]] std::size_t UndoSize() const { return m_Undo.size(); }
    [[nodiscard]] std::size_t RedoSize() const { return m_Redo.size(); }
    [[nodiscard]] std::size_t Capacity() const { return m_Capacity; }

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
    std::deque<std::unique_ptr<EditorCommand>> m_Undo;
    std::deque<std::unique_ptr<EditorCommand>> m_Redo;
    std::size_t m_Capacity = DEFAULT_CAPACITY;
};
