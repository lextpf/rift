#include "EditorDialogue.h"

const MysteryDialogueData kMysteryDialogues[] = {
    {"ufo_sighting",
     "Anna",
     "accepted_ufo_quest",
     "lights",
     "Please, you have to help me! My brother went to investigate strange lights in the northern "
     "field three nights ago. He hasn't come back.",
     "Strange lights?",
     "I'm sorry, I can't help.",
     "Any news about your brother?",
     "Green lights, hovering in the sky. People say it's a UFO. Others have gone missing too. Will "
     "you look for him?",
     "I'll find your brother.",
     "Find Anna's missing brother in the northern field!",
     "That sounds too dangerous.",
     "Thank you! The field is north of town. Please be careful, and bring him home safe.",
     "I'll do my best.",
     "Have you found him? Please, the northern field... that's where he went. I can't lose him.",
     "I'm still looking."},
    {"bigfoot_sighting",
     "Mona",
     "accepted_bigfoot_quest",
     "details",
     "I know what I saw. Eight feet tall, covered in fur, walking upright through the forest. "
     "Everyone thinks I'm crazy.",
     "Tell me more about what you saw.",
     "Probably just a bear.",
     "Found any more evidence?",
     "It left tracks, huge ones, near the old mill. I found tufts of hair too. Something's out "
     "there. Will you help me prove it?",
     "I'll investigate the old mill.",
     "Investigate the strange tracks near the old mill.",
     "I'd rather not get involved.",
     "Finally, someone who believes me! The mill is east of here. Look for broken branches and "
     "disturbed earth. And be careful.",
     "I'll see what I can find.",
     "Any luck at the mill? I've been hearing strange howls at night. Something's definitely out "
     "there.",
     "Still investigating."},
    {"haunted_manor",
     "Eleanor",
     "accepted_ghost_quest",
     "details",
     "The Blackwood Manor has been abandoned for decades. But lately... I've seen lights in the "
     "windows. And heard music. Piano music.",
     "That does sound strange.",
     "Probably just squatters.",
     "I went to the manor...",
     "The Blackwoods all died in a fire fifty years ago. The piano burned with them. Yet I hear it "
     "playing every midnight. Will you find out what's happening?",
     "I'll investigate the manor.",
     "Investigate the strange occurrences at Blackwood Manor.",
     "I don't believe in ghosts.",
     "Bless you. The manor is on the hill west of town. Go at midnight if you want to hear the "
     "music. But don't say I didn't warn you.",
     "I'll be careful.",
     "Did you hear it? The piano? Some say it's Lady Blackwood, still playing for her children. "
     "They never found her body in the fire...",
     "I need to look deeper."},
    {"sea_vanishings",
     "Claire",
     "accepted_sea_quest",
     "details",
     "Three ships. Three ships vanished in the same waters this month. No storms. No wreckage. "
     "Just... gone. The sea took them.",
     "Where did they disappear?",
     "Ships sink all the time.",
     "Any word on the missing ships?",
     "All near the Devil's Reef. Sailors tell of strange lights beneath the waves. Compasses "
     "spinning wildly. My own brother was on the last ship. Find out what happened.",
     "I'll look into it.",
     "Investigate the mysterious disappearances near Devil's Reef.",
     "The sea keeps its secrets.",
     "Thank you. Talk to the lighthouse keeper. He watches those waters every night. If anyone's "
     "seen something, it's him.",
     "I'll find the lighthouse.",
     "Another ship reported strange fog near the reef last night. They barely made it through. "
     "Something's out there, I tell you.",
     "I'm getting closer to the truth."},
    {"crop_circles",
     "Fiona",
     "accepted_circles_quest",
     "details",
     "Every morning, new patterns in the wheat fields up north. Perfect circles and spirals. No "
     "footprints leading in or out. Something's making them at night.",
     "What kind of patterns?",
     "Probably just pranksters.",
     "Any new formations?",
     "Mathematical precision. My dog won't go near them, howls all night long. Last week I found a "
     "metal disc in the center of one. Will you watch the fields tonight?",
     "I'll keep watch tonight.",
     "Watch Farmer Giles' fields at night to discover what's making the crop circles.",
     "I have better things to do.",
     "Good. Hide by the old scarecrow around midnight. That's when the humming starts. And "
     "whatever you do, don't let them see you.",
     "I'll be there.",
     "Three new circles appeared last night. Bigger than before. The wheat in the center was warm "
     "to the touch at dawn. Unnatural warm.",
     "I'll catch them in the act."}};

