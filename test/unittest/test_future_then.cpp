/**
 * @file        test_future_then.cpp
 * @brief       Unit tests for Future::Then() continuation (AUTOSAR core::Future)
 * @date        2026/02/16
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <core/CFuture.hpp>
#include <core/CPromise.hpp>
#include <core/CResult.hpp>

using namespace lap::core;

// ====================================================================
// Basic Then() — value transformation
// ====================================================================

TEST( FutureThenTest, ThenTransformsValue )
{
    Promise< int > promise;
    auto future = promise.GetFuture();

    // Chain: Future<int> → Future<std::string>
    auto chained = future.Then(
        []( Future< int > f ) -> std::string
        {
            auto result = f.GetResult();
            return std::to_string( result.Value() );
        } );

    // Resolve the original promise
    promise.SetValue( 42 );

    // Wait for chained future
    chained.Wait();
    auto result = chained.GetResult();
    EXPECT_TRUE( result.HasValue() );
    EXPECT_EQ( result.Value(), "42" );
}

// ====================================================================
// Then() with Result unwrapping
// ====================================================================

TEST( FutureThenTest, ThenUnwrapsResult )
{
    Promise< int > promise;
    auto future = promise.GetFuture();

    // Chain: func returns Result<double> → Future<double>
    auto chained = future.Then(
        []( Future< int > f ) -> Result< double >
        {
            auto result = f.GetResult();
            if ( result.HasValue() )
            {
                return Result< double >::FromValue(
                    static_cast< double >( result.Value() ) * 2.5 );
            }
            return Result< double >::FromError( result.Error() );
        } );

    promise.SetValue( 10 );

    chained.Wait();
    auto result = chained.GetResult();
    EXPECT_TRUE( result.HasValue() );
    EXPECT_DOUBLE_EQ( result.Value(), 25.0 );
}

// ====================================================================
// Then() with Result error propagation
// ====================================================================

TEST( FutureThenTest, ThenPropagatesError )
{
    Promise< int > promise;
    auto future = promise.GetFuture();

    auto chained = future.Then(
        []( Future< int > f ) -> Result< std::string >
        {
            auto result = f.GetResult();
            if ( !result.HasValue() )
            {
                return Result< std::string >::FromError( result.Error() );
            }
            return Result< std::string >::FromValue( "ok" );
        } );

    // Set an error instead of a value
    promise.SetError( ErrorCode( FutureErrc::kBrokenPromise ) );

    chained.Wait();
    auto result = chained.GetResult();
    EXPECT_FALSE( result.HasValue() );
}

// ====================================================================
// Then() on already-ready future (inline execution)
// ====================================================================

TEST( FutureThenTest, ThenOnAlreadyReadyFuture )
{
    Promise< int > promise;
    promise.SetValue( 100 );
    auto future = promise.GetFuture();

    // Future is already ready → then() should execute inline
    auto chained = future.Then(
        []( Future< int > f ) -> int
        {
            return f.GetResult().Value() + 1;
        } );

    auto result = chained.GetResult();
    EXPECT_TRUE( result.HasValue() );
    EXPECT_EQ( result.Value(), 101 );
}

// ====================================================================
// Then() on invalid future
// ====================================================================

TEST( FutureThenTest, ThenOnInvalidFuture )
{
    Future< int > future;  // Default-constructed = invalid

    auto chained = future.Then(
        []( Future< int > f ) -> int
        {
            return f.GetResult().Value();
        } );

    auto result = chained.GetResult();
    EXPECT_FALSE( result.HasValue() );
}

// ====================================================================
// Then() with delayed resolution (async)
// ====================================================================

TEST( FutureThenTest, ThenWithDelayedResolution )
{
    Promise< int > promise;
    auto future = promise.GetFuture();

    auto chained = future.Then(
        []( Future< int > f ) -> int
        {
            return f.GetResult().Value() * 3;
        } );

    // Resolve after a delay
    std::thread setter( [&promise]()
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        promise.SetValue( 7 );
    } );

    chained.Wait();
    auto result = chained.GetResult();
    EXPECT_TRUE( result.HasValue() );
    EXPECT_EQ( result.Value(), 21 );

    setter.join();
}

// ====================================================================
// Then() chaining (multiple continuations)
// ====================================================================

TEST( FutureThenTest, ThenChaining )
{
    Promise< int > promise;
    auto future = promise.GetFuture();

    auto step1 = future.Then(
        []( Future< int > f ) -> int
        {
            return f.GetResult().Value() + 10;
        } );

    auto step2 = step1.Then(
        []( Future< int > f ) -> std::string
        {
            return "result=" + std::to_string( f.GetResult().Value() );
        } );

    promise.SetValue( 5 );

    step2.Wait();
    auto result = step2.GetResult();
    EXPECT_TRUE( result.HasValue() );
    EXPECT_EQ( result.Value(), "result=15" );
}

// ====================================================================
// Then() with void Future
// ====================================================================

TEST( FutureThenTest, ThenWithVoidInput )
{
    Promise< void > promise;
    auto future = promise.GetFuture();

    auto chained = future.Then(
        []( Future< void > f ) -> int
        {
            auto result = f.GetResult();
            if ( result.HasValue() )
            {
                return 999;
            }
            return -1;
        } );

    promise.SetValue();

    chained.Wait();
    auto result = chained.GetResult();
    EXPECT_TRUE( result.HasValue() );
    EXPECT_EQ( result.Value(), 999 );
}
