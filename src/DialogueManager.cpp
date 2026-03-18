#include "DialogueManager.h"
#include "GameStateManager.h"
#include "NonPlayerCharacter.h"

#include <iostream>

namespace
{
bool EvaluateCondition(const GameStateManager& state, const DialogueCondition& condition)
{
    switch (condition.type)
    {
        case DialogueCondition::Type::FLAG_SET:
            return state.HasFlag(condition.key);

        case DialogueCondition::Type::FLAG_NOT_SET:
            return !state.HasFlag(condition.key);

        case DialogueCondition::Type::FLAG_EQUALS:
            return state.GetFlagValue(condition.key) == condition.value;

            // No default case -- the compiler will warn if a new
            // DialogueCondition::Type enumerator is added without
            // a corresponding case here.
    }
    return false;
}

bool EvaluateConditions(const GameStateManager& state,
                        const std::vector<DialogueCondition>& conditions)
{
    for (const auto& condition : conditions)
    {
        if (!EvaluateCondition(state, condition))
        {
            return false;
        }
    }
    return true;
}
}  // namespace

// Invariants: when active, m_CurrentTree points at m_ActiveTree and
// m_VisibleOptions stores pointers into that owned copy.
DialogueManager::DialogueManager()
    : m_StateManager(nullptr),  // Set by Initialize() - evaluates conditions and stores flags
      m_Active(false),          // No conversation in progress until StartDialogue()
      m_CurrentTree(nullptr),   // Points to m_ActiveTree when dialogue is active
      m_CurrentNode(nullptr),   // Current position in the dialogue tree
      m_SelectedOption(0)       // UI cursor position in the options list
{
}

void DialogueManager::Initialize(GameStateManager* stateManager)
{
    m_StateManager = stateManager;
}

bool DialogueManager::StartDialogue(NonPlayerCharacter* npc)
{
    // Respect contract do not start if a conversation is already active
    if (m_Active)
    {
        std::cerr << "DialogueManager: Dialogue already active; refusing to start a new one"
                  << std::endl;
        return false;
    }

    // Validate required pointers
    if (!npc || !m_StateManager)
    {
        return false;
    }

    // Check if NPC has dialogue content
    if (!npc->HasDialogueTree())
    {
        return false;
    }

    // Get the starting point for this conversation
    const DialogueTree& tree = npc->GetDialogueTree();
    const DialogueNode* startNode = tree.GetStartNode();
    if (!startNode)
    {
        std::cerr << "DialogueManager: Start node not found in NPC tree" << std::endl;
        return false;
    }

    // Copy the tree locally so pointers remain valid even if the NPC is removed
    m_ActiveTree = tree;
    startNode = m_ActiveTree.GetStartNode();
    if (!startNode)
    {
        std::cerr << "DialogueManager: Start node missing after tree copy" << std::endl;
        return false;
    }

    // Initialize dialogue state
    m_Active = true;
    m_CurrentTree = &m_ActiveTree;
    m_CurrentNode = startNode;
    m_SelectedOption = 0;

    // Build the list of currently available options
    RefreshVisibleOptions();

    // TODO: surface start failures and logging through the game's logger instead of
    // std::cout/std::cerr.
    std::cout << "DialogueManager: Started dialogue with NPC '" << npc->GetType() << "'"
              << std::endl;
    return true;
}

void DialogueManager::EndDialogue()
{
    m_Active = false;
    m_CurrentTree = nullptr;
    m_CurrentNode = nullptr;
    m_ActiveTree = DialogueTree{};
    m_VisibleOptions.clear();
    m_SelectedOption = 0;

    std::cout << "DialogueManager: Dialogue ended" << std::endl;
}

void DialogueManager::SelectOption(int optionIndex)
{
    // Validate state and bounds
    if (!m_Active || !m_CurrentNode)
    {
        return;
    }
    if (optionIndex < 0 || optionIndex >= static_cast<int>(m_VisibleOptions.size()))
    {
        return;
    }

    const DialogueOption* option = m_VisibleOptions[optionIndex];

    // Apply any game state changes from this choice
    ExecuteConsequences(option->consequences);

    // Move to the next part of the conversation (or end it)
    TransitionToNode(option->nextNodeId);
}