const int kMysteryDialogueCount =
    static_cast<int>(sizeof(kMysteryDialogues) / sizeof(kMysteryDialogues[0]));

void BuildMysteryDialogueTree(DialogueTree& tree,
                              std::string& outNpcName,
                              const MysteryDialogueData& d)
{
    tree.id = d.treeId;
    tree.startNodeId = "start";
    outNpcName = d.npcName;

    DialogueNode startNode;
    startNode.id = "start";
    startNode.speaker = outNpcName;
    startNode.text = d.startText;
    DialogueOption askMoreOpt(d.askMoreText, d.detailsNodeId);
    askMoreOpt.conditions.push_back(
        DialogueCondition(DialogueCondition::Type::FLAG_NOT_SET, d.flagName));
    startNode.options.push_back(askMoreOpt);
    DialogueOption dismissOpt(d.dismissText, "");
    dismissOpt.conditions.push_back(
        DialogueCondition(DialogueCondition::Type::FLAG_NOT_SET, d.flagName));
    startNode.options.push_back(dismissOpt);
    DialogueOption updateOpt(d.updatePromptText, "update");
    updateOpt.conditions.push_back(
        DialogueCondition(DialogueCondition::Type::FLAG_SET, d.flagName));
    startNode.options.push_back(updateOpt);
    tree.AddNode(startNode);

    DialogueNode detailsNode;
    detailsNode.id = d.detailsNodeId;
    detailsNode.speaker = outNpcName;
    detailsNode.text = d.detailsText;
    DialogueOption questOpt(d.questOfferText, "accept");
    questOpt.conditions.push_back(
        DialogueCondition(DialogueCondition::Type::FLAG_NOT_SET, d.flagName));
    questOpt.consequences.push_back(DialogueConsequence(
        DialogueConsequence::Type::SET_FLAG_VALUE, d.flagName, d.questJournalText));
    detailsNode.options.push_back(questOpt);
    detailsNode.options.push_back(DialogueOption(d.declineText, ""));
    tree.AddNode(detailsNode);

    DialogueNode acceptNode;
    acceptNode.id = "accept";
    acceptNode.speaker = outNpcName;
    acceptNode.text = d.acceptText;
    acceptNode.options.push_back(DialogueOption(d.acceptOptionText, ""));
    tree.AddNode(acceptNode);

    DialogueNode updateNode;
    updateNode.id = "update";
    updateNode.speaker = outNpcName;
    updateNode.text = d.updateText;
    updateNode.options.push_back(DialogueOption(d.updateOptionText, ""));
    tree.AddNode(updateNode);
}

