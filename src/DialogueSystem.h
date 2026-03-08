#pragma once

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @struct DialogueCondition
 * @brief Condition that must be met for a dialogue option to appear.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Conditions are evaluated against the GameStateManager's flag storage.
 * All conditions on an option must pass for it to be visible.
 *
 * @par Example
 * @code{.cpp}
 * // Only show option if player has completed intro quest
 * DialogueCondition c(DialogueCondition::Type::FLAG_SET, "intro_complete");
 * option.conditions.push_back(c);
 * @endcode
 */
struct DialogueCondition
{
    /**
     * @brief Types of condition checks.
     */
    enum class Type
    {
        FLAG_SET,      ///< Check if flag key exists (presence-based, any value)
        FLAG_NOT_SET,  ///< Check if flag key does not exist
        FLAG_EQUALS    ///< Check if flag equals a specific string value
    };

    Type type = Type::FLAG_SET;  ///< The type of condition check
    std::string key;             ///< Flag name to check in GameStateManager
    std::string value;           ///< Expected value (only used for FLAG_EQUALS)

    DialogueCondition() = default;

    /**
     * @brief Construct a condition.
     * @param t Condition type
     * @param k Flag key to check
     * @param v Expected value (for FLAG_EQUALS)
     */
    DialogueCondition(Type t, const std::string& k, const std::string& v = "")
        : type(t), key(k), value(v)
    {
    }
};

/**
 * @struct DialogueConsequence
 * @brief Action that executes when a dialogue option is selected.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Consequences modify game state when the player selects an option.
 * Multiple consequences can be attached to a single option and are
 * executed in order.
 *
 * @par Example
 * @code{.cpp}
 * // Mark quest as accepted when player chooses this option
 * DialogueConsequence c(DialogueConsequence::Type::SET_FLAG, "quest_accepted");
 * option.consequences.push_back(c);
 * @endcode
 */
struct DialogueConsequence
{
    /**
     * @brief Types of consequences.
     */
    enum class Type
    {
        SET_FLAG,        ///< Set a boolean flag to true
        CLEAR_FLAG,      ///< Remove/unset a flag key
        SET_FLAG_VALUE,  ///< Set a flag to a specific string value
    };

    Type type = Type::SET_FLAG;  ///< The type of consequence
    std::string key;             ///< Flag name
    std::string value;           ///< New value (for SET_FLAG_VALUE)

    DialogueConsequence() = default;

    /**
     * @brief Construct a consequence.
     * @param t Consequence type
     * @param k Flag key or NPC type
     * @param v New value
     */
    DialogueConsequence(Type t, const std::string& k, const std::string& v = "")
        : type(t), key(k), value(v)
    {
    }
};

/**
 * @struct DialogueOption
 * @brief A single response option the player can choose.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Options are displayed as choices when rendering a dialogue node.
 * Each option can have conditions that determine visibility and
 * consequences that execute when selected.
 *
 * @par Example
 * @code{.cpp}
 * DialogueOption o("Tell me about the quest", "quest_info");
 * o.conditions.push_back({DialogueCondition::Type::FLAG_NOT_SET, "knows_quest"});
 * o.consequences.push_back({DialogueConsequence::Type::SET_FLAG, "knows_quest"});
 * @endcode
 */
struct DialogueOption
{
    std::string text;                               ///< Display text shown to player
    std::string nextNodeId;                         ///< ID of next node (empty ends dialogue)
    std::vector<DialogueCondition> conditions;      ///< All must pass to show option
    std::vector<DialogueConsequence> consequences;  ///< Executed when option selected

    DialogueOption() = default;

    /**
     * @brief Construct a simple option.
     * @param t Display text
     * @param next Next node ID (empty to end dialogue)
     */
    DialogueOption(const std::string& t, const std::string& next = "") : text(t), nextNodeId(next)
    {
    }
};

/**
 * @struct DialogueNode
 * @brief A single node in the dialogue tree representing one exchange.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * Each node contains the speaker's text and available response options.
 * The dialogue progresses by transitioning between nodes based on
 * which option the player selects.
 *
 * @par Example
 * @code{.cpp}
 * DialogueNode n("greeting", "Stranger", "Hello there, traveler!");
 * n.options.push_back({"Who are you?", "introduce"});
 * n.options.push_back({"Goodbye", ""});  // Empty ends dialogue
 * @endcode
 */
struct DialogueNode
{
    std::string id;                       ///< Unique identifier within the tree
    std::string speaker;                  ///< Name displayed above dialogue text
    std::string text;                     ///< The dialogue text to display
    std::vector<DialogueOption> options;  ///< Available player responses

    DialogueNode() = default;

