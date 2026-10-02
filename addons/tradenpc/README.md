# TradeNPC (Ashita v4)

## Approval by staff

| Version | Status | Submitted | Reviewed by | Notes |
|---|---|---|---|---|
| 1.20.09.02-ashita.1 | Pending review | 2026-10-02 | | Sends the trade packet (0x036) |

**Author:** Ivaar (Windower original); Ashita port by Spongeh. **Program:** Ashita v4.
**Type:** trade helper. **This addon sends one outgoing packet**, the trade request (0x036), and
only when you run its command.

---

Trade items and gil to an NPC with one command instead of the trade menu. This is a port of
Ivaar's Windower 4 TradeNPC (https://github.com/Ivaar/Windower-addons, BSD 3-Clause; see
`LICENSE`).

```
/addon load tradenpc
/tradenpc 3 "ear of millioncorn" Melyon
/tradenpc 100 "1 byne bill"            (trades to your current target)
/tradenpc 5000 gil 1 "beastmen's seal"
```

- **Items:** use the item's name or its log name, in quotes if it has spaces. Auto-translate
  phrases work too. Up to 8 item stacks, plus gil.
- **Quantities:** larger amounts are split across slots, fullest stacks first. Partial stacks
  are fine. Equipped and bazaared items are skipped.
- **Target:** your current target, or name the NPC as the last argument. It must be an NPC within
  6 yalms, and you must be standing (not engaged, resting or in an event).
- **First use:** builds an item-name index, which takes a moment.

## For reviewers

### What it sends

**One packet: client 0x036, the trade request.** It is sent only when you type `/tradenpc`,
once per command. Nothing is sent on its own, on a timer or in a loop.

The packet is the same one the game sends when you confirm a trade in the trade window, built to
the same layout:

| Offset | Field |
|---|---|
| 0x04 | NPC server id |
| 0x08 | item counts (10 x u32; slot 1 is gil when trading gil) |
| 0x30 | inventory slot indexes (10 x u8) |
| 0x3A | NPC target index |
| 0x3C | number of slots used |

**Checks before sending.** If any check fails, nothing is sent:
- the target must be an NPC (spawn type), targetable, and within 6 yalms;
- your character must have status 0 (standing: not engaged, resting, in an event, ...);
- every item must exist in your main inventory in that quantity, unequipped and not in your
  bazaar;
- gil can't exceed what you carry;
- at most 8 item stacks (plus gil);
- linkshell items are refused.

It can't trade to players, can't trade from other bags, and doesn't move, buy, sell or drop
anything.

### What it reads

- **Your main inventory**, through Ashita's API: item ids, counts and flags, and your gil.
- **Entities:** the NPC's name, server id, distance, spawn type and render flags. It searches
  the entity list by name only when you name an NPC.
- **Your current target index, and your own status.**
- **Item names** from the client's resource data. It scans these once to build the name lookup.
- **Auto-translate conversion** of the typed arguments, through Ashita's chat manager.

### What it writes

- **Chat:** messages to your own chat log only (confirmations and errors).
- **No files:** no settings file or other files.

### What it does NOT do

- **No other outgoing packets,** and no incoming packets read, modified or blocked.
- **No commands:** no `QueueCommand` or automated chat or actions.
- **No automation:** no timers, loops or repeat trading. Each trade is one command you type.
- **No network or file access.**

### Files in the reviewed version

| File | SHA-256 |
|---|---|
| `tradenpc.lua` | `308400b39409a65a249f7d5b5996b4e60c6215958e0774610a2291016085323c` |
| `LICENSE` | `0b0cb9c8db5682ef8a7834217f77a2f7239a3475a49e84932213601f903a7222` |