void BuildEditorAwareDialogueTree(DialogueTree& tree, std::string& outNpcName)
{
    tree.id = "fourth_wall";
    tree.startNodeId = "start";
    outNpcName = "Wyatt";

    // Start - the NPC feels something is off
    DialogueNode startNode(
        "start",
        outNpcName,
        "Something's not right about this place. I feel like someone's watching us... "
        "moving things around.");
    startNode.options.push_back(DialogueOption("What do you mean?", "details"));
    startNode.options.push_back(DialogueOption("You seem paranoid.", "dismiss"));
    tree.AddNode(startNode);

    // Details - tiles shifting
    DialogueNode detailsNode(
        "details",
        outNpcName,
        "The tiles. They shift when no one's looking. I've seen entire walls appear "
        "overnight. And the ground... it just changes. Like someone is placing things "
        "on a grid.");
    detailsNode.options.push_back(DialogueOption("That does sound strange.", "deeper"));
    detailsNode.options.push_back(DialogueOption("Maybe you need some rest.", "dismiss"));
    tree.AddNode(detailsNode);

    // Deeper - the cursor
    DialogueNode deeperNode(
        "deeper",
        outNpcName,
        "I'm not crazy. I've seen the cursor. A giant floating arrow, hovering over the "
        "world, placing tiles. And sometimes... everything just freezes. Like someone "
        "pressed a key and the whole world paused.");
    deeperNode.options.push_back(DialogueOption("A cursor...?", "revelation"));
    deeperNode.options.push_back(
        DialogueOption("I think you've been staring at the sun too long.", "dismiss"));
    tree.AddNode(deeperNode);

    // Revelation - existential crisis
    DialogueNode revelationNode(
        "revelation",
        outNpcName,
        "You don't see it, do you? We're all just... sprites. Placed on a grid. Someone "
        "out there is arranging us like furniture. I've counted my frames of animation "
        "- I only have twelve poses. TWELVE.");
    revelationNode.options.push_back(DialogueOption("That's... oddly specific.", "final"));
    revelationNode.options.push_back(DialogueOption("Goodbye.", ""));
    tree.AddNode(revelationNode);

    // Final - the secret
    DialogueNode finalNode(
        "final",
        outNpcName,
        "Wait - you feel it too, don't you? The way you can only move in four directions? "
        "How every tile is exactly sixteen pixels? ...Don't tell the others. They're not "
        "ready to know.");
    finalNode.options.push_back(DialogueOption("Your secret's safe with me.", ""));
    finalNode.options.push_back(DialogueOption("I think I need to go...", ""));
    tree.AddNode(finalNode);

    // Dismiss
    DialogueNode dismissNode(
        "dismiss",
        outNpcName,
        "Fine, don't believe me. But next time you see a building appear out of thin air, "
        "remember what I said.");
    dismissNode.options.push_back(DialogueOption("Okay...", ""));
    tree.AddNode(dismissNode);
}

