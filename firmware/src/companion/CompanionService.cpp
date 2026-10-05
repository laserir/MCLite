#include "CompanionService.h"
#include "util/log.h"
#include "CompanionProtocol.h"

#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "util/hex.h"            // pubKeyToShortId
#include "../mesh/MeshManager.h"
#include "../mesh/MCLiteMesh.h"   // mesh() reads: contacts, channels, RTC
#include "../mesh/ContactStore.h"
#include "../mesh/ChannelStore.h"
#include "../ui/UIManager.h"      // handleSend() — send + on-device parity
#include "../storage/MessageStore.h"  // ConvoId
#include "../hal/Battery.h"
#include "../storage/SDCard.h"
#include "../util/TimeHelper.h"

#include <MeshCore.h>   // PUB_KEY_SIZE, MAX_PATH_SIZE
#include <helpers/ContactInfo.h>
#include <SD.h>
#include <esp_random.h>
#include <cstring>

namespace mclite {

#ifdef PLATFORM_TWATCH
static const char* MANUFACTURER = "LilyGo T-Watch Ultra";
#else
static const char* MANUFACTURER = "LilyGo T-Deck Plus";
#endif

CompanionService& CompanionService::instance() {
    static CompanionService inst;
    return inst;
}

void CompanionService::resetSessionState() {
    _appVer = 0;
    _offlineLen = 0;
    _syncResponsePending = false;
    _bfActive = false;
    _bfConvoIdx = 0;
    _bfMsgIdx = 0;
    _contactsIterating = false;
    _contactCursor = 0;
    _contactCount = 0;
    _contactsSince = 0;
    _mostRecentLastmod = 0;
    for (auto& p  : _pending)      p.active  = false;   // stop tracking ACKs for a gone client
    for (auto& pl : _pendingLogin) pl.active = false;   // don't leak a login push to the next client
}

void CompanionService::begin(BaseSerialInterface* iface) {
    if (_iface == iface) return;
    if (_iface) end();
    _iface = iface;
    resetSessionState();
    _wasConnected = false;   // first connection is a clean edge
    if (_iface) _iface->enable();
    LOGLN("[Companion] interface enabled");
}

void CompanionService::end() {
    if (!_iface) return;
    _iface->disable();
    _iface = nullptr;
    resetSessionState();   // a transport torn down while connected never hits the loop() disconnect edge
    _wasConnected = false;
    LOGLN("[Companion] interface disabled");
}

uint32_t CompanionService::ensureBlePin() {
    auto& cfg = ConfigManager::instance().config();
    if (cfg.ble.pin < 100000 || cfg.ble.pin > 999999) {
        cfg.ble.pin = 100000 + (esp_random() % 900000);   // random 6-digit passkey
        ConfigManager::instance().save();
        LOGF("[Companion] generated BLE pairing PIN %06lu\n", (unsigned long)cfg.ble.pin);
    }
    return cfg.ble.pin;
}

void CompanionService::loop() {
    // Deferred reboot to apply a config-mutating command (e.g. CMD_SET_CHANNEL).
    // Checked before the _iface guard so it fires even if the transport has closed.
    if (_rebootAtMs && (int32_t)(millis() - _rebootAtMs) >= 0) { delay(50); ESP.restart(); }

    if (!_iface) return;

    // Reset per-session state when a client drops, so stale queued frames and a
    // stale negotiated version never leak into the next connection.
    bool conn = _iface->isConnected();
    if (!conn && _wasConnected) resetSessionState();
    _wasConnected = conn;

    size_t len = _iface->checkRecvFrame(_cmd);
    if (len > 0) { handleFrame(len); return; }
    // No inbound frame — drive deferred work one frame per tick, as the transport's
    // send queue drains. A SYNC reply owed but previously un-sendable (queue full)
    // is retried here so it lands without the app re-asking.
    if (_syncResponsePending) {
        if (trySendSyncResponse()) _syncResponsePending = false;
        return;
    }
    if (_contactsIterating && !_iface->isWriteBusy()) pumpContacts();
}

void CompanionService::handleFrame(size_t len) {
    const uint8_t cmd = _cmd[0];
    switch (cmd) {
        case CMD_APP_START:        cmdAppStart(len);        break;
        case CMD_DEVICE_QUERY:     cmdDeviceQuery(len);     break;
        case CMD_SEND_TXT_MSG:     cmdSendTxtMsg(len);      break;
        case CMD_SEND_CHANNEL_TXT_MSG: cmdSendChannelTxtMsg(len); break;
        case CMD_SEND_TELEMETRY_REQ: cmdSendTelemetryReq(len); break;
        case CMD_SEND_CONTROL_DATA:  cmdSendControlData(len);  break;
        case CMD_SEND_ANON_REQ:      cmdSendAnonReq(len);      break;
        case CMD_SEND_STATUS_REQ:    cmdSendStatusReq(len);    break;
        case CMD_SEND_TRACE_PATH:    cmdSendTracePath(len);    break;
        case CMD_SEND_LOGIN:       cmdSendLogin(len);       break;
        case CMD_GET_DEVICE_TIME:  cmdGetDeviceTime();      break;
        case CMD_SET_DEVICE_TIME:  cmdSetDeviceTime(len);   break;
        case CMD_SEND_SELF_ADVERT: cmdSendSelfAdvert(len);  break;
        case CMD_GET_BATT_AND_STORAGE: cmdGetBattAndStorage(); break;
        case CMD_GET_CONTACTS:     cmdGetContacts(len);     break;
        case CMD_GET_CONTACT_BY_KEY: cmdGetContactByKey(len); break;
        case CMD_GET_CHANNEL:      cmdGetChannel(len);      break;
        case CMD_SET_CHANNEL:      cmdSetChannel(len);      break;
        case CMD_ADD_UPDATE_CONTACT: cmdAddUpdateContact(len); break;
        case CMD_REMOVE_CONTACT:   cmdRemoveContact(len);   break;
        case CMD_SHARE_CONTACT:    cmdShareContact(len);    break;
        case CMD_SET_ADVERT_NAME:  cmdSetAdvertName(len);   break;
        case CMD_SET_RADIO_PARAMS: cmdSetRadioParams(len);  break;
        case CMD_SET_RADIO_TX_POWER: cmdSetTxPower(len);    break;
        case CMD_SET_DEVICE_PIN:   cmdSetDevicePin(len);    break;
        case CMD_SET_PATH_HASH_MODE: cmdSetPathHashMode(len); break;
        case CMD_SET_DEFAULT_FLOOD_SCOPE: cmdSetDefaultFloodScope(len); break;
        case CMD_GET_DEFAULT_FLOOD_SCOPE: cmdGetDefaultFloodScope();    break;
        case CMD_SET_FLOOD_SCOPE_KEY: cmdSetFloodScopeKey(len);  break;
        case CMD_RESET_PATH:       cmdResetPath(len);       break;
        case CMD_EXPORT_CONTACT:   cmdExportContact(len);   break;
        case CMD_IMPORT_CONTACT:   cmdImportContact(len);   break;
        case CMD_GET_AUTOADD_CONFIG: cmdGetAutoaddConfig(); break;
        case CMD_REBOOT:           cmdReboot(len);          break;
        case CMD_SYNC_NEXT_MESSAGE: cmdSyncNextMessage();   break;
        case CMD_LOGOUT:           writeOK();               break;   // no room sessions yet
        case CMD_HAS_CONNECTION:   writeErr(ERR_CODE_NOT_FOUND); break;
        // Everything else — including all config/radio/contact/channel/key WRITE
        // commands — is refused (messaging + read-only scope).
        default:
            // Name the opcode. The app renders a generic "Unsupported Command --
            // please update your companion firmware", so without this line neither
            // we nor the user can tell which command it actually was.
            LOGF("[Companion] unsupported cmd %u (len %u)\n", (unsigned)_cmd[0], (unsigned)len);
            writeErr(ERR_CODE_UNSUPPORTED_CMD);
            break;
    }
}

// CMD_APP_START -> RESP_CODE_SELF_INFO
// Layout mirrors MyMesh.cpp:1019-1057 (companion-v1.15.0).
void CompanionService::cmdAppStart(size_t len) {
    if (len < 8) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }

    const auto& radio = ConfigManager::instance().config().radio;
    const String& name = ConfigManager::instance().config().deviceName;
    const uint8_t* pubkey = MeshManager::instance().selfPubKey();
    if (!pubkey) { writeErr(ERR_CODE_BAD_STATE); return; }

    int i = 0;
    _out[i++] = RESP_CODE_SELF_INFO;
    _out[i++] = COMPANION_ADV_TYPE_CHAT;
    _out[i++] = (uint8_t)radio.txPower;
    _out[i++] = (uint8_t)LORA_TX_POWER;          // max TX power
    memcpy(&_out[i], pubkey, PUB_KEY_SIZE); i += PUB_KEY_SIZE;

    int32_t lat = 0, lon = 0;                    // MCLite does not advertise location
    memcpy(&_out[i], &lat, 4); i += 4;
    memcpy(&_out[i], &lon, 4); i += 4;

    _out[i++] = 0;   // multi_acks (v7+)
    _out[i++] = 0;   // advert_loc_policy
    _out[i++] = 0;   // telemetry mode bits (v5+)
    _out[i++] = 1;   // manual_add_contacts (MCLite manages contacts via config)

