#include "UIManager.h"
#include "theme.h"
#include "../mesh/MeshManager.h"
#include "../mesh/ContactStore.h"
#include "../mesh/ChannelStore.h"
#include "../hal/Display.h"
#include "../hal/GPS.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "../input/Keyboard.h"
#include "../input/Trackball.h"
#include "../input/Touch.h"
#include "../hal/Speaker.h"
#include "../hal/Battery.h"
#include "../i18n/I18n.h"
#include "../storage/TelemetryCache.h"
#include "../storage/TileLoader.h"
#include "../util/distance.h"
#include "../util/hex.h"
#include "../util/mgrs.h"
#include <helpers/BaseChatMesh.h>  // RESP_SERVER_LOGIN_OK

namespace mclite {

UIManager& UIManager::instance() {
    static UIManager inst;
    return inst;
}

bool UIManager::init() {
    // Create a new screen for main UI (boot screen may still be active)
    _mainScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_mainScreen, theme::BG_PRIMARY, 0);

    // Create LVGL input group and bind input devices
    _inputGroup = lv_group_create();
    lv_group_set_default(_inputGroup);
    if (Keyboard::instance().indev()) {
        lv_indev_set_group(Keyboard::instance().indev(), _inputGroup);
    }
    if (Trackball::instance().indev()) {
        lv_indev_set_group(Trackball::instance().indev(), _inputGroup);
    }

    // Create all UI components
    _statusBar.create(_mainScreen);
    _convoList.create(_mainScreen);
    _chatScreen.create(_mainScreen);
    _adminScreen.create(_mainScreen);

    // Wire up callbacks
    _convoList.onSelect([this](const ConvoId& id) {
        openChat(id);
    });

    _chatScreen.onSend([this](const ConvoId& id, const String& text) {
        handleSend(id, text);
    });

    _chatScreen.onRetry([this](const ConvoId& id, const String& text, uint32_t oldPacketId) {
        handleRetry(id, text, oldPacketId);
    });

    _chatScreen.onBack([this]() {
        goHome();
    });

    _chatScreen.onInfo([this](const ConvoId& id) {
        showTelemetryModal(id);
    });

    _lastActivity = millis();

    // Turn on keyboard backlight if enabled
    const auto& initCfg = ConfigManager::instance().config();
    if (initCfg.display.kbdBacklight) {
        Keyboard::instance().setBacklight(initCfg.display.kbdBrightness);
    }

    // Do NOT show any screen yet — loadMainScreen() will do that after boot
    Serial.println("[UI] Initialized");
    return true;
}

void UIManager::update() {
    uint32_t now = millis();

    // LVGL tick handler (runs indev readCb which sets _lastKey)
    lv_timer_handler();

    // Check for input activity to wake from dim (after LVGL so _lastKey is fresh)
    checkWake();

    // Periodic status bar update
    if (now - _lastStatusUpdate >= STATUS_UPDATE_MS) {
        _statusBar.update();
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
                Keyboard::instance().setBacklight(0);
            }
            _dimmed = true;
            // Auto-lock on dim — fallback chain: pin → key → none
            const auto& sec = cfg.security;
            if (sec.autoLock == "pin" && sec.lockMode == "pin" && sec.pinCode.length() >= 4 && !_isLocked) {
                showPinLock();
            } else if (sec.autoLock == "pin" && sec.lockMode == "key" && !_isLocked && !_keyLocked) {
                // Fallback: PIN auto-lock requested but only key lock available
                _keyLocked = true;
                showKeyLockOverlay();
                Serial.println("[UI] Key lock engaged (auto-dim, pin fallback)");
            } else if (sec.autoLock == "key" && (sec.lockMode == "key" || sec.lockMode == "pin") && !_isLocked && !_keyLocked) {
                _keyLocked = true;
                showKeyLockOverlay();
                Serial.println("[UI] Key lock engaged (auto-dim)");
            }
        }
    }

    // Telemetry request timeout
    if (_telemPending && _telemMsgbox && (int32_t)(now - _telemTimeout) >= 0) {
        _telemText = t("telem_no_response");
        lv_label_set_text(lv_msgbox_get_text(_telemMsgbox), _telemText.c_str());
        _telemPending = false;
        _telemTimeout = 0;
        MeshManager::instance().clearPendingTelemetry();
    }

    // Periodic convo list refresh (update timestamps like "12s", "3m")
    if (_currentScreen == Screen::CONVO_LIST && now - _lastConvoRefresh >= CONVO_REFRESH_MS) {
        _convoList.refresh();
        _lastConvoRefresh = now;
    }

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

    // Keyboard: any key pressed
    if (Keyboard::instance().lastKey() != 0) {
        activity = true;
    }

    // Trackball: pressed or moved
    if (Trackball::instance().isPressed() || Trackball::instance().hasMoved()) {
        activity = true;
    }

    // Touch: screen touched
    if (Touch::instance().isTouched()) {
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
            Keyboard::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
        // Consume the keyboard wake key so it doesn't pass through
        if (!_isLocked && Keyboard::instance().lastKey() != 0) {
            Keyboard::instance().clearKey();
        }
    }
}

