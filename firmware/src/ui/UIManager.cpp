#include "UIManager.h"
#include "util/log.h"
#include "theme.h"
#include "ModalDialog.h"
#include "../util/MsgHash.h"
#include "../util/ReactionParse.h"
#include "../mesh/MeshManager.h"
#include "../mesh/ContactStore.h"
#include "../mesh/ChannelStore.h"
#include "../hal/Display.h"
#include "../hal/GPS.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "../hal/IInput.h"
#include "../hal/Speaker.h"
#include "../hal/Battery.h"
#include "../i18n/I18n.h"
#include "../storage/TelemetryCache.h"
#include "../storage/TileLoader.h"
#include "../util/ContactLocation.h"
#include "Screenshot.h"
#ifdef PLATFORM_TWATCH
#include "../hal/twatch/Pmu.h"
#include "../hal/twatch/Haptic.h"
#endif
#include "../util/distance.h"
#include "../util/version.h"
#include "../ota/FirmwareUpdater.h"
#include "../ota/UpdateChecker.h"
#include "../net/WiFiManager.h"
#include "../util/hex.h"
#include "../util/mgrs.h"
#include "../util/TimeHelper.h"
#include <helpers/BaseChatMesh.h>  // RESP_SERVER_LOGIN_OK

namespace mclite {

// Defined below; forward-declared so the telemetry-timeout path (in update())
// can rebuild the modal body with any fallback (advert/heard) position.
static String buildTelemText(const Contact* contact, const TelemetryData* td);

UIManager& UIManager::instance() {
    static UIManager inst;
    return inst;
}

bool UIManager::init() {
    // Create a new screen for main UI (boot screen may still be active)
    _mainScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_mainScreen, theme::BG_PRIMARY(), 0);

    // Create LVGL input group and bind input devices
    _inputGroup = lv_group_create();
    lv_group_set_default(_inputGroup);
    IInput::instance().attachToGroup(_inputGroup);

    // Create all UI components
    _statusBar.create(_mainScreen);
    _convoList.create(_mainScreen);
    _chatScreen.create(_mainScreen);
    _adminScreen.create(_mainScreen);
    _settingsScreen.create(_mainScreen);
    _heardAdvertsScreen.create(_mainScreen);
    _wifiSetupScreen.create(_mainScreen);
    _usbSetupScreen.create(_mainScreen);
    _bleSetupScreen.create(_mainScreen);

    // Wire up callbacks
    _convoList.onSelect([this](const ConvoId& id) {
        openChat(id);
    });

    _chatScreen.onSend([this](const ConvoId& id, const String& text) {
        handleSend(id, text);
    });

    _chatScreen.onReact([this](const ConvoId& id, const String& wireText) {
        handleReaction(id, wireText);
    });

    _chatScreen.onRetry([this](const ConvoId& id, const String& text, uint32_t oldPacketId) {
        handleRetry(id, text, oldPacketId);
    });

    _chatScreen.onBack([this]() {
        goHome();
    });

    _chatScreen.onMap([this](const ConvoId& id) {
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                ContactLocation loc = bestKnownLocation(c->publicKey);
                if (loc.valid) openMapAt(c->publicKey, loc.lat, loc.lon, c->name);
                break;
            }
        }
    });

    _chatScreen.onTelem([this](const ConvoId& id) {
        showTelemetryModal(id);
    });

    _chatScreen.onShare([this](const ConvoId& id) {
        // Re-broadcast the contact's signed advert at zero hop so nearby nodes
        // can add it from their Heard Adverts.
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                bool ok = MeshManager::instance().shareContact(c->publicKey);
                showToast(ok ? t("toast_shared") : t("toast_share_fail"));
                return;
            }
        }
        showToast(t("toast_share_fail"));
    });

    _convoList.onMute([this](const ConvoId& id, bool muted) {
        showToast(muted ? t("toast_muted") : t("toast_unmuted"));
        // If currently viewing this chat, refresh the header mute indicator
        if (_currentScreen == Screen::CHAT && _chatScreen.currentConvo() &&
            *_chatScreen.currentConvo() == id) {
            _chatScreen.open(id);  // re-open to refresh mute icon
        }
    });

    _chatScreen.onMute([this](const ConvoId& id, bool muted) {
        showToast(muted ? t("toast_muted") : t("toast_unmuted"));
        // Refresh convo list so the mute indicator appears there too
        if (_currentScreen == Screen::CONVO_LIST) {
            _convoList.refresh();
        }
    });

    _lastActivity = millis();

    // Turn on keyboard backlight if enabled
    const auto& initCfg = ConfigManager::instance().config();
    if (initCfg.display.kbdBacklight) {
        IInput::instance().setBacklight(initCfg.display.kbdBrightness);
    }

    // Do NOT show any screen yet — loadMainScreen() will do that after boot
    LOGLN("[UI] Initialized");
    return true;
}

void UIManager::update() {
    uint32_t now = millis();

    // LVGL tick handler (runs indev readCb which sets _lastKey)
    lv_timer_handler();

    // Check for input activity to wake from dim (after LVGL so _lastKey is fresh)
    checkWake();

#ifdef PLATFORM_TWATCH
    // Upper power button (AXP2101 PEK) short-press: toggle Admin <-> home.
    // Long-press remains a hardware shutdown via the PMU itself.
    // Suppressed while the screen is key-locked or PIN-locked so the lock
    // can't be bypassed to reach Admin.
    // consumeShortPress() clears the PMU IRQ as a side effect, so it must be read
    // exactly once per loop and never short-circuited away by a later condition --
    // doing that swallowed the press and killed the double-press screenshot
    // whenever a PIN prompt was up.
    const bool pekShortPress = Pmu::instance().consumeShortPress();
    if (pekShortPress && !_keyLocked) {
        const auto& sec = ConfigManager::instance().config().security;
        // Admin toggle only when nothing is already asking for a PIN; the lock must
        // not be bypassable to reach Admin.
        if (!_isLocked) {
            if (_currentScreen == Screen::ADMIN)      goHome();
            else if (sec.adminEnabled)                showScreen(Screen::ADMIN);
            else if (sec.adminPin.length() >= 4)      showPinLock(PinPurpose::AdminUnlock);
        }
        _lastActivity = now;
        // Double short-press → screenshot (gated by debug.screenshots). Deliberately
        // outside the Admin gate: it has to keep working whatever the admin state,
        // including with the PIN prompt showing, which is the case that used to fail.
        // Single press stays instant (no added latency).
        if (now - _pekLastMs < 500 && ConfigManager::instance().config().debug.screenshots) {
            Screenshot::capture();
        }
        _pekLastMs = now;
    }
#endif

    // Tick the failed-PIN countdown so the user sees it running down rather than a
    // frozen number. Only relabels when the whole second changes.
    if (_isLocked && _pinStatus &&
        ((_pinPurpose == PinPurpose::ScreenUnlock) ? _pinWaitUntil : _adminWaitUntil) != 0) {
        uint32_t left = pinWaitRemaining();
        if (left == 0) {
            if (_pinPurpose == PinPurpose::ScreenUnlock) _pinWaitUntil = 0; else _adminWaitUntil = 0;
            _pinWaitShown = 0;
            lv_obj_set_style_text_color(_pinStatus, theme::TEXT_SECONDARY(), 0);
            lv_label_set_text(_pinStatus, "");
        } else if ((uint8_t)left != _pinWaitShown) {
            _pinWaitShown = (uint8_t)left;
            char buf[32];
            snprintf(buf, sizeof(buf), t("pin_wait"), (int)left);
            lv_label_set_text(_pinStatus, buf);
        }
    }

    // Periodic status bar update
    if (now - _lastStatusUpdate >= STATUS_UPDATE_MS) {
        _statusBar.update();
        // Re-evaluate the chat header map button so it appears when an advert
        // brings in a position and disappears when a last-known fix ages out of
        // its freshness window (bestKnownLocation is time-sensitive).
        if (_currentScreen == Screen::CHAT) refreshChatHeaderButtons(false);  // map only; share is evaluated on open
        _lastStatusUpdate = now;
    }

    // Auto-dim check — re-read millis() since checkWake() may have updated _lastActivity
    uint32_t nowDim = millis();
    const auto& cfg = ConfigManager::instance().config();
    if (cfg.display.autoDimSeconds > 0) {
        uint32_t dimTimeout = cfg.display.autoDimSeconds * 1000;
        if (nowDim - _lastActivity > dimTimeout && !_dimmed) {
            Display::instance().setBrightness(cfg.display.dimBrightness);
            if (cfg.display.kbdBacklight) {
                IInput::instance().setBacklight(0);
            }
            _dimmed = true;
            // Auto-lock on dim — fallback chain: pin → key → none.
            // Skip when on the setup screen so a missing-SD device doesn't
            // lock itself before the user can recover it.
            const auto& sec = cfg.security;
            if (_inSetupMode) {
                // dim only
            } else if (sec.autoLock == "pin" && sec.lockMode == "pin" && sec.pinCode.length() >= 4 && !_isLocked) {
                showPinLock();
            } else if (sec.autoLock == "pin" && sec.lockMode == "key" && !_isLocked && !_keyLocked) {
                // Fallback: PIN auto-lock requested but only key lock available
                _keyLocked = true;
                showKeyLockOverlay();
                LOGLN("[UI] Key lock engaged (auto-dim, pin fallback)");
            } else if (sec.autoLock == "key" && (sec.lockMode == "key" || sec.lockMode == "pin") && !_isLocked && !_keyLocked) {
                _keyLocked = true;
                showKeyLockOverlay();
                LOGLN("[UI] Key lock engaged (auto-dim)");
            }
        }
    }

    // Telemetry request timeout
    if (_telemPending && _telemMsgbox && (int32_t)(now - _telemTimeout) >= 0) {
        _telemPending = false;
        _telemTimeout = 0;
        MeshManager::instance().clearPendingTelemetry();
        // No telemetry reply — but still show any known (advert/heard) position
        // instead of a bare "No response".
        const Contact* c = nullptr;
        auto& cs = ContactStore::instance();
        for (size_t i = 0; i < cs.count(); i++) {
            const Contact* cc = cs.findByIndex(i);
            if (cc && cc->shortId() == _telemContactId) { c = cc; break; }
        }
        String body = c ? buildTelemText(c, TelemetryCache::instance().get(c->publicKey))
                        : String();
        if (body.length() && body != t("telem_no_data")) {
            _telemText = String(t("telem_no_response")) + "\n" + body;
        } else {
            _telemText = t("telem_no_response");
        }
        ModalDialog::setBody(_telemMsgbox, _telemText);
    }

    // Periodic convo list refresh (update timestamps like "12s", "3m")
    if (_currentScreen == Screen::CONVO_LIST && now - _lastConvoRefresh >= CONVO_REFRESH_MS) {
        _convoList.refresh();
        _lastConvoRefresh = now;
    }

    // Live updates for heard-adverts list and admin's heard-count row.
    // Both no-op cheaply when not visible / version unchanged.
    _heardAdvertsScreen.tick();
    _adminScreen.tick();
    _settingsScreen.tick();  // live Heard-Adverts count on the Radio section
    _wifiSetupScreen.tick();
    _usbSetupScreen.tick();
    _bleSetupScreen.tick();

    // Room login tick (boot path with backoff). No-op for already-logged-in rooms.
    roomLoginTick();

    // Decision #15 — if user is sitting on a ROOM chat, fire a silence-triggered
    // re-login at most every 10 min when no signed-room message has arrived.
    if (_currentScreen == Screen::CHAT && _chatScreen.currentConvo() &&
        _chatScreen.currentConvo()->type == ConvoId::ROOM) {
        const auto& rooms = ConfigManager::instance().config().roomServers;
        const String& shortId = _chatScreen.currentConvo()->id;
        for (size_t i = 0; i < rooms.size() && i < MAX_ROOMS; i++) {
            if (rooms[i].publicKey.length() == 64 &&
                rooms[i].publicKey.substring(0, 16) == shortId) {
                roomSilenceTick(i);
                break;
            }
        }
    }

    // History is saved automatically on each message send/receive in MessageStore::addMessage()
}

