void setNonBlocking(bool non_blocking);

#include "tap.h"

void TapDevice::setNonBlocking(bool non_blocking) {
    // Implementation using non_blocking parameter
}

#include <gtest/gtest.h>

TEST(TapDeviceTest, SetNonBlocking) {
    TapDevice device;
    device.setNonBlocking(true);
    // Add assertions to verify behavior for true

    device.setNonBlocking(false);
    // Add assertions to verify behavior for false
}