void UIManager::loadMainScreen() {
    lv_scr_load(_mainScreen);
    showScreen(Screen::CONVO_LIST);
    lv_timer_handler();
}

void UIManager::showScreen(Screen screen) {
    // Dismiss telemetry modal if open (it's a top-level overlay)
    if (_telemMsgbox) dismissTelemetryModal();

    _convoList.hide();
    _chatScreen.hide();
    _adminScreen.hide();

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
    }
    _currentScreen = screen;
    _lastActivity = millis();

    // Wake display if dimmed
    if (_dimmed) {
        const auto& dispCfg = ConfigManager::instance().config().display;
        Display::instance().setBrightness(dispCfg.brightness);
        if (dispCfg.kbdBacklight) {
            Keyboard::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
    }
}

void UIManager::openChat(const ConvoId& id) {
    showScreen(Screen::CHAT);  // Hide other screens first
    _chatScreen.open(id);      // open() calls show() internally

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
    if (checkSOS(id, msg)) {
        // SOS alert handled — skip normal notification
    } else {
        // Normal notification with per-contact always-sound check
        auto& speaker = Speaker::instance();
        if (!speaker.isMuted()) {
            speaker.playNotification();
        } else if (id.type == ConvoId::DM) {
            // Check if this DM contact has always_sound enabled
            auto& contacts = ContactStore::instance();
            for (size_t i = 0; i < contacts.count(); i++) {
                Contact* c = contacts.findByIndex(i);
                if (c && c->shortId() == id.id && c->alwaysSound) {
                    speaker.playNotificationForced();
                    break;
                }
            }
        }
    }

    // Wake display
    if (_dimmed) {
        const auto& dispCfg = ConfigManager::instance().config().display;
        Display::instance().setBrightness(dispCfg.brightness);
        if (dispCfg.kbdBacklight) {
            Keyboard::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
    }
    _lastActivity = millis();
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

    // Button labels — must persist (static)
    static const char* btns[3];
    btns[0] = t("btn_dismiss");
    btns[1] = t("btn_sos_seen");
    btns[2] = "";
    String sosTitleStr = String(LV_SYMBOL_WARNING " ") + t("sos_alert_title");
    _sosMsgbox = lv_msgbox_create(NULL, sosTitleStr.c_str(),
                                  _sosAlertText.c_str(), btns, false);
    lv_obj_center(_sosMsgbox);
    lv_obj_set_width(_sosMsgbox, 280);

    // Style: red border, high contrast
    lv_obj_set_style_border_color(_sosMsgbox, theme::BATTERY_LOW, 0);
    lv_obj_set_style_border_width(_sosMsgbox, 3, 0);
    lv_obj_set_style_bg_color(_sosMsgbox, theme::BG_SECONDARY, 0);
    lv_obj_set_style_text_color(_sosMsgbox, theme::TEXT_PRIMARY, 0);

    // Switch trackball/keyboard to modal group so they can't navigate behind
    lv_obj_t* btnmatrix = lv_msgbox_get_btns(_sosMsgbox);
    if (btnmatrix) switchToModalGroup(btnmatrix);

    // Button callback
    lv_obj_add_event_cb(_sosMsgbox, sosButtonCb, LV_EVENT_VALUE_CHANGED, this);

    // Disengage key lock so user can respond without unlocking first.
    // PIN lock (_isLocked) stays engaged — don't bypass security.
    if (_keyLocked) disengageKeyLock();

    // Start SOS sound
    const auto& cfg = ConfigManager::instance().config();
    Speaker::instance().startSOS(cfg.sosRepeat);

    // Wake display to max brightness
    Display::instance().setBrightness(255);
    if (cfg.display.kbdBacklight) {
        Keyboard::instance().setBacklight(cfg.display.kbdBrightness);
    }
    _dimmed = false;
    _lastActivity = millis();

    Serial.printf("[UI] SOS alert from %s\n", msg.senderName.c_str());
}

void UIManager::sosButtonCb(lv_event_t* e) {
    UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (!self || !self->_sosMsgbox) return;

    lv_obj_t* btnmatrix = lv_msgbox_get_btns(self->_sosMsgbox);
    uint16_t btnIdx = lv_btnmatrix_get_selected_btn(btnmatrix);

    // btn 0 = "Dismiss" (no reply), btn 1 = "SOS seen" (send reply)
    self->dismissSOSAlert(btnIdx == 1);
}

void UIManager::dismissSOSAlert(bool sendReply) {
    Speaker::instance().stopSOS();

    // Send "SOS acknowledged" reply to the conversation it came from. Rooms
    // are excluded: we'd have to broadcast to the whole room (no addressable
    // sender from a 4-byte prefix), and per decision #11 we don't push to rooms.
    // For ROOM SOS, "SOS seen" just stops the sound + closes the modal.
    if (sendReply && _sosConvoId.type != ConvoId::ROOM) {
        const String replyText = "Acknowledged SOS";  // Always English — must NOT start with SOS keyword to avoid retriggering alert
        Message reply;
        reply.fromSelf  = true;
        reply.text      = replyText;
        reply.timestamp = GPS::instance().isTimeSynced()
            ? GPS::instance().currentTimestamp() : (millis() / 1000);

        if (_sosIsDM && _sosContactIndex >= 0) {
            reply.packetId = MeshManager::instance().sendMessage(_sosContactIndex, replyText.c_str());
            reply.status = reply.packetId ? MessageStatus::SENDING : MessageStatus::FAILED;
        } else if (_sosConvoId.type == ConvoId::CHANNEL) {
            // Find channel index and send as group message
            Channel* ch = ChannelStore::instance().findByName(_sosConvoId.id);
            if (ch) {
                MeshManager::instance().sendGroupMessage(ch->index, replyText.c_str());
            }
            reply.status = MessageStatus::SENT;  // Channels are fire-and-forget
        }

        Conversation* convo = MessageStore::instance().getConversation(_sosConvoId);
        String displayName = convo ? convo->displayName : _sosConvoId.id;
        bool isPrivate = convo ? convo->isPrivate : false;
        MessageStore::instance().addMessage(_sosConvoId, displayName, isPrivate, reply);

        Serial.println("[UI] SOS reply sent");
    }

    // Restore input group and close modal
    if (_sosMsgbox) {
        restoreFromModalGroup();
        lv_msgbox_close(_sosMsgbox);
        _sosMsgbox = nullptr;
    }

    _sosAlertText = "";  // Free the persisted text

    // Restore normal brightness
    const auto& dispCfgSos = ConfigManager::instance().config().display;
    Display::instance().setBrightness(dispCfgSos.brightness);
    if (dispCfgSos.kbdBacklight) {
        Keyboard::instance().setBacklight(dispCfgSos.kbdBrightness);
    }

    Serial.println("[UI] SOS alert dismissed");
}

void UIManager::onAckReceived(uint32_t packetId) {
    MessageStore::instance().updateStatus(packetId, MessageStatus::DELIVERED);
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

void UIManager::handleSend(const ConvoId& id, const String& text) {
    uint32_t packetId = 0;
    bool isDM   = (id.type == ConvoId::DM);
    bool isRoom = (id.type == ConvoId::ROOM);

    if (isDM) {
        // Find contact index
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const auto* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                packetId = MeshManager::instance().sendMessage(i, text);
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
                packetId = MeshManager::instance().sendRoomPost(i, text);
                break;
            }
        }
    } else {
        // Find channel index
        auto* ch = ChannelStore::instance().findByName(id.id);
        if (ch) {
            packetId = MeshManager::instance().sendGroupMessage(ch->index, text);
        }
    }

    // Determine initial status:
    // DMs and rooms: SENDING (waiting for ACK; retry pipeline handles DELIVERED/FAILED)
    // Channels: SENT immediately (fire-and-forget, no ACK possible)
    MessageStatus initialStatus;
    if (packetId == 0) {
        initialStatus = MessageStatus::FAILED;
    } else if (isDM || isRoom) {
        initialStatus = MessageStatus::SENDING;
    } else {
        initialStatus = MessageStatus::SENT;
    }

    // Add to local store
    Message msg;
    msg.fromSelf  = true;
    msg.text      = text;
    msg.timestamp = GPS::instance().isTimeSynced()
        ? GPS::instance().currentTimestamp() : (millis() / 1000);
    msg.status    = initialStatus;
    msg.packetId  = packetId;

    Conversation* convo = MessageStore::instance().getConversation(id);
    String displayName = convo ? convo->displayName : id.id;
    bool isPrivate = convo ? convo->isPrivate : false;
    MessageStore::instance().addMessage(id, displayName, isPrivate, msg);

    // Update chat view
    _chatScreen.addMessageToView(msg);

    _lastActivity = millis();
}

