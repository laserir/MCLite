# Changelog

All notable changes to MCLite are documented here. The format is loosely based on
[Keep a Changelog](https://keepachangelog.com/), and the project follows semantic-ish versioning.

Targets: **T-Deck Plus** (`mclite-vX.Y.Z.bin`) and **T-Watch Ultra** (`mclite-watch-vX.Y.Z.bin`).

## [Unreleased]

### Added
- **OTA also refreshes SD translations.** A WiFi firmware update (**Admin → WiFi → Check for updates**) now, right
  after flashing, re-downloads the language files you already have on the SD card (`/mclite/lang/*.json`) from the
  release it just installed — so strings added in a new version show up translated instead of falling back to
  English until you next re-copy the files by hand. It only refreshes languages already present (never adds new
  ones), validates each download before replacing the file (a failed or corrupt fetch leaves the existing one
  untouched), and never blocks or fails the firmware update itself. English-only devices are unaffected. Also
  clears the "lang file is older than firmware" serial warning after an update.

## [0.4.2] — 2026-07-23

### Added
- **Pick a scope from a nearby repeater.** When setting a region scope — global (**Settings → Radio → Scope**) or
  a per-channel/room override — a new **From repeater** button asks a heard repeater for the list of regions it
  actually serves, so you don't have to know the exact names. Pick a repeater, then pick **one** region from the
  reply; it fills the scope field for you to review and **Save**. The manual text entry is untouched — this just
  adds a shortcut. The picker lists **direct-range (0-hop) repeaters** the device has heard (tap the flood-advert
  button on the Heard Adverts screen to prompt discovery). Requires a repeater running firmware that answers the
  region query. **Multi-hop repeaters are not supported** — the query needs a route-direct path, and an overheard
  advert path isn't a reliable one; configure scopes from a repeater within direct range. (#45)
- **"Heard by N repeaters" on sent channel messages.** A channel message you send now shows `✓ ↻ N` once nearby
  repeaters rebroadcast it — the `✓` means sent, and `↻ N` is how many distinct repeaters were heard echoing it
  back (the count climbs as echoes arrive). It's deliberately different from a direct message's `✓✓` (which is a
  real delivery ACK) — a channel echo only confirms a repeater relayed it. Always on; only meaningful where
  repeaters are in range. Thanks @jason-s13r (#39).
- **Show received hop count.** Optionally show how many hops a received message took (`xN` next to the time;
  `x0` = heard directly). Off by default — enable in **Settings → Messaging → Show Hop Count**. (#39)
- **Memory usage indicator (debug).** Optionally show `P% R%` (PSRAM / internal-RAM used) in the status bar, and
  log internal-DRAM free / low-water / largest-free-block plus PSRAM over serial — handy for diagnosing memory
  pressure. Off by default — enable in **Settings → Display → Show Memory Usage**. Thanks @jason-s13r (#38).

### Changed
- **LVGL now allocates from PSRAM instead of internal DRAM.** This frees scarce internal RAM for the things that
  can only live there — notably BLE companion mode. Measured on the T-Deck with BLE companion *and* an app
  connected: ~124 KB internal DRAM free with a ~111 KB largest contiguous block (negligible fragmentation); the
  redirect is effectively what lets that combination fit. Validated on both T-Deck (octal PSRAM) and T-Watch Ultra
  (quad PSRAM) with no perceptible UI slowdown. Falls back to DRAM if PSRAM is unavailable. Thanks @jason-s13r (#38).
- **Chat bubbles are freed when you leave a chat**, so a previously-viewed conversation no longer keeps its
  rendered bubbles in memory while you're on other screens (they're rebuilt from history on reopen). (#38)

### Fixed
- **A scope/region field with spaces no longer silently kills scoped floods.** A scope holds a *single* region
  name; pasting a multi-region `region def` list (e.g. `west pnw or wv eug`) hashed the whole string into a
  transport key that matched no repeater, so hashtag/private-channel traffic was dropped while Public kept
  working. The config tool now rejects a scope containing a space, and the firmware keeps only the first token
  (with a serial note) across every input path — `config.json`, the companion set-scope command, and the
  on-device scope editor. (#36)

## [0.4.1] — 2026-06-25

### Added
- **Manage rooms and channels from the companion app.** The companion protocol gained the standard write/action
  commands the official MeshCore app uses, so things that previously failed against MCLite now work:
  - **Room login** (`CMD_SEND_LOGIN`): log into a configured room or repeater from the app. A blank password field
    uses the password already in the device config, and a wrong password instantly retries with the stored one.
    Thanks to the reporters (#32).
  - **Add / remove channels** (`CMD_SET_CHANNEL`): join a Public, hashtag, or private channel — or remove one —
    from the app. **Adding applies instantly with no reboot** — the channel is usable right away and its share QR
    shows the real key, not zeros, with the session staying connected. **Removing a channel still reboots** to
    apply (a known limitation — MeshCore offers no way to remove a channel from the running radio, so the device
    rebuilds its channel table from config at boot; the app reconnects on its own). Gated by the
    `permissions.conversation_management` setting, so a locked-down device still refuses it (#31).
  - **Add / edit / remove contacts** (`CMD_ADD_UPDATE_CONTACT`, `CMD_REMOVE_CONTACT`): add a contact, rename one,
    or delete one (and its chat history) straight from the app. **Adding and renaming apply instantly** (the app
    stays connected); **removing reboots** to apply (the app reconnects) — see the consistent add/remove model
    below. Editing maps to the contact's **display name**; per-contact permission flags stay device-owner settings
    (the config tool / on-device Admin), and the app's own contact flags remain app-local. Gated by
    `permissions.conversation_management` (#33).
  - **Share a contact** (`CMD_SHARE_CONTACT`) re-broadcasts a contact's advert so a nearby device can add them,
    and **reboot** (`CMD_REBOOT`) is now honoured from the app's button.
  - The app's **Local vs Flood** advert buttons were already handled; confirmed during this work.
  With this, the companion can fully manage rooms, channels, and contacts (#31, #32, #33 all closed). A consistent
  rule across both contacts and channels: **adding and editing apply instantly; removing reboots** to apply (the
  app reconnects on its own). The reboot on removal is required for channels — MeshCore has no way to drop a
  channel from the running radio — and kept for contacts so the behaviour is uniform and predictable. Live changes
  are reflected on the **device's own UI** too — a contact/channel added or renamed from the app shows up in the
  on-device conversation list and Admin screens right away, no reboot. In-place editing of a contact's permission
  flags / a channel's settings still arrives alongside on-device editing.
- **Change device settings from the companion app.** With `permissions.settings` set to `full`, a connected app
  can now set the **device name**, **radio parameters** (frequency / spreading factor / bandwidth / coding rate),
  **TX power**, the **BLE pairing PIN**, and the **path-hash mode** (1/2/3 bytes per hop). Values are range-checked;
  the device name applies instantly to mesh adverts (the Bluetooth scan name refreshes on the next reboot), while
  radio / TX / PIN / path-hash save and apply on a quick reboot (the app reconnects). **Path-hash mode** is
  especially useful on large meshes — it lets app users match a 3-byte network
  without hand-editing `config.json` (see #36). A device left at `restricted` or `none` refuses these. Identity
  keys, advert location (MCLite advertises GPS at the configured precision), and auto-add stay device-managed by
  design and are rejected.

- **Set the region/scope from the companion app.** The companion now honours `CMD_SET_DEFAULT_FLOOD_SCOPE` (set the
  persistent region), `CMD_GET_DEFAULT_FLOOD_SCOPE` (read it), and `CMD_SET_FLOOD_SCOPE_KEY` (a temporary session
  override), gated by `permissions.settings = full`. Together with the existing path-hash command, the radio's
  region and path-hash size are now both configurable from the app — the two settings a large 3-byte mesh needs.
  A public `#region` round-trips exactly (MCLite derives the same transport key MeshCore does); a custom/private key
  that isn't derivable from the name is refused.
- **On-device contact/channel/room actions.** Tapping a contact in Admin now offers **Reset path** (force flood
  rediscovery of the route) beside Delete; tapping a **channel or room** offers **Set scope** (edit that entry's own
  region override, blank = inherit the global one) beside Delete. A configured override is shown as `[scope:…]` on
  the row in both the manage and read-only views.
- **Edit region and path-hash size on the device.** Admin → Radio now lets you change the **region/scope** and the
  **path-hash size** (1/2/3 bytes per hop) directly on-device (a roller for the size, a text editor for the region),
  gated by `permissions.settings = full`. Both apply on a quick reboot. Handy for matching a large 3-byte mesh
  without the config tool. A blank region means none (`*`); a bare name like `region` is treated as `#region`. The
  other radio params (frequency / SF / BW / CR / TX) stay on the existing region-preset picker + TX slider.

- **More companion commands:** **reset a contact's path** (`CMD_RESET_PATH` — forces flood rediscovery of the
  route), **export a contact** (`CMD_EXPORT_CONTACT` — your own or a known contact's signed advert, as a portable
  blob), and **import a contact** (`CMD_IMPORT_CONTACT` — the imported advert appears in **Heard Adverts** to review
  and save, keeping the contact list curated). Import is gated by `permissions.conversation_management`.
- **Explicit un-scoped flood scope from the app.** The companion app can now force flood sends out un-scoped,
  independent of the configured region (`CMD_SET_FLOOD_SCOPE_KEY` explicit-unscoped variant). Session-only —
  reverts to the configured scope on reboot. Gated by `permissions.settings == "full"`.
- **Send a request to a node that isn't a contact** (`CMD_SEND_ANON_REQ`). The companion app can query a node
  addressed only by its public key — without adding it as a contact first — and the reply is forwarded back to
  the app. The temporary node is never saved to the contact list. Gated by `permissions.settings == "full"`.
  With this, MCLite now advertises companion firmware-version **13** so apps surface both this and the
  un-scoped-scope option above.
- **Node status and path-trace from the app.** Two more standard companion commands: **status request**
  (`CMD_SEND_STATUS_REQ` — ask a repeater/node for its status, e.g. uptime and counters) and **trace path**
  (`CMD_SEND_TRACE_PATH` — trace the route to a node and report per-hop signal). This completes the companion
  command set MCLite supports.

### Changed
- **Updated the MeshCore library 1.15 → 1.16.** Brings RadioLib 7.6, an SF-dependent preamble (32 symbols for
  SF ≤ 8, matching the rest of the network for better low-SF link reliability), and upstream fixes (EU
  client-repeat frequency, WiFi reconnect, repeater neighbor-discovery when path-hash mode ≠ 0). Confirmed
  interoperable with 1.16 nodes — including message ACKs — at SF8.

### Fixed
- **Companion message sync no longer drops messages on connect.** Two issues fixed: the on-connect history
  replay was capped (about two dozen messages) and walked oldest-first, so an active chat could lose its **newest**
  messages and other chats could sync nothing; and over WiFi a full transport send-queue could silently drop a
  message frame. Sync now streams the full stored history with no cap (newest included, all chats) and only
  removes a message once the transport confirms it was sent — so reconnecting shows everything you received.
  Works the same over WiFi and Bluetooth. (Note: messages you type **on the device** still don't appear in the
  app — the MeshCore companion protocol has no outgoing-message frame; see the README.)
- **Region scope without a leading `#` now matches MeshCore.** A region/flood-scope written as a bare name (e.g.
  `region` instead of `#region`) was hashed literally, producing a different transport key than the rest of the
  network. MCLite now prepends `#` to a bare name before deriving the key — mirroring MeshCore's implicit-hashtag
  behavior — so `region` and `#region` are the same region and interoperate. (`#`/`$` prefixes pass through
  unchanged; explicit `#region` configs are unaffected.)
- **Custom canned-message list (global) was lost on save.** A `canned_messages` array configured at the messaging
  level was serialized back as a bare `true`, so the custom list vanished on the next boot — and with the new
  companion config writes, almost any settings change triggered it. Serialization now writes the array faithfully
  (and honors the on/off toggle, so a disabled state also persists). Per-conversation canned lists
  (per contact/channel/room) were always stored separately and were never affected. Thanks @jason-s13r (#34).
- **Non-English devices ignored a configured global canned list in the chat quick-reply picker.** The picker only
  honored a custom `canned_messages` array when the UI language was English; on other languages it always showed
  the translated defaults. A configured custom array is an explicit, language-independent choice and now wins
  regardless of language (translated defaults still apply when no custom array is set). Thanks @jason-s13r (#34).

## [0.4.0] — 2026-06-22

### Added
- **Request contact telemetry from the companion app.** The companion protocol now honours the standard
  `CMD_SEND_TELEMETRY_REQ` (39): a connected client (meshcore-cli, meshcore-proxy, custom apps, and the official
  app where its UI offers it) can ask MCLite to query a contact's telemetry over the mesh, and the parsed reply
  is pushed back as the standard `PUSH_CODE_TELEMETRY_RESPONSE` (0x8B) carrying the raw CayenneLPP. Reuses the
  on-device telemetry path and its single pending-request slot (rejects with an error while a request is already
  in flight), and is gated by the existing `messaging.request_telemetry` setting. The first companion command
  that initiates a mesh request — it changes no stored state (same scope as send-message / send-advert).
- **Share a contact over the air.** Direct-message chats gain a **Share** button in the header that
  re-broadcasts that contact's original signed advert at zero hop — a nearby device hears it and can add the
  contact straight from its **Heard Adverts**, no key typing. This is MeshCore's standard contact-sharing
  mechanism (`shareContactZeroHop`); MCLite now caches each heard advert and backs saved contacts' adverts to
  the SD card so sharing still works after a reboot. The button only appears when we hold a re-broadcastable
  advert for the contact (added from a heard advert, or heard this session). Gated by the new
  `messaging.share_contact` setting (**on by default**; set false to hide the button).
- **On-device add/remove of contacts, channels, and rooms.** With `permissions.conversation_management`
  (config-tool provisioned, **on by default**; set false to lock the lists down), the Admin → Conversations screens gain **Add** and
  **Remove** for every type: contacts (from a heard advert, or by entering a 64-hex key), channels (Public
  one-tap, hashtag by name, or private with a generated/entered PSK), and room servers (name + key + optional
  password). Mirrors the config tool's rules (PSK derivation, Public's fixed key, caps of 32/16/8, duplicate
  checks) so device-made entries round-trip cleanly. Changes save immediately and **apply after a reboot**
  (you're prompted), and removing an entry also clears its chat history. When the flag is off, the lists stay
  read-only as before.
- **More radio region presets + a roller picker.** The region preset list grew to 19 entries (EU/UK/CH,
  US/Canada, several AU regions, Brazil, Czech Republic, EU 433, Netherlands, New Zealand, Portugal,
  Switzerland, Vietnam, …), sourced from the MeshCore config API and kept in sync between the config tool and
  firmware. On-device the Radio region picker is now a scrollable roller (instead of a button grid) that
  pre-selects the current region. Importing a config whose radio settings don't match a preset round-trips as
  "Custom" with the raw values preserved. Thanks [@jason-s13r](https://github.com/jason-s13r) (#29).
- **Chat header action buttons (DM).** The chat screen moved to the standard windowed header (back · title ·
  buttons), and direct-message chats gain two header buttons: **Telemetry** (refresh) opens the
  battery/location/distance modal and requests fresh telemetry, and **Map** (GPS) opens the map centred on the
  contact. The map button appears only when we actually have a position for the contact (telemetry / advert /
  heard) and updates live while the chat is open — it shows when an advert brings in a location and hides when a
  last-known fix ages out. Thanks [@jason-s13r](https://github.com/jason-s13r) (#30).

### Changed
- **Contact telemetry is now a header button, not the contact name.** Tapping the contact name in a DM chat no
  longer opens the telemetry/info pop-up — use the **telemetry (refresh) button** in the chat header instead
  (the name is now just the title). Part of the chat-header rework above (#30).
- **Modal buttons are consistent everywhere.** Every confirmation/chooser/info dialog (reboot, delete, offgrid,
  SOS alert, telemetry, firmware install, Heard-Adverts detail, …) now uses one shared modal widget with
  full-width, stacked buttons instead of cramped side-by-side rows, and the per-section settings editors
  (device name, boot text, SOS keyword, PIN, timezone) stack their Save/Cancel the same way — easier to read and
  tap on both boards.

### Fixed
- **Configured aliases now display everywhere.** The map (global + contact-focused) and the companion app's
  contact/room list were showing each node's *self-advertised* name instead of your configured alias — MeshCore
  overwrites a contact's stored name with the advertised one on every advert received. The map now resolves
  marker names from the local contact store (advert-stable alias), and the companion contact frames send the
  configured alias for contacts and the configured name for room servers (matched by pubkey). Heard-but-not-
  configured nodes still show their advertised name. (Companion aliases refresh on the next full contact sync.)

## [0.3.9] — 2026-06-21

### Added
- **Step-wise admin permissions.** Beyond the existing `security.admin_enabled` (global on/off for the Admin
  screen), a new `permissions` config block scopes what's reachable *inside* Admin: `permissions.settings`
  (`full` / `restricted` / `none`) — **restricted** keeps only the basics editable (brightness, auto-dim, dim
  brightness, keyboard brightness, theme) and shows everything else read-only (no chevron); **none** makes all
  settings read-only. `permissions.companion` (default on) hides the Companion group (WiFi/USB/Bluetooth) when
  off — configured services still run. `permissions.conversation_management` (default off) is reserved for a
  future release (on-device add/edit/remove of contacts/channels/rooms; they stay read-only views for now). All
  three are provisionable from the config tool. Defaults are fully permissive, so existing configs are unchanged.
- **Settings reorganised into per-section screens + Admin is now a pure hub.** The on-device Admin screen no
  longer mixes settings, diagnostics and shortcuts — it's three labelled groups of links: **Companion** (WiFi /
  USB / Bluetooth), **Conversations** (Contacts / Channels / Rooms, read-only views), and **Settings** (Device,
  Radio, Display, Messaging, Sound, GPS, Battery, Security). Each section is its own screen mirroring the config
  tool, with all of its editable settings *and* its read-only diagnostics in one place (no more duplicated rows
  across Admin and Device Settings). Newly editable on-device: **Radio** (region preset picker — EU/UK/CH vs
  US/Canada — plus a TX-power slider and an advert-interval picker; frequency/SF/BW/CR/scope/path-hash stay read-only),
  **Messaging** (history, max-per-chat, location format, retries, telemetry request/badges/auto-refresh, canned
  messages, allow-mute), and **GPS** (enable, location-advert precision, timezone, clock offset, last-known max
  age). Offgrid mode and the live Heard-Adverts count now live at the top of the Radio screen. Each hub link
  carries an icon (gear for settings; `@`/`#`/`R` for contacts/channels/rooms; Wi-Fi/USB/Bluetooth for
  companion), and the 3rd-party licenses moved to an *About* block at the bottom of the hub. Radio/GPS changes
  reboot once on exit (same batched-save model as theme/language). The old single "Device Settings" screen is
  superseded by this layout.
- **Selectable UI themes.** Choose a color palette — **Dark** (default), **Light**, **Amber** (a "military"
  night mode that preserves night vision), or **High contrast** — on-device (Admin → Theme, reboots to apply)
  or via `display.theme` in config. Custom palettes can be defined under `display.themes` (start from a built-in
  `base`, override any color with `#RRGGBB`). Default appearance is unchanged. On/off switches now use the
  theme accent colour too. Thanks [@jason-s13r](https://github.com/jason-s13r) (#24).
- **Per-row Info + Map buttons on the Heard Adverts screen.** Each heard node now has an explicit info (eye)
  button that opens its detail dialog, and — when the advert carries a location — a map button that opens the
  map centered on that node. Back now returns to the Admin screen. Thanks [@jason-s13r](https://github.com/jason-s13r) (#15).
- **Map screen pan buttons + windowed chrome.** The map gains an on-screen D-pad (up/left/centre/right/down)
  alongside the existing drag-to-pan. On the T-Deck the map now keeps the **status bar visible** and uses the
  standard `lv_win` header with a back button (the T-Watch stays full-screen). Thanks [@jason-s13r](https://github.com/jason-s13r) (#22, supersedes #20/#21).
- **Uptime + last-charged in the Admin Battery section.** Shows when the device booted (wall-clock + relative)
  and when charging last stopped (with the level at the time). Thanks [@jason-s13r](https://github.com/jason-s13r) (#23).
- **On-device Device Settings.** A new editable settings screen (Admin → Device Settings, behind the existing
  `admin.enabled` gate) for changing device name, boot text, language, theme, security (lock mode / auto-lock /
  PIN), sound (SOS keyword/repeat, low-battery alert), and display (brightness, auto-dim, keyboard backlight,
  emoji, screenshots) directly on the device — no config-tool round-trip needed. Thanks [@jason-s13r](https://github.com/jason-s13r) (#27). The theme picker now lives here (removed from the read-only Admin info screen).
- **Mention tag uses square brackets** — tapping a sender's name inserts `@[name]` so names with spaces stay
  intact. Thanks [@jason-s13r](https://github.com/jason-s13r) (#26).

### Changed
- **Device Settings saves once on exit.** Edits update the device live (brightness, etc.) but the SD write is
  now batched — `config.json` is written a single time when you leave the screen, instead of on every change.
  Theme/language changes reboot once on exit (toast on selection) rather than immediately, so you can change
  several settings and apply them together.
- **Translation files now carry a release version** (`"version"`, e.g. `39` for 0.3.9). On boot the firmware
  logs a serial warning if a loaded language file is older than the firmware's string set, so missing
  translations (English fallback) are diagnosable — re-export the lang files from the config tool to refresh.
- **Auto GPS refresh now defaults off** (`messaging.auto_telemetry`). A fresh device no longer emits periodic
  telemetry requests on the mesh unless you opt in — quiet by default, matching the advert changes in 0.3.8.
  Existing configs that set the field are unaffected.
- **Standardized screen chrome.** The Admin, WiFi, USB, and Bluetooth screens now use the same windowed
  header with a left-arrow back button as the rest of the UI. Back from a companion/WiFi screen returns to
  Admin; back from Admin returns to the conversation list. Thanks [@jason-s13r](https://github.com/jason-s13r) (#16–#19).
- **Conversation-list row icon order** now matches the status bar — mute · GPS · battery · last-seen eye ·
  time, with the time pinned to the right edge so the times line up in a column down the list.

## [0.3.8] — 2026-06-16

### Added
- **Location-advert privacy precision.** The location-advert setting (now `gps.location_precision`) can coarsen
  the position you broadcast: **Off · Exact · ~100 m · ~750 m · ~3 km · ~12 km · ~50 km** (Meshtastic-style grid
  snapping, centred in the cell). Only the broadcast advert is coarsened — **telemetry replies to authorized
  contacts and the in-chat GPS insert always use your exact position**. Default off; old `location_advert:
  true/false` configs are read automatically (true → exact). Scheme adopted from [@jason-s13r](https://github.com/jason-s13r).
- **Zero-hop "Local" advert button** on the Heard Adverts screen (alongside the existing flood/mesh-wide one) —
  announce yourself to immediate neighbours without flooding the whole mesh.

### Changed
- **No more periodic flood adverts by default** (issue #13). MCLite previously broadcast a mesh-wide flood
  advert every ~9 minutes, which congests established meshes (one device was measured generating ~half of all
  adverts on a 110-repeater network). Now the device sends a single flood advert **on boot** and otherwise only
  advertises **on demand** — matching how stock MeshCore clients behave. Inbound reachability relies on
  MeshCore's flood-route discovery + the existing flood-retry. Thanks to @stucamp (#13) and @jason-s13r.
- **Opt-in periodic advert** — a new `radio.advert_interval_min` config field (config tool → Radio) re-enables
  periodic flood adverts for ad-hoc / SAR / private meshes. **Default 0 = off**; if set, enforced to ≥60 min
  (1-hour floor) — 720 (12 h) recommended, like a repeater.
- **GPS button inserts your location into the message** instead of popping a "Send Location?" confirm. Tapping
  the GPS icon in chat now appends `@ <coords>` to the input so you can add context and send with the normal
  Send button (mirrors the @mention insert; byte-guarded against the 160-byte limit).

## [0.3.7] — 2026-06-15

### Added
- **Tap a shared location to open it on the map.** When a received (or sent) message contains a GPS position —
  decimal `lat, lon` **or** MGRS/UTMREF — an underlined **"Open in map"** link appears under the bubble; tapping
  it opens the map centered there. Touch-only (doesn't touch trackball navigation), and shown only when map
  tiles are present on SD (same rule as the telemetry Map button). One link per message — a "both"-format
  position links the decimal. Adds a reverse MGRS→lat/lon parser (`util/mgrs.h`) and a coordinate detector
  (`util/coordparse.h`), both unit-tested.
- **Screenshot to SD** (debug aid, off by default). With `debug.screenshots` enabled in config, capture the
  current screen to `/screenshots/*.bmp` (24-bit BMP, opens on any PC) — **T-Deck: Shift+$**; **T-Watch:
  double-press the side (PEK) button**. Uses LVGL's snapshot into a PSRAM buffer; a toast confirms the save.
  (Overlays on the top layer — toasts/PIN/SOS — aren't captured.)

## [0.3.6] — 2026-06-12

### Added
- **Emoji in chat** — received emoji now render inline (a monochrome OpenMoji font with a Montserrat fallback,
  so plain text is unchanged and unknown glyphs degrade gracefully). An on-device **emoji picker**
  (`display.emoji`, **default on**, can be disabled) adds a smiley button to the chat input for composing from a curated set;
  it won't let you push a message past the 160-byte limit. Incoming/outgoing text is sanitized (strips emoji
  variation selectors that render as boxes, normalizes “smart” quotes to ASCII). Adopted from the
  [@jason-s13r](https://github.com/jason-s13r) fork; OpenMoji is CC-BY-SA 4.0 (see LICENSES.md).
- **Three-step volume** — the status-bar bell now cycles **max → mute → mid → max** instead of a binary
  mute, and the built-in chime *and* custom WAV notifications scale to the level. Default is max (loudness
  unchanged); SOS stays fixed and loud; always-sound contacts still override mute. Note: custom WAVs are now
  volume-scaled, so they play at the chime's level rather than their raw file loudness
  ([@jason-s13r](https://github.com/jason-s13r), #11).

### Changed
- **`sound.enabled` is now a true master switch.** Previously `false` just booted the device muted (still
  toggleable via the status-bar bell, and SOS/always-sound contacts could still make noise). Now `false` means
  *fully silent* — no notifications, no chime, **and no SOS sound** — and the status-bar bell is hidden so
  there's no per-session volume toggle. Set `sound.enabled: true` (the default) for the previous behavior with
  the 3-step volume bell.

### Fixed
- A last-known position restored after reboot no longer reports "~0s ago" before the clock has synced. The
  saved fix carries an absolute timestamp but `millis()` resets on reboot, so until GPS re-locks (or NTP/WiFi
  syncs the clock) its age can't be computed — it now shows "Last known position" instead of a misleading 0s
  (which also avoided sending a stale position over the mesh as if it were current). Once the clock syncs, the
  real "~Xm ago" age is shown again.
- **A failed send now shows a toast** instead of silently drawing a `FAILED` bubble. When a message can't be
  queued — e.g. the static packet pool is drained by a burst — you get a "Send failed - try again" toast (the
  failed bubble is still there to tap-retry). Previously the only signal was the bubble's small status icon.
- **Map tile loader hardening.** The slippy-tile PNG decoder now rejects any tile larger than the standard
  256×256 (a corrupt or non-standard PNG could previously overflow the fixed scanline buffer) and validates
  tile coordinates (zoom 0–19, indices within range) before touching the SD card — out-of-range tiles grey-fill
  cleanly instead of building nonsense paths. Missing/undecodable tiles already grey-filled; this closes the
  oversized-tile gap.

## [0.3.5] — 2026-06-11

### Fixed
- **Translations past ~128 keys reverted to English.** The i18n loader capped SD-loaded strings at 128, but the
  language files now hold ~197 keys — so on German/French/Italian every key past the cap silently fell back to
  English (e.g. `canned_5`–`8`, plus the offgrid / firmware-update / WiFi / USB / BLE / map / heard-adverts /
  toast screens). Raised the cap to 256 and added a boot-time warning if a language file ever exceeds it.
- **Config tool wiped stored WiFi on edit.** The tool's file-import never loaded the `wifi` section, so
  importing a device's `config.json`, editing it, and re-exporting produced an empty `wifi` block — clearing
  the device's stored SSID/password on the next copy to SD. WiFi (and the persisted BLE pairing PIN) now
  round-trip correctly, including through the share-link and start-fresh paths.

## [0.3.4] — 2026-06-11

### Added
- **Auto-refresh contact GPS** — keeps the map markers / convo-list badges of contacts who *don't* broadcast
  their own location fresh, by quietly re-requesting telemetry GPS before the cached fix goes stale. Throttled
  (one request per scan, respects the EU duty cycle, yields to manual requests) and self-limiting (stops asking
  a contact that doesn't answer). New setting `messaging.auto_telemetry`, **default on**, can be disabled.
- **Per-conversation quick replies** — any contact, channel, or room can carry its own `canned` list (max 8)
  that overrides the global quick-reply list *for that chat only*; leave it empty to fall back to the global
  list. Editable per card in the config tool. Turns a conversation into a command menu — e.g. a Home Assistant
  / automation bridge ("Open gate", "Lights on", "Status?").
- **Last-known location persists across reboots** — the most recent GPS fix is saved to SD
  (`/mclite/last_location.json`, throttled) and restored on boot, so the map opens to your last position
  without waiting for a fresh fix ([@jason-s13r](https://github.com/jason-s13r), #10).
- **Advertise from the companion app** — the MeshCore phone/desktop app's **Advertise** button now works while
  connected (BLE/WiFi/USB); previously it was rejected as an unsupported command. Honours the app's flood vs
  local (zero-hop) option. The on-device advert button and the automatic periodic advert are unchanged.

### Changed
- The device-info / admin screen is now fully localized — every row label routes through the translation
  table (de/fr/it) ([@jason-s13r](https://github.com/jason-s13r), #9).
- Conversation history now loads only the most recent `max_history_per_chat` messages per chat at boot
  (previously the whole file was loaded into RAM). Bounds memory if a history file is larger than the cap —
  e.g. after lowering the setting. No visible change; the runtime cap was already in place.

### Fixed
- Telemetry retry is no longer cancelled by the contact-info pop-up's own timeout when the mesh's outbound
  queue is busy (the two timers now stay in lockstep) — the retry fires under congestion as intended.
- Closing the contact-info pop-up now cancels its in-flight telemetry retry, so no stray flood request goes
  out for a pop-up you already closed.
- Muting a chat is now absolute: it silences notifications even for a contact flagged `always_sound`
  (`always_sound` still overrides *global* mute, unchanged).
- Messages that exceed the 160-**byte** limit (e.g. emoji or accented/non-Latin text — which can be ≤160
  *characters* but more bytes) are now refused with a "Message too long" toast that keeps your text, instead
  of silently failing to send while still drawing a (failed) bubble.
- A timed-out telemetry request now releases the radio's single telemetry slot when the exchange ends
  (previously the slot stayed held after a no-response request). Without this, **auto-refresh contact GPS**
  would stall for the rest of the session after the first contact that didn't answer, and slow/multi-hop
  contacts could be backed off too eagerly; both are resolved.
- Auto-refresh no longer lets a single un-sendable contact block the rest of the round-robin, and
  `max_history_per_chat: 0` now consistently means "unlimited" on both load and prune (previously prune at 0
  would wipe the conversation).

## [0.3.3] — 2026-06-10

Reliability + contact-location improvements, plus a batch of community contributions
([@jason-s13r](https://github.com/jason-s13r)'s fork PRs).

### Added
- **Unified contact location** — one source of truth for where a contact is: fresh telemetry (accurate) →
  their advert GPS → a heard advert. The convo-list **GPS badge** now appears for *any* known position (not
  just telemetry), and the **telemetry pop-up** shows that position even without a telemetry reply — marking
  advert-sourced coordinates approximate (`~`) and offering the **Map** button — and on a request timeout it
  shows the known position instead of a bare "No response". Telemetry stays primary; the 30-min window is
  unchanged.
- **Flood-routing retries** for better delivery when a direct path degrades — on **DM** retries (#5) and on
  **telemetry-request** retries (#6, with a "Retrying…" state and queue-aware timeout extension).
- **Per-chat mute** (opt-in, `messaging.allow_mute`, default off) — long-press a conversation to mute; muted
  chats don't beep or wake the screen (SOS always does), with an indicator in the list and chat header (#4).
- **Vendor row** on the device-info screen showing the firmware's source repo (`owner/repo`) — handy with
  fork flashing (#7).

### Fixed
- Chat **scroll-to-bottom** is more robust on an empty chat area / on open (#3).

### Thanks
- @jason-s13r for PRs #3–#7.

## [0.3.2] — 2026-06-09

Map unification + fixes from a careful review of 0.3.0/0.3.1.

### Changed
- The contact "Map" button and the status-bar GPS icon now open the **same** map (one screen, one set of
  controls). Opening from a contact just centers on that contact and pre-selects it — drawn slightly larger
  with a highlight ring and its name in the bottom bar — while still showing every other contact / telemetry /
  heard-node location and your own position (a distinct green/amber dot).
- **Reload** rebuilds *all* markers and re-checks your own position; **Center** always jumps to your own
  location once a fix is available (even when the map was opened from a contact), falling back to the
  location the map opened on when there's no fix.

### Fixed
- Center button now uses your own location on a contact-opened map (previously it only ever recentered on the
  contact).
- The status-bar GPS icon stays visible (dimmed) and tappable when GPS is disabled in config, so the general
  map remains reachable.
- `@mention` no longer truncates a near-full message draft (skips the insert when it wouldn't fit).
- Companion: the contacts `since` field is read via `memcpy` (removed an unaligned read).

## [0.3.1] — 2026-06-09

Fork-adoption batch (features adopted from the jason-s13r/MCLite fork) plus map polish.

### Added
- **GPS location in adverts** (opt-in, `gps.location_advert`, default off): broadcast your position so
  contacts see you on their map. Uses MeshCore's native advert location (full precision); sends a LIVE fix or
  a still-valid last-known one. Unencrypted broadcast — hence opt-in. Read-only status shown on the admin GPS
  screen; toggled via the config tool / SD only.
- **General map** — tap the status-bar GPS icon to open a map of your own location plus every heard node /
  contact that carries GPS, drawn with the same chat / repeater / room / sensor symbols as the heard-adverts
  list. Tap a marker for its name; **Reload** button re-scans heard nodes without panning.
- **NTP time sync** — when WiFi is connected and GPS hasn't locked, set the clock from an NTP server using the
  configured POSIX timezone. GPS still overrides once it locks.
- **@mention** — tap a sender's name in a channel/room to insert `@name ` into the message box.
- **Fork-aware OTA** — build-time overridable update repo (`MCLITE_REPO_OWNER` / `MCLITE_REPO_NAME`,
  default `laserir/MCLite`) so forks can self-update from their own releases.
- **Web flasher repo/fork picker** — choose which repo's published releases to flash.

### Changed
- Map markers render as filled colored dots (type color + black rim) with a contrasting symbol, so they read
  against any map tile; selection ring sits just outside the dot.

### Fixed
- Map markers are now reliably tappable (tap-slop dead-zone so a jittery tap selects instead of micro-panning;
  wider hit tolerance).
- Map markers no longer blink out near the viewport edge across zoom levels (consistent rounding + wider clip).

## [0.3.0] — 2026-06-09

### Added
- **Companion mode** — bridge the radio to a phone, desktop, or CLI over the standard MeshCore companion
  protocol, in parallel with normal on-device use (messages appear in both):
  - **Bluetooth** — pairs with the official MeshCore iOS/Android apps (6-digit passkey + bonding).
  - **WiFi** — reachable from `meshcore-cli` / `meshcore.js` / `meshcore_py` on the LAN.
  - **USB** — wired serial companion (debug logs muted while active so they don't corrupt the protocol).
  - Messaging works; config is read-only. One transport at a time.

### Changed
- Refreshed README (Quick Start + balanced companion docs).

### Notes
- WiFi and Bluetooth can't run together (shared radio/RAM); the device handles the switch and offers a reboot
  when needed.
- Known limitation: messages typed on the device don't mirror to the companion app (the protocol has no
  firmware-composed-message event).

## [0.2.2] — 2026-06-07

### Fixed
- WiFi over-the-air firmware updates no longer crash the device (TLS handshake + on-stack buffer overflowed
  the loop-task stack). SD-card install and USB flashing were unaffected.

### Notes
- **0.2.1 was withdrawn** — its WiFi installer had the crash above. Update from 0.2.1 via SD card or USB.

## [0.2.0] — 2026-06-01

### Added
- **T-Watch Ultra support** — second target board (touch-native UI, AMOLED, RTC, haptics).
- **On-device firmware update** — install a `.bin` from the SD card, or check & download updates over WiFi
  (the WiFi path was hardened in 0.2.2).

## [0.1.8] — 2026-05-05

### Added
- **Heard Adverts** — 64-entry rolling list of every advert the radio decodes, with type icons, last-heard
  age, per-hop path, and a one-tap **Save** to add a chat advert to contacts (applies next boot). Manual-advert
  button announces yourself on demand.

### Changed
- Config saves are **atomic** (stage → `.bak` → rename) with boot fallback to `config.json.bak`, protecting
  the identity keys against a torn write.

### Fixed
- `Discovered contact` log printed the raw packed `path_len` byte instead of the hop count.

## [0.1.7] — 2026-04-29

### Added
- **Room server client** — join MeshCore community message boards (up to 8), with auto-login + backoff,
  disconnect recovery, per-room flags (`read_only` / `allow_sos` / `send_sos` / `scope`), sender resolution,
  and persisted `sync_since`. Admin "Rooms" section + config-tool Rooms card.

### Changed
- Contact/conversation caps raised for the extra rooms (40 contacts / 56 conversations).

## [0.1.0] – [0.1.5] — 2026-03 / 2026-04

Foundation releases: the core standalone MeshCore companion firmware for the T-Deck Plus — encrypted DMs and
channels, SOS alert system, GPS location sharing (lat/lon + MGRS), telemetry, message history on SD,
internationalization (de/fr/it), the offline config tool, and the browser web flasher — iterated across
0.1.1–0.1.5.
