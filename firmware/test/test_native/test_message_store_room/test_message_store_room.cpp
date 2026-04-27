#include <Arduino.h>
#include <map>
#include <string>

// Make historyPath / private members accessible
#define private public
#include "storage/MessageStore.h"
#include "config/ConfigManager.h"
#undef private

#include "config/defaults.h"

// Pull in implementations
#include "config/ConfigManager.cpp"
#include "storage/MessageStore.cpp"

// In-memory SD stub: writeFile stores; readFile/fileExists query; mkdir no-op.
static std::map<std::string, std::string>& sdFiles() {
    static std::map<std::string, std::string> m;
    return m;
}

namespace mclite {
    SDCard& SDCard::instance() { static SDCard inst; return inst; }
    bool SDCard::fileExists(const char* path) {
        return sdFiles().count(path) > 0;
    }
    bool SDCard::dirExists(const char*) { return true; }
    String SDCard::readFile(const char* path, size_t) {
        auto it = sdFiles().find(path);
        return it == sdFiles().end() ? String("") : String(it->second.c_str());
    }
    bool SDCard::writeFile(const char* path, const String& content) {
        sdFiles()[path] = content.c_str();
        return true;
    }
    bool SDCard::mkdir(const char*) { return true; }
}

#include <unity.h>

using namespace mclite;

void setUp() {
    sdFiles().clear();
    // Reset singletons
    ConfigManager::instance().config() = AppConfig{};
    ConfigManager::instance().config().messaging.saveHistory = true;
    ConfigManager::instance().config().messaging.maxHistoryPerChat = 100;
    ConfigManager::instance().config().publicKey = "selfpub";
    // Wipe MessageStore conversations
    MessageStore::instance()._convos.clear();
    MessageStore::instance()._activityCounter = 0;
}

void tearDown() {}

// ═══ ConvoId equality discriminates by type ═══

void test_convoid_room_not_equal_to_dm_with_same_id() {
    ConvoId room { ConvoId::ROOM, "abcd1234" };
    ConvoId dm   { ConvoId::DM,   "abcd1234" };
    TEST_ASSERT_FALSE(room == dm);
}

void test_convoid_room_not_equal_to_channel_with_same_id() {
    ConvoId room    { ConvoId::ROOM,    "abcd1234" };
    ConvoId channel { ConvoId::CHANNEL, "abcd1234" };
    TEST_ASSERT_FALSE(room == channel);
}

void test_convoid_room_equal_to_room_with_same_id() {
    ConvoId a { ConvoId::ROOM, "abcd1234" };
    ConvoId b { ConvoId::ROOM, "abcd1234" };
    TEST_ASSERT_TRUE(a == b);
}

// ═══ historyPath has room_ prefix only for ROOM ═══

void test_history_path_room_has_prefix() {
    ConvoId id { ConvoId::ROOM, "abcd1234" };
    String path = MessageStore::instance().historyPath(id);
    // Path is <HISTORY_DIR>/room_abcd1234.json
    TEST_ASSERT_TRUE(path.indexOf("/room_abcd1234.json") >= 0);
}

void test_history_path_dm_no_prefix() {
    ConvoId id { ConvoId::DM, "abcd1234" };
    String path = MessageStore::instance().historyPath(id);
    TEST_ASSERT_TRUE(path.indexOf("/abcd1234.json") >= 0);
    TEST_ASSERT_TRUE(path.indexOf("room_") < 0);
}

void test_history_path_channel_no_prefix() {
    ConvoId id { ConvoId::CHANNEL, "mclite" };
    String path = MessageStore::instance().historyPath(id);
    TEST_ASSERT_TRUE(path.indexOf("/mclite.json") >= 0);
    TEST_ASSERT_TRUE(path.indexOf("room_") < 0);
}

// ═══ syncSince round-trips through saveHistory → loadHistory ═══

void test_sync_since_round_trips() {
    ConvoId id { ConvoId::ROOM, "abcd1234" };
    auto& store = MessageStore::instance();

    // Create the conversation and add one message so the file isn't empty
    Message msg;
    msg.fromSelf  = false;
    msg.text      = "hello";
    msg.timestamp = 1714000000;
    msg.status    = MessageStatus::SENT;
    store.addMessage(id, "RoomName", false, msg);

    // Set syncSince and persist
    store.updateRoomSyncSince(id, 1714000500);
    TEST_ASSERT_EQUAL_UINT32(1714000500, store.getConversation(id)->syncSince);

    // Simulate reboot: clear in-memory state, then reload
    store._convos.clear();
    store._activityCounter = 0;
    store.ensureConversation(id, "RoomName", false);
    store.loadHistory(id);

    Conversation* convo = store.getConversation(id);
    TEST_ASSERT_NOT_NULL(convo);
    TEST_ASSERT_EQUAL_UINT32(1714000500, convo->syncSince);
}

void test_sync_since_never_goes_backwards() {
    ConvoId id { ConvoId::ROOM, "abcd1234" };
    auto& store = MessageStore::instance();
    store.ensureConversation(id, "RoomName", false);

    store.updateRoomSyncSince(id, 2000);
    TEST_ASSERT_EQUAL_UINT32(2000, store.getConversation(id)->syncSince);

    // Older timestamp must be ignored
    store.updateRoomSyncSince(id, 1500);
    TEST_ASSERT_EQUAL_UINT32(2000, store.getConversation(id)->syncSince);
}

// ═══ DM/CHANNEL files don't write syncSince when zero ═══

void test_dm_save_does_not_emit_sync_since() {
    ConvoId id { ConvoId::DM, "deadbeef" };
    auto& store = MessageStore::instance();

    Message msg;
    msg.fromSelf = true;
    msg.text     = "hi";
    msg.timestamp = 1234;
    store.addMessage(id, "Alice", false, msg);

    String path = store.historyPath(id);
    auto it = sdFiles().find(path.c_str());
    TEST_ASSERT_TRUE(it != sdFiles().end());
    // syncSince==0 must NOT be serialized — DM/CHANNEL files stay byte-identical
    TEST_ASSERT_TRUE(it->second.find("syncSince") == std::string::npos);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_convoid_room_not_equal_to_dm_with_same_id);
    RUN_TEST(test_convoid_room_not_equal_to_channel_with_same_id);
    RUN_TEST(test_convoid_room_equal_to_room_with_same_id);

    RUN_TEST(test_history_path_room_has_prefix);
    RUN_TEST(test_history_path_dm_no_prefix);
    RUN_TEST(test_history_path_channel_no_prefix);

    RUN_TEST(test_sync_since_round_trips);
    RUN_TEST(test_sync_since_never_goes_backwards);

    RUN_TEST(test_dm_save_does_not_emit_sync_since);

    return UNITY_END();
}