void UIManager::checkWake() {
    bool activity = false;

    if (IInput::instance().pollKey() != 0) {
        activity = true;
    }

    if (IInput::instance().isPressed() || IInput::instance().hasMoved()) {
        activity = true;
    }

    if (IInput::instance().isTouched()) {
        activity = true;
    }

    if (!activity) return;

    // Any input resets the dim timer
    _lastActivity = millis();

    // Wake display if dimmed
    if (_dimmed) {
        const auto& dispCfg = ConfigManager::instance().config().display;
        Display::instance().setBrightness(dispCfg.brightness);
        if (dispCfg.kbdBacklight) {
            IInput::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
        // Consume the keyboard wake key so it doesn't pass through
        if (!_isLocked && IInput::instance().pollKey() != 0) {
            IInput::instance().clearKey();
        }
    }
}

void UIManager::loadMainScreen() {
    lv_scr_load(_mainScreen);
    showScreen(Screen::CONVO_LIST);
    lv_timer_handler();
}

void UIManager::showGlobalCannedList() {
    _settingsScreen.openCannedList(SettingsScreen::CannedTarget::Global, -1);
}

void UIManager::showSettings(SettingsSection s) {
    _settingsScreen.setSection(s);
    showScreen(Screen::DEVICE_SETTINGS);
}

void UIManager::showScreen(Screen screen) {
    // Central admin gate. Six routes reach Screen::ADMIN -- the '0' key, the
    // status-bar gear, the T-Watch PEK short-press, and the Back buttons in
    // Settings / Heard Adverts / WiFi / BLE / USB -- and historically only the
    // first two checked admin_enabled. Gating here covers all of them and any
    // future entry point, rather than relying on every caller to remember.
    // Matters most for the Back buttons: with admin_enabled now toggleable at
    // runtime, being inside a sub-screen when it flips would otherwise walk
    // straight back into Admin.
    if (screen == Screen::ADMIN &&
        !ConfigManager::instance().config().security.adminEnabled) {
        goHome();
        return;
    }

    // Dismiss telemetry modal if open (it's a top-level overlay)
    if (_telemMsgbox) dismissTelemetryModal();

    _convoList.hide();
    _chatScreen.hide();
    _adminScreen.hide();
    _settingsScreen.hide();
    _heardAdvertsScreen.hide();
    _wifiSetupScreen.hide();
    _usbSetupScreen.hide();
    _bleSetupScreen.hide();

    switch (screen) {
        case Screen::CONVO_LIST:
            _convoList.show();
            _lastConvoRefresh = millis();
            break;
        case Screen::CHAT:
            // show() deferred to open() which calls it after setup
            break;
        case Screen::ADMIN:
            _adminScreen.show();
            break;
        case Screen::DEVICE_SETTINGS:
            _settingsScreen.show();
            break;
        case Screen::HEARD_ADVERTS:
            _heardAdvertsScreen.show();
            break;
        case Screen::WIFI_SETUP:
            _wifiSetupScreen.show();
            break;
        case Screen::USB_SETUP:
            _usbSetupScreen.show();
            break;
        case Screen::BLE_SETUP:
            _bleSetupScreen.show();
            break;
    }
    _currentScreen = screen;
    _lastActivity = millis();

    // Wake display if dimmed
    if (_dimmed) {
        const auto& dispCfg = ConfigManager::instance().config().display;
        Display::instance().setBrightness(dispCfg.brightness);
        if (dispCfg.kbdBacklight) {
            IInput::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
    }
}

void UIManager::refreshChatHeaderButtons(bool evalShare) {
    // Reveal the chat header's map button only when we have a position for the
    // open DM contact (so the map always opens with something to centre on), and
    // the Share button only when we hold a re-broadcastable advert for them.
    //
    // The map check is in-memory and runs on the 1 Hz status tick (location ages
    // out). Share availability only changes when a fresh advert is heard (which
    // updates the in-RAM blob cache) and its check can touch the SD card, so it's
    // evaluated only on chat open / after telemetry (evalShare), never per tick.
    const ConvoId* cc = _chatScreen.currentConvo();
    if (_currentScreen != Screen::CHAT || !cc || cc->type != ConvoId::DM) {
        _chatScreen.setMapAvailable(false);
        if (evalShare) _chatScreen.setShareAvailable(false);
        return;
    }
    const bool shareOn = ConfigManager::instance().config().messaging.shareContact;
    auto& contacts = ContactStore::instance();
    for (size_t i = 0; i < contacts.count(); i++) {
        const Contact* c = contacts.findByIndex(i);
        if (c && c->shortId() == cc->id) {
            _chatScreen.setMapAvailable(bestKnownLocation(c->publicKey).valid);
            if (evalShare) _chatScreen.setShareAvailable(shareOn &&
                MeshManager::instance().canShareContact(c->publicKey));
            return;
        }
    }
    _chatScreen.setMapAvailable(false);
    if (evalShare) _chatScreen.setShareAvailable(false);
}

void UIManager::openChat(const ConvoId& id) {
    showScreen(Screen::CHAT);  // Hide other screens first
    _chatScreen.open(id);      // open() calls show() internally
    refreshChatHeaderButtons();    // map button shown only if the contact is located

    // Decision #14 — re-login on ROOM ChatScreen open. Wakes any server-side
    // 3-strike push-freeze caused by brief radio dropouts (~36 s tripwire).
    if (id.type == ConvoId::ROOM) {
        const auto& rooms = ConfigManager::instance().config().roomServers;
        for (size_t i = 0; i < rooms.size() && i < MAX_ROOMS; i++) {
            if (rooms[i].publicKey.length() == 64 &&
                rooms[i].publicKey.substring(0, 16) == id.id) {
                roomChatOpenRelogin(i);
                break;
            }
        }
    }
}

void UIManager::goHome() {
    showScreen(Screen::CONVO_LIST);
}

void UIManager::onIncomingMessage(const ConvoId& id, const Message& msg) {
    // Detect MeshCore reaction messages before treating as regular messages.
    // Reactions are silently applied to their target (no bubble, no notification).
    if (!msg.fromSelf) {
        bool isChannel = (id.type == ConvoId::CHANNEL || id.type == ConvoId::ROOM);
        String rxEmoji, rxHash;
        if (parseIncomingReaction(msg.text, isChannel, rxEmoji, rxHash)) {
            // Consuming a message means it is never stored or shown, so leave a
            // trace: if this ever misfires on an ordinary message again, the
            // serial log is the only way to tell it apart from a lost packet.
            LOGF("[Rxn] Consumed as reaction: emoji=%s hash=%s\n",
                 rxEmoji.c_str(), rxHash.c_str());
            bool applied = MessageStore::instance().applyReaction(id, rxHash, rxEmoji, msg.senderName);
            if (applied && _currentScreen == Screen::CHAT &&
                _chatScreen.currentConvo() && *_chatScreen.currentConvo() == id) {
                _chatScreen.refresh();
            }
            return;
        }
    }

    // Check if currently viewing this conversation
    bool viewingThis = (_currentScreen == Screen::CHAT && _chatScreen.currentConvo() &&
                        *_chatScreen.currentConvo() == id);

    // Add to store
    Conversation* convo = MessageStore::instance().getConversation(id);
    String displayName = convo ? convo->displayName : id.id;
    bool isPrivate = convo ? convo->isPrivate : false;
    MessageStore::instance().addMessage(id, displayName, isPrivate, msg);

    // If currently viewing this conversation, update the chat and clear unread
    if (viewingThis) {
        _chatScreen.addMessageToView(msg);
        MessageStore::instance().markRead(id);
    }

    // If on convo list, refresh it
    if (_currentScreen == Screen::CONVO_LIST) {
        _convoList.refresh();
    }

    // Check SOS before normal notification
    bool isSos = checkSOS(id, msg);
    bool chatMuted = ConfigManager::instance().config().messaging.allowMute &&
                     MessageStore::instance().isMuted(id);
    if (!isSos) {
        // Normal notification with per-contact always-sound check
        // Skip sound if this specific chat is muted
        auto& speaker = Speaker::instance();
        if (!speaker.isMuted() && !chatMuted) {
            speaker.playNotification();
        } else if (!chatMuted && id.type == ConvoId::DM) {
            // Global mute only: a per-contact always_sound still rings. (Muting
            // this specific chat is the more deliberate gesture, so it wins —
            // chatMuted suppresses even always_sound.)
            auto& contacts = ContactStore::instance();
            for (size_t i = 0; i < contacts.count(); i++) {
                Contact* c = contacts.findByIndex(i);
                if (c && c->shortId() == id.id && c->alwaysSound) {
                    speaker.playNotificationForced();
                    break;
                }
            }
        }
#ifdef PLATFORM_TWATCH
        // Haptic always fires on incoming message — silent + buzzing is a
        // common "do not disturb" combo and we don't want to suppress it
        // along with the sound. Future: separate `hapticEnabled` config.
        Haptic::instance().playMessage();
#endif
    }

    // Wake display (skip for muted chats unless it's an SOS)
    if (!chatMuted || isSos) {
        if (_dimmed) {
            const auto& dispCfg = ConfigManager::instance().config().display;
            Display::instance().setBrightness(dispCfg.brightness);
            if (dispCfg.kbdBacklight) {
                IInput::instance().setBacklight(dispCfg.kbdBrightness);
            }
            _dimmed = false;
        }
        _lastActivity = millis();
    }
}

bool UIManager::checkSOS(const ConvoId& id, const Message& msg) {
    const auto& cfg = ConfigManager::instance().config();
    const String& keyword = cfg.sosKeyword;
    if (keyword.isEmpty()) return false;

    // Case-insensitive startsWith check
    String textLower = msg.text;
    textLower.toLowerCase();
    String kwLower = keyword;
    kwLower.toLowerCase();
    if (!textLower.startsWith(kwLower)) return false;

    // Find sender contact and check allowSos
    bool isDM = (id.type == ConvoId::DM);
    if (isDM) {
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            Contact* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                if (!c->allowSos) return false;  // SOS blocked for this contact
                break;
            }
        }
    } else if (id.type == ConvoId::CHANNEL) {
        // Check channel-level allowSos
        Channel* ch = ChannelStore::instance().findByName(id.id);
        if (ch && !ch->allowSos) return false;
        // Also check sender contact allowSos
        Contact* c = ContactStore::instance().findByName(msg.senderName);
        if (c && !c->allowSos) return false;
    } else if (id.type == ConvoId::ROOM) {
        // Check room-level allowSos (matches the channel pattern)
        const auto& rooms = ConfigManager::instance().config().roomServers;
        for (const auto& r : rooms) {
            if (r.publicKey.length() == 64 && r.publicKey.substring(0, 16) == id.id) {
                if (!r.allowSos) return false;
                break;
            }
        }
        // Also honor per-contact allowSos when the sender resolved to a known
        // alias (msg.senderName came from ContactStore lookup in onRoomMessageReceived;
        // unknown senders show as 8-hex and won't match by name).
        Contact* c = ContactStore::instance().findByName(msg.senderName);
        if (c && !c->allowSos) return false;
    }

    showSOSAlert(id, msg);
    return true;
}

void UIManager::showSOSAlert(const ConvoId& id, const Message& msg) {
    // Close previous SOS alert if open
    if (_sosMsgbox) {
        dismissSOSAlert(false);
    }
    // SOS is async and can fire over an open modal. Only one ModalDialog may be
    // active (they share a single modal input group), so tear down a telemetry
    // pop-up first — otherwise its panel/scrim would be orphaned on screen while
    // SOS steals the group. (Other transient modals are screen-local + brief.)
    if (_telemMsgbox) {
        dismissTelemetryModal();
    }

    _sosConvoId = id;
    _sosIsDM = (id.type == ConvoId::DM);
    _sosContactIndex = -1;

    // Find contact index for DM reply
    if (_sosIsDM) {
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const auto* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                _sosContactIndex = (int)i;
                break;
            }
        }
    }

    // Persist alert text — LVGL only stores pointer, local String would dangle
    char fromBuf[64];
    snprintf(fromBuf, sizeof(fromBuf), t("sos_from"), msg.senderName.c_str());
    _sosAlertText = String(fromBuf) + "\n\n" + msg.text;

    // Shared modal widget; btn 0 = Dismiss (no reply), btn 1 = SOS seen (reply).
    String sosTitleStr = String(LV_SYMBOL_WARNING " ") + t("sos_alert_title");
    _sosMsgbox = ModalDialog::show(sosTitleStr, _sosAlertText,
        { t("btn_dismiss"), t("btn_sos_seen") },
        [this](lv_obj_t* dlg, int idx) { (void)dlg; dismissSOSAlert(idx == 1); });
    // SOS keeps its distinctive red border over the shared styling.
    lv_obj_set_style_border_color(_sosMsgbox, theme::BATTERY_LOW(), 0);
    lv_obj_set_style_border_width(_sosMsgbox, 3, 0);

    // Disengage key lock so user can respond without unlocking first.
    // PIN lock (_isLocked) stays engaged — don't bypass security.
    if (_keyLocked) disengageKeyLock();

    // Start SOS sound
    const auto& cfg = ConfigManager::instance().config();
    Speaker::instance().startSOS(cfg.sosRepeat);