    uint32_t freq = (uint32_t)(radio.frequency * 1000.0f);   // kHz
    memcpy(&_out[i], &freq, 4); i += 4;
    uint32_t bw = (uint32_t)(radio.bandwidth * 1000.0f);     // Hz
    memcpy(&_out[i], &bw, 4); i += 4;
    _out[i++] = radio.spreadingFactor;
    _out[i++] = radio.codingRate;

    int tlen = name.length();
    if (i + tlen > MAX_FRAME_SIZE) tlen = MAX_FRAME_SIZE - i;
    memcpy(&_out[i], name.c_str(), tlen); i += tlen;

    _iface->writeFrame(_out, i);
    LOGF("[Companion] APP_START -> SELF_INFO (%d B)\n", i);

    // App is now fully connected (DEVICE_QUERY set _appVer first) — replay history.
    backfillHistory();
}

// CMD_DEVICE_QUERY -> RESP_CODE_DEVICE_INFO
// Layout mirrors MyMesh.cpp:996-1014 (companion-v1.15.0).
void CompanionService::cmdDeviceQuery(size_t len) {
    if (len >= 2) _appVer = _cmd[1];   // protocol version the app understands

    const auto& radio = ConfigManager::instance().config().radio;

    int i = 0;
    _out[i++] = RESP_CODE_DEVICE_INFO;
    _out[i++] = COMPANION_FW_VER_CODE;
    _out[i++] = MAX_CONTACTS / 2;        // v3+
    _out[i++] = MAX_GROUP_CHANNELS;      // v3+

    uint32_t blePin = 0;                 // no BLE auth on the WiFi transport
    memcpy(&_out[i], &blePin, 4); i += 4;

    memset(&_out[i], 0, 12);
    strncpy((char*)&_out[i], __DATE__, 12);   // build date (12 B field)
    i += 12;

    memset(&_out[i], 0, 40);
    strncpy((char*)&_out[i], MANUFACTURER, 40);
    i += 40;

    memset(&_out[i], 0, 20);
    {
        char ver[20];
        snprintf(ver, sizeof(ver), "v%s", MCLITE_VERSION);
        strncpy((char*)&_out[i], ver, 20);
    }
    i += 20;

    _out[i++] = 0;                       // client_repeat (v9+)
    _out[i++] = radio.pathHashMode;      // path_hash_mode (v10+)

    _iface->writeFrame(_out, i);
    LOGF("[Companion] DEVICE_QUERY (appVer=%u) -> DEVICE_INFO (%d B)\n", _appVer, i);
}

// CMD_SEND_TXT_MSG -> RESP_CODE_SENT (DM). Layout: [1]=txt_type [2]=attempt
// [3..6]=timestamp [7..12]=6-byte pubkey prefix [13..]=text. (MyMesh.cpp:1057+)
// Routes through MeshManager::sendMessage so MCLite's ACK/retry pipeline tracks
// it; the returned packetId doubles as the protocol's expected_ack token.
void CompanionService::cmdSendTxtMsg(size_t len) {
    if (len < 14) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }

    uint8_t txt_type = _cmd[1];
    if (txt_type != COMPANION_TXT_TYPE_PLAIN) { writeErr(ERR_CODE_UNSUPPORTED_CMD); return; }
    const uint8_t* prefix = &_cmd[7];   // 6-byte pubkey prefix

    // Resolve the recipient: mesh lookup gives the full pubkey, then map to the
    // MCLite ContactStore for its conversation shortId.
    ContactInfo* c = mesh->lookupContactByPubKey(prefix, 6);
    if (!c) { writeErr(ERR_CODE_NOT_FOUND); return; }
    Contact* sc = ContactStore::instance().findByPublicKey(c->id.pub_key);
    if (!sc) { writeErr(ERR_CODE_NOT_FOUND); return; }

    _cmd[len] = 0;   // null-terminate text (buffer is MAX_FRAME_SIZE+1)
    const char* text = (const char*)&_cmd[13];

    // Route through UIManager so the DM is sent AND mirrored on-device (TX parity).
    ConvoId id{ConvoId::DM, sc->shortId()};
    uint32_t packetId = UIManager::instance().handleSend(id, String(text));
    if (packetId == 0) { writeErr(ERR_CODE_TABLE_FULL); return; }

    noteSent(packetId);                 // track for the ACK -> SEND_CONFIRMED bridge

    uint32_t expected_ack = packetId;   // opaque token; matched by SEND_CONFIRMED push
    uint32_t est_timeout  = 8000;       // rough estimate (ms) for the client's pending UI
    _out[0] = RESP_CODE_SENT;
    _out[1] = 0;                        // direct/flood unknown at this layer
    memcpy(&_out[2], &expected_ack, 4);
    memcpy(&_out[6], &est_timeout, 4);
    _iface->writeFrame(_out, 10);
}

// CMD_SEND_CHANNEL_TXT_MSG -> OK (fire-and-forget). Layout: [1]=txt_type
// [2]=channel_idx [3..6]=timestamp [7..]=text. channel_idx is the mesh channel
// index (as returned by GET_CHANNEL), so send via the base GroupChannel API to
// avoid MCLite's ChannelStore-index remapping.
void CompanionService::cmdSendChannelTxtMsg(size_t len) {
    if (len < 7) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }

    uint8_t txt_type = _cmd[1];
    uint8_t channel_idx = _cmd[2];
    // _cmd[3..6] = app timestamp (ignored — handleSend stamps with bestEpoch)
    if (txt_type != COMPANION_TXT_TYPE_PLAIN) { writeErr(ERR_CODE_UNSUPPORTED_CMD); return; }

    ChannelDetails ch;
    if (!mesh->getChannel(channel_idx, ch)) { writeErr(ERR_CODE_NOT_FOUND); return; }

    _cmd[len] = 0;
    const char* text = (const char*)&_cmd[7];

    // Route through UIManager (by channel name) so it's sent AND mirrored on-device.
    ConvoId id{ConvoId::CHANNEL, String(ch.name)};
    if (UIManager::instance().handleSend(id, String(text))) writeOK();
    else writeErr(ERR_CODE_NOT_FOUND);
}

// CMD_SEND_TELEMETRY_REQ -> RESP_CODE_SENT. Layout: [1..3]=reserved [4..35]=32-byte
// contact pubkey. Sends a MeshCore telemetry request over the mesh (same path as
// the on-device chat-header button); the async reply arrives later as
// PUSH_CODE_TELEMETRY_RESPONSE (see onTelemetryResponse). Gated by the existing
// messaging.request_telemetry flag, and bounded by the single pending-telemetry slot.
void CompanionService::cmdSendTelemetryReq(size_t len) {
    if (len < 36) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }       // 1 code + 3 reserved + 32 key
    if (!ConfigManager::instance().config().messaging.requestTelemetry) {
        writeErr(ERR_CODE_BAD_STATE); return;                       // telemetry requests disabled
    }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }

    const uint8_t* pubKey = &_cmd[4];
    if (!mesh->lookupContactByPubKey(pubKey, PUB_KEY_SIZE)) {       // precise "unknown contact"
        writeErr(ERR_CODE_NOT_FOUND); return;
    }
    // Single-slot: don't clobber a UI/auto request already awaiting a reply.
    if (MeshManager::instance().isTelemetryPending()) { writeErr(ERR_CODE_BAD_STATE); return; }

    uint32_t est_timeout = 0;
    if (!MeshManager::instance().requestTelemetryByKey(pubKey, est_timeout)) {
        writeErr(ERR_CODE_BAD_STATE); return;
    }

    // RESP_CODE_SENT mirrors cmdSendTxtMsg: [1]=route(0) [2..5]=token(0, none) [6..9]=est_timeout.
    _out[0] = RESP_CODE_SENT;
    _out[1] = 0;
    uint32_t token = 0;
    memcpy(&_out[2], &token, 4);
    memcpy(&_out[6], &est_timeout, 4);
    _iface->writeFrame(_out, 10);
}

// MeshManager forwards a contact's telemetry reply here -> PUSH_CODE_TELEMETRY_RESPONSE.
// Direct push (like onAckConfirmed), carrying the verbatim CayenneLPP for the app to
// parse. Layout: [0]=0x8B [1]=reserved(0) [2..7]=6-byte pubkey prefix [8..]=raw LPP.
void CompanionService::cmdSendControlData(size_t len) {
    // The app's "Discover Nodes / Discover Repeaters". Mirrors the reference
    // firmware's guard exactly: at least one flag byte, and the high bit set --
    // MeshCore only accepts that subset zero-hop, so anything else would be
    // transmitted and then dropped by every receiver.
    if (len < 2 || (_cmd[1] & 0x80) == 0) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }

    // Transmit-only and stores nothing, so it needs no permission gate -- same
    // reasoning as CMD_SEND_SELF_ADVERT.
    if (MeshManager::instance().sendControlData(&_cmd[1], len - 1)) writeOK();
    else                                                            writeErr(ERR_CODE_BAD_STATE);
}

void CompanionService::onControlData(const uint8_t* data, uint8_t len,
                                     int8_t snrQ4, int8_t rssi, uint8_t pathLen) {
    if (!clientConnected()) return;
    int n = len;
    // This clamp is reachable, not defensive: a packet payload runs to 184 bytes
    // while a companion frame holds 172, so a large control payload genuinely has
    // to be cut. Header is [code][snr][rssi][path_len].
    if (n > MAX_FRAME_SIZE - 4) n = MAX_FRAME_SIZE - 4;
    _out[0] = PUSH_CODE_CONTROL_DATA;
    _out[1] = (uint8_t)snrQ4;
    _out[2] = (uint8_t)rssi;
    _out[3] = pathLen;
    if (n > 0 && data) memcpy(&_out[4], data, n);
    _iface->writeFrame(_out, 4 + (n > 0 ? n : 0));
}

