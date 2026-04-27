#include <Arduino.h>

// Make parseJson/toJson accessible
#define private public
#include "config/ConfigManager.h"
#undef private

#include "config/defaults.h"

// Pull in ConfigManager implementation
#include "config/ConfigManager.cpp"

// Stub SDCard methods (parseJson/toJson don't touch SD)
namespace mclite {
    SDCard& SDCard::instance() { static SDCard inst; return inst; }
    bool SDCard::fileExists(const char*) { return false; }
    String SDCard::readFile(const char*, size_t) { return ""; }
    bool SDCard::writeFile(const char*, const String&) { return false; }
}

#include <unity.h>

using namespace mclite;

static ConfigManager* cfg;

void setUp() {
    cfg = &ConfigManager::instance();
    cfg->config() = AppConfig{};
}

void tearDown() {}

static const char* PUBKEY_64HEX =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void test_room_servers_missing_block_yields_empty_list() {
    TEST_ASSERT_TRUE(cfg->parseJson("{}"));
    TEST_ASSERT_EQUAL(0, (int)cfg->config().roomServers.size());
}

void test_room_servers_round_trip() {
    String json = "{\"room_servers\":[{";
    json += "\"name\":\"Ruhrgebiet\",";
    json += "\"public_key\":\"";
    json += PUBKEY_64HEX;
    json += "\",";
    json += "\"password\":\"secret\"}]}";

    TEST_ASSERT_TRUE(cfg->parseJson(json));
    TEST_ASSERT_EQUAL(1, (int)cfg->config().roomServers.size());
    TEST_ASSERT_EQUAL_STRING("Ruhrgebiet", cfg->config().roomServers[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING(PUBKEY_64HEX,  cfg->config().roomServers[0].publicKey.c_str());
    TEST_ASSERT_EQUAL_STRING("secret",      cfg->config().roomServers[0].password.c_str());
    TEST_ASSERT_TRUE(cfg->config().roomServers[0].allowSos);  // default

    // Serialize and re-parse — fields must survive the round-trip
    String reserialized = cfg->toJson();
    cfg->config() = AppConfig{};
    TEST_ASSERT_TRUE(cfg->parseJson(reserialized));
    TEST_ASSERT_EQUAL(1, (int)cfg->config().roomServers.size());
    TEST_ASSERT_EQUAL_STRING("Ruhrgebiet", cfg->config().roomServers[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING(PUBKEY_64HEX,  cfg->config().roomServers[0].publicKey.c_str());
    TEST_ASSERT_EQUAL_STRING("secret",      cfg->config().roomServers[0].password.c_str());
    TEST_ASSERT_TRUE(cfg->config().roomServers[0].allowSos);
}

void test_room_servers_allow_sos_false_round_trip() {
    String json = "{\"room_servers\":[{";
    json += "\"name\":\"NoisyPublic\",";
    json += "\"public_key\":\"";
    json += PUBKEY_64HEX;
    json += "\",";
    json += "\"password\":\"\",";
    json += "\"allow_sos\":false}]}";

    TEST_ASSERT_TRUE(cfg->parseJson(json));
    TEST_ASSERT_EQUAL(1, (int)cfg->config().roomServers.size());
    TEST_ASSERT_FALSE(cfg->config().roomServers[0].allowSos);

    String reserialized = cfg->toJson();
    cfg->config() = AppConfig{};
    TEST_ASSERT_TRUE(cfg->parseJson(reserialized));
    TEST_ASSERT_EQUAL(1, (int)cfg->config().roomServers.size());
    TEST_ASSERT_FALSE(cfg->config().roomServers[0].allowSos);
}

void test_room_servers_empty_password_round_trip() {
    // Public room: empty password is valid (BaseChatMesh::sendLogin handles strlen("")==0)
    String json = "{\"room_servers\":[{";
    json += "\"name\":\"PublicRoom\",";
    json += "\"public_key\":\"";
    json += PUBKEY_64HEX;
    json += "\",";
    json += "\"password\":\"\"}]}";

    TEST_ASSERT_TRUE(cfg->parseJson(json));
    TEST_ASSERT_EQUAL(1, (int)cfg->config().roomServers.size());
    TEST_ASSERT_EQUAL_STRING("", cfg->config().roomServers[0].password.c_str());

    String reserialized = cfg->toJson();
    cfg->config() = AppConfig{};
    TEST_ASSERT_TRUE(cfg->parseJson(reserialized));
    TEST_ASSERT_EQUAL(1, (int)cfg->config().roomServers.size());
    TEST_ASSERT_EQUAL_STRING("", cfg->config().roomServers[0].password.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_room_servers_missing_block_yields_empty_list);
    RUN_TEST(test_room_servers_round_trip);
    RUN_TEST(test_room_servers_empty_password_round_trip);
    RUN_TEST(test_room_servers_allow_sos_false_round_trip);
    return UNITY_END();
}
