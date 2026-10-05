#!/usr/bin/env python3
"""
Builds questtracker's quest steps from a PhoenixXI (LandSandBoat) server checkout.

    pip install pyyaml lupa
    python tools/build_quests.py --server <phoenix checkout> [--era toau]

Writes data/quests.lua:

    [log * 1000 + id] = {
        log, id, key, name, repeatable,
        start = { npc, zone, zoneId, grid },          -- who gives the quest
        steps = {
            { text, npc, zone, zoneId, grid,           -- what to do next, and where
              ev = { { zone, csid }, ... },            -- events that finish this step
              ki = keyItemId },                        -- or: holding this key item means it's done
            ...
        },
        rewards = { gil, items, keyItems, title },
    }

Quest names, logs, start NPCs and era filtering come from quest_catalog.py (the generator written for
VanaCompass - Phoenix). Steps come from the interaction-framework scripts, scripts/quests/**.lua,
loaded under LuaJIT with stubbed Quest classes:

  - The server tracks progress in a quest var (usually 'Prog'). Every place a script sets it,
    quest:setVar(player, 'Prog', N), is a step boundary: in onEventFinish[csid] (the step is "trigger
    that event at its NPC"), in an NPC's onTrigger (e.g. examining a ??? after defeating an NM, often
    handing a key item), or in a mob's onMobDeath ("defeat the mob").
  - The NPC for an event is the section key whose handler starts it; the zone is the [xi.zone.X]
    key around it. Trades, key items given (giveKeyItem / quest:keyItem / quest.reward), NMs spawned
    (SpawnMob in the handler, mobs named in the zone table) are added to the step text.
  - Steps are ordered by the progress value they set; the event whose finish calls quest:complete()
    is the last step. A quest without a progress var gets one step: its completing event.

The addon advances a step when it sees one of the step's events (zone + cutscene id in the 0x032 /
0x034 event packets) or when you hold the step's key item. data/overrides.lua replaces the wording
of selected steps by hand.
"""

import argparse
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import quest_catalog as qc
import grid as gridmod

ADDON_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SET_RE = re.compile(r"(?:quest|mission|\w+):setVar\(\s*player\s*,\s*['\"](\w+)['\"]\s*,\s*(\d+)\s*\)")
GET_RE = re.compile(r"(?:quest|mission|\w+):getVar\(\s*player\s*,\s*['\"](\w+)['\"]\s*\)")
ALIAS_RE = re.compile(r"local\s+(\w+)\s*=\s*(?:quest|mission|\w+):getVar\(\s*player\s*,\s*['\"](\w+)['\"]\s*\)")
CMP_RE = re.compile(r"(?:(?:quest|mission|\w+):getVar\(\s*player\s*,\s*['\"](\w+)['\"]\s*\)|vars\.(\w+)|\b([a-zA-Z_]\w*))"
                    r"\s*(==|>=|<=|~=|<|>)\s*(\d+)")
KI_RE = re.compile(r"(?:giveKeyItem\(\s*player\s*,|addKeyItem\(|quest:keyItem\(|messageSpecial\([^)]*KEYITEM_OBTAINED[^)]*,)\s*xi\.keyItem\.([A-Z0-9_]+)")
COMPLETE_RE = re.compile(r"\bquest:complete\(|\bnpcUtil\.completeQuest\(")
SPAWN_RE = re.compile(r"\bSpawnMob\(|\bspawnMob\(|npcUtil\.popFromQM\(")
TRADE_RE = re.compile(r"npcUtil\.tradeHas(?:Exactly)?\s*\(\s*trade\s*,\s*")
ITEM_TOKEN = r"(?:xi\.item\.[A-Z0-9_]+|\d+)"
PAIR_RE = re.compile(r"\{\s*(" + ITEM_TOKEN + r")\s*,\s*(\d{1,2})\s*\}")
SKIP_VARS = {'Timer', 'Wait', 'Day', 'Time', 'Option', 'Choice', 'Prompt', 'Local'}
# NPC names that are objects you examine rather than people you talk to
OBJECT_LIST = 'gate|door|ladder|monument|tablet|chest|coffer|casket|book|shelf|altar|statue|stone|torch|lever|tombstone|crystal|grave|well|pot|cauldron|barrel|crate|box|rock|tree|flowers?|planter|cushion|mural|pedestal|plate|tower|cermet|porter|marker|sign|signpost|dais|throne|hourglass|chain|lamp|bed|ornament|platform|pillar|seal|stump|cradle|machine|mechanism|device|elevator|lift|portal|basin|bookshelf|tomb|coffin'
OBJECT_WORDS = re.compile(r'(?<![a-z])(?:' + OBJECT_LIST + r')(?![a-z])', re.I)


