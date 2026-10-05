"""Map grid (A-1 style) from a world position: craftguide's GridCalibrator, which mirrors
VanaCompass's currentGrid(). Reads grids.lua and grid_calibrations.lua next to this file."""

import math
import re
from pathlib import Path

try:
    import lupa.luajit21 as lupa
except ImportError:  # pragma: no cover
    import lupa


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
