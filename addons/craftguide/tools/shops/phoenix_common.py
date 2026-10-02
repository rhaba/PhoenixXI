"""Shared helpers for the PhoenixXI (LandSandBoat fork) data generators.

How the server is mirrored:

- NPCs come from ``data/zones/<zone>/npcs.yaml`` keyed by server NPC id. Every
  ``modules/init.txt`` entry that ships ``modules/<entry>/zones/<zone>/npcs.yaml``
  is applied on top, in init.txt order, as an RFC 7386 merge patch (maps merge,
  null deletes, lists replace). ``sql/npc_list.sql`` is not used: after LSB's
  YAML migration it is a stale leftover.
- An NPC is live when its merged ``status`` is not ``disappear`` (the loader's
  default when ``status`` is absent) and its ``content`` tag is enabled
  (``luautils::IsContentEnabled``: ``ENABLE_<TAG>`` is only enforced while
  ``RESTRICT_CONTENT`` is on). Disabled NPCs are moved to 0,0,0 and hidden by
  ``zoneutils``.
- Era: ``--era toau`` (default) turns RESTRICT_CONTENT on, enables RoZ, CoP and
  ToAU and disables WotG and later, the way PhoenixXI live runs. Non-expansion
  content flags (survival_guide, field_manuals, ...) keep the checkout's
  ``settings/default/main.lua`` values unless overridden with ``--set``.
  Zones that belong wholly to disabled content (WotG past zones, Abyssea,
  Adoulin/SoA and later) are dropped by zone id as a second guard, because a
  handful of NPCs in those zones carry no content tag.
- Lua modules listed in init.txt are loaded with the checkout's own
  ``modules/module_utils.lua`` so ``Module:new`` conditions, ``xi.pre`` and
  ``addOverrideByEra`` resolve exactly as on the server. The enabled overrides
  are reported with the source span of the replacing function.
"""

from __future__ import annotations

import math
import os
import re
from collections import Counter, defaultdict
from pathlib import Path

import yaml

try:
    import lupa.luajit21 as lupa
except ImportError:  # pragma: no cover - lupa without the luajit21 build
    import lupa

Loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)

EXPANSIONS = ['ROTZ', 'COP', 'TOAU', 'WOTG', 'ACP', 'AMK', 'ASA', 'ABYSSEA', 'VOIDWATCH', 'SOA', 'ROV', 'TVR']

# Zones that exist only for content PhoenixXI keeps disabled. Zone ids per
# data/enums/zone.yaml. Used together with (not instead of) NPC content tags.
WOTG_ZONES = {
    *range(80, 100),                     # [S] past zones, Everbloom Hollow, Ruhotz Silvermines
    136, 137, 138, 155, 156, 164, 171, 175,  # Beaucedine..Eldieme [S]
    71, 182,                             # The Colosseum, Walk of Echoes
}
ABYSSEA_ZONES = {15, 45, 132, 215, 216, 217, 218, 253, 254, 255, 183}  # + Legion maquette
LATE_ZONES = {
    *range(256, 300),                    # Adoulin, Escha/Reisenjima, Mog Garden, Leafallia, RoV and later
    43, 44, 133, 189, 222, 229,          # Diorama/Purgonorgo, Outer Ra'Kaznar [U2]/[U3], Provenance, Throne Room [V]
}
ZONE_ERA = {**{z: 'WOTG' for z in WOTG_ZONES}, **{z: 'ABYSSEA' for z in ABYSSEA_ZONES},
            **{z: 'SOA' for z in LATE_ZONES}}

# Client-style names for zones whose script headers disagree or are missing.
ZONE_NAME_FIXES = {
    'Riverne-Site_A01': 'Riverne - Site #A01',
    'Riverne-Site_B01': 'Riverne - Site #B01',
    'Crawlers_Nest': "Crawlers' Nest",
    'Windurst_Waters': 'Windurst Waters',
    'FeiYin': "Fei'Yin",
    'Southern_San_dOria_[S]': "Southern San d'Oria [S]",
    'Fort_Karugo-Narugo_[S]': 'Fort Karugo-Narugo [S]',
}


def merge_patch(target, patch):
    """RFC 7386, as src/map/data/yaml/merge.cpp."""
    if not isinstance(patch, dict):
        return patch
    out = dict(target) if isinstance(target, dict) else {}
    for key, value in patch.items():
        if value is None:
            out.pop(key, None)
        else:
            out[key] = merge_patch(out.get(key), value)
    return out


