"""
Publish an addon into this repository for staff review.

    python tools/publish.py <addon key>

For each addon in ADDONS below:
  1. writes SHA256SUMS in the addon's source folder (every published file except README.md and
     SHA256SUMS itself);
  2. puts the "Approval by staff" header at the top of the source README and the "For reviewers"
     section at the bottom (replacing earlier copies), including the SHA-256 of SHA256SUMS and of
     the main code files;
  3. copies the published files to addons/<folder>/ (byte for byte).
Then add or update the row in the root README and tag the commit.
"""

import fnmatch, hashlib, os, re, shutil, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_ROOT = os.path.dirname(HERE)

HEADER_START = '<!-- staff-approval -->'
HEADER_END = '<!-- /staff-approval -->'
REVIEW_START = '<!-- for-reviewers -->'

ADDONS = {}


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def published_files(src, exclude):
    out = []
    for dp, dn, fn in os.walk(src):
        dn[:] = sorted(d for d in dn if d not in ('.git', '__pycache__'))
        for f in sorted(fn):
            rel = os.path.relpath(os.path.join(dp, f), src).replace(os.sep, '/')
            if any(fnmatch.fnmatch(rel, pat) for pat in exclude):
                continue
            out.append(rel)
    return out


def publish(key):
    a = ADDONS[key]
    src = os.path.join(SRC_ROOT, a['src'])
    exclude = ['README.md', 'SHA256SUMS', '.gitignore', '.gitattributes', '.gitmodules', '*.zip'] + a.get('exclude', [])
    # screenshots/ holds the README's images: published, but not part of the addon, so they
    # stay out of SHA256SUMS (adding a screenshot doesn't change the reviewed files)
    files = published_files(src, exclude + ['screenshots/*'])
    docs = [f for f in published_files(src, exclude) if f.startswith('screenshots/')]
    sums = ''.join(f'{sha(os.path.join(src, f))}  {f}\n' for f in files)
    with open(os.path.join(src, 'SHA256SUMS'), 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(sums)
    sums_hash = sha(os.path.join(src, 'SHA256SUMS'))

    readme_path = os.path.join(src, 'README.md')
    readme = open(readme_path, encoding='utf-8').read().replace('\r\n', '\n')
    readme = re.sub(re.escape(HEADER_START) + r'.*?' + re.escape(HEADER_END) + r'\n*(?:---\n+)*', '', readme, flags=re.S)
    if REVIEW_START in readme:
        readme = readme[:readme.index(REVIEW_START)].rstrip() + '\n'
    title, _, body = readme.partition('\n')
    header = f'''{HEADER_START}
## Approval by staff

| Version | Status | Submitted | Reviewed by | Notes |
|---|---|---|---|---|
| {a["version"]} | {a.get("status", "Pending review")} | {a["submitted"]} | {a.get("reviewed_by", "")} | {a["note"]} |

{a["summary"]}
{HEADER_END}

---

'''
    rows = '\n'.join(f'| `{f}` | `{sha(os.path.join(src, f))}` |' for f in a['key_files'])
    docs_note = (" The README's images in `screenshots/` aren't part of the addon and aren't listed."
                 if docs else '')
    review = f'''{REVIEW_START}
## For reviewers

{a["review"].strip()}

### Files in the reviewed version

`SHA256SUMS` lists the SHA-256 of every file in this version ({len(files)} files).{docs_note}
Its own SHA-256 is `{sums_hash}`.

Main files:

| File | SHA-256 |
|---|---|
{rows}
'''
    readme = title + '\n\n' + header + body.lstrip('\n').rstrip() + '\n\n' + review
    with open(readme_path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(readme)

    dst = os.path.join(HERE, a.get('dest', 'addons'), a['folder'])
    if os.path.isdir(dst):
        # Remove the old files, then empty folders. A folder OneDrive (or an editor) holds open
        # can't be removed on Windows; it is simply reused.
        for dp, dn, fn in os.walk(dst, topdown=False):
            for f in fn:
                os.remove(os.path.join(dp, f))
            if dp != dst:
                try:
                    os.rmdir(dp)
                except OSError:
                    pass
    for f in files + docs + ['README.md', 'SHA256SUMS']:
        os.makedirs(os.path.dirname(os.path.join(dst, f)), exist_ok=True)
        shutil.copyfile(os.path.join(src, f), os.path.join(dst, f))
    print(f'{key}: {len(files)} files, SHA256SUMS {sums_hash[:16]}')


# ---------------------------------------------------------------------------------------------
ADDONS['farplaneui'] = dict(
    src='farplaneui_plugin', folder='farplaneui', dest='plugins', version='0.11.7', submitted='2026-11-03',
    note='Ashita v4 plugin (C++ DLL), source included. Hides and replaces parts of the game UI. One outgoing packet: 0x0F1 (cancel a status effect), only when the player asks for it. Replaces 0.11.0 - 0.11.6 (not yet reviewed).',
    summary='**Author:** Spongeh, building on the work of the XIUI creators (special thanks to Shuu for the idea of '
            'packaging many UI elements into a single load). **Program:** Ashita v4 plugin (C++). **Type:** UI replacement. It reads data '
            'the client already has or receives, draws its own interface, and hides the game\'s own versions of '
            'the parts it replaces. The only packet it sends is the game\'s own "cancel this status effect" request, when the player '
            'right-clicks an effect or confirms it in the game\'s status icon cursor. Only active while the Farplane linkshell is equipped.',
    key_files=['plugins/FarplaneUI.dll', 'CMakeLists.txt', 'src/farplaneui.cpp', 'src/modules/mod_native.cpp',
               'src/modules/mod_cmdmenu.cpp', 'src/modules/mod_chat.cpp', 'src/modules/mod_shadows.cpp', 'src/modules/mod_status.cpp', 'src/modules/mod_pad.cpp', 'src/modules/mod_party.cpp', 'src/worldring.cpp', 'src/mask.cpp',
               'src/data/quest_db.hpp', 'tools/gen_mobdb.cpp', 'tools/gen_quest_db.cpp'],
    review='''
This is a plugin, so the source is included next to the DLL (`src/`, built with `CMakeLists.txt`).
`plugins/FarplaneUI.dll` is the release build of exactly this source (MSVC 2022, x86 Release).

### Changes since 0.11.0
0.11.0 - 0.11.6 were submitted earlier the same day and haven't been reviewed; 0.11.7 replaces them
(0.11.5 was a local test build only).
- **0.11.1, Ctrl+R reply** (`src/modules/mod_chat.cpp`, declarations in `src/modules.hpp`): Ctrl+R
  (the game's "reply to last tell") opens FarplaneUI's own chat line prefilled with
  `/t <name of the last player who sent you a tell> ` instead of the game's input bar. The player
  still types the reply and presses Enter; nothing is sent automatically.
- **0.11.2, Utsusemi shadow display** (new `src/modules/mod_shadows.cpp`, drawing in
  `src/worldring.cpp`, shared state in `src/module.hpp`): shows how many Copy Image shadows you have
  around your own character, in a style the player picks (wisps, diamond, rune ring, smoke, pips or
  none). The count is read from your own buff list (Copy Image status ids 66 / 444 / 445 / 446), the
  same buffs the status icons show. It is drawn in the same 3D pass as the target ring, through the
  existing `SetRenderTarget` hook; no new hook, packet or memory write.
- **0.11.3:**
  - **Cancel a status effect (the one outgoing packet).** Right-clicking one of FarplaneUI's
    status icons, or pressing confirm (Enter / the controller's confirm button) while the game's own
    status icon cursor is on it, sends packet **0x0F1** with that status id: the game's own "cancel
    this effect" request, byte for byte what the statustimers addon sends. Only for effects the game
    marks as cancellable (`IStatusIcon::CanCancel`), and only on that click / press
    (`src/modules/mod_status.cpp`). The game's cursor position is read from its "menu    buff" menu
    object (+0x4C).
  - **Controller navigation of FarplaneUI's settings** (new `src/modules/mod_pad.cpp`, `src/pad.hpp`,
    `src/cfgui.cpp`): read through Ashita's DirectInput controller callback. While FarplaneUI's
    settings window is open from the controller, that callback returns TRUE for the D-pad, the
    buttons and the left stick, so the game doesn't also act on them; released as soon as the window
    closes. `/fpui pad game on|off` (typed by the player) calls Ashita's
    `IInputManager::SetDisableGamepad`.
  - **Display only:** Blink shimmer and a front / behind split for the shadow effects (the front part
    is drawn as screen-space triangles when the scene is finished: `src/worldring.cpp` `DrawFront`,
    called from the mask's existing under-UI draw), a cast ring around the player's feet, warning
    colours on the target ring while the target readies a move or casts, Ctrl + mouse wheel zoom on
    the minimap (Ashita's mouse callback; TRUE only for that wheel event over the map), the ready
    check also reading party lines another addon blocked (read only), notifications closing when
    answered, inventory counts, objects named on the target bar, and the game's target frame left
    visible for objects and during events.
- **0.11.4** (display only, reads only):
  - **The chat steps aside only for a game menu that covers it** (`src/modules/mod_chat.cpp`), instead
    of for every menu (the auction house hid it). Reads the open menu's rectangle (menu object
    +0x42..+0x48) and the game's menu resolution: registry values `0037` / `0038` under
    `HKLM\\SOFTWARE\\PlayOnline*\\SquareEnix\\FinalFantasyXI`, read with `RegQueryValueExA` (Ashita answers
    with the boot profile's `[ffxi.registry]` values). Nothing is written to the registry.
  - **"Hide in cutscenes" only hides while the game hides its own interface** (`src/modules/common.cpp`
    `GameInterfaceHidden`, `src/farplaneui.cpp` `InCutscene`), so talking to an NPC (also an event)
    keeps FarplaneUI up. Reads one byte: statustimers' check, sig
    `8B4424046A016A0050B9????????E8????????F6D81BC040C3`, object at *(sig + 10), +0xB4.
- **0.11.6:**
  - **Game menus that open over the chat are moved to the COMMAND MENU frame** ("Move dialog menus",
    `src/modules/mod_cmdmenu.cpp`): the same game set-position routine and window list the command
    menu move already uses (no new hook or signature). Decided once per window from where the game
    first put it; the game's own log / input windows are never moved; every moved window goes back
    to the game's position when the option is turned off or the plugin unloads.
  - **Party rows** (`src/modules/mod_party.cpp`, display only): HP / MP bars with their values, TP
    line, damage ghost, faded out-of-range members, leader and level sync icons. Leader / sync come
    from `IParty::GetMemberFlagMask` (0x04 party leader, 0x100 level sync target, as the XivParty
    addon reads them) and `GetAlliancePartyLeaderServerId1`. The two icons are new PNGs in
    `resources/farplaneui/ember_dock/icons/`.
  - **Cast bar** (display only): the "Bar" style draws in the existing resting / auto-attack panel.
- **0.11.7** (display only, reads only):
  - **Pet row** under the player's own party row: the pet entity from `IEntity::GetPetTargetIndex` of
    the player, its name / HP %, and `IPlayer::GetPetMPPercent` / `GetPetTP`. Clicking it sends
    `/ta <pet>` (player click, like the party rows' `/ta <name>`).
  - **Menus over the chat:** a window the game moves onto the chat after it opened (the item info
    box) is moved too.
  - `/fpui party petlog` (diagnostics, on request): writes the pet fields and the monsters claimed by
    the player to `<Ashita>/logs/farplaneui_pet.log`.

### What it reads
**Incoming packets.** It only listens: packets are never changed, blocked or injected.

| Packet | Used for |
|---|---|
| 0x028 (action) | cast bar, DPS meter (damage messages), party members' job ability recasts, monsters acting on the party (enemy list), auto-attack round timing, the target ring's TP-move / cast pulse |
| 0x029 (battle message) | experience / skill-up notices, defeat messages |
| 0x02D (kill message) | XP / limit / capacity points per kill |
| 0x0D2 (treasure pool) | "item dropped" notices |
| 0x00A (zone in) | resets per-zone state |
| 0x056 (quest / mission log), quest ports only | which quests are current or completed (quest tracker) |
| 0x032 / 0x034 (event start) | the zone and cutscene number, to tick off a quest step |

**Client memory, through Ashita's API:** party / alliance members (name, job, HP / MP / TP, zone,
status icons), your own status effects and their timers, ability and spell recast timers, your
inventory counts and equipped items, the target and entity list (name, position, HP %, claim,
model size, render flags) for the target bar, target ring, enemy list and minimap, key items
(quest steps), and your linkshell item (the Farplane check).

**Game data:** the client's map DATs (minimap), item / status / ability / spell names and icons
from Ashita's resource manager. Monster and quest data are built in (see the README).

### What it changes in the client
- **Hides game UI elements by writing their visibility bytes every frame** (party / alliance list
  and target frame via the hideparty addon's signatures; status icons, cast bar and the "Target"
  window by their menu names). They are set back to visible on toggle-off and on unload.
  While a fish is on the line (status 57) the party list hide is lifted, because the fishing
  gauge is drawn through the same object.
- **Native compass:** one byte of game code (the HUD's compass switch, sig
  `33C0668B81????????483DE3000000` + 0x24) is set from 1 to 0, the same switch XIUI's Game HUD
  uses. It is checked to be 0 or 1 before patching and restored to 1 on toggle-off and unload.
- **Native chat log window:** its stay-open timer is held at 0 (FancyChat's method) so the log
  stays collapsed; the chat input line keeps working.
- **Command menu position:** optionally moves the game's own command menu windows, and game menus
  that would open over the chat, by calling the game's window set-position routine; original
  positions are restored when disabled or unloaded.
- **Direct3D:** draws its own interface through Ashita's ImGui and D3D8 sprites; patches the
  device's `SetRenderTarget` vtable slot to draw the target ring and the Utsusemi shadow display
  inside the 3D scene (restored on unload); reads the camera matrices from draw calls.

### Input
- **Keyboard:** Space or `/` opens FarplaneUI's own chat input line (Ctrl / Alt chords and other
  ImGui text fields are left alone). While that line is open, keys go to it, and Enter is removed
  from what the game reads through DirectInput, so submitting a line doesn't also target something.
- **Ctrl+R:** opens that line prefilled with `/t <last tell sender> ` (the name is read from incoming
  tell lines, chat mode 12). While Ctrl+R opens FarplaneUI's line, R is removed from what the game
  reads through DirectInput so the game's own reply bar doesn't open too. Other Ctrl / Alt chords
  (macro palettes) are untouched.
- **Mouse:** only over FarplaneUI's own windows. Ctrl + mouse wheel over the minimap zooms it (that
  wheel event is kept from the game).
- **Controller:** see "Changes since 0.11.0" (0.11.3). Buttons, D-pad and left stick are held from
  the game only while FarplaneUI's settings window is being driven by the controller.

### Commands it sends (`QueueCommand`)
Only these, and all but the clock are a direct result of the player typing or clicking:
- the chat line the player typed and submitted in FarplaneUI's input (same as the game's chat bar);
- `/clock off` on load and `/clock on` on unload (hides / restores the game's clock);
- `/ta <name>` when the player clicks a party member's row (`/ta <pet>` on the pet row);
- `/p ...` when the player starts or answers a ready check (`/readycheck`), and `/p|/l|/l2 ...`
  when the player runs `/fpui dps report`.

### Outgoing packets
Exactly one, and only on a player's click / press: **0x0F1** (8 bytes, status id at +4), the game's
own request to cancel one of the player's own status effects (see 0.11.3 above). No other
`AddOutgoingPacket`, no packet injection, no packet modification.

### What it does NOT do
- **No automation.** No actions, abilities, spells, trades or targeting on its own; nothing runs on
  a timer that acts in game.
- **No network access.** No sockets, HTTP or external programs (the Settings > Performance page can
  open the local logs folder in Explorer when you click it).
- **Incoming text:** chat lines are kept from the game's own log window while FarplaneUI's chat is
  showing them (they appear in FarplaneUI's log instead); NPC dialogue modes 150 / 151 always reach
  the game. Nothing else is blocked.

### Files it writes
`<Ashita>/config/farplaneui/farplaneui.ini` (settings), `quests_<character>.txt` (quest progress),
and, only on request, diagnostics in `<Ashita>/logs/`: `farplaneui_perf_*.log`, `farplaneui_enter.log`,
`farplaneui_pad.log`, `farplaneui_minimap.log`, `farplaneui_target.log`, `farplaneui_menu.log`,
`farplaneui_cmdmenu.log`, `farplaneui_pet.log`.

### Information shown
Everything comes from what the client already has or from public server data: the minimap and
enemy list show monsters loaded in the client (the game's own draw range); the mob info window
shows drop tables, steal lists, respawn timers and NM placeholders from PhoenixXI's public server
repository (the same data as the mobdb 2.x addon).
''')

ADDONS['npcgil_phx'] = dict(
    status='Approved 2026-10-05', reviewed_by='Phoenix staff',
    src='npcgil_phx', folder='npcgil_phx', version='2.3.0', submitted='2026-10-02',
    note='Display only, sends nothing. Replaces 2.2.1 and 2.2.2, which were never reviewed. Adds quest turn-ins and a theme button.',
    summary='**Author:** Spongeh. **Program:** Ashita v4. **Type:** display only. It reads data the client '
            'already receives and never sends anything to the server.',
    key_files=['npcgil_phx.lua', 'session.lua', 'fame.lua', 'phxui.lua', 'data/prices.lua', 'data/quest_items.lua',
               'tools/build_prices.py', 'tools/build_quest_items.py'],
    review='''
### What it reads

**Incoming packets.** It only listens; the packets are never changed or blocked.

| Packet | Used for |
|---|---|
| 0x0D2 (item added to the treasure pool) | remembers which item is in which pool slot |
| 0x0D3 (lot result) | counts an item only when the result is "won" by your own character |
| 0x029 (battle message) with message 565 ("obtains gil") | adds your own share of gil from a kill |
| 0x00A (zone in) | clears the remembered pool when you change zones |

**Client memory and resources, through Ashita's API:**
- your own party slot: your server id (to recognise your own wins) and name (shown in the window
  header);
- item names from the client's resource data.

### What it writes

- Its own settings file, through Ashita's settings library: the session tally, session time, fame
  levels you typed in, and window theme.
- Text to your own chat log, but only when you click **Report to chat** or use `/npcgil report`.
  It isn't sent to any chat channel.

### What it does NOT do

- **No outgoing packets.** It has no `AddOutgoingPacket` call or any other packet injection.
- **No commands.** No `QueueCommand` or automated chat or actions.
- **No changes to incoming data.** No incoming packets are modified, blocked or injected.
- **No network or file access** beyond Ashita's settings file. No sockets, HTTP or external
  programs.
- **No access** to other players' data beyond what the treasure-pool and battle packets already
  show in your own log.

### Data tables

Both are fixed tables generated from PhoenixXI's public server repository. Nothing is looked up
at run time.

- `data/prices.lua`: base NPC sell prices from `sql/item_basic.sql` plus the module SQL in
  `modules/init.txt` (Phoenix's pre-RMT vendor price reverts). The fame levels you type in only
  change the displayed estimate.
- `data/quest_items.lua` (new in 2.3.0): which quests take an item as a turn-in, read from the
  trade checks in `scripts/quests/` and `scripts/zones/*/npcs/`, with the NPC, zone, quantity,
  gil reward and whether it repeats. It is only shown in the window's tooltip and the chat report.

To rebuild them:

```
git clone -b live https://github.com/phoenixffxi/Phoenix phoenix
python tools/build_prices.py phoenix
python tools/build_quest_items.py phoenix
```

### Changes since 2.2.2

- `session.lua`: Quest column with a hover tooltip, quest lines in the chat report, a Theme
  button next to Fame.
- `data/quest_items.lua` and `tools/build_quest_items.py`: new.
- `npcgil_phx.lua`: version and description.
''')

ADDONS['questtracker'] = dict(
    src='questtracker', folder='questtracker', version='0.3.1', submitted='2026-10-05',
    note='Display only, sends nothing. Quest tracker: next step on screen, item / key item checks, kill counters.',
    summary='**Author:** Spongeh. **Program:** Ashita v4. **Type:** display only. A WoW-style quest tracker: '
            'it reads the quest log and event information the client already receives and shows each '
            'tracked quest\'s next step. It never sends anything to the server.',
    key_files=['questtracker.lua', 'qt/engine.lua', 'qt/ui.lua', 'phxui.lua', 'data/quests.lua',
               'data/overrides.lua', 'data/overrides_jobs.lua', 'tools/build_quests.py', 'tools/quest_catalog.py',
               'tools/grid.py'],
    review='''
### What it reads

**Incoming packets.** It only listens; the packets are never changed or blocked.

| Packet | Used for |
|---|---|
| 0x056 (quest / mission log), quest ports 0x50-0x80 and 0x90-0xC0 | which quests are current and which are completed |
| 0x032 / 0x034 (event start) | the zone and cutscene number, to tick off the step that cutscene finishes |
| 0x028 (action), category 1 from your own character | your melee hits, for a melee-only kill counter (Blade of Darkness) |
| 0x029 (battle message) with message 6 ("defeats") | kill counters |

**Client memory, through Ashita's API:**
- your key items (`IPlayer:HasKeyItem`), for steps that hand out or need a key item;
- item counts in your bags, only for items a tracked quest's current step needs;
- your main weapon, for a weapon-specific kill counter;
- your zone and party member ids (whether a kill was yours or your party's), and the defeated mob's name.

### What it writes

- Its own settings file, through Ashita's settings library: tracked quests, step progress, kill
  counts and window options.
- Notices in your own chat log (new quest, next step, quest complete). Nothing is sent to any chat
  channel.

### What it does NOT do

- **No outgoing packets.** It has no `AddOutgoingPacket` call or any other packet injection.
- **No commands.** No `QueueCommand` or automated chat or actions; no targeting.
- **No changes to incoming data.** No incoming packets are modified, blocked or injected.
- **No network or file access** beyond Ashita's settings file.
- **No hidden information:** quest progress isn't sent by the server, so steps are inferred from
  what the client already shows (cutscenes, key items, kills) or ticked by the player.

### Data tables

`data/quests.lua` is generated by `tools/build_quests.py` from PhoenixXI's public server repository
(the interaction-framework quest scripts under `scripts/quests/`, the NPC data and the zone and item
enums); `tools/quest_catalog.py` supplies quest names, start NPCs and era filtering, and
`tools/grid.py` the map grid. `data/overrides.lua` and `data/overrides_jobs.lua` are hand-written
step lists for the job unlock quests, taken from the same server scripts. To rebuild:

```
git clone -b live https://github.com/phoenixffxi/Phoenix phoenix
pip install pyyaml lupa
python tools/build_quests.py --server phoenix
```
''')

ADDONS['enemybar'] = dict(
    status='Approved 2026-10-05', reviewed_by='Phoenix staff',
    src='enemybar', folder='enemybar', version='1.5.1', submitted='2026-10-02',
    note='Display only, sends nothing. Distance display off by default (enemybar2 condition).',
    summary='**Author:** mmckee and akaden (enemybar2, BSD 3-Clause); XIUI authors (debuff tracking, '
            'GPL-3.0); Ashita port by Spongeh. **Program:** Ashita v4. **Type:** display only. Enemy '
            'HP bars with buff and debuff timers; it never sends anything to the server.',
    key_files=['enemybar.lua', 'eb/render.lua', 'eb/tracker.lua', 'eb/resists.lua', 'eb/configui.lua', 'eb/defaults.lua',
               'eb/ekg.lua', 'eb/hearts.lua', 'eb/ff9.lua', 'eb/util.lua', 'phxui.lua', 'handlers/debuffhandler.lua',
               'handlers/enemycasts.lua', 'handlers/actiontracker.lua', 'handlers/statushandler.lua', 'libs/packets.lua'],
    review='''
### Relationship to approved addons

This is an Ashita v4 port of **enemybar2**, which is approved for Windower **with distance
display off**. Here the distance display is **off by default**, and a one-time update turns it off
for existing settings. It's still a per-bar option in `/eb`, so staff can require it locked off.

Buff and debuff tracking comes from **XIUI**, which is approved for Ashita.

### What it reads

**Incoming packets.** It only listens; the packets are never changed or blocked.

| Packet | Used for |
|---|---|
| 0x028 (action) | which mob is acting on whom (target of target), spells and TP moves being readied, debuffs landing |
| 0x029 (battle message) | debuffs wearing off, resists, interrupted casts |
| 0x00E (NPC/mob update) | the entity cache used by the debuff tracker |
| 0x00A / 0x00B (zone in / out) | clearing all tracked state when you zone |
| 0x0DD (party member update) | refreshing the party list (to tell party and alliance claims apart) |
| 0x08C / 0x08D (merits / job points) | your own merits that lengthen debuff durations (from XIUI) |

**Client memory, through Ashita's API:**
- entities you can already see: name, HP %, distance, claim id and status;
- your target and sub-target;
- party and alliance member ids and names.

**Files:** if MobDB is installed in `addons/mobdb`, it reads MobDB's own per-zone data files
(read only) to show weaknesses and immunities. Icons come from its own `assets` folder.

### What it writes

- **Its own settings file,** through Ashita's settings library.
- **Chat:** text to your own chat log, only from its `/eb` commands.

### What it does NOT do

- **No outgoing packets.** It has no `AddOutgoingPacket` call or any other packet injection.
- **No commands:** no `QueueCommand`, no targeting, and no automated actions. `e.blocked` is
  used only to consume its own `/eb` command.
- **No changes to incoming data.** No incoming packets are modified, blocked or injected.
- **No network access.** File access is limited to its settings, its own assets and MobDB's data
  files (read only).
- **No hidden information:** it shows only what your client already receives, the same data
  XIUI's target bar uses.
''')

ADDONS['ttimers'] = dict(
    status='Approved 2026-10-05', reviewed_by='Phoenix staff',
    src='ttimers', folder='ttimers', version='0.25-party.5', submitted='2026-10-02',
    note='Fork of approved tTimers 0.25. Adds party job ability recasts, a theme and a skin.',
    summary='**Author:** Thorny (tTimers, MIT); party tracker, theme and Farplane IX skin by Spongeh. '
            '**Program:** Ashita v4. **Type:** display only: timer panels. It is a fork of tTimers 0.25, '
            'which is approved.',
    exclude=[],
    key_files=['ttimers.lua', 'initializer.lua', 'callbacks.lua', 'config.lua', 'blockeditor.lua', 'trackers/party.lua',
               'data/partyrecasts.lua', 'durations/songs.lua', 'durations/data.lua', 'phxui.lua',
               'resources/skins/classic/farplane9.lua', 'resources/skins/classic/farplane9_bottom_justified.lua',
               'gdifonts/gdifonttexture.dll'],
    review='''
### Changes from stock tTimers 0.25

Compared file by file with the stock 0.25 release, ignoring line endings:

| | Files |
|---|---|
| **Changed** | `ttimers.lua` (version), `initializer.lua` (party panel, theme and skin settings), `callbacks.lua` (`/tt theme`), `config.lua` and `blockeditor.lua` (themed settings window, Party tab), `durations/songs.lua` (a stray `;` after a function header stopped song durations from loading), `README.md` |
| **Added** | `trackers/party.lua` and `data/partyrecasts.lua` (party job ability recasts), `phxui.lua` (window theme), `resources/skins/classic/farplane9*.lua` with two textures (a skin), `tools/build_party_recasts.py` and `tools/make_farplane9_skin.py` (data generators, not loaded in game) |
| **Removed** | `.gitmodules` |

Everything else, including `gdifonts/gdifonttexture.dll`, is byte-identical to stock 0.25. The DLL
is Thorny's GDI font renderer.

### The party tracker (new)

It listens to incoming **0x028 (action)** packets for job abilities used by your party (and,
optionally, alliance) members. It then starts a timer using that ability's recast from
`data/partyrecasts.lua`, a fixed table generated from PhoenixXI's public server data. It reads
member ids, names and main jobs through Ashita's party API. It sends nothing and blocks nothing.

### Behaviour inherited from stock tTimers (unchanged)

- **Incoming packets** are read for buffs, debuffs and recasts (0x028, 0x029, 0x063, 0x076, 0x0DD
  and others). They are never modified or blocked.
- **Outgoing packets.** Stock `durations/data.lua` sends two menu requests:
  - **0x061** (main menu) and **0xC0** (job point menu), to read job point totals;
  - only when your main job is **level 99** and you have the **Job Points key item (2544)**.

  That can't happen at PhoenixXI's level cap, so in practice nothing is sent. The code is
  unchanged from the approved release.
- **Mouse clicks.** Ctrl+click and Shift+click on a timer remove that timer from the display. The
  click is consumed (`e.blocked`) so it doesn't fall through to the game. This **does not cancel
  any buff in game**; no packet or command is sent.
- **Debug file dumps** in `durations/include.lua` are behind `debugMode = false` and never run.

### What it writes

- **Its own settings file,** through Ashita's settings library.
- **Chat:** text to your own chat log, from its commands.

### What it does NOT do

- **No commands:** no `QueueCommand` or automated actions.
- **No network access.**
''')


if __name__ == '__main__':
    for k in sys.argv[1:] or ADDONS:
        publish(k)
