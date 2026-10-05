--[[
    Hand-written quest steps for questtracker, keyed by the quest's enum key (xi.quest.id.*).

        KEY = {
            note = 'shown at the top of the quest in the journal',
            steps = { [3] = 'better wording for generated step 3', ... },   -- reword generated steps
            replace = {                                                     -- or a full step list
                { text = '...', zone = 'Port Bastok', zoneId = 236, grid = 'I-5',
                  ev = { { zoneId, cutsceneId } },  -- finishing events (from the server scripts)
                  ki = keyItemId },                 -- or: holding this key item means it's done
                ...
            },
        },

    Steps without ev or ki advance when you mark them done (right-click on the tracker, or /qt done).
]]

return {
    -- Ninja: Port Bastok, Korroloka Tunnel, Norg. scripts/quests/bastok/Ayame_and_Kaede.lua
    AYAME_AND_KAEDE = {
        note = 'Ninja job unlock (level 30+). Missing step 4 (back to Ensetsu after the coral) blocks the Sealed Dagger.',
        steps = {
            [1] = 'Talk to Kagetora in Port Bastok (F-6).',
            [2] = 'Talk to Ensetsu in Port Bastok (I-5), Kaede\'s father.',
            [3] = 'Korroloka Tunnel: examine the ??? (pos -208 -9 176). Three Korroloka Leeches appear; defeat all three, '
                .. 'then examine the ??? again for the Strangely Shaped Coral key item.',
            [4] = 'Go back to Ensetsu in Port Bastok (I-5) with the coral. Don\'t skip this: Ryoma won\'t help until you do.',
            [5] = 'Talk to Ryoma in Norg (H-8). He takes the coral and gives you the Sealed Dagger key item.',
            [6] = 'Bring the Sealed Dagger to Ensetsu in Port Bastok (I-5) to finish and unlock Ninja.',
        },
    },
}