#ifdef PLATFORM_TWATCH
    Haptic::instance().playSos();
#endif

    // Wake display to max brightness
    Display::instance().setBrightness(255);
    if (cfg.display.kbdBacklight) {
        IInput::instance().setBacklight(cfg.display.kbdBrightness);
    }
    _dimmed = false;
    _lastActivity = millis();

    LOGF("[UI] SOS alert from %s\n", msg.senderName.c_str());
}

void UIManager::dismissSOSAlert(bool sendReply) {
    Speaker::instance().stopSOS();
#ifdef PLATFORM_TWATCH
    Haptic::instance().stop();
#endif

    // Send "SOS acknowledged" reply to the conversation it came from. Rooms
    // are excluded: we'd have to broadcast to the whole room (no addressable
    // sender from a 4-byte prefix), and per decision #11 we don't push to rooms.
    // For ROOM SOS, "SOS seen" just stops the sound + closes the modal.
    if (sendReply && _sosConvoId.type != ConvoId::ROOM) {
        const String replyText = "Acknowledged SOS";  // Always English — must NOT start with SOS keyword to avoid retriggering alert
        Message reply;
        reply.fromSelf  = true;
        reply.text      = replyText;
        // Store the timestamp that actually went on the wire, not a second sample.
        uint32_t ackTs = 0;

        if (_sosIsDM && _sosContactIndex >= 0) {
            reply.packetId = MeshManager::instance().sendMessage(_sosContactIndex, replyText.c_str(), &ackTs);
            reply.status = reply.packetId ? MessageStatus::SENDING : MessageStatus::FAILED;
        } else if (_sosConvoId.type == ConvoId::CHANNEL) {
            // Find channel index and send as group message
            Channel* ch = ChannelStore::instance().findByName(_sosConvoId.id);
            if (ch) {
                MeshManager::instance().sendGroupMessage(ch->index, replyText.c_str(), &ackTs);
            }
            reply.status = MessageStatus::SENT;  // Channels are fire-and-forget
        }
        reply.timestamp = ackTs ? ackTs : TimeHelper::instance().bestEpoch();

        Conversation* convo = MessageStore::instance().getConversation(_sosConvoId);
        String displayName = convo ? convo->displayName : _sosConvoId.id;
        bool isPrivate = convo ? convo->isPrivate : false;
        MessageStore::instance().addMessage(_sosConvoId, displayName, isPrivate, reply);

        LOGLN("[UI] SOS reply sent");
    }

    // Close modal (restores input group internally)
    if (_sosMsgbox) {
        ModalDialog::close(_sosMsgbox);
        _sosMsgbox = nullptr;
    }

    _sosAlertText = "";  // Free the persisted text

    // Restore normal brightness
    const auto& dispCfgSos = ConfigManager::instance().config().display;
    Display::instance().setBrightness(dispCfgSos.brightness);
    if (dispCfgSos.kbdBacklight) {
        IInput::instance().setBacklight(dispCfgSos.kbdBrightness);
    }

    LOGLN("[UI] SOS alert dismissed");
}

void UIManager::onAckReceived(uint32_t packetId) {
    MessageStore::instance().updateStatus(packetId, MessageStatus::DELIVERED);
    if (_currentScreen == Screen::CHAT) {
        _chatScreen.refresh();
    }
}

void UIManager::onMessageRepeated(uint32_t packetId, uint8_t repeaterCount) {
    MessageStore::instance().updateRepeaterCount(packetId, repeaterCount);
    if (_currentScreen == Screen::CHAT) {
        _chatScreen.refresh();
    }
}

void UIManager::onMessageFailed(uint32_t packetId) {
    MessageStore::instance().updateStatus(packetId, MessageStatus::FAILED);
    if (_currentScreen == Screen::CHAT) {
        _chatScreen.refresh();
    }
}

void UIManager::refreshConvoList() {
    if (_currentScreen == Screen::CONVO_LIST) _convoList.refresh();
}

// Channels pay for MeshCore's "<sender>: " prefix out of the same MAX_TEXT_LEN
// budget (BaseChatMesh::sendGroup truncates text_len to MAX_TEXT_LEN - prefix_len).
// Charging it here instead means the user is told the message is too long, rather
// than the radio quietly dropping the tail: the sender's own bubble would show
// text that was never delivered, the receiver would store and hash a different
// string (so reactions miss), and the cut can land mid-UTF-8-sequence.
size_t UIManager::maxMsgBytesFor(const ConvoId& id) {
    if (id.type != ConvoId::CHANNEL) return defaults::MAX_MSG_BYTES;
    const size_t prefix = ConfigManager::instance().config().deviceName.length() + 2;  // "name: "
    return prefix < defaults::MAX_MSG_BYTES ? defaults::MAX_MSG_BYTES - prefix : 0;
}

uint32_t UIManager::handleSend(const ConvoId& id, const String& text) {
    // Defensive byte-length guard (ChatScreen guards user-typed text first; this
    // also covers the location-send path). String::length() is the UTF-8 byte
    // count — over budget would fail in MeshCore and leave a silent FAILED bubble.
    if (text.length() > maxMsgBytesFor(id)) {
        showToast(t("msg_too_long"));
        return 0;
    }

    uint32_t packetId = 0;
    bool isDM   = (id.type == ConvoId::DM);
    bool isRoom = (id.type == ConvoId::ROOM);
    // Capture the timestamp that actually goes on the wire. Sampling bestEpoch()
    // again below would give a different value whenever the send straddles a
    // second boundary, and the stored reaction hash would then never match the
    // one a peer computes. Falls back to a fresh sample only if nothing was sent.
    uint32_t wireTimestamp = 0;

    if (isDM) {
        // Find contact index
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const auto* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                packetId = MeshManager::instance().sendMessage(i, text, &wireTimestamp);
                break;
            }
        }
    } else if (isRoom) {
        // Find room config index whose pubkey shortId matches id.id
        const auto& rooms = ConfigManager::instance().config().roomServers;
        for (size_t i = 0; i < rooms.size() && i < MAX_ROOMS; i++) {
            if (rooms[i].publicKey.length() != 64) continue;
            // Compare first 8 bytes (16 hex chars) to room shortId
            if (rooms[i].publicKey.substring(0, 16) == id.id) {
                packetId = MeshManager::instance().sendRoomPost(i, text, &wireTimestamp);
                break;
            }
        }
    } else {
        // Find channel index
        auto* ch = ChannelStore::instance().findByName(id.id);
        if (ch) {
            packetId = MeshManager::instance().sendGroupMessage(ch->index, text, &wireTimestamp);
        }
    }

    // Determine initial status:
    // DMs and rooms: SENDING (waiting for ACK; retry pipeline handles DELIVERED/FAILED)
    // Channels: SENT immediately (fire-and-forget, no ACK possible)
    MessageStatus initialStatus;
    if (packetId == 0) {
        initialStatus = MessageStatus::FAILED;
        // Send never reached the air (length was already guarded above, so this is
        // most likely the static packet pool drained by a burst, or createDatagram
        // returning NULL). The FAILED bubble is still drawn so the user can tap to
        // retry, but toast too so the failure isn't silent.
        showToast(t("msg_send_failed"));
    } else if (isDM || isRoom) {
        initialStatus = MessageStatus::SENDING;
    } else {
        initialStatus = MessageStatus::SENT;
    }

    // Add to local store
    Message msg;
    msg.fromSelf  = true;
    msg.text      = text;
    msg.timestamp = wireTimestamp ? wireTimestamp
                                  : TimeHelper::instance().bestEpoch();
    msg.status    = initialStatus;
    msg.packetId  = packetId;

    Conversation* convo = MessageStore::instance().getConversation(id);
    String displayName = convo ? convo->displayName : id.id;
    bool isPrivate = convo ? convo->isPrivate : false;
    MessageStore::instance().addMessage(id, displayName, isPrivate, msg);

    // Update the view only if this conversation is on screen (companion sends may
    // target a conversation that isn't currently open).
    bool viewingThis = (_currentScreen == Screen::CHAT && _chatScreen.currentConvo() &&
                        *_chatScreen.currentConvo() == id);
    if (viewingThis) {
        _chatScreen.addMessageToView(msg);
    } else if (_currentScreen == Screen::CONVO_LIST) {
        _convoList.refresh();
    }

    _lastActivity = millis();
    return packetId;
}

void UIManager::handleReaction(const ConvoId& id, const String& wireText) {
    bool isDM   = (id.type == ConvoId::DM);
    bool isRoom = (id.type == ConvoId::ROOM);

    if (isDM) {
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const auto* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                MeshManager::instance().sendMessage(i, wireText);
                break;
            }
        }
    } else if (isRoom) {
        const auto& rooms = ConfigManager::instance().config().roomServers;
        for (size_t i = 0; i < rooms.size() && i < MAX_ROOMS; i++) {
            if (rooms[i].publicKey.length() != 64) continue;
            if (rooms[i].publicKey.substring(0, 16) == id.id) {
                MeshManager::instance().sendRoomPost(i, wireText);
                break;
            }
        }
    } else {
        auto* ch = ChannelStore::instance().findByName(id.id);
        if (ch) MeshManager::instance().sendGroupMessage(ch->index, wireText);
    }

    // Apply the reaction locally — our own outgoing packets never loop back
    // through onIncomingMessage, so we must update MessageStore ourselves.
    String rxEmoji, rxHash;
    if (parseIncomingReaction(wireText, !isDM, rxEmoji, rxHash)) {
        const String& myName = ConfigManager::instance().config().deviceName;
        bool applied = MessageStore::instance().applyReaction(id, rxHash, rxEmoji, myName);
        if (applied && _currentScreen == Screen::CHAT &&
            _chatScreen.currentConvo() && *_chatScreen.currentConvo() == id) {
            _chatScreen.refresh();
        }
    }

    _lastActivity = millis();
}

// ─── Room callbacks (wired from main.cpp setupMeshCallbacks) ───

void UIManager::onRoomMessageReceived(size_t roomIdx, const String& roomName,
                                       const uint8_t* senderPrefix /* 4 B */,
                                       const String& text, uint32_t timestamp, uint8_t hops) {
    if (roomIdx >= MAX_ROOMS) return;
    _lastRoomMsgMs[roomIdx] = millis();

    // Resolve sender alias: scan ContactStore for any contact whose pubkey first
    // 4 bytes match. Hit → use the alias. Miss → 8-hex-char prefix.
    String sender;
    auto& contacts = ContactStore::instance();
    for (size_t i = 0; i < contacts.count(); i++) {
        const Contact* c = contacts.findByIndex(i);
        if (c && memcmp(c->publicKey, senderPrefix, 4) == 0) {
            sender = c->name;
            break;
        }
    }
    if (sender.isEmpty()) {
        char hex[9];
        for (int i = 0; i < 4; i++) sprintf(hex + i*2, "%02x", senderPrefix[i]);
        hex[8] = '\0';
        sender = String(hex);
    }

    // Find the room contact's pubkey from config to compute the room shortId
    const auto& rooms = ConfigManager::instance().config().roomServers;
    if (roomIdx >= rooms.size()) return;
    if (rooms[roomIdx].publicKey.length() != 64) return;

    // shortId = first 16 hex chars of the room's pubkey (matches pubKeyToShortId)
    String shortId = rooms[roomIdx].publicKey.substring(0, 16);
    ConvoId id { ConvoId::ROOM, shortId };

    Message msg;
    msg.fromSelf  = false;
    msg.text      = text;
    msg.timestamp = timestamp;
    msg.senderName = sender;
    msg.status    = MessageStatus::DELIVERED;
    msg.hops      = hops;

    onIncomingMessage(id, msg);

    // Persist sync_since so next boot only replays newer posts. BaseChatMesh has
    // already advanced contact.sync_since by the time we got here; this commits
    // it to /mclite/history/room_<shortId>.json.
    MessageStore::instance().updateRoomSyncSince(id, timestamp);
}