    /**
     * @brief Construct a dialogue node.
     * @param nodeId Unique identifier
     * @param spk Speaker name
     * @param txt Dialogue text
     */
    DialogueNode(const std::string& nodeId, const std::string& spk, const std::string& txt)
        : id(nodeId), speaker(spk), text(txt)
    {
    }

    /**
     * @brief Check if this is a terminal node.
     *
     * A node is terminal if it has no options, or all options
     * have empty nextNodeId (meaning they all end the dialogue).
     *
     * @return True if selecting any option ends the dialogue
     */
    [[nodiscard]] bool IsTerminal() const
    {
        if (options.empty())
            return true;
        for (const auto& opt : options)
        {
            if (!opt.nextNodeId.empty())
                return false;
        }
        return true;
    }
};

/**
 * @struct DialogueTree
 * @brief Complete dialogue tree for an NPC conversation.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Dialogue
 *
 * A dialogue tree contains all nodes for a conversation and specifies
 * which node to start from. Trees are stored directly on NPCs rather
 * than in a central repository.
 *
 * @par Example
 * @code{.cpp}
 * DialogueTree t("stranger_intro", "greeting");
 *
 * DialogueNode g("greeting", "Stranger", "Hello!");
 * g.options.push_back({"Hi!", "response"});
 * t.AddNode(g);
 *
 * DialogueNode r("response", "Stranger", "Nice to meet you.");
 * r.options.push_back({"Goodbye", ""}); // End dialogue
 * t.AddNode(r);
 * @endcode
 *
 * @par Architecture
 * Dialogues are organized as trees where each node represents a point
 * in the conversation. The JSON format uses a simplified syntax:
 * @code{.json}
 * {
 *   "dialogueTree": {
 *     "speaker": "Marcus",
 *     "start": "greeting",
 *     "nodes": {
 *       "greeting": {
 *         "text": "Hello, traveler!",
 *         "choices": [
 *           { "text": "Who are you?", "goto": "introduce" },
 *           { "text": "Need work.", "goto": "work", "when": "!talked" },
 *           { "text": "Goodbye." }
 *         ]
 *       },
 *       "introduce": { ... }
 *     }
 *   }
 * }
 * @endcode
 *
 * @par JSON Format Reference
 * | Field      | Description                                       |
 * |------------|---------------------------------------------------|
 * | speaker    | Default speaker for all nodes (inherits to nodes) |
 * | start      | Starting node ID (defaults to "start")            |
 * | text       | Dialogue text displayed to player                 |
 * | choices    | Array of player response options                  |
 * | goto       | Next node ID (empty or omitted ends dialogue)     |
 * | when       | Condition string (see below)                      |
 * | do         | Consequence array (see below)                     |
 *
 * @par Condition Syntax ("when" field)
 * Conditions control when choices are visible:
 * | Syntax         | Description                     |
 * |----------------|---------------------------------|
 * | `flag`         | Show if flag key exists         |
 * | `!flag`        | Show if flag key does not exist |
 * | `flag=value`   | Show if flag equals value       |
 * | `a & b`        | Multiple conditions (AND)       |
 *
 * @par Consequence Syntax ("do" field)
 * Consequences modify game state when a choice is selected:
 * | Syntax              | Description                         |
 * |---------------------|-------------------------------------|
 * | `"flag"`            | Set flag to true                    |
 * | `"-flag"`           | Clear/remove flag                   |
 * | `"flag=value"`      | Set flag to specific value          |
 * | `"accepted_x:desc"` | Set flag to true                    |
 *
 * @see DialogueManager for runtime dialogue control
 * @see GameStateManager for flag storage and evaluation
 */
struct DialogueTree
{
    std::string id;                                       ///< Unique tree identifier
    std::string startNodeId;                              ///< ID of the entry point node
    std::unordered_map<std::string, DialogueNode> nodes;  ///< All nodes keyed by ID

    DialogueTree() = default;

    /**
     * @brief Construct a dialogue tree.
     * @param treeId Unique identifier for this tree
     * @param startNode ID of the starting node
     */
    DialogueTree(const std::string& treeId, const std::string& startNode)
        : id(treeId), startNodeId(startNode)
    {
    }

    /**
     * @brief Get a node by ID.
     * @param nodeId The node identifier to look up
     * @return Pointer to the node, or nullptr if not found
     */
    [[nodiscard]] const DialogueNode* GetNode(const std::string& nodeId) const
    {
        auto it = nodes.find(nodeId);
        return (it != nodes.end()) ? &it->second : nullptr;
    }

    /**
     * @brief Get the starting node for this tree.
     * @return Pointer to the start node, or nullptr if not found
     */
    [[nodiscard]] const DialogueNode* GetStartNode() const { return GetNode(startNodeId); }

    /**
     * @brief Add a node to the tree.
     * @param node The node to add (copied into the tree)
     */
    void AddNode(const DialogueNode& node) { nodes[node.id] = node; }
};