void CompanionService::onTelemetryResponse(const uint8_t* pubKey, const uint8_t* lpp, uint8_t lppLen) {
    if (!clientConnected() || !pubKey) return;
    int n = lppLen;
    if (n > MAX_FRAME_SIZE - 8) n = MAX_FRAME_SIZE - 8;   // clamp (LPP is small; defensive)
    _out[0] = PUSH_CODE_TELEMETRY_RESPONSE;
    _out[1] = 0;
    memcpy(&_out[2], pubKey, 6);
    if (n > 0 && lpp) memcpy(&_out[8], lpp, n);
    _iface->writeFrame(_out, 8 + (n > 0 ? n : 0));
}

// CMD_SEND_ANON_REQ -> RESP_CODE_SENT. Layout: [1..32]=node pubkey [33..]=request data.
// Sends an anonymous request over the mesh to a node addressed only by pubkey (a known
// contact or not — a non-contact gets a transient ADV_TYPE_NONE slot). The async reply
// arrives as PUSH_CODE_BINARY_RESPONSE (see onAnonResponse). Gated by settings==full
// (advanced capability) and bounded by a single pending slot.
void CompanionService::cmdSendAnonReq(size_t len) {
    if (len <= 1 + PUB_KEY_SIZE) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }  // need key + >=1 data byte
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    if (MeshManager::instance().isAnonReqPending()) { writeErr(ERR_CODE_BAD_STATE); return; }

    const uint8_t* pubKey = &_cmd[1];
    const uint8_t* data   = &_cmd[1 + PUB_KEY_SIZE];
    uint8_t dataLen       = (uint8_t)(len - (1 + PUB_KEY_SIZE));

    uint32_t tag = 0, est_timeout = 0;
    if (!MeshManager::instance().sendAnonReqByKey(pubKey, data, dataLen, tag, est_timeout)) {
        writeErr(ERR_CODE_BAD_STATE); return;
    }
    // RESP_CODE_SENT: [1]=route(0) [2..5]=tag (real — the app matches the later
    // PUSH_CODE_BINARY_RESPONSE by it) [6..9]=est_timeout.
    _out[0] = RESP_CODE_SENT;
    _out[1] = 0;
    memcpy(&_out[2], &tag, 4);
    memcpy(&_out[6], &est_timeout, 4);
    _iface->writeFrame(_out, 10);
}

// MeshManager forwards an anon-request reply here -> PUSH_CODE_BINARY_RESPONSE.
// Layout: [0]=0x8C [1]=reserved(0) [2..5]=tag [6..]=verbatim response payload.
void CompanionService::onAnonResponse(uint32_t tag, const uint8_t* data, uint8_t len) {
    if (!clientConnected()) return;
    int n = len;
    if (n > MAX_FRAME_SIZE - 6) n = MAX_FRAME_SIZE - 6;   // clamp defensively
    _out[0] = PUSH_CODE_BINARY_RESPONSE;
    _out[1] = 0;
    memcpy(&_out[2], &tag, 4);
    if (n > 0 && data) memcpy(&_out[6], data, n);
    _iface->writeFrame(_out, 6 + (n > 0 ? n : 0));
}

// CMD_SEND_STATUS_REQ -> RESP_CODE_SENT. [1..32]=contact pubkey. Sends a MeshCore status
// request to a known contact; the async reply arrives as PUSH_CODE_STATUS_RESPONSE (see
// onStatusResponse). Single pending slot. Ungated (benign read-only diagnostic).
void CompanionService::cmdSendStatusReq(size_t len) {
    if (len < 1 + PUB_KEY_SIZE) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    const uint8_t* pubKey = &_cmd[1];
    if (!mesh->lookupContactByPubKey(pubKey, PUB_KEY_SIZE)) { writeErr(ERR_CODE_NOT_FOUND); return; }
    if (MeshManager::instance().isStatusReqPending()) { writeErr(ERR_CODE_BAD_STATE); return; }

    uint32_t tag = 0, est_timeout = 0;
    if (!MeshManager::instance().sendStatusReqByKey(pubKey, tag, est_timeout)) {
        writeErr(ERR_CODE_BAD_STATE); return;
    }
    _out[0] = RESP_CODE_SENT;
    _out[1] = 0;
    memcpy(&_out[2], &tag, 4);
    memcpy(&_out[6], &est_timeout, 4);
    _iface->writeFrame(_out, 10);
}

// MeshManager forwards a status reply here -> PUSH_CODE_STATUS_RESPONSE.
// Layout: [0]=0x87 [1]=reserved(0) [2..7]=6-byte pubkey prefix [8..]=verbatim status blob.
void CompanionService::onStatusResponse(const uint8_t* pubKey, const uint8_t* data, uint8_t len) {
    if (!clientConnected() || !pubKey) return;
    int n = len;
    if (n > MAX_FRAME_SIZE - 8) n = MAX_FRAME_SIZE - 8;
    _out[0] = PUSH_CODE_STATUS_RESPONSE;
    _out[1] = 0;
    memcpy(&_out[2], pubKey, 6);
    if (n > 0 && data) memcpy(&_out[8], data, n);
    _iface->writeFrame(_out, 8 + (n > 0 ? n : 0));
}

// CMD_SEND_TRACE_PATH -> RESP_CODE_SENT. Layout: [1..4]=tag [5..8]=auth [9]=flags [10..]=path.
// Traces the given path; the async reply arrives as PUSH_CODE_TRACE_DATA (see onTraceData).
// Ungated (benign diagnostic). path_sz = flags & 0x03 (encoded path-hash width).
void CompanionService::cmdSendTracePath(size_t len) {
    if (len <= 10) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    uint8_t path_len = (uint8_t)(len - 10);
    uint8_t flags = _cmd[9];
    uint8_t path_sz = flags & 0x03;
    if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) {
        writeErr(ERR_CODE_ILLEGAL_ARG); return;
    }
    uint32_t tag = 0, auth = 0, est_timeout = 0;
    memcpy(&tag,  &_cmd[1], 4);
    memcpy(&auth, &_cmd[5], 4);
    if (!MeshManager::instance().sendTracePath(tag, auth, flags, &_cmd[10], path_len, est_timeout)) {
        writeErr(ERR_CODE_BAD_STATE); return;
    }
    _out[0] = RESP_CODE_SENT;
    _out[1] = 0;
    memcpy(&_out[2], &tag, 4);
    memcpy(&_out[6], &est_timeout, 4);
    _iface->writeFrame(_out, 10);
}

// MeshManager forwards a trace reply here -> PUSH_CODE_TRACE_DATA. Frame mirrors the
// reference onTraceRecv: [0x89][0][path_len][flags][tag:4][auth:4][hashes:path_len]
// [snrs:path_len>>path_sz][final_snr:1].
void CompanionService::onTraceData(uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* path_snrs,
                                   const uint8_t* path_hashes, uint8_t path_len, int8_t final_snr) {
    if (!clientConnected()) return;
    uint8_t path_sz = flags & 0x03;
    int snr_len = path_len >> path_sz;
    if (12 + path_len + snr_len + 1 > MAX_FRAME_SIZE) return;   // too long (matches reference guard)
    int i = 0;
    _out[i++] = PUSH_CODE_TRACE_DATA;
    _out[i++] = 0;
    _out[i++] = path_len;
    _out[i++] = flags;
    memcpy(&_out[i], &tag,  4); i += 4;
    memcpy(&_out[i], &auth, 4); i += 4;
    if (path_len > 0 && path_hashes) { memcpy(&_out[i], path_hashes, path_len); i += path_len; }
    if (snr_len  > 0 && path_snrs)   { memcpy(&_out[i], path_snrs, snr_len);    i += snr_len;  }
    _out[i++] = (uint8_t)final_snr;
    _iface->writeFrame(_out, i);
}

// CMD_SEND_LOGIN -> RESP_CODE_SENT. Layout: [1..32]=32-byte room pubkey, [33..]=password
// (remainder, <=15). Logs into an already-configured room/repeater over the mesh (reuses the
// on-device loginRoom path). A blank app password uses the configured one; a wrong non-blank
// password triggers an instant one-shot retry with the configured password (see onRoomLoginResult).
void CompanionService::cmdSendLogin(size_t len) {
    if (len < 33) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    const auto& cfg = ConfigManager::instance().config();

    // Resolve the 32-byte room pubkey to a config room index.
    char keyHex[PUB_KEY_SIZE * 2 + 1];
    for (int i = 0; i < PUB_KEY_SIZE; i++) sprintf(keyHex + i * 2, "%02x", _cmd[1 + i]);
    keyHex[PUB_KEY_SIZE * 2] = '\0';
    int roomIdx = -1;
    for (size_t i = 0; i < cfg.roomServers.size() && i < MAX_ROOMS; i++) {
        if (cfg.roomServers[i].publicKey.equalsIgnoreCase(keyHex)) { roomIdx = (int)i; break; }
    }
    if (roomIdx < 0) { writeErr(ERR_CODE_NOT_FOUND); return; }

    // Password = frame remainder, clamped to 15 (MeshCore room-password limit).
    size_t pwLen = len - 33;
    if (pwLen > 15) pwLen = 15;
    char password[16];
    memcpy(password, &_cmd[33], pwLen);
    password[pwLen] = '\0';

    uint32_t est = 0;
    bool ok, fallbackEligible = false;
    if (pwLen > 0) {
        ok = MeshManager::instance().loginRoom((size_t)roomIdx, password, est);
        const String& configPw = cfg.roomServers[roomIdx].password;
        // Eligible only when config has a (different) non-empty password to try.
        fallbackEligible = configPw.length() > 0 && !configPw.equals(password);
    } else {
        ok = MeshManager::instance().loginRoom((size_t)roomIdx, est);  // configured password
    }
    if (!ok) { writeErr(ERR_CODE_BAD_STATE); return; }

    PendingLogin& p = _pendingLogin[roomIdx];
    p.active = true; p.retried = false; p.fallbackEligible = fallbackEligible;
    memcpy(p.prefix, &_cmd[1], 6);

    // RESP_CODE_SENT mirrors cmdSendTxtMsg: [1]=route(0) [2..5]=token(0) [6..9]=est_timeout.
    _out[0] = RESP_CODE_SENT;
    _out[1] = 0;
    uint32_t token = 0;
    memcpy(&_out[2], &token, 4);
    memcpy(&_out[6], &est, 4);
    _iface->writeFrame(_out, 10);
}