// ─── Room callbacks (wired from main.cpp setupMeshCallbacks) ───

void UIManager::onRoomMessageReceived(size_t roomIdx, const String& roomName,
                                       const uint8_t* senderPrefix /* 4 B */,
                                       const String& text, uint32_t timestamp) {
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
        Serial.printf("[UI] Room '%s' logged in (perms=%u)\n",
                      roomName.c_str(), (unsigned)permissions);
    } else {
        Serial.printf("[UI] Room '%s' login failed (status=%u)\n",
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
            Serial.printf("[UI] Room '%s' login attempt %u; next in %us\n",
                          rooms[i].name.c_str(), (unsigned)_loginAttempt[i],
                          (unsigned)delaySec);
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
        Serial.printf("[UI] Room idx=%u: chat-open re-login\n", (unsigned)roomIdx);
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
        Serial.printf("[UI] Room idx=%u: silence-triggered re-login\n", (unsigned)roomIdx);
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
    if (id.type == ConvoId::DM) {
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const auto* c = contacts.findByIndex(i);
            if (c && c->shortId() == id.id) {
                newPacketId = MeshManager::instance().sendMessage(i, text);
                break;
            }
        }
    } else {  // ROOM
        const auto& cfgRooms = ConfigManager::instance().config().roomServers;
        for (size_t i = 0; i < cfgRooms.size() && i < MAX_ROOMS; i++) {
            if (cfgRooms[i].publicKey.length() == 64 &&
                cfgRooms[i].publicKey.substring(0, 16) == id.id) {
                newPacketId = MeshManager::instance().sendRoomPost(i, text);
                break;
            }
        }
    }

    if (newPacketId == 0) return;

    // Update the existing failed message in-place
    target->packetId = newPacketId;
    target->status = MessageStatus::SENDING;
    MessageStore::instance().saveHistory(id);

    _chatScreen.refresh();
    _lastActivity = millis();
}

