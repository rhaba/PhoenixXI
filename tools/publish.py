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

    dst = os.path.join(HERE, 'addons', a['folder'])
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
