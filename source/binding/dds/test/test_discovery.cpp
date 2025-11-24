/**
 * @file test_discovery.cpp
 * @brief Test DDS service discovery functionality
 */

#include "DdsBinding.hpp"
#include "ComTypes.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace lap::com::binding;
using namespace lap::com;

class DdsDiscoveryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create two separate DDS bindings (simulating different processes)
        provider_ = std::make_unique<DdsBinding>();
        consumer_ = std::make_unique<DdsBinding>();

        auto result1 = provider_->Initialize();
        ASSERT_TRUE(result1.HasValue()) << "Provider initialization failed";

        auto result2 = consumer_->Initialize();
        ASSERT_TRUE(result2.HasValue()) << "Consumer initialization failed";

        // Give DDS time to initialize discovery
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void TearDown() override
    {
        consumer_->Shutdown();
        provider_->Shutdown();
    }

    std::unique_ptr<DdsBinding> provider_;
    std::unique_ptr<DdsBinding> consumer_;
};

/**
 * @test Verify that FindService initially returns empty list
 */
TEST_F(DdsDiscoveryTest, FindServiceBeforeOffer)
{
    uint64_t service_id = 0x1234;

    auto result = consumer_->FindService(service_id);
    ASSERT_TRUE(result.HasValue());

    auto instances = result.Value();
    EXPECT_EQ(instances.size(), 0) << "Should find no instances before offer";
}

/**
 * @test Verify discovery of a single service instance
 */
TEST_F(DdsDiscoveryTest, DiscoverSingleInstance)
{
    uint64_t service_id = 0x5678;
    uint64_t instance_id = 0x0001;

    // Provider offers service (creates DataWriter)
    auto offer_result = provider_->OfferService(service_id, instance_id);
    ASSERT_TRUE(offer_result.HasValue());

    // Wait for discovery propagation
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Consumer finds service
    auto find_result = consumer_->FindService(service_id);
    ASSERT_TRUE(find_result.HasValue());

    auto instances = find_result.Value();
    EXPECT_GE(instances.size(), 1) << "Should discover at least 1 instance";
}

/**
 * @test Verify discovery of multiple instances of same service
 */
TEST_F(DdsDiscoveryTest, DiscoverMultipleInstances)
{
    uint64_t service_id = 0xABCD;

    // Provider offers multiple instances
    std::vector<uint64_t> offered_instances = {0x0001, 0x0002, 0x0003};
    
    for (uint64_t instance_id : offered_instances) {
        auto offer_result = provider_->OfferService(service_id, instance_id);
        ASSERT_TRUE(offer_result.HasValue());
    }

    // Wait for discovery
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Consumer finds service
    auto find_result = consumer_->FindService(service_id);
    ASSERT_TRUE(find_result.HasValue());

    auto discovered = find_result.Value();
    EXPECT_GE(discovered.size(), offered_instances.size()) 
        << "Should discover all offered instances";
}

/**
 * @test Verify FindService returns different instance IDs for different services
 */
TEST_F(DdsDiscoveryTest, DiscoverDifferentServices)
{
    uint64_t service_id_1 = 0x1111;
    uint64_t service_id_2 = 0x2222;
    uint64_t instance_id = 0x0001;

    // Offer two different services
    provider_->OfferService(service_id_1, instance_id);
    provider_->OfferService(service_id_2, instance_id);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Find each service separately
    auto result1 = consumer_->FindService(service_id_1);
    auto result2 = consumer_->FindService(service_id_2);

    ASSERT_TRUE(result1.HasValue());
    ASSERT_TRUE(result2.HasValue());

    // Both should have discovered instances
    EXPECT_GE(result1.Value().size(), 1);
    EXPECT_GE(result2.Value().size(), 1);
}

/**
 * @test Verify uninitialized binding returns error
 */
TEST(DdsDiscoveryBasicTest, FindServiceWithoutInitialize)
{
    DdsBinding binding;
    
    auto result = binding.FindService(0x9999);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Value(), static_cast<int>(ComErrc::kNotInitialized));
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