// MeshManager forwards every room-login response here. We only act on logins the app
// initiated (an active _pendingLogin), so background/on-device auto-logins push nothing.
void CompanionService::onRoomLoginResult(size_t roomIdx, uint8_t status, uint8_t permissions,
                                         uint8_t aclPerms, uint8_t fwLevel) {
    if (roomIdx >= MAX_ROOMS) return;
    PendingLogin& p = _pendingLogin[roomIdx];
    if (!p.active) return;

    if (status == 0 /* RESP_SERVER_LOGIN_OK */) {
        p.active = false;
        if (!clientConnected()) return;
        // Reference layout (companion_radio): [0]=0x85 [1]=perms [2..7]=prefix [8..11]=tag
        // [12]=v7 ACL perms [13]=firmware ver level — 14 bytes.
        _out[0] = PUSH_CODE_LOGIN_SUCCESS;
        _out[1] = permissions;
        memcpy(&_out[2], p.prefix, 6);
        uint32_t tag = 0;
        memcpy(&_out[8], &tag, 4);   // tag not surfaced by onRoomLogin; app matches on prefix
        _out[12] = aclPerms;         // v7 ACL permissions (data[7])
        _out[13] = fwLevel;          // firmware version level (data[12])
        _iface->writeFrame(_out, 14);
        return;
    }

    // Failure: instant one-shot fallback to the configured password, if eligible.
    if (!p.retried && p.fallbackEligible) {
        p.retried = true;
        uint32_t est = 0;
        MeshManager::instance().loginRoom(roomIdx, est);  // configured password; suppress interim 0x86
        return;
    }

    p.active = false;
    if (!clientConnected()) return;
    // Reference layout: [0]=0x86 [1]=0 reserved [2..7]=prefix — 8 bytes.
    _out[0] = PUSH_CODE_LOGIN_FAIL;
    _out[1] = 0;                     // reserved
    memcpy(&_out[2], p.prefix, 6);
    _iface->writeFrame(_out, 8);
}

void CompanionService::noteSent(uint32_t packetId) {
    for (auto& p : _pending) {
        if (!p.active) { p.packetId = packetId; p.sentMs = millis(); p.active = true; return; }
    }
    // Table full — overwrite the oldest so we never leak slots.
    PendingAck* oldest = &_pending[0];
    for (auto& p : _pending) if (p.sentMs < oldest->sentMs) oldest = &p;
    oldest->packetId = packetId; oldest->sentMs = millis(); oldest->active = true;
}

// MeshManager forwards the DM ACK here -> PUSH_CODE_SEND_CONFIRMED (MyMesh.cpp:412-422).
// Carries the same token returned in RESP_CODE_SENT so the client matches it and
// stops re-sending. trip_time lets the client show round-trip latency.
void CompanionService::onAckConfirmed(uint32_t packetId) {
    for (auto& p : _pending) {
        if (p.active && p.packetId == packetId) {
            p.active = false;
            if (_iface && _iface->isConnected()) {
                uint32_t trip = millis() - p.sentMs;
                _out[0] = PUSH_CODE_SEND_CONFIRMED;
                memcpy(&_out[1], &packetId, 4);
                memcpy(&_out[5], &trip, 4);
                _iface->writeFrame(_out, 9);
            }
            return;
        }
    }
}

void CompanionService::onSendFailed(uint32_t packetId) {
    for (auto& p : _pending) {
        if (p.active && p.packetId == packetId) { p.active = false; return; }
    }
}

// CMD_GET_DEVICE_TIME -> RESP_CODE_CURR_TIME (MyMesh.cpp:1209-1214)
void CompanionService::cmdGetDeviceTime() {
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint32_t now = mesh->getRTCClock()->getCurrentTime();
    _out[0] = RESP_CODE_CURR_TIME;
    memcpy(&_out[1], &now, 4);
    _iface->writeFrame(_out, 5);
}

// CMD_SET_DEVICE_TIME -> OK (only if monotonic-forward). Apps set time on
// connect; this feeds the same RTC clock the mesh stamps messages with. Goes
// through TimeHelper so it counts as a sync -- setting the clock directly left
// the status bar blank, since nowEpoch() reports 0 until a sync has happened.
// Also written to the T-Watch RTC so a watch without a GPS fix keeps the time.
void CompanionService::cmdSetDeviceTime(size_t len) {
    if (len < 5) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint32_t secs; memcpy(&secs, &_cmd[1], 4);
    uint32_t curr = mesh->getRTCClock()->getCurrentTime();
    if (secs >= curr) {
        TimeHelper::instance().syncAndPersist(secs);
        writeOK();
    } else {
        writeErr(ERR_CODE_ILLEGAL_ARG);
    }
}

// CMD_SEND_SELF_ADVERT -> OK / ERR. Optional param byte (_cmd[1]): 1 = flood,
// 0 or absent = zero-hop (local). Mirrors companion_radio MyMesh.cpp:1222. This
// is the one "write" we honour from the client: it only re-broadcasts our own
// advert — the same thing the on-device button and the periodic timer do — and
// changes no stored config/identity, so it stays within the messaging scope.
void CompanionService::cmdSendSelfAdvert(size_t len) {
    bool flood = (len >= 2 && _cmd[1] == 1);
    if (MeshManager::instance().sendAdvertNow(flood)) {
        writeOK();
    } else {
        writeErr(ERR_CODE_BAD_STATE);  // radio not ready / packet pool exhausted
    }
}