void UIManager::showSetupScreen(SetupReason reason) {
    // Hide all normal screens
    _convoList.hide();
    _chatScreen.hide();
    _adminScreen.hide();

    // Full-screen overlay on top of everything
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, 320, 240);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, theme::BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 20, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(overlay, 12, 0);

    // Icon
    lv_obj_t* icon = lv_label_create(overlay);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, theme::BATTERY_LOW, 0);

    // Title
    lv_obj_t* title = lv_label_create(overlay);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Message
    lv_obj_t* msg = lv_label_create(overlay);
    lv_obj_set_style_text_color(msg, theme::TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(msg, 280);

    switch (reason) {
        case NO_SD:
            lv_label_set_text(icon, LV_SYMBOL_WARNING);
            lv_label_set_text(title, t("no_sd_title"));
            lv_label_set_text(msg, t("no_sd_msg"));
            break;

        case NO_CONFIG:
            lv_label_set_text(icon, LV_SYMBOL_SD_CARD);
            lv_obj_set_style_text_color(icon, theme::TEXT_PRIMARY, 0);
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
    lv_obj_set_style_text_color(footer, lv_color_make(0x55, 0x55, 0x77), 0);
    lv_label_set_text(footer, "MCLite v" MCLITE_VERSION);

    Serial.printf("[UI] Setup screen shown (reason=%d)\n", (int)reason);
}

void UIManager::insertLocation() {
    if (_currentScreen != Screen::CHAT) return;
    auto& gps = GPS::instance();
    if (gps.fixStatus() == FixStatus::NO_FIX) return;

    String loc = "@ " + gps.formatLocationWithStatus();
    const ConvoId* id = _chatScreen.currentConvo();
    if (id) {
        handleSend(*id, loc);
    }
}

void UIManager::updateSOSHold() {
    if (_keyLocked) return;  // No SOS trigger while key-locked

    auto& tb = Trackball::instance();
    uint32_t held = tb.holdDurationMs();

    if (!tb.isPressed() || held < SOS_HOLD_SHOW_MS) {
        // Not held long enough or released — cancel countdown
        if (_sosCountdownActive) {
            if (_sosCountdownLabel) {
                lv_obj_del(_sosCountdownLabel);
                _sosCountdownLabel = nullptr;
            }
            _sosCountdownActive = false;
        }
        if (!tb.isPressed()) {
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

    // Show or update countdown label
    if (!_sosCountdownActive) {
        _sosCountdownActive = true;
        _sosCountdownLabel = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_opa(_sosCountdownLabel, LV_OPA_80, 0);
        lv_obj_set_style_bg_color(_sosCountdownLabel, lv_color_black(), 0);
        lv_obj_set_style_text_color(_sosCountdownLabel, theme::BATTERY_LOW, 0);
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
        Serial.println("[UI] SOS send failed — no radio or no contacts");
        return;
    }

    // Build SOS message with GPS location if available (live or last known)
    String sosText = cfg.sosKeyword;
    auto& gps = GPS::instance();
    if (gps.fixStatus() != FixStatus::NO_FIX) {
        sosText += " @ " + gps.formatLocationWithStatus();
    }

    Serial.printf("[SOS] Begin burst: %u contacts, %u channels\n",
                  (unsigned)contacts.count(), (unsigned)ChannelStore::instance().count());

    // Send to every contact
    uint32_t sent = 0;
    for (size_t i = 0; i < contacts.count(); i++) {
        Contact* c = contacts.findByIndex(i);
        if (!c || !c->sendSos) continue;

        uint32_t packetId = mesh.sendMessage(i, sosText);
        Serial.printf("[SOS] DM %s: packetId=%u %s\n",
                      c->name.c_str(), packetId,
                      packetId ? "queued" : "FAILED (pool?)");

        // Add to local message store
        ConvoId id{ConvoId::DM, c->shortId()};
        Message msg;
        msg.fromSelf  = true;
        msg.text      = sosText;
        msg.timestamp = gps.isTimeSynced()
            ? gps.currentTimestamp() : (millis() / 1000);
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
        uint32_t packetId = mesh.sendGroupMessage(allCh[i].index, sosText);
        Serial.printf("[SOS] CH %s: packetId=%u %s\n",
                      allCh[i].name.c_str(), packetId,
                      packetId ? "queued" : "FAILED (pool?)");

        ConvoId id{ConvoId::CHANNEL, allCh[i].name};
        Message msg;
        msg.fromSelf  = true;
        msg.text      = sosText;
        msg.timestamp = gps.isTimeSynced()
            ? gps.currentTimestamp() : (millis() / 1000);
        msg.status    = packetId ? MessageStatus::SENT : MessageStatus::FAILED;
        msg.packetId  = packetId;

        Conversation* convo = MessageStore::instance().getConversation(id);
        String displayName = convo ? convo->displayName : allCh[i].name;
        MessageStore::instance().addMessage(id, displayName, false, msg);

        if (packetId) sent++;

        MeshManager::instance().update();
        delay(50);
    }

    // Show confirmation toast via a brief modal
    char confirmBuf[64];
    snprintf(confirmBuf, sizeof(confirmBuf), t("sos_sent"), sent);
    static const char* sentBtns[2];
    sentBtns[0] = t("btn_ok");
    sentBtns[1] = "";
    String sentTitleStr = String(LV_SYMBOL_WARNING " ") + t("sos_sent_title");
    lv_obj_t* msgbox = lv_msgbox_create(NULL, sentTitleStr.c_str(), confirmBuf, sentBtns, false);
    lv_obj_center(msgbox);
    lv_obj_set_style_border_color(msgbox, theme::BATTERY_LOW, 0);
    lv_obj_set_style_border_width(msgbox, 2, 0);
    lv_obj_set_style_bg_color(msgbox, theme::BG_SECONDARY, 0);
    lv_obj_set_style_text_color(msgbox, theme::TEXT_PRIMARY, 0);

    lv_obj_t* btnmatrix = lv_msgbox_get_btns(msgbox);
    if (btnmatrix && _inputGroup) {
        lv_group_add_obj(_inputGroup, btnmatrix);
    }
    lv_obj_add_event_cb(msgbox, [](lv_event_t* e) {
        lv_obj_t* mbox = lv_event_get_current_target(e);
        lv_obj_t* btns = lv_msgbox_get_btns(mbox);
        if (btns) lv_group_remove_obj(btns);
        lv_msgbox_close(mbox);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Refresh chat view if open
    if (_currentScreen == Screen::CHAT) {
        _chatScreen.refresh();
    } else if (_currentScreen == Screen::CONVO_LIST) {
        _convoList.refresh();
    }

    Serial.printf("[UI] SOS broadcast sent to %d recipient(s)\n", sent);
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
            uint32_t ts = gps.isTimeSynced()
                ? gps.currentTimestamp() : (millis() / 1000);

            for (size_t i = 0; i < contacts.count(); i++) {
                Contact* c = contacts.findByIndex(i);
                if (!c || !c->sendSos) continue;
                uint32_t packetId = mesh.sendMessage(i, alertText);

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
                uint32_t packetId = mesh.sendGroupMessage(ch.index, alertText);

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
        }

        _batteryAlertSent = true;
        Serial.printf("[UI] Battery low alert sent: %d%%\n", pct);
    } else if (pct > threshold + 5 && _batteryAlertSent) {
        // Hysteresis reset
        _batteryAlertSent = false;
        Serial.println("[UI] Battery alert reset (hysteresis)");
    }
}

void UIManager::showPinLock() {
    if (_pinOverlay) return;  // Already showing

    _isLocked = true;
    _pinBuffer = "";

    _pinOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_pinOverlay, 320, 240);
    lv_obj_set_pos(_pinOverlay, 0, 0);
    lv_obj_set_style_bg_color(_pinOverlay, theme::BG_PRIMARY, 0);
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
    lv_obj_set_style_text_color(lockIcon, theme::ACCENT, 0);
    lv_label_set_text(lockIcon, ICON_LOCK);

    // Title
    lv_obj_t* title = lv_label_create(_pinOverlay);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY, 0);
    lv_label_set_text(title, t("pin_title"));

    // PIN dots display
    _pinDots = lv_label_create(_pinOverlay);
    lv_obj_set_style_text_font(_pinDots, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_pinDots, theme::TEXT_PRIMARY, 0);
    lv_label_set_text(_pinDots, "");

    // Status message (for errors)
    _pinStatus = lv_label_create(_pinOverlay);
    lv_obj_set_style_text_font(_pinStatus, FONT_SMALL, 0);
    lv_obj_set_style_text_color(_pinStatus, theme::TEXT_SECONDARY, 0);
    lv_label_set_text(_pinStatus, t("pin_hint"));

    // Use a dedicated group so trackball/keyboard can't focus away from the overlay
    _pinGroup = lv_group_create();
    lv_obj_add_flag(_pinOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(_pinGroup, _pinOverlay);
    lv_group_focus_obj(_pinOverlay);
    if (Keyboard::instance().indev())
        lv_indev_set_group(Keyboard::instance().indev(), _pinGroup);
    if (Trackball::instance().indev())
        lv_indev_set_group(Trackball::instance().indev(), _pinGroup);
    lv_obj_add_event_cb(_pinOverlay, pinKeyCb, LV_EVENT_KEY, this);

    Serial.println("[UI] PIN lock shown");
}

void UIManager::pinKeyCb(lv_event_t* e) {
    UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(e));
    uint32_t key = lv_event_get_key(e);
    self->onPinKey(key);
}

void UIManager::onPinKey(uint32_t key) {
    // Wake display on any keypress while locked and dimmed
    if (_dimmed) {
        const auto& dispCfg = ConfigManager::instance().config().display;
        Display::instance().setBrightness(dispCfg.brightness);
        if (dispCfg.kbdBacklight) {
            Keyboard::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
    }
    _lastActivity = millis();

    const auto& cfg = ConfigManager::instance().config();

    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (_pinBuffer.length() > 0) {
            _pinBuffer.remove(_pinBuffer.length() - 1);
        }
    } else if (key == LV_KEY_ENTER) {
        // Case-insensitive comparison
        String inputLower = _pinBuffer;
        inputLower.toLowerCase();
        String codeLower = cfg.security.pinCode;
        codeLower.toLowerCase();
        if (inputLower == codeLower) {
            dismissPinLock();
            return;
        } else {
            // Wrong PIN
            lv_obj_set_style_text_color(_pinStatus, theme::BATTERY_LOW, 0);
            lv_label_set_text(_pinStatus, t("pin_wrong"));
            _pinBuffer = "";
        }
    } else if ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')) {
        if (_pinBuffer.length() < 8) {
            _pinBuffer += (char)tolower(key);
            // Reset status to normal after typing
            lv_obj_set_style_text_color(_pinStatus, theme::TEXT_SECONDARY, 0);
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

    // Restore input group for keyboard/trackball before deleting PIN group
    if (_inputGroup) {
        if (Keyboard::instance().indev())
            lv_indev_set_group(Keyboard::instance().indev(), _inputGroup);
        if (Trackball::instance().indev())
            lv_indev_set_group(Trackball::instance().indev(), _inputGroup);
    }

    if (_pinOverlay) {
        if (_pinGroup) lv_group_remove_obj(_pinOverlay);
        lv_obj_del(_pinOverlay);
        _pinOverlay = nullptr;
        _pinDots = nullptr;
        _pinStatus = nullptr;
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
            Keyboard::instance().setBacklight(dispCfg.kbdBrightness);
        }
        _dimmed = false;
    }
    _lastActivity = millis();

    Serial.println("[UI] PIN lock dismissed");
}

// ---- Telemetry modal ----

static String buildTelemText(const Contact* contact, const TelemetryData* td) {
    if (!td) return String(t("telem_no_data"));

    String text;
    bool stale = (millis() - td->receivedAt >= TelemetryCache::STALE_MS);

    if (td->hasVoltage) {
        // Estimate percentage: 4.2V=100%, 3.0V=0% (linear approximation for LiPo)
        int pct = constrain((int)((td->voltage - 3.0f) / 1.2f * 100.0f), 0, 100);
        char buf[48];
        snprintf(buf, sizeof(buf), t("telem_battery"), td->voltage, pct);
        text += buf;
        text += "\n";
    }

    if (td->hasLocation) {
        const auto& cfg = ConfigManager::instance().config();
        const String& fmt = cfg.messaging.locationFormat;
        String locStr;
        char latlonBuf[48];
        snprintf(latlonBuf, sizeof(latlonBuf), "%.6f, %.6f", td->lat, td->lon);
        if (fmt == "mgrs") {
            locStr = latLonToMGRS(td->lat, td->lon, 4);
        } else if (fmt == "both") {
            locStr = String(latlonBuf) + " (" + latLonToMGRS(td->lat, td->lon, 4) + ")";
        } else {
            locStr = latlonBuf;
        }

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
            double dist = haversineMeters(ourLat, ourLon, td->lat, td->lon);
            String distStr = formatDistance(dist);
            char distBuf[48];
            snprintf(distBuf, sizeof(distBuf), t("telem_distance"), distStr.c_str());
            text += distBuf;
            text += "\n";
        }
    }

    if (td->hasTemperature || td->hasHumidity || td->hasPressure) {
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

    // Update age
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
                lv_label_set_text(lv_msgbox_get_text(_telemMsgbox), _telemText.c_str());
            }
        }
    }

    Serial.printf("[UI] Telemetry modal shown for %s\n", contact->name.c_str());
}

