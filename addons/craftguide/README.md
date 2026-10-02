# craftguide (Ashita v4, PhoenixXI)

## Approval by staff

| Version | Status | Submitted | Reviewed by | Notes |
|---|---|---|---|---|
| 1.3.2 | Pending review | 2026-10-02 | | Display only, sends nothing. Replaces 1.3.1 (fixes fame thresholds; never reviewed). |

**Author:** Spongeh. **Program:** Ashita v4. **Type:** display only: a crafting and fishing
reference. It reads your own skills and never sends anything to the server.

---

Shows the cheapest way to level every craft using materials NPCs sell, along with a rank
test checklist, your craft skills (including decimals) and a fishing guide.

`/addon load craftguide`, then `/cg`.

## Craft tabs

There's one tab for each craft: Woodworking, Smithing, Goldsmithing, Clothcraft,
Leathercraft, Bonecraft, Alchemy and Cooking.

- **Your skill and rank**, read from the game. The decimal (for example 34.6) appears after
  your next skill-up and is remembered across reloads. When you're close to your rank's cap,
  the tab tells you a rank test is available.
- **Rank tests**: a checklist with the skill needed and the item to turn in for each test.
  Ranks you already hold are ticked automatically. You can tick others yourself to plan.
- **Cheapest path**: from your current skill to the target level (100 by default), split into
  level ranges. Each range shows the recipe, the expected number of synths and the expected
  gil. Open a range to see what to buy, how many, the price each, and where to buy it: an NPC
  shop, a guild's supply counter, a guild shop, or a regional vendor.
- **Alternatives at a level**: every recipe that gives skill-ups at the chosen level, cheapest
  first. Recipes that need ingredients NPCs don't sell are shown in yellow. Type what those
  ingredients cost you (AH, bazaar, or 0 if you farm them) and they're added to the path.

### How the path is calculated

The rules match the PhoenixXI server (`synthutils.cpp`):

- **Skill-ups (era rules):** only recipes above your skill give skill-ups. The chance is 60%
  per synth below 50.0 and 25% from 50.0 on. A failed synth can still give a skill-up at
  half the chance, if the recipe is 1 to 5 levels above you.
- **Skill-up size:** larger the further the recipe is above you, until skill 60.0. From 60.0
  on, every skill-up is 0.1.
- **Success rate:** 95% at or below your level, and lower the further the recipe is above
  you. If a recipe also needs another craft that you're below, its success roll applies too.
- **Failed synths:** you lose each ingredient with a 50% chance, and always lose the crystal.
- **Cost per synth:** crystal, plus ingredients, minus what an NPC pays for the result.
  Selling results can be turned off. Both buy and sell prices follow your fame (below).
- **Guild supply items:** these unlock by rank, and the path assumes you take each rank test
  as you reach it.
- **Intermediates:** for items like ingots or lumber, the path uses whichever is cheaper,
  buying them or making them with a recipe you can already do safely.

All numbers are expected averages, so actual runs will vary. Guild shop prices change with
stock; the path uses the price at the shop's normal stock level.

## Fishing tab

Every fish you can catch in the ToAU era, with:

- the skill level it's balanced for;
- what the best-paying NPC gives you, for your fame;
- each zone and fishing area it lives in (hover for the full list);
- the baits and lures it bites best on.

You can search by fish, zone or bait, limit the list to fish near your skill, and sort it by
skill, price or name.

## Settings

| Setting | |
|---|---|
| Crystal prices | NPCs don't sell crystals; set what they cost you (default 150 each) |
| Use regional vendors | regional produce vendors only sell while their nation holds the region |
| Fame | your fame level (1-9, or maxed) in San d'Oria, Bastok, Windurst and Norg |
| Sell results to NPCs | subtract the result's NPC price from each synth |
| Guild key item recipes | include recipes that need an advanced-technique key item |
| Modern skill-up rules | use the newer rules, where recipes up to 10 levels below you can still skill up |
| Your item prices | every price you've entered, which you can edit or remove |
| Window theme | Phoenix (ember red), Farplane (Final Fantasy X ember and mist), Farplane9 (Farplane colors in a squared FF9 style; the default), Umbrella (Resident Evil green), Midnight (blue) or Classic (stock ImGui); also `/cg theme <name>` |

Settings, targets, rank ticks and decimals are saved per character.

### Fame and NPC prices

PhoenixXI turns fame into a price rank from 1 to 21 for each shop's area
(`scripts/globals/shop.lua`):

