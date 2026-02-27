/**
 * @file        CCoreIPCServiceManager.cpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — CCoreIPCServiceManager implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements service lifecycle: OfferService, StopOfferService,
 *              FindService, and publisher access helpers.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Split from monolithic CoreIPCBinding
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CCoreIPCServiceManager.hpp"
#include "CCoreIPCCodec.hpp"
#include "CRegistryProxy.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/IPCFactory.hpp>
#include <lap/log/CLog.hpp>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace lap::core;
    using namespace lap::core::ipc;

    // ====================================================================
    // Constructor
    // ====================================================================

    CCoreIPCServiceManager::CCoreIPCServiceManager(
        const CoreIPCConfig& config,
        ShmSegmentMap& mapShmSegments,
        SharedHandle< registry::CRegistryProxy >& pServiceRegistry,
        TransportMetrics& metrics ) noexcept
        : m_config( config )
        , m_mapShmSegments( mapShmSegments )
        , m_pServiceRegistry( pServiceRegistry )
        , m_metrics( metrics )
    {
    }

    // ====================================================================
    // Service Lifecycle
    // ====================================================================

    Result< void > CCoreIPCServiceManager::OfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LAP_LOG_INFO() << "[CCoreIPCServiceManager] OfferService: serviceId=0x"
                       << serviceId << ", instanceId=0x" << instanceId;

        auto key = CCoreIPCCodec::MakeServiceKey( serviceId, instanceId );

        // Check if already offered
        if ( m_mapPublishers.find( key ) != m_mapPublishers.end() ) {
            LAP_LOG_WARN() << "[CCoreIPCServiceManager] Service already offered";
            return Result< void >::FromValue();
        }

        // Generate SHM path
        auto shmPath = CCoreIPCCodec::MakeServicePath( serviceId, instanceId );

        // Ensure SHM exists for event channel
        SharedMemoryConfig shmConfig{};
        shmConfig.max_chunks        = static_cast< UInt16 > ( m_config.m_iMaxChunks );
        shmConfig.chunk_size        = static_cast< UInt32 > (
            m_config.m_iMaxPayloadSize + kCoreIPCEventHeaderSize );
        shmConfig.ipc_type          = IPCType::kSPMC;
        shmConfig.channel_capacity  = kMaxChannelCapacity;

        auto shmResult = CCoreIPCCodec::EnsureSharedMemory(
            shmPath, shmConfig, m_mapShmSegments );
        if ( !shmResult ) {
            LAP_LOG_ERROR() << "[CCoreIPCServiceManager] Failed to create shared memory: "
                            << shmResult.Error().Message();
            return Result< void >::FromError( shmResult.Error() );
        }

        // Create publisher
        PublisherConfig pubConfig;
        pubConfig.max_chunks        = m_config.m_iMaxChunks;
        pubConfig.chunk_size        = m_config.m_iMaxPayloadSize + kCoreIPCEventHeaderSize;
        pubConfig.publish_timeout   = 100000000;  // 100ms
        pubConfig.policy            = PublishPolicy::kOverwrite;

        auto pubResult = Publisher::Create( shmPath, pubConfig );
        if ( !pubResult ) {
            LAP_LOG_ERROR() << "[CCoreIPCServiceManager] Failed to create publisher: "
                            << pubResult.Error().Message();
            return Result< void >::FromError( pubResult.Error() );
        }

        // Store publisher wrapper
        auto pWrapper = MakeUnique< detail::PublisherWrapper > (
            serviceId, instanceId, shmPath, ::std::move( pubResult ).Value() );
        m_mapPublishers[key] = ::std::move( pWrapper );

        // Register in CRegistryProxy
        auto regResult = m_pServiceRegistry->RegisterService(
            serviceId, instanceId, 1, 0, "coreipc", shmPath.c_str() );

        if ( !regResult ) {
            LAP_LOG_ERROR() << "[CCoreIPCServiceManager] Failed to register service: "
                            << regResult.Error().Message();
            m_mapPublishers.erase( key );
            return Result< void >::FromError( regResult.Error() );
        }

        LAP_LOG_INFO() << "[CCoreIPCServiceManager] Service offered: " << shmPath;
        return Result< void >::FromValue();
    }

    Result< void > CCoreIPCServiceManager::StopOfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LAP_LOG_INFO() << "[CCoreIPCServiceManager] StopOfferService: serviceId=0x"
                       << serviceId << ", instanceId=0x" << instanceId;

        auto key = CCoreIPCCodec::MakeServiceKey( serviceId, instanceId );

        // Remove publisher
        auto it = m_mapPublishers.find( key );
        if ( it != m_mapPublishers.end() ) {
            m_mapPublishers.erase( it );
        }

        // Release shared memory segments for this service
        m_mapShmSegments.erase(
            CCoreIPCCodec::MakeServicePath( serviceId, instanceId ) );
        m_mapShmSegments.erase(
            CCoreIPCCodec::MakeMethodRequestPath( serviceId, instanceId ) );
        m_mapShmSegments.erase(
            CCoreIPCCodec::MakeMethodResponsePath( serviceId, instanceId ) );

        // Unregister from CRegistryProxy
        auto regResult = m_pServiceRegistry->UnregisterService( serviceId );
        if ( !regResult ) {
            LAP_LOG_WARN() << "[CCoreIPCServiceManager] Failed to unregister: "
                           << regResult.Error().Message();
        }

        LAP_LOG_INFO() << "[CCoreIPCServiceManager] Service stopped";
        return Result< void >::FromValue();
    }

    Result< Vector< UInt64 > > CCoreIPCServiceManager::FindService(
        UInt64 serviceId ) noexcept
    {
        LAP_LOG_INFO() << "[CCoreIPCServiceManager] FindService: serviceId=0x"
                       << serviceId;

        auto slotResult = m_pServiceRegistry->FindService( serviceId );

        Vector< UInt64 > instances;

        if ( slotResult && slotResult->IsActive() ) {
            instances.push_back( slotResult->m_instanceId );
            LAP_LOG_INFO() << "[CCoreIPCServiceManager] Found instance: 0x"
                           << slotResult->m_instanceId
                           << ", endpoint: " << slotResult->m_endpoint;
        }

        return Result< Vector< UInt64 > >::FromValue( instances );
    }

    // ====================================================================
    // Internal Accessors
    // ====================================================================

    detail::PublisherWrapper* CCoreIPCServiceManager::GetPublisher(
        UInt64 serviceKey ) noexcept
    {
        auto it = m_mapPublishers.find( serviceKey );
        if ( it == m_mapPublishers.end() ) {
            return nullptr;
        }
        return it->second.get();
    }

    void CCoreIPCServiceManager::Clear() noexcept
    {
        m_mapPublishers.clear();
    }

} // namespace binding
} // namespace com
} // namespace lap