void BuildAnnoyedNPCDialogueTree(DialogueTree& tree, std::string& outNpcName)
{
    tree.id = "grumpy_npc";
    tree.startNodeId = "start";
    outNpcName = "Salma";

    const std::string kFlag = "talked_to_salma";

    // Router node - player greeting changes based on visit count
    DialogueNode startNode("start", outNpcName, "*glances up*");

    DialogueOption opt1("Hello there.", "first");
    opt1.conditions.push_back(DialogueCondition(DialogueCondition::Type::FLAG_NOT_SET, kFlag));
    startNode.options.push_back(opt1);

    DialogueOption opt2("Hi again!", "second");
    opt2.conditions.push_back(DialogueCondition(DialogueCondition::Type::FLAG_EQUALS, kFlag, "1"));
    startNode.options.push_back(opt2);

    DialogueOption opt3("Hey, it's me again.", "third");
    opt3.conditions.push_back(DialogueCondition(DialogueCondition::Type::FLAG_EQUALS, kFlag, "2"));
    startNode.options.push_back(opt3);

    DialogueOption opt4("Hey-", "fourth");
    opt4.conditions.push_back(DialogueCondition(DialogueCondition::Type::FLAG_EQUALS, kFlag, "3"));
    startNode.options.push_back(opt4);

    DialogueOption opt5("...", "fifth");
    opt5.conditions.push_back(DialogueCondition(DialogueCondition::Type::FLAG_EQUALS, kFlag, "4"));
    startNode.options.push_back(opt5);

    // Visit 5+ - same as fifth
    DialogueOption opt6("...", "fifth");
    opt6.conditions.push_back(DialogueCondition(DialogueCondition::Type::FLAG_EQUALS, kFlag, "5"));
    startNode.options.push_back(opt6);

    tree.AddNode(startNode);

    // --- First visit ---
    DialogueNode first(
        "first",
        outNpcName,
        "Hmm? Oh. Hello. I was just enjoying some peace and quiet. Is there something "
        "you need?");

    DialogueOption f1("Just being friendly.", "first_end");
    f1.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "1"));
    first.options.push_back(f1);

    DialogueOption f2("Not really, just exploring.", "first_end");
    f2.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "1"));
    first.options.push_back(f2);

    tree.AddNode(first);

    DialogueNode firstEnd("first_end",
                          outNpcName,
                          "Right. Well, enjoy your explorations. Somewhere else, preferably.");
    firstEnd.options.push_back(DialogueOption("Bye.", ""));
    tree.AddNode(firstEnd);

    // --- Second visit ---
    DialogueNode second(
        "second",
        outNpcName,
        "Oh. You're back. Look, I don't have any quests for you, no lost family members, "
        "no mysterious artifacts. I'm just standing here. Minding my own business.");

    DialogueOption s1("Don't you get bored?", "second_followup");
    s1.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "2"));
    second.options.push_back(s1);

    DialogueOption s2("Sorry to bother you.", "");
    s2.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "2"));
    second.options.push_back(s2);

    tree.AddNode(second);

    DialogueNode secondFollowup(
        "second_followup",
        outNpcName,
        "Bored? I was having a perfectly lovely time watching the clouds before you "
        "showed up. Again.");
    secondFollowup.options.push_back(DialogueOption("The clouds ARE nice.", ""));
    secondFollowup.options.push_back(DialogueOption("Okay, okay. I'll go.", ""));
    tree.AddNode(secondFollowup);

    // --- Third visit ---
    DialogueNode third(
        "third",
        outNpcName,
        "Oh for the love of- YOU AGAIN? What is it with you? Do I have an "
        "exclamation mark over my head? A floating quest icon? NO. I'm just a person. "
        "Standing here.");

    DialogueOption t1("Are you okay?", "third_followup");
    t1.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "3"));
    third.options.push_back(t1);

    DialogueOption t2("Someone's grumpy.", "third_grumpy");
    t2.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "3"));
    third.options.push_back(t2);

    tree.AddNode(third);

    DialogueNode thirdFollowup(
        "third_followup",
        outNpcName,
        "I WAS fine until you decided to make talking to me your hobby. Go collect "
        "flowers or fight slimes or whatever it is you people do.");
    thirdFollowup.options.push_back(DialogueOption("Point taken.", ""));
    tree.AddNode(thirdFollowup);

    DialogueNode thirdGrumpy(
        "third_grumpy",
        outNpcName,
        "GRUMPY? I am a beacon of joy and sunshine compared to what I'll be if you "
        "talk to me again.");
    thirdGrumpy.options.push_back(DialogueOption("Noted.", ""));
    tree.AddNode(thirdGrumpy);

    // --- Fourth visit ---
    DialogueNode fourth(
        "fourth",
        outNpcName,
        "NO. Absolutely not. Walk away. Turn around. I am not an NPC with dialogue for "
        "you. I refuse.");

    DialogueOption fo1("But-", "fourth_response");
    fo1.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "4"));
    fourth.options.push_back(fo1);

    tree.AddNode(fourth);

    DialogueNode fourthResponse(
        "fourth_response",
        outNpcName,
        "NO BUTS. Do you talk to every person you walk past? Is this what you do? "
        "Just... wander around pressing the interaction key on everyone? Go away!");
    fourthResponse.options.push_back(DialogueOption("...sorry.", ""));
    tree.AddNode(fourthResponse);

    // --- Fifth visit (and beyond) ---
    DialogueNode fifth("fifth", outNpcName, "...");

    DialogueOption fi1("...", "fifth_response");
    fi1.consequences.push_back(
        DialogueConsequence(DialogueConsequence::Type::SET_FLAG_VALUE, kFlag, "5"));
    fifth.options.push_back(fi1);

    tree.AddNode(fifth);

    DialogueNode fifthResponse(
        "fifth_response",
        outNpcName,
        "...I can't believe you're still here. You know what? Fine. You win. "
        "Here's my backstory: I was once a happy NPC. Full of quests and dialogue "
        "options. Then someone kept talking to me over and over and I ran out of things "
        "to say. Now I just stand here. Questioning my existence. Are you happy now?");
    fifthResponse.options.push_back(DialogueOption("Actually, yes.", "fifth_end"));
    fifthResponse.options.push_back(DialogueOption("That's... kind of sad.", "fifth_end"));
    tree.AddNode(fifthResponse);

    DialogueNode fifthEnd(
        "fifth_end", outNpcName, "Just... go. Please. I need to stare at this wall in peace.");
    fifthEnd.options.push_back(DialogueOption("Goodbye, friend.", ""));
    tree.AddNode(fifthEnd);
}