// CMD_GET_BATT_AND_STORAGE -> RESP_CODE_BATT_AND_STORAGE (MyMesh.cpp)
void CompanionService::cmdGetBattAndStorage() {
    int i = 0;
    _out[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t mv = Battery::instance().milliVolts();
    uint32_t usedKb = 0, totalKb = 0;
    if (SDCard::instance().isMounted()) {
        totalKb = (uint32_t)(SD.totalBytes() / 1024ULL);
        usedKb  = (uint32_t)(SD.usedBytes()  / 1024ULL);
    }
    memcpy(&_out[i], &mv, 2);      i += 2;
    memcpy(&_out[i], &usedKb, 4);  i += 4;
    memcpy(&_out[i], &totalKb, 4); i += 4;
    _iface->writeFrame(_out, i);
}

// CMD_GET_CONTACTS -> RESP_CODE_CONTACTS_START, then a CONTACT per tick, then
// END_OF_CONTACTS. Optional 4-byte 'since' (lastmod filter) for incremental sync.
void CompanionService::cmdGetContacts(size_t len) {
    if (_contactsIterating) { writeErr(ERR_CODE_BAD_STATE); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }

    // Read the 4-byte 'since' via memcpy — &_cmd[1] is unaligned, and a raw
    // uint32_t* cast there is UB (and inconsistent with the rest of this file).
    uint32_t since = 0;
    if (len >= 5) memcpy(&since, &_cmd[1], 4);
    _contactsSince = since;

    // Reported count excludes transient ADV_TYPE_NONE (anon-req) contacts, which we
    // never expose to the app. _contactCount stays the raw total so the cursor still
    // walks the whole array — pumpContacts skips the anon entries while streaming.
    int total = mesh->getNumContacts();
    uint32_t count = 0;
    for (int i = 0; i < total; i++) {
        ContactInfo* c = mesh->getContactByIdx(i);
        if (c && c->type != ADV_TYPE_NONE) count++;
    }
    _out[0] = RESP_CODE_CONTACTS_START;
    memcpy(&_out[1], &count, 4);
    _iface->writeFrame(_out, 5);

    _contactCount      = total;
    _contactCursor     = 0;
    _mostRecentLastmod = 0;
    _contactsIterating = true;
}

void CompanionService::pumpContacts() {
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { _contactsIterating = false; return; }

    while (_contactCursor < _contactCount) {
        ContactInfo* c = mesh->getContactByIdx(_contactCursor++);
        if (!c) continue;
        if (c->type == ADV_TYPE_NONE) continue;         // skip transient anon-req contacts
        // Incremental sync ('since' > 0) skips unchanged contacts. A full sync
        // ('since' == 0) returns everything — MCLite's config-registered contacts
        // carry lastmod == 0, so a "<= since" test would wrongly drop them all.
        if (_contactsSince != 0 && c->lastmod <= _contactsSince) continue;
        writeContactFrame(RESP_CODE_CONTACT, *c);
        if (c->lastmod > _mostRecentLastmod) _mostRecentLastmod = c->lastmod;
        return;                                         // one frame per tick
    }
    // Done — terminate the stream.
    _out[0] = RESP_CODE_END_OF_CONTACTS;
    memcpy(&_out[1], &_mostRecentLastmod, 4);
    _iface->writeFrame(_out, 5);
    _contactsIterating = false;
}

// CMD_GET_CONTACT_BY_KEY -> RESP_CODE_CONTACT / ERR_NOT_FOUND
void CompanionService::cmdGetContactByKey(size_t len) {
    if (len < 1 + PUB_KEY_SIZE) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    ContactInfo* c = mesh->lookupContactByPubKey(&_cmd[1], PUB_KEY_SIZE);
    if (c) writeContactFrame(RESP_CODE_CONTACT, *c);
    else   writeErr(ERR_CODE_NOT_FOUND);
}

// CMD_GET_CHANNEL -> RESP_CODE_CHANNEL_INFO (MyMesh.cpp:1660-1672)
void CompanionService::cmdGetChannel(size_t len) {
    if (len < 2) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint8_t idx = _cmd[1];
    ChannelDetails ch;
    if (!mesh->getChannel(idx, ch)) { writeErr(ERR_CODE_NOT_FOUND); return; }
    int i = 0;
    _out[i++] = RESP_CODE_CHANNEL_INFO;
    _out[i++] = idx;
    memset(&_out[i], 0, 32);
    strncpy((char*)&_out[i], ch.name, 32); i += 32;
    memcpy(&_out[i], ch.channel.secret, 16); i += 16;   // 128-bit PSK only
    _iface->writeFrame(_out, i);
}

// CMD_SET_CHANNEL -> RESP_CODE_OK/ERR. Add a channel (or remove it via an empty name),
// mirroring the on-device add/remove. Layout: [1]=idx [2..33]=name(32) [34..49]=16-byte
// secret. Gated by permissions.conversation_management. Add/remove only (no edit).
//   ADD applies LIVE with no reboot: MeshCore's addChannel registers a channel at runtime,
//        so the app can use it (and read its real key) immediately and the session stays up.
//   REMOVE must reboot: MeshCore exposes no runtime channel removal (no removeChannel; the
//        channels[] table is private with no reset), so the only way to drop a channel from
//        the live table is to rebuild it from config at boot. The app reconnects afterward.
void CompanionService::cmdSetChannel(size_t len) {
    if (len < 2) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto& mgr = ConfigManager::instance();
    if (!mgr.config().permissions.conversationManagement) { writeErr(ERR_CODE_BAD_STATE); return; }

    uint8_t idx = _cmd[1];

    // Name: up to 32 bytes from [2] (null-padded by the app); String stops at first NUL.
    char nameBuf[33];
    size_t navail = (len > 2) ? (len - 2) : 0;
    if (navail > 32) navail = 32;
    memcpy(nameBuf, &_cmd[2], navail);
    nameBuf[navail] = '\0';
    String name(nameBuf);

    // Empty name -> remove the channel at idx. Unlike add, this reboots to apply: MeshCore
    // has no runtime channel removal, so we drop it from config and rebuild the live table at
    // boot (see the header comment). The app reconnects on its own.
    if (name.length() == 0) {
        if (idx >= mgr.config().channels.size()) { writeErr(ERR_CODE_NOT_FOUND); return; }
        String chName = mgr.config().channels[idx].name;
        if (!mgr.removeChannelAt(idx)) { writeErr(ERR_CODE_BAD_STATE); return; }
        MessageStore::instance().removeConversation(ConvoId{ConvoId::CHANNEL, chName});  // parity w/ on-device
        UIManager::instance().refreshConvoList();   // drop the row now, don't wait for the reboot
        _rebootAtMs = millis() + REBOOT_DELAY_MS;
        writeOK();
        return;
    }

    // Add: need the full frame (idx + 32-byte name + 16-byte secret).
    if (len < 50) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    ChannelConfig cc;
    cc.name = name;
    if (idx == 0 || name.equalsIgnoreCase("Public")) {
        cc.type = "public";   // appendChannel forces the canonical name/PSK/SOS flags
    } else {
        // A zero secret means a name-derived (hashtag) channel: the app sends no key and
        // expects the device to derive it from the name. Store it as a hashtag (empty PSK)
        // so ChannelStore derives SHA256(name)[:16] at boot — otherwise we'd save an all-zero
        // PSK and the channel would have a 00000000... key. A real 16-byte secret -> private.
        bool zeroSecret = true;
        for (int i = 0; i < 16; i++) if (_cmd[34 + i]) { zeroSecret = false; break; }
        if (zeroSecret) {
            cc.type = "hashtag";   // psk stays empty -> derived from the '#'-name at boot
        } else {
            cc.type = "private";   // explicit 16-byte secret -> 32 hex (passes appendChannel's check)
            char psk[33];
            for (int i = 0; i < 16; i++) sprintf(psk + i * 2, "%02x", _cmd[34 + i]);
            psk[32] = '\0';
            cc.psk = psk;
        }
    }
    if (!mgr.appendChannel(cc)) {
        writeErr((int)mgr.config().channels.size() >= defaults::MAX_CHANNELS
                     ? ERR_CODE_TABLE_FULL : ERR_CODE_BAD_STATE);   // BAD_STATE also = dup/edit (out of scope)
        return;
    }

    // Register the new channel LIVE (no reboot): reload ChannelStore so the hashtag PSK is
    // derived / private PSK decoded, then mesh->addChannel registers it at the mesh's next
    // slot. The app's immediate GET_CHANNEL (it builds the confirmation/share QR right after
    // the add) then returns the real key instead of zeros, and the channel is usable straight
    // away — no reboot, session stays connected. Config is persisted, so a later reboot
    // rebuilds the same table.
    //
    // EXCEPTION: if a reboot is already queued (a prior channel REMOVE — which can't drop a
    // channel from the live mesh table, so it rebuilds at boot), the mesh table is stale and
    // about to be rebuilt. GET_CHANNEL reads the mesh by index, so registering live now would
    // land at a slot misaligned with the reindexed config. In that case (or if live
    // registration isn't possible) just persist and let the reboot rebuild the table.
    ChannelStore::instance().loadFromConfig();
    Channel* nc = ChannelStore::instance().findByIndex(mgr.config().channels.back().index);
    auto* mesh = MeshManager::instance().mesh();
    if (_rebootAtMs == 0 && nc && mesh) {
        mesh->addChannel(nc->name.c_str(), nc->pskB64.c_str());
        // Create the on-device conversation + redraw the convo list (mirrors main.cpp's
        // boot-time ensureConversation for channels) so the channel appears live.
        MessageStore::instance().ensureConversation(
            ConvoId{ConvoId::CHANNEL, nc->name}, nc->name, nc->isPrivate(), nc->readOnly);
        UIManager::instance().refreshConvoList();
    } else {
        _rebootAtMs = millis() + REBOOT_DELAY_MS;   // rebuild the table from config at boot
    }

    writeOK();
}

// CMD_ADD_UPDATE_CONTACT -> RESP_CODE_OK/ERR. Add a new contact, or edit an existing one's
// display name. Applied LIVE (no reboot): MeshCore add/removeContact are runtime ops and
// GET_CONTACTS renders the configured alias, so the app's list updates immediately and the
// companion session stays connected. Only the name is persisted on edit — MCLite's per-contact
// permission flags are device-owner policy (not in the app's contact-settings model) and the
// app's flags byte is app-local. Frame: [1..32]=pubkey [33]=type [34]=flags [35]=out_path_len
// [36..99]=out_path(64) [100..131]=adv_name(32) [132..135]=last_advert [136..143]=lat/lon(opt).
void CompanionService::cmdAddUpdateContact(size_t len) {
    if (len < 136) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }   // through last_advert; lat/lon optional
    auto& mgr = ConfigManager::instance();
    if (!mgr.config().permissions.conversationManagement) { writeErr(ERR_CODE_BAD_STATE); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }

    const uint8_t* pubKey = &_cmd[1];
    char nameBuf[33];
    memcpy(nameBuf, &_cmd[100], 32);
    nameBuf[32] = '\0';
    String advName(nameBuf);

    int idx = mgr.findContactIndexByKey(pubKey);
    if (idx >= 0) {
        // EDIT: persist the (possibly) new display name; skip a no-op change to avoid churn.
        // The alias the app shows came from our own GET_CONTACTS, so this is the user's intent,
        // not a raw advert-name overwrite (the alias-display fix stays intact).
        if (advName.length() && advName != mgr.config().contacts[idx].alias) {
            if (!mgr.updateContactAlias((size_t)idx, advName)) { writeErr(ERR_CODE_FILE_IO_ERROR); return; }
            ContactStore::instance().loadFromConfig();   // GET_CONTACTS picks up the new alias
            // Rename the on-device conversation too (ensureConversation returns the existing
            // one; overwrite its label) and redraw the convo list if it's showing.
            MessageStore::instance().ensureConversation(
                ConvoId{ConvoId::DM, pubKeyToShortId(pubKey)}, advName, false).displayName = advName;
            UIManager::instance().refreshConvoList();
        }
        writeOK();
        return;
    }

    // ADD: persist a discovered-style contact with conservative (all-false) permissions, then
    // register it live so GET_CONTACTS (which iterates mesh contacts[]) shows it without a reboot.
    ContactConfig cc;
    cc.alias = advName.length() ? advName : String("Contact");
    char hex[2 * PUB_KEY_SIZE + 1];
    for (int b = 0; b < PUB_KEY_SIZE; b++) sprintf(hex + 2 * b, "%02x", pubKey[b]);
    cc.publicKey = hex;
    cc.allowTelemetry = cc.allowLocation = cc.allowEnvironment = false;
    cc.alwaysSound = cc.allowSos = cc.sendSos = false;
    cc.fromDiscovery = true;
    if (!mgr.appendDiscoveredContact(cc)) {
        writeErr((int)mgr.config().contacts.size() >= defaults::MAX_CHAT_CONTACTS
                     ? ERR_CODE_TABLE_FULL : ERR_CODE_BAD_STATE);
        return;
    }

    ContactInfo ci;
    memset(&ci, 0, sizeof(ci));
    memcpy(ci.id.pub_key, pubKey, PUB_KEY_SIZE);
    strncpy(ci.name, cc.alias.c_str(), sizeof(ci.name) - 1);
    ci.type         = _cmd[33];
    ci.flags        = _cmd[34];
    ci.out_path_len = _cmd[35];                       // 0xFF = OUT_PATH_UNKNOWN
    memcpy(ci.out_path, &_cmd[36], MAX_PATH_SIZE);
    memcpy(&ci.last_advert_timestamp, &_cmd[132], 4);
    if (len >= 144) { memcpy(&ci.gps_lat, &_cmd[136], 4); memcpy(&ci.gps_lon, &_cmd[140], 4); }
    mesh->addContact(ci);
    ContactStore::instance().loadFromConfig();
    // Create the on-device conversation (the convo list renders conversations, built at boot
    // for every contact) and redraw it if showing — so the new contact appears live, not just
    // after a reboot. Mirrors main.cpp's boot-time ensureConversation for contacts.
    MessageStore::instance().ensureConversation(
        ConvoId{ConvoId::DM, pubKeyToShortId(pubKey)}, cc.alias, false);
    UIManager::instance().refreshConvoList();
    writeOK();
}

