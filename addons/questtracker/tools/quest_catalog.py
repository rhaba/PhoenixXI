#!/usr/bin/env python3
"""Generate VanaCompass - Phoenix's quest, mission and spell-quest catalogs from a PhoenixXI checkout.

Writes (relative to the addon folder):

    data/phoenix_quests.lua        { logs = {...}, quests = {...} }
    data/phoenix_missions.lua      { logs = {...}, missions = {...} }
    data/phoenix_spell_quests.lua  { [scroll item id] = { { log=, id=, name= }, ... } }

Everything is derived from the PhoenixXI (LandSandBoat, GPL-3) checkout or computed; nothing is read
from the DriftwoodXI-authored data files that the upstream VanaCompass ships.

    pip install pyyaml lupa
    python tools/generate_quests_phoenix.py --server <phoenix checkout> [--era toau]

Sources
-------
- Ids: xi.questLog (scripts/enum/quest_log.lua), xi.quest.id (scripts/globals/quests.lua),
  xi.mission.log_id / xi.mission.id (scripts/globals/missions.lua), xi.assault.mission
  (scripts/enum/assault.lua) for the Assault log. Log labels come from the section banners in those
  enum files ("--  San d'Oria - 0").
- Scripts: every scripts/quests/**.lua and scripts/missions/**.lua (.todo files are skipped) is
  executed under LuaJIT (lupa) with the real enums loaded (scripts/enum/*.lua, data/enums/*.yaml ->
  xi.zone, xi.keyItem, xi.fameArea, ...) and stubbed Quest/Mission classes. Quest:new/Mission:new give
  the (log, id) a file implements; quest.reward/mission.reward give the rewards; quest.sections give
  the NPC tables. Functions are not run: their source text (located with debug.getinfo) is scanned.
- implemented: an interaction-framework script exists (impl='script'), or the quest is still coded
  the legacy way: some scripts/{zones,battlefields,globals,items,mixins} file calls completeQuest for
  it, or quests.lua marks it '+' ("coded and completeable") and an NPC script calls addQuest for it
  (impl='npc'). Missions: a Mission:new script. Assault: an instance script naming the assault.
- Names, in priority order: the script header line ("-- The Pickpocket"); a capitalised, non-shouted
  phrase in any script comment whose letters equal the enum key ("-- Involved in Quest: Tea with a
  Tonberry?"; title-cased and most frequent wins); otherwise the enum key humanised. Tallied in stats.
- start / starts (how = ...):
  'begin'     quests: the NPC, in a section whose check tests the quest's own status == QUEST_AVAILABLE,
              whose event leads to quest:begin (onEventFinish[csid] contains quest:begin, or the handler
              itself begins); then a begin in any section. When no NPC in the section table triggers
              the begin csid, the zone's scripts/zones/<zone>/npcs/*.lua that startEvent(csid) are used
              (this is how nation gate guards are found). Also: another quest's event that calls
              player:addQuest for this one (Mysteries of Beadeaux II).
              missions: the same with mission:begin.
  'zone'      the begin (or, for missions, the next event) comes from a zone handler (onZoneIn /
              onTriggerAreaEnter): zone only, no npc/x/y/z.
  'available' quests: first NPC with a progress event in an AVAILABLE section. missions: first NPC with
              a progress event in the first section checking currentMission == mission.missionId (the NPC
              that advances into the mission), else any section.
  'legacy'    the NPC script that calls player:addQuest for a legacy-coded quest.
  'header'    last resort: the first "-- Npc : !pos x y z zone" line of the script header.
  'assault'   the Whitegate orders NPC for the area (xi.assault.onMissionGiverTrigger / missionToArea).
  `start` is the first candidate (section order, then source order); `starts` lists all when several.
- NPC display name and position: data/zones/<zone>/npcs.yaml looked up by script name, with every
  modules/<init.txt entry>/zones/<zone>/npcs.yaml applied as an RFC 7386 merge patch in init.txt
  order (as src/map/data/yaml/merge.cpp does). Zone display name: the most common "-- Area: X" header
  among that zone's NPC scripts that matches the folder name, else the scripts/zones folder name
  humanised; zone id from sql/zone_settings.sql.
- rewards: quest.reward / mission.reward (items, gil, fame, fameArea, keyItems, title, exp, bayld,
  rankPoints). Legacy quests: the npcUtil.completeQuest params table, plus giveItem/addFame/addGil/
  addTitle/giveKeyItem calls in the same if/elseif branch as the completion.
- level: the minimum from player:getMainLvl() >= N in an AVAILABLE check (settings constants resolved
  from settings/default/main.lua). Assault: the instance's suggestedLevel.
- repeatable (framework quests only): a section whose check tests status == QUEST_COMPLETED (or
  status ~= QUEST_ACCEPTED) and completes or begins the quest again. Nation missions: the missionType
  table in scripts/globals/missions.lua (1 or 3 = repeatable).
- rank / step / order (missions): rank and step from the script file name (1_2_Bat_Hunt -> rank 1,
  step "1-2"; 05_... -> step "5"), nation rank for unscripted ids from missions.lua's getRequiredRank
  formula; order = position in the log sorted by id.
- Spell quests: item ids whose sql/item_basic.sql flags include FLAG_SCROLL, found in a quest's reward
  table, in a giveItem/addItem call in its framework script, or in its legacy completion branch.

Era rule
--------
LSB has no per-quest content tag; content gating is by expansion flag (ENABLE_<TAG>, enforced only
with RESTRICT_CONTENT, luautils::IsContentEnabled / xi.pre) and by the `content:` tag on NPC and mob
YAML entries. With --era toau (the default, PhoenixXI live) only base game, RoZ, CoP and ToAU are on;
every other tag (wotg, acp, amk, asa, abyssea, voidwatch, soa, rov, tvr, and the post-2008 features
field_manuals, grounds_tomes, survival_guide, daily_tally, trust_quests) is off, and the level cap is
75 (--max-level). Then:
- Quest logs: Crystal War (wotg), Abyssea (abyssea), Adoulin and Coalition (soa) are dropped.
- Mission logs: WotG, Campaign (wotg), ACP, AMK, ASA, SoA, RoV, TVR are dropped; Assault is ToAU.
- Within kept logs a quest is dropped when its AVAILABLE check or begin handler tests a disabled
  ENABLE_<TAG> flag, or requires a mission/quest from a disabled log; when it needs MAX_LEVEL above
  the cap or calls setLevelCap() above it; when its start NPC carries a disabled content tag after
  overlays; when its enum entry sits under a disabled-content banner in quests.lua ("-- Voidwatch
  (100-105)") or uses the enum's Voidwatch naming (VW_OP_*, VOIDWATCH_*, *_VOIDWATCHER); and, to a
  fixpoint, when its AVAILABLE check references a quest dropped by these rules. Dropped keys are
  printed. Unscripted quests carry no signal, so post-ToAU quests with no code (e.g. Unlocking a
  Myth, Trial in Tandem, Records of Eminence) stay in with implemented = false.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

import yaml

try:
    import lupa.luajit21 as lupa
except ImportError:  # pragma: no cover
    import lupa

Loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
ADDON_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXPANSIONS = ['rotz', 'cop', 'toau', 'wotg', 'acp', 'amk', 'asa', 'abyssea', 'voidwatch', 'soa', 'rov', 'tvr']
# Non-expansion content tags and the expansion era they belong to.
FEATURE_ERA = {'field_manuals': 'wotg', 'grounds_tomes': 'wotg', 'survival_guide': 'soa',
               'daily_tally': 'soa', 'trust_quests': 'soa'}

QUEST_LOG_CONTENT = {'CRYSTAL_WAR': 'wotg', 'ABYSSEA': 'abyssea', 'ADOULIN': 'soa', 'COALITION': 'soa',
                     'AHT_URHGAN': 'toau'}
MISSION_LOG_CONTENT = {'ZILART': 'rotz', 'TOAU': 'toau', 'WOTG': 'wotg', 'COP': 'cop', 'ASSAULT': 'toau',
                       'CAMPAIGN': 'wotg', 'ACP': 'acp', 'AMK': 'amk', 'ASA': 'asa', 'SOA': 'soa',
                       'ROV': 'rov', 'TVR': 'tvr'}
NATION_MISSION_LOGS = ('SANDORIA', 'BASTOK', 'WINDURST')
BANNER_CONTENT = {'voidwatch': 'voidwatch', 'abyssea': 'abyssea', 'adoulin': 'soa', 'wings of the goddess': 'wotg'}

SMALL_WORDS = {'a', 'an', 'and', 'as', 'at', 'but', 'by', 'for', 'from', 'in', 'into', 'of', 'on', 'or',
               'the', 'to', 'with', 'vs'}

LUA_PRELUDE = r"""
local proxyMT = {}
local function newProxy() return setmetatable({}, proxyMT) end
proxyMT.__index    = function(t, k) local p = newProxy(); rawset(t, k, p); return p end
proxyMT.__call     = function() return newProxy() end
proxyMT.__add      = function() return newProxy() end
proxyMT.__sub      = proxyMT.__add
proxyMT.__mul      = proxyMT.__add
proxyMT.__div      = proxyMT.__add
proxyMT.__mod      = proxyMT.__add
proxyMT.__unm      = proxyMT.__add
proxyMT.__concat   = function() return '' end
proxyMT.__tostring = function() return '<proxy>' end
function isProxy(v) return type(v) == 'table' and getmetatable(v) == proxyMT end
newProxyFn = newProxy

