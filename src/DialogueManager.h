#pragma once

#include "DialogueTypes.h"

#include <string>
#include <vector>

// Forward declarations
class GameStateManager;
class NonPlayerCharacter;

/**
 * @class DialogueManager
 * @brief Runtime dialogue controller for NPC conversations.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * DialogueManager orchestrates dialogue interactions between the player
 * and NPCs. It maintains conversation state, filters available options based
 * on game conditions, and executes consequences when choices are made.
 * Dialogue trees are stored directly on NPCs rather than loaded centrally.
 *
 * @par Key Responsibilities
 * - Managing active conversation state (current tree, node, options)
 * - Evaluating conditions to filter visible options
 * - Executing consequences (flag changes)
 * - Providing UI state for rendering (selected option, visible choices)
 *
 * @par Usage Example
 * @code{.cpp}
 * DialogueManager d;
 * d.Initialize(&stateManager);
 *
 * // Start conversation with an NPC
 * if (d.StartDialogue(npc)) {
 *     // Dialogue is now active
 * }
 *
 * // Handle player input each frame
 * if (d.IsActive()) {
 *     if (upPressed) d.SelectPrevious();
 *     if (downPressed) d.SelectNext();
 *     if (confirmPressed) d.ConfirmSelection();
 * }
 *
 * // Render current dialogue state
 * if (d.IsActive()) {
 *     const auto* node = d.GetCurrentNode();
 *     RenderDialogue(node->speaker, node->text);
 *     for (const auto* opt : d.GetVisibleOptions()) {
 *         RenderOption(opt->text);
 *     }
 * }
 * @endcode
 *
 * @par Thread Safety
 * This class is not thread-safe. All methods should be called from
 * the main game thread.
 *
 * @see DialogueTree, DialogueNode, DialogueOption
 * @see GameStateManager for flag storage
 */
class DialogueManager
{
public:
    /**
     * @brief Default constructor.
     *
     * Creates an inactive dialogue manager. Call Initialize() before use.
     */
    DialogueManager();

    /**
     * @brief Initialize with references to game systems.
     *
     * Must be called before using any other DialogueManager methods.
     * The manager stores non-owning pointers to these systems for
     * condition evaluation and consequence execution.
     *
     * @param stateManager Reference to game state manager (for flag evaluation)
     */
    void Initialize(GameStateManager* stateManager);

    /// @name Dialogue Flow Control
    /// @brief Methods for starting, advancing, and ending conversations.
    /// @{

    /**
     * @brief Start dialogue with an NPC using their assigned tree.
     *
     * Looks up the NPC's dialogue tree and initiates the conversation
     * starting from the tree's designated start node.
     *
     * @param npc Pointer to the NPC to converse with
     * @return True if dialogue started successfully
     *
     * @par Prerequisites
     * - NPC must have a valid dialogue tree assigned
     * - No dialogue currently active (will fail if already in conversation)
     */
    bool StartDialogue(NonPlayerCharacter* npc);

    /**
     * @brief End the current dialogue.
     *
     * Immediately terminates the conversation, resetting all
     * dialogue state. Safe to call even if no dialogue is active.
     */
    void EndDialogue();

    /// @}

    /// @name State Queries
    /// @brief Methods for querying current dialogue state.
    /// @{

    /**
     * @brief Check if dialogue is currently active.
     *
     * @return True if a conversation is in progress
     */
    [[nodiscard]] bool IsActive() const { return m_Active; }

    /**
     * @brief Get the current dialogue node.
     *
     * @return Pointer to current node, or nullptr if inactive
     */
    [[nodiscard]] const DialogueNode* GetCurrentNode() const { return m_CurrentNode; }

    /**
     * @brief Get visible options (filtered by conditions).
     *
     * Returns only the options whose conditions are currently met.
     * This list is refreshed whenever the current node changes. Pointers
     * remain valid until the next refresh because the dialogue tree is
     * stored locally while a conversation is active.
     *
     * @return Vector of pointers to visible options
     */
    [[nodiscard]] const std::vector<const DialogueOption*>& GetVisibleOptions() const
    {
        return m_VisibleOptions;
    }

    /**
     * @brief Get currently selected option index.
     *
     * @return Index of highlighted option (0-based)
     */
    [[nodiscard]] int GetSelectedOptionIndex() const { return m_SelectedOption; }

    /**
     * @brief Move selection up (previous option).
     *
     * Wraps around to last option if at the top.
     */
    void SelectPrevious();

    /**
     * @brief Move selection down (next option).
     *
     * Wraps around to first option if at the bottom.
     */
    void SelectNext();

    /**
     * @brief Confirm current selection.
     *
     * Executes the selected option's consequences and transitions
     * to the next node. Ends dialogue if no options available.
     */
    void ConfirmSelection();

    /// @}

private:
    /**
     * @brief Select a dialogue option by index.
     *
     * Triggers the selected option's consequences and transitions
     * to the next node. If the option's nextNodeId is empty,
     * the dialogue ends.
     *
     * @param optionIndex Index in the visible options list (0-based)
     */
    void SelectOption(int optionIndex);

    /**
     * @brief Execute consequences for a selected option.
     *
     * Processes each consequence in order, modifying game state flags.
     *
     * @param consequences List of consequences to execute
     */
    void ExecuteConsequences(const std::vector<DialogueConsequence>& consequences);

    /**
     * @brief Refresh the visible options list based on current conditions.
     *
     * Evaluates each option's conditions against the current game state
     * and populates m_VisibleOptions with those that pass all checks.
     */
    void RefreshVisibleOptions();

    /**
     * @brief Transition to a specific node.
     *
     * @param nodeId ID of node to transition to (empty string ends dialogue)
     */
    void TransitionToNode(const std::string& nodeId);

    /// @name Game System References
    /// @{

    GameStateManager* m_StateManager = nullptr;

    /// @}

    /// @name Active Dialogue State
    /// @{

    DialogueTree
        m_ActiveTree;       ///< Owned copy of active dialogue tree (avoids dangling NPC pointers)
    bool m_Active = false;  ///< True while a conversation is in progress
    const DialogueTree* m_CurrentTree = nullptr;  ///< Points into m_ActiveTree
    const DialogueNode* m_CurrentNode = nullptr;  ///< Current node being displayed

    /// @}

    /// @name UI State
    /// @{

    std::vector<const DialogueOption*>
        m_VisibleOptions;      ///< Pointers into m_ActiveTree nodes; rebuilt on node change
    int m_SelectedOption = 0;  ///< Index of highlighted option in m_VisibleOptions

    /// @}
};
