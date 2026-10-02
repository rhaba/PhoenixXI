"""
Builds craftguide's bundled data from a PhoenixXI server checkout. The addon itself needs nothing
else at runtime; this only runs when you refresh the data.

    data/crafts.lua    recipes, rank tests and guild suppliers for all eight synthesis crafts
    data/prices.lua    NPC buy sources and NPC sell prices
    data/fishing.lua   fish with skill, NPC price, where to catch them and the best baits

Usage:
    pip install lupa
    python tools/build_crafts.py --server <phoenix checkout> --shops <vanacompass_phoenix>/data [--era toau]

Inputs:
  - sql/synth_recipes.sql: synthesis recipes (no desynthesis). Rows tagged with disabled content
    are dropped; key-item recipes are kept and flagged.
  - scripts/globals/hobbies/crafting/guild_master.lua: rank test items.
  - scripts/globals/shop.lua: each guild's general supplier (fixed price, rank-gated) and the
    regional produce vendors (fixed price, open while their nation holds the region).
  - scripts/data/guild_shops.lua + scripts/globals/guild_shops.lua: guild shops. Prices follow
    stock; items that restock are priced at their target stock (what the shelf settles to each
    day). Items with no starting stock and no restock only appear after players sell them, so they
    aren't counted.
  - VanaCompass - Phoenix's data/shops.lua: regular NPC shops (fixed price), already extracted
    from Phoenix's NPC scripts with its era stock changes.
  - sql/item_basic.sql + module SQL: each item's base NPC sell price (NOSALE items excluded). The
    addon applies the player's fame at runtime.
  - NPC shop scripts and era module overrides: the fame area each shop prices by
    (xi.shop.general(player, stock, xi.fameArea.X) or xi.shop.nation(player, stock, xi.nation.X)).
    Fame moves prices by up to 10% (scripts/globals/shop.lua, getPriceRank).
  - sql/fishing_*.sql: fish, catch groups, zones/areas and bait affinity.
"""

import argparse
import os
import re
from collections import defaultdict

import lupa.luajit21 as lupa

CRAFTS = ['Woodworking', 'Smithing', 'Goldsmithing', 'Clothcraft', 'Leathercraft', 'Bonecraft', 'Alchemy', 'Cooking']
EXPANSIONS = ['ROTZ', 'COP', 'TOAU', 'WOTG', 'ACP', 'AMK', 'ASA', 'ABYSSEA', 'VOIDWATCH', 'SOA', 'ROV', 'TVR']
RANK_NAMES = ['Amateur', 'Recruit', 'Initiate', 'Novice', 'Apprentice', 'Journeyman', 'Craftsman',
              'Artisan', 'Adept', 'Veteran', 'Expert']
REGION_NAMES = {
    'RONFAURE': 'Ronfaure', 'ZULKHEIM': 'Zulkheim', 'NORVALLEN': 'Norvallen', 'GUSTABERG': 'Gustaberg',
    'DERFLAND': 'Derfland', 'SARUTABARUTA': 'Sarutabaruta', 'KOLSHUSHU': 'Kolshushu', 'ARAGONEU': 'Aragoneu',
    'FAUREGANDI': 'Fauregandi', 'VALDEAUNIA': 'Valdeaunia', 'QUFIMISLAND': 'Qufim Island', 'LITELOR': "Li'Telor",
    'KUZOTZ': 'Kuzotz', 'VOLLBOW': 'Vollbow', 'ELSHIMOLOWLANDS': 'Elshimo Lowlands', 'ELSHIMOUPLANDS': 'Elshimo Uplands',
    'TULIA': "Tu'Lia", 'MOVALPOLOS': 'Movalpolos', 'TAVNAZIANARCH': 'Tavnazian Archipelago',
}
NATIONS = {'SANDORIA': "San d'Oria", 'BASTOK': 'Bastok', 'WINDURST': 'Windurst'}


def chunked(rows, size=300):
    """Wrap rows in functions so no single Lua function passes LuaJIT's 65536-constant limit."""
    out = []
    for i in range(0, len(rows), size):
        out.append('(function()')
        out.extend(rows[i:i + size])
        out.append('end)();')
    return out


def name_key(text):
    """Loose key for matching NPC and zone names across sources."""
    return re.sub(r'[^a-z0-9]', '', text.lower())


def lua_str(s):
    return "'" + str(s).replace('\\', '\\\\').replace("'", "\\'") + "'"


