#pragma once

#include "DialogueTypes.hpp"

#include <ecs.hpp>

#include <string>
#include <vector>

// Forward declarations
class GameStateManager;

/**
 * @class DialogueManager
 * @brief Runtime dialogue controller for NPC conversations.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * DialogueManager orchestrates dialogue interactions between the player
 * and NPCs. It maintains conversation state, filters available options based
 * on game conditions, and executes consequences when choices are made.
 * Dialogue trees are owned centrally by the @ref DialogueStore in the registry's
 * globals; @ref StartDialogue resolves the NPC's @ref DialogueHandle through it and
 * copies the tree into @c m_ActiveTree for the duration of the conversation.
 *
 * @par Key responsibilities
 * - Managing active conversation state (current tree, node, options)
 * - Evaluating conditions to filter visible options
 * - Executing consequences (flag changes)
 * - Providing UI state for rendering (selected option, visible choices)
 *
 * @par Conversation flow
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     classDef idle fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef active fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef done fill:#4a2020,stroke:#ef4444,color:#e2e8f0
 *
 *     state "Idle" as Idle:::idle
 *     state "Showing Node" as Show:::active
 *     state "Awaiting Input" as Wait:::active
 *     state "Executing Consequences" as Exec:::active
 *     state "Ended" as Done:::done
 *
 *     [*] --> Idle
 *     Idle --> Show: StartDialogue(npc)
 *     Show --> Wait: RefreshVisibleOptions
 *     Wait --> Wait: SelectPrevious / SelectNext
 *     Wait --> Exec: ConfirmSelection (some option visible)
 *     Wait --> Done: ConfirmSelection (no visible option)
 *     Exec --> Show: nextNodeId resolves to a node
 *     Exec --> Done: nextNodeId empty
 *     Exec --> Done: nextNodeId unknown (logged as an error)
 *     Show --> Done: EndDialogue
 *     Wait --> Done: EndDialogue
 *     Done --> Idle
 * </pre>
 * @endhtmlonly
 *
 * @par Usage example
 * @code{.cpp}
 * DialogueManager d;
 * d.Initialize(&stateManager);
 *
 * // Start conversation with an NPC (entity handle + its registry)
 * if (d.StartDialogue(npc, world)) {
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
 * @par Thread safety
 * Not thread-safe. Call every method from
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
     * @param stateManager Non-owning pointer to the game state manager used for flag
     *                     evaluation. Must outlive this manager; if it is null,
     *                     @ref StartDialogue refuses to start a conversation.
     */
    void Initialize(GameStateManager* stateManager);

    /**
     * @name Dialogue flow control
     * @brief Methods for starting, advancing, and ending conversations.
     * @{
     */

    /**
     * @brief Start dialogue with an NPC using their assigned tree.
     *
     * Resolves @p npc 's @c Dialogue component and its @ref DialogueHandle through the
     * @ref DialogueStore published in @p world 's globals, then copies that tree into
     * @c m_ActiveTree and enters the tree's start node. The copy is what lets the node
     * and option pointers stay valid if the NPC is despawned mid-conversation.
     *
     * @param npc   Entity handle (a value, not a pointer) of the NPC to converse with.
     * @param world Registry the handle belongs to. Required: it is used to liveness-
     *              check @p npc, to fetch the @c Dialogue component, and to reach the
     *              @ref DialogueStore in @c globals(). Not retained.
     * @return True if dialogue started successfully; false on any failed
     *         prerequisite (all of which are silent except the two logged below).
     *
     * @par Prerequisites
     * - No dialogue currently active (logs an error and fails)
     * - @p npc alive in @p world, and @ref Initialize already called
     * - @p npc has a @c Dialogue component whose handle resolves to a non-empty tree
     * - that tree has a start node (logs an error and fails)
     */
    bool StartDialogue(ecs::entity npc, const ecs::registry& world);

    /**
     * @brief End the current dialogue.
     *
     * Immediately terminates the conversation, resetting all
     * dialogue state. Safe to call even if no dialogue is active.
     */
    void EndDialogue();

    /// @}

    /**
     * @name Selection and state queries
     * @brief Methods for reading dialogue state and for moving the option cursor.
     * @{
     */

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
     * An empty @p nodeId ends the dialogue, and so does a null current tree. A
     * non-empty id that the active tree does not contain is not a no-op: it logs an
     * error and ends the dialogue as well.
     *
     * @param nodeId ID of node to transition to (empty string ends dialogue)
     *
     * @post On success the selection resets to index 0 and the visible-option list
     *       is rebuilt.
     */
    void TransitionToNode(const std::string& nodeId);

    /// @name Game system references
    /// @{

    /// Non-owning flag store set by @ref Initialize; must outlive this manager. Null
    /// blocks @ref StartDialogue and makes consequence execution a no-op.
    GameStateManager* m_StateManager = nullptr;

    /// @}

    /// @name Active dialogue state
    /// @{

    DialogueTree
        m_ActiveTree;       ///< Owned copy of active dialogue tree (avoids dangling NPC pointers).
    bool m_Active = false;  ///< True while a conversation is in progress.
    const DialogueTree* m_CurrentTree = nullptr;  ///< Points into m_ActiveTree.
    const DialogueNode* m_CurrentNode = nullptr;  ///< Current node being displayed.

    /// @}

    /// @name UI state
    /// @{

    std::vector<const DialogueOption*>
        m_VisibleOptions;      ///< Pointers into m_ActiveTree nodes; rebuilt on node change.
    int m_SelectedOption = 0;  ///< Index of highlighted option in m_VisibleOptions.

    /// @}
};
