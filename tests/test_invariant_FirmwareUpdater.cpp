#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "firmware/src/ota/FirmwareUpdater.h"

class SecurityTest : public ::testing::TestWithParam<std::string> {};

TEST_P(SecurityTest, FirmwareUpdateMustVerifySignature) {
    // Invariant: Firmware update must reject unsigned or tampered firmware images
    std::string payload = GetParam();
    
    FirmwareUpdater updater;
    bool result = updater.flashFromSd(payload.c_str(), nullptr, nullptr);
    
    // Security property: unsigned/tampered firmware must be rejected
    EXPECT_FALSE(result) << "Firmware update must fail for: " << payload;
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    SecurityTest,
    ::testing::Values(
        // Exact exploit case: unsigned firmware image
        "malicious_firmware.bin",
        // Boundary case: path traversal attempt
        "../../../etc/passwd",
        // Valid input case: properly signed firmware
        "signed_firmware_v1.2.bin"
    )
);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}