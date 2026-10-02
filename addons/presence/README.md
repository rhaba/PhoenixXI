# presence

<!-- staff-approval -->
## Approval by staff

| Version | Status | Submitted | Reviewed by | Notes |
|---|---|---|---|---|
| 2.0.0 | Pending review | 2026-10-02 | | Display only, sends nothing to the game server. Updates your local Discord status. |

**Author:** Spongeh. **Program:** Ashita v4. **Type:** Discord Rich Presence. It shows your job and zone as your Discord "Playing ..." status, and never sends anything to the game server.
<!-- /staff-approval -->

---

Ashita v4 addon that shows your FFXI job and zone as your **Discord Rich
Presence** status - the "Playing ..." line that appears under your name in
Discord. It does not post any messages to any channel; it only updates your
own local Discord client's status, the same mechanism games use natively.

It talks directly to your locally running Discord desktop client over its
IPC (named pipe) protocol using LuaJIT's FFI - no external helper process,
no bot, no webhook.

**Requires the Discord desktop client to be running on the same PC as
FFXI.** Rich Presence is a local-client feature; it will not work against
Discord in a browser or on another machine.

## Set up a Discord Client ID (one-time, free)

Rich Presence requires a Discord "Application" purely to get a Client ID -
you do not create a bot, add it to a server, or invite it anywhere.

1. Go to <https://discord.com/developers/applications> and log in.
2. Click **New Application**, give it a name (e.g. "FFXI Presence") - this
   name is what Discord shows next to your status.
3. On the **General Information** page, copy the **Application ID**. That's
   your Client ID.
4. (Optional) Under **Rich Presence > Art Assets**, upload an image (e.g.
   `branding/phoenix_presence_logo.png` in this addon's folder) and note the
   asset key you give it - then run `/presence icon <key> [hover text]`
   in-game to use it as your large icon.

## Install

1. Copy the `presence` folder into your Ashita `addons` directory, e.g.:
   `Ashita\addons\presence\presence.lua`
2. In-game (or in `scripts/default.txt`), load it:
   ```
   /addon load presence
   ```
3. Set your Client ID:
   ```
   /presence clientid 123456789012345678
   ```
4. Verify it works:
   ```
   /presence test
   ```
   Check your own Discord profile - you should see "Playing FFXI Presence"
   (or whatever you named the application) with your job and zone.

Settings are saved automatically to
`Ashita\config\addons\presence\settings.lua`.

## Commands

| Command                          | Description                                    |
|-----------------------------------|-------------------------------------------------|
| `/presence clientid <id>`        | Set the Discord application Client ID          |
| `/presence on` / `off`           | Enable/disable Rich Presence updates           |
| `/presence job <on\|off>`         | Show job/subjob in the details line (default on) |
| `/presence zone <on\|off>`        | Show current zone in the state line (default on) |
| `/presence icon <key> [text]`    | Set the large image asset key (and optional hover text) |
| `/presence icon clear`           | Remove the large image asset                   |
| `/presence test`                 | Force an immediate presence update             |
| `/presence status`               | Show current settings/connection state         |
| `/presence help`                 | Show command list                              |

## How it works

Each render frame (throttled to once every `poll_interval` seconds, default
2) the addon checks your own party slot (`party:GetMemberIsActive(0)`),
job/subjob, and zone:

- **Login** (character-select -> in world): opens the local Discord IPC
  connection, sends the handshake, and sets your activity with an elapsed
  timer starting from that moment.
- **Job, subjob, or zone change**: updates the details/state text on the
  existing connection.
- **Logout** (in world -> character-select): clears your activity so
  Discord stops showing "Playing ...".
- **Addon unload / `/addon unload presence`**: clears the activity and
  closes the pipe.

If Discord isn't running (or hasn't been given a Client ID yet), updates are
marked "pending" and retried automatically, at most once every 15 seconds,
so it recovers on its own once Discord is available.

## Notes / caveats

- Only your own character (party index 0) is tracked - this shows one
  character's presence, not a party/alliance monitor.
- The IPC write is effectively instant (a local named pipe, not a network
  call), so there's no noticeable hitch like there would be with an HTTP
  request.
- If you run multiple FFXI characters at once, each Ashita instance connects
  to Discord independently; only the most recently updated one will be
  visible, since Discord shows one Rich Presence per application per user.

<!-- for-reviewers -->
## For reviewers

### What it reads

- **No packets** of any kind.
- **Client memory, through Ashita's API:**
  - **your own party slot (index 0):** whether you're logged in, your name and your zone. The
    name is used only to detect login, logout and character changes, and is never sent anywhere;
  - **your own player data:** main and sub job and their levels;
  - **zone names** from the client's resource data.

It checks these at most once every 2 seconds.

### What it writes

- **Its own settings file,** through Ashita's settings library: your Discord application id, the
  on/off switches and the optional icon key.
- **Your local Discord client,** over Discord's local IPC named pipe (`\\.\pipe\discord-ipc-N`),
  the same mechanism games use for Rich Presence. It sends the handshake and a `SET_ACTIVITY`
  with:
  - your main/sub job and levels (if enabled);
  - your zone name (if enabled);
  - a session start time;
  - the optional icon.

  On logout or unload it clears the activity. This is local to your PC. It isn't a network
  request from the addon, and it posts no messages to any Discord channel.

### What it does NOT do

- **No game traffic:** no outgoing or incoming game packets are sent, read, modified or
  blocked.
- **No commands:** no `QueueCommand` or automated chat or actions.
- **No other network or file access:** no HTTP, sockets, bots or webhooks. The only I/O is the
  local Discord pipe and its own settings file.
- **No other players' data:** only your own character.

`branding/phoenix_presence_logo.png` is an optional image you can upload to your own Discord
application as the status icon. It's original artwork, not Phoenix's logo.

### Files in the reviewed version

`SHA256SUMS` lists the SHA-256 of every file in this version (2 files).
Its own SHA-256 is `57a71d467fc5c39d25c572c59753420ed58e53571809a5180e66c591a5c55cfe`.

Main files:

| File | SHA-256 |
|---|---|
| `presence.lua` | `f90230b8042785e25702919fede8a64daba9114821a5b0f166ccfe23e8fe4ebd` |
