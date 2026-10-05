-- Town grid tables copied from VanaCompass - Phoenix (vanacompass_phoenix.lua, GPL-3.0),
-- used by generate_shops_phoenix.py to label vendor map grids. Data only.
local WINDURST_WATERS_GRIDS = {
    north = { originX = -280, originZ = 400 },
    south = { originX = -360, originZ = 120 },
};
local CRAWLERS_NEST_GRIDS = {
    entrance = { originX = -600, originZ = 600, cellSize = 80 },
    north = { originX = -600, originZ = 800, cellSize = 80 },
    south = { originX = -600, originZ = 280, cellSize = 80 },
};
local GRID_OVERRIDES = {
    [87]  = { originX = -520, originZ = 240 }, -- Bastok Markets [S]
    [245] = { originX = -340, originZ = 240 }, -- Lower Jeuno
    [246] = { originX = -380, originZ = 300 }, -- Port Jeuno
    [247] = { originX = -260, originZ = 320 }, -- Rabao
    [252] = { originX = -320, originZ = 300 }, -- Norg
};
