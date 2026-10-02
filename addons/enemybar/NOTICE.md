# Notice

enemybar for Ashita v4 combines code from two projects. As a whole it is distributed under
the GNU General Public License v3.0 (`LICENSE`). The enemybar2 portions also remain under
their original BSD 3-Clause terms, reproduced below.

## enemybar2 (Windower 4)

https://github.com/AkadenTK/enemybar2, by mmckee and akaden.

Ported to Ashita v4 by Spongeh, October 2026:

- the bar design and layout, with its textures (`assets/bar/`);
- target, sub-target, focus target and aggro-stack frames;
- claim-state name colors;
- the attention (aggro) tracking.

Changes from the original:

- every frame is its own movable window;
- target of target became its own bar;
- each bar has its own fill color, with an optional claim-state palette;
- crowd-control icons were replaced by XIUI's full buff/debuff tracking;
- the original's bugs were fixed (every packet treated as a battle message, debuff
  expiry never running, alpha lost on color changes, and others).

```
Copyright © 2015, Mike McKee
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
    * Neither the name of enemybar nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Mike McKee BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## XIUI (Ashita v4)

https://github.com/tirem/XIUI (commit 4cdb817), GPL-3.0. The files under `handlers/` and
`libs/`, and the status icons in `assets/status/XIView/`, are copied from XIUI. Each keeps
its own attribution header, including the debuff handler's credit to XITools by mousseng
and the status handler's credit to statustimers by Heals.

Changes, each marked `enemybar:` in the code:

- `handlers/debuffhandler.lua`: removed XIUI's `require('handlers.helpers')`.
- `handlers/actiontracker.lua`: replaced XIUI's `require('handlers.helpers')` with local
  definitions.
- `libs/imtext.lua`: error messages print `[enemybar]` instead of `[XIUI]`.

FINAL FANTASY XI is owned by Square Enix. This project is not affiliated with or endorsed
by Square Enix, Windower, Ashita, or the XIUI and enemybar2 authors.
