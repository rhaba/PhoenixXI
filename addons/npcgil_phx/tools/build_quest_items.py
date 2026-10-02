"""
Builds npcgil-phx's quest turn-in table from a PhoenixXI (or other LandSandBoat) server checkout.

    data/quest_items.lua   [itemId] = { { quest=, npc=, zone=, qty=, gil=, repeatable= }, ... }

Usage:
    python build_quest_items.py <server checkout>

Where turn-ins are coded on the server:
  - Interaction-framework quests, scripts/quests/<log>/<Quest>.lua: an NPC's onTrade in
    quest.sections checks the trade with npcUtil.tradeHas / tradeHasExactly (or trade:hasItemQty).
    The NPC is the section key the onTrade sits under ['Parvipon'], the zone the [xi.zone.X] key
    above it. Gil: quest.reward.gil, or npcUtil.giveCurrency(player, 'gil', N) / player:addGil(N)
    in the file. Repeatable: the section's check also lets a completed quest through
    (status ~= QUEST_AVAILABLE, or tests QUEST_COMPLETED), so the trade works again afterwards.
  - Older quests, scripts/zones/<zone>/npcs/<Npc>.lua: a trade check inside onTrade, tied to the
    nearest xi.quest.id.<log>.<QUEST> reference in the lines leading up to it. Gil from the same
    file's addGil / giveCurrency / completeQuest { gil = N } calls.

Trade arguments understood: a single item, { item, item, ... } (repeats add up), { { item, n } },
and mixes; hasItemQty(item, n). Items are xi.item.NAME (scripts/enum/item.lua) or plain ids. Gil
and items held in variables are skipped.

Era: quests in the Crystal War, Abyssea, Adoulin and Coalition logs are left out (post-ToAU), and so
are NPC scripts in zones from those expansions (any quest they reference is in those logs).
"""

import os
import re
import sys
from collections import defaultdict

ADDON_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DROP_LOGS = {'crystalWar', 'abyssea', 'adoulin', 'coalition'}
ENUM_DROP_LOGS = {'CRYSTAL_WAR', 'ABYSSEA', 'ADOULIN', 'COALITION', 'crystalWar', 'abyssea', 'adoulin', 'coalition'}

ITEM_TOKEN = r"(?:xi\.item\.[A-Z0-9_]+|\d+)"
PAIR = re.compile(r"\{\s*(" + ITEM_TOKEN + r")\s*,\s*(\d{1,2})\s*\}")   # { item, qty } ({ 1837, 1826 } is two items)
SINGLE = re.compile(ITEM_TOKEN)
TRADE_CALL = re.compile(r"npcUtil\.tradeHas(?:Exactly)?\s*\(\s*trade\s*,\s*")
HAS_QTY = re.compile(r"hasItemQty\s*\(\s*(" + ITEM_TOKEN + r")\s*,\s*(\d+)\s*\)")
QUEST_REF = re.compile(r"xi\.quest\.id\.(\w+)\.([A-Z0-9_]+)")
GIL = [re.compile(r"giveCurrency\s*\(\s*player\s*,\s*'gil'\s*,\s*(\d+)"),
       re.compile(r"addGil\s*\(\s*(?:xi\.settings\.main\.GIL_RATE\s*\*\s*)?(\d+)"),
       re.compile(r"\bgil\s*=\s*(\d+)")]

ZONE_NAME_FIX = {"dOria": "d'Oria", "RoMaeve": "Ro'Maeve", "RuAun": "Ru'Aun", "RuLude": "Ru'Lude",
                 "QuBia": "Qu'Bia", "RuHmet": "Ru'Hmet", "AlTaieu": "Al'Taieu", "BoDis": "Bo'Dis",
                 "dOraguille": "d'Oraguille", "FeiYin": "Fei'Yin", "Ifrits": "Ifrit's",
                 "Ranperres": "Ranperre's", "Carpenters": "Carpenters'", "Heavens": "Heavens"}


def read_items(root):
    ids = {}
    for line in open(os.path.join(root, 'scripts', 'enum', 'item.lua'), encoding='utf-8'):
        m = re.match(r"\s*([A-Z0-9_]+)\s*=\s*(\d+)\s*,", line)
        if m:
            ids[m.group(1)] = int(m.group(2))
    return ids


def zone_folders(root):
    """xi.zone key (upper case, no punctuation) -> display name from the scripts/zones folder."""
    out = {}
    for folder in os.listdir(os.path.join(root, 'scripts', 'zones')):
        name = folder
        for a, b in ZONE_NAME_FIX.items():
            name = name.replace(a, b)
        out[re.sub(r'[^A-Z0-9]', '', folder.upper())] = name.replace('_', ' ')
    return out


def zone_display(key, zones):
    return zones.get(re.sub(r'[^A-Z0-9]', '', key.upper()), key.replace('_', ' ').title())