def split_values(text):
    out, cur, quoted, i = [], [], False, 0
    while i < len(text):
        c = text[i]
        if c == '\\' and quoted:
            cur.append(text[i:i + 2]); i += 2; continue
        if c == "'":
            quoted = not quoted
        if c == ',' and not quoted:
            out.append(''.join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    out.append(''.join(cur).strip())
    return out


def sql_rows(path, table):
    prefix = f'INSERT INTO `{table}` VALUES ('
    for line in open(path, encoding='utf-8'):
        if line.startswith(prefix):
            # The statement ends at ');'; anything after it is a trailing -- comment.
            yield split_values(line[len(prefix):line.index(');', len(prefix))])


def unquote(v):
    return v[1:-1].replace("\\'", "'") if len(v) >= 2 and v[0] == "'" else v


def zone_display(folder):
    return folder.replace('_', ' ').replace(' dOria', " d'Oria").replace('San dOria', "San d'Oria")


class Builder:
    def __init__(self, root, shops_dir, era):
        self.root = root
        self.shops_dir = shops_dir
        last = len(EXPANSIONS) - 1 if era == 'all' else EXPANSIONS.index(era.upper())
        self.enabled = set(EXPANSIONS[:last + 1])
        self.lua = lupa.LuaRuntime()
        self.lua.execute('xi = {}')
        self.lua.execute(open(os.path.join(root, 'scripts', 'enum', 'item.lua'), encoding='utf-8-sig').read())
        self.item_enum = {k: int(v) for k, v in self.lua.globals().xi.item.items()}
        self.item_names = {}
        self.base_sell, self.nosale = {}, set()
        for v in sql_rows(os.path.join(root, 'sql', 'item_basic.sql'), 'item_basic'):
            item = int(v[0])
            self.item_names.setdefault(unquote(v[2]), item)
            self.base_sell[item] = int(v[9])
            if '@FLAG_NOSALE' in v[7]:
                self.nosale.add(item)
        self.apply_item_module_sql()
        self.npc_zone = self.index_npc_scripts()

    def scripts(self, rel):
        return open(os.path.join(self.root, rel), encoding='utf-8-sig').read()

    # ---- prices ---------------------------------------------------------------------------
    def apply_item_module_sql(self):
        init = os.path.join(self.root, 'modules', 'init.txt')
        if not os.path.exists(init):
            return
        update = re.compile(r"UPDATE\s+`?item_basic`?\s+SET\s+(.+?)\s+WHERE\s+`?itemid`?\s*=\s*(\d+)\s*;", re.I | re.S)
        for line in open(init, encoding='utf-8'):
            entry = line.strip().rstrip('/')
            if not entry or entry.startswith('#'):
                continue
            path = os.path.join(self.root, 'modules', entry)
            files = [path] if entry.endswith('.sql') and os.path.isfile(path) else (
                sorted(os.path.join(path, n) for n in os.listdir(path) if n.endswith('.sql'))
                if os.path.isdir(path) and re.search(r'(^|/)sql(/|$)', entry) else [])
            for f in files:
                text = re.sub(r'--[^\n]*', '', open(f, encoding='utf-8-sig').read())
                for m in update.finditer(text):
                    item = int(m.group(2))
                    for clause in re.split(r',(?![^()]*\))', m.group(1)):
                        key, _, value = clause.partition('=')
                        key, value = key.strip().strip('`').lower(), value.strip()
                        if key == 'basesell' and value.isdigit():
                            self.base_sell[item] = int(value)
                        elif key == 'flags' and '@FLAG_NOSALE' in value:
                            if re.search(r'&\s*~\s*@FLAG_NOSALE', value):
                                self.nosale.discard(item)
                            elif '|' in value:
                                self.nosale.add(item)

    def sell_price(self, item):
        """Base NPC sell price; the addon applies fame (BaseSell x (priceRank + 389) / 400)."""
        base = self.base_sell.get(item, 0)
        if item in self.nosale or base <= 0:
            return None
        return base

    # ---- shop fame areas ------------------------------------------------------------------
    SHOP_FAME = re.compile(r'xi\.shop\.(general|nation)\(\s*player\s*,\s*[A-Za-z_.\[\]]+\s*(?:,\s*xi\.(?:fameArea|nation)\.([A-Z_]+))?')

    def shop_fame(self):
        """(zone key, npc key) -> fame area name ('' = no fame pricing) for every shop NPC."""
        out = {}
        def record(folder, npc, text):
            m = self.SHOP_FAME.search(text)
            if m:
                out[(name_key(folder), name_key(npc))] = m.group(2) or ''
        zones = os.path.join(self.root, 'scripts', 'zones')
        for folder in os.listdir(zones):
            npcs = os.path.join(zones, folder, 'npcs')
            if os.path.isdir(npcs):
                for fn in os.listdir(npcs):
                    if fn.endswith('.lua'):
                        record(folder, fn[:-4], open(os.path.join(npcs, fn), encoding='utf-8-sig', errors='replace').read())
        # Module overrides of an NPC's onTrigger replace the base script's shop.
        override = re.compile(r"xi\.zones\.([A-Za-z_']+)\.npcs\.([A-Za-z_'\-]+)\.onTrigger")
        for dirpath, _, files in os.walk(os.path.join(self.root, 'modules')):
            for fn in files:
                if not fn.endswith('.lua'):
                    continue
                text = open(os.path.join(dirpath, fn), encoding='utf-8-sig', errors='replace').read()
                marks = list(override.finditer(text))
                for i, m in enumerate(marks):
                    end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
                    record(m.group(1), m.group(2), text[m.start():end])
        return out

    # ---- NPC locations --------------------------------------------------------------------
    def index_npc_scripts(self):
        """NPC script name -> zone folder, for naming where suppliers stand."""
        out = {}
        zones = os.path.join(self.root, 'scripts', 'zones')
        for folder in os.listdir(zones):
            npcs = os.path.join(zones, folder, 'npcs')
            if os.path.isdir(npcs):
                for fn in os.listdir(npcs):
                    if fn.endswith('.lua'):
                        out.setdefault(fn[:-4], folder)
        return out

    def where(self, npc):
        folder = self.npc_zone.get(npc.replace(' ', '_'))
        return zone_display(folder) if folder else ''

    # ---- recipes / ranks -------------------------------------------------------------------
    def recipes(self):
        out, skipped = [], 0
        for v in sql_rows(os.path.join(self.root, 'sql', 'synth_recipes.sql'), 'synth_recipes'):
            if v[1] != '0':
                continue
            tag = unquote(v[30]) if len(v) > 30 and v[30] != 'NULL' else None
            if tag and tag.upper() not in self.enabled:
                skipped += 1
                continue
            skills = {CRAFTS[i]: int(v[3 + i]) for i in range(8) if int(v[3 + i]) > 0}
            if not skills:
                continue
            ingredients = defaultdict(int)
            for raw in v[13:21]:
                if int(raw):
                    ingredients[int(raw)] += 1
            out.append({
                'id': int(v[0]), 'skills': skills, 'keyItem': int(v[2]), 'crystal': int(v[11]),
                'ingredients': sorted(ingredients.items()), 'result': int(v[21]), 'qty': int(v[25]),
                'name': unquote(v[29]),
            })
        return out, skipped

    def rank_tests(self):
        text = self.scripts('scripts/globals/hobbies/crafting/guild_master.lua')
        tests = {}
        for craft in CRAFTS:
            m = re.search(r'\[xi\.guild\.' + craft.upper() + r'\]\s*=\s*\{(.*?)\}', text, re.S)
            if m:
                tests[craft] = [{'rank': i, 'name': RANK_NAMES[i], 'skill': i * 10 - 2,
                                 'item': self.item_enum.get(k)}
                                for i, k in enumerate(re.findall(r'xi\.item\.([A-Z0-9_]+)', m.group(1)), start=1)]
        return tests

    # ---- buy sources ------------------------------------------------------------------------
    def general_guild(self):
        """Each guild's fixed-price supplier (xi.shop.generalGuildStock): item -> offers."""
        text = self.scripts('scripts/globals/shop.lua')
        block = text[text.index('xi.shop.generalGuildStock ='):]
        block = block[:block.index('\n}\n')]
        suppliers = {}
        zones = os.path.join(self.root, 'scripts', 'zones')
        for folder in os.listdir(zones):
            npcs = os.path.join(zones, folder, 'npcs')
            if not os.path.isdir(npcs):
                continue
            for fn in os.listdir(npcs):
                src = open(os.path.join(npcs, fn), encoding='utf-8-sig', errors='replace').read()
                if 'xi.shop.generalGuild(' in src:
                    m = re.search(r'guildSkillId\s*=\s*xi\.skill\.([A-Z]+)', src)
                    if m:
                        suppliers.setdefault(m.group(1).title(), []).append((fn[:-4].replace('_', ' '), zone_display(folder)))
        offers = defaultdict(list)
        for m in re.finditer(r'\[xi\.skill\.([A-Z]+)\]\s*=\s*\{(.*?)\n    \}', block, re.S):
            craft = m.group(1).title()
            for item, price, rank in re.findall(r'xi\.item\.([A-Z0-9_]+),\s*(\d+),\s*xi\.craftRank\.([A-Z]+)', m.group(2)):
                item_id = self.item_enum.get(item)
                if item_id is None:
                    continue
                for npc, zone in suppliers.get(craft, []):
                    offers[item_id].append({'price': int(price), 'npc': npc, 'zone': zone, 'kind': 'guild-supply',
                                            'craft': craft, 'rank': RANK_NAMES.index(rank.title())})
        return offers, suppliers

    def regional(self):
        text = self.scripts('scripts/globals/shop.lua')
        vendors = defaultdict(list)
        for npc, region, nation, fame in re.findall(r"\['([^']+)'\s*\]\s*=\s*\{\s*xi\.region\.([A-Z_]+),\s*xi\.nation\.([A-Z]+),\s*xi\.fameArea\.([A-Z_]+)", text):
            vendors[region.replace('_', '')].append((npc.strip().replace('_', ' '), NATIONS.get(nation, nation.title()), fame))
        block = text[text.index('local regionalStockTable ='):]
        block = block[:block.index('\n}\n')]
        offers = defaultdict(list)
        for m in re.finditer(r'\[xi\.region\.([A-Z_]+)\]\s*=\s*\{(.*?)\n    \}', block, re.S):
            region = m.group(1).replace('_', '')
            if region == 'TAVNAZIANARCH' and 'COP' not in self.enabled:
                continue
            for item, price in re.findall(r'xi\.item\.([A-Z0-9_]+),\s*(\d+)', m.group(2)):
                item_id = self.item_enum.get(item)
                for npc, nation, fame in vendors.get(region, []):
                    offers[item_id].append({'price': int(price), 'npc': npc, 'zone': self.where(npc), 'kind': 'regional',
                                            'region': REGION_NAMES.get(region, region.title()), 'nation': nation, 'fame': fame})
        return offers

    def guild_shops(self):
        g = self.lua.globals()
        self.lua.execute('xi.day = setmetatable({}, { __index = function(_, k) return k end })')
        self.lua.execute(self.scripts('scripts/data/guild_shops.lua'))
        offers = defaultdict(list)
        for npc, shop in g.xi.data.guildShops.items():
            stock = shop['stock']
            if stock is None:
                continue
            hours = shop['hours']
            for cfg in stock.values():
                initial, restock = cfg['initial'] or 0, cfg['restockRate'] or 0
                if initial <= 0 and restock <= 0:
                    continue  # only stocked by player sales
                max_stock = cfg['maxStock']
                target = cfg['targetStock'] or max_stock * 3 / 4
                floor = cfg['priceFloor'] or max_stock * 3 / 4
                buy_max = cfg['buyMax']
                knee = 2 / 3 * floor
                if floor <= 0:
                    price = buy_max
                elif target <= knee:
                    price = buy_max * (125 - int(150 * target / floor)) // 125
                else:
                    price = buy_max * (200 - int(100 * (target - knee) / (max_stock - knee))) // 1000
                offers[int(cfg['id'])].append({
                    'price': int(price), 'npc': npc.replace('_', ' '), 'zone': self.where(npc), 'kind': 'guild-shop',
                    'hours': f'{hours[1]}:00-{hours[2]}:00' if hours else '', 'holiday': str(shop['holiday'] or '').title(),
                })
        return offers

    def regular_shops(self):
        path = os.path.join(self.shops_dir, 'shops.lua')
        offers = defaultdict(list)
        if not os.path.exists(path):
            print('warning: no VanaCompass shops.lua; regular NPC shops left out')
            return offers
        data = self.lua.execute(open(path, encoding='utf-8').read())
        fame = self.shop_fame()
        self.fame_misses = set()
        for item, entry in data.items():
            for v in entry['vendors'].values():
                if v['price']:
                    area = fame.get((name_key(v['zone']), name_key(v['npc'])))
                    if area is None:
                        self.fame_misses.add(f"{v['npc']} ({v['zone']})")
                    offers[int(item)].append({'price': int(v['price']), 'npc': v['npc'], 'zone': v['zone'],
                                              'loc': v['location'] or '', 'kind': 'shop', 'fame': area or ''})
        return offers

    # ---- fishing ------------------------------------------------------------------------------
    def fishing(self):
        sql = lambda t: os.path.join(self.root, 'sql', t + '.sql')
        zones = {int(v[0]): unquote(v[1]).replace('_', ' ') for v in sql_rows(sql('fishing_zone'), 'fishing_zone')}
        areas = {(int(v[0]), int(v[1])): unquote(v[2]) for v in sql_rows(sql('fishing_area'), 'fishing_area')}
        groups = defaultdict(list)
        for v in sql_rows(sql('fishing_group'), 'fishing_group'):
            groups[int(v[0])].append(int(v[1]))
        where = defaultdict(set)
        for v in sql_rows(sql('fishing_catch'), 'fishing_catch'):
            zone_id, area_id, group_id = int(v[0]), int(v[1]), int(v[2])
            name = zones.get(zone_id, f'Zone {zone_id}')
            if zone_id >= 256 and 'SOA' not in self.enabled:
                continue
            if ('[S]' in name or 'Abyssea' in name) and 'WOTG' not in self.enabled:
                continue
            area = areas.get((zone_id, area_id), '')
            label = name if area in ('', 'Whole Zone') else f'{name} ({area})'
            for fish in groups[group_id]:
                where[fish].add(label)
        baits = {int(v[0]): unquote(v[1]) for v in sql_rows(sql('fishing_bait'), 'fishing_bait')}
        affinity = defaultdict(list)
        for v in sql_rows(sql('fishing_bait_affinity'), 'fishing_bait_affinity'):
            affinity[int(v[1])].append((int(v[2]), baits.get(int(v[0]), str(v[0]))))
        fish = []
        for v in sql_rows(sql('fishing_fish'), 'fishing_fish'):
            fish_id = int(v[0])
            if v[-1] == '1' or not where.get(fish_id):
                continue  # disabled, or only in zones this era doesn't open
            best = [name for _, name in sorted(affinity[fish_id], reverse=True)[:3]]
            fish.append({'id': fish_id, 'name': unquote(v[1]), 'skill': int(v[2]), 'legendary': v[18] == '1',
                         'item': v[20] == '1', 'quest': v[-4] == '1', 'base': self.sell_price(fish_id),
                         'where': sorted(where[fish_id]), 'baits': best})
        fish.sort(key=lambda f: (f['skill'], f['name']))
        return fish

    # ---- output -------------------------------------------------------------------------------
    def build(self, out_dir):
        recipes, skipped = self.recipes()
        tests = self.rank_tests()
        supply, suppliers = self.general_guild()
        sources = [self.regular_shops(), supply, self.regional(), self.guild_shops()]

        needed = set(range(4096, 4104))
        results = set()
        for r in recipes:
            needed.update(i for i, _ in r['ingredients'])
            results.add(r['result'])

        lines = ['-- Generated by tools/build_crafts.py from PhoenixXI (live); do not hand-edit.',
                 '-- buy[item]  = NPC offers: kind = shop | guild-supply (rank-gated) | regional (nation must hold region) | guild-shop',
                 '-- fame = the fame area a shop prices by (SANDORIA, BASTOK, WINDURST, JEUNO, NORG, SELBINA_RABAO; absent = list price).',
                 '-- base[item] = base NPC sell price; the addon applies fame: base x (priceRank + 389) / 400.',
                 'local prices = { buy = {}, base = {} };']
        priced = 0
        rows = []
        for item in sorted(needed):
            offers = [o for src in sources for o in src.get(item, [])]
            if not offers:
                continue
            priced += 1
            parts = []
            for o in sorted(offers, key=lambda o: o['price'])[:8]:
                fields = [f"price = {o['price']}", f"npc = {lua_str(o['npc'])}", f"zone = {lua_str(o['zone'])}", f"kind = {lua_str(o['kind'])}"]
                for key in ('loc', 'craft', 'rank', 'region', 'nation', 'fame', 'hours', 'holiday'):
                    if o.get(key) not in (None, ''):
                        fields.append(f'{key} = {o[key] if isinstance(o[key], int) else lua_str(o[key])}')
                parts.append('{ ' + ', '.join(fields) + ' }')
            rows.append(f'prices.buy[{item}] = {{ ' + ', '.join(parts) + ' };')
        for item in sorted(results):
            price = self.sell_price(item)
            if price:
                rows.append(f'prices.base[{item}] = {price};')
        lines.extend(chunked(rows))
        lines.append('return prices;')
        open(os.path.join(out_dir, 'prices.lua'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')

        lines = ['-- Generated by tools/build_crafts.py from PhoenixXI (live); do not hand-edit.',
                 '-- skills: craft levels the recipe needs (the first listed is the craft it is filed under).',
                 'local crafts = { order = { ' + ', '.join(lua_str(c) for c in CRAFTS) + ' }, ranks = {}, suppliers = {}, recipes = {} };']
        for craft in CRAFTS:
            lines.append(f'crafts.ranks.{craft} = {{')
            for t in tests.get(craft, []):
                lines.append(f"    {{ rank = {t['rank']}, name = {lua_str(t['name'])}, skill = {t['skill']}, item = {t['item'] or 'nil'} }},")
            lines.append('};')
            sup = ', '.join(f'{{ npc = {lua_str(n)}, zone = {lua_str(z)} }}' for n, z in suppliers.get(craft, []))
            lines.append(f'crafts.suppliers.{craft} = {{ {sup} }};')
        lines.append('local r = crafts.recipes;')
        rows = []
        for rec in sorted(recipes, key=lambda x: x['id']):
            skills = ', '.join(f'{k} = {v}' for k, v in rec['skills'].items())
            ingr = ', '.join(f'{{ {i}, {q} }}' for i, q in rec['ingredients'])
            extra = f", keyItem = {rec['keyItem']}" if rec['keyItem'] else ''
            rows.append(f"r[#r + 1] = {{ id = {rec['id']}, name = {lua_str(rec['name'])}, skills = {{ {skills} }}, crystal = {rec['crystal']}, "
                         f"ingredients = {{ {ingr} }}, result = {rec['result']}, qty = {rec['qty']}{extra} }};")
        lines.extend(chunked(rows))
        lines.append('return crafts;')
        open(os.path.join(out_dir, 'crafts.lua'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')

        fish = self.fishing()
        lines = ['-- Generated by tools/build_crafts.py from PhoenixXI (live) fishing tables; do not hand-edit.',
                 '-- base = base NPC sell price before fame (nil: NPCs will not buy it). baits = strongest affinity first.',
                 'local f = {};']
        rows = []
        for x in fish:
            flags = ''.join([', legendary = true' if x['legendary'] else '', ', notFish = true' if x['item'] else '',
                             ', quest = true' if x['quest'] else ''])
            rows.append(f"f[#f + 1] = {{ id = {x['id']}, name = {lua_str(x['name'])}, skill = {x['skill']}, base = {x['base'] or 'nil'}, "
                         f"where = {{ {', '.join(lua_str(w) for w in x['where'])} }}, baits = {{ {', '.join(lua_str(b) for b in x['baits'])} }}{flags} }};")
        lines.extend(chunked(rows, 60))
        lines.append('return f;')
        open(os.path.join(out_dir, 'fishing.lua'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')

        per_craft = defaultdict(int)
        for rec in recipes:
            for c in rec['skills']:
                per_craft[c] += 1
        print(f'recipes: {len(recipes)} ({skipped} skipped for era)  ' + '  '.join(f'{c[:4]}:{per_craft[c]}' for c in CRAFTS))
        print(f'ingredients/crystals priced: {priced}/{len(needed)}  sources: shops {sum(map(len, sources[0].values()))}, '
              f'guild supply {sum(map(len, supply.values()))}, regional {sum(map(len, sources[2].values()))}, guild shops {sum(map(len, sources[3].values()))}')
        print(f'rank tests: ' + ', '.join(f'{c[:4]}:{len(tests.get(c, []))}' for c in CRAFTS))
        print(f'fish: {len(fish)}  with an NPC price: {sum(1 for x in fish if x["base"])}')
        print(f'shops without a fame area match: {len(self.fame_misses)}  ' + ', '.join(sorted(self.fame_misses)[:15]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--server', required=True)
    ap.add_argument('--shops', required=True, help="VanaCompass - Phoenix's data folder (for shops.lua)")
    ap.add_argument('--era', default='toau')
    args = ap.parse_args()
    out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')
    os.makedirs(out_dir, exist_ok=True)
    Builder(args.server, args.shops, args.era).build(out_dir)


if __name__ == '__main__':
    main()