def read_init(root: Path) -> list[str]:
    path = root / 'modules' / 'init.txt'
    if not path.exists():
        return []
    entries = []
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            entries.append(line.rstrip('/'))
    return entries


def load_zone_yaml(root: Path, entries: list[str], zone_key: str, name: str) -> tuple[dict, list[str]]:
    base = root / 'data' / 'zones' / zone_key / (name + '.yaml')
    if not base.exists():
        return {}, []
    doc = yaml.load(base.read_text(encoding='utf-8'), Loader=Loader) or {}
    used = []
    for entry in entries:
        overlay = root / 'modules' / entry / 'zones' / zone_key / (name + '.yaml')
        if overlay.is_file():
            doc = merge_patch(doc, yaml.load(overlay.read_text(encoding='utf-8'), Loader=Loader) or {})
            used.append(entry)
    return doc, used


def lua_quote(value: str) -> str:
    return "'" + value.replace('\\', '\\\\').replace("'", "\\'") + "'"


def name_key(value: str) -> str:
    return re.sub(r'[^a-z0-9]', '', value.lower())


def normalize_zone(value: str) -> str:
    """Mirror of vanacompass_phoenix.lua normalizeZone (including PORT_ZONE_ALIASES)."""
    zone = re.sub(r'\s+', ' ', value.replace('[S]', '(S)')).strip()
    aliases = {
        'Al Zahbi': 'Aht Urhgan Whitegate',
        "Ordelle's Caves": 'Ordelles Caves',
        'Windurst Waters North': 'Windurst Waters',
        'Windurst Waters South': 'Windurst Waters',
    }
    return aliases.get(zone, zone)


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
local looseMT = { __index = function(t, k) local p = newProxy(); rawset(t, k, p); return p end }
function looseTable(t) return setmetatable(t or {}, looseMT) end

xi = looseTable({})
xi.module = { registry = {}, commandRegistry = {} }
xi.settings = { main = {} }
xi.data = {}
zones = looseTable({})

ROOT = ''
local requireCache = {}
require = function(name)
    if requireCache[name] ~= nil then return requireCache[name] end
    local result = newProxy()
    if type(name) == 'string' and name:sub(1, 8) == 'modules/' then
        local chunk = loadfile(ROOT .. name .. '.lua')
        if chunk then
            local ok, value = pcall(chunk)
            result = (ok and value ~= nil) and value or true
        end
    end
    requireCache[name] = result
    return result
end

setmetatable(_G, { __index = function() return newProxy() end })

function runFile(path)
    local chunk, err = loadfile(path)
    if not chunk then return false, err end
    local ok, value = pcall(chunk)
    return ok, value
end

-- Enabled overrides in registry (= load) order, with the replacing function's source span.
function collectOverrides()
    local out = {}
    for _, module in ipairs(xi.module.registry) do
        if module.enabled then
            for _, override in ipairs(module.overrides or {}) do
                if override.enabled then
                    local info = debug.getinfo(override.func, 'S')
                    out[#out + 1] = {
                        module = module.name,
                        name = override.name,
                        func = override.func,
                        source = info.source,
                        first = info.linedefined,
                        last = info.lastlinedefined,
                    }
                end
            end
        end
    end
    return out
