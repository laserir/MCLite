#include "MessageStore.h"
#include "SDCard.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include <ArduinoJson.h>
#include <algorithm>

namespace mclite {

MessageStore& MessageStore::instance() {
    static MessageStore inst;
    return inst;
}

String MessageStore::historyPath(const ConvoId& id) const {
    String path = defaults::HISTORY_DIR;
    path += "/";
    if (id.type == ConvoId::ROOM) path += "room_";
    path += id.id;
    path += ".json";
    return path;
}

Conversation& MessageStore::getOrCreate(const ConvoId& id, const String& displayName,
                                         bool isPrivate, bool readOnly) {
    for (auto& c : _convos) {
        if (c.convoId == id) return c;
    }
    Conversation c;
    c.convoId = id;
    c.displayName = displayName;
    c.isPrivate = isPrivate;
    c.readOnly = readOnly;
    // MAX_CONVERSATIONS == MAX_CONTACTS(40) + MAX_GROUP_CHANNELS(16); 40 covers
    // up to 32 chat contacts + 8 rooms. Cap can only be reached if those build
    // flags are increased without updating MAX_CONVERSATIONS. Guard kept as
    // defensive fallback — returns last conversation which would corrupt it;
    // acceptable since this path is unreachable under current config limits.
    if (_convos.size() >= MAX_CONVERSATIONS) {
        Serial.println("[MessageStore] ERROR: max conversations reached, reusing last");
        return _convos.back();
    }
    _convos.push_back(c);
    return _convos.back();
}

void MessageStore::loadHistory(const ConvoId& id) {
    auto& sd = SDCard::instance();
    String path = historyPath(id);

    if (!sd.fileExists(path.c_str())) return;

    String json = sd.readFile(path.c_str());
    if (json.isEmpty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        Serial.printf("[MessageStore] Failed to parse history: %s\n", path.c_str());
        return;
    }

    // Find the conversation (must already exist)
    Conversation* convo = getConversation(id);
    if (!convo) return;

    convo->messages.clear();

    // Support both formats: wrapped {"lastActivity":N,"messages":[...]} and legacy bare array
    JsonArray arr;
    if (doc.is<JsonObject>()) {
        uint32_t saved = doc["lastActivity"] | (uint32_t)0;
        convo->lastActivity = saved;
        // Track highest loaded value so new messages always sort above
        if (saved > _activityCounter) _activityCounter = saved;
        convo->syncSince = doc["syncSince"] | (uint32_t)0;
        arr = doc["messages"].as<JsonArray>();
    } else {
        arr = doc.as<JsonArray>();
    }

    for (JsonObject obj : arr) {
        Message msg;
        const char* from = obj["from"] | "them";
        const String& myKey = ConfigManager::instance().config().publicKey;
        msg.fromSelf  = (strcmp(from, "self") == 0 || myKey == from);
        msg.text      = obj["text"] | "";
        msg.timestamp = obj["time"] | 0;
        msg.senderName = obj["sender"] | "";
        const char* status = obj["status"] | "sent";
        if (strcmp(status, "delivered") == 0)    msg.status = MessageStatus::DELIVERED;
        else if (strcmp(status, "failed") == 0)  msg.status = MessageStatus::FAILED;
        else if (strcmp(status, "sending") == 0) msg.status = MessageStatus::FAILED;  // Can't ACK after reboot
        else                                     msg.status = MessageStatus::SENT;

        convo->messages.push_back(msg);
    }
}

void MessageStore::saveHistory(const ConvoId& id) {
    const auto& cfg = ConfigManager::instance().config();
    if (!cfg.messaging.saveHistory) return;

    Conversation* convo = getConversation(id);
    if (!convo) return;

    auto& sd = SDCard::instance();
    sd.mkdir(defaults::HISTORY_DIR);

    JsonDocument doc;
    doc["lastActivity"] = convo->lastActivity;
    if (convo->syncSince != 0) {
        doc["syncSince"] = convo->syncSince;
    }
    JsonArray arr = doc["messages"].to<JsonArray>();

    for (const auto& msg : convo->messages) {
        JsonObject obj = arr.add<JsonObject>();
        obj["from"]   = msg.fromSelf ? cfg.publicKey.c_str() : "them";
        obj["text"]   = msg.text;
        obj["time"]   = msg.timestamp;
        const char* statusStr = "sent";
        if (msg.status == MessageStatus::DELIVERED)    statusStr = "delivered";
        else if (msg.status == MessageStatus::FAILED)  statusStr = "failed";
        else if (msg.status == MessageStatus::SENDING) statusStr = "sending";
        obj["status"] = statusStr;
        if (msg.senderName.length() > 0) {
            obj["sender"] = msg.senderName;
        }
    }

    String json;
    serializeJson(doc, json);
    String path = historyPath(id);
    bool ok = sd.writeFile(path.c_str(), json);
    Serial.printf("[History] Save %s: %u msgs, %u bytes, %s\n",
                  path.c_str(), (unsigned)convo->messages.size(), (unsigned)json.length(),
                  ok ? "OK" : "FAILED");
}

Conversation& MessageStore::ensureConversation(const ConvoId& id, const String& displayName,
                                                bool isPrivate, bool readOnly) {
    return getOrCreate(id, displayName, isPrivate, readOnly);
}

Conversation& MessageStore::addMessage(const ConvoId& id, const String& displayName,
                                        bool isPrivate, const Message& msg, bool readOnly) {
    Conversation& convo = getOrCreate(id, displayName, isPrivate, readOnly);
    convo.messages.push_back(msg);
    convo.lastActivity = ++_activityCounter;  // Monotonic: always above loaded values
    if (!msg.fromSelf) {
        convo.hasUnread = true;
    }
    pruneIfNeeded(convo);
    saveHistory(id);
    return convo;
}

void MessageStore::updateStatus(uint32_t packetId, MessageStatus status) {
    if (packetId == 0) return;
    for (auto& convo : _convos) {
        for (auto& msg : convo.messages) {
            if (msg.packetId == packetId && msg.fromSelf) {
                msg.status = status;
                saveHistory(convo.convoId);
                return;
            }
        }
    }
}

Conversation* MessageStore::getConversation(const ConvoId& id) {
    for (auto& c : _convos) {
        if (c.convoId == id) return &c;
    }
    return nullptr;
}

std::vector<Conversation*> MessageStore::getConversationsSorted() {
    std::vector<Conversation*> result;
    for (auto& c : _convos) {
        result.push_back(&c);
    }
    std::sort(result.begin(), result.end(), [](const Conversation* a, const Conversation* b) {
        if (a->lastActivity != b->lastActivity)
            return a->lastActivity > b->lastActivity;
        // Tie-break: last message timestamp preserves ordering from previous session
        uint32_t tsA = a->lastMessage() ? a->lastMessage()->timestamp : 0;
        uint32_t tsB = b->lastMessage() ? b->lastMessage()->timestamp : 0;
        return tsA > tsB;
    });
    return result;
}

void MessageStore::markRead(const ConvoId& id) {
    Conversation* c = getConversation(id);
    if (c) c->hasUnread = false;
}

void MessageStore::updateRoomSyncSince(const ConvoId& id, uint32_t timestamp) {
    Conversation* c = getConversation(id);
    if (!c) return;
    if (timestamp <= c->syncSince) return;  // never go backwards
    c->syncSince = timestamp;
    saveHistory(id);
}

void MessageStore::pruneIfNeeded(Conversation& convo) {
    uint16_t maxHist = ConfigManager::instance().config().messaging.maxHistoryPerChat;
    if (convo.messages.size() > maxHist) {
        size_t excess = convo.messages.size() - maxHist;
        convo.messages.erase(convo.messages.begin(), convo.messages.begin() + excess);
    }
}

}  // namespace mclite
