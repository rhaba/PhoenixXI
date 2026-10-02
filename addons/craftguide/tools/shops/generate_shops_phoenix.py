#!/usr/bin/env python3
"""Generate VanaCompass's shop catalogs (data/shops.lua, data/guild_shops.lua) from PhoenixXI.

PhoenixXI port of tools/generate_shops.py. Output schema is unchanged:

    shops.lua        [itemId] = { category = 'weapon'|'armor'|'scroll'|'supply', vendors = {
                         { npc, zone, zoneId, price, location?, x?, y?, wx?, wy?, wz?, tier? } } }
    guild_shops.lua  [itemId] = { vendors = {
                         { npc, zone, zoneId, shopType = 'Guild shop', buyMax, initial, maxStock,
                           restockRate, openHour?, closeHour?, holiday?, location?, x?, y?, wx?, wy?, wz? } } }

What changed from the upstream tool (see tools/phoenix_common.py for the shared rules):

- Positions come from data/zones/<zone>/npcs.yaml with module overlays, not the
  stale sql/npc_list.sql. The NPC is matched by its script name inside its own
  zone, so same-named NPCs in other zones can no longer be mixed up.
- Era: a vendor is kept only if its NPC is live under the selected era (status
  not 'disappear', content tag enabled, zone not wholly disabled content).
- Stock follows the PhoenixXI Lua modules. When an enabled module override
  replaces xi.zones.<Zone>.npcs.<Npc>.onTrigger (e.g. modules/era vendor
  adjustments via addOverrideByEra), the stock rows are read from the
  replacing function instead of the base script (both when it calls super).
- Guild shop definitions are scripts/data/guild_shops.lua after the enabled
  xi.server.onServerStart overrides that edit xi.data.guildShops have run
  (PhoenixXI's era guild stock, shared stocks, new Tenshodo guild shops).
  Guild NPCs are matched by script name, which is the key the server uses.
- Map grid: BG-Wiki NPC grid when published (cached), otherwise computed from
  the world position with the same calibrations the addon uses.

Usage:
    python tools/generate_shops_phoenix.py --server <phoenix checkout> \
        --output data/shops.lua --guild-output data/guild_shops.lua --cache <dir> [--era toau]
"""

from __future__ import annotations

import argparse
import json
import re
import time
import urllib.parse
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path

from phoenix_common import GridCalibrator, Phoenix, grid_label, lua_quote, name_key, summarize_phoenix

ADDON_DIR = Path(__file__).resolve().parent   # craftguide copy: grid data lives next to this file
SHOP_CALL = re.compile(r'xi\.shop\.(?:general|nation)\s*\(')
GUILD_CALL = 'xi.guildShops.onTrigger'
ITEM_ROW = re.compile(r'\{\s*(?:(?:xi\.item\.([A-Z0-9_]+))|(\d+))\s*,\s*(\d+)(?:\s*,\s*(\d+))?')


def wiki_zone(value: str) -> str:
    value = value.replace('[S]', '(S)').strip()
    aliases = {
        'Batok Markets': 'Bastok Markets',
        'Southern San dOria': "Southern San d'Oria",
        'Northern San dOria': "Northern San d'Oria",
        'Port San dOria': "Port San d'Oria",
        'Southern SandOria (S)': "Southern San d'Oria (S)",
        'Tavnasian Safehold': 'Tavnazian Safehold',
        'Fort Karugo-Narugo': 'Fort Karugo-Narugo (S)',
        'RuLude Gardens': "Ru'Lude Gardens",
    }
    return aliases.get(value, value)