def humanize(key):
    small = {'a', 'an', 'and', 'as', 'at', 'by', 'for', 'from', 'in', 'of', 'on', 'or', 'the', 'to', 'with'}
    words = key.replace('-', '_').lower().split('_')
    return ' '.join(w if (i and w in small) else w.capitalize() for i, w in enumerate(words) if w)


def balanced(text, start):
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


class Extractor:
    def __init__(self, server, era):
        self.gen = qc.Generator(server, era)
        self.lua = self.gen.lua
        self.kind = self.lua.eval('kind')
        self.grids = gridmod.GridCalibrator(Path(os.path.dirname(os.path.abspath(__file__))))
        xi = self.gen.g.xi
        self.ki_enum = {k: int(v) for k, v in xi.keyItem.items() if isinstance(v, (int, float))}
        self.item_names = self.gen.item_names
        self.item_enum = self.gen.item_enum
        self.stats = defaultdict(int)

    # ---- small helpers ---------------------------------------------------------------------------
    def src(self, fn):
        return self.gen.fn_source(fn)[0] if self.kind(fn) == 'function' else ''

    def item_name(self, token):
        iid = self.item_enum.get(token[8:]) if token.startswith('xi.item.') else int(token)
        if iid is None:
            return humanize(token[8:])
        raw = self.item_names.get(iid)
        return humanize(raw) if raw else f'item {iid}'

    def item_id(self, token):
        return self.item_enum.get(token[8:]) if token.startswith('xi.item.') else int(token)

    def trade_items(self, text):
        """Items a handler's trade checks ask for: [(item id, qty, name), ...]."""
        found = []
        for m in TRADE_RE.finditer(text):
            arg = balanced(text, m.end())
            arg = re.sub(r"\{\s*['\"]gil['\"]\s*,\s*(\d+)\s*\}", r'', arg)
            counts = defaultdict(int)
            for tok, n in PAIR_RE.findall(arg):
                counts[tok] += int(n)
            rest = PAIR_RE.sub('', arg)
            rest = re.sub(r"\[[^\]]*\]", '', re.sub(r"\{[^{}]*\}", '', rest))
            for tok in re.findall(r"xi\.item\.[A-Z0-9_]+", rest):
                counts[tok] += 1
            found += list(counts.items())
        for m in re.finditer(r"hasItemQty\(\s*(" + ITEM_TOKEN + r")\s*,\s*(\d+)\s*\)", text):
            found.append((m.group(1), int(m.group(2))))
        out, seen = [], set()
        for tok, n in found:
            iid = self.item_id(tok)
            # a bare number under 640 is a quantity or index, not an item id
            if iid is None or iid in seen or (not str(tok).startswith('xi.') and iid < 640):
                continue
            seen.add(iid)
            out.append((iid, n, self.item_name(tok)))
        return out

    def need_kis(self, cond, fsrc, gives):
        """Key items a step needs: ones its condition checks you hold, and ones its cutscene takes
        away (delKeyItem), minus any it gives you."""
        out = []
        for m in re.finditer(r"(not\s+)?\w+:hasKeyItem\(\s*xi\.keyItem\.([A-Z0-9_]+)\s*\)", cond or ''):
            if not m.group(1):
                out.append(m.group(2))
        out += re.findall(r"delKeyItem\(\s*(?:player\s*,\s*)?xi\.keyItem\.([A-Z0-9_]+)", fsrc or '')
        res, given = [], {g[0] for g in gives}
        for k in out:
            kid = self.ki_enum.get(k)
            if kid is not None and kid not in given and kid not in [r[0] for r in res]:
                res.append((kid, humanize(k)))
        return res

    def trades(self, text):
        """Items a handler's trade checks ask for: ['3 Rabbit Hide', ...]."""
        out = []
        for m in TRADE_RE.finditer(text):
            arg = balanced(text, m.end())
            arg = re.sub(r"\{\s*['\"]gil['\"]\s*,\s*(\d+)\s*\}", r'', arg)
            counts = defaultdict(int)
            for tok, n in PAIR_RE.findall(arg):
                counts[tok] += int(n)
            rest = PAIR_RE.sub('', arg)
            rest = re.sub(r"\[[^\]]*\]", '', re.sub(r"\{[^{}]*\}", '', rest))
            for tok in re.findall(r"xi\.item\.[A-Z0-9_]+", rest):
                counts[tok] += 1
            for tok, n in counts.items():
                out.append((f'{n} ' if n > 1 else '') + self.item_name(tok))
        for m in re.finditer(r"hasItemQty\(\s*(" + ITEM_TOKEN + r")\s*,\s*(\d+)\s*\)", text):
            n = int(m.group(2))
            out.append((f'{n} ' if n > 1 else '') + self.item_name(m.group(1)))
        seen, res = set(), []
        for t in out:
            if t not in seen:
                seen.add(t)
                res.append(t)
        return res

    def kis(self, text):
        res = []
        for k in KI_RE.findall(text):
            if k in self.ki_enum and self.ki_enum[k] not in [x[0] for x in res]:
                res.append((self.ki_enum[k], humanize(k)))
        return res

    def where(self, zid, npc_key):
        """(display npc, zone name, grid)"""
        start = self.gen.make_start(zid, npc_key, 'step') if npc_key else {'zone': self.gen.zone_name.get(zid, str(zid))}
        g = None
        if 'x' in start:
            g = gridmod.grid_label(self.grids.grid(zid, start['x'], start['y'], start['z']))
        npc = start.get('npc')
        if npc_key and npc_key.lower().startswith('qm'):
            npc = '???'
        return npc, start['zone'], g

    # ---- condition analysis ----------------------------------------------------------------------
    @staticmethod
    def conditions_at(src, pos):
        """Texts of the if / elseif conditions enclosing position pos in a function's source."""
        lines = src[:pos].split('\n')
        cur_indent = len(lines[-1]) - len(lines[-1].lstrip())
        conds = []
        for i in range(len(lines) - 2, -1, -1):
            line = lines[i]
            st = line.strip()
            if not st:
                continue
            ind = len(line) - len(line.lstrip())
            if ind < cur_indent and re.match(r'(if|elseif)\b', st):
                # gather the condition up to 'then' (may span lines)
                text = st
                j = i
                full = src[:pos].split('\n')
                while 'then' not in text and j + 1 < len(full):
                    j += 1
                    text += ' ' + full[j].strip()
                conds.append(text.split('then')[0])
                cur_indent = ind
            elif ind < cur_indent and re.match(r'(function|local function)\b', st):
                break
            elif ind < cur_indent and not st.startswith(('else', 'end', 'or', 'and')):
                cur_indent = ind
        return ' and '.join(conds)

    @staticmethod
    def constraint(text, var, aliases):
        """Set of progress values allowed by a condition text, or None if it doesn't mention var."""
        lo, hi, eq, ne = None, None, set(), set()
        hit = False
        for m in CMP_RE.finditer(text):
            name = m.group(1) or m.group(2) or aliases.get(m.group(3) or '', None)
            if name != var:
                continue
            hit = True
            op, n = m.group(4), int(m.group(5))
            if op == '==':
                eq.add(n)
            elif op == '~=':
                ne.add(n)
            elif op == '>=':
                lo = n if lo is None else max(lo, n)
            elif op == '>':
                lo = n + 1 if lo is None else max(lo, n + 1)
            elif op == '<=':
                hi = n if hi is None else min(hi, n)
            elif op == '<':
                hi = n - 1 if hi is None else min(hi, n - 1)
        if not hit:
            return None
        if eq:
            vals = set(eq)
        else:
            vals = set(range(lo if lo is not None else 0, (hi if hi is not None else (lo or 0) + 1) + 1))
        return vals - ne

    # ---- per-quest extraction --------------------------------------------------------------------
    def handlers(self, v):
        """[(kind, value)] for an NPC entry: ('action', (kind, csid)) or ('fn', source) or ('mob', source)."""
        out = []
        t = self.kind(v)
        if t == 'table':
            kind, first = self.gen.g.actionInfo(v)
            if kind:
                out.append(('action', (kind, first)))
                return out
            for hk, hv in v.items():
                if hk in ('onTrigger', 'onTrade'):
                    out += [(k, (x, hk)) if k == 'fn' else (k, x) for k, x in self.handlers(hv)]
                elif hk in ('onMobDeath', 'onMobDisengage'):
                    s = self.src(hv)
                    if s:
                        out.append(('mob', s))
        elif t == 'function':
            out.append(('fn', self.src(v)))
        return out

    def extract(self, path, obj):
        secs = obj.sections
        if self.kind(secs) != 'table':
            return None
        text = self.gen.text(path)
        # progress var: the one set most often (ignoring timers / choices)
        counts = defaultdict(int)
        for name, _ in SET_RE.findall(text):
            if name not in SKIP_VARS:
                counts[name] += 1
        var = max(counts, key=lambda k: (counts[k], k == 'Prog')) if counts else None

        transitions = []   # dicts: to (int|'done'), frm (set|None), zone, npc_key, csid, kind, trades, kis, mobs, spawns
        begin_sets = []
        for idx in sorted(k for k in secs.keys() if isinstance(k, int)):
            sec = secs[idx]
            if self.kind(sec) != 'table':
                continue
            check = self.src(sec.check)
            begin_phase = 'QUEST_AVAILABLE' in check and 'QUEST_ACCEPTED' not in check and '~=' not in check
            after_phase = 'QUEST_COMPLETED' in check and 'QUEST_ACCEPTED' not in check
            if after_phase:
                continue
            sec_vals = self.constraint(check, var, {}) if var else None
            for zk, ztab in sec.items():
                if not isinstance(zk, int) or self.kind(ztab) != 'table':
                    continue
                zid = int(zk)
                finish = {}
                for hk in ('onEventFinish', 'onEventUpdate'):
                    fin = ztab[hk]
                    if self.kind(fin) == 'table':
                        for csid, fn in fin.items():
                            if isinstance(csid, (int, float)) and hk == 'onEventFinish':
                                finish[int(csid)] = self.src(fn)
                mob_names = [k for k, vv in ztab.items() if isinstance(k, str) and self.kind(vv) == 'table'
                             and any(h[0] == 'mob' for h in self.handlers(vv))]
                # events started by each NPC, with the condition they're started under
                for nk, nv in ztab.items():
                    if not isinstance(nk, str) or nk in ('onEventFinish', 'onEventUpdate'):
                        continue
                    zone_handler = bool(re.match(r'^(on|after)[A-Z]', nk))
                    entries = self.handlers(nv) if not zone_handler else [('fn', (self.src(nv), nk))]
                    for kind, val in entries:
                        if kind == 'action':
                            akind, csid = val
                            if csid is None:
                                continue
                            transitions.append(self._event_step(zid, nk, int(csid), finish, sec_vals, '', [], var,
                                                                begin_phase, akind))
                        elif kind == 'fn':
                            src, hk = val if isinstance(val, tuple) else (val, 'onTrigger')
                            aliases = {a: b for a, b in ALIAS_RE.findall(src)}
                            for m in qc.EVENT_RE.finditer(src):
                                csid = int(m.group(2) or m.group(3))
                                cond = self.conditions_at(src, m.start())
                                vals = self.constraint(cond, var, aliases) if var else None
                                if vals is None:
                                    vals = sec_vals
                                trades = self.trades(cond) if hk == 'onTrade' or 'trade' in cond else []
                                step = self._event_step(zid, nk, csid, finish, vals, cond, trades, var,
                                                        begin_phase, m.group(1) or 'startEvent',
                                                        zone_handler=nk if zone_handler else None)
                                step['need'] = self.trade_items(cond) if trades else []
                                step['needKi'] = self.need_kis(cond, finish.get(csid, ''), step['kis'])
                                transitions.append(step)
                            # direct progress changes in the handler (no event): examine a ??? etc.
                            for m in SET_RE.finditer(src):
                                if m.group(1) != var:
                                    continue
                                cond = self.conditions_at(src, m.start())
                                vals = self.constraint(cond, var, aliases)
                                block = src[m.start():m.start() + 600]
                                kis = self.kis(block)
                                spawn = bool(SPAWN_RE.search(src))
                                need_ki = self.need_kis(cond, block, kis)
                                transitions.append({
                                    'to': int(m.group(2)), 'frm': vals if vals is not None else sec_vals,
                                    'zone': zid, 'npc_key': None if zone_handler else nk,
                                    'zone_handler': nk if zone_handler else None, 'csid': None, 'kind': 'direct',
                                    'trades': [], 'kis': kis, 'mobs': mob_names if spawn else [],
                                    'done': False, 'begin': begin_phase, 'needKi': need_ki,
                                })
                        elif kind == 'mob':
                            for m in SET_RE.finditer(val):
                                if m.group(1) == var:
                                    transitions.append({
                                        'to': int(m.group(2)), 'frm': sec_vals, 'zone': zid, 'npc_key': None,
                                        'csid': None, 'kind': 'kill', 'mobs': [nk], 'trades': [], 'kis': [],
                                        'done': False, 'begin': begin_phase,
                                    })
        return var, [t for t in transitions if t]

    def _event_step(self, zid, nk, csid, finish, vals, cond, trades, var, begin_phase, akind, zone_handler=None):
        fsrc = finish.get(csid, '')
        to = None
        if var:
            sets = [int(n) for name, n in SET_RE.findall(fsrc) if name == var]
            to = max(sets) if sets else None
        done = bool(COMPLETE_RE.search(fsrc))
        return {'to': to, 'frm': vals, 'zone': zid, 'npc_key': None if zone_handler else nk, 'zone_handler': zone_handler,
                'csid': csid, 'kind': 'event', 'trades': trades, 'kis': self.kis(fsrc), 'mobs': [],
                'done': done, 'begin': begin_phase or bool(re.search(r'\bquest:begin\(', fsrc)),
                'progress': akind in qc.PROGRESS_KINDS}

    # ---- steps -------------------------------------------------------------------------------------
    def step_text(self, t, npc, zone, g):
        at = f' ({g})' if g else ''
        if t['kind'] == 'kill':
            return f"Defeat {', '.join(humanize(m) for m in t['mobs'])} in {zone}."
        if t.get('zone_handler'):
            h = t['zone_handler']
            if h in ('onZoneIn', 'afterZoneIn'):
                return f'Enter {zone}.'
            if h == 'onZoneOut':
                return f'Leave {zone}.'
            return f'Go to the marked spot in {zone}.'
        if t['kind'] == 'direct':
            parts = []
            if t['mobs']:
                parts.append(f"Examine the {npc or 'spot'} in {zone}{at} to make "
                             f"{', '.join(humanize(m) for m in t['mobs'])} appear, defeat "
                             f"{'them' if len(t['mobs']) > 1 or t['mobs'][0].endswith('s') else 'it'}, then examine it again.")
            else:
                parts.append(f"Examine the {npc or 'spot'} in {zone}{at}.")
            for _, name in t['kis']:
                parts.append(f'You receive the key item {name}.')
            return ' '.join(parts)
        obj = npc == '???' or bool(npc and OBJECT_WORDS.search(npc))
        who = (f'the {npc}' if obj else npc) or 'someone'
        verb = f"Trade {', '.join(t['trades'])} to" if t['trades'] else ('Examine' if obj else 'Talk to')
        s = f'{verb} {who} in {zone}{at}.'
        for _, name in t['kis']:
            s += f' You receive the key item {name}.'
        return s

    def steps(self, var, transitions):
        work = [t for t in transitions if not t['begin'] or t['done']]
        if var:
            advancing = [t for t in work if t['done'] or (t['to'] is not None and t.get('progress', True)
                                                           and (t['frm'] is None or t['to'] not in t['frm']))]
        else:
            advancing = [t for t in work if t['done']]
        if not advancing:
            return []
        groups = defaultdict(list)
        for t in advancing:
            key = 10 ** 6 if t['done'] else t['to']
            groups[key].append(t)
        out = []
        for key in sorted(groups):
            ts = groups[key]
            texts, ev, ki, first, need, need_ki = [], [], None, None, None, None
            for t in ts:
                npc, zone, g = self.where(t['zone'], t['npc_key'])
                text = self.step_text(t, npc, zone, g)
                if text not in texts:
                    texts.append(text)
                if t['csid'] is not None and [t['zone'], t['csid']] not in ev:
                    ev.append([t['zone'], t['csid']])
                if t['kis'] and ki is None:
                    ki = t['kis'][0][0]
                if first is None:
                    first = (npc, zone, t['zone'], g)
                if need is None and t.get('need'):
                    need = t['need']
                if need_ki is None and t.get('needKi'):
                    need_ki = t['needKi']
            step = {'text': texts[0] if len(texts) == 1 else ' Or: '.join(texts)}
            if first:
                if first[0]:
                    step['npc'] = first[0]
                step['zone'], step['zoneId'] = first[1], first[2]
                if first[3]:
                    step['grid'] = first[3]
            if ev:
                step['ev'] = ev
            if ki:
                step['ki'] = ki
            if need:
                step['need'] = [[iid, n, name] for iid, n, name in need]
            if need_ki:
                step['needKi'] = [[kid, name] for kid, name in need_ki]
            if key == 10 ** 6:
                step['last'] = True
            out.append(step)
        return out


