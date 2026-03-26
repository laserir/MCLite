#pragma once

#include <vector>
#include <cstdint>
#include <Arduino.h>

namespace mclite {

enum class MessageStatus : uint8_t {
    SENDING,    // Queued for transmission
    SENT,       // Transmitted (single tick)
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

// Identifies a conversation (either DM or channel)
struct ConvoId {
    enum Type { DM, CHANNEL };
    Type   type;
    String id;  // Contact shortId for DM, channel name for channels

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
    uint32_t lastActivity = 0;
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

    // Get conversation by ID
    Conversation* getConversation(const ConvoId& id);

    // Get all conversations sorted by last activity
    std::vector<Conversation*> getConversationsSorted();

    // Mark conversation as read
    void markRead(const ConvoId& id);

    static MessageStore& instance();

private:
    static constexpr size_t MAX_CONVERSATIONS = 48;  // MAX_CONTACTS(32) + MAX_GROUP_CHANNELS(16)

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