void UIManager::onRoomLoginResponse(size_t roomIdx, const String& roomName,
                                     uint8_t status, uint8_t permissions) {
    if (roomIdx >= MAX_ROOMS) return;
    bool ok = (status == RESP_SERVER_LOGIN_OK);
    _roomLoggedIn[roomIdx] = ok;
    if (ok) {
        _loginAttempt[roomIdx] = 0;
        LOGF("[UI] Room '%s' logged in (perms=%u)\n",
                      roomName.c_str(), (unsigned)permissions);
    } else {
        LOGF("[UI] Room '%s' login failed (status=%u)\n",
                      roomName.c_str(), (unsigned)status);
    }
}

// Boot login + backoff retry for not-logged-in rooms. Tick from update() at the
// existing STATUS_UPDATE_MS cadence (1 s) — cheap; only fires when due.
void UIManager::roomLoginTick() {
    if (!MeshManager::instance().isRadioReady()) return;

    const auto& rooms = ConfigManager::instance().config().roomServers;
    unsigned long now = millis();
    size_t roomCount = rooms.size() < MAX_ROOMS ? rooms.size() : MAX_ROOMS;

    // Stagger login bursts: at most one room login per tick. With 8 rooms at boot
    // this spreads the initial login flood over 8 s (STATUS_UPDATE_MS cadence),
    // avoiding packet-pool pressure during the first second after radio-ready.
    for (size_t i = 0; i < roomCount; i++) {
        if (_roomLoggedIn[i]) continue;
        if (now < _nextLoginAttemptMs[i] && _lastLoginMs[i] != 0) continue;

        uint32_t estTimeout = 0;
        if (MeshManager::instance().loginRoom(i, estTimeout)) {
            _lastLoginMs[i] = now;
            // Backoff: 1 → 2 → 4 → cap 30 min. Reset to 0 happens in onRoomLoginResponse.
            uint32_t delaySec = 60u << (_loginAttempt[i] < 5 ? _loginAttempt[i] : 5);
            if (delaySec > 1800) delaySec = 1800;
            _nextLoginAttemptMs[i] = now + (unsigned long)delaySec * 1000;
            if (_loginAttempt[i] < 255) _loginAttempt[i]++;
            LOGF("[UI] Room '%s' login attempt %u; next in %us\n",
                          rooms[i].name.c_str(), (unsigned)_loginAttempt[i],
                          (unsigned)delaySec);
            return;  // one login per tick
        }
    }
}

// Decision #14: rate-limited re-login on ROOM ChatScreen open. Suppress if a
// login fired within the last 30 s to avoid thrashing on rapid back-and-forth.
void UIManager::roomChatOpenRelogin(size_t roomIdx) {
    if (roomIdx >= MAX_ROOMS) return;
    if (!MeshManager::instance().isRadioReady()) return;
    unsigned long now = millis();
    if (_lastLoginMs[roomIdx] != 0 && now - _lastLoginMs[roomIdx] < 30000) return;

    uint32_t estTimeout = 0;
    if (MeshManager::instance().loginRoom(roomIdx, estTimeout)) {
        _lastLoginMs[roomIdx] = now;
        LOGF("[UI] Room idx=%u: chat-open re-login\n", (unsigned)roomIdx);
    }
}

// Decision #15: silence-triggered re-login while a ROOM ChatScreen is foreground.
// Active rooms reset _lastRoomMsgMs on every receipt so this never fires for
// them. Quiet rooms with passive readers re-login at most once per 10 min.
void UIManager::roomSilenceTick(size_t roomIdx) {
    if (roomIdx >= MAX_ROOMS) return;
    if (!MeshManager::instance().isRadioReady()) return;
    unsigned long now = millis();
    constexpr unsigned long SILENCE_THRESHOLD_MS = 10UL * 60UL * 1000UL;  // 10 min

    if (_lastRoomMsgMs[roomIdx] != 0 && now - _lastRoomMsgMs[roomIdx] < SILENCE_THRESHOLD_MS) return;
    if (_lastLoginMs[roomIdx]   != 0 && now - _lastLoginMs[roomIdx]   < SILENCE_THRESHOLD_MS) return;

    uint32_t estTimeout = 0;
    if (MeshManager::instance().loginRoom(roomIdx, estTimeout)) {
        _lastLoginMs[roomIdx] = now;
        LOGF("[UI] Room idx=%u: silence-triggered re-login\n", (unsigned)roomIdx);
    }
}

void UIManager::handleRetry(const ConvoId& id, const String& text, uint32_t oldPacketId) {
    // Channels are fire-and-forget (status SENT immediately; never reaches FAILED),
    // so the retry button only ever fires for DM or ROOM bubbles.
    if (id.type != ConvoId::DM && id.type != ConvoId::ROOM) return;

    // Verify message is still FAILED before sending (guards against double-tap)
    auto* convo = MessageStore::instance().getConversation(id);
    if (!convo) return;
    Message* target = nullptr;
    for (auto& msg : convo->messages) {
        if (msg.packetId == oldPacketId && msg.fromSelf &&
            msg.status == MessageStatus::FAILED) {
            target = &msg;
            break;
        }
    }
    if (!target) return;

    // Re-send via MeshManager — match the original send path by convo type
    uint32_t newPacketId = 0;
    uint32_t wireTimestamp = 0;   // the retry goes out with a FRESH timestamp
    if (id.type == ConvoId::DM) {
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const auto* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                newPacketId = MeshManager::instance().sendMessage(i, text, &wireTimestamp);
                break;
            }
        }
    } else {  // ROOM
        const auto& cfgRooms = ConfigManager::instance().config().roomServers;
        for (size_t i = 0; i < cfgRooms.size() && i < MAX_ROOMS; i++) {
            if (cfgRooms[i].publicKey.length() == 64 &&
                cfgRooms[i].publicKey.substring(0, 16) == id.id) {
                newPacketId = MeshManager::instance().sendRoomPost(i, text, &wireTimestamp);
                break;
            }
        }
    }

    if (newPacketId == 0) return;

    // Update the existing failed message in-place. The timestamp and hash must
    // move with it: the resend carries a new sender_timestamp, and the peer will
    // store and hash THAT. Keeping the original values here would leave the two
    // ends hashing different inputs, so every reaction to this message would miss
    // in both directions -- silently, since a failed hash lookup just queues.
    target->packetId = newPacketId;
    target->status = MessageStatus::SENDING;
    if (wireTimestamp) {
        target->timestamp = wireTimestamp;
        target->msgHash   = computeMsgHash(target->text, wireTimestamp);
    }
    MessageStore::instance().saveHistory(id);

    _chatScreen.refresh();
    _lastActivity = millis();
}

void UIManager::showSetupScreen(SetupReason reason) {
    _inSetupMode = true;

    // Hide all normal screens
    _convoList.hide();
    _chatScreen.hide();
    _adminScreen.hide();
    _heardAdvertsScreen.hide();
    _wifiSetupScreen.hide();
    _usbSetupScreen.hide();
    _bleSetupScreen.hide();

    // Full-screen overlay on top of everything
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, Display::width(), Display::height());
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 20, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(overlay, 12, 0);

    // Icon
    lv_obj_t* icon = lv_label_create(overlay);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, theme::BATTERY_LOW(), 0);

    // Title
    lv_obj_t* title = lv_label_create(overlay);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Message
    lv_obj_t* msg = lv_label_create(overlay);
    lv_obj_set_style_text_color(msg, theme::TEXT_SECONDARY(), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(msg, theme::MODAL_TEXT_WIDTH);

    switch (reason) {
        case NO_SD:
            lv_label_set_text(icon, LV_SYMBOL_WARNING);
            lv_label_set_text(title, t("no_sd_title"));
            lv_label_set_text(msg, t("no_sd_msg"));
            break;

        case NO_CONFIG:
            lv_label_set_text(icon, LV_SYMBOL_SD_CARD);
            lv_obj_set_style_text_color(icon, theme::TEXT_PRIMARY(), 0);
            lv_label_set_text(title, t("setup_title"));
            lv_label_set_text(msg, t("setup_msg"));
            break;

        case CONFIG_ERROR:
            lv_label_set_text(icon, LV_SYMBOL_WARNING);
            lv_label_set_text(title, t("config_error_title"));
            lv_label_set_text(msg, t("config_error_msg"));
            break;
    }

    // Footer hint
    lv_obj_t* footer = lv_label_create(overlay);
    lv_obj_set_style_text_color(footer, theme::TEXT_TIMESTAMP(), 0);
    lv_label_set_text(footer, "MCLite v" MCLITE_VERSION);

    LOGF("[UI] Setup screen shown (reason=%d)\n", (int)reason);
}


