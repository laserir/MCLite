#include <gtest/gtest.h>
#include <string>
#include <fstream>
#include <vector>
#include <cstdio>
#include "firmware/src/ota/FirmwareUpdater.h"

// --------------------------------------------------------------------------
// Adversarial inputs — must always be rejected (no sidecar, not a real file)
// --------------------------------------------------------------------------

class FirmwareRejectTest : public ::testing::TestWithParam<std::string> {};

TEST_P(FirmwareRejectTest, RejectsBadInput) {
    FirmwareUpdater updater;
    bool result = updater.flashFromSd(GetParam().c_str(), nullptr, nullptr);
    EXPECT_FALSE(result) << "Firmware update must fail for: " << GetParam();
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    FirmwareRejectTest,
    ::testing::Values(
        "malicious_firmware.bin",   // unsigned: no sidecar present
        "../../../etc/passwd"       // path traversal attempt
    )
);

// --------------------------------------------------------------------------
// Valid input — a well-formed .bin + matching .sha256 must be accepted
//
// The mbedTLS stub (firmware/test/stubs/mbedtls/sha256.h) always produces
// the deterministic digest 00 01 02 ... 1f, so the sidecar just needs to
// contain that hex string.
// --------------------------------------------------------------------------

static const char* kStubHex = "000102030405060708090a0b0c0d0e0f"
                               "101112131415161718191a1b1c1d1e1f";

class FirmwareAcceptTest : public ::testing::Test {
protected:
    std::string binPath;
    std::string shaPath;

    void SetUp() override {
        // Create a temp .bin: must be > APP_OFFSET (0x10000) bytes with magic
        // byte 0xE9 at that offset.
        char tmpl[] = "/tmp/test_fw_XXXXXX.bin";
        int fd = mkstemps(tmpl, 4);
        ASSERT_NE(fd, -1);
        close(fd);
        binPath = tmpl;
        shaPath = binPath + ".sha256";

        const size_t APP_OFFSET = 0x10000;
        std::vector<uint8_t> buf(APP_OFFSET + 1, 0x00);
        buf[APP_OFFSET] = 0xE9;

        std::ofstream bin(binPath, std::ios::binary);
        bin.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        bin.close();

        std::ofstream sha(shaPath);
        sha << kStubHex << "\n";
        sha.close();
    }

    void TearDown() override {
        std::remove(binPath.c_str());
        std::remove(shaPath.c_str());
    }
};

TEST_F(FirmwareAcceptTest, AcceptsValidFirmwareWithMatchingSidecar) {
    FirmwareUpdater updater;
    bool result = updater.flashFromSd(binPath.c_str(), nullptr, nullptr);
    EXPECT_TRUE(result) << "Firmware update must succeed for a valid image + sidecar";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
