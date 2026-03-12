#pragma once

#include "DialogueTypes.h"

#include <string>

/**
 * @struct MysteryDialogueData
 * @brief Data-driven template for generating mystery/quest dialogue trees.
 * @ingroup Editor
 *
 * Each entry describes one complete quest dialogue: the NPC's opening line,
 * follow-up questions, quest offer/accept/decline text, and a revisit prompt.
 * BuildMysteryDialogueTree() inflates this into a full DialogueTree.
 */
struct MysteryDialogueData
{
    const char* treeId;
    const char* npcName;
    const char* flagName;
    const char* detailsNodeId;
    const char* startText;
    const char* askMoreText;
    const char* dismissText;
    const char* updatePromptText;
    const char* detailsText;
    const char* questOfferText;
    const char* questJournalText;
    const char* declineText;
    const char* acceptText;
    const char* acceptOptionText;
    const char* updateText;
    const char* updateOptionText;
};

/// Pool of mystery dialogue templates used when placing NPCs in the editor.
extern const MysteryDialogueData kMysteryDialogues[];
extern const int kMysteryDialogueCount;

/**
 * @brief Inflate a MysteryDialogueData template into a full DialogueTree.
 *
 * Creates start -> details -> accept/decline -> update nodes with flag-gated
 * options so the dialogue remembers whether the player accepted the quest.
 */
void BuildMysteryDialogueTree(DialogueTree& tree,
                              std::string& outNpcName,
                              const MysteryDialogueData& d);

/**
 * @brief Build a fourth-wall-breaking dialogue tree for NPC "Wyatt".
 *
 * The NPC progressively reveals awareness of being inside a tile editor,
 * referencing cursors, sprites, and 16-pixel grids.
 */
void BuildEditorAwareDialogueTree(DialogueTree& tree, std::string& outNpcName);

/**
 * @brief Build a progressively annoyed dialogue tree for NPC "Salma".
 *
 * Uses a visit counter (flag "talked_to_salma") to escalate irritation
 * across five visits, from polite dismissal to existential breakdown.
 */
void BuildAnnoyedNPCDialogueTree(DialogueTree& tree, std::string& outNpcName);