void UIManager::updateSOSHold() {
    if (_keyLocked) {
        // No SOS trigger while key-locked, but DO clean up any in-flight
        // countdown label — otherwise it gets orphaned if the lock engages
        // mid-hold (auto-dim) and the user never sees it disappear.
        if (_sosCountdownActive) {
            if (_sosCountdownLabel) {
                lv_obj_del(_sosCountdownLabel);
                _sosCountdownLabel = nullptr;
            }
            _sosCountdownActive = false;
        }
        return;
    }

    bool pressed = IInput::instance().isPressed();
    uint32_t held = IInput::instance().holdDurationMs();

    if (!pressed || held < SOS_HOLD_SHOW_MS) {
        // Not held long enough or released — cancel countdown
        if (_sosCountdownActive) {
            if (_sosCountdownLabel) {
                lv_obj_del(_sosCountdownLabel);
                _sosCountdownLabel = nullptr;
            }
            _sosCountdownActive = false;
        }
        if (!pressed) {
            _sosSentThisHold = false;
        }
        return;
    }

    // Already sent this hold cycle — wait for release
    if (_sosSentThisHold) return;

    // Held >= 2s: show or update countdown
    uint32_t remaining = (held >= SOS_HOLD_SEND_MS) ? 0 : (SOS_HOLD_SEND_MS - held);
    uint8_t secsLeft = (remaining + 999) / 1000;  // Round up

    if (held >= SOS_HOLD_SEND_MS) {
        // 6 seconds reached — send SOS
        if (_sosCountdownLabel) {
            lv_obj_del(_sosCountdownLabel);
            _sosCountdownLabel = nullptr;
        }
        _sosCountdownActive = false;
        _sosSentThisHold = true;
        sendSOSToAll();
        return;
    }

    // Show or update countdown label (original styling — T-Deck worked fine
    // with this; T-Watch update-in-place issue is a panel/driver bug we'll
    // tackle separately in 4d, not by restructuring this UI).
    if (!_sosCountdownActive) {
        _sosCountdownActive = true;
        _sosCountdownLabel = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_opa(_sosCountdownLabel, LV_OPA_80, 0);
        lv_obj_set_style_bg_color(_sosCountdownLabel, theme::SCRIM(), 0);
        lv_obj_set_style_text_color(_sosCountdownLabel, theme::BATTERY_LOW(), 0);
        lv_obj_set_style_text_font(_sosCountdownLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_all(_sosCountdownLabel, 12, 0);
        lv_obj_set_style_radius(_sosCountdownLabel, 8, 0);
        lv_obj_center(_sosCountdownLabel);
    }

    char buf[64];
    char countBuf[48];
    snprintf(countBuf, sizeof(countBuf), t("sos_countdown"), secsLeft);
    snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", countBuf);
    lv_label_set_text(_sosCountdownLabel, buf);
}

void UIManager::sendSOSToAll() {
    const auto& cfg = ConfigManager::instance().config();
    auto& contacts = ContactStore::instance();
    auto& mesh = MeshManager::instance();

    if (!mesh.isRadioReady() || contacts.count() == 0) {
        LOGLN("[UI] SOS send failed — no radio or no contacts");
        return;
    }

    // Build SOS message with GPS location if available (live or last known)
    String sosText = cfg.sosKeyword;
    auto& gps = GPS::instance();
    if (gps.fixStatus() != FixStatus::NO_FIX) {
        sosText += " @ " + gps.formatLocationWithStatus();
    }

    LOGF("[SOS] Begin burst: %u contacts, %u channels\n",
                  (unsigned)contacts.count(), (unsigned)ChannelStore::instance().count());

    // Send to every contact
    uint32_t sent = 0;
    for (size_t i = 0; i < contacts.count(); i++) {
        Contact* c = contacts.findByIndex(i);
        if (!c || !c->sendSos) continue;

        uint32_t sosTs = 0;
        uint32_t packetId = mesh.sendMessage(i, sosText, &sosTs);
        LOGF("[SOS] DM %s: packetId=%u %s\n",
                      c->name.c_str(), packetId,
                      packetId ? "queued" : "FAILED (pool?)");

        // Add to local message store
        ConvoId id{ConvoId::DM, c->shortId()};
        Message msg;
        msg.fromSelf  = true;
        msg.text      = sosText;
        msg.timestamp = sosTs ? sosTs : TimeHelper::instance().bestEpoch();
        msg.status    = packetId ? MessageStatus::SENDING : MessageStatus::FAILED;
        msg.packetId  = packetId;

        Conversation* convo = MessageStore::instance().getConversation(id);
        String displayName = convo ? convo->displayName : c->name;
        MessageStore::instance().addMessage(id, displayName, false, msg);

        if (packetId) sent++;

        // Yield to dispatcher between sends so it can drain one packet
        // before we enqueue the next. Prevents tight-burst pool pressure
        // and lets CAD settle.
        MeshManager::instance().update();
        delay(50);
    }

    // Also send to all channels
    auto& channels = ChannelStore::instance();
    for (size_t i = 0; i < channels.count(); i++) {
        const auto& allCh = channels.all();
        if (!allCh[i].sendSos) continue;
        uint32_t sosTs = 0;
        uint32_t packetId = mesh.sendGroupMessage(allCh[i].index, sosText, &sosTs);
        LOGF("[SOS] CH %s: packetId=%u %s\n",
                      allCh[i].name.c_str(), packetId,
                      packetId ? "queued" : "FAILED (pool?)");

        ConvoId id{ConvoId::CHANNEL, allCh[i].name};
        Message msg;
        msg.fromSelf  = true;
        msg.text      = sosText;
        msg.timestamp = sosTs ? sosTs : TimeHelper::instance().bestEpoch();
        msg.status    = packetId ? MessageStatus::SENT : MessageStatus::FAILED;
        msg.packetId  = packetId;

        Conversation* convo = MessageStore::instance().getConversation(id);
        String displayName = convo ? convo->displayName : allCh[i].name;
        MessageStore::instance().addMessage(id, displayName, false, msg);

        if (packetId) sent++;

        MeshManager::instance().update();
        delay(50);
    }

    // Also send to rooms with send_sos enabled. Rooms are pubkey-addressed
    // (DM-style ACK pipeline) so the local message starts SENDING, not SENT.
    const auto& rooms = ConfigManager::instance().config().roomServers;
    for (size_t i = 0; i < rooms.size() && i < MAX_ROOMS; i++) {
        if (!rooms[i].sendSos) continue;
        if (rooms[i].publicKey.length() != 64) continue;

        uint32_t sosTs = 0;
        uint32_t packetId = mesh.sendRoomPost(i, sosText, &sosTs);
        LOGF("[SOS] ROOM %s: packetId=%u %s\n",
                      rooms[i].name.c_str(), packetId,
                      packetId ? "queued" : "FAILED (pool?)");

        String shortId = rooms[i].publicKey.substring(0, 16);
        ConvoId id{ConvoId::ROOM, shortId};
        Message msg;
        msg.fromSelf  = true;
        msg.text      = sosText;
        msg.timestamp = sosTs ? sosTs : TimeHelper::instance().bestEpoch();
        msg.status    = packetId ? MessageStatus::SENDING : MessageStatus::FAILED;
        msg.packetId  = packetId;

        Conversation* convo = MessageStore::instance().getConversation(id);
        String displayName = convo ? convo->displayName : rooms[i].name;
        MessageStore::instance().addMessage(id, displayName, false, msg);

        if (packetId) sent++;

        MeshManager::instance().update();
        delay(50);
    }

    // Show confirmation toast via a brief modal
    char confirmBuf[64];
    snprintf(confirmBuf, sizeof(confirmBuf), t("sos_sent"), sent);
    String sentTitleStr = String(LV_SYMBOL_WARNING " ") + t("sos_sent_title");
    lv_obj_t* msgbox = ModalDialog::show(sentTitleStr, confirmBuf, { t("btn_ok") },
        [](lv_obj_t* dlg, int) { ModalDialog::close(dlg); });
    lv_obj_set_style_border_color(msgbox, theme::BATTERY_LOW(), 0);
    lv_obj_set_style_border_width(msgbox, 2, 0);

    // Refresh chat view if open
    if (_currentScreen == Screen::CHAT) {
        _chatScreen.refresh();
    } else if (_currentScreen == Screen::CONVO_LIST) {
        _convoList.refresh();
    }

    LOGF("[UI] SOS broadcast sent to %d recipient(s)\n", sent);
}

void UIManager::checkBatteryAlert() {
    uint32_t now = millis();
    if (now - _lastBatteryCheck < BATTERY_CHECK_MS) return;
    _lastBatteryCheck = now;

    const auto& cfg = ConfigManager::instance().config();
    if (!cfg.battery.lowAlertEnabled) return;

    uint8_t pct = Battery::instance().percent();
    uint8_t threshold = cfg.battery.lowAlertThreshold;

    if (pct <= threshold && !_batteryAlertSent) {
        // Build alert message
        char alertBuf[48];
        snprintf(alertBuf, sizeof(alertBuf), "LOW BATTERY: %d%%", (int)pct);  // Always English — recipient may use different language
        String alertText = alertBuf;
        auto& gps = GPS::instance();
        if (gps.fixStatus() != FixStatus::NO_FIX) {
            alertText += " @ " + gps.formatLocationWithStatus();
        }

        // Send to all contacts/channels with sendSos==true (reuse SOS broadcast pattern)
        auto& contacts = ContactStore::instance();
        auto& channels_store = ChannelStore::instance();
        auto& mesh = MeshManager::instance();

        if (mesh.isRadioReady()) {
            // Per-send, not one pre-loop sample: each send() takes its own
            // bestEpoch(), and there is real radio work plus a yield between
            // them, so a single shared value is guaranteed to disagree with the
            // wire for every contact after the first -- which silently breaks any
            // reaction to a battery alert.
            uint32_t ts = 0;

            for (size_t i = 0; i < contacts.count(); i++) {
                Contact* c = contacts.findByIndex(i);
                if (!c || !c->sendSos) continue;
                uint32_t packetId = mesh.sendMessage(i, alertText, &ts);

                ConvoId id{ConvoId::DM, c->shortId()};
                Message msg;
                msg.fromSelf  = true;
                msg.text      = alertText;
                msg.timestamp = ts;
                msg.status    = packetId ? MessageStatus::SENDING : MessageStatus::FAILED;
                msg.packetId  = packetId;
                Conversation* convo = MessageStore::instance().getConversation(id);
                String displayName = convo ? convo->displayName : c->name;
                MessageStore::instance().addMessage(id, displayName, false, msg);
            }
            for (const auto& ch : channels_store.all()) {
                if (!ch.sendSos) continue;
                uint32_t packetId = mesh.sendGroupMessage(ch.index, alertText, &ts);

                ConvoId id{ConvoId::CHANNEL, ch.name};
                Message msg;
                msg.fromSelf  = true;
                msg.text      = alertText;
                msg.timestamp = ts;
                msg.status    = packetId ? MessageStatus::SENT : MessageStatus::FAILED;
                msg.packetId  = packetId;
                Conversation* convo = MessageStore::instance().getConversation(id);
                String displayName = convo ? convo->displayName : ch.name;
                MessageStore::instance().addMessage(id, displayName, ch.isPrivate(), msg);
            }
            // Also send to rooms with sendSos enabled. Same broadcast policy as SOS.
            const auto& rooms = cfg.roomServers;
            for (size_t i = 0; i < rooms.size() && i < MAX_ROOMS; i++) {
                if (!rooms[i].sendSos) continue;
                if (rooms[i].publicKey.length() != 64) continue;
                uint32_t packetId = mesh.sendRoomPost(i, alertText, &ts);

                ConvoId id{ConvoId::ROOM, rooms[i].publicKey.substring(0, 16)};
                Message msg;
                msg.fromSelf  = true;
                msg.text      = alertText;
                msg.timestamp = ts;
                msg.status    = packetId ? MessageStatus::SENDING : MessageStatus::FAILED;
                msg.packetId  = packetId;
                Conversation* convo = MessageStore::instance().getConversation(id);
                String displayName = convo ? convo->displayName : rooms[i].name;
                MessageStore::instance().addMessage(id, displayName, false, msg);
            }
        }

        _batteryAlertSent = true;
        LOGF("[UI] Battery low alert sent: %d%%\n", pct);
    } else if (pct > threshold + 5 && _batteryAlertSent) {
        // Hysteresis reset
        _batteryAlertSent = false;
        LOGLN("[UI] Battery alert reset (hysteresis)");
    }
}

void UIManager::showPinLock(PinPurpose purpose) {
    if (_pinOverlay) return;  // Already showing

    _pinPurpose = purpose;
    // _isLocked is set for every purpose on purpose: while any PIN prompt is up we
    // want key shortcuts, the SD/WiFi update prompts and key-lock all suppressed.
    _isLocked = true;
    _pinBuffer = "";
    // Do NOT clear _pinFails/_pinWaitUntil here: re-arming the lock (auto-dim)
    // must not hand out a fresh set of free guesses. dismissPinLock() clears them
    // on a correct entry, which is the only way they should reset.

    _pinOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_pinOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_pinOverlay, 0, 0);
    lv_obj_set_style_bg_color(_pinOverlay, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(_pinOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_pinOverlay, 0, 0);
    lv_obj_set_style_radius(_pinOverlay, 0, 0);
    lv_obj_set_style_pad_all(_pinOverlay, 20, 0);
    lv_obj_set_flex_flow(_pinOverlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_pinOverlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_pinOverlay, 12, 0);
    lv_obj_clear_flag(_pinOverlay, LV_OBJ_FLAG_SCROLLABLE);

    // Lock icon
    lv_obj_t* lockIcon = lv_label_create(_pinOverlay);
    lv_obj_set_style_text_font(lockIcon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lockIcon, theme::ACCENT(), 0);
    lv_label_set_text(lockIcon, ICON_LOCK);

    // Title
    lv_obj_t* title = lv_label_create(_pinOverlay);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY(), 0);
    // Say which PIN is being asked for -- the screen PIN and the admin PIN are
    // different secrets, so an unlabelled prompt would be genuinely ambiguous.
    lv_label_set_text(title, purpose == PinPurpose::ScreenUnlock ? t("pin_title")
                                                                 : t("pin_title_admin"));

    // PIN dots display
    _pinDots = lv_label_create(_pinOverlay);
    lv_obj_set_style_text_font(_pinDots, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_pinDots, theme::TEXT_PRIMARY(), 0);
    lv_label_set_text(_pinDots, "");

    // Status message (for errors)
    _pinStatus = lv_label_create(_pinOverlay);
    lv_obj_set_style_text_font(_pinStatus, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_pinStatus, theme::TEXT_SECONDARY(), 0);
    lv_label_set_text(_pinStatus, t("pin_hint"));

    // Cancel, for the admin prompts only. The screen lock must stay uncancellable,
    // but AdminUnlock is reached by a single '0' press (T-Deck) or side-button
    // press (T-Watch), so without an exit an accidental press traps the whole UI
    // until a power cycle -- and an incoming SOS alert would be unreadable behind
    // this opaque overlay. Deliberately NOT added to _pinGroup (touch-only
    // buttons in the encoder group break the refocus chain).
    if (purpose != PinPurpose::ScreenUnlock) {
        lv_obj_t* cancelBtn = lv_btn_create(_pinOverlay);
        lv_obj_set_style_bg_color(cancelBtn, theme::BG_SECONDARY(), 0);
        lv_obj_set_style_shadow_width(cancelBtn, 0, 0);
        lv_obj_set_ext_click_area(cancelBtn, 8);
        lv_obj_add_event_cb(cancelBtn, pinCancelCb, LV_EVENT_CLICKED, this);
        lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
        lv_obj_set_style_text_font(cancelLbl, FONT_BODY, 0);
        lv_obj_set_style_text_color(cancelLbl, theme::TEXT_PRIMARY(), 0);
        lv_label_set_text(cancelLbl, t("btn_cancel"));
        lv_obj_center(cancelLbl);
    }

    // On a board with no physical keyboard the only indev is a touch POINTER,
    // which never emits LV_EVENT_KEY -- so without this the overlay has no input
    // path at all and arming a PIN lock bricks the device until the SD card is
    // pulled. Full alphanumeric rather than a numeric pad: pin_code accepts
    // letters (onPinKey below), and a numeric-only pad would strand anyone who
    // set one through the Settings editor.
    if (!IInput::instance().has(InputCapability::Keyboard)) {
        _pinKeypad = lv_keyboard_create(_pinOverlay);
        lv_keyboard_set_mode(_pinKeypad, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(_pinKeypad, NULL);   // we own the buffer, not a textarea
        lv_obj_set_width(_pinKeypad, LV_PCT(100));
        lv_obj_set_height(_pinKeypad, LV_PCT(45));
        // PREPROCESS is load-bearing. lv_keyboard registers its own
        // VALUE_CHANGED handler first, and that handler swaps the button map for
        // the mode keys ("1#", "abc", "ABC") WITHOUT resetting the selected index.
        // Running after it, lv_btnmatrix_get_btn_text() reads the NEW map at the
        // old index: "1#" in lowercase is index 0, which is "1" in the special
        // map, so tapping the mode key to reach the digits typed a stray 1 -- and
        // "abc" in the special map sits where backspace is in lowercase, deleting
        // a character. Both maps have 40 entries, so it never went out of range,
        // it just silently corrupted the PIN.
        lv_obj_add_event_cb(_pinKeypad, pinKeypadCb,
                            (lv_event_code_t)(LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS), this);
    }

    // Remember what input was on so dismiss can hand it back exactly (an auto-dim
    // lock can fire over an open Settings editor, whose group is not _inputGroup).
    _pinPrevGroup = IInput::instance().currentGroup();

    // Use a dedicated group so trackball/keyboard can't focus away from the overlay
    _pinGroup = lv_group_create();
    lv_obj_add_flag(_pinOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(_pinGroup, _pinOverlay);
    lv_group_focus_obj(_pinOverlay);
    IInput::instance().attachToGroup(_pinGroup);
    lv_obj_add_event_cb(_pinOverlay, pinKeyCb, LV_EVENT_KEY, this);

    LOGLN("[UI] PIN lock shown");
}

// Runs one main-loop turn after a correct PIN (or a cancel), so the overlay and
// its group are destroyed outside their own event dispatch.
void UIManager::finishPinUnlock() {
    if (!_pinOverlay) return;                 // already gone (double async, reboot)
    const PinPurpose done = _pinPendingAction;
    dismissPinLock();
    auto& mgr = ConfigManager::instance();
    if (done == PinPurpose::AdminUnlock) {
        mgr.config().security.adminEnabled = true;   // permanent re-enable
        // A failed write leaves the change in RAM only, so Admin would be locked
        // again after the next reboot. Say so rather than claiming success.
        if (!mgr.save()) showToast(t("err_save_failed"));
        // Say so explicitly: the lock is now OFF and stays off until it is locked
        // again, which is not obvious from Admin simply opening.
        showToast(t("admin_unlocked"));
        showScreen(Screen::ADMIN);
    } else if (done == PinPurpose::ConfirmAdminLock) {
        mgr.config().security.adminEnabled = false;  // typing the PIN is the confirmation
        if (!mgr.save()) showToast(t("err_save_failed"));
        showToast(t("admin_locked"));
        goHome();
    }
}

void UIManager::pinKeyCb(lv_event_t* e) {
    UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(e));
    uint32_t key = lv_event_get_key(e);
    self->onPinKey(key);
}

// Translate an on-screen keypad press into the same key codes the physical
// keyboard produces, so onPinKey() stays the single place that knows the rules.
void UIManager::pinKeypadCb(lv_event_t* e) {
    UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(e));
    lv_obj_t*  kb   = lv_event_get_target(e);
    uint16_t   id   = lv_btnmatrix_get_selected_btn(kb);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char* txt = lv_btnmatrix_get_btn_text(kb, id);
    if (!txt) return;
    if (!strcmp(txt, LV_SYMBOL_BACKSPACE)) { self->onPinKey(LV_KEY_BACKSPACE); return; }
    if (!strcmp(txt, LV_SYMBOL_NEW_LINE) ||
        !strcmp(txt, LV_SYMBOL_OK))        { self->onPinKey(LV_KEY_ENTER);     return; }
    if (!strcmp(txt, LV_SYMBOL_KEYBOARD) ||
        !strcmp(txt, LV_SYMBOL_CLOSE))     { self->onPinKey(LV_KEY_ESC);       return; }
    // Single ASCII character. The mode-switch keys ("abc"/"ABC"/"1#") and the
    // multi-byte LV_SYMBOLs are all longer than one byte, so this skips them and
    // lv_keyboard's own handler deals with the mode changes.
    if (txt[0] && !txt[1]) self->onPinKey((uint8_t)txt[0]);
}