void DialogueManager::TransitionToNode(const std::string& nodeId)
{
    // Empty nodeId means "end dialogue" (terminal option)
    if (nodeId.empty() || !m_CurrentTree)
    {
        EndDialogue();
        return;
    }

    // Look up the next node in the tree
    const DialogueNode* nextNode = m_CurrentTree->GetNode(nodeId);
    if (!nextNode)
    {
        std::cerr << "DialogueManager: Node not found: " << nodeId << std::endl;
        EndDialogue();
        return;
    }

    // Update state and rebuild options for the new node
    m_CurrentNode = nextNode;
    m_SelectedOption = 0;
    RefreshVisibleOptions();
}

void DialogueManager::SelectPrevious()
{
    if (m_VisibleOptions.empty())
    {
        return;
    }

    m_SelectedOption--;
    if (m_SelectedOption < 0)
    {
        // Wrap from first to last
        m_SelectedOption = static_cast<int>(m_VisibleOptions.size()) - 1;
    }
}

void DialogueManager::SelectNext()
{
    if (m_VisibleOptions.empty())
    {
        return;
    }

    m_SelectedOption++;
    if (m_SelectedOption >= static_cast<int>(m_VisibleOptions.size()))
    {
        // Wrap from last to first
        m_SelectedOption = 0;
    }
}

void DialogueManager::ConfirmSelection()
{
    if (m_VisibleOptions.empty())
    {
        // No options available treat as end of dialogue
        // TODO: support non-choice nodes (e.g., auto-advance) instead of always ending here.
        EndDialogue();
    }
    else
    {
        // Process the currently highlighted option
        SelectOption(m_SelectedOption);
    }
}

void DialogueManager::ExecuteConsequences(const std::vector<DialogueConsequence>& consequences)
{
    if (!m_StateManager)
    {
        return;
    }

    for (const auto& cons : consequences)
    {
        switch (cons.type)
        {
            case DialogueConsequence::Type::SET_FLAG:
                // Mark a boolean flag as true (e.g., "quest_accepted")
                m_StateManager->SetFlag(cons.key, true);
                std::cout << "DialogueManager: Set flag '" << cons.key << "' = true" << std::endl;
                break;

            case DialogueConsequence::Type::CLEAR_FLAG:
                // Remove/unset a flag key entirely (e.g., "has_item")
                m_StateManager->ClearFlag(cons.key);
                std::cout << "DialogueManager: Cleared flag '" << cons.key << "'" << std::endl;
                break;

            case DialogueConsequence::Type::SET_FLAG_VALUE:
                // Set a flag to a specific string value (e.g., "reputation" = "friendly")
                m_StateManager->SetFlagValue(cons.key, cons.value);
                std::cout << "DialogueManager: Set flag '" << cons.key << "' = '" << cons.value
                          << "'" << std::endl;
                break;

            default:
                std::cerr << "DialogueManager: Unhandled consequence type "
                          << static_cast<int>(cons.type) << std::endl;
                break;
        }
        // TODO: extend consequences to support scripted actions or item grants; log unhandled types
        // when the enum grows.
    }
}

void DialogueManager::RefreshVisibleOptions()
{
    m_VisibleOptions.clear();

    if (!m_CurrentNode || !m_StateManager)
    {
        return;
    }

    // Filter options based on their conditions
    for (const auto& option : m_CurrentNode->options)
    {
        // Only show options where all conditions are satisfied
        if (EvaluateConditions(*m_StateManager, option.conditions))
        {
            m_VisibleOptions.push_back(&option);
        }
    }

    // Ensure selected index is still valid after filtering
    if (m_SelectedOption >= static_cast<int>(m_VisibleOptions.size()))
    {
        m_SelectedOption = std::max(0, static_cast<int>(m_VisibleOptions.size()) - 1);
    }
}
