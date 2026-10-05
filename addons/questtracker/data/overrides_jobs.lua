--[[
    Hand-written job-unlock quest steps for questtracker, keyed by quest enum key (xi.quest.id.*).
    Researched from the PhoenixXI (LandSandBoat) server scripts. Same shape as overrides.lua:
    ev = finishing events { zoneId, csid }, ki = key item id that marks the step done.
    Steps with neither advance when marked done manually.
]]

return {
    -- Paladin. scripts/quests/sandoria/A_Knights_Test.lua
    A_KNIGHTS_TEST = {
        note = 'Paladin unlock (level 30+, needs A Squire\'s Test II done). You can get the two books in either order; the Davoi well gives nothing until you hold both.',
        replace = {
            { text = 'Talk to Baunise in Southern San d\'Oria (!pos -55 -8 -32) for the Book of the West key item.', zone = 'Southern San d\'Oria', zoneId = 230, ev = { { 230, 634 } }, ki = 145 },
            { text = 'Talk to Cahaurme in Southern San d\'Oria (!pos 55 -8 -29) for the Book of the East key item.', zone = 'Southern San d\'Oria', zoneId = 230, ev = { { 230, 633 } }, ki = 144 },
            { text = 'In Davoi, examine the Disused Well (!pos -221 2 -293) while holding both books to get the Knight\'s Soul key item.', zone = 'Davoi', zoneId = 149, ki = 146 },
            { text = 'Return to Balasiel in Southern San d\'Oria (!pos -136 -11 64) with the Knight\'s Soul to unlock Paladin.', zone = 'Southern San d\'Oria', zoneId = 230, ev = { { 230, 628 } } },
        },
    },

    -- Dark Knight. scripts/quests/bastok/Blade_of_Darkness.lua, scripts/items/chaosbringer.lua
    BLADE_OF_DARKNESS = {
        note = 'Dark Knight unlock (level 30+). Dropping the Chaosbringer resets the kill count; kills only count while it is equipped.',
        replace = {
            { text = 'Go through Palborough Mines and zone into Zeruhn Mines from it. A cutscene gives you the Chaosbringer (zone in the same way again if you lose it).', zone = 'Zeruhn Mines', zoneId = 172, ev = { { 172, 130 } } },
            { text = 'Equip the Chaosbringer and land the killing blow on 100 enemies with a melee hit (any target counts). Don\'t drop the sword or the count resets.', zone = 'Anywhere', zoneId = 0,
              kills = { n = 100, weapon = 16607, melee = true } },
            { text = 'Zone into Beadeaux from Pashhow Marshlands with 100+ kills to see the cutscene and unlock Dark Knight.', zone = 'Beadeaux', zoneId = 147, ev = { { 147, 121 } } },
        },
    },

    -- Beastmaster. scripts/quests/jeuno/Path_of_the_Beastmaster.lua
    PATH_OF_THE_BEASTMASTER = {
        note = 'Beastmaster unlock (level 30+, needs Save My Son done). One cutscene starts and finishes the quest, so it never shows as in progress.',
        replace = {
            { text = 'Talk to Brutus in Upper Jeuno (!pos -55 8 95) to unlock Beastmaster.', zone = 'Upper Jeuno', zoneId = 244, ev = { { 244, 70 } } },
        },
    },

    -- Bard. scripts/quests/jeuno/Path_of_the_Bard.lua
    PATH_OF_THE_BARD = {
        note = 'Bard unlock (needs A Minstrel in Despair done; no level check in the script). The Lower Jeuno dialogue is optional. One cutscene starts and finishes it.',
        replace = {
            { text = 'Examine the Song Runes in Valkurm Dunes (!pos -721 -7 102) to unlock Bard. Talking to Bki Tbujhja or Mataligeat in Lower Jeuno first is optional.', zone = 'Valkurm Dunes', zoneId = 103, ev = { { 103, 2 } } },
        },
    },

    -- Ranger. scripts/quests/windurst/The_Fanged_One.lua, scripts/zones/Sauromugue_Champaign/mobs/Old_Sabertooth.lua
    THE_FANGED_ONE = {
        note = 'Ranger unlock (level 30+). Do NOT attack Old Sabertooth: it has to die from its own poison. Hitting it means a 5-minute wait before it can be popped again.',
        replace = {
            { text = 'Sauromugue Champaign: examine the Tiger Bones (!pos 666 -8 -379) to call Old Sabertooth. Stay within ~30 yalms and don\'t attack it; let it die from poison (~3.5 min).', zone = 'Sauromugue Champaign', zoneId = 120 },
            { text = 'Within 3 minutes of its death, examine the Tiger Bones again for the Old Tiger\'s Fang key item.', zone = 'Sauromugue Champaign', zoneId = 120, ki = 117 },
            { text = 'Bring the fang to Perih Vashai in Windurst Woods (!pos 117 -3 92) to unlock Ranger.', zone = 'Windurst Woods', zoneId = 241, ev = { { 241, 357 } } },
        },
    },

    -- Samurai. scripts/quests/outlands/Forge_Your_Destiny.lua
    FORGE_YOUR_DESTINY = {
        note = 'Samurai unlock (level 30+). Bring a Hatchet. After the hand-in there is a 3 Vana\'diel day wait (about 2 hr 53 min real time).',
        replace = {
            { text = 'Talk to Aeka in Norg (!pos 4 0 -4) for a Lump of Oriental Steel. If you lose it, trade her a Chunk of Darksteel Ore for another.', zone = 'Norg', zoneId = 252, ev = { { 252, 44 }, { 252, 47 } } },
            { text = 'Talk to Ranemaud in Norg (!pos 15 0 23) for a Sacred Sprig. If you lose it, trade him 2 Chunks of Gold Ore and 1 Chunk of Platinum Ore.', zone = 'Norg', zoneId = 252, ev = { { 252, 40 }, { 252, 43 } } },
            { text = 'Konschtat Highlands: stand right next to the ??? (!pos -709 2 102) and trade it the Oriental Steel. Defeat Forger to get a Lump of Bomb Steel.', zone = 'Konschtat Highlands', zoneId = 108 },
            { text = 'Sanctuary of Zi\'Tah: with the Sacred Sprig in inventory, trade a Hatchet to the ??? (!pos 642 -5 -150). Defeat the Guardian Treant that appears.', zone = 'The Sanctuary of Zi\'Tah', zoneId = 121 },
            { text = 'Trade the Sacred Sprig to the same ??? in the Sanctuary of Zi\'Tah to get a Sacred Branch.', zone = 'The Sanctuary of Zi\'Tah', zoneId = 121 },
            { text = 'Trade the Lump of Bomb Steel and the Sacred Branch to Jaucribaix in Norg (!pos 91 -7 -8).', zone = 'Norg', zoneId = 252, ev = { { 252, 27 } } },
            { text = 'Wait 3 Vana\'diel days (about 2 hr 53 min real time), then talk to Jaucribaix in Norg for the Mumeito and to unlock Samurai.', zone = 'Norg', zoneId = 252, ev = { { 252, 29 } } },
        },
    },

    -- Dragoon (legacy NPC scripts). Morjean.lua, Excavation_Point.lua (Shakhrami), qm1.lua (Meriphataud), Rahal.lua,
    -- scripts/battlefields/Ghelsba_Outpost/holy_crest.lua
    THE_HOLY_CREST = {
        note = 'Dragoon unlock (level 30+). Before it is in your log: talk to Ceraulian or Arminibit (Port San d\'Oria), then Novalmauge (Bostaunieux Oubliette), then Morjean. Bring a Pickaxe.',
        replace = {
            { text = 'Maze of Shakhrami: trade a Pickaxe to an Excavation Point (e.g. !pos 234 0 -110) to dig up a Wyvern Egg.', zone = 'Maze of Shakhrami', zoneId = 198 },
            { text = 'Bring the Wyvern Egg to Morjean in Northern San d\'Oria (!pos 99 0 116) and talk to him.', zone = 'Northern San d\'Oria', zoneId = 231, ev = { { 231, 62 } } },
            { text = 'Meriphataud Mountains: trade the Wyvern Egg to the ??? (!pos 641 -15 7).', zone = 'Meriphataud Mountains', zoneId = 119, ev = { { 119, 56 } } },
            { text = 'Talk to Rahal in Chateau d\'Oraguille (!pos -28 0 -6) for the Dragon Curse Remedy key item.', zone = 'Chateau d\'Oraguille', zoneId = 233, ev = { { 233, 60 } }, ki = 294 },
            { text = 'Ghelsba Outpost: enter the Hut Door (!pos -162 -11 78) for the Holy Crest battle (up to 6 people, 15 minutes). Defeat Cyranuce M Cutauleon, then name your wyvern to unlock Dragoon.', zone = 'Ghelsba Outpost', zoneId = 140, ev = { { 140, 32001 } } },
        },
    },

    -- Summoner. scripts/quests/windurst/SMN_I_Can_Hear_a_Rainbow.lua
    I_CAN_HEAR_A_RAINBOW = {
        note = 'Summoner unlock (level 30+). Carbuncle\'s Ruby has to be in your main inventory (not satchel or storage) both to start and for the lights to count.',
        replace = {
            { text = 'With the ruby in inventory, zone into outdoor areas during 7 weathers: clear/sunny, fire, water, earth, wind, ice, thunder. Each new one plays a short cutscene.', zone = 'Outdoor zones', zoneId = 0 },
            { text = 'Once all 7 lights are seen, trade Carbuncle\'s Ruby to the ??? in La Theine Plateau (G-6) to unlock Summoner and learn Carbuncle.', zone = 'La Theine Plateau', zoneId = 102, ev = { { 102, 124 } } },
        },
    },

    -- NOTE: on this server AN_UNDYING_PLEDGE is the Norg quest (Light Buckler), not the Blue Mage unlock.
    -- scripts/zones/Norg/npcs/Stray_Cloud.lua, scripts/zones/Norg/Zone.lua, scripts/zones/Sea_Serpent_Grotto/npcs/qm5.lua
    AN_UNDYING_PLEDGE = {
        note = 'Norg quest (Norg fame 4) that rewards a Light Buckler. It does NOT unlock Blue Mage; that is An Empty Vessel.',
        replace = {
            { text = 'In Norg, walk into the tunnel toward the Sea Serpent Grotto exit (around -20 0 -55) to trigger a cutscene.', zone = 'Norg', zoneId = 252, ev = { { 252, 226 } } },
            { text = 'Sea Serpent Grotto: examine the ??? (!pos 135 -9 220) to spawn Glyryvilu, then defeat it.', zone = 'Sea Serpent Grotto', zoneId = 176 },
            { text = 'Examine the same ??? again for the Caliginous Blade key item.', zone = 'Sea Serpent Grotto', zoneId = 176, ev = { { 176, 18 } }, ki = 715 },
            { text = 'Bring the Caliginous Blade to Stray Cloud in Norg (!pos -20 1 -29) to finish the quest.', zone = 'Norg', zoneId = 252, ev = { { 252, 227 } } },
        },
    },

    -- Blue Mage (the actual BLU unlock). scripts/quests/ahtUrhgan/An_Empty_Vessel.lua
    AN_EMPTY_VESSEL = {
        note = 'Blue Mage unlock (level 30+). Accepted after answering all 10 of Waoud\'s questions correctly. Each wait needs a Vana\'diel day change plus a zone. Refusing Yasfel deletes the quest.',
        replace = {
            { text = 'Wait for the Vana\'diel day to change and zone, then talk to Waoud in Aht Urhgan Whitegate (!pos 65 -6 -78).', zone = 'Aht Urhgan Whitegate', zoneId = 50, ev = { { 50, 65 } } },
            { text = 'Talk to Waoud again to hear which item he wants (Siren\'s Tear, Valkurm Sunsand or Dangruf Stone). Get it and trade it to him. He gives it back.', zone = 'Aht Urhgan Whitegate', zoneId = 50, ev = { { 50, 67 } } },
            { text = 'Take the item to Aydeewa Subterrane and walk to the spot near !pos 380 0 340. Accept Yasfel\'s offer to unlock Blue Mage (refusing deletes the quest).', zone = 'Aydeewa Subterrane', zoneId = 68, ev = { { 68, 3 } } },
        },
    },

    -- Corsair. scripts/quests/ahtUrhgan/Luck_of_the_Draw.lua
    LUCK_OF_THE_DRAW = {
        note = 'Corsair unlock (level 30+). Rewards a Corsair Die.',
        replace = {
            { text = 'Talk to Mafwahb in Aht Urhgan Whitegate (!pos 149 -2 -3).', zone = 'Aht Urhgan Whitegate', zoneId = 50, ev = { { 50, 548 } } },
            { text = 'Arrapago Reef: examine the ??? at H-10 (!pos 468 -12 111).', zone = 'Arrapago Reef', zoneId = 54, ev = { { 54, 211 } } },
            { text = 'Talacca Cove: examine the ??? (!pos -62 -8 -137) for the Forgotten Hexagun key item.', zone = 'Talacca Cove', zoneId = 57, ev = { { 57, 2 } }, ki = 791 },
            { text = 'Talacca Cove: examine the Rock Slab (!pos -99 -7 -91) to unlock Corsair.', zone = 'Talacca Cove', zoneId = 57, ev = { { 57, 3 } } },
        },
    },

    -- Puppetmaster. scripts/quests/ahtUrhgan/No_Strings_Attached.lua
    NO_STRINGS_ATTACHED = {
        note = 'Puppetmaster unlock (level 30+). Talk to Shamarhaan in Bastok Markets first, or Iruki-Waraki won\'t offer it. The Ghatsad wait needs a Vana\'diel day change.',
        replace = {
            { text = 'Talk to Ghatsad in Aht Urhgan Whitegate (!pos 34 -8 57).', zone = 'Aht Urhgan Whitegate', zoneId = 50, ev = { { 50, 262 } } },
            { text = 'Arrapago Reef: examine the pile of discarded materials ??? (!pos 457 -8 60) for the Antique Automaton key item.', zone = 'Arrapago Reef', zoneId = 54, ev = { { 54, 214 } }, ki = 798 },
            { text = 'Bring the Antique Automaton to Ghatsad in Aht Urhgan Whitegate.', zone = 'Aht Urhgan Whitegate', zoneId = 50, ev = { { 50, 264 } } },
            { text = 'Wait until the next Vana\'diel day, then talk to Ghatsad again.', zone = 'Aht Urhgan Whitegate', zoneId = 50, ev = { { 50, 265 } } },
            { text = 'Talk to Iruki-Waraki in Aht Urhgan Whitegate (!pos 101 -7 -29) and name your automaton to unlock Puppetmaster.', zone = 'Aht Urhgan Whitegate', zoneId = 50, ev = { { 50, 266 } } },
        },
    },
}