void UIManager::pinCancelCb(lv_event_t* e) {
    UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(e));
    self->onPinKey(LV_KEY_ESC);
}

// Seconds left on the failed-PIN backoff, 0 when free. Rollover-safe: compares
// as a signed difference rather than millis() < _pinWaitUntil.
uint32_t UIManager::pinWaitRemaining() const {
    uint32_t until = (_pinPurpose == PinPurpose::ScreenUnlock) ? _pinWaitUntil : _adminWaitUntil;
    if (until == 0) return 0;
    int32_t left = (int32_t)(until - millis());
    return left > 0 ? (uint32_t)((left + 999) / 1000) : 0;
}

void UIManager::onPinKey(uint32_t key) {
    // Wake display on any keypress while locked and dimmed
    if (_dimmed) {
        const auto& dispCfg = ConfigManager::instance().config().display;
        Display::instance().setBrightness(dispCfg.brightness);
        if (dispCfg.kbdBacklight) {
            IInput::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
    }
    _lastActivity = millis();

    const auto& cfg = ConfigManager::instance().config();

    if (key == LV_KEY_ESC) {
        // Screen unlock is deliberately not cancellable -- that is the whole point
        // of it. The admin prompts are, per the feature's design: typing the PIN
        // arms the action, anything else backs out. Async for the same reason as
        // the success path: Cancel and the keypad live inside the overlay.
        if (_pinPurpose != PinPurpose::ScreenUnlock) {
            _pinPendingAction = PinPurpose::ScreenUnlock;   // dismiss only, no action
            lv_async_call([](void* p) { ((UIManager*)p)->finishPinUnlock(); }, this);
        }
        return;
    }

    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (_pinBuffer.length() > 0) {
            _pinBuffer.remove(_pinBuffer.length() - 1);
        }
    } else if (key == LV_KEY_ENTER) {
        // Still cooling down from earlier wrong guesses: refuse to even compare,
        // so guesses cannot be pipelined while the countdown runs.
        uint32_t wait = pinWaitRemaining();
        if (wait > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), t("pin_wait"), (int)wait);
            lv_obj_set_style_text_color(_pinStatus, theme::BATTERY_LOW(), 0);
            lv_label_set_text(_pinStatus, buf);
            _pinBuffer = "";
            if (_pinDots) lv_label_set_text(_pinDots, "");
            return;
        }
        // Case-insensitive comparison against whichever secret this prompt is for.
        String inputLower = _pinBuffer;
        inputLower.toLowerCase();
        String codeLower = (_pinPurpose == PinPurpose::ScreenUnlock)
                             ? cfg.security.pinCode
                             : cfg.security.adminPin;
        codeLower.toLowerCase();
        if (codeLower.length() >= 4 && inputLower == codeLower) {
            if (_pinPurpose == PinPurpose::ScreenUnlock) { _pinFails = 0; _pinWaitUntil = 0; }
            else                                         { _adminFails = 0; _adminWaitUntil = 0; }
            // Tear down asynchronously. onPinKey runs from the on-screen keypad's
            // and the Cancel button's own event callbacks, and both are CHILDREN of
            // _pinOverlay -- deleting it (and its group) synchronously from inside
            // its own dispatch is the pattern this codebase has already been bitten
            // by (see ChatScreen's modal dismissal). The follow-up action has to
            // move with it so it still runs after the overlay is gone.
            _pinPendingAction = _pinPurpose;
            lv_async_call([](void* p) { ((UIManager*)p)->finishPinUnlock(); }, this);
            return;
        } else {
            // Wrong PIN. First PIN_FREE_TRIES are unpenalised (fat fingers); after
            // that each further miss costs an escalating wait, capped so a
            // legitimate user is never locked out for long. Capped at 60s a 4-digit
            // PIN takes days to exhaust, which is enough: the SD card is the real
            // shortcut for anyone holding the device.
            static const uint16_t BACKOFF_S[] = { 5, 10, 30, 60 };
            static constexpr uint8_t PIN_FREE_TRIES = 3;
            const bool screen = (_pinPurpose == PinPurpose::ScreenUnlock);
            uint8_t&  fails   = screen ? _pinFails : _adminFails;
            uint32_t& until   = screen ? _pinWaitUntil : _adminWaitUntil;
            if (fails < 255) fails++;
            if (fails > PIN_FREE_TRIES) {
                uint8_t step = fails - PIN_FREE_TRIES - 1;
                if (step >= (uint8_t)(sizeof(BACKOFF_S) / sizeof(BACKOFF_S[0])))
                    step = (uint8_t)(sizeof(BACKOFF_S) / sizeof(BACKOFF_S[0])) - 1;
                until = millis() + (uint32_t)BACKOFF_S[step] * 1000UL;
                if (until == 0) until = 1;   // never collide with "free"
                _pinWaitShown = 0;
                char buf[32];
                snprintf(buf, sizeof(buf), t("pin_wait"), (int)BACKOFF_S[step]);
                lv_label_set_text(_pinStatus, buf);
            } else {
                lv_label_set_text(_pinStatus, t("pin_wrong"));
            }
            lv_obj_set_style_text_color(_pinStatus, theme::BATTERY_LOW(), 0);
            _pinBuffer = "";
        }
    } else if ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')) {
        if (pinWaitRemaining() > 0) return;   // ignore typing during the cooldown
        if (_pinBuffer.length() < 8) {
            _pinBuffer += (char)tolower(key);
            // Reset status to normal after typing
            lv_obj_set_style_text_color(_pinStatus, theme::TEXT_SECONDARY(), 0);
            lv_label_set_text(_pinStatus, "");
        }
    }

    // Update dots display
    if (_pinDots) {
        String dots;
        for (size_t i = 0; i < _pinBuffer.length(); i++) {
            if (i > 0) dots += " ";
            dots += "*";
        }
        lv_label_set_text(_pinDots, dots.c_str());
    }
}