def humanize(key):
    small = {'a', 'an', 'and', 'as', 'at', 'by', 'for', 'from', 'in', 'of', 'on', 'or', 'the', 'to', 'with'}
    words = key.lower().split('_')
    return ' '.join(w if (i and w in small) else w.capitalize() for i, w in enumerate(words))


def balanced(text, start):
    """Text of the argument list starting at `start` (just inside the open paren), up to the
    matching close paren."""
    depth, i = 0, start
    while i < len(text):
        c = text[i]
        if c in '({':
            depth += 1
        elif c in ')}':
            if depth == 0:
                return text[start:i]
            depth -= 1
        i += 1
    return text[start:]


def parse_trade_arg(arg, items):
    """{item id: qty} for one tradeHas argument; None if it holds nothing we can resolve."""
    arg = re.split(r",\s*(?:true|false)\s*$", arg.strip())[0]   # optional 3rd parameter
    if "'gil'" in arg or '"gil"' in arg:
        arg = re.sub(r"\{\s*['\"]gil['\"]\s*,\s*\d+\s*\}", '', arg)
    out = defaultdict(int)
    for tok, n in PAIR.findall(arg):
        iid = resolve(tok, items)
        if iid:
            out[iid] += int(n)
    rest = PAIR.sub('', arg)
    rest = re.sub(r"\{[^{}]*\}", '', rest)       # pairs naming a variable ({ itemToTrade, 1 })
    rest = re.sub(r"\[[^\]]*\]", '', rest)       # table indexing (options[i][2])
    for tok in SINGLE.findall(rest):
        if tok.isdigit() and int(tok) < 640:     # a quantity or index, not an item id
            continue
        iid = resolve(tok, items)
        if iid:
            out[iid] += 1
    return dict(out) or None


def resolve(tok, items):
    if tok.startswith('xi.item.'):
        return items.get(tok[8:])
    n = int(tok)
    return n if n > 0 and n != 65535 else None


def rejected(text, pos):
    """True when the check belongs to a statement collecting wrong items (local badtrade = ...)."""
    lines = text[:pos].split('\n')
    i = len(lines) - 1
    # walk back to the first line of the statement (continuations start or end with or/and)
    while i > 0 and (lines[i].strip().startswith(('or ', 'and ')) or
                     re.search(r'\b(or|and)\s*$', lines[i - 1].strip())):
        i -= 1
    return re.search(r'\b(bad|wrong|invalid)', lines[i], re.I) is not None


def trades_in(text, items):
    """[(position, {item: qty})] for every trade check in `text`."""
    found = []
    for m in TRADE_CALL.finditer(text):
        got = parse_trade_arg(balanced(text, m.end()), items)
        if got and not rejected(text, m.start()):
            found.append((m.start(), got))
    for m in HAS_QTY.finditer(text):
        if rejected(text, m.start()):
            continue
        iid = resolve(m.group(1), items)
        if iid:
            found.append((m.start(), {iid: int(m.group(2))}))
    return found


def gil_in(text):
    vals = [int(v) for rx in GIL for v in rx.findall(text)]
    vals = [v for v in vals if 0 < v < 1000000]
    return max(vals) if vals else None


def section_bounds(text, pos):
    """The quest.sections entry (from its `check =`) containing pos."""
    start = text.rfind('check = function', 0, pos)
    nxt = text.find('check = function', pos)
    return (start if start >= 0 else 0), (nxt if nxt >= 0 else len(text))


def framework_quests(root, items, zones, entries):
    base = os.path.join(root, 'scripts', 'quests')
    for log in sorted(os.listdir(base)):
        if log in DROP_LOGS or not os.path.isdir(os.path.join(base, log)):
            continue
        for fn in sorted(os.listdir(os.path.join(base, log))):
            if not fn.endswith('.lua'):
                continue
            text = open(os.path.join(base, log, fn), encoding='utf-8', errors='replace').read()
            head = re.search(r"^-+\s*\n--\s*(.+?)\s*\n", text)
            name = (head.group(1).strip() if head else humanize(fn[:-4].upper())).replace('_', ' ')
            reward = re.search(r"quest\.reward\s*=\s*\{(.*?)\n\s*\}", text, re.S)
            gil = None
            if reward:
                m = re.search(r"\bgil\s*=\s*(\d+)", reward.group(1))
                gil = int(m.group(1)) if m else None
            gil = gil or gil_in(text)
            for pos, got in trades_in(text, items):
                s0, s1 = section_bounds(text, pos)
                check = text[s0:text.find('end', s0) if text.find('end', s0) > 0 else s0 + 200]
                repeatable = ('QUEST_COMPLETED' in check) or ('~= xi.questStatus.QUEST_AVAILABLE' in check)
                before = text[s0:pos]
                npc = re.findall(r"\[\s*'([^']+)'\s*\]\s*=|^\s*([A-Z][A-Za-z_]+)\s*=\s*\{?\s*$", before, re.M)
                npc = next((a or b for a, b in reversed(npc) if (a or b) not in ('onEventFinish', 'onEventUpdate')), None)
                zone = re.findall(r"\[\s*xi\.zone\.([A-Z0-9_]+)\s*\]", before)
                for iid, qty in got.items():
                    entries[iid].append({'quest': name, 'npc': npc and npc.replace('_', ' ').strip(),
                                         'zone': zone_display(zone[-1], zones) if zone else None,
                                         'qty': qty, 'gil': gil, 'repeatable': repeatable})


