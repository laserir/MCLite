<p align="center">
  <img src="docs/images/mclite-banner.png" alt="MCLite — Off-grid messaging, made simple" width="100%">
</p>

# MCLite for T-Deck Plus & T-Watch Ultra

Lightweight off-grid communicator firmware for the LilyGo T-Deck Plus and T-Watch Ultra. Built on [MeshCore](https://github.com/ripplebiz/MeshCore), MCLite is purpose-built for emergency and outdoor communication -- no internet, no cell towers, no training needed. Turn it on and communicate.

MCLite is ideal for groups, families, search and rescue teams, and anyone who needs reliable off-grid messaging. It is fully compatible with other MeshCore devices and companion apps (iOS, Android) -- MCLite users can communicate with anyone on the same MeshCore network. All configuration is done in a single file -- one person sets it up, copies it to everyone's SD card, and the whole group is ready to go. No accounts, no pairing, no per-device setup. Perfect for kids, older family members, or anyone who just needs it to work.

Most features below are optional. The primary goal is to keep things extremely simple -- anyone who can use a smartphone can use MCLite without explanation. Advanced features like telemetry, GPS sharing, or PIN lock are there when you need them, but they stay out of the way until then.

<p align="center">
  <a href="https://laserir.github.io/MCLite/tools/web-flasher/"><img src="docs/images/btn-flash.svg" alt="Flash MCLite now" height="48"></a>
  &nbsp;
  <a href="https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html"><img src="docs/images/btn-config.svg" alt="Open Config Tool" height="48"></a>
</p>

<table align="center">
  <tr>
    <td align="center"><img src="docs/images/conversation-list.png" width="200" alt="Conversation list"><br><sub>Conversations</sub></td>
    <td align="center"><img src="docs/images/chat-view.png" width="200" alt="Chat view with map / telemetry / share header buttons"><br><sub>Chat &mdash; map / telemetry / share</sub></td>
    <td align="center"><img src="docs/images/chat-canned-messages.png" width="200" alt="Canned quick-reply picker"><br><sub>Canned quick replies</sub></td>
    <td align="center"><img src="docs/images/chat-emoji-picker.png" width="200" alt="Emoji picker"><br><sub>Emoji picker</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/heard-adverts.png" width="200" alt="Heard adverts list"><br><sub>Heard adverts</sub></td>
    <td align="center"><img src="docs/images/map-view.png" width="200" alt="Map view with pan / zoom controls"><br><sub>Map view</sub></td>
    <td align="center"><img src="docs/images/admin-screen.png" width="200" alt="Admin hub"><br><sub>Admin hub</sub></td>
    <td align="center"><img src="docs/images/admin-contacts.png" width="200" alt="On-device contact management"><br><sub>Contact management</sub></td>
  </tr>
</table>

<p align="center">
<img src="docs/images/twatchultra.jpg" width="810" alt="MCLite running on three T-Watch Ultra devices">
</p>

## Supported hardware

MCLite runs on two LilyGo ESP32-S3 boards. They share the same SX1262 LoRa radio, so they interoperate on the same mesh and run the same features -- pick whichever form factor suits you (or mix both in one group).

- **[T-Deck Plus](https://lilygo.cc/products/t-deck-plus)** -- handheld with a physical QWERTY keyboard, trackball, GPS, and a 2.8" display. The original, fully-featured target.
- **[T-Watch Ultra](https://lilygo.cc/products/t-watch-ultra)** -- wrist-worn device with a 2.01" AMOLED touchscreen. Fully touch-driven with an on-screen keyboard.

## Quick Start

New here? Three steps, no toolchain or install needed:

1. **Flash** your T-Deck Plus or T-Watch Ultra from the browser with the [web flasher](https://laserir.github.io/MCLite/tools/web-flasher/) (Chrome or Edge).
2. **Configure** your identity, contacts, and channels in the offline [config tool](https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html), then download `config.json`.
3. **Copy** `config.json` to the SD card, insert it, and power on.

That's it. Full walkthrough in [Getting Started](#getting-started) below, plus [Companion mode](#companion-mode) for pairing a phone/desktop/CLI.

## Features

> [!TIP]
> Out of the box it's just a messenger — pick a contact or channel and type. Everything below is **optional**; turn on only what you need.

*Release history and per-version changes: see [CHANGELOG.md](CHANGELOG.md).*

**Everyday messaging**
- **Direct messages** -- private, end-to-end encrypted 1:1 chats
- **Channels** -- group chats via shared, public, or hashtag channels (optional listen-only mode)
- **Room servers** -- join community message boards (up to 8), with auto-login and reconnect
- **Quick replies** -- optional canned-message picker; per-chat lists can double as a command menu (e.g. a Home Assistant bridge)
- **Emoji** -- inline emoji rendering plus an on-device picker, with graceful plain-text fallback
- **Reactions** -- long-press a message to react with an emoji, interoperable with the MeshCore One app (off by default; other clients see a plain text message)
- **Message history** -- conversations saved to SD and restored on reboot

**Location & awareness**
- **GPS location sharing** -- send your position (lat/lon or UTMREF/MGRS); optional advert broadcast with privacy-grid precision (off by default)
- **Offline map view** -- see contacts, heard nodes, and yourself on a slippy map (needs a tile pack on SD)
- **Telemetry** -- answer or request battery / GPS / distance, with per-contact permissions
- **Heard adverts** -- rolling list of every device your radio decodes, with one-tap **Save** to contacts
- **Share a contact** -- re-broadcast a contact's advert so a nearby device adds them with no key typing

**Safety & alerts**
- **SOS broadcast** -- long-press to send an emergency alert to your group
- **Battery alerts** -- automatic low-battery warning sent to your contacts
- **Notification sounds** -- incoming chime and SOS alarm, with custom-WAV support
- **Haptic feedback** *(T-Watch Ultra)* -- vibrate on messages and SOS, independent of mute

<details>
<summary><strong>Power &amp; advanced features</strong> &mdash; click to expand</summary>

**Device & comfort**
- **Themes** -- Dark, Light, Amber (night-vision), High-contrast, or your own custom palette
- **Multi-language** -- English, German, French, Italian, plus your own translations via SD
- **Auto-dim** -- screen and keyboard backlight dim on inactivity to save battery
- **Screen & PIN lock** -- key lock or PIN code, with optional auto-lock on dim
- **Real-time clock** *(T-Watch Ultra)* -- battery-backed RTC keeps time across reboots
- **Multiple input methods** -- QWERTY keyboard, trackball, and touchscreen

**Network & range**
- **Region scope** -- tag packets so repeaters can filter by region (global or per channel/room)
- **Path-hash mode** -- tune the repeater path-fingerprint size (1/2/3 bytes per hop) for dense meshes
- **Offgrid mode** -- one-flag switch to the community offgrid frequency, relaying for nearby offgrid nodes

**Connect & maintain**
- **Companion mode** -- bridge the radio to a phone / desktop / CLI over Bluetooth, WiFi, or USB; BLE pairs with the official MeshCore mobile apps
- **Update from SD card** -- drop a `.bin` on the card and install on boot -- no USB needed
- **Update over WiFi** -- check GitHub and install the latest release on-device (off by default)
- **Zero-config deployment** -- all settings in one JSON file; set up once, copy to the whole group
</details>

> [!NOTE]
> **Heard nodes go to "Heard Adverts", not your contact list — by design.** Unlike the stock MeshCore apps, MCLite never auto-adds a node you overhear to your contacts. Your contacts are the people you deliberately define (config tool) or **Save** from Heard Adverts; discovered nodes live in **Admin → Heard Adverts**. So a sparse or empty *contact* list is not a sign of weak reception — it's the intended behaviour. Adverts are also bursty: a freshly-booted device can take ~10–15 minutes to populate the heard list even with perfect reception.

## Getting Started

### Install the firmware

**Option 1: Web Flasher (recommended)**

Visit the [MCLite Web Flasher](https://laserir.github.io/MCLite/tools/web-flasher/), **choose your device** (T-Deck Plus or T-Watch Ultra), select a version, and flash directly from your browser. The flasher walks you through putting your board into download mode. No software to install -- just Chrome/Edge and a USB cable.

**Option 2: Manual**

Download the latest binary for your board from the [Releases](../../releases) page -- `mclite-v*.bin` for the **T-Deck Plus**, `mclite-watch-v*.bin` for the **T-Watch Ultra** -- and flash with esptool at offset `0x0`:

```
esptool.py write_flash 0x0 mclite-v0.4.0.bin          # T-Deck Plus
esptool.py write_flash 0x0 mclite-watch-v0.4.0.bin    # T-Watch Ultra
```

The T-Watch Ultra has no power switch -- if esptool can't connect, put it in download mode manually: hold **BOOT**, tap **RST**, release **BOOT**.

### Set up your config

**Option 1: Config Tool (recommended)**

1. Open the [MCLite Config Tool](https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html) in any web browser (or use the local file at `tools/config-tool/mclite_config_tool.html`). It works offline -- no internet needed, nothing to install.
2. The **Setup Wizard** opens automatically and walks you through the essentials: device name, region, key pair, and default channels. For more detailed settings (telemetry, GPS format, PIN lock, etc.), use the full editor after completing the wizard.
3. Click **Export** to download `mclite-config.zip` (includes config and language files)
4. Unzip and copy all contents to the root of your SD card
5. Insert the SD card and power on -- done

To set up a group: use **Fleet Mode** in the Setup Wizard. Add a device for each person in your group, generate keys for all, then export -- the tool creates a ZIP with a folder per device, each containing `config.json` and language files. Copy each folder's contents to the corresponding SD card and you're done.

**Option 2: Blank SD card (quick start)**

1. Insert a blank FAT32-formatted SD card (max 32 GB) and power on
2. MCLite auto-generates a unique identity and creates a default config with the Public and #mclite channels
3. The device is immediately functional -- you can send and receive on both channels
4. To add contacts, channels, or change settings (language, radio preset, etc.): open the Config Tool, click **Import** to load the auto-created `config.json` from your SD card, make your changes, click **Export**, and copy the updated file back to the SD card

<details>
<summary><strong>Example config.json</strong> (click to expand)</summary>

```jsonc
{
  // Language: "" = English (default), "de" = German, "fr" = French, "it" = Italian
  // Add your own: place <code>.json in /mclite/lang/ on the SD card
  "language": "",

  "device": {
    "name": "Alice"                    // Display name shown in status bar and to other devices
  },

  // Radio settings — all devices in your group MUST use the same values
  // Use the Config Tool's preset buttons to fill these automatically
  "radio": {
    "frequency": 869.618,              // MHz — EU/UK/CH: 869.618, US/Canada: 910.525
    "spreading_factor": 8,             // EU: 8, US: 7 — higher = more range, slower
    "bandwidth": 62.5,                 // kHz — 62.5 for both presets
    "tx_power": 22,                    // dBm (1-22) — transmission power
    "coding_rate": 8,                  // EU: 8, US: 5 — error correction level
    "scope": "*",                      // Region scope: "*" = no filtering, "#name" = hashtag region (e.g. "#europe")
    "path_hash_mode": 0                // 0 (default, 1B/hop), 1 (2B/hop), 2 (3B/hop). Larger = fewer collisions in dense meshes but more airtime per hop. Keep 0 for compatibility with pre-v1.15 peers.
  },

  // Your identity — generated by the Config Tool (do NOT share your private key)
  "identity": {
    "private_key": "...your private key (hex)...",
    "public_key": "...your public key (hex)..."
  },

  // Contacts — people you can send direct messages to
  "contacts": [
    {
      "alias": "Bob",                  // Display name
      "public_key": "...hex...",       // Their public key (from their config)
      "allow_telemetry": true,         // Base telemetry permission (must be true for location/environment to work)
      "allow_location": false,         // Let them request your GPS location (requires allow_telemetry)
      "allow_environment": false,      // Let them request environment sensor data (requires allow_telemetry)
      "always_sound": false,           // Play sound even when device is muted
      "allow_sos": true,               // Show SOS alerts from this contact
      "send_sos": true,                // Include in your outgoing SOS broadcast
      "canned": ["Open gate", "Lights on", "Status?"]  // Optional per-conversation quick replies — overrides the
                                       //   global list for THIS chat (max 8). Works on contacts, channels and rooms.
                                       //   Great as a command menu for a Home Assistant / automation bridge. Omit = use global.
    }
  ],

  // Channels — group communication
  "channels": [
    {
      "name": "Public",                // Well-known MeshCore public channel (all devices use this key)
      "type": "public",                // "public" = hardcoded PSK, "hashtag" = open, "private" = user-provided PSK
      "psk": "8b3387e9c5cdea6ac9e5edbaa115cd72",
      "index": 0,
      "allow_sos": false,               // Public channel: ignore SOS from strangers
      "send_sos": false,               // Public channel: SOS off by default (readable by everyone)
      "read_only": true                // Listen-only mode — hides the input bar
    },
    {
      "name": "#mclite",             // Hashtag channels: lowercase only (a-z, 0-9, -), shared by name
      "type": "hashtag",
      "index": 1,
      "allow_sos": true,
      "send_sos": false              // Hashtag default false — community channels shouldn't get SOS spam
    },
    {
      "name": "Team Alpha",
      "type": "private",               // Private channel — PSK shared out-of-band with group members
      "psk": "a1b2c3d4e5f6a7b8a1b2c3d4e5f6a7b8",
      "index": 2,
      "allow_sos": true,
      "send_sos": true,                // Private channel default true (trusted small group)
      "scope": "#local"                // Override global scope for this channel (omit or "" = inherit global)
    }
  ],

  // Room servers — community message boards run by MeshCore room servers (max 8)
  "room_servers": [
    {
      "name": "Ruhrgebiet",            // Display name shown in conversation list
      "public_key": "...64-hex...",    // Server's Ed25519 public key (shared out-of-band)
      "password": "secret",            // Up to 15 chars; "" = public room
      "allow_sos": true,               // Trigger SOS alert on posts starting with the SOS keyword
      "send_sos": false,               // Include this room in your outgoing SOS broadcast (default off — community rooms shouldn't be spammed)
      "read_only": false,              // Listen-only mode — hides the input bar
      "scope": ""                      // Per-room scope override (omit or "" = inherit global)
    }
  ],

  "display": {
    "brightness": 180,                 // 0-255
    "theme": "dark",                   // UI palette: "dark" (default), "light", "amber", "high_contrast", or a custom name (see "themes")
    "auto_dim_seconds": 30,            // Dim screen after N seconds of inactivity (0 = off)
    "dim_brightness": 0,               // Brightness when dimmed (0 = screen off, default)
    "boot_text": "",                   // Optional text shown on boot screen (e.g. team name)
    "kbd_backlight": true,             // Keyboard backlight on/off with auto-dim
    "kbd_brightness": 127,             // Keyboard backlight brightness (1-255)
    "emoji": true,                     // Show the chat emoji picker (received emoji always render). Default on.
    "menu_button": false,              // T-Deck only: status-bar gear that opens Admin. Default off (use the "0" key). Hidden when security.admin_enabled is false.

    // Optional custom palettes. Select one by putting its "name" in "theme" above.
    // Each entry starts from "base" (a built-in) and overrides any of the color
    // keys with "#RRGGBB". Unspecified keys inherit from the base.
    "themes": [
      {
        "name": "my-theme",            // selectable name (use it in "theme")
        "base": "dark",                // built-in to start from: dark | light | amber | high_contrast
        "accent": "#FF8C00",           // override any keys: bg_primary, bg_secondary, bg_status_bar,
        "bg_primary": "#101018"        //   bg_input, text_primary, text_secondary, text_timestamp,
      }                                //   bubble_self, bubble_self_meta, bubble_them, bubble_self_text,
    ]                                  //   accent, unread_dot, online_dot, battery_low, battery_ok,
  },                                   //   gps_last_known, offgrid_accent, room_accent, scrim, text_on_accent

  "messaging": {
    "save_history": true,              // Save messages to SD card
    "max_history_per_chat": 100,       // Max messages stored per conversation
    "location_format": "decimal",       // "decimal" = lat/lon, "mgrs" = UTMREF/MGRS, "both"
    "max_retries": 3,                  // DM delivery retry attempts (1-5)
    "request_telemetry": true,         // Telemetry button in a chat shows battery/location (optional)
    "show_telemetry": "both",          // Badges on convo list: "battery", "location", "both", "none"
    "auto_telemetry": false,           // Auto-refresh GPS of contacts who don't broadcast it, to keep map
                                       //   markers fresh. Throttled + self-limiting (backs off non-responders). Opt-in, off by default.
    "canned_messages": true,           // Quick-reply picker: true = on (default messages, default), false = off,
                                       //   or ["Reply 1", "Reply 2"] = on with custom messages (max 8).
                                       //   Editable on-device: Admin -> Canned Messages (or Settings -> Messaging).
                                       //   Per-conversation lists are editable on-device too:
                                       //   Admin -> Contacts/Channels/Rooms -> pick one -> Quick Replies.
                                       //   This is the GLOBAL list; individual contacts/channels/rooms can override
                                       //   it with their own "canned" array (see above). Per-conversation lists win.
    "share_contact": true,             // Share button in a DM chat header re-broadcasts that contact's advert so
                                       //   a nearby device can add them from Heard Adverts (no key typing). On by default.
                                       //   Also on-device: Settings -> Messaging -> Share Contact.
    "reactions": false                 // Long-press a chat bubble to react with an emoji (MeshCore One spec).
                                       //   ONLY MeshCore One and MCLite understand reactions -- on any other MeshCore
                                       //   client a reaction arrives as a plain text message (emoji + 8-char code).
                                       //   Off by default. On-device: Settings -> Messaging -> Reactions.
  },

  "sound": {
    "enabled": true,                   // Master sound toggle
    "sos_keyword": "SOS",              // Keyword that triggers SOS alert sound
    "sos_repeat": 3                    // How many times to repeat SOS alarm (1-10)
  },

  "gps": {
    "enabled": true,
    "timezone": "",                    // POSIX TZ string for automatic DST (e.g. "CET-1CEST,M3.5.0/2,M10.5.0/3")
    "clock_offset": 0,                 // UTC offset in hours, no DST (-12 to +14). Ignored if timezone is set
    "last_known_max_age": 1800,        // Seconds before last-known GPS position expires (60-7200)
    "location_precision": 0            // Location in adverts: 0=off (default), 32=exact, or a privacy grid (19≈100m, 16≈750m, 14≈3km, 12≈12km, 10≈50km). Unencrypted; telemetry replies + in-chat GPS stay exact
  },

  "battery": {
    "low_alert_enabled": true,         // Send automatic low-battery alert to contacts
    "low_alert_threshold": 15          // Battery % that triggers the alert (5-50)
  },

  "security": {
    "lock": "key",                     // "none" = no lock, "key" = key lock (1s hold), "pin" = PIN lock
    "pin_code": "",                    // PIN code (4-8 alphanumeric characters, only for "pin" mode)
    "auto_lock": "key",                // Lock on display dim: "none", "key", "pin" (falls back if unavailable)
    "admin_enabled": true              // Allow access to device info screen (press 0)
  },

  // Optional. Only written when an SSID is set, so it is absent on devices that
  // never use WiFi. Also editable on-device: Admin -> WiFi.
  "wifi": {
    "ssid": "",                        // Network to auto-connect to on boot
    "password": "",                    // Stored in plain text on the SD card
    "auto_update": false               // Check GitHub for a newer release on boot and offer to install it.
                                       //   Off by default. On-device: Admin -> WiFi -> Auto Update.
  }
}
```

You don't need to write this by hand -- use the Config Tool. This example shows all available options with their defaults.

</details>

### Map tiles (optional)

If you want a visual map view (the **map button** in a chat header, or a contact's telemetry pop-up → **Map**), copy slippy map tiles to `/tiles/` on the SD card root:

```
/tiles/<zoom>/<x>/<y>.png    (256×256 PNG, Web Mercator / EPSG:3857)
```

This is the same layout used by MeshCore's official T-Deck firmware, so any existing tile pack works unchanged. Zoom levels are auto-detected from the folder names present.

**Getting tiles**: [map-tiles-downloader](https://github.com/tekk/map-tiles-downloader) is a terminal tool (TUI) that produces exactly this layout -- pick a country/region, zoom range, tile source, and export.

Region codes in that tool are GeoNames admin1 codes, not the local administrative numbers you may be familiar with. For Germany, a few common ones: `02` = Bayern, `04` = Hamburg, `07` = Nordrhein-Westfalen, `16` = Berlin. Check the [GeoNames admin1 codes list](https://download.geonames.org/export/dump/admin1CodesASCII.txt) for other countries.

### Updating the firmware (on-device, no USB)

Once MCLite is installed you can update it without a computer:

- **From SD card** -- copy a newer merged binary (`mclite-v*.bin` for the T-Deck Plus, `mclite-watch-v*.bin` for the T-Watch Ultra) to the SD card. On the next boot the device detects it and offers **Install / Cancel**, then flashes and reboots. The file is renamed afterwards so it won't re-prompt.
- **Over WiFi** -- on the device go to **Admin → WiFi**, switch WiFi on, pick your network and enter the password (saved for next time), then tap **Check for updates**. If a newer release exists on GitHub it downloads and installs. Enable **auto-update** (config tool → WiFi, or it checks on boot when on) to be prompted automatically.

## Companion mode

Use a phone, desktop, or CLI as a companion to the radio while the device keeps working normally — received and app-sent messages appear in **both** places at once. (One exception: messages you type *on the device* don't sync to the app — see the **Important limitation** note under Notes below.) MCLite speaks the standard MeshCore companion protocol over three transports (**one active at a time**): **Bluetooth**, **WiFi**, and **USB**. A companion can send/receive messages and **manage conversations** — add, rename, and remove contacts; add and remove channels; log into rooms — gated by `permissions.conversation_management`. With `permissions.settings` set to `full` it can also change **device settings** — device name, radio parameters (frequency/SF/BW/CR), TX power, BLE pairing PIN, path-hash mode, and region/scope (the last two handy for matching a large 3-byte mesh). The **identity/keys**, advert **location** (MCLite advertises GPS at the configured precision), and **auto-add** stay device-managed by design and are refused.

### Bluetooth (official mobile apps)

The way to use the official MeshCore **iOS / Android** apps.

1. On the device: **Admin → Bluetooth**. The screen shows a 6-digit **pairing PIN** (generated once and saved).
2. Turn the **Bluetooth Companion** switch on — the device advertises as `MeshCore-<name>`.
3. In the app: scan, pick this device, and enter the PIN when prompted. It bonds once and reconnects automatically.

The Bluetooth status-bar icon turns **green** while a client is connected.

### WiFi (desktop / CLI on your LAN)

1. On the device: **Admin → WiFi**, switch WiFi **on** and connect to your network.
2. Turn on the **WiFi Companion** switch (enabled once WiFi is connected). The row shows `Companion <ip>:5000`.
3. From a computer on the same network:
   ```
   pip install meshcore-cli
   meshcore-cli -t <device-ip> -p 5000 infos      # or: contacts, recv, chan_msg, etc.
   ```
   (`meshcore.js` and `meshcore_py` work too.) The status-bar WiFi icon turns **green** while a client is attached. Note: the WiFi transport has **no pairing/auth** (the protocol's only auth is the Bluetooth passkey) — only enable it on networks you trust.

### USB (wired, computer)

Turn on the **USB Companion** switch (**Admin → USB**; works with WiFi off), then connect over the USB-CDC port:
```
meshcore-cli -s /dev/ttyACM0 infos
```
While USB companion is active the device's **serial debug logging is muted** — the binary protocol and log text can't share the one USB port (there's no spare log UART on these boards). Logs resume the moment you turn it off. The charge bolt turns **green** while a USB client is bridging.

### Notes

- **One transport, one client at a time** — the modes are mutually exclusive by design (the protocol is single-session). Turning one on turns the others off.
- **WiFi vs Bluetooth can't run together** — they share the 2.4 GHz radio and there isn't enough RAM for both. Enabling Bluetooth turns WiFi off; once Bluetooth has been used, **switching back to WiFi needs a reboot** (the BLE stack can't be freed at runtime). The WiFi screen shows a notice and a **Reboot** button when this applies.
- **Conversation management** — one consistent rule for both contacts and channels: **adding and editing apply instantly** (the companion session stays connected, and the change shows on the device's own conversation list and Admin screens right away), while **removing reboots the device** to apply (the app reconnects on its own). The reboot on removal is required for channels — MeshCore offers no way to drop a channel from the running radio, so MCLite rebuilds its channel table from config at boot — and is kept for contact removal too so the behaviour is uniform. Editing a contact from the app changes its **display name** only; per-contact permissions stay in the config file / on-device Admin.

> [!IMPORTANT]
> **Messages you type _on the device_ do not appear in the companion app.** Sending from the device **still works**: the message goes out over the mesh and others receive it normally — it just won't show up in the app. This is a hard constraint of the MeshCore companion protocol, not an MCLite bug: its sync stream carries only **received** messages, and there is **no frame for an outgoing/firmware-composed message** (stock MeshCore assumes the app is the sole composer — verified against the 1.16 reference firmware, whose sync queue is fed exclusively by receive callbacks). A direct message you typed on the device therefore can't be shown as "sent" in the app at all, so it is left out rather than mislabelled as incoming. **If you want a complete, correctly-sided history in the app, compose your messages from the app.** Everything else mirrors both ways: messages you **receive**, and messages you **send from the app**, appear on the device *and* in the app.

## Hints

**Controls & shortcuts**

The T-Deck Plus is keyboard + trackball driven; the T-Watch Ultra is touch driven with two physical buttons (lower = BOOT, upper = PWR). Touch shortcuts work the same on both.

| Action | T-Deck Plus | T-Watch Ultra |
|--------|-------------|---------------|
| Admin / device info | Sym + 0, or tap the gear in the status bar (see `display.menu_button`) | Short-press upper (PWR) button |
| Back to the conversation list | Esc | Swipe right |
| Open the Nth conversation | Sym + 1 ... 9, from the conversation list | Tap the conversation |
| Lock / unlock | Hold trackball 1s | Hold lower (BOOT) button 1s |
| SOS broadcast | Hold trackball 6s | Hold lower (BOOT) button 6s |
| Power off | Slide the power switch | Long-press upper (PWR) button |
| Mute / unmute | Tap speaker icon in status bar | Same |
| Contact telemetry | Tap the telemetry (refresh) button in chat header | Same |
| Share a contact | Tap the share (↑) button in a DM chat header | Same |
| Retry failed message | Tap the X on a failed message | Same |
| Quick reply | Tap the list icon (≡) by the text input | Same |
| Screenshot (when `debug.screenshots` is on) | Shift + $ | Double-press upper (PWR) button |

**Conversation list icons**

| Symbol | Meaning |
|--------|---------|
| @ | Direct message (contact) |
| # | Public or hashtag channel |
| * | Private channel |
| R | Room server (purple) |
| Green eye | Contact recently seen on the mesh |
| Green dot | Unread messages |
| Battery / GPS | Contact's last reported telemetry |

**Status bar**

| Icon | Meaning |
|------|---------|
| GPS | Green = live fix, amber = last known, gray = no fix, dimmed = GPS disabled. **Tap to open the map** |
| Battery | Charge level; charging symbol when plugged in |

**Map view** (needs offline tiles on the SD card -- see [Map tiles](#map-tiles-optional))

Open it by tapping the **GPS icon** in the status bar (works even when GPS is off -- the icon stays dimmed but tappable), or from a contact's telemetry pop-up via **Map** (which opens the same map centered on that contact, with it highlighted). It's one map either way -- it always shows every node whose location we know: contacts (from a telemetry request or their advert), heard nodes, and your own position.

Markers use the same letters and colors as the Heard Adverts list, drawn as a filled dot with the symbol on top:

| Symbol | Meaning |
|--------|---------|
| @ (blue) | Chat client that's a saved contact |
| @ (grey) | Chat client heard on the mesh (not a contact) |
| P | Repeater |
| R | Room server |
| S | Sensor |
| Green / amber dot | **Your own** position (green = live fix, amber = last known) -- distinct from node markers |

- **Tap a marker** to show its name in the bottom bar; the selected one gets a ring and a slightly larger dot.
- **Center** (GPS button) centers on your own location as soon as a fix is available; with no fix it returns to where the map opened (the contact, or a nearby node).
- **Reload** (↻) re-scans all node locations and re-checks your own, without moving the view.
- **Drag** to pan, **+ / −** to zoom.

**Message delivery indicators**

| Icon | Meaning |
|------|---------|
| Single check | Message sent, waiting for acknowledgement |
| Double check | Message delivered (acknowledged by recipient) |
| X | Delivery failed -- tap to retry |

**SOS keyword** -- incoming messages containing "SOS" (default) trigger an alert sound. The same keyword is sent via the trackball long-press SOS. Best left unchanged so all devices recognize each other's alerts.

## Radio Compatibility

MCLite uses MeshCore for radio communication and is compatible with other MeshCore devices. Built-in radio presets:

- **EU/UK/CH**: 869.618 MHz, SF8, BW62.5, CR8, 22 dBm
- **US/Canada**: 910.525 MHz, SF7, BW62.5, CR5, 22 dBm
- **Australia Wide**: 915.800 MHz, SF10, BW250, CR5, 22 dBm
- **Australia Victoria**: 916.575 MHz, SF10, BW250, CR5, 22 dBm

All devices in your group must use the same radio settings. Compatible with both 1-byte and 2-byte repeaters.

### Regulatory Compliance

**EU/UK/CH (868 MHz)**: MCLite enforces the ETSI EN 300 220 duty cycle limit for the G3 sub-band (869.40–869.65 MHz). TX duty cycle is capped at 10% through MeshCore's airtime budget mechanism. The Admin screen shows real-time channel utilization so you can verify compliance.

**US/Canada (915 MHz)**: The US ISM band (902–928 MHz) has no duty cycle restriction under FCC Part 15.247. MCLite uses the MeshCore default airtime budget (33%).

**Australia (915 MHz)**: The 915–928 MHz ISM band is available under ACMA class licence (LIPD-2015). MCLite uses the MeshCore default airtime budget (33%).

> **Disclaimer**: MCLite is experimental open-source software, not a certified radio product. The firmware-level duty cycle cap is provided as a best-effort aid, not a guarantee of regulatory compliance. The operator is solely responsible for ensuring that their complete setup (firmware, hardware, antenna, configuration) complies with all applicable local laws and regulations. The authors accept no liability for non-compliance or any consequences arising from the use of this software.

## License

MCLite firmware is released under the **MIT License**. See [LICENSE](LICENSE) for details.

It builds on several open-source libraries and assets (MeshCore, LVGL, LovyanGFX, ArduinoJson, RadioLib, TinyGPSPlus, the Arduino ESP32 core, OpenMoji, and more). The full list — with versions and copyright notices — is in [LICENSES.md](LICENSES.md). The LGPL-2.1 libraries (TinyGPSPlus, Arduino ESP32 core) are dynamically linked and may be replaced by rebuilding with PlatformIO.

## Contributing

Contributions are welcome. Please open an issue or pull request on GitHub.

## Embed on your site

Help spread MCLite by linking to it from your own page. Drop one of these snippets into any HTML — the buttons render identically across light and dark themes and require no external CSS or fonts.

<p align="center">
  <a href="https://laserir.github.io/MCLite/tools/web-flasher/"><img src="docs/images/btn-flash.svg" alt="Flash MCLite now" height="48"></a>
  &nbsp;
  <a href="https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html"><img src="docs/images/btn-config.svg" alt="Open Config Tool" height="48"></a>
  &nbsp;
  <a href="https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html#preset="><img src="docs/images/btn-group-preset.svg" alt="Open MCLite Group Preset" height="48"></a>
  &nbsp;
  <a href="https://github.com/laserir/MCLite"><img src="docs/images/btn-github.svg" alt="View MCLite on GitHub" height="48"></a>
</p>

```html
<!-- Flash button -->
<a href="https://laserir.github.io/MCLite/tools/web-flasher/">
  <img src="https://raw.githubusercontent.com/laserir/MCLite/main/docs/images/btn-flash.svg" alt="Flash MCLite now" height="48">
</a>

<!-- Config tool button -->
<a href="https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html">
  <img src="https://raw.githubusercontent.com/laserir/MCLite/main/docs/images/btn-config.svg" alt="Open Config Tool" height="48">
</a>

<!-- GitHub button -->
<a href="https://github.com/laserir/MCLite">
  <img src="https://raw.githubusercontent.com/laserir/MCLite/main/docs/images/btn-github.svg" alt="View MCLite on GitHub" height="48">
</a>
```

### Pre-filled preset links for local groups

Running a regional MCLite group? You can publish a **preset link** that opens the config tool with your channels, radio settings, contacts, boot text, and more already filled in — visitors only need to add their own identity before exporting.

1. Open the [Config Tool](https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html) and configure the settings you want to share.
2. Choose **Share preset link…** from the ⋯ menu (top-right).
3. Tick the sidebar sections to include — `Device` (name, identity keys) and `Security` (PIN, lock mode) are always excluded.
4. Copy the generated URL and embed it on your site, just like the Config Tool button:

```html
<!-- Pre-filled config link (replace the # fragment with your generated preset) -->
<a href="https://laserir.github.io/MCLite/tools/config-tool/mclite_config_tool.html#preset=YOUR_BASE64_PAYLOAD">
  <img src="https://raw.githubusercontent.com/laserir/MCLite/main/docs/images/btn-group-preset.svg" alt="Open MCLite Group Preset" height="48">
</a>
```

The preset is encoded entirely in the URL fragment (`#preset=…`), so it's never sent to a server. Opening the link generates a fresh keypair and overlays your shared settings on top — the visitor reviews them in the **Preset applied** banner before exporting.