def lua_str(s):
    return "'" + str(s).replace('\\', '\\\\').replace("'", "\\'") + "'"


def lua_val(v, ind=0):
    pad = '    ' * ind
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        return lua_str(v)
    if isinstance(v, list):
        if all(not isinstance(x, (dict, list)) for x in v):
            return '{ ' + ', '.join(lua_val(x) for x in v) + ' }'
        return '{\n' + ''.join(f'{pad}    {lua_val(x, ind + 1)},\n' for x in v) + pad + '}'
    if isinstance(v, dict):
        items = [f'{k} = {lua_val(x, ind + 1)}' for k, x in v.items() if x is not None]
        flat = '{ ' + ', '.join(items) + ' }'
        if len(flat) < 110 and '\n' not in flat:
            return flat
        return '{\n' + ''.join(f'{pad}    {it},\n' for it in items) + pad + '}'
    return 'nil'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('--server', required=True)
    ap.add_argument('--era', default='toau')
    args = ap.parse_args()

    ex = Extractor(args.server, args.era)
    logs, quests, _, _, _ = ex.gen.build_quests()
    catalog = {(q['log'], q['id']): q for q in quests}
    scripts = {}
    for path, obj in ex.gen.run_scripts('scripts/quests'):
        if obj['__kind'] == 'quest' and isinstance(obj.areaId, (int, float)) and isinstance(obj.questId, (int, float)):
            scripts.setdefault((int(obj.areaId), int(obj.questId)), (path, obj))

    rows, with_steps, multi = [], 0, 0
    for (log, qid), q in sorted(catalog.items()):
        row = {'log': log, 'id': qid, 'key': q['key'], 'name': q['name']}
        if q.get('repeatable'):
            row['repeatable'] = True
        st = q.get('start')
        if st and st.get('npc'):
            g = gridmod.grid_label(ex.grids.grid(st['zoneId'], st['x'], st['y'], st['z'])) if 'x' in st else None
            row['start'] = {'npc': st['npc'], 'zone': st['zone'], 'zoneId': st['zoneId'], 'grid': g}
        if (log, qid) in scripts:
            path, obj = scripts[(log, qid)]
            try:
                var, trans = ex.extract(path, obj)
                steps = ex.steps(var, trans)
            except Exception as e:  # keep going; report
                ex.stats['errors'] += 1
                print(f'  ! {q["key"]}: {e}')
                steps = []
            if steps:
                row['steps'] = steps
                with_steps += 1
                if len(steps) > 1:
                    multi += 1
        r = q.get('rewards') or {}
        rew = {k: r[k] for k in ('gil', 'items', 'keyItems', 'title') if k in r}
        if rew.get('items'):
            rew['items'] = [humanize(ex.item_names.get(i, str(i))) for i in rew['items']]
        if rew.get('keyItems'):
            names = {v: humanize(k) for k, v in ex.ki_enum.items()}
            rew['keyItems'] = [names.get(i, str(i)) for i in rew['keyItems']]
        if isinstance(rew.get('title'), (int, float)):
            rew.pop('title')
        if rew:
            row['rewards'] = rew
        rows.append(row)

    commit = qc.git_commit(args.server)
    out = os.path.join(ADDON_DIR, 'data', 'quests.lua')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('-- Quest steps for questtracker. Generated by tools/build_quests.py from the PhoenixXI (LandSandBoat,\n')
        f.write(f'-- GPL-3.0) server source{" (commit " + commit + ")" if commit else ""}, era {args.era}. Do not edit by hand;\n')
        f.write('-- reword steps in data/overrides.lua instead.\nreturn {\n')
        for row in rows:
            f.write(f"    [{row['log'] * 1000 + row['id']}] = {lua_val(row, 1)},\n")
        f.write('}\n')
    print(f'{len(rows)} quests, {with_steps} with steps ({multi} multi-step), errors {ex.stats["errors"]} -> {out}')


if __name__ == '__main__':
    main()
