# PhoenixXI addons

Ashita v4 addons for the [Phoenix](https://phoenix-xi.com) FFXI server, made or ported by Spongeh. Each addon
is published here when it's finished and submitted to Phoenix staff for review. Only use an
addon once its exact version is listed as **Approved** below and on Phoenix's
[approved addons list](https://phoenix-xi.com/approved-addons).

**Addons that aren't approved are removed from this repository.** Only approved addons stay.

## Approval by staff

| Addon | Version | Status | Submitted | Reviewed by | Notes |
|---|---|---|---|---|---|
| [npcgil-phx](addons/npcgil_phx) | 2.3.0 | Approved 2026-10-05 | 2026-10-02 | Phoenix staff | Display only, sends nothing. |
| [craftguide](addons/craftguide) | 1.3.3 | Approved 2026-10-05 | 2026-10-02 | Phoenix staff | Display only, sends nothing. |
| [enemybar](addons/enemybar) | 1.5.1 | Approved 2026-10-05 | 2026-10-02 | Phoenix staff | Display only, sends nothing. Port of enemybar2; distance display off by default. |
| [tTimers (party fork)](addons/ttimers) | 0.25-party.5 | Approved 2026-10-05 | 2026-10-02 | Phoenix staff | Fork of approved tTimers 0.25: party job ability recasts, theme, skin. |

Not approved (reviewed 2026-10-05) and removed from this repository:

- **presence** 2.0.0: Phoenix already ships a Discord presence addon in its launcher,
  **phxpresence**, and recommends using it.
- **TradeNPC** 1.20.09.02-ashita.1: trade assist addons aren't allowed on Phoenix XI.

Any change that adds or modifies a feature makes a new version that has to be submitted for
review again. Staff have said cosmetic or visual changes that don't affect how an addon works
don't need resubmitting. Every update still gets a new version number and a new row here, and
each addon's README lists a SHA-256 for every file in that exact version.

## Install

Copy an addon's folder from `addons/` into `<Ashita>/addons/`, then `/addon load <name>`.

## For reviewers

Each addon's README starts with its own approval header, then a **For reviewers** section. That
section lists exactly which packets and game data it reads, what it writes, and what it does not
do (outgoing packets, commands, network access), plus the file hashes. Larger addons also ship a
`SHA256SUMS` file covering every file, so you can check a copy with `sha256sum -c SHA256SUMS`.
`tools/publish.py` generates the headers, reviewer sections and SHA256SUMS.

## How this repository works

1. An addon is finished and tested elsewhere, then copied into `addons/<name>/`.
2. Its version is added to the table above as **Pending review**, and the submission is tagged
   `<name>-v<version>`.
3. When staff respond, the row is updated to **Approved** or **Approved with conditions** (the
   conditions go in Notes). An addon that isn't approved is removed from the repository.

## License

GPL-3.0 (see `LICENSE`) unless an addon's folder has its own license. Price tables are generated from PhoenixXI's GPL-3.0 server repository.