def fetch_zone_npcs(zone: str, cache_dir: Path, offline: bool) -> dict[str, str]:
    """BG-Wiki zone page -> {name_key(npc): 'G-8'}; cached per zone."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    cache_path = cache_dir / (re.sub(r'[^A-Za-z0-9._-]+', '_', zone) + '.txt')
    if cache_path.exists():
        text = cache_path.read_text(encoding='utf-8')
    elif offline:
        return {}
    else:
        query = urllib.parse.urlencode({
            'action': 'parse', 'page': wiki_zone(zone), 'prop': 'wikitext',
            'format': 'json', 'formatversion': 2, 'redirects': 1,
        })
        request = urllib.request.Request(
            'https://www.bg-wiki.com/api.php?' + query,
            headers={'User-Agent': 'VanaCompass/0.1 personal addon generator'},
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                payload = json.load(response)
            text = payload['parse']['wikitext']
            cache_path.write_text(text, encoding='utf-8')
            time.sleep(0.10)
        except Exception as exc:
            print(f'warning: could not fetch {zone}: {exc}')
            return {}

    result: dict[str, str] = {}
    current: str | None = None
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line.startswith('|NPC.Name='):
            current = line.split('=', 1)[1].strip()
            current = re.sub(r'\[\[(?:[^]|]+\|)?([^]]+)\]\]', r'\1', current)
            current = re.sub(r'<[^>]+>', '', current).strip()
        elif current and line.startswith('|NPC.Position='):
            value = line.split('=', 1)[1].strip()
            match = re.search(r'([A-P](?:/[A-P])?-\d+)', value, re.I)
            if match:
                result[name_key(current)] = match.group(1).upper()
            current = None
    return result


def grid_xy(value: str | None) -> tuple[int | None, int | None]:
    if not value:
        return None, None
    match = re.match(r'([A-P])(?:/[A-P])?-(\d+)', value, re.I)
    if not match:
        return None, None
    return ord(match.group(1).upper()) - 64, int(match.group(2))


def parse_sql_ids(path: Path, table: str) -> set[int]:
    pattern = re.compile(rf'^INSERT INTO `{re.escape(table)}` VALUES \((\d+),', re.M)
    return {int(value) for value in pattern.findall(path.read_text(encoding='utf-8'))}


class ShopBuilder:
    def __init__(self, px: Phoenix, cache: Path, offline: bool):
        self.px = px
        self.cache = cache
        self.offline = offline
        self.overrides = px.load_modules()
        self.report: dict[str, list[str]] = defaultdict(list)
        self.wiki: dict[str, dict[str, str]] = {}
        self.shop_rows: list[dict] = []
        self.guild_rows: list[dict] = []
        self.triggers = self._final_triggers()

    # ---- which code answers onTrigger for each NPC script -----------------------------
    def _final_triggers(self) -> dict[tuple[str, str], dict]:
        zones_dir = self.px.root / 'scripts' / 'zones'
        result: dict[tuple[str, str], dict] = {}
        for path in zones_dir.glob('*/npcs/*.lua'):
            text = path.read_text(encoding='utf-8', errors='replace')
            area = re.search(r'^--\s*Area:\s*(.+?)\s*$', text, re.M)
            npc = re.search(r'^--\s+NPC:\s*(.+?)\s*$', text, re.M)
            result[(path.parents[1].name, path.stem)] = {
                'texts': [text],
                'area': area.group(1).strip() if area else None,
                'header': npc.group(1).strip() if npc else None,
                'overridden': [],
            }
        pattern = re.compile(r'^xi\.zones\.([^.]+)\.npcs\.([^.]+)\.onTrigger$')
        for override in self.overrides:
            match = pattern.match(override['name'])
            if not match:
                continue
            key = (match.group(1), match.group(2))
            text = self.px.source_span(override)
            entry = result.setdefault(key, {'texts': [], 'area': None, 'header': None, 'overridden': []})
            if re.search(r'\bsuper\s*\(', text):
                entry['texts'].append(text)
            else:
                entry['texts'] = [text]
            entry['overridden'].append(override['module'])
        return result

    def _locate(self, folder: str, script: str, kind: str) -> tuple[dict | None, int | None]:
        zone_id = self.px.folder_zone.get(folder)
        if zone_id is None:
            self.report['excluded: unknown zone folder'].append(f'{kind} {folder}/{script}')
            return None, None
        rows = self.px.npcs_by_script(zone_id, script)
        live = [r for r in rows if self.px.npc_live(r)]
        if not rows:
            self.report['excluded: no npcs.yaml entry'].append(f'{kind} {folder}/{script}')
            return None, zone_id
        if not live:
            row = rows[0]
            if not self.px.zone_enabled(zone_id):
                why = 'zone is disabled content'
            elif not self.px.content_enabled(row['content']):
                why = f"content tag '{row['content']}' disabled"
            else:
                why = f"status {row['status']}" + ('' if row['at'] else ', no position')
            self.report['excluded: npc not live'].append(f'{kind} {folder}/{script} ({why})')
            return None, zone_id
        return live[0], zone_id

    def _names(self, folder: str, entry: dict, npc: dict, zone_id: int) -> tuple[str, str]:
        area = entry['area'] if entry['area'] and not re.search(r'_|\(\d+\)', entry['area']) else None
        zone = wiki_zone(area or self.px.zone_names.get(zone_id, folder.replace('_', ' ')))
        name = npc['display'] or entry['header'] or npc['script'].replace('_', ' ')
        return zone, name

    def _grid(self, zone: str, names: list[str]) -> str | None:
        if zone not in self.wiki:
            self.wiki[zone] = fetch_zone_npcs(zone, self.cache, self.offline)
        for name in names:
            grid = self.wiki[zone].get(name_key(name))
            if grid:
                return grid
        return None

    def _position(self, vendor: dict, npc: dict, zone: str, names: list[str]) -> None:
        x, height, ground = (float(v) for v in npc['at'][:3])
        vendor['wx'], vendor['wy'], vendor['wz'] = round(x, 3), round(height, 3), round(ground, 3)
        grid = self._grid(zone, names)
        if grid:
            vendor['location'] = grid
            vendor['x'], vendor['y'] = grid_xy(grid)
            vendor['gridSource'] = 'wiki'

    def fill_computed_grids(self, rows: list[dict]) -> int:
        calibrator = GridCalibrator(ADDON_DIR, [r for r in rows if r.get('gridSource') == 'wiki'])
        filled = 0
        for row in rows:
            if row.get('location') or row.get('wx') is None:
                continue
            cell = calibrator.grid(row['zoneId'], row['wx'], row['wy'], row['wz'])
            if cell:
                row['location'] = grid_label(cell)
                row['x'], row['y'] = cell
                row['gridSource'] = 'computed'
                filled += 1
        return filled

    # ---- general / nation shops ------------------------------------------------------
    def build_shops(self, weapons: set[int], equipment: set[int]) -> dict[int, dict]:
        catalog: dict[int, dict] = {}
        seen: set[tuple[int, str, int, int]] = set()
        rows_out: list[dict] = []
        for (folder, script), entry in sorted(self.triggers.items()):
            text = '\n'.join(entry['texts'])
            if not SHOP_CALL.search(text):
                continue
            rows = []
            for match in ITEM_ROW.finditer(text):
                item_id = self.px.items.get(match.group(1)) if match.group(1) else int(match.group(2))
                if item_id is None:
                    self.report['warning: unknown item constant'].append(f'{folder}/{script}: {match.group(1)}')
                    continue
                rows.append((item_id, int(match.group(3)), int(match.group(4)) if match.group(4) else None))
            if not rows:
                self.report['warning: shop call without literal stock'].append(f'{folder}/{script}')
                continue
            npc, zone_id = self._locate(folder, script, 'shop')
            if npc is None:
                continue
            zone, name = self._names(folder, entry, npc, zone_id)
            if entry['overridden']:
                self.report['stock from module override'].append(
                    f"{folder}/{script} ({', '.join(dict.fromkeys(entry['overridden']))})")
            base: dict = {'npc': name, 'zone': zone, 'zoneId': zone_id}
            self._position(base, npc, zone, [name, entry['header'] or '', script])
            for item_id, price, tier in rows:
                key = (item_id, name_key(name), zone_id, price)
                if key in seen:
                    continue
                seen.add(key)
                constant = self.px.item_names.get(item_id, '')
                if constant.startswith('SCROLL_OF_'):
                    category = 'scroll'
                elif item_id in weapons:
                    category = 'weapon'
                elif item_id in equipment:
                    category = 'armor'
                else:
                    category = 'supply'
                vendor = dict(base, price=price)
                if tier is not None:
                    vendor['tier'] = tier
                catalog.setdefault(item_id, {'category': category, 'vendors': []})['vendors'].append(vendor)
                rows_out.append(vendor)
        self.shop_rows = rows_out
        return catalog

    # ---- guild shops -----------------------------------------------------------------
    def guild_definitions(self) -> dict[str, dict]:
        lua = self.px.lua
        self.px._run('scripts/data/guild_shops.lua')
        applied = []
        runner = lua.eval('''function(func)
            local env = setmetatable({ super = function() end }, { __index = getfenv(func) })
            setfenv(func, env)
            return pcall(func)
        end''')
        for override in self.overrides:
            if override['name'] != 'xi.server.onServerStart':
                continue
            if 'guildShops' not in self.px.source_span(override):
                continue
            result = runner(override['func'])
            ok, err = (result if isinstance(result, tuple) else (result, None))
            if not ok:
                self.px.problems.append(f"{override['module']} onServerStart failed: {err}")
            applied.append(override['module'])
        self.report['guild definitions patched by'] = sorted(set(applied))

        days = {int(v): k.title() for k, v in lua.globals().xi.day.items()}
        definitions: dict[str, dict] = {}
        for name, shop in lua.globals().xi.data.guildShops.items():
            hours = shop['hours']
            stock = []
            for _, row in (shop['stock'] or {}).items():
                if row['id'] is None:
                    self.report['warning: guild stock row without item id'].append(name)
                    continue
                values = {'itemId': int(row['id'])}
                for key in ('initial', 'maxStock', 'targetStock', 'buyMax', 'restockRate'):
                    if row[key] is not None:
                        values[key] = int(row[key])
                stock.append(values)
            definitions[name] = {
                'shared': shop['sharedStock'],
                'hours': (int(hours[1]), int(hours[2])) if hours is not None else None,
                'holiday': days.get(int(shop['holiday'])) if shop['holiday'] is not None else None,
                'stock': stock,
            }
        return definitions

    def build_guilds(self) -> dict[int, dict]:
        definitions = self.guild_definitions()
        catalog: dict[int, dict] = {}
        seen: set[tuple[int, str, int]] = set()
        matched = set()
        for (folder, script), entry in sorted(self.triggers.items()):
            text = '\n'.join(entry['texts'])
            if GUILD_CALL not in text:
                continue
            definition = definitions.get(script)
            if definition is None:
                self.report['warning: guild NPC without definition'].append(f'{folder}/{script}')
                continue
            npc, zone_id = self._locate(folder, script, 'guild')
            if npc is None:
                continue
            matched.add(script)
            owner = str(definition.get('shared') or script)
            stock_definition = definitions.get(owner, definition)
            zone, name = self._names(folder, entry, npc, zone_id)
            base: dict = {'npc': name, 'zone': zone, 'zoneId': zone_id, 'shopType': 'Guild shop'}
            self._position(base, npc, zone, [name, entry['header'] or '', script])
            for row in stock_definition.get('stock', []):
                item_id = int(row['itemId'])
                key = (item_id, name_key(name), zone_id)
                if key in seen:
                    continue
                seen.add(key)
                vendor = dict(base)
                vendor.update({
                    'buyMax': int(row.get('buyMax', 0)),
                    'initial': int(row.get('initial', 0)),
                    'maxStock': int(row.get('maxStock', 0)),
                    'restockRate': int(row.get('restockRate', 0)),
                })
                if stock_definition.get('hours'):
                    vendor['openHour'], vendor['closeHour'] = stock_definition['hours']
                if stock_definition.get('holiday'):
                    vendor['holiday'] = stock_definition['holiday']
                catalog.setdefault(item_id, {'vendors': []})['vendors'].append(vendor)
                self.guild_rows.append(vendor)
        unused = sorted(set(definitions) - matched)
        if unused:
            self.report['guild definitions with no live NPC'] = unused
        return catalog


def lua_value(value) -> str:
    if isinstance(value, str):
        return lua_quote(value)
    if isinstance(value, float):
        return f'{value:.3f}'.rstrip('0').rstrip('.') if value != int(value) else str(int(value))
    return str(value)


def write_catalog(path: Path, catalog: dict[int, dict], header: str) -> None:
    lines = [header, 'return {']
    for item_id in sorted(catalog):
        item = catalog[item_id]
        lines.append(f"    [{item_id}] = {{ category = {lua_quote(str(item['category']))}, vendors = {{")
        for vendor in item['vendors']:
            values = [f"{key} = {lua_value(vendor[key])}" for key in
                      ('npc', 'zone', 'zoneId', 'price', 'location', 'x', 'y', 'wx', 'wy', 'wz', 'tier')
                      if key in vendor]
            lines.append('        { ' + ', '.join(values) + ' },')
        lines.append('    } },')
    lines.extend(['}', ''])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines), encoding='utf-8', newline='\n')


def write_guild_catalog(path: Path, catalog: dict[int, dict], header: str) -> None:
    lines = [header, 'return {']
    for item_id in sorted(catalog):
        lines.append(f'    [{item_id}] = {{ vendors = {{')
        for vendor in catalog[item_id]['vendors']:
            values = [f'{key} = {lua_value(vendor[key])}' for key in
                      ('npc', 'zone', 'zoneId', 'shopType', 'buyMax', 'initial', 'maxStock',
                       'restockRate', 'openHour', 'closeHour', 'holiday', 'location',
                       'x', 'y', 'wx', 'wy', 'wz') if key in vendor]
            lines.append('        { ' + ', '.join(values) + ' },')
        lines.append('    } },')
    lines.extend(['}', ''])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines), encoding='utf-8', newline='\n')


def parse_set(values: list[str]) -> dict[str, int]:
    out = {}
    for value in values:
        key, _, raw = value.partition('=')
        out[key.strip()] = int(raw)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--server', type=Path, required=True, help='PhoenixXI checkout')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--guild-output', type=Path)
    parser.add_argument('--cache', type=Path, required=True, help='BG-Wiki page cache directory')
    parser.add_argument('--offline', action='store_true', help='use only cached BG-Wiki pages')
    parser.add_argument('--era', default='toau',
                        help="last enabled expansion (rotz, cop, toau, wotg, ...), 'all', or 'checkout' "
                             'to use settings/default/main.lua as-is (default: toau)')
    parser.add_argument('--set', action='append', default=[], metavar='KEY=VALUE',
                        help='override an xi.settings.main value, e.g. ENABLE_SURVIVAL_GUIDE=0')
    parser.add_argument('--report', type=Path, help='write the inclusion/exclusion report here')
    args = parser.parse_args()

    px = Phoenix(args.server, args.era, parse_set(args.set))
    print(summarize_phoenix(px))
    builder = ShopBuilder(px, args.cache, args.offline)
    files, failed, count = px.module_stats
    print(f'lua modules: {files} files ({failed} failed to load), {count} enabled overrides')

    weapons = parse_sql_ids(args.server / 'sql/item_weapon.sql', 'item_weapon')
    equipment = parse_sql_ids(args.server / 'sql/item_equipment.sql', 'item_equipment')
    catalog = builder.build_shops(weapons, equipment)
    guild_catalog = builder.build_guilds() if args.guild_output is not None else {}

    filled = builder.fill_computed_grids(builder.shop_rows + builder.guild_rows)
    era = f'{args.era} era' + (f", {', '.join(args.set)}" if args.set else '')
    write_catalog(args.output, catalog,
                  f'-- Generated from PhoenixXI shop scripts and modules ({era}); do not hand-edit.')
    counts = Counter(str(i['category']) for i in catalog.values())
    print(f'Generated {len(catalog)} items and {sum(len(i["vendors"]) for i in catalog.values())} '
          f'vendor rows from {len({(r["npc"], r["zoneId"]) for r in builder.shop_rows})} NPCs: {dict(counts)}')
    if args.guild_output is not None:
        write_guild_catalog(args.guild_output, guild_catalog,
                            f'-- Generated from PhoenixXI dynamic guild-shop data ({era}); do not hand-edit.')
        print(f'Generated {len(guild_catalog)} guild-shop items and '
              f'{sum(len(i["vendors"]) for i in guild_catalog.values())} vendor rows from '
              f'{len({(r["npc"], r["zoneId"]) for r in builder.guild_rows})} NPCs.')
    rows = builder.shop_rows + builder.guild_rows
    sources = Counter(r.get('gridSource', 'none') for r in rows)
    print(f'grid source per vendor row: {dict(sources)} ({filled} computed from world position)')

    report = []
    for key, values in sorted(builder.report.items()):
        values = sorted(set(values))
        report.append(f'## {key} ({len(values)})')
        report += [f'  {v}' for v in values]
    for problem in px.problems:
        report.append(f'problem: {problem}')
    missing = sorted({(r['zone'], r['npc']) for r in rows if not r.get('location')})
    report.append(f'## vendors without a grid cell ({len(missing)})')
    report += [f'  {z}: {n}' for z, n in missing]
    if args.report:
        args.report.write_text('\n'.join(report) + '\n', encoding='utf-8')
        print(f'report: {args.report}')
    else:
        print('\n'.join(report))


if __name__ == '__main__':
    main()
