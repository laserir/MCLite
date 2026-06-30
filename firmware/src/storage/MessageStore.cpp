#include "MessageStore.h"
#include "util/log.h"
#include "SDCard.h"
#include "../config/ConfigManager.h"
#include "../config/defaults.h"
#include "../util/MsgHash.h"
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

void MessageStore::removeConversation(const ConvoId& id) {
    for (auto it = _convos.begin(); it != _convos.end(); ++it) {
        if (it->convoId == id) { _convos.erase(it); break; }
    }
    SDCard::instance().remove(historyPath(id).c_str());  // safe no-op if file missing
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
        LOGLN("[MessageStore] ERROR: max conversations reached, reusing last");
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
        LOGF("[MessageStore] Failed to parse history: %s\n", path.c_str());
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
        convo->muted = doc["muted"] | false;
        arr = doc["messages"].as<JsonArray>();
    } else {
        arr = doc.as<JsonArray>();
    }

    // Cap to the most recent maxHistoryPerChat messages. History is stored
    // oldest-first, so skip the leading (oldest) entries beyond the cap. This
    // bounds the resident String copies in convo->messages — loadHistory runs
    // for EVERY conversation at boot, so without this an oversized file (e.g.
    // after lowering max_history_per_chat) would keep all of it in RAM. The
    // runtime path stays capped via pruneIfNeeded().
    const uint16_t cap = ConfigManager::instance().config().messaging.maxHistoryPerChat;
    const size_t total = arr.size();
    size_t skip = (cap > 0 && total > cap) ? total - cap : 0;
    size_t idx = 0;

    for (JsonObject obj : arr) {
        if (idx++ < skip) continue;  // drop oldest beyond the cap
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
        msg.hops = obj["hops"] | 0;
        msg.repeaterCount = obj["rpt"] | 0;
        msg.msgHash = obj["hash"] | "";
        JsonArray rxns = obj["rxn"].as<JsonArray>();
        for (JsonObject r : rxns) {
            Reaction rx;
            rx.emoji      = r["e"] | "";
            rx.senderName = r["s"] | "";
            if (rx.emoji.length() > 0) msg.reactions.push_back(rx);
        }

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
    if (convo->muted) {
        doc["muted"] = true;
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
        if (msg.hops > 0) obj["hops"] = msg.hops;   // received hop count (omit when 0/direct)
        if (msg.repeaterCount > 0) obj["rpt"] = msg.repeaterCount;   // heard-by-N-repeaters (#39)
        if (msg.msgHash.length() > 0) obj["hash"] = msg.msgHash;
        if (!msg.reactions.empty()) {
            JsonArray rxns = obj["rxn"].to<JsonArray>();
            for (const auto& r : msg.reactions) {
                JsonObject ro = rxns.add<JsonObject>();
                ro["e"] = r.emoji;
                ro["s"] = r.senderName;
            }
        }
    }

    String json;
    serializeJson(doc, json);
    String path = historyPath(id);
    bool ok = sd.writeFile(path.c_str(), json);
    LOGF("[History] Save %s: %u msgs, %u bytes, %s\n",
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
    Message stored = msg;
    // Compute hash on first storage so future reactions can target this message.
    if (stored.msgHash.isEmpty() && stored.timestamp > 0) {
        stored.msgHash = computeMsgHash(stored.text, stored.timestamp);
    }
    convo.messages.push_back(stored);
    convo.lastActivity = ++_activityCounter;  // Monotonic: always above loaded values
    if (!stored.fromSelf) {
        convo.hasUnread = true;
    }
    pruneIfNeeded(convo);
    if (!stored.msgHash.isEmpty()) {
        resolvePendingReactionsInternal(convo, stored.msgHash);
    }
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

void MessageStore::updateRepeaterCount(uint32_t packetId, uint8_t count) {
    if (packetId == 0) return;
    for (auto& convo : _convos) {
        for (auto& msg : convo.messages) {
            if (msg.packetId == packetId && msg.fromSelf) {
                if (msg.repeaterCount == count) return;   // no change
                msg.repeaterCount = count;
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

void MessageStore::setMuted(const ConvoId& id, bool muted) {
    Conversation* c = getConversation(id);
    if (c) {
        c->muted = muted;
        saveHistory(id);
    }
}

bool MessageStore::isMuted(const ConvoId& id) {
    Conversation* c = getConversation(id);
    return c ? c->muted : false;
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
    if (maxHist > 0 && convo.messages.size() > maxHist) {  // 0 = unlimited (matches loadHistory)
        size_t excess = convo.messages.size() - maxHist;
        convo.messages.erase(convo.messages.begin(), convo.messages.begin() + excess);
    }
}

bool MessageStore::applyReaction(const ConvoId& id, const String& targetHash,
                                  const String& emoji, const String& senderName) {
    Conversation* convo = getConversation(id);
    if (convo) {
        for (auto& msg : convo->messages) {
            if (msg.msgHash == targetHash) {
                // Dedup: (hash, sender, emoji) triple must be unique
                for (const auto& r : msg.reactions) {
                    if (r.emoji == emoji && r.senderName == senderName) return true;
                }
                msg.reactions.push_back({emoji, senderName});
                saveHistory(id);
                return true;
            }
        }
    }
    // Target not yet received — queue for out-of-order resolution
    if (_pendingReactions.size() >= MAX_PENDING_REACTIONS) {
        _pendingReactions.erase(_pendingReactions.begin());  // evict oldest
    }
    _pendingReactions.push_back({id, targetHash, emoji, senderName});
    return false;
}

void MessageStore::resolvePendingReactionsInternal(Conversation& convo, const String& msgHash) {
    auto it = _pendingReactions.begin();
    while (it != _pendingReactions.end()) {
        if (it->convoId == convo.convoId && it->targetHash == msgHash) {
            for (auto& msg : convo.messages) {
                if (msg.msgHash == msgHash) {
                    bool dup = false;
                    for (const auto& r : msg.reactions) {
                        if (r.emoji == it->emoji && r.senderName == it->senderName) {
                            dup = true; break;
                        }
                    }
                    if (!dup) msg.reactions.push_back({it->emoji, it->senderName});
                    break;
                }
            }
            it = _pendingReactions.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace mclite