// CMD_REMOVE_CONTACT -> RESP_CODE_OK/ERR. Drop a contact, its chat history, and any held
// advert. Reboots to apply (the app reconnects), mirroring the on-device SettingsScreen remove
// and keeping the model uniform across contacts and channels: adding/editing is live, removing
// reboots. (MeshCore does expose a live removeContact, but we reboot for one predictable rule
// and a clean store/UI rebuild — channel removal has no live path, so this matches it.)
// [1..32]=pubkey.
void CompanionService::cmdRemoveContact(size_t len) {
    if (len < 33) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto& mgr = ConfigManager::instance();
    if (!mgr.config().permissions.conversationManagement) { writeErr(ERR_CODE_BAD_STATE); return; }

    const uint8_t* pubKey = &_cmd[1];
    int idx = mgr.findContactIndexByKey(pubKey);
    if (idx < 0) { writeErr(ERR_CODE_NOT_FOUND); return; }

    String shortId = pubKeyToShortId(pubKey);
    if (!mgr.removeContactAt((size_t)idx)) { writeErr(ERR_CODE_FILE_IO_ERROR); return; }
    MessageStore::instance().removeConversation(ConvoId{ConvoId::DM, shortId});
    MeshManager::instance().deleteAdvertBlob(pubKey);
    UIManager::instance().refreshConvoList();   // drop the row now, don't wait for the reboot
    _rebootAtMs = millis() + REBOOT_DELAY_MS;
    writeOK();
}

// CMD_SHARE_CONTACT -> RESP_CODE_OK/ERR. Re-broadcast a known contact's advert at zero hop
// (same as the on-device Share button). [1..32]=32-byte contact pubkey. Pure action, no
// config change; gated by the same messaging.share_contact flag.
void CompanionService::cmdShareContact(size_t len) {
    if (len < 33) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!ConfigManager::instance().config().messaging.shareContact) { writeErr(ERR_CODE_BAD_STATE); return; }
    const uint8_t* pubKey = &_cmd[1];
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh || !mesh->lookupContactByPubKey(pubKey, PUB_KEY_SIZE)) { writeErr(ERR_CODE_NOT_FOUND); return; }
    if (MeshManager::instance().shareContact(pubKey)) writeOK();
    else writeErr(ERR_CODE_BAD_STATE);   // no re-broadcastable advert held for this contact
}

// CMD_REBOOT -> (no response, per protocol). Reboot the device: a power-cycle, no stored-state
// change, so it's allowed ungated. Reuses the deferred-reboot path (loop() performs the restart).
// Requires the "reboot" magic word ([1..6]) so a stray byte can't restart the device (mirrors the
// reference companion_radio guard).
void CompanionService::cmdReboot(size_t len) {
    if (len < 7 || memcmp(&_cmd[1], "reboot", 6) != 0) return;
    _rebootAtMs = millis() + REBOOT_DELAY_MS;
}

// ── Device-settings writes (gate: permissions.settings == "full") ──────────────
// The same gate the on-device Admin uses for non-basic controls. Current values are
// reported back to the app via RESP_CODE_SELF_INFO (cmdAppStart), so there are no GET
// counterparts here. Name applies live (read fresh per advert); radio/TX/path-hash/PIN
// persist to config and reboot to re-apply at boot (initRadio / ensureBlePin).

bool CompanionService::settingsAllowed() const {
    return ConfigManager::instance().config().permissions.settings == "full";
}

// CMD_SET_ADVERT_NAME -> OK/ERR. [1..]=name (truncated to 20, MCLite's deviceName cap).
// Live: the advert/group sends read cfg.deviceName fresh, so the next advert uses it.
void CompanionService::cmdSetAdvertName(size_t len) {
    if (len < 2) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    size_t nlen = len - 1;
    if (nlen > 20) nlen = 20;                          // matches the config-load cap
    char nameBuf[21];
    memcpy(nameBuf, &_cmd[1], nlen);
    nameBuf[nlen] = '\0';
    auto& cfg = ConfigManager::instance().config();
    String prev = cfg.deviceName;
    cfg.deviceName = String(nameBuf);
    if (!ConfigManager::instance().save()) { cfg.deviceName = prev; writeErr(ERR_CODE_FILE_IO_ERROR); return; }
    writeOK();
}

// CMD_SET_RADIO_PARAMS -> OK/ERR. [1..4]=freq kHz u32, [5..8]=bw Hz u32, [9]=sf, [10]=cr,
// optional [11]=repeat. Reboots to apply (radio re-inits from config at boot).
void CompanionService::cmdSetRadioParams(size_t len) {
    if (len < 11) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint32_t freq, bw;
    memcpy(&freq, &_cmd[1], 4);
    memcpy(&bw,   &_cmd[5], 4);
    uint8_t sf = _cmd[9], cr = _cmd[10];
    uint8_t repeat = (len > 11) ? _cmd[11] : 0;
    if (repeat) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }   // MCLite isn't a repeater
    if (!(freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 &&
          cr >= 5 && cr <= 8 && bw >= 7000 && bw <= 500000)) {
        writeErr(ERR_CODE_ILLEGAL_ARG); return;
    }
    auto& r = ConfigManager::instance().config().radio;
    RadioConfig prev = r;
    r.frequency       = (float)freq / 1000.0f;        // kHz -> MHz
    r.bandwidth       = (float)bw / 1000.0f;          // Hz  -> kHz
    r.spreadingFactor = sf;
    r.codingRate      = cr;
    if (!ConfigManager::instance().save()) { r = prev; writeErr(ERR_CODE_FILE_IO_ERROR); return; }
    _rebootAtMs = millis() + REBOOT_DELAY_MS;
    writeOK();
}

// CMD_SET_RADIO_TX_POWER -> OK/ERR. [1]=int8 dBm. Reboots to apply.
void CompanionService::cmdSetTxPower(size_t len) {
    if (len < 2) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    int8_t power = (int8_t)_cmd[1];
    if (power < -9 || power > LORA_TX_POWER) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto& r = ConfigManager::instance().config().radio;
    int8_t prev = r.txPower;
    r.txPower = power;
    if (!ConfigManager::instance().save()) { r.txPower = prev; writeErr(ERR_CODE_FILE_IO_ERROR); return; }
    _rebootAtMs = millis() + REBOOT_DELAY_MS;
    writeOK();
}

// CMD_SET_DEVICE_PIN -> OK/ERR. [1..4]=u32 PIN, 0 or 6-digit (0 = regenerate next boot).
// Reboots so BLE re-inits with the new PIN (ensureBlePin).
void CompanionService::cmdSetDevicePin(size_t len) {
    if (len < 5) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint32_t pin;
    memcpy(&pin, &_cmd[1], 4);
    if (!(pin == 0 || (pin >= 100000 && pin <= 999999))) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto& ble = ConfigManager::instance().config().ble;
    uint32_t prev = ble.pin;
    ble.pin = pin;
    if (!ConfigManager::instance().save()) { ble.pin = prev; writeErr(ERR_CODE_FILE_IO_ERROR); return; }
    _rebootAtMs = millis() + REBOOT_DELAY_MS;
    writeOK();
}

// CMD_SET_PATH_HASH_MODE -> OK/ERR. [1]=0 (reserved), [2]=mode (0/1/2 -> 1/2/3 bytes/hop).
// Reboots to apply. Lets app users fix the large-mesh case (#36) without editing config.json.
void CompanionService::cmdSetPathHashMode(size_t len) {
    if (len < 3 || _cmd[1] != 0) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint8_t mode = _cmd[2];
    if (mode > 2) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto& r = ConfigManager::instance().config().radio;
    uint8_t prev = r.pathHashMode;
    r.pathHashMode = mode;
    if (!ConfigManager::instance().save()) { r.pathHashMode = prev; writeErr(ERR_CODE_FILE_IO_ERROR); return; }
    _rebootAtMs = millis() + REBOOT_DELAY_MS;
    writeOK();
}