void UIManager::dismissPinLock() {
    _isLocked = false;
    _pinBuffer = "";

    // Restore input to the group it was actually on when the overlay appeared.
    // _modalGroup is the wrong target: switchToModalGroup() puts only the overlay
    // CONTAINER in it and every editor immediately re-attaches its own
    // _editorGroup, so restoring _modalGroup left the keyboard and trackball
    // pointing at a group with nothing focusable in it. _pinPrevGroup is captured
    // in showPinLock() from the indev itself, so it is exact.
    lv_group_t* restore = _pinPrevGroup ? _pinPrevGroup : _inputGroup;
    _pinPrevGroup = nullptr;
    if (restore) {
        IInput::instance().attachToGroup(restore);
    }

    if (_pinOverlay) {
        if (_pinGroup) lv_group_remove_obj(_pinOverlay);
        lv_obj_del(_pinOverlay);
        _pinOverlay = nullptr;
        _pinDots = nullptr;
        _pinStatus = nullptr;
        _pinKeypad = nullptr;   // child of the overlay, already deleted with it
    }
    if (_pinGroup) {
        lv_group_del(_pinGroup);
        _pinGroup = nullptr;
    }

    // Wake display
    if (_dimmed) {
        const auto& dispCfg = ConfigManager::instance().config().display;
        Display::instance().setBrightness(dispCfg.brightness);
        if (dispCfg.kbdBacklight) {
            IInput::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
    }
    _lastActivity = millis();

    LOGLN("[UI] PIN lock dismissed");
}

// ---- Telemetry modal ----

static String buildTelemText(const Contact* contact, const TelemetryData* td) {
    String text;
    bool stale = td && (millis() - td->receivedAt >= TelemetryCache::STALE_MS);

    if (td && td->hasVoltage) {
        // Estimate percentage: 4.2V=100%, 3.0V=0% (linear approximation for LiPo)
        int pct = constrain((int)((td->voltage - 3.0f) / 1.2f * 100.0f), 0, 100);
        char buf[48];
        snprintf(buf, sizeof(buf), t("telem_battery"), td->voltage, pct);
        text += buf;
        text += "\n";
    }

    // Location — fresh telemetry (accurate), else the contact's advert / heard
    // position (precision unknown, prefixed "~" to flag it as approximate).
    ContactLocation loc = bestKnownLocation(contact->publicKey);
    if (loc.valid) {
        const auto& cfg = ConfigManager::instance().config();
        const String& fmt = cfg.messaging.locationFormat;
        String locStr;
        char latlonBuf[48];
        snprintf(latlonBuf, sizeof(latlonBuf), "%.6f, %.6f", loc.lat, loc.lon);
        if (fmt == "mgrs") {
            locStr = latLonToMGRS(loc.lat, loc.lon, 4);
        } else if (fmt == "both") {
            locStr = String(latlonBuf) + " (" + latLonToMGRS(loc.lat, loc.lon, 4) + ")";
        } else {
            locStr = latlonBuf;
        }
        if (loc.approximate) locStr = "~ " + locStr;   // advert/heard — may be coarse

        char lineBuf[96];
        snprintf(lineBuf, sizeof(lineBuf), t("telem_location"), locStr.c_str());
        text += lineBuf;
        text += "\n";

        // Distance from our position
        auto& gps = GPS::instance();
        FixStatus ourFix = gps.fixStatus();
        if (ourFix == FixStatus::LIVE || ourFix == FixStatus::LAST_KNOWN) {
            double ourLat = (ourFix == FixStatus::LIVE) ? gps.lat() : gps.lastPosition().lat;
            double ourLon = (ourFix == FixStatus::LIVE) ? gps.lon() : gps.lastPosition().lon;
            double dist = haversineMeters(ourLat, ourLon, loc.lat, loc.lon);
            String distStr = formatDistance(dist);
            char distBuf[48];
            snprintf(distBuf, sizeof(distBuf), t("telem_distance"), distStr.c_str());
            text += distBuf;
            text += "\n";
        }
    }

    if (td && (td->hasTemperature || td->hasHumidity || td->hasPressure)) {
        char envBuf[64];
        String envParts;
        if (td->hasTemperature) {
            snprintf(envBuf, sizeof(envBuf), "%.1f C", td->temperature);
            envParts += envBuf;
        }
        if (td->hasHumidity) {
            if (envParts.length() > 0) envParts += ", ";
            snprintf(envBuf, sizeof(envBuf), "%.0f%%", td->humidity);
            envParts += envBuf;
        }
        if (td->hasPressure) {
            if (envParts.length() > 0) envParts += ", ";
            snprintf(envBuf, sizeof(envBuf), "%.1f hPa", td->pressure);
            envParts += envBuf;
        }
        if (envParts.length() > 0) {
            char lineBuf[96];
            snprintf(lineBuf, sizeof(lineBuf), t("telem_environment"), envParts.c_str());
            text += lineBuf;
            text += "\n";
        }
    }

    if (td) {
        // Telemetry age
        uint32_t ageSec = (millis() - td->receivedAt) / 1000;
        char ageBuf[32];
        if (ageSec < 60)       snprintf(ageBuf, sizeof(ageBuf), "%ds", (int)ageSec);
        else if (ageSec < 3600) snprintf(ageBuf, sizeof(ageBuf), "%dm", (int)(ageSec / 60));
        else                   snprintf(ageBuf, sizeof(ageBuf), "%dh", (int)(ageSec / 3600));

        char updBuf[48];
        snprintf(updBuf, sizeof(updBuf), t("telem_updated"), ageBuf);
        text += updBuf;

        if (stale) {
            text += "\n";
            text += t("telem_stale");
        }
    }

    if (text.length() == 0) return String(t("telem_no_data"));
    return text;
}

void UIManager::showTelemetryModal(const ConvoId& id) {
    const auto& cfg = ConfigManager::instance().config();
    if (!cfg.messaging.requestTelemetry) return;
    if (id.type != ConvoId::DM) return;

    // Close existing modal if open
    if (_telemMsgbox) dismissTelemetryModal();

    // Find contact
    auto& contacts = ContactStore::instance();
    const Contact* contact = nullptr;
    size_t contactIdx = 0;
    for (size_t i = 0; i < contacts.count(); i++) {
        const Contact* c = contacts.findByIndex(i);
        if (c && c->shortId() == id.id) {
            contact = c;
            contactIdx = i;
            break;
        }
    }
    if (!contact) return;

    _telemContactId = id.id;

    // Build text from cache
    const TelemetryData* td = TelemetryCache::instance().get(contact->publicKey);
    _telemText = buildTelemText(contact, td);

    // Build widget via helper so updateTelemetryModal() can recreate with a
    // different button count (re-layouting a live btnmatrix doesn't work).
    buildTelemetryMsgbox(evalCanMap(contact->publicKey));

    // Auto-request if no cached data or stale
    if (!td || !TelemetryCache::instance().isFresh(contact->publicKey)) {
        uint32_t estTimeout = 0;
        if (MeshManager::instance().requestTelemetry(contactIdx, estTimeout)) {
            _telemPending = true;
            _telemTimeout = millis() + estTimeout;
            if (!td) {
                _telemText = t("telem_requesting");
                ModalDialog::setBody(_telemMsgbox, _telemText);
            }
        }
    }

    LOGF("[UI] Telemetry modal shown for %s\n", contact->name.c_str());
}

void UIManager::openMapAsync(void* user) {
    UIManager* self = static_cast<UIManager*>(user);
    if (!self) return;
    // A coordinate link (no contact) passes a null key so MapScreen centers on the
    // point without selecting a contact marker; the telemetry path passes the key.
    const uint8_t* key = self->_pendingMapHasKey ? self->_pendingMapKey : nullptr;
    self->showMapScreen(key, self->_pendingMapLat, self->_pendingMapLon,
                        self->_pendingMapName);
}

void UIManager::openMapAt(double lat, double lon, const String& name) {
    openMapAt(nullptr, lat, lon, name);
}

void UIManager::openMapAt(const uint8_t* pubKey, double lat, double lon, const String& name) {
    // Tiles are guaranteed present (callers only expose the action when
    // tilesAvailable()), so just defer the screen change — a touch cb must not
    // switch screens synchronously (matches the telemetry Map button flow).
    // Passing a pubKey lets MapScreen resolve the node's real marker (type +
    // name) instead of falling back to the generic "Chat" focus type.
    _pendingMapLat    = lat;
    _pendingMapLon    = lon;
    _pendingMapName   = name;
    if (pubKey) { memcpy(_pendingMapKey, pubKey, 32); _pendingMapHasKey = true; }
    else        { _pendingMapHasKey = false; }
    lv_async_call(&UIManager::openMapAsync, this);
}

void UIManager::showMapScreen(const uint8_t* pubKey, double lat, double lon,
                              const String& contactName) {
    _mapScreen.open(pubKey, lat, lon, contactName);
}

void UIManager::openGeneralMapAsync(void* user) {
    UIManager* self = static_cast<UIManager*>(user);
    if (self) self->_mapScreen.openGeneral();
}

void UIManager::showGeneralMap() {
    if (!TileLoader::instance().tilesAvailable()) {
        showToast(t("map_no_tiles"));
        return;
    }
    // Defer so we're not opening a screen from inside the status-bar tap event.
    lv_async_call(&UIManager::openGeneralMapAsync, this);
}

void UIManager::toggleAdminAsync(void* user) {
    UIManager* self = static_cast<UIManager*>(user);
    if (!self) return;
    if (self->_currentScreen == Screen::ADMIN) self->goHome();
    else                                       self->showScreen(Screen::ADMIN);
}

void UIManager::toggleAdminFromStatusBar() {
    // The gear's visibility already encodes most of this, but re-check at the
    // action site — same as the '0' shortcut (main.cpp) and, since the admin
    // lockout work, the T-Watch PEK path too (it did not check adminEnabled before).
    // admin_enabled is toggled from *inside* Admin, so the button can stay visible
    // for up to one status-bar tick (1 Hz) after it is turned off.
    if (!ConfigManager::instance().config().security.adminEnabled) return;

    // Lock guard. On T-Deck the key-lock overlay is only a centred card, not a
    // click-catching scrim — the sole barrier is Touch.cpp suppressing pointer
    // events. Don't let one line in another subsystem be all that guards Admin.
    if (_keyLocked || _isLocked) return;

    // The map is an lv_win over the content area, not a Screen — the status bar
    // stays visible and tappable above it, and showScreen() would not close it.
    // Switching underneath would strand Admin behind an opaque map that still
    // owns the input group, breaking the refocus chain when the map closes.
    if (_mapScreen.isOpen()) return;

    // Defer: showScreen() tears down the current screen (lv_obj_clean,
    // lv_group_remove_obj, and from Settings a config save + possible restart),
    // which must not run from inside this tap event.
    lv_async_call(&UIManager::toggleAdminAsync, this);
}

bool UIManager::evalCanMap(const uint8_t* pubKey) const {
    if (!pubKey) return false;
    // Any known position (telemetry, advert, or heard) — the map renders them all.
    return bestKnownLocation(pubKey).valid && TileLoader::instance().tilesAvailable();
}

void UIManager::buildTelemetryMsgbox(bool canMap) {
    if (_telemMsgbox) { ModalDialog::close(_telemMsgbox); _telemMsgbox = nullptr; }

    _telemHasMap = canMap;
    std::vector<String> btns = { t("btn_close"), t("btn_refresh") };
    if (canMap) btns.push_back(t("btn_map"));

    String title = String(LV_SYMBOL_EYE_OPEN " ") + t("telem_title");
    _telemMsgbox = ModalDialog::show(title, _telemText, btns,
        [this](lv_obj_t* dlg, int idx) { onTelemModalChoice(dlg, idx); });
}

void UIManager::onTelemModalChoice(lv_obj_t* dlg, int idx) {
    (void)dlg;
    if (idx == 0) {  // Close
        dismissTelemetryModal();
        return;
    }
    if (idx == 1) {  // Refresh — request, keep modal open and update body
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            if (c && c->shortId() == _telemContactId) {
                uint32_t estTimeout = 0;
                if (MeshManager::instance().requestTelemetry(i, estTimeout)) {
                    _telemPending = true;
                    _telemTimeout = millis() + estTimeout;
                    _telemText = t("telem_requesting");
                } else {
                    _telemText = t("telem_send_failed");
                }
                ModalDialog::setBody(_telemMsgbox, _telemText);
                break;
            }
        }
        return;
    }
    if (idx == 2) {  // Map
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            if (c && c->shortId() == _telemContactId) {
                ContactLocation loc = bestKnownLocation(c->publicKey);
                if (loc.valid) {
                    _pendingMapLat  = loc.lat;
                    _pendingMapLon  = loc.lon;
                    _pendingMapName = c->name;
                    memcpy(_pendingMapKey, c->publicKey, 32);
                    _pendingMapHasKey = true;
                    dismissTelemetryModal();
                    lv_async_call(&UIManager::openMapAsync, this);
                }
                break;
            }
        }
    }
}

void UIManager::updateTelemetryModal(const uint8_t* pubKey) {
    if (!_telemMsgbox || !pubKey) return;

    auto& contacts = ContactStore::instance();
    for (size_t i = 0; i < contacts.count(); i++) {
        const Contact* c = contacts.findByIndex(i);
        if (c && c->shortId() == _telemContactId) {
            if (memcmp(c->publicKey, pubKey, 32) != 0) return;

            const TelemetryData* td = TelemetryCache::instance().get(pubKey);
            _telemText = buildTelemText(c, td);
            _telemPending = false;

            const bool canMap = evalCanMap(pubKey);
            if (canMap != _telemHasMap) buildTelemetryMsgbox(canMap);  // button set changed
            else                        ModalDialog::setBody(_telemMsgbox, _telemText);
            break;
        }
    }

    // A telemetry reply may have just given us a position — reveal the chat
    // header map button underneath the modal if so.
    refreshChatHeaderButtons();
}

void UIManager::onTelemetryRetry(uint32_t newTimeoutMs) {
    if (!_telemMsgbox) return;
    _telemText = t("telem_retrying");
    ModalDialog::setBody(_telemMsgbox, _telemText);
    _telemTimeout = millis() + newTimeoutMs;
    LOGF("[UI] Telemetry retrying, extended timeout to %ums\n", newTimeoutMs);
}