def legacy_npcs(root, items, zones, entries, scripted):
    base = os.path.join(root, 'scripts', 'zones')
    for zone in sorted(os.listdir(base)):
        d = os.path.join(base, zone, 'npcs')
        if zone.endswith('_S') or not os.path.isdir(d):   # [S] zones are Crystal War (post-ToAU)
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith('.lua'):
                continue
            text = open(os.path.join(d, fn), encoding='utf-8', errors='replace').read()
            m = re.search(r"onTrade\s*=\s*function|function\s+\w*\.?onTrade", text)
            if not m:
                continue
            end = re.search(r"\n(?:entity|\w+)\.onTrigger|\nentity\.onEvent", text[m.end():])
            body = text[m.start(): m.end() + (end.start() if end else len(text))]
            gil = gil_in(text)
            for pos, got in trades_in(body, items):
                window = body[max(0, pos - 700): pos + 200]
                refs = QUEST_REF.findall(window)
                if not refs:
                    continue
                log, key = refs[-1] if pos - 700 < 0 or True else refs[-1]
                # prefer the reference closest to (at or before) the trade check
                before = QUEST_REF.findall(body[max(0, pos - 700): pos + 120])
                if before:
                    log, key = before[-1]
                if log in ENUM_DROP_LOGS or (log, key) in scripted:
                    continue
                for iid, qty in got.items():
                    entries[iid].append({'quest': humanize(key), 'npc': fn[:-4].replace('_', ' ').strip(),
                                         'zone': zone_display(zone, zones), 'qty': qty, 'gil': gil,
                                         'repeatable': None})


def scripted_keys(root):
    """(log, KEY) of every quest that has a framework script (its own trade data wins)."""
    out = set()
    base = os.path.join(root, 'scripts', 'quests')
    for log in os.listdir(base):
        p = os.path.join(base, log)
        if not os.path.isdir(p):
            continue
        for fn in os.listdir(p):
            if fn.endswith('.lua'):
                m = QUEST_REF.search(open(os.path.join(p, fn), encoding='utf-8', errors='replace').read())
                if m:
                    out.add(m.groups())
    return out


def lua_str(s):
    return "'" + s.replace('\\', '\\\\').replace("'", "\\'") + "'"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    root = sys.argv[1]
    items, zones = read_items(root), zone_folders(root)
    entries = defaultdict(list)
    framework_quests(root, items, zones, entries)
    legacy_npcs(root, items, zones, entries, scripted_keys(root))

    commit = ''
    try:
        commit = open(os.path.join(root, '.git', 'HEAD')).read().strip()
        if commit.startswith('ref:'):
            commit = open(os.path.join(root, '.git', commit[5:])).read().strip()
    except OSError:
        pass

    lines = ['-- Quest turn-ins: [itemId] = { { quest, npc, zone, qty, gil, repeatable }, ... }',
             '-- Built by tools/build_quest_items.py from the PhoenixXI server source (LandSandBoat, GPL-3.0)'
             + (f', commit {commit[:10]}' if commit else '') + '. Do not edit by hand.',
             'return {']
    count = 0
    for iid in sorted(entries):
        seen, rows = set(), []
        for e in entries[iid]:
            k = (e['quest'], e['npc'], e['qty'])
            if k in seen:
                continue
            seen.add(k)
            parts = [f"quest = {lua_str(e['quest'])}", f"qty = {e['qty']}"]
            if e['npc']:
                parts.append(f"npc = {lua_str(e['npc'])}")
            if e['zone']:
                parts.append(f"zone = {lua_str(e['zone'])}")
            if e['gil']:
                parts.append(f"gil = {e['gil']}")
            if e['repeatable']:
                parts.append('repeatable = true')
            rows.append('{ ' + ', '.join(parts) + ' }')
        count += len(rows)
        lines.append(f'    [{iid}] = {{ ' + ', '.join(rows) + ' },')
    lines.append('}')
    out = os.path.join(ADDON_DIR, 'data', 'quest_items.lua')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'{len(entries)} items, {count} turn-ins -> {out}')


if __name__ == '__main__':
    main()