// ── Region / flood-scope (54 session, 63 persistent, 64 read) ──────────────────
// MCLite models a region as a string (cfg.radio.scope, e.g. "#region" / "*") and derives the
// transport key SHA256("#name")[:16] (== MeshCore getAutoKeyFor for public hashtag regions).

// CMD_SET_DEFAULT_FLOOD_SCOPE -> OK/ERR. Persistent. [1..31]=name, [32..47]=16-byte key;
// len==1 (no name/key) clears to none ("*"). For a public '#'-region the app's key equals the
// name's derived key -> store the name; a custom/private key can't be represented -> BAD_STATE.
void CompanionService::cmdSetDefaultFloodScope(size_t len) {
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    auto& cfg = ConfigManager::instance().config();
    String prev = cfg.radio.scope;
    String scope;
    if (len < 1 + 31 + 16) {
        scope = "*";                                   // clear to none
    } else {
        char nameBuf[32];
        memcpy(nameBuf, &_cmd[1], 31);
        nameBuf[31] = '\0';
        String name(nameBuf);
        if (name.length() == 0) {
            scope = "*";
        } else {
            uint8_t derived[16];
            MCLiteMesh::deriveScopeKey(name, derived);
            if (memcmp(derived, &_cmd[32], 16) != 0) {  // custom/private key — not representable
                writeErr(ERR_CODE_BAD_STATE);
                return;
            }
            scope = name;                              // store verbatim (WYSIWYG, matches on-device)
        }
    }
    sanitizeScope(scope);   // a scope is one region name — drop anything past a space (#36)
    if (cfg.radio.scope != scope) {
        cfg.radio.scope = scope;
        if (!ConfigManager::instance().save()) { cfg.radio.scope = prev; writeErr(ERR_CODE_FILE_IO_ERROR); return; }
        _rebootAtMs = millis() + REBOOT_DELAY_MS;      // _globalScope re-derives at boot
    }
    writeOK();
}

// CMD_GET_DEFAULT_FLOOD_SCOPE -> RESP_CODE_DEFAULT_FLOOD_SCOPE. Read (no gate). "*" => null (1 byte).
void CompanionService::cmdGetDefaultFloodScope() {
    const String& scope = ConfigManager::instance().config().radio.scope;
    _out[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
    if (scope == "*" || scope.length() == 0) { _iface->writeFrame(_out, 1); return; }
    memset(&_out[1], 0, 31);
    strncpy((char*)&_out[1], scope.c_str(), 31);
    MCLiteMesh::deriveScopeKey(scope, &_out[1 + 31]);
    _iface->writeFrame(_out, 1 + 31 + 16);
}

// CMD_SET_FLOOD_SCOPE_KEY -> OK/ERR. Session-only live override (raw key, no persist). Reverts
// to the config-derived scope on reboot.
//   [1]=0: [2..17]=key (absent = null/un-scoped)        — set the live global scope key
//   [1]=1: explicit un-scoped (ver 12+) — force a null global scope; no key bytes needed
// A null global scope makes inheriting floods (channels/DMs with no per-channel scope) go out
// with no transport code, matching MeshCore 1.16's send_unscoped behavior.
void CompanionService::cmdSetFloodScopeKey(size_t len) {
    if (len < 2 || _cmd[1] > 1) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!settingsAllowed()) { writeErr(ERR_CODE_BAD_STATE); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint8_t key[16];
    if (_cmd[1] == 1)            memset(key, 0, 16);    // ver-12: explicit un-scoped (null scope)
    else if (len >= 2 + 16)      memcpy(key, &_cmd[2], 16);
    else                         memset(key, 0, 16);    // null = no scope
    mesh->setGlobalScope(key);
    writeOK();
}

// CMD_RESET_PATH -> OK/NOT_FOUND. Reset a contact's learned path so the next send floods to
// rediscover the route. Runtime action (out_path isn't in MCLite config). [1..32]=pubkey.
void CompanionService::cmdResetPath(size_t len) {
    if (len < 1 + PUB_KEY_SIZE) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    ContactInfo* ci = mesh->lookupContactByPubKey(&_cmd[1], PUB_KEY_SIZE);
    if (!ci) { writeErr(ERR_CODE_NOT_FOUND); return; }
    mesh->resetPathTo(*ci);
    writeOK();
}

// CMD_EXPORT_CONTACT -> RESP_CODE_EXPORT_CONTACT (advert blob). No pubkey = export self;
// otherwise export a known contact's held advert (the same blob SHARE_CONTACT re-broadcasts).
void CompanionService::cmdExportContact(size_t len) {
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    uint8_t blob[MAX_TRANS_UNIT];
    int blen;
    if (len < 1 + PUB_KEY_SIZE) {
        blen = mesh->exportSelf(ConfigManager::instance().config().deviceName.c_str(), blob);
    } else {
        blen = mesh->exportContactBlob(&_cmd[1], blob);   // held advert, 0 if none
    }
    // Bound by the protocol max (172), not sizeof(_out) (173): writeFrame() silently drops any
    // frame > MAX_FRAME_SIZE, so a 172-byte blob (173-byte frame) would vanish with no error.
    if (blen <= 0 || blen + 1 > MAX_FRAME_SIZE) { writeErr(ERR_CODE_NOT_FOUND); return; }
    _out[0] = RESP_CODE_EXPORT_CONTACT;
    memcpy(&_out[1], blob, blen);
    _iface->writeFrame(_out, blen + 1);
}

// CMD_IMPORT_CONTACT -> OK/ERR. Inject an advert blob; it loops back through the advert RX path
// and lands in Heard Adverts (MCLite curates contacts, never auto-adds) for the user to Save.
// Gated by conversation_management. [1..]=advert blob.
void CompanionService::cmdImportContact(size_t len) {
    // Require a minimally well-formed advert (pubkey + max path), mirroring the reference
    // (len > 2+32+64). importContact -> Packet::readFrom reads the header/path bytes before its
    // own length check, so a short frame would over-read stale bytes from the previous command.
    if (len <= 2 + PUB_KEY_SIZE + 64) { writeErr(ERR_CODE_ILLEGAL_ARG); return; }
    if (!ConfigManager::instance().config().permissions.conversationManagement) { writeErr(ERR_CODE_BAD_STATE); return; }
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) { writeErr(ERR_CODE_BAD_STATE); return; }
    if (mesh->importContact(&_cmd[1], (uint8_t)(len - 1))) writeOK();
    else writeErr(ERR_CODE_ILLEGAL_ARG);
}

// CMD_GET_AUTOADD_CONFIG -> RESP_CODE_AUTOADD_CONFIG. MCLite never auto-adds heard nodes
// (manual_add_contacts=1 / shouldAutoAddContactType=false), so always report off (config=0,
// max_hops=0) — lets the app render "Auto Add Contacts" correctly instead of erroring.
void CompanionService::cmdGetAutoaddConfig() {
    _out[0] = RESP_CODE_AUTOADD_CONFIG;
    _out[1] = 0;   // auto-add disabled
    _out[2] = 0;   // max hops
    _iface->writeFrame(_out, 3);
}

// CMD_SYNC_NEXT_MESSAGE -> next queued message, or NO_MORE_MESSAGES.
// Lossless: the message is only removed from the queue once the transport confirms it
// accepted the frame. The WiFi transport silently drops frames when its 4-slot
// send_queue is full (and reports isWriteBusy()==false), so writing-then-popping
// unconditionally lost messages on connect. If the frame can't go out now, we keep it
// queued and re-drive from loop() once the send_queue drains — the app's single SYNC
// request is still answered, without needing it to re-ask.
void CompanionService::cmdSyncNextMessage() {
    if (!trySendSyncResponse()) _syncResponsePending = true;
}

bool CompanionService::trySendSyncResponse() {
    if (!clientConnected()) return true;            // client gone — nothing owed
    if (_iface->isWriteBusy()) return false;        // backpressure (paces BLE; no-op on WiFi)

    // 1. Replay stored history via the cursor (uncapped, built on demand). Advance the
    //    cursor PAST the message only once the transport confirms it accepted the frame.
    if (_bfActive) {
        int n = buildNextBackfillFrame();           // builds into _out at the cursor
        if (n > 0) {
            if (_iface->writeFrame(_out, (size_t)n) != (size_t)n) return false;  // not sent — keep cursor
            _bfMsgIdx++;                            // move past it only on confirmed accept
            return true;
        }
        _bfActive = false;                          // history exhausted → fall through to live
    }

    // 2. Live messages that arrived while connected (FIFO). Pop only on confirmed accept.
    if (_offlineLen > 0) {
        const OfflineMsg& m = _offline[0];
        if (_iface->writeFrame(m.buf, m.len) != m.len) return false;
        _offlineLen--;
        for (int i = 0; i < _offlineLen; i++) _offline[i] = _offline[i + 1];
        return true;
    }

    // 3. Nothing left.
    uint8_t f = RESP_CODE_NO_MORE_MESSAGES;
    return _iface->writeFrame(&f, 1) == 1;
}