void UIManager::showToast(const char* msg, uint32_t durationMs) {
    if (!msg || !msg[0]) return;
    // Wrapper lv_obj draws the rounded badge (lv_label alone won't render a
    // bg even with bg styles set — labels paint glyphs only).
    lv_obj_t* toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(toast);  // start clean
    lv_obj_set_style_bg_color(toast, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(toast, theme::ACCENT(), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_radius(toast, 6, 0);
    lv_obj_set_style_pad_hor(toast, 12, 0);
    lv_obj_set_style_pad_ver(toast, 6, 0);
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(toast);
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_text_font(lbl, FONT_NORMAL, 0);
    lv_label_set_text(lbl, msg);

#ifdef PLATFORM_TWATCH
    // Sit above the footer bar so the clock isn't covered.
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -theme::FOOTER_HEIGHT - theme::PAD_LARGE);
#else
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -24);
#endif

    // Auto-dismiss via one-shot timer that deletes the wrapper async.
    // lv_timer_set_repeat_count(timer, 1) tells LVGL to free the timer
    // itself after the callback returns — don't call lv_timer_del(t) here
    // or we'd double-free.
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* t) {
        lv_obj_t* obj = (lv_obj_t*)t->user_data;
        if (obj) lv_obj_del_async(obj);
    }, durationMs, toast);
    lv_timer_set_repeat_count(timer, 1);
}

void UIManager::switchToModalGroup(lv_obj_t* modalWidget) {
    if (_modalGroup) restoreFromModalGroup();  // clean up any stale modal group
    _modalGroup = lv_group_create();
    lv_group_add_obj(_modalGroup, modalWidget);
    lv_group_focus_obj(modalWidget);
    // Enable editing mode so encoder (trackball) navigates between buttons
    // inside the btnmatrix rather than cycling group objects
    lv_group_set_editing(_modalGroup, true);
    IInput::instance().attachToGroup(_modalGroup);
}

void UIManager::restoreFromModalGroup() {
    // If the PIN overlay is up, input belongs to it. An inbound SOS alert is
    // deliberately shown while PIN-locked, and its ModalDialog takes the indevs;
    // handing them back to _inputGroup on dismiss left the opaque PIN overlay on
    // screen with nothing able to deliver a key to it, so the device was dead
    // until a power cycle -- immediately after an emergency alert.
    if (_pinOverlay && _pinGroup) {
        if (_modalGroup) {
            if (_pinPrevGroup == _modalGroup) _pinPrevGroup = nullptr;
            lv_group_del(_modalGroup);
            _modalGroup = nullptr;
        }
        IInput::instance().attachToGroup(_pinGroup);
        lv_group_focus_obj(_pinOverlay);
        return;
    }
    if (_inputGroup) {
        IInput::instance().attachToGroup(_inputGroup);
    }
    if (_modalGroup) {
        lv_group_del(_modalGroup);
        _modalGroup = nullptr;
    }
}

// ─── Firmware update (SD-card install) ──────────────────────────────────────

void UIManager::checkForSdFirmware() {
    if (_fwPromptDismissed) return;
    if (_isLocked) return;  // don't surface the install prompt behind a PIN lock
    String ver;
    String path = FirmwareUpdater::findSdFirmware(/*autoMode=*/true, ver);
    if (path.length() == 0) return;
    showFirmwareInstallModal(path, ver);
}

void UIManager::showFirmwareInstallModal(const String& path, const String& version) {
    _fwPath = path;
    _fwUrl = "";              // SD install
    _fwVersion = version;
    buildFwInstallModal();
}

void UIManager::showWiFiInstallModal(const String& version, const String& url) {
    _fwPath = "";
    _fwUrl = url;             // WiFi install — download then flash
    _fwVersion = version;
    buildFwInstallModal();
}

void UIManager::buildFwInstallModal() {
    static char bodyBuf[160];
    snprintf(bodyBuf, sizeof(bodyBuf), t("fw_update_body"),
             _fwVersion.c_str(), MCLITE_VERSION);

    ModalDialog::show(t("fw_update_title"), bodyBuf, { t("btn_cancel"), t("fw_install") },
        [this](lv_obj_t* dlg, int idx) {
            ModalDialog::close(dlg);
            if (idx == 1) {
                doFirmwareInstall();         // Install
            } else {
                _fwPromptDismissed = true;   // Abort — don't nag again this session
                if (_fwUrl.length()) {       // WiFi offer declined — drop the link
                    WiFiManager::instance().disconnect();
                    _fwUrl = "";
                }
            }
        });
}

void UIManager::fwProgressCb(uint8_t percent, void* user) {
    UIManager* self = static_cast<UIManager*>(user);
    if (self && self->_fwBar) {
        // WiFi install: flash is the second half (50-100%); SD install: full range.
        uint8_t v = self->_fwUrl.length() ? (uint8_t)(50 + percent / 2) : percent;
        lv_bar_set_value(self->_fwBar, v, LV_ANIM_OFF);
        lv_refr_now(NULL);  // repaint between chunks (single-threaded)
    }
}

void UIManager::fwDownloadProgressCb(uint8_t percent, void* user) {
    UIManager* self = static_cast<UIManager*>(user);
    if (self && self->_fwBar) {
        lv_bar_set_value(self->_fwBar, percent / 2, LV_ANIM_OFF);  // download = first half
        lv_refr_now(NULL);
    }
}

void UIManager::doFirmwareInstall() {
    // Full-screen "installing" overlay with a progress bar.
    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ov, Display::width(), Display::height());
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, theme::BG_PRIMARY(), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(ov);
    lv_label_set_text(lbl, t("fw_installing"));
    lv_obj_set_style_text_color(lbl, theme::TEXT_PRIMARY(), 0);
    lv_obj_set_style_text_font(lbl, FONT_HEADING, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, theme::MODAL_TEXT_WIDTH);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -30);

    _fwBar = lv_bar_create(ov);
    lv_obj_set_size(_fwBar, theme::MODAL_TEXT_WIDTH, 16);
    lv_obj_align(_fwBar, LV_ALIGN_CENTER, 0, 20);
    lv_bar_set_range(_fwBar, 0, 100);
    lv_bar_set_value(_fwBar, 0, LV_ANIM_OFF);

    lv_refr_now(NULL);

    bool ok;
    if (_fwUrl.length() > 0) {
        // WiFi: download to SD (0-50%), then flash (50-100%).
        const char* dest = "/firmware/_ota.bin";
        bool dok = FirmwareUpdater::downloadToSd(_fwUrl.c_str(), dest, fwDownloadProgressCb, this);
        ok = dok && FirmwareUpdater::flashFromSd(dest, fwProgressCb, this);
        // While WiFi is still up, refresh the SD translations to match the new firmware, so
        // languages the user already has pick up any strings added this release instead of
        // falling back to English. Best-effort — never blocks or fails the firmware update.
        if (ok && _fwVersion.length() > 0) FirmwareUpdater::refreshLangFiles(_fwVersion);
        WiFiManager::instance().disconnect();
    } else {
        ok = FirmwareUpdater::flashFromSd(_fwPath.c_str(), fwProgressCb, this);
    }

    if (ok) {
        delay(300);
        ESP.restart();
        return;
    }

    // Failure: surface it briefly, then drop the overlay and carry on.
    lv_label_set_text(lbl, t("fw_update_failed"));
    lv_refr_now(NULL);
    delay(1800);
    _fwBar = nullptr;
    lv_obj_del(ov);
    _fwUrl = "";
    _fwPromptDismissed = true;
}

void UIManager::checkForWiFiUpdateOnBoot() {
    if (_isLocked) return;
    if (_modalGroup) return;  // an SD-install prompt is already up — let it win
    const auto& cfg = ConfigManager::instance().config();
    if (!cfg.wifi.autoUpdate || cfg.wifi.ssid.length() == 0) return;

    if (!WiFiManager::instance().connect(cfg.wifi.ssid, cfg.wifi.password)) {
        WiFiManager::instance().disconnect();  // quiet: no WiFi, no nag
        return;
    }

    RemoteRelease rel;
    bool found = UpdateChecker::checkLatest(rel);
    if (found && compareVersions(rel.version.c_str(), MCLITE_VERSION) > 0) {
        // Keep WiFi up — the install reuses the live connection for the download.
        showWiFiInstallModal(rel.version, rel.url);
    } else {
        WiFiManager::instance().disconnect();  // up-to-date / error → drop the link
    }
}

void UIManager::dismissTelemetryModal() {
    if (!_telemMsgbox) return;

    ModalDialog::close(_telemMsgbox);
    _telemMsgbox = nullptr;
    _telemText = "";
    _telemContactId = "";
    _telemPending = false;
    _telemTimeout = 0;
    // Cancel any in-flight telemetry retry too — otherwise checkTelemTimeout
    // would still fire after the modal is gone and transmit a flood request for
    // a contact-info pop-up the user already closed.
    MeshManager::instance().clearPendingTelemetry();
}

// --- Key Lock ---

void UIManager::engageKeyLock() {
    if (_keyLocked || _isLocked) return;  // Already locked or PIN-locked
    _keyLocked = true;
    showKeyLockOverlay();
    LOGLN("[UI] Key lock engaged");
}

void UIManager::disengageKeyLock() {
    if (!_keyLocked) return;
    _keyLocked = false;
    hideKeyLockOverlay();
    LOGLN("[UI] Key lock disengaged");
}

void UIManager::showKeyLockOverlay() {
    if (_keyLockOverlay) return;  // Already showing

#ifdef PLATFORM_TWATCH
    // T-Watch: full-screen modal backdrop catches all touches. On T-Watch
    // touch is the primary input, so the lock must physically block it.
    // The visible "Locked" card is centered inside.
    _keyLockOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_keyLockOverlay, Display::width(), Display::height());
    lv_obj_set_pos(_keyLockOverlay, 0, 0);
    lv_obj_set_style_bg_color(_keyLockOverlay, theme::SCRIM(), 0);
    lv_obj_set_style_bg_opa(_keyLockOverlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(_keyLockOverlay, 0, 0);
    lv_obj_set_style_radius(_keyLockOverlay, 0, 0);
    lv_obj_set_style_pad_all(_keyLockOverlay, 0, 0);
    lv_obj_add_flag(_keyLockOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_keyLockOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(_keyLockOverlay);

    lv_obj_t* card = lv_obj_create(_keyLockOverlay);
#else
    // T-Deck: centered card directly on lv_layer_top. QWERTY+trackball
    // input is blocked via handleKeyShortcuts checking isKeyLocked().
    _keyLockOverlay = lv_obj_create(lv_layer_top());
    lv_obj_t* card = _keyLockOverlay;
#endif

    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, theme::BG_SECONDARY(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, theme::PAD_LARGE, 0);
    lv_obj_set_style_pad_row(card, theme::PAD_SMALL, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, theme::TEXT_SECONDARY(), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(card);

    lv_obj_t* icon = lv_label_create(card);
    lv_label_set_text(icon, LV_SYMBOL_KEYBOARD);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, theme::TEXT_PRIMARY(), 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, t("key_locked"));
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY(), 0);

    lv_obj_t* hint = lv_label_create(card);
#ifdef PLATFORM_TWATCH
    lv_label_set_text(hint, t("key_lock_hint_watch"));
#else
    lv_label_set_text(hint, t("key_lock_hint"));
#endif
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, theme::TEXT_SECONDARY(), 0);
}

void UIManager::hideKeyLockOverlay() {
    if (!_keyLockOverlay) return;
    lv_obj_del(_keyLockOverlay);
    _keyLockOverlay = nullptr;
}

void UIManager::updateKeyLockToggle() {
    const auto& sec = ConfigManager::instance().config().security;
    if (sec.lockMode == "none") return;
    if (_isLocked) return;  // PIN lock already showing

    if (!IInput::instance().isPressed()) {
        _keyLockActioned = false;  // Reset for next hold
        return;
    }

    uint32_t held = IInput::instance().holdDurationMs();

    // Already acted this hold — check if we need to cancel a key lock (held into SOS)
    if (_keyLockActioned) {
        if (held >= SOS_HOLD_SHOW_MS && _keyLocked) {
            // User held past 2s — cancel the lock, SOS takes over
            _keyLocked = false;
            hideKeyLockOverlay();
        }
        return;
    }

    // 1s threshold reached — act immediately
    if (held >= KEY_LOCK_HOLD_MS) {
        _keyLockActioned = true;
        if (sec.lockMode == "pin" && sec.pinCode.length() >= 4) {
            // PIN lock takes precedence — show PIN screen
            showPinLock();
        } else if (_keyLocked) {
            disengageKeyLock();
        } else {
            engageKeyLock();
        }
    }
}

}  // namespace mclite