end
"""


class Phoenix:
    """A PhoenixXI checkout with settings, zones, NPCs and module overrides resolved."""

    def __init__(self, root: Path, era: str = 'toau', overrides: dict[str, int] | None = None):
        self.root = Path(root)
        self.entries = read_init(self.root)
        self.lua = lupa.LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(LUA_PRELUDE)
        self.g = self.lua.globals()
        self.g.ROOT = self.root.as_posix().rstrip('/') + '/'
        self.problems: list[str] = []
        self._load_settings(era, overrides or {})
        self._load_enums()
        self._load_zones()

    # ---- settings -------------------------------------------------------------------
    def _run(self, rel: str):
        ok, value = self.g.runFile(self.g.ROOT + rel)
        if not ok:
            raise SystemExit(f'{rel} failed: {value}')
        return value

    def _load_settings(self, era: str, overrides: dict[str, int]) -> None:
        self._run('settings/default/main.lua')
        main = self.g.xi.settings.main
        if era != 'checkout':
            last = len(EXPANSIONS) if era == 'all' else EXPANSIONS.index(era.upper()) + 1
            main['RESTRICT_CONTENT'] = 0 if era == 'all' else 1
            for index, tag in enumerate(EXPANSIONS):
                main['ENABLE_' + tag] = 1 if index < last else 0
        for key, value in overrides.items():
            main[key] = value
        # Only the content flags; other settings may hold non-UTF-8 text.
        flags = self.lua.eval('''function(main)
            local out = {}
            for k, v in pairs(main) do
                if type(k) == 'string' and (k:find('^ENABLE_') or k == 'RESTRICT_CONTENT')
                    and (type(v) == 'number' or type(v) == 'boolean') then
                    out[k] = v
                end
            end
            return out
        end''')(main)
        self.settings: dict[str, object] = dict(flags.items())

    def content_enabled(self, tag: str | None) -> bool:
        if not tag or tag == 'none':
            return True
        enabled = self.settings.get('ENABLE_' + tag.upper()) in (1, True)
        restricted = self.settings.get('RESTRICT_CONTENT') in (1, True)
        return enabled or not restricted

    def zone_enabled(self, zone_id: int) -> bool:
        return self.content_enabled(ZONE_ERA.get(zone_id))

    def enabled_content(self) -> list[str]:
        return [t for t in EXPANSIONS if self.content_enabled(t)]

    # ---- enums ----------------------------------------------------------------------
    def _load_enums(self) -> None:
        for rel in ('scripts/enum/item.lua', 'scripts/enum/day.lua', 'scripts/enum/region.lua',
                    'scripts/enum/nation.lua'):
            if (self.root / rel).exists():
                self._run(rel)
        zone_enum = yaml.load((self.root / 'data/enums/zone.yaml').read_text(encoding='utf-8'), Loader=Loader)['values']
        self.zone_ids: dict[str, int] = {k: int(v) for k, v in zone_enum.items()}
        table = self.lua.table()
        for key, value in self.zone_ids.items():
            table[key.upper()] = value
        self.g.xi.zone = table
        self.items: dict[str, int] = {}
        self.item_names: dict[int, str] = {}
        for key, value in self.g.xi.item.items():
            if isinstance(key, str) and isinstance(value, (int, float)):
                self.items[key] = int(value)
                self.item_names.setdefault(int(value), key)

    # ---- zones and NPCs -------------------------------------------------------------
    def _load_zones(self) -> None:
        # script folder <- zone key, from each zone's IDs.lua
        self.folders: dict[str, str] = {}
        zones_dir = self.root / 'scripts' / 'zones'
        for folder in os.listdir(zones_dir):
            ids = zones_dir / folder / 'IDs.lua'
            if ids.exists():
                match = re.search(r'zones\[xi\.zone\.([A-Z0-9_]+)\]', ids.read_text(encoding='utf-8', errors='replace'))
                if match:
                    self.folders[match.group(1).lower()] = folder
        self.folder_zone: dict[str, int] = {f: self.zone_ids[k] for k, f in self.folders.items() if k in self.zone_ids}

        self.zone_names: dict[int, str] = {}
        for key, folder in self.folders.items():
            zone_id = self.zone_ids.get(key)
            if zone_id is None:
                continue
            headers: Counter[str] = Counter()
            for path in (zones_dir / folder / 'npcs').glob('*.lua'):
                match = re.search(r'^--\s*Area:\s*(.+?)\s*$', path.read_text(encoding='utf-8', errors='replace'), re.M)
                if match:
                    headers[match.group(1)] += 1
            candidates = [h for h, _ in headers.most_common() if not re.search(r'\(\d+\)|_', h)]
            name = ZONE_NAME_FIXES.get(folder) or (candidates[0] if candidates else folder.replace('_', ' '))
            self.zone_names[zone_id] = name.replace('[S]', '(S)')

        self.npcs: dict[int, list[dict]] = {}
        self.overlay_use: Counter[str] = Counter()
        for key in sorted(os.listdir(self.root / 'data' / 'zones')):
            doc, used = load_zone_yaml(self.root, self.entries, key, 'npcs')
            for entry in used:
                self.overlay_use[entry] += 1
            zone_id = self.zone_ids.get(key)
            rows = []
            for raw_id, npc in (doc.get('npcs') or {}).items():
                npc = npc or {}
                npc_id = int(raw_id)
                at = npc.get('at') or None
                rows.append({
                    'id': npc_id,
                    'zoneId': (npc_id >> 12) & 0xFFF,
                    'script': npc.get('script') or '',
                    'display': npc.get('display_name') or '',
                    'at': at,
                    'status': npc.get('status') or 'disappear',
                    'content': npc.get('content'),
                })
            if zone_id is None and rows:
                zone_id = rows[0]['zoneId']
            if zone_id is not None:
                self.npcs[zone_id] = rows

    def conquest_toggled(self) -> set[str]:
        """name_key of NPCs that xi.conquest.toggleRegionalNPCs shows by region control.

        Their YAML status is 'disappear'; conquest.lua turns them on at runtime."""
        if not hasattr(self, '_conquest_toggled'):
            text = (self.root / 'scripts/globals/conquest.lua').read_text(encoding='utf-8', errors='replace')
            names = re.findall(r'\{\s*offset\s*=\s*\d+\s*,\s*nation\s*=\s*xi\.nation\.\w+\s*\},\s*--\s*(.+?)\s*$',
                               text, re.M)
            self._conquest_toggled = {name_key(n) for n in names if n.strip().lower() != 'flag'}
        return self._conquest_toggled

    def npc_live(self, npc: dict) -> bool:
        shown = npc['status'] != 'disappear' or name_key(npc['display'] or npc['script']) in self.conquest_toggled()
        return (shown and npc['at'] is not None
                and self.content_enabled(npc['content']) and self.zone_enabled(npc['zoneId']))

    def npcs_by_script(self, zone_id: int, script: str) -> list[dict]:
        return [n for n in self.npcs.get(zone_id, []) if n['script'] == script]

    # ---- Lua modules ----------------------------------------------------------------
    def module_files(self) -> list[Path]:
        """Lua files in init.txt order, as src/map/utils/moduleutils.cpp expands them:
        a directory entry loads every .lua below it (recursively, sorted)."""
        files = []
        for entry in self.entries:
            path = self.root / 'modules' / entry
            if path.is_dir():
                found = [p for p in path.rglob('*.lua') if p.is_file()]
                files += sorted(found, key=lambda p: p.relative_to(path).as_posix())
            elif path.suffix == '.lua' and path.is_file():
                files.append(path)
        return [f for f in files if f.name != 'module_utils.lua']

    def load_modules(self) -> list[dict]:
        """Load init.txt Lua modules; return enabled overrides in load order."""
        self._run('modules/module_utils.lua')
        failed = 0
        files = self.module_files()
        for path in files:
            ok, err = self.g.runFile(path.as_posix())
            if not ok:
                failed += 1
                self.problems.append(f'module {path.relative_to(self.root).as_posix()}: {err}')
        rows = []
        for _, row in self.lua.eval('collectOverrides')().items():
            source = str(row['source'] or '')
            rows.append({
                'module': row['module'],
                'name': row['name'],
                'func': row['func'],
                'path': Path(source[1:]) if source.startswith('@') else None,
                'first': int(row['first']),
                'last': int(row['last']),
            })
        self.module_stats = (len(files), failed, len(rows))
        return rows

    @staticmethod
    def source_span(override: dict) -> str:
        path = override['path']
        if path is None or not path.exists():
            return ''
        lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
        return '\n'.join(lines[override['first'] - 1:override['last']])


def summarize_phoenix(px: Phoenix) -> str:
    lines = [f'modules/init.txt entries: {len(px.entries)}',
             f'content enabled: {", ".join(px.enabled_content())} '
             f'(RESTRICT_CONTENT={px.settings.get("RESTRICT_CONTENT")})']
    for entry, count in sorted(px.overlay_use.items()):
        lines.append(f'  npcs.yaml overlay {entry}: {count} zones')
    return '\n'.join(lines)


def group_by(rows, key):
    out = defaultdict(list)
    for row in rows:
        out[key(row)].append(row)
    return out


# ---- map grid ---------------------------------------------------------------------------
class GridCalibrator:
    """Python mirror of vanacompass_phoenix.lua currentGrid(). (craftguide copy: reads the grid
    tables from grids.lua and grid_calibrations.lua next to this file.)

    Order, as in the addon: the Windurst Waters / Crawler's Nest selectors,
    GRID_OVERRIDES, a single-page entry of data/grid_calibrations.lua, then the
    origin solved from vendor anchors (shops.lua rows carrying both a verified
    grid cell and a world position, 40-unit cells, at least two agreeing
    anchors). As a generator-only extension, a multi-page zone whose pages all
    put the point in the same cell is accepted too.
    """

    def __init__(self, addon_dir: Path, anchor_rows: list[dict] | None = None):
        self.lua = lupa.LuaRuntime(unpack_returned_tuples=True)
        source = (addon_dir / 'grids.lua').read_text(encoding='utf-8')
        self.cell = 40

        def table(name: str) -> dict:
            match = re.search(r'local ' + name + r'\s*=\s*(\{.*?\n\});', source, re.S)
            if not match:
                raise SystemExit(f'grids.lua: {name} not found')
            value = self.lua.execute('return ' + match.group(1))
            return {k: dict(v.items()) for k, v in value.items()}

        self.overrides = table('GRID_OVERRIDES')
        self.windurst = table('WINDURST_WATERS_GRIDS')
        self.crawlers = table('CRAWLERS_NEST_GRIDS')
        calibration_text = (addon_dir / 'grid_calibrations.lua').read_text(encoding='utf-8')
        pages = self.lua.execute(calibration_text)
        self.pages = {int(z): [dict(p.items()) for _, p in v.items()] for z, v in pages.items()}
        # A zone listed with a single "<Zone>: Map N" entry has other, uncalibrated
        # pages; its one transform is not trusted for arbitrary points (the addon's
        # pageGridCalibration does use it).
        self.partial: set[int] = set()
        for zone_id, body in re.findall(r'\[(\d+)\]\s*=\s*\{(.*?)\n    \},', calibration_text, re.S):
            if len(self.pages.get(int(zone_id), [])) == 1 and re.search(r':\s*Map\s+\d', body):
                self.partial.add(int(zone_id))
        self.anchors = self.solve_anchors(anchor_rows or [])

    def solve_anchors(self, rows: list[dict]) -> dict[int, dict]:
        candidates: dict[int, dict] = {}
        seen = set()
        for row in rows:
            if None in (row.get('zoneId'), row.get('x'), row.get('y'), row.get('wx'), row.get('wz')):
                continue
            key = (row['zoneId'], round(row['wx'], 3), round(row['wz'], 3), row['x'], row['y'])
            if key in seen:
                continue
            seen.add(key)
            c = candidates.setdefault(row['zoneId'], {'count': 0, 'xLow': -1e18, 'xHigh': 1e18,
                                                       'zLow': -1e18, 'zHigh': 1e18})
            c['count'] += 1
            c['xLow'] = max(c['xLow'], row['wx'] - row['x'] * self.cell)
            c['xHigh'] = min(c['xHigh'], row['wx'] - (row['x'] - 1) * self.cell)
            c['zLow'] = max(c['zLow'], row['wz'] + (row['y'] - 1) * self.cell)
            c['zHigh'] = min(c['zHigh'], row['wz'] + row['y'] * self.cell)
        return {z: {'originX': (c['xLow'] + c['xHigh']) / 2, 'originZ': (c['zLow'] + c['zHigh']) / 2}
                for z, c in candidates.items()
                if c['count'] >= 2 and c['xLow'] < c['xHigh'] and c['zLow'] < c['zHigh']}

    def _cell(self, cal: dict, x: float, ground: float) -> tuple[int, int] | None:
        size = cal.get('cellSize') or self.cell
        column = math.floor((x - cal['originX']) / size) + 1
        row = math.floor((cal['originZ'] - ground) / size) + 1
        if column < 1 or column > 26 or row < 1 or row > 99:
            return None
        return column, row

    def grid(self, zone_id: int, x: float, height: float, ground: float) -> tuple[int, int] | None:
        """World (x, height, ground=z) -> (column, row), or None when uncalibrated."""
        if zone_id in (238, 94):
            return self._cell(self.windurst['south'] if ground < -80 else self.windurst['north'], x, ground)
        if zone_id == 197:
            if height < -15:
                cal = self.crawlers['entrance']
            elif ground < -100 or (ground <= 180 and x < -100):
                cal = self.crawlers['south']
            else:
                cal = self.crawlers['north']
            return self._cell(cal, x, ground)
        if zone_id in self.overrides:
            return self._cell(self.overrides[zone_id], x, ground)
        pages = self.pages.get(zone_id)
        if zone_id in self.partial:
            pages = None
        if pages and len(pages) == 1:
            return self._cell(pages[0], x, ground)
        if zone_id in self.anchors:
            return self._cell(self.anchors[zone_id], x, ground)
        if pages:
            cells = {self._cell(p, x, ground) for p in pages}
            if len(cells) == 1:
                return cells.pop()
        return None


def grid_label(cell: tuple[int, int] | None) -> str | None:
    return None if cell is None else f'{chr(64 + cell[0])}-{cell[1]}'