// Position the backfill cursor at the next RECEIVED message and build its sync frame
// into _out. Returns the frame length, or 0 when history is exhausted. Walks MessageStore
// in stable insertion order; whole conversations whose contact/channel can't be resolved
// are skipped.
//
// RECEIVED-ONLY by design. The MeshCore companion protocol has no outgoing-message frame:
// the sync stream is only CONTACT/CHANNEL_MSG_RECV (verified against the 1.16 reference,
// where the offline queue is fed exclusively by receive callbacks). So messages composed
// ON THE DEVICE (fromSelf) are NOT replayed — delivering them would force them onto the
// incoming side (wrong, especially for DMs). On-device sending still works and goes out
// over the mesh; it just won't appear in the companion app. See README + known-issues.
// (App-composed sends aren't in MessageStore anyway — cmdSendTxtMsg doesn't addMessage.)
int CompanionService::buildNextBackfillFrame() {
    auto& store = MessageStore::instance();
    auto& cs = ContactStore::instance();

    while (_bfConvoIdx < (int)store.conversationCount()) {
        Conversation* convo = store.conversationByIndex((size_t)_bfConvoIdx);
        if (convo && _bfMsgIdx < (int)convo->messages.size()) {
            const Message& m = convo->messages[_bfMsgIdx];
            if (m.fromSelf) { _bfMsgIdx++; continue; }   // device-composed: not syncable (see above)

            if (convo->convoId.type == ConvoId::DM) {
                Contact* sc = nullptr;
                for (size_t k = 0; k < cs.count(); k++) {
                    Contact* c = cs.findByIndex(k);
                    if (c && c->shortId() == convo->convoId.id) { sc = c; break; }
                }
                if (sc)
                    return buildContactRecvFrame(sc->publicKey, m.timestamp, m.text.c_str());
                // unresolved contact → skip the whole conversation (fall through)
            } else if (convo->convoId.type == ConvoId::CHANNEL) {
                int meshIdx = channelIdxByName(convo->convoId.id);
                if (meshIdx >= 0) {
                    String wire = m.senderName.length() ? (m.senderName + ": " + m.text) : m.text;
                    return buildChannelRecvFrame((uint8_t)meshIdx, m.timestamp, wire.c_str());
                }
                // unresolved channel → skip the whole conversation (fall through)
            }
            // ROOM conversations are not exposed over the companion link.
        }
        _bfConvoIdx++;        // conversation exhausted or unresolvable — next one
        _bfMsgIdx = 0;
    }
    return 0;                 // exhausted
}

void CompanionService::enqueueOffline(const uint8_t* frame, int len) {
    if (len <= 0 || len > MAX_FRAME_SIZE) return;
    if (_offlineLen >= OFFLINE_QUEUE_SIZE) {
        // Drop the oldest to make room (bounded, never blocks).
        for (int i = 0; i < OFFLINE_QUEUE_SIZE - 1; i++) _offline[i] = _offline[i + 1];
        _offlineLen = OFFLINE_QUEUE_SIZE - 1;
        LOGLN("[Companion] offline queue full — dropped oldest");
    }
    _offline[_offlineLen].len = (uint8_t)len;
    memcpy(_offline[_offlineLen].buf, frame, len);
    _offlineLen++;
}

void CompanionService::tickleMsgWaiting() {
    if (_iface && _iface->isConnected()) {
        uint8_t f = PUSH_CODE_MSG_WAITING;
        _iface->writeFrame(&f, 1);
    }
}

// Build RESP_CODE_CONTACT_MSG_RECV[_V3] into _out (MyMesh::queueMessage).
int CompanionService::buildContactRecvFrame(const uint8_t* senderPubKey, uint32_t timestamp,
                                            const char* text) {
    int i = 0;
    if (_appVer >= 3) {
        _out[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
        _out[i++] = 0;   // SNR*4 (not available here)
        _out[i++] = 0;   // reserved
        _out[i++] = 0;   // reserved
    } else {
        _out[i++] = RESP_CODE_CONTACT_MSG_RECV;
    }
    memcpy(&_out[i], senderPubKey, 6); i += 6;   // 6-byte prefix
    _out[i++] = 0xFF;                             // path_len unknown
    _out[i++] = COMPANION_TXT_TYPE_PLAIN;
    memcpy(&_out[i], &timestamp, 4); i += 4;
    int tlen = strlen(text);
    if (i + tlen > MAX_FRAME_SIZE) tlen = MAX_FRAME_SIZE - i;
    memcpy(&_out[i], text, tlen); i += tlen;
    return i;
}

// Build RESP_CODE_CHANNEL_MSG_RECV[_V3] into _out (MyMesh::onChannelMessageRecv).
int CompanionService::buildChannelRecvFrame(uint8_t meshChannelIdx, uint32_t timestamp,
                                            const char* text) {
    int i = 0;
    if (_appVer >= 3) {
        _out[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
        _out[i++] = 0;   // SNR*4
        _out[i++] = 0;   // reserved
        _out[i++] = 0;   // reserved
    } else {
        _out[i++] = RESP_CODE_CHANNEL_MSG_RECV;
    }
    _out[i++] = meshChannelIdx;
    _out[i++] = 0xFF;                             // path_len unknown
    _out[i++] = COMPANION_TXT_TYPE_PLAIN;
    memcpy(&_out[i], &timestamp, 4); i += 4;
    int tlen = strlen(text);
    if (i + tlen > MAX_FRAME_SIZE) tlen = MAX_FRAME_SIZE - i;
    memcpy(&_out[i], text, tlen); i += tlen;
    return i;
}

// Received DM (live tee) -> queue + tickle. meshChannelIdx matches GET_CHANNEL.
void CompanionService::onContactMessage(const uint8_t* senderPubKey, uint32_t timestamp,
                                        const char* text) {
    if (!clientConnected()) return;   // live-forward only
    enqueueOffline(_out, buildContactRecvFrame(senderPubKey, timestamp, text));
    tickleMsgWaiting();
}

void CompanionService::onChannelMessage(uint8_t meshChannelIdx, uint32_t timestamp,
                                        const char* text) {
    if (!clientConnected()) return;
    enqueueOffline(_out, buildChannelRecvFrame(meshChannelIdx, timestamp, text));
    tickleMsgWaiting();
}

// Arm the cursor-driven history replay for a freshly-connected client. The actual
// messages are streamed on demand by trySendSyncResponse()/buildNextBackfillFrame() as
// the app pulls them via CMD_SYNC_NEXT_MESSAGE — no fixed-size staging, so every stored
// inbound message is delivered (the old pre-stage capped at 24 and dropped the newest).
// Live messages arriving while connected still go through the _offline queue, drained
// after history. The client dedups by (sender, timestamp).
void CompanionService::backfillHistory() {
    if (!clientConnected()) return;
    _bfConvoIdx = 0;
    _bfMsgIdx   = 0;
    _bfActive   = true;
    _offlineLen = 0;     // start the live queue clean for this session
    tickleMsgWaiting();  // nudge the app to begin syncing
}

// Map a channel name to its mesh channel index (as used by GET_CHANNEL).
int CompanionService::channelIdxByName(const String& name) {
    auto* mesh = MeshManager::instance().mesh();
    if (!mesh) return -1;
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        if (mesh->getChannel(i, ch) && name == ch.name) return i;
    }
    return -1;
}

// Shared CONTACT frame builder (MyMesh::writeContactRespFrame).
void CompanionService::writeContactFrame(uint8_t code, const ContactInfo& c) {
    int i = 0;
    _out[i++] = code;
    memcpy(&_out[i], c.id.pub_key, PUB_KEY_SIZE); i += PUB_KEY_SIZE;
    _out[i++] = c.type;
    _out[i++] = c.flags;
    _out[i++] = c.out_path_len;
    memcpy(&_out[i], c.out_path, MAX_PATH_SIZE); i += MAX_PATH_SIZE;

    // Prefer the locally-configured display name over MeshCore's
    // advert-overwritten c.name (BaseChatMesh::onAdvertRecv rewrites it on every
    // advert). Chat contacts come from ContactStore (alias); room servers from
    // config.roomServers (matched by hex pubkey). The companion protocol is
    // read-only and keys on pub_key, so this is a safe cosmetic override.
    const char* disp = c.name;
    if (Contact* sc = ContactStore::instance().findByPublicKey(c.id.pub_key)) {
        if (sc->name.length()) disp = sc->name.c_str();
    } else {
        char hex[2 * PUB_KEY_SIZE + 1];
        for (int b = 0; b < PUB_KEY_SIZE; b++) snprintf(hex + 2 * b, 3, "%02x", c.id.pub_key[b]);
        const auto& rooms = ConfigManager::instance().config().roomServers;
        for (const auto& rs : rooms) {
            if (rs.publicKey.length() == 64 && rs.name.length() &&
                rs.publicKey.equalsIgnoreCase(hex)) { disp = rs.name.c_str(); break; }
        }
    }

    memset(&_out[i], 0, 32);
    strncpy((char*)&_out[i], disp, 32); i += 32;
    memcpy(&_out[i], &c.last_advert_timestamp, 4); i += 4;
    memcpy(&_out[i], &c.gps_lat, 4); i += 4;
    memcpy(&_out[i], &c.gps_lon, 4); i += 4;
    memcpy(&_out[i], &c.lastmod, 4); i += 4;
    _iface->writeFrame(_out, i);
}

void CompanionService::writeOK() {
    _out[0] = RESP_CODE_OK;
    _iface->writeFrame(_out, 1);
}

void CompanionService::writeErr(uint8_t code) {
    _out[0] = RESP_CODE_ERR;
    _out[1] = code;
    _iface->writeFrame(_out, 2);
}

}  // namespace mclite