| Shop area | Price rank |
|---|---|
| San d'Oria, Bastok, Windurst | floor((2400 + 4 x that nation's fame - the other two nations' fame + 399) / 400) |
| Selbina, Rabao | floor((1200 + San d'Oria + Bastok - Windurst + 199) / 200) |
| Norg | floor((7200 + 12 x Norg - all three nations + 1199) / 1200) |
| Jeuno, Aht Urhgan, Tavnazia, shops without fame | 11 |

- **Buying:** listed price x (111 - rank) / 100. At fame level 1 a nation shop charges 5% over
  the listed price; at maxed fame it charges 10% under.
- **Selling:** base price x (rank + 389) / 400. Any Jeuno shop pays the full base price (rank
  11), so the guide sells wherever pays you most: a fame shop only beats Jeuno once your
  rank there is above 11.
- **Fame with one nation lowers your rank with the other two nations' shops.**
- **Fame levels as points:** fame is stored as points from 0 to 2500, and the guide counts each
  level as the points where it starts. PhoenixXI uses the pre-2014 thresholds: level 2 at 200,
  3 at 500, 4 at 900, 5 at 1300, 6 at 1700, 7 at 1950, 8 at 2200 and 9 at 2450. "9 (maxed)" is
  2500.
- **Regional vendors** price by their nation's fame. Guild supply counters and guild shops
  ignore fame.

## Commands

| Command | |
|---|---|
| `/cg` or `/craftguide` | open or close the window |
| `/cg cook` | open on a tab: `wood`, `smith`, `gold`, `cloth`, `leather`, `bone`, `alchemy`, `cook`, `fish` |
| `/cg skills` | print your craft skills and ranks to chat |
| `/cg theme <name>` | window theme: Phoenix, Farplane, Umbrella, Midnight, Classic |

## Data and credits

craftguide doesn't depend on any other addon. All its data ships in `data/`.

- **PhoenixXI server** ([phoenixffxi/Phoenix](https://github.com/phoenixffxi/Phoenix),
  a LandSandBoat fork, GPL-3.0): recipes, rank tests, guild supply and guild shop stock,
  regional vendors, item sell prices, and the fishing tables (fish, zones, areas, baits).
- **VanaCompass - Phoenix** (from SmithReact's VanaCompass, GPL-3.0): the list of regular
  NPC shops and their prices.
- **Fame pricing** follows PhoenixXI's `shop.lua`. Each shop's fame area is read from its NPC
  script, or from the era module's override of that script.
- The [PhoenixXI wiki](https://wiki.phoenix-xi.com/Cooking/Guild) guild pages were used to
  check the rank tests.

To rebuild the data from a server checkout:

```
python tools/build_crafts.py --server <Phoenix checkout> --shops <vanacompass_phoenix>/data [--era toau]
python tools/test_engine.py Cooking     # print a path, headless (pip install lupa)
python tools/test_addon.py              # smoke-test the addon with stubbed Ashita/ImGui
```

craftguide is by Spongeh and is distributed under GPL-3.0 (see `LICENSE`), the same license
as the data sources above.

## For reviewers

### What it reads

**Incoming packets.** It only listens; the packets are never changed or blocked.

| Packet | Used for |
|---|---|
| 0x029 (battle message), only messages 38, 53 and 310 for craft skills | your own skill-ups (the 0.x decimal), level-ups and skill drops |

All other 0x029 messages are ignored.

**Client memory and resources, through Ashita's API:**
- your own craft skills and guild ranks (`GetPlayer():GetCraftSkill`);
- item names from the client's resource data.

### What it writes

- Its own settings file, through Ashita's settings library:
  - target levels, rank-test checkmarks and skill decimals;
  - the crystal and item prices and fame levels you typed in;
  - the window theme.
- Text to your own chat log, only from its own commands (for example `/cg skills`).

### What it does NOT do

- **No outgoing packets.** It has no `AddOutgoingPacket` call or any other packet injection.
- **No commands.** No `QueueCommand`, and no automated synthesis, buying, selling or movement.
- **No changes to incoming data.** No incoming packets are modified, blocked or injected.
- **No network or file access** beyond Ashita's settings file.

### Data

`data/*.lua` are fixed tables generated by `tools/build_crafts.py`:
- **From PhoenixXI's public server repository:** recipes, rank tests, guild and regional
  vendor stock, NPC sell prices, and the fishing tables.
- **From VanaCompass - Phoenix's extracted shop list:** regular NPC shop prices.

They are read at load and never change while the game runs.

### Files in the reviewed version

| File | SHA-256 |
|---|---|
| `craftguide.lua` | `5c47aca4e6b0921cd87ac8918134dba6d5de7c92bada9f04fe7fd29f00f63d50` |
| `phxui.lua` | `807bae19ddfb7b541589c2baf008fa384689d79c25bdc4befcf8a1845a8c7a72` |
| `cg/engine.lua` | `f43babac2bfcbfb0cc458637d478a76ec405bd7ecb982e6d5ac10e5b1c551d1c` |
| `cg/planner.lua` | `2dfc12a01b5e590366bba500a4efd4d8b7ac8866ec8e83d7b372ed1887f72653` |
| `cg/fame.lua` | `d8e555ca9d6f4540e5a6975d9cdb5b2e27ad81f80e99b717c7214e3a069dc879` |
| `cg/ui.lua` | `980ca1e7ad5f1b6a22cafb8177f0da7e00ebb91cea08bd9ab17a7c9974b632e6` |
| `data/crafts.lua` | `1a67305c3c19d1d52f2c07600005031deead7694fec2889dead95f5fc0921a37` |
| `data/prices.lua` | `3c0911afd575a3057f79e958178a0c4c32a53123c655e4ed993facda1b666356` |
| `data/fishing.lua` | `8e4600cb2d6668099e3c2efd64ac1ff8ebc307fe108a3febeaeeb493d271331b` |
| `tools/build_crafts.py` | `856a0d302867d6d9323867eb16c5349205cebbf0c055a866f471c71c84e0923b` |
| `LICENSE` | `0b383d5a63da644f628d99c33976ea6487ed89aaa59f0b3257992deac1171e6b` |