void UIManager::telemBtnCb(lv_event_t* e) {
    UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (!self || !self->_telemMsgbox) return;

    uint16_t idx = lv_msgbox_get_active_btn(self->_telemMsgbox);

    if (idx == 0) {
        // Close
        self->dismissTelemetryModal();
    } else if (idx == 1) {
        // Refresh — find contact and request
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            if (c && c->shortId() == self->_telemContactId) {
                uint32_t estTimeout = 0;
                if (MeshManager::instance().requestTelemetry(i, estTimeout)) {
                    self->_telemPending = true;
                    self->_telemTimeout = millis() + estTimeout;
                    self->_telemText = t("telem_requesting");
                } else {
                    self->_telemText = t("telem_send_failed");
                }
                lv_label_set_text(lv_msgbox_get_text(self->_telemMsgbox),
                                  self->_telemText.c_str());
                break;
            }
        }
    } else if (idx == 2) {
        // Map — find contact's cached location and launch MapScreen
        auto& contacts = ContactStore::instance();
        for (size_t i = 0; i < contacts.count(); i++) {
            const Contact* c = contacts.findByIndex(i);
            if (c && c->shortId() == self->_telemContactId) {
                const TelemetryData* td = TelemetryCache::instance().get(c->publicKey);
                if (td && td->hasLocation) {
                    self->_pendingMapLat  = td->lat;
                    self->_pendingMapLon  = td->lon;
                    self->_pendingMapName = c->name;
                    self->dismissTelemetryModal();
                    lv_async_call(&UIManager::openMapAsync, self);
                }
                break;
            }
        }
    }
}

