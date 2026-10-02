# PhoenixXI addons

Ashita v4 addons for the [Phoenix](https://phoenix-xi.com) FFXI server, made or ported by Spongeh. Each addon
is published here when it's finished and submitted to Phoenix staff for review. Only use an
addon once its exact version is listed as **Approved** below and on Phoenix's
[approved addons list](https://phoenix-xi.com/approved-addons).

## Approval by staff

| Addon | Version | Status | Submitted | Reviewed by | Notes |
|---|---|---|---|---|---|
| [npcgil-phx](addons/npcgil_phx) | 2.2.1 | Pending review | 2026-10-02 | | Display only, sends nothing |
| [craftguide](addons/craftguide) | 1.3.2 | Pending review | 2026-10-02 | | Display only, sends nothing. Replaces 1.3.1, which was never reviewed. |
| [TradeNPC](addons/tradenpc) | 1.20.09.02-ashita.1 | Pending review | 2026-10-02 | | Sends the trade packet (0x036), only on command |

Phoenix's rules treat any change to an addon's script as a new, unreviewed version. So every
update gets a new version number and a new row here, and each addon's README lists a SHA-256 for
every file in that exact version.

## Install

Copy an addon's folder from `addons/` into `<Ashita>/addons/`, then `/addon load <name>`.

## For reviewers

Each addon's README starts with its own approval header, then a **For reviewers** section. That
section lists exactly which packets and game data it reads, what it writes, and what it does not
do (outgoing packets, commands, network access), plus the file hashes.

## How this repository works

1. An addon is finished and tested elsewhere, then copied into `addons/<name>/`.
2. Its version is added to the table above as **Pending review**, and the submission is tagged
   `<name>-v<version>`.
3. When staff respond, the row is updated: **Approved**, **Approved with conditions** (the
   conditions go in Notes) or **Not approved**.

## License

GPL-3.0 (see `LICENSE`) unless an addon's folder has its own license. TradeNPC keeps Ivaar's
BSD 3-Clause license. Price tables are generated from PhoenixXI's GPL-3.0 server repository.
