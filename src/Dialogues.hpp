#pragma once

#include "DialogueTypes.hpp"

#include <string>

/**
 * @struct MysteryDialogueData
 * @brief Data-driven template for generating mystery/quest dialogue trees.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Editor
 *
 * Each entry describes one complete quest dialogue: the NPC's opening line,
 * follow-up questions, quest offer/accept/decline text, and a revisit prompt.
 * `BuildMysteryDialogueTree()` inflates this into a full `DialogueTree`.
 *
 * @par Pattern
 * The flat-data + builder pattern lets designers add a new mystery NPC by
 * appending one entry to `kMysteryDialogues[]` without writing tree code:
 *
 * @code{.cpp}
 * // In Dialogues.cpp:
 * const MysteryDialogueData kMysteryDialogues[] = {
 *     { "lost_locket", "Tessa", "lost_locket_quest",
 *       "details", "I lost my locket...", ...},
 *     // add more entries here; no other file needs to change
 * };
 * @endcode
 *
 * Then the editor's NPC placement picks one randomly and calls
 * `BuildMysteryDialogueTree(tree, npcName, entry)`.
 *
 * @warning Every entry in `kMysteryDialogues[]` is a positional brace initializer of
 * sixteen bare string literals, so transposing two same-shaped fields (say
 * `declineText` and `dismissText`) compiles cleanly, is invisible in review, and is
 * only observable by playing the conversation. This is exactly the case
 * CONTRIBUTING.md's "prefer named data over positional data" rule warns about; the
 * per-field docs below are the only guard rail there currently is.
 *
 * @par Generated shape
 * `BuildMysteryDialogueTree` always emits four nodes - `start`, @ref detailsNodeId,
 * `accept`, `update` - with @ref flagName gating which options appear on `start`:
 * @verbatim
 *   start ---- askMoreText ------> [detailsNodeId]   (only while flag not set)
 *     |------- dismissText ------> (end)             (only while flag not set)
 *     '------- updatePromptText -> update            (only once flag is set)
 *
 *   [detailsNodeId]
 *     |------- questOfferText ---> accept   (only while flag not set;
 *     |                                      sets flag = questJournalText)
 *     '------- declineText ------> (end)    (always shown)
 *
 *   accept ---- acceptOptionText -> (end)
 *   update ---- updateOptionText -> (end)
 * @endverbatim
 */
struct MysteryDialogueData
{
    const char* treeId;   ///< Becomes `DialogueTree::id`; not shown to the player.
    const char* npcName;  ///< Written to `outNpcName` and used as every node's speaker.
    /**
     * @brief GameStateManager key that records acceptance, by convention `accepted_&lt;quest&gt;`.
     *
     * Accepting stores @ref questJournalText under it, and its presence is what flips the
     * `start` node from the intro options to the revisit option.
     */
    const char* flagName;
    /**
     * @brief Id of the second node.
     *
     * Also the `goto` target of the @ref askMoreText option, so the two must stay in
     * step. Entries use `"details"` except the UFO one, which uses `"lights"`.
     */
    const char* detailsNodeId;
    const char* startText;    ///< Text of the `start` node - the NPC's opening line.
    const char* askMoreText;  ///< `start` option -> details node. Hidden once accepted.
    /// `start` option that ends the conversation immediately ("I can't help").
    /// Hidden once accepted.
    const char* dismissText;
    /// `start` option shown only after acceptance; goes to the `update` node.
    const char* updatePromptText;
    const char* detailsText;  ///< Text of the details node - the full backstory.
    /// Details option that accepts the quest: goes to the `accept` node and sets
    /// @ref flagName to @ref questJournalText. Hidden once already accepted.
    const char* questOfferText;
    /// The value stored in @ref flagName on acceptance - i.e. the journal line
    /// `GameStateManager::GetQuestDescription` hands back. Never shown as an option.
    const char* questJournalText;
    /// Details option that ends the conversation without accepting. Unconditional,
    /// so it stays selectable even after the quest was taken.
    const char* declineText;
    const char* acceptText;        ///< Text of the `accept` node - the thank-you line.
    const char* acceptOptionText;  ///< Sole `accept` option; ends the conversation.
    const char* updateText;        ///< Text of the `update` node - the revisit nag.
    const char* updateOptionText;  ///< Sole `update` option; ends the conversation.
};

/// Pool of mystery dialogue templates used when placing NPCs in the editor.
extern const MysteryDialogueData kMysteryDialogues[];
/// Number of entries in @ref kMysteryDialogues. The extern above declares an array of
/// unknown bound, so the count is recoverable only in Dialogues.cpp and is exported here.
extern const int kMysteryDialogueCount;

/**
 * @brief Inflate a MysteryDialogueData template into a full DialogueTree.
 *
 * Creates start -> details -> accept/decline -> update nodes with flag-gated
 * options so the dialogue remembers whether the player accepted the quest.
 *
 * @param tree       Tree to populate. Its id, start node id and nodes are written;
 *                   any pre-existing nodes are left in place.
 * @param outNpcName Receives @p d.npcName, and is used as the speaker on every node.
 * @param d          Template to inflate; see @ref MysteryDialogueData for which
 *                   string lands where.
 */
void BuildMysteryDialogueTree(DialogueTree& tree,
                              std::string& outNpcName,
                              const MysteryDialogueData& d);

/**
 * @brief Build a fourth-wall-breaking dialogue tree for NPC "Wyatt".
 *
 * The NPC progressively reveals awareness of being inside a tile editor,
 * referencing cursors, sprites, and 16-pixel grids. Purely branching: it reads and
 * writes no GameStateManager flags, so every visit restarts from the top.
 *
 * @param tree       Tree to populate (id `"fourth_wall"`, start node `"start"`).
 * @param outNpcName Receives `"Wyatt"`.
 */
void BuildEditorAwareDialogueTree(DialogueTree& tree, std::string& outNpcName);

/**
 * @brief Build a progressively annoyed dialogue tree for NPC "Salma".
 *
 * Uses a visit counter (flag "talked_to_salma", stored as the strings "1".."5") to
 * escalate irritation across five visits, from polite dismissal to existential
 * breakdown. The `start` node is a pure router: the first greeting is gated on
 * FLAG_NOT_SET and the rest on FLAG_EQUALS against the counter, and every branch it
 * leads to bumps the counter. The fifth branch also matches a counter of "5", so
 * visits past the fifth loop on it forever.
 *
 * @param tree       Tree to populate (id `"grumpy_npc"`, start node `"start"`).
 * @param outNpcName Receives `"Salma"`.
 */
void BuildAnnoyedNPCDialogueTree(DialogueTree& tree, std::string& outNpcName);