local looseMT = { __index = function() return newProxy() end }
function looseTable(t) return setmetatable(t or {}, looseMT) end

xi = looseTable({})
xi.settings = looseTable({ main = looseTable({}) })
zones = looseTable({})
require = function() return newProxy() end
setmetatable(_G, { __index = function() return newProxy() end })

function ensurePath(path)
    local t = _G
    for part in string.gmatch(path, '[^.]+') do
        local v = rawget(t, part)
        if type(v) ~= 'table' or isProxy(v) then
            v = looseTable({})
            rawset(t, part, v)
        end
        t = v
    end
    return t
end

function runFile(path)
    local chunk, err = loadfile(path)
    if not chunk then return false, err end
    local ok, v = pcall(chunk)
    return ok, v
end

-- Actions returned by quest:progressEvent(...) etc. Chained modifiers (:oncePerZone(), ...) return self.
local ActionMT = { __index = function(t, k) return function(self) return self end end }
local function mkAction(kind, args) return setmetatable({ __action = kind, args = args }, ActionMT) end
function actionInfo(v)
    if type(v) ~= 'table' or getmetatable(v) ~= ActionMT then return false, false end
    local first = v.args[1]
    if type(first) ~= 'number' then first = nil end
    return v.__action, first
end

CAPTURED = {}
local ContainerMT = { __index = function(t, k)
    return function(self, ...) return mkAction(k, { ... }) end
end }
local function newContainer(kind, fields)
    fields.__kind = kind
    fields.reward = fields.reward or {}
    fields.sections = {}
    local obj = setmetatable(fields, ContainerMT)
    CAPTURED[#CAPTURED + 1] = obj
    return obj
end
Quest = { new = function(self, areaId, questId) return newContainer('quest', { areaId = areaId, questId = questId }) end }
Mission = { new = function(self, areaId, missionId) return newContainer('mission', { areaId = areaId, missionId = missionId }) end }
HiddenQuest = { new = function(self, name) return newContainer('hidden', { name = name }) end }

function fnInfo(f)
    local i = debug.getinfo(f, 'S')
    return i.source, i.linedefined, i.lastlinedefined
end

function kind(v)
    if isProxy(v) then return 'proxy' end
    return type(v)
end
"""


# ----------------------------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------------------------

def norm(s):
    return re.sub(r'[^A-Z0-9]', '', s.upper())


def humanize_key(key):
    words = key.lower().split('_')
    out = []
    for i, w in enumerate(words):
        if not w:
            continue
        if re.fullmatch(r'[ivx]+', w) and w not in ('i',) or w in ('ii', 'iii', 'iv', 'v', 'vi'):
            out.append(w.upper())
        elif i > 0 and w in SMALL_WORDS:
            out.append(w)
        else:
            out.append(w[0].upper() + w[1:])
    return ' '.join(out)


def humanize_folder(folder):
    name = folder.replace('_', ' ')
    # A lower-case letter directly followed by a capital marks a dropped apostrophe (San dOria,
    # RuLude, FeiYin, AlTaieu).
    name = re.sub(r'(?<=[a-z])(?=[A-Z])', "'", name)
    name = name.replace('-', ' - ') if ' ' not in name and '-' in name else name
    return name


def merge_patch(target, patch):
    """RFC 7386."""
    if not isinstance(patch, dict):
        return patch
    out = dict(target) if isinstance(target, dict) else {}
    for key, value in patch.items():
        if value is None:
            out.pop(key, None)
        else:
            out[key] = merge_patch(out.get(key), value)
    return out


def read_init(root):
    path = os.path.join(root, 'modules', 'init.txt')
    if not os.path.exists(path):
        return []
    entries = []
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if line and not line.startswith('#'):
            entries.append(line.rstrip('/'))
    return entries


def load_yaml(path):
    with open(path, encoding='utf-8') as f:
        return yaml.load(f, Loader=Loader) or {}


def read_text(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def lua_string(s):
    s = str(s)
    return '"' + (s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r')) + '"'


IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')
LUA_KEYWORDS = {'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function', 'goto', 'if', 'in',
                'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while'}


def lua_value(v, indent=0):
    pad = '    ' * indent
    if v is None:
        return 'nil'
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return repr(round(v, 3))
    if isinstance(v, str):
        return lua_string(v)
    if isinstance(v, (list, tuple)):
        inner = [lua_value(x, indent + 1) for x in v]
        if all('\n' not in x for x in inner) and sum(len(x) for x in inner) < 100:
            return '{ ' + ', '.join(inner) + ' }' if inner else '{}'
        return '{\n' + ''.join(f'{pad}    {x},\n' for x in inner) + pad + '}'
    if isinstance(v, dict):
        parts = []
        for k, x in v.items():
            if x is None:
                continue
            if isinstance(k, str) and IDENT.match(k) and k not in LUA_KEYWORDS:
                key = k
            elif isinstance(k, int):
                key = f'[{k}]'
            else:
                key = f'[{lua_string(k)}]'
            parts.append(f'{key} = {lua_value(x, indent + 1)}')
        if not parts:
            return '{}'
        if all('\n' not in p for p in parts) and sum(len(p) for p in parts) < 160:
            return '{ ' + ', '.join(parts) + ' }'
        return '{\n' + ''.join(f'{pad}    {p},\n' for p in parts) + pad + '}'
    raise TypeError(type(v))


# ----------------------------------------------------------------------------------------------
# generator
# ----------------------------------------------------------------------------------------------

EVENT_RE = re.compile(r'(?:\b\w+):(progressEvent|progressCutscene|progressOptionalCutscene|priorityEvent|'
                      r'event|cutscene|replaceEvent)\(\s*(\d+)|\bstart(?:Event|Cutscene|OptionalCutscene)\(\s*(\d+)')
PROGRESS_KINDS = {'progressEvent', 'progressCutscene', 'progressOptionalCutscene', 'priorityEvent'}
EVENT_KINDS = PROGRESS_KINDS | {'event', 'cutscene', 'replaceEvent', 'startEvent', 'startCutscene',
                                'startOptionalCutscene'}


class Generator:
    def __init__(self, root, era, max_level=None):
        self.root = root.replace('\\', '/').rstrip('/') + '/'
        self.entries = read_init(self.root)
        self.problems = []
        self.set_era(era)
        # Level cap of the era: 75 until Abyssea raised it.
        self.max_level = max_level or (75 if not self.tag_on('abyssea') else 99)
        self.lua = lupa.LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(LUA_PRELUDE)
        self.g = self.lua.globals()
        self.text_cache = {}
        self.load_settings()
        self.load_zones()
        self.load_items()
        self.load_enums()

    # ---- era -------------------------------------------------------------------------------
    def set_era(self, era):
        if era == 'all':
            self.enabled = set(EXPANSIONS) | set(FEATURE_ERA)
        else:
            last = EXPANSIONS.index(era) + 1
            self.enabled = set(EXPANSIONS[:last])
            self.enabled |= {t for t, e in FEATURE_ERA.items() if e in self.enabled}
        self.enabled.add('none')

    def tag_on(self, tag):
        return not tag or str(tag).lower() in self.enabled

    # ---- files -----------------------------------------------------------------------------
    def text(self, path):
        path = path.replace('\\', '/')
        if path not in self.text_cache:
            self.text_cache[path] = read_text(path)
        return self.text_cache[path]

    def lines(self, path):
        key = ('lines', path.replace('\\', '/'))
        if key not in self.text_cache:
            self.text_cache[key] = self.text(path).split('\n')
        return self.text_cache[key]

    def fn_source(self, fn):
        src, a, b = self.g.fnInfo(fn)
        if not src or not src.startswith('@') or a is None or a <= 0:
            return '', None, None
        path = src[1:]
        lines = self.lines(path)
        return '\n'.join(lines[a - 1:b]), path, a

    # ---- settings / zones / items -------------------------------------------------------------
    def load_settings(self):
        self.settings_num = {}
        path = self.root + 'settings/default/main.lua'
        for m in re.finditer(r'^\s*([A-Z0-9_]+)\s*=\s*(-?\d+)\s*,', self.text(path), re.M):
            self.settings_num[m.group(1)] = int(m.group(2))

    def load_zones(self):
        self.zone_folder = {}
        for m in re.finditer(r"INSERT INTO `zone_settings` VALUES \((\d+),'[^']*',\d+,'([^']*)'\)",
                             self.text(self.root + 'sql/zone_settings.sql')):
            self.zone_folder[int(m.group(1))] = m.group(2)
        enum = load_yaml(self.root + 'data/enums/zone.yaml')['values']
        self.zone_key = {int(v): k for k, v in enum.items() if isinstance(v, int)}
        self.zone_name = {}
        for zid, folder in self.zone_folder.items():
            self.zone_name[zid] = self.area_name(folder)
        self.npc_cache = {}

    def area_name(self, folder):
        votes = Counter()
        npc_dir = os.path.join(self.root, 'scripts', 'zones', folder, 'npcs')
        if os.path.isdir(npc_dir):
            for name in os.listdir(npc_dir):
                if not name.endswith('.lua'):
                    continue
                head = self.text(os.path.join(npc_dir, name))[:400]
                m = re.search(r'^--\s*Area:\s*(.+?)\s*(?:\(\d+\))?\s*$', head, re.M)
                if m and norm(m.group(1)) == norm(folder):
                    votes[m.group(1).replace('_', ' ')] += 1
        if votes:
            return sorted(votes.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]
        return humanize_folder(folder)

    def zone_npcs(self, zid):
        """script name -> npc row, from npcs.yaml with module overlays (first entry wins)."""
        if zid in self.npc_cache:
            return self.npc_cache[zid]
        key = self.zone_key.get(zid)
        out = {}
        base = f'{self.root}data/zones/{key}/npcs.yaml' if key else None
        if base and os.path.exists(base):
            doc = load_yaml(base)
            for entry in self.entries:
                overlay = f'{self.root}modules/{entry}/zones/{key}/npcs.yaml'
                if os.path.isfile(overlay):
                    doc = merge_patch(doc, load_yaml(overlay))
            for nid, row in sorted((doc.get('npcs') or {}).items(), key=lambda kv: int(kv[0])):
                row = row or {}
                script = row.get('script')
                if script and script not in out:
                    out[script] = dict(row, id=int(nid))
        self.npc_cache[zid] = out
        return out

    def load_items(self):
        self.item_names = {}
        self.scrolls = set()
        pat = re.compile(r"^INSERT INTO `item_basic` VALUES \((\d+),\d+,'((?:[^'\\]|\\.)*)'.*$")
        for line in self.text(self.root + 'sql/item_basic.sql').split('\n'):
            m = pat.match(line)
            if not m:
                continue
            iid = int(m.group(1))
            self.item_names[iid] = m.group(2)
            if '@FLAG_SCROLL' in line and m.group(2).startswith('scroll_of_'):
                self.scrolls.add(iid)

    # ---- enums -------------------------------------------------------------------------------
    def load_enums(self):
        for name in sorted(os.listdir(self.root + 'data/enums')):
            if not name.endswith('.yaml'):
                continue
            doc = load_yaml(self.root + 'data/enums/' + name)
            table = ((doc.get('meta') or {}).get('lua') or {}).get('table')
            if not table:
                continue
            t = self.g.ensurePath(table)
            for k, v in (doc.get('values') or {}).items():
                if isinstance(v, int):
                    t[str(k).upper()] = v
        for name in sorted(os.listdir(self.root + 'scripts/enum')):
            if name.endswith('.lua'):
                ok, err = self.g.runFile(self.root + 'scripts/enum/' + name)
                if not ok:
                    self.problems.append(f'enum {name}: {err}')
        for rel in ('scripts/globals/quests.lua', 'scripts/globals/missions.lua', 'scripts/globals/assault/data.lua'):
            ok, err = self.g.runFile(self.root + rel)
            if not ok:
                raise SystemExit(f'{rel} failed: {err}')
        xi = self.g.xi
        self.item_enum = {k: v for k, v in xi.item.items() if isinstance(v, int)}
        self.quest_log = {k: int(v) for k, v in xi.questLog.items() if isinstance(v, (int, float))}
        self.mission_log = {k: int(v) for k, v in xi.mission.log_id.items() if isinstance(v, (int, float))}
        self.quest_area = {int(k): v for k, v in xi.quest.area.items()}
        self.mission_area = {int(k): v for k, v in xi.mission.area.items()}
        self.quest_ids = {}
        for log, area in self.quest_area.items():
            t = xi.quest.id[area]
            if self.lua.eval('kind')(t) == 'table':
                self.quest_ids[log] = {k: int(v) for k, v in t.items() if isinstance(v, (int, float))}
        self.mission_ids = {}
        for log, area in self.mission_area.items():
            t = xi.mission.id[area]
            if self.lua.eval('kind')(t) == 'table':
                self.mission_ids[log] = {k: int(v) for k, v in t.items()
                                         if isinstance(v, (int, float)) and k != 'NONE' and v != 65535}
        self.assault_ids = {k: int(v) for k, v in xi.assault.mission.items()}
        self.assault_area_enum = {k: int(v) for k, v in xi.assault.assaultArea.items()}
        self.assault_area_of = {}
        for area, missions in xi.assault.missionsByArea.items():
            for _, mid in missions.items():
                self.assault_area_of[int(mid)] = int(area)
        self.parse_banners()

    def parse_banners(self):
        """Log labels and disabled-content sub-banners from the enum files' comments."""
        self.quest_log_label = {}
        self.quest_banner_tag = {}  # (log, key) -> content tag
        self.quest_plus = set()
        text = self.text(self.root + 'scripts/globals/quests.lua')
        log = None
        sub_tag = None
        for line in text.split('\n'):
            m = re.match(r'^\s*--\s+(.+?)\s+-\s+(\d+)\s*$', line)
            if m:
                log = int(m.group(2))
                self.quest_log_label[log] = m.group(1)
                sub_tag = None
                continue
            m = re.match(r'^\s*--\s*([A-Za-z][A-Za-z\' ]+?)\s*\(\d+\s*-\s*\d+\)\s*$', line)
            if m and log is not None:
                sub_tag = BANNER_CONTENT.get(m.group(1).strip().lower())
                continue
            m = re.match(r'^\s*([A-Z0-9_]+)\s*=\s*\d+\s*,?\s*(--\s*\+)?', line)
            if m and log is not None and sub_tag:
                self.quest_banner_tag[(log, m.group(1))] = sub_tag
            if m and log is not None and m.group(2):
                self.quest_plus.add((log, m.group(1)))  # '+': "coded and completeable" per the file's legend
        self.mission_log_label = {}
        text = self.text(self.root + 'scripts/globals/missions.lua')
        for m in re.finditer(r'^\s*--\s+(.+?)\s*\((\d+)\)\s*$', text, re.M):
            label = re.sub(r'\s*-\s*Interaction Framework$', '', m.group(1)).strip()
            self.mission_log_label.setdefault(int(m.group(2)), label)

    # ---- names -------------------------------------------------------------------------------
    def build_comment_names(self, keys):
        """Collect capitalised comment phrases whose letters equal an enum key."""
        wanted = {}
        for k in keys:
            wanted.setdefault(norm(k), set()).add(k)
        max_len = max((len(k.split('_')) for k in keys), default=1) + 2
        votes = defaultdict(Counter)
        word_re = re.compile(r"[A-Za-z0-9][A-Za-z0-9'\u2019.!?,:-]*")
        for sub in ('scripts/zones', 'scripts/quests', 'scripts/missions', 'scripts/globals', 'scripts/battlefields',
                    'scripts/items'):
            for dirpath, _, names in os.walk(self.root + sub):
                for name in names:
                    if not name.endswith('.lua'):
                        continue
                    for line in self.text(os.path.join(dirpath, name)).split('\n'):
                        i = line.find('--')
                        if i < 0:
                            continue
                        comment = line[i + 2:]
                        if '!pos' in comment:
                            comment = comment.split('!pos')[0]
                        words = word_re.findall(comment)
                        for a in range(len(words)):
                            if not words[a][0].isupper():
                                continue
                            acc = ''
                            for b in range(a, min(len(words), a + max_len)):
                                w = words[b]
                                acc += norm(w)
                                if acc in wanted:
                                    phrase = ' '.join(words[a:b + 1]).rstrip('.,:;')
                                    if phrase and phrase[-1].isalnum() or phrase.endswith(('?', '!')):
                                        votes[acc][phrase] += 1
        best = {}
        for n, counter in votes.items():
            def quality(phrase):
                letters = [c for c in phrase if c.isalpha()]
                if letters and all(c.isupper() for c in letters):
                    return 0  # SHOUTED comment text
                words = [w for w in phrase.split(' ') if w[:1].isalpha()]
                titled = all(w[0].isupper() or w.lower() in SMALL_WORDS for w in words)
                return 2 if titled else 1
            ranked = sorted(counter.items(), key=lambda kv: (-quality(kv[0]), -kv[1],
                                                             -sum(c in "'!?" for c in kv[0]), kv[0]))
            if quality(ranked[0][0]) > 0:
                best[n] = ranked[0][0]
        return best

    # ---- script execution ----------------------------------------------------------------------
    def run_scripts(self, folder):
        found = []
        base = self.root + folder
        for dirpath, dirs, names in os.walk(base):
            dirs.sort()
            for name in sorted(names):
                if not name.endswith('.lua'):
                    continue
                path = os.path.join(dirpath, name).replace('\\', '/')
                self.g.CAPTURED = self.lua.table()
                ok, err = self.g.runFile(path)
                if not ok:
                    self.problems.append(f'{os.path.relpath(path, self.root)}: {err}')
                    continue
                for _, obj in self.g.CAPTURED.items():
                    found.append((path, obj))
        return found

    # ---- section analysis --------------------------------------------------------------------
    def handler_events(self, v, all_keys=False):
        """Return (events [(csid, progress)], begins_directly, source_text) for an NPC handler value."""
        events, begins, texts = [], False, []
        t = self.lua.eval('kind')(v)
        if t == 'table':
            kind, first = self.g.actionInfo(v)
            if kind:
                if kind in EVENT_KINDS and first is not None:
                    events.append((int(first), kind in PROGRESS_KINDS))
                return events, begins, ''
            for hk, hv in v.items():
                if all_keys or hk in ('onTrigger', 'onTrade'):
                    e, b, s = self.handler_events(hv, all_keys)
                    events += e
                    begins = begins or b
                    texts.append(s)
        elif t == 'function':
            src, _, _ = self.fn_source(v)
            texts.append(src)
            for m in EVENT_RE.finditer(src):
                if m.group(2):
                    events.append((int(m.group(2)), m.group(1) in PROGRESS_KINDS))
                else:
                    events.append((int(m.group(3)), True))
            if all_keys:  # onZoneIn and friends may return a bare csid
                for m in re.finditer(r'\breturn\s+(\d+)\s*$', src, re.M):
                    events.append((int(m.group(1)), True))
            if self.begin_re.search(src):
                begins = True
        return events, begins, '\n'.join(texts)

    def analyse_sections(self, obj, path, kind_name):
        """Yield per-section info: check source, check line, zones -> npcs/begin events."""
        out = []
        sections = obj.sections
        if self.lua.eval('kind')(sections) != 'table':
            return out
        for idx in sorted(k for k in sections.keys() if isinstance(k, int)):
            sec = sections[idx]
            if self.lua.eval('kind')(sec) != 'table':
                continue
            check_src, check_line = '', 0
            if self.lua.eval('kind')(sec.check) == 'function':
                check_src, _, check_line = self.fn_source(sec.check)
                check_line = check_line or 0
            zones_info = []
            for zk, ztab in sec.items():
                if not isinstance(zk, int) or self.lua.eval('kind')(ztab) != 'table':
                    continue
                begin_csids, zone_begins, finish_src = set(), False, []
                adds = defaultdict(list)  # csid -> [(log name, quest key)] other quests added there
                for hk in ('onEventFinish', 'onEventUpdate'):
                    fin = ztab[hk]
                    if self.lua.eval('kind')(fin) != 'table':
                        continue
                    for csid, fn in fin.items():
                        if self.lua.eval('kind')(fn) != 'function':
                            continue
                        src, _, _ = self.fn_source(fn)
                        finish_src.append(src)
                        if self.begin_re.search(src) and isinstance(csid, (int, float)):
                            begin_csids.add(int(csid))
                        if isinstance(csid, (int, float)):
                            for m in re.finditer(r'addQuest\(\s*xi\.questLog\.([A-Z_]+)\s*,\s*xi\.quest\.id\.\w+\.([A-Z0-9_]+)', src):
                                adds[int(csid)].append((m.group(1), m.group(2)))
                zone_handler_begins = False
                zone_handler_events = []
                npcs = []
                for nk, nv in ztab.items():
                    if not isinstance(nk, str) or nk in ('onEventFinish', 'onEventUpdate'):
                        continue
                    if re.match(r'^on[A-Z]', nk):
                        e, b, _ = self.handler_events(nv, all_keys=True)
                        zone_handler_events += e
                        zone_handler_begins = zone_handler_begins or b
                        continue
                    if nk in ('mob', 'mobs'):
                        continue
                    e, b, _ = self.handler_events(nv)
                    npcs.append({'name': nk, 'events': e, 'begins': b})
                if any(c in begin_csids for c, _ in zone_handler_events):
                    zone_handler_begins = True
                # A begin event no NPC in this table triggers is started by a legacy NPC script in the
                # zone (e.g. nation gate guards open the mission list, the mission file finishes it).
                triggered = {c for n in npcs for c, _ in n['events']} | {c for c, _ in zone_handler_events}
                listed = {n['name'] for n in npcs}
                for c in sorted(begin_csids - triggered):
                    for script in self.zone_csid_npcs(int(zk)).get(c, []):
                        if script not in listed:
                            listed.add(script)
                            npcs.append({'name': script, 'events': [(c, True)], 'begins': True})
                # Order NPCs by where their key first appears in the file after the section's check.
                lines = self.lines(path)
                for npc in npcs:
                    pos = None
                    pat = re.compile(r"\[\s*['\"]" + re.escape(npc['name']) + r"['\"]\s*\]")
                    for ln in range(max(check_line - 1, 0), len(lines)):
                        if pat.search(lines[ln]):
                            pos = ln
                            break
                    if pos is None:
                        for ln, l in enumerate(lines):
                            if pat.search(l):
                                pos = ln
                                break
                    npc['pos'] = pos if pos is not None else 10 ** 6
                    npc['begin'] = npc['begins'] or any(c in begin_csids for c, _ in npc['events'])
                    npc['progress'] = any(p for _, p in npc['events'])
                zones_info.append({'zone': int(zk), 'npcs': npcs, 'zone_begins': zone_handler_begins,
                                   'zone_events': bool(zone_handler_events),
                                   'adds': [(n['name'], lg, k) for n in npcs for c, _ in n['events']
                                            for lg, k in adds.get(c, [])],
                                   'finish_src': '\n'.join(finish_src)})
            # Lua hash order is not stable: order zones by where the section's source names them.
            lines = self.lines(path)

            def zone_pos(z):
                key = 'xi.zone.' + (self.zone_key.get(z['zone']) or '').upper() + ']'
                for ln in range(max(check_line - 1, 0), len(lines)):
                    if key in lines[ln]:
                        return (ln, z['zone'])
                return (10 ** 6, z['zone'])
            zones_info.sort(key=zone_pos)
            out.append({'idx': idx, 'check': check_src, 'check_line': check_line, 'zones': zones_info})
        return out

    def header_start(self, path):
        """First '-- Npc Name : !pos x y z zone' line of a script's header comment."""
        for line in self.lines(path)[:30]:
            if not line.startswith('--'):
                break
            m = re.match(r'^--\s*([^:!]+?)\s*:\s*!pos\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(\d+)', line)
            if not m:
                continue
            zid = int(m.group(5))
            if zid not in self.zone_folder:
                continue
            name = re.sub(r'\s*\(.*?\)\s*', '', m.group(1)).strip()
            npcs = self.zone_npcs(zid)
            script = name.replace(' ', '_')
            if script not in npcs:
                script = next((k for k, r in npcs.items() if r.get('display_name') == name), script)
            start = self.make_start(zid, script, 'header')
            if 'x' not in start:
                start.update({'x': float(m.group(2)), 'y': float(m.group(3)), 'z': float(m.group(4))})
            start['_missing'] = False
            return start
        return None

    def zone_csid_npcs(self, zid):
        """csid -> NPC script names in scripts/zones/<zone>/npcs that start that event."""
        cache = self.text_cache.setdefault('csid_npcs', {})
        if zid in cache:
            return cache[zid]
        out = defaultdict(list)
        folder = self.zone_folder.get(zid)
        npc_dir = os.path.join(self.root, 'scripts', 'zones', folder or '', 'npcs')
        if folder and os.path.isdir(npc_dir):
            for name in sorted(os.listdir(npc_dir)):
                if name.endswith('.lua'):
                    for m in re.finditer(r'start(?:Event|Cutscene|OptionalCutscene)\(\s*(\d+)',
                                         self.text(os.path.join(npc_dir, name))):
                        if name[:-4] not in out[int(m.group(1))]:
                            out[int(m.group(1))].append(name[:-4])
        cache[zid] = out
        return out

    def make_start(self, zid, npc_name, how):
        start = {'zone': self.zone_name.get(zid, str(zid)), 'zoneId': zid}
        if npc_name:
            row = self.zone_npcs(zid).get(npc_name)
            disp = (row or {}).get('display_name') or npc_name.replace('_', ' ')
            start = {'npc': disp, **start}
            if row and isinstance(row.get('at'), list) and len(row['at']) >= 3:
                x, y, z = row['at'][:3]
                start.update({'x': round(float(x), 3), 'y': round(float(y), 3), 'z': round(float(z), 3)})
            start['_content'] = (row or {}).get('content')
            start['_missing'] = row is None
        start['how'] = how
        return start

    def pick_starts(self, secs, primary_filter, fallback_filter, fallback_any=False):
        """Return (list of starts) using begin NPCs in primary sections, then anywhere, then fallback."""
        def collect(pred, want):
            cands = []
            for s in secs:
                if not pred(s):
                    continue
                for z in s['zones']:
                    for n in z['npcs']:
                        if want(n):
                            cands.append((s['idx'], n['pos'], z['zone'], n['name']))
            cands.sort()
            seen, res = set(), []
            for _, _, zid, name in cands:
                if (zid, name) not in seen:
                    seen.add((zid, name))
                    res.append((zid, name))
            return res

        for pred, how in ((primary_filter, 'begin'), (lambda s: True, 'begin')):
            res = collect(pred, lambda n: n['begin'])
            if res:
                return [self.make_start(z, n, how) for z, n in res]
        for s in secs:
            if primary_filter(s):
                for z in s['zones']:
                    if z['zone_begins']:
                        return [self.make_start(z['zone'], None, 'zone')]
        for s in secs:
            for z in s['zones']:
                if z['zone_begins']:
                    return [self.make_start(z['zone'], None, 'zone')]
        for filt in ((fallback_filter, lambda s: True) if fallback_any else (fallback_filter,)):
            for s in secs:
                if filt(s):
                    res = collect(lambda x, s=s: x is s, lambda n: n['progress'])
                    if res:
                        return [self.make_start(z, n, 'available') for z, n in res[:1]]
                    zone_ev = [z for z in s['zones'] if z['zone_events']]
                    if zone_ev:
                        return [self.make_start(zone_ev[0]['zone'], None, 'zone')]
        return []

    # ---- rewards -------------------------------------------------------------------------------
    def reward_to_py(self, r):
        out = {}
        if self.lua.eval('kind')(r) != 'table':
            return out

        def ids(v):
            t = self.lua.eval('kind')(v)
            if isinstance(v, (int, float)):
                return [int(v)]
            if t == 'table':
                res = []
                vals = [x for _, x in sorted(((k, x) for k, x in v.items() if isinstance(k, (int, float))))]
                if len(vals) == 2 and all(isinstance(x, (int, float)) for x in vals) and vals[1] < 100 \
                        and vals[0] > 100:
                    return [int(vals[0])]  # { itemId, quantity }
                for x in vals:
                    res += ids(x)
                return res
            return []

        items = ids(r.item)
        if items:
            out['items'] = items
        for field in ('gil', 'fame', 'exp', 'bayld', 'rankPoints', 'title', 'fameArea'):
            v = r[field]
            if isinstance(v, (int, float)):
                out[field] = int(v)
        kis = ids(r.keyItem)
        if kis:
            out['keyItems'] = kis
        return out

    def scrolls_in(self, text):
        found = []
        for m in re.finditer(r'(?:giveItem|addItem|item\s*=)\s*\(?\s*([^\n]{0,200})', text):
            for ref in re.finditer(r'xi\.item\.([A-Z0-9_]+)|\b(\d{4,5})\b', m.group(1)):
                iid = self.item_enum.get(ref.group(1)) if ref.group(1) else int(ref.group(2))
                if iid in self.scrolls and iid not in found:
                    found.append(int(iid))
        return found

    # ---- legacy (NPC-script) quests ---------------------------------------------------------------
    def scan_legacy(self):
        self.legacy_start = defaultdict(list)   # (log, key) -> [(zid, npc or None, path)]
        self.legacy_done = defaultdict(list)    # (log, key) -> [(path, params text, window text)]
        area_to_log = {v: k for k, v in self.quest_area.items()}
        folder_zid = {f: z for z, f in self.zone_folder.items()}
        call_re = re.compile(r'(addQuest|completeQuest)\(\s*(?:player\s*,\s*)?xi\.questLog\.([A-Z_]+)\s*,\s*'
                             r'([A-Za-z_.]+?)\.([A-Z0-9_]+)\s*([,)])')
        walk = [w for sub in ('zones', 'battlefields', 'globals', 'items', 'mixins')
                for w in os.walk(self.root + 'scripts/' + sub)]
        for dirpath, dirs, names in walk:
            for name in sorted(names):
                if not name.endswith('.lua'):
                    continue
                path = os.path.join(dirpath, name).replace('\\', '/')
                text = self.text(path)
                if 'Quest(' not in text:
                    continue
                aliases = {m.group(1): m.group(2) for m in
                           re.finditer(r'local\s+(\w+)\s*=\s*xi\.quest\.id\.(\w+)\s*$', text, re.M)}
                in_zones = path.startswith(self.root + 'scripts/zones/')
                rel = path[len(self.root + 'scripts/zones/'):].split('/') if in_zones else ['']
                zid = folder_zid.get(rel[0])
                npc = rel[2][:-4] if len(rel) == 3 and rel[1] == 'npcs' else None
                for m in call_re.finditer(text):
                    log = self.quest_log.get(m.group(2))
                    prefix = m.group(3)
                    area = prefix.split('.')[-1] if prefix.startswith('xi.quest.id') else aliases.get(prefix)
                    if log is None or area is None or area_to_log.get(area) != log:
                        continue
                    key = m.group(4)
                    if m.group(1) == 'addQuest':
                        self.legacy_start[(log, key)].append((zid, npc, path))
                    else:
                        params = ''
                        if m.group(5) == ',':
                            params = self.balanced(text, m.end())
                        line_no = text.count('\n', 0, m.start())
                        window = self.block_window(self.lines(path), line_no)
                        self.legacy_done[(log, key)].append((path, params, window))

    @staticmethod
    def block_window(lines, line_no):
        """The lines of the if/elseif branch around a legacy completeQuest call (bounded)."""
        stop = re.compile(r'^\s*(elseif\b|else\b|end\b\s*$|.*\bfunction\b|if\s+csid\b|if\s+option\b)')
        a = line_no
        while a > 0 and line_no - a < 10 and not stop.match(lines[a - 1]):
            a -= 1
        b = line_no
        while b + 1 < len(lines) and b - line_no < 6 and not stop.match(lines[b + 1]):
            b += 1
        return '\n'.join(lines[a:b + 1])

    def window_rewards(self, text):
        """Best-effort rewards from the statements of a legacy completion branch."""
        out = {}
        xi = self.g.xi

        def enum(table, name):
            v = table[name]
            return int(v) if isinstance(v, (int, float)) else None
        items = []
        for m in re.finditer(r'(?:giveItem\(\s*player\s*,|addItem\()\s*\{?\s*(xi\.item\.([A-Z0-9_]+)|(\d{3,5}))', text):
            iid = self.item_enum.get(m.group(2)) if m.group(2) else int(m.group(3))
            if iid and iid not in items:
                items.append(int(iid))
        if items:
            out['items'] = items
        m = re.search(r'addFame\(\s*xi\.fameArea\.([A-Z_]+)\s*,\s*(\d+)', text)
        if m:
            out['fame'] = int(m.group(2))
            area = enum(xi.fameArea, m.group(1))
            if area is not None:
                out['fameArea'] = area
        m = re.search(r'(?:addGil|giveGil)\(\s*(?:player\s*,\s*)?(\d+)', text)
        if m:
            out['gil'] = int(m.group(1))
        m = re.search(r'addTitle\(\s*xi\.title\.([A-Z0-9_]+)', text)
        if m and enum(xi.title, m.group(1)) is not None:
            out['title'] = enum(xi.title, m.group(1))
        kis = []
        for m in re.finditer(r'(?:giveKeyItem\(\s*player\s*,|addKeyItem\()\s*xi\.keyItem\.([A-Z0-9_]+)', text):
            v = enum(xi.keyItem, m.group(1))
            if v is not None and v not in kis:
                kis.append(v)
        if kis:
            out['keyItems'] = kis
        return out

    @staticmethod
    def balanced(text, i):
        j = text.find('{', i)
        if j < 0 or text[i:j].strip():
            return ''
        depth = 0
        for k in range(j, min(len(text), j + 4000)):
            if text[k] == '{':
                depth += 1
            elif text[k] == '}':
                depth -= 1
                if depth == 0:
                    return text[j:k + 1]
        return ''

    def eval_table(self, src):
        if not src:
            return None
        try:
            fn = self.lua.eval('function(s) local f = load("return " .. s); if not f then return nil end; '
                               'local ok, v = pcall(f); if ok then return v end; return nil end')
            return fn(src)
        except Exception:  # noqa: BLE001
            return None

    # ---- level / repeatable / era --------------------------------------------------------------
    def min_level(self, texts):
        best = None
        for t in texts:
            for m in re.finditer(r'getMainLvl\(\)\s*(>=|>)\s*(\d+|xi\.settings\.main\.([A-Z0-9_]+))', t):
                v = int(m.group(2)) if m.group(2).isdigit() else self.settings_num.get(m.group(3))
                if v is None:
                    continue
                if m.group(1) == '>':
                    v += 1
                best = v if best is None else min(best, v)
        return best

    def disabled_refs(self, text):
        tags = set()
        for m in re.finditer(r'ENABLE_([A-Z_]+)\s*(==\s*1|==\s*true|~=\s*0)?', text):
            tag = m.group(1).lower()
            if (tag in EXPANSIONS or tag in FEATURE_ERA) and not self.tag_on(tag):
                tags.add(tag)
        for m in re.finditer(r'xi\.mission\.log_id\.([A-Z_]+)|xi\.mission\.id\.(\w+)\.', text):
            log = m.group(1) or next((k for k, v in self.mission_log.items()
                                      if self.mission_area.get(v) == m.group(2)), None)
            tag = MISSION_LOG_CONTENT.get(log)
            if tag and not self.tag_on(tag):
                tags.add(tag)
        for m in re.finditer(r'xi\.questLog\.([A-Z_]+)|xi\.quest\.id\.(\w+)\.', text):
            log = m.group(1) or next((k for k, v in self.quest_log.items()
                                      if self.quest_area.get(v) == m.group(2)), None)
            tag = QUEST_LOG_CONTENT.get(log)
            if tag and not self.tag_on(tag):
                tags.add(tag)
        return tags

    # ---- quests ----------------------------------------------------------------------------------
    def build_quests(self):
        self.begin_re = re.compile(r'\bquest:begin\(|:addQuest\(\s*quest\.areaId')
        enabled_logs = []
        for key, log in sorted(self.quest_log.items(), key=lambda kv: kv[1]):
            if log < 0:
                continue
            tag = QUEST_LOG_CONTENT.get(key)
            if self.tag_on(tag):
                enabled_logs.append(log)
        scripts = {}
        for path, obj in self.run_scripts('scripts/quests'):
            if obj['__kind'] != 'quest' or not isinstance(obj.areaId, (int, float)) or not isinstance(obj.questId, (int, float)):
                continue
            scripts.setdefault((int(obj.areaId), int(obj.questId)), (path, obj))
        self.scan_legacy()
        # Pre-pass: analyse every script once; note quests another quest's event adds (cross starts).
        analysed = {}
        cross = defaultdict(list)
        for qkey, (path, obj) in sorted(scripts.items()):
            secs = self.analyse_sections(obj, path, 'quest')
            analysed[qkey] = secs
            for sec in secs:
                for z in sec['zones']:
                    for npc, lg, k in z['adds']:
                        if (z['zone'], npc) not in cross[(lg, k)]:
                            cross[(lg, k)].append((z['zone'], npc))

        all_keys = [k for log in enabled_logs for k in self.quest_ids.get(log, {})]
        comment_names = self.build_comment_names(all_keys + [k for ids in self.mission_ids.values() for k in ids]
                                                 + list(self.assault_ids))
        self.comment_names = comment_names

        stats = Counter()
        dropped = []
        quests = []
        spell = defaultdict(list)
        for log in enabled_logs:
            for key, qid in sorted(self.quest_ids.get(log, {}).items(), key=lambda kv: kv[1]):
                entry = {'log': log, 'id': qid, 'key': key}
                era_tags = set()
                banner = self.quest_banner_tag.get((log, key))
                if re.match(r'^(VW_OP_|VOIDWATCH_)|_VOIDWATCHER$', key):  # the enum's Voidwatch naming
                    banner = banner or 'voidwatch'
                if banner and not self.tag_on(banner):
                    era_tags.add(banner)
                script = scripts.get((log, qid))
                starts, rewards, level, repeatable, scroll_ids = [], {}, None, None, []
                prereqs = set()
                name, name_src = None, None
                impl = None
                if script:
                    path, obj = script
                    impl = 'script'
                    head = self.lines(path)[:4]
                    for l in head:
                        m = re.match(r'^--\s*(?!-)(.+?)\s*$', l)
                        if m:
                            name = re.sub(r'^(?:Quest|Hidden Quest)\s*:\s*', '', m.group(1)).replace('_', ' ')
                            name_src = 'header'
                            break
                    secs = analysed[(log, qid)]

                    def avail(s):
                        c = s['check']
                        return bool(re.search(r'\bstatus\s*(==\s*xi\.questStatus\.QUEST_AVAILABLE|'
                                              r'~=\s*xi\.questStatus\.QUEST_(ACCEPTED|COMPLETED)|'
                                              r'<\s*xi\.questStatus\.QUEST_(ACCEPTED|COMPLETED))', c))
                    starts = self.pick_starts(secs, avail, avail)
                    avail_checks = [s['check'] for s in secs if avail(s)]
                    level = self.min_level(avail_checks)
                    gate_text = '\n'.join(avail_checks + [z['finish_src'] for s in secs if avail(s) for z in s['zones']])
                    era_tags |= self.disabled_refs(gate_text)
                    prereqs = set(re.findall(r'xi\.quest\.id\.(\w+)\.([A-Z0-9_]+)', '\n'.join(avail_checks)))
                    prereqs.discard((self.quest_area[log], key))
                    # Level-cap gating: the quest needs MAX_LEVEL above the era cap or raises the cap past it.
                    for m in re.finditer(r'MAX_LEVEL\s*(>=|>)\s*(\d+)', gate_text):
                        if int(m.group(2)) + (1 if m.group(1) == '>' else 0) > self.max_level:
                            era_tags.add('level_cap')
                    for m in re.finditer(r'setLevelCap\(\s*(\d+)', self.text(path)):
                        if int(m.group(1)) > self.max_level:
                            era_tags.add('level_cap')
                    repeatable = False
                    for s in secs:
                        c = s['check']
                        if re.search(r'\bstatus\s*(==\s*xi\.questStatus\.QUEST_COMPLETED|~=\s*xi\.questStatus\.QUEST_ACCEPTED)', c):
                            blob = '\n'.join(z['finish_src'] for z in s['zones'])
                            if re.search(r'quest:complete\(|quest:begin\(|completeQuest\(', blob):
                                repeatable = True
                    rewards = self.reward_to_py(obj.reward)
                    scroll_ids = [i for i in rewards.get('items', []) if i in self.scrolls]
                    for i in self.scrolls_in(self.text(path)):
                        if i not in scroll_ids:
                            scroll_ids.append(i)
                legacy_done = self.legacy_done.get((log, key), [])
                if not impl and (legacy_done or ((log, key) in self.quest_plus and self.legacy_start.get((log, key)))):
                    impl = 'npc'
                log_name = next(k for k, v in self.quest_log.items() if v == log)
                if not starts and cross.get((log_name, key)):
                    starts = [self.make_start(z, n, 'begin') for z, n in cross[(log_name, key)]]
                if not starts and self.legacy_start.get((log, key)):
                    cands = sorted(self.legacy_start[(log, key)], key=lambda c: (c[1] is None, c[2]))
                    seen = set()
                    for zid, npc, _ in cands:
                        if zid is None or (zid, npc) in seen:
                            continue
                        seen.add((zid, npc))
                        starts.append(self.make_start(zid, npc, 'legacy'))
                if not starts and script:
                    hs = self.header_start(script[0])
                    if hs:
                        starts = [hs]
                if not rewards and legacy_done:
                    for _, params, window in legacy_done:
                        tbl = self.eval_table(params)
                        if tbl is not None:
                            rewards = self.reward_to_py(tbl)
                        extra = self.window_rewards(window)
                        for k, v in extra.items():
                            rewards.setdefault(k, v)
                        if rewards:
                            break
                if legacy_done and not script:
                    for _, params, window in legacy_done:
                        for i in self.scrolls_in(params + '\n' + window):
                            if i not in scroll_ids:
                                scroll_ids.append(i)
                    for i in rewards.get('items', []):
                        if i in self.scrolls and i not in scroll_ids:
                            scroll_ids.append(i)
                # Era: start NPC content tag.
                for s in starts:
                    if s.get('_content') and not self.tag_on(s['_content']):
                        era_tags.add(str(s['_content']).lower())
                if era_tags:
                    dropped.append(f'{self.quest_area[log]}.{key} ({", ".join(sorted(era_tags))})')
                    stats['dropped_era'] += 1
                    continue
                if not name:
                    cn = comment_names.get(norm(key))
                    if cn:
                        name, name_src = cn, 'comment'
                    else:
                        name, name_src = humanize_key(key), 'humanized'
                entry['name'] = name
                entry['implemented'] = impl is not None
                if impl:
                    entry['impl'] = impl
                starts = [s for s in starts if not s.get('_missing')] or starts
                if starts:
                    entry['start'] = self.clean_start(starts[0])
                    if len(starts) > 1:
                        entry['starts'] = [self.clean_start(s) for s in starts]
                if repeatable is not None:
                    entry['repeatable'] = repeatable
                if rewards:
                    entry['rewards'] = self.clean_rewards(rewards)
                if level:
                    entry['level'] = level
                entry['_name_src'] = name_src
                entry['_prereqs'] = prereqs
                entry['_scrolls'] = scroll_ids
                quests.append(entry)

        # A quest whose AVAILABLE check requires a dropped quest is dropped too (to a fixpoint).
        gone = {tuple(d.split(' ')[0].split('.')) for d in dropped}
        changed = True
        while changed:
            changed = False
            for q in list(quests):
                need = q['_prereqs'] & gone
                if need:
                    quests.remove(q)
                    gone.add((self.quest_area[q['log']], q['key']))
                    dropped.append(f"{self.quest_area[q['log']]}.{q['key']} (requires {', '.join('.'.join(n) for n in sorted(need))})")
                    stats['dropped_era'] += 1
                    changed = True

        for q in quests:
            stats['name_' + q.pop('_name_src')] += 1
            if q.get('impl'):
                stats['impl_' + q['impl']] += 1
            if q.get('start'):
                stats['start_' + q['start']['how']] += 1
                if 'npc' not in q['start']:
                    stats['start_zone_only'] += 1
            else:
                stats['start_none'] += 1
            stats[f"log_{q['log']}"] += 1
            q.pop('_prereqs')
            for sid in q.pop('_scrolls'):
                spell[sid].append({'log': q['log'], 'id': q['id'], 'name': q['name']})
        return enabled_logs, quests, spell, stats, dropped

    @staticmethod
    def clean_start(s):
        return {k: v for k, v in s.items() if not k.startswith('_')}

    @staticmethod
    def clean_rewards(r):
        order = ('items', 'gil', 'fame', 'fameArea', 'keyItems', 'title', 'exp', 'bayld', 'rankPoints')
        return {k: r[k] for k in order if k in r}

    # ---- missions --------------------------------------------------------------------------------
    def build_missions(self):
        self.begin_re = re.compile(r'\bmission:begin\(|:addMission\(\s*mission\.areaId')
        enabled = []
        for key, log in sorted(self.mission_log.items(), key=lambda kv: kv[1]):
            if self.tag_on(MISSION_LOG_CONTENT.get(key)):
                enabled.append((key, log))
        scripts = {}
        for path, obj in self.run_scripts('scripts/missions'):
            if obj['__kind'] != 'mission' or not isinstance(obj.areaId, (int, float)) or not isinstance(obj.missionId, (int, float)):
                continue
            scripts.setdefault((int(obj.areaId), int(obj.missionId)), (path, obj))
        repeat_mask = {}
        text = self.text(self.root + 'scripts/globals/missions.lua')
        for m in re.finditer(r'\[xi\.mission\.log_id\.([A-Z]+)\]\s*=\s*\{([\d,\s]+)\}', text):
            repeat_mask[self.mission_log[m.group(1)]] = [int(x) for x in m.group(2).replace(' ', '').split(',') if x]

        stats = Counter()
        missions = []
        logs = []
        for key, log in enabled:
            if key == 'ASSAULT':
                rows = self.build_assault(log, stats)
            else:
                ids = self.mission_ids.get(log, {})
                if not ids:
                    continue
                rows = []
                for order, (mkey, mid) in enumerate(sorted(ids.items(), key=lambda kv: kv[1]), start=1):
                    entry = {'log': log, 'id': mid, 'key': mkey}
                    name, name_src, starts, rewards, rank, step = None, None, [], {}, None, None
                    script = scripts.get((log, mid))
                    if script:
                        path, obj = script
                        fname = os.path.basename(path)[:-4]
                        m = re.match(r'^(\d+)_(\d+)(?:_(\d+))?_', fname)
                        if m:
                            rank = int(m.group(1))
                            step = f'{int(m.group(1))}-{int(m.group(2))}' + (f'-{int(m.group(3))}' if m.group(3) else '')
                        else:
                            m = re.match(r'^(\d+)_', fname)
                            if m:
                                step = str(int(m.group(1)))
                        for l in self.lines(path)[:4]:
                            mm = re.match(r'^--\s*(?!-)(.+?)\s*$', l)
                            if mm:
                                name, name_src = mm.group(1), 'header'
                                break
                        secs = self.analyse_sections(obj, path, 'mission')

                        def current(s):
                            return bool(re.search(r'currentMission\s*==\s*mission\.missionId', s['check']))

                        def offered(s):
                            return 'currentMission' in s['check'] and not current(s)
                        starts = self.pick_starts(secs, lambda s: True, current, fallback_any=True)
                        if not starts:
                            hs = self.header_start(path)
                            starts = [hs] if hs else []
                        rewards = self.reward_to_py(obj.reward)
                        stats['impl_script'] += 1
                    if log in (0, 1, 2) and rank is None:
                        rank = 1 if mid <= 2 else 2 if mid <= 9 else 3 if mid <= 12 else 4 if mid == 13 else (mid - 14) // 2 + 5
                    if not name:
                        cn = self.comment_names.get(norm(mkey))
                        if cn:
                            name, name_src = cn, 'comment'
                        else:
                            name, name_src = humanize_key(re.sub(r'\d+$', '', mkey)), 'humanized'
                    stats['name_' + name_src] += 1
                    entry['name'] = name
                    entry['implemented'] = script is not None
                    entry['order'] = order
                    if rank is not None:
                        entry['rank'] = rank
                    if step:
                        entry['step'] = step
                    starts = [s for s in starts if not s.get('_missing')] or starts
                    if starts:
                        entry['start'] = self.clean_start(starts[0])
                        if len(starts) > 1:
                            entry['starts'] = [self.clean_start(s) for s in starts]
                        stats['start_' + starts[0]['how']] += 1
                    else:
                        stats['start_none'] += 1
                    mask = repeat_mask.get(log)
                    if mask is not None and mid < len(mask):
                        entry['repeatable'] = mask[mid] in (1, 3)
                    if rewards:
                        entry['rewards'] = self.clean_rewards(rewards)
                    rows.append(entry)
            if rows:
                logs.append({'log': log, 'key': self.mission_area.get(log, key.lower()),
                             'label': 'Assault' if key == 'ASSAULT' else self.mission_log_label.get(log, humanize_key(key))})
                missions += rows
                stats[f'log_{log}'] += len(rows)
        return logs, missions, stats

    def build_assault(self, log, stats):
        givers = {}
        folder_zid = {f: z for z, f in self.zone_folder.items()}
        impl = {}
        for dirpath, dirs, names in os.walk(self.root + 'scripts'):
            for name in names:
                if not name.endswith('.lua'):
                    continue
                path = os.path.join(dirpath, name).replace('\\', '/')
                text = self.text(path)
                if 'assault' not in text.lower():
                    continue
                rel = path[len(self.root + 'scripts/'):].split('/')
                if rel[0] == 'zones' and len(rel) == 4 and rel[2] == 'npcs':
                    for m in re.finditer(r'onMissionGiverTrigger\([^)]*assaultArea\.([A-Z_]+)\)|'
                                         r'missionToArea\[\w+\]\s*==\s*xi\.assault\.assaultArea\.([A-Z_]+)', text):
                        area = self.assault_area_enum.get(m.group(1) or m.group(2))
                        if area is not None and area not in givers:
                            givers[area] = (folder_zid.get(rel[1]), rel[3][:-4])
                m = re.search(r'assaultID\s*=\s*xi\.assault\.mission\.([A-Z0-9_]+)', text)
                if m:
                    head = re.search(r'^--\s*(?:Assault\s*\d*\s*:\s*)?(.+?)\s*$', self.lines(path)[1] if len(self.lines(path)) > 1 else '')
                    impl[m.group(1)] = (path, head.group(1) if head else None, text)
                elif rel[-2:-1] == ['instances'] and norm(name[:-4]) in {norm(k) for k in self.assault_ids}:
                    key = next(k for k in self.assault_ids if norm(k) == norm(name[:-4]))
                    head = re.search(r'^--\s*(?:Assault\s*\d*\s*:\s*)?(.+?)\s*$', self.lines(path)[1] if len(self.lines(path)) > 1 else '')
                    impl.setdefault(key, (path, head.group(1) if head else None, text))
        rows = []
        for order, (key, mid) in enumerate(sorted(self.assault_ids.items(), key=lambda kv: kv[1]), start=1):
            entry = {'log': log, 'id': mid, 'key': key}
            name, src = None, None
            if key in impl and impl[key][1] and not impl[key][1].startswith('-'):
                name, src = impl[key][1], 'header'
            elif self.comment_names.get(norm(key)):
                name, src = self.comment_names[norm(key)], 'comment'
            else:
                name, src = humanize_key(key), 'humanized'
            stats['name_' + src] += 1
            entry['name'] = name
            entry['implemented'] = key in impl
            if key in impl:
                stats['impl_script'] += 1
            entry['order'] = order
            area = self.assault_area_of.get(mid)
            if area is not None:
                entry['area'] = next(k for k, v in self.assault_area_enum.items() if v == area)
            giver = givers.get(area)
            if giver and giver[0] is not None:
                entry['start'] = self.clean_start(self.make_start(giver[0], giver[1], 'assault'))
                stats['start_assault'] += 1
            else:
                stats['start_none'] += 1
            if key in impl:
                m = re.search(r'suggestedLevel\s*=\s*(\d+)', impl[key][2])
                if m:
                    entry['level'] = int(m.group(1))
            rows.append(entry)
        return rows


# ----------------------------------------------------------------------------------------------
# output
# ----------------------------------------------------------------------------------------------

HEADER = ('-- Generated by tools/generate_quests_phoenix.py from the PhoenixXI (LandSandBoat, GPL-3.0) server\n'
          '-- source{commit}. Do not edit by hand; re-run the generator.\n')

QUEST_FIELDS = ('log', 'id', 'key', 'name', 'implemented', 'impl', 'start', 'starts', 'repeatable', 'rewards', 'level')
MISSION_FIELDS = ('log', 'id', 'key', 'name', 'implemented', 'order', 'rank', 'step', 'area', 'start', 'starts',
                  'repeatable', 'rewards', 'level')
START_FIELDS = ('npc', 'zone', 'zoneId', 'x', 'y', 'z', 'how')


def ordered(d, fields):
    out = {k: d[k] for k in fields if k in d}
    for k in ('start',):
        if k in out:
            out[k] = {f: out[k][f] for f in START_FIELDS if f in out[k]}
    if 'starts' in out:
        out['starts'] = [{f: s[f] for f in START_FIELDS if f in s} for s in out['starts']]
    return out


def write_lua(path, header, body):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(header)
        f.write('return ' + body + '\n')


def rows_block(rows, fields):
    return '{\n' + ''.join(f'        {lua_value(ordered(r, fields), 2)},\n' for r in rows) + '    }'


def git_commit(root):
    head = os.path.join(root, '.git', 'HEAD')
    try:
        ref = open(head, encoding='utf-8').read().strip()
        if ref.startswith('ref: '):
            p = os.path.join(root, '.git', *ref[5:].split('/'))
            if os.path.exists(p):
                return open(p, encoding='utf-8').read().strip()[:8]
            packed = os.path.join(root, '.git', 'packed-refs')
            for line in open(packed, encoding='utf-8'):
                if line.strip().endswith(ref[5:]):
                    return line.split()[0][:8]
        return ref[:8]
    except OSError:
        return ''


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--server', required=True, help='PhoenixXI checkout')
    ap.add_argument('--era', default='toau', choices=EXPANSIONS + ['all'],
                    help='last enabled expansion (default toau, PhoenixXI live)')
    ap.add_argument('--max-level', type=int, default=None,
                    help='era level cap (default 75 unless Abyssea is enabled)')
    ap.add_argument('--out', default=os.path.join(ADDON_DIR, 'data'), help='output folder (default: data/)')
    args = ap.parse_args()

    gen = Generator(args.server, args.era, args.max_level)
    commit = git_commit(args.server)
    header = HEADER.format(commit=f' (commit {commit})' if commit else '') + f'-- Era: {args.era}.\n'

    logs_q, quests, spell, qstats, dropped = gen.build_quests()
    logs_m, missions, mstats = gen.build_missions()

    qlogs = [{'log': l, 'key': gen.quest_area[l], 'label': gen.quest_log_label.get(l, gen.quest_area[l])} for l in logs_q]
    body = ('{\n    logs = ' + lua_value(qlogs, 1) + ',\n    quests = ' + rows_block(quests, QUEST_FIELDS) + ',\n}')
    write_lua(os.path.join(args.out, 'phoenix_quests.lua'), header, body)

    body = ('{\n    logs = ' + lua_value(logs_m, 1) + ',\n    missions = ' + rows_block(missions, MISSION_FIELDS) + ',\n}')
    write_lua(os.path.join(args.out, 'phoenix_missions.lua'), header, body)

    lines = ['{']
    for sid in sorted(spell):
        rows = sorted(spell[sid], key=lambda r: (r['log'], r['id']))
        lines.append(f'    [{sid}] = {lua_value(rows, 1)}, -- {gen.item_names.get(sid, "")}')
    lines.append('}')
    write_lua(os.path.join(args.out, 'phoenix_spell_quests.lua'), header, '\n'.join(lines))

    # Verify under LuaJIT.
    check = lupa.LuaRuntime()
    for name in ('phoenix_quests.lua', 'phoenix_missions.lua', 'phoenix_spell_quests.lua'):
        p = os.path.join(args.out, name).replace('\\', '/')
        ok = check.eval('function(p) local f, e = loadfile(p); if not f then return e end; '
                        'local ok, v = pcall(f); if not ok then return v end; if type(v) ~= "table" then return "not a table" end; return true end')(p)
        if ok is not True:
            raise SystemExit(f'{name} does not load under LuaJIT: {ok}')

    def show(title, stats, logs, labels):
        print(f'\n{title}')
        for l in logs:
            print(f'  log {l:>2} {labels.get(l, ""):<24} {stats.get(f"log_{l}", 0)}')
        for k in sorted(stats):
            if not k.startswith('log_'):
                print(f'  {k}: {stats[k]}')

    print(f'content enabled: {", ".join(sorted(gen.enabled - {"none"}))}')
    show(f'quests: {len(quests)}', qstats, logs_q, gen.quest_log_label)
    print(f'  with start NPC: {sum(1 for q in quests if "start" in q and "npc" in q["start"])}')
    show(f'missions: {len(missions)}', mstats, [l["log"] for l in logs_m], {l["log"]: l["label"] for l in logs_m})
    print(f'  with start NPC: {sum(1 for q in missions if "start" in q and "npc" in q["start"])}')
    print(f'\nspell scrolls with a quest source: {len(spell)}')
    if dropped:
        print(f'\ndropped for era ({len(dropped)}):')
        for d in dropped:
            print('  ' + d)
    if gen.problems:
        print(f'\nscripts that failed to load ({len(gen.problems)}):')
        for p in gen.problems[:40]:
            print('  ' + str(p)[:200])


if __name__ == '__main__':
    sys.exit(main())
