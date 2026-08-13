// Platform integration tests for QuerySet.
//
// test_device.cpp already covers "createQuerySet with type=occlusion,
// count=8 returns non-null". These tests cover the other QuerySetType,
// count edges, and that multiple query sets are independent objects.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/constants/query_set_type.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__)
    return Device::createDefaultDevice(nullptr);
#elif defined(__APPLE__)
    return Device::createDefaultDevice(nullptr);
#elif defined(_WIN32)
    return Device::createDefaultDevice(nullptr);
#elif defined(__linux__)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Type coverage.
// ---------------------------------------------------------------------------

TEST(QuerySet, CreateOcclusionQuerySetReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    QuerySetDescriptor desc{};
    desc.type  = QuerySetType::occlusion;
    desc.count = 4;

    auto querySet = device->createQuerySet(desc);
    EXPECT_NE(querySet, nullptr);
}

TEST(QuerySet, CreateTimestampQuerySetReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    QuerySetDescriptor desc{};
    desc.type  = QuerySetType::timestamp;
    desc.count = 4;

    auto querySet = device->createQuerySet(desc);
    EXPECT_NE(querySet, nullptr);
}

// ---------------------------------------------------------------------------
// Count edges.
// ---------------------------------------------------------------------------

TEST(QuerySet, CreateWithCountOfOneReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    QuerySetDescriptor desc{};
    desc.type  = QuerySetType::occlusion;
    desc.count = 1;

    auto querySet = device->createQuerySet(desc);
    EXPECT_NE(querySet, nullptr);
}

TEST(QuerySet, CreateWithLargeCountReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    QuerySetDescriptor desc{};
    desc.type  = QuerySetType::occlusion;
    desc.count = 4096;

    auto querySet = device->createQuerySet(desc);
    EXPECT_NE(querySet, nullptr);
}

// ---------------------------------------------------------------------------
// Multiple independent query sets.
// ---------------------------------------------------------------------------

TEST(QuerySet, MultipleQuerySetsCanBeCreatedIndependently) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    QuerySetDescriptor descA{};
    descA.type  = QuerySetType::occlusion;
    descA.count = 4;

    QuerySetDescriptor descB{};
    descB.type  = QuerySetType::timestamp;
    descB.count = 2;

    auto querySetA = device->createQuerySet(descA);
    auto querySetB = device->createQuerySet(descB);

    ASSERT_NE(querySetA, nullptr);
    ASSERT_NE(querySetB, nullptr);
    EXPECT_NE(querySetA.get(), querySetB.get());
}
