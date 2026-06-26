#pragma once

#include <vector>
#include <cstdint>
#include <Arduino.h>

namespace mclite {

enum class MessageStatus : uint8_t {
    SENDING,    // Queued for transmission
    SENT,       // Transmitted (single tick)
    REPEATED,   // Heard re-broadcast by a repeater (↺)
    DELIVERED,  // ACK received (double tick)
    FAILED      // Send failed
};

struct Message {
    bool     fromSelf;      // true = we sent it, false = received
    String   text;
    uint32_t timestamp;     // Unix timestamp (from GPS) or millis
    MessageStatus status = MessageStatus::SENT;
    String   senderName;    // For channel messages: who sent it
    uint32_t packetId = 0;  // For ACK tracking
};

// Identifies a conversation (DM, channel, or room)
struct ConvoId {
    enum Type { DM, CHANNEL, ROOM };
    Type   type;
    String id;  // Contact shortId for DM, channel name for channels, room shortId for rooms

    bool operator==(const ConvoId& other) const {
        return type == other.type && id == other.id;
    }
};

// A conversation with cached messages
struct Conversation {
    ConvoId convoId;
    String  displayName;
    bool    isPrivate = false;  // For channels
    bool    readOnly  = false;  // Hide input bar in chat view
    bool    hasUnread = false;
    bool    muted     = false;  // Mute regular message notifications for this chat
    uint32_t lastActivity = 0;
    uint32_t syncSince    = 0;  // ROOM: last synced post timestamp; 0 for DM/CHANNEL
    std::vector<Message> messages;  // RAM cache

    const Message* lastMessage() const {
        return messages.empty() ? nullptr : &messages.back();
    }
};

class MessageStore {
public:
    // Load history for a conversation from SD
    void loadHistory(const ConvoId& id);
    // Save a single conversation's history to SD
    void saveHistory(const ConvoId& id);

    // Create a conversation entry without adding a message
    Conversation& ensureConversation(const ConvoId& id, const String& displayName,
                                     bool isPrivate, bool readOnly = false);

    // Add message to conversation (creates convo if needed)
    Conversation& addMessage(const ConvoId& id, const String& displayName,
                             bool isPrivate, const Message& msg, bool readOnly = false);

    // Update message status by packetId
    void updateStatus(uint32_t packetId, MessageStatus status);

    // Upgrade SENT → REPEATED; won't downgrade DELIVERED or overwrite FAILED.
    void updateStatusToRepeated(uint32_t packetId);

    // Get conversation by ID
    Conversation* getConversation(const ConvoId& id);

    // Get all conversations sorted by last activity
    std::vector<Conversation*> getConversationsSorted();

    // Stable index access to the underlying conversation list (insertion order, NOT
    // recency — unlike getConversationsSorted). Used by the companion sync cursor so a
    // session can walk every stored message without a fixed-size staging cap. An index
    // can shift if a conversation is removed mid-walk; callers tolerate it (the app
    // dedups by sender+timestamp and re-syncs on reconnect).
    size_t conversationCount() const { return _convos.size(); }
    Conversation* conversationByIndex(size_t i) { return i < _convos.size() ? &_convos[i] : nullptr; }

    // Mark conversation as read
    void markRead(const ConvoId& id);

    // Mute / unmute a conversation
    void setMuted(const ConvoId& id, bool muted);
    bool isMuted(const ConvoId& id);

    // ROOM-only: persist sender_timestamp as syncSince. Triggers saveHistory().
    void updateRoomSyncSince(const ConvoId& id, uint32_t timestamp);

    // Drop a conversation's in-memory cache AND its on-SD history file. Used when
    // a contact/channel/room is removed on-device so a re-added same-id entry
    // can't inherit stale history. No-op if neither exists.
    void removeConversation(const ConvoId& id);

    static MessageStore& instance();

private:
    // MAX_CONTACTS(40) + MAX_GROUP_CHANNELS(16); 40 covers 32 chat contacts + 8 rooms
    static constexpr size_t MAX_CONVERSATIONS = 56;

    MessageStore() { _convos.reserve(MAX_CONVERSATIONS); }

    // IMPORTANT: reserve() in ctor guarantees pointer/reference stability.
    // getOrCreate() enforces the cap so size never exceeds capacity.
    std::vector<Conversation> _convos;
    uint32_t _activityCounter = 0;  // Monotonic counter for conversation ordering

    Conversation& getOrCreate(const ConvoId& id, const String& displayName,
                              bool isPrivate, bool readOnly = false);
    String historyPath(const ConvoId& id) const;
    void pruneIfNeeded(Conversation& convo);
};

}  // namespace mclite