void UIManager::openMapAsync(void* user) {
    UIManager* self = static_cast<UIManager*>(user);
    if (!self) return;
    self->showMapScreen(self->_pendingMapLat, self->_pendingMapLon, self->_pendingMapName);
}

void UIManager::showMapScreen(double lat, double lon, const String& contactName) {
    _mapScreen.open(lat, lon, contactName);
}

bool UIManager::evalCanMap(const uint8_t* pubKey) const {
    if (!pubKey) return false;
    const TelemetryData* td = TelemetryCache::instance().get(pubKey);
    return td && td->hasLocation
        && TelemetryCache::instance().isFresh(pubKey)
        && TileLoader::instance().tilesAvailable();
}

void UIManager::buildTelemetryMsgbox(bool canMap) {
    // Tear down any existing msgbox widget (but keep _telemText/_telemContactId).
    if (_telemMsgbox) {
        restoreFromModalGroup();
        lv_msgbox_close(_telemMsgbox);
        _telemMsgbox = nullptr;
    }

    _telemBtns[0] = t("btn_close");
    _telemBtns[1] = t("btn_refresh");
    if (canMap) { _telemBtns[2] = t("btn_map"); _telemBtns[3] = ""; }
    else        { _telemBtns[2] = "";           _telemBtns[3] = nullptr; }

    String title = String(LV_SYMBOL_EYE_OPEN " ") + t("telem_title");
    _telemMsgbox = lv_msgbox_create(NULL, title.c_str(), _telemText.c_str(), _telemBtns, false);
    lv_obj_center(_telemMsgbox);
    lv_obj_set_width(_telemMsgbox, 280);
    lv_obj_set_height(_telemMsgbox, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(_telemMsgbox, 200, 0);
    lv_obj_set_style_bg_color(_telemMsgbox, theme::BG_SECONDARY, 0);
    lv_obj_set_style_text_color(_telemMsgbox, theme::TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(_telemMsgbox, FONT_NORMAL, 0);

    lv_obj_t* content = lv_msgbox_get_text(_telemMsgbox);
    if (content) {
        lv_obj_t* contentParent = lv_obj_get_parent(content);
        lv_obj_set_style_max_height(contentParent, 140, 0);
        lv_obj_add_flag(contentParent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(contentParent, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(contentParent, LV_SCROLLBAR_MODE_AUTO);
    }

    lv_obj_t* btnm = lv_msgbox_get_btns(_telemMsgbox);
    if (btnm) switchToModalGroup(btnm);

    lv_obj_add_event_cb(_telemMsgbox, telemBtnCb, LV_EVENT_VALUE_CHANGED, this);
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
            const bool hadMap = (_telemBtns[2] && _telemBtns[2][0] != '\0');
            if (canMap != hadMap) {
                // Button set changed — recreate the msgbox so layout is correct.
                buildTelemetryMsgbox(canMap);
            } else {
                // Text-only update.
                lv_label_set_text(lv_msgbox_get_text(_telemMsgbox), _telemText.c_str());
            }
            break;
        }
    }
}

void UIManager::switchToModalGroup(lv_obj_t* modalWidget) {
    if (_modalGroup) restoreFromModalGroup();  // clean up any stale modal group
    _modalGroup = lv_group_create();
    lv_group_add_obj(_modalGroup, modalWidget);
    lv_group_focus_obj(modalWidget);
    // Enable editing mode so encoder (trackball) navigates between buttons
    // inside the btnmatrix rather than cycling group objects
    lv_group_set_editing(_modalGroup, true);
    if (Keyboard::instance().indev())
        lv_indev_set_group(Keyboard::instance().indev(), _modalGroup);
    if (Trackball::instance().indev())
        lv_indev_set_group(Trackball::instance().indev(), _modalGroup);
}

void UIManager::restoreFromModalGroup() {
    if (_inputGroup) {
        if (Keyboard::instance().indev())
            lv_indev_set_group(Keyboard::instance().indev(), _inputGroup);
        if (Trackball::instance().indev())
            lv_indev_set_group(Trackball::instance().indev(), _inputGroup);
    }
    if (_modalGroup) {
        lv_group_del(_modalGroup);
        _modalGroup = nullptr;
    }
}

void UIManager::dismissTelemetryModal() {
    if (!_telemMsgbox) return;

    restoreFromModalGroup();
    lv_msgbox_close(_telemMsgbox);
    _telemMsgbox = nullptr;
    _telemText = "";
    _telemContactId = "";
    _telemPending = false;
    _telemTimeout = 0;
}

// --- Key Lock ---

void UIManager::engageKeyLock() {
    if (_keyLocked || _isLocked) return;  // Already locked or PIN-locked
    _keyLocked = true;
    showKeyLockOverlay();
    Serial.println("[UI] Key lock engaged");
}

void UIManager::disengageKeyLock() {
    if (!_keyLocked) return;
    _keyLocked = false;
    hideKeyLockOverlay();
    Serial.println("[UI] Key lock disengaged");
}

void UIManager::showKeyLockOverlay() {
    if (_keyLockOverlay) return;  // Already showing

    _keyLockOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_keyLockOverlay, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(_keyLockOverlay, theme::BG_SECONDARY, 0);
    lv_obj_set_style_bg_opa(_keyLockOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_keyLockOverlay, theme::PAD_LARGE, 0);
    lv_obj_set_style_pad_row(_keyLockOverlay, theme::PAD_SMALL, 0);
    lv_obj_set_style_radius(_keyLockOverlay, 8, 0);
    lv_obj_set_style_border_width(_keyLockOverlay, 2, 0);
    lv_obj_set_style_border_color(_keyLockOverlay, theme::TEXT_SECONDARY, 0);
    lv_obj_clear_flag(_keyLockOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_keyLockOverlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_keyLockOverlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(_keyLockOverlay, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* icon = lv_label_create(_keyLockOverlay);
    lv_label_set_text(icon, LV_SYMBOL_KEYBOARD);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, theme::TEXT_PRIMARY, 0);

    lv_obj_t* title = lv_label_create(_keyLockOverlay);
    lv_label_set_text(title, t("key_locked"));
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, theme::TEXT_PRIMARY, 0);

    lv_obj_t* hint = lv_label_create(_keyLockOverlay);
    lv_label_set_text(hint, t("key_lock_hint"));
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, theme::TEXT_SECONDARY, 0);
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

    auto& tb = Trackball::instance();
    bool pressed = tb.isPressed();

    if (!pressed) {
        _keyLockActioned = false;  // Reset for next hold
        return;
    }

    uint32_t held = tb.holdDurationMs();

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
