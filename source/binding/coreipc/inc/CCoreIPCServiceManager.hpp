/**
 * @file        CCoreIPCServiceManager.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — Service lifecycle management
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Manages the service lifecycle for the CoreIPC binding:
 *              - OfferService / StopOfferService / FindService
 *              - Owns the publisher map (one publisher per offered service)
 *              - Creates shared-memory event channels for offered services
 *
 *              NOT thread-safe — the facade (CoreIPCBinding) serialises access.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Split from monolithic CoreIPCBinding
 * </table>
 */

#ifndef LAP_COM_CORE_IPC_CCOREIPCSVCMGR_HPP
#define LAP_COM_CORE_IPC_CCOREIPCSVCMGR_HPP

// ==================== Project-Internal Headers ====================
#include "CoreIPCTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>
#include <lap/core/ipc/Publisher.hpp>

// ==================== Forward Declarations ====================
namespace lap { namespace com { namespace registry { class CRegistryProxy; } } }

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::Result;

    // ====================================================================
    // detail::PublisherWrapper
    // ====================================================================

    namespace detail
    {

        /**
         * @brief   Publisher wrapper — holds publisher + metadata for one service
         */
        struct PublisherWrapper
        {
            UInt64                          m_iServiceId;
            UInt64                          m_iInstanceId;
            String                          m_strShmPath;
            lap::core::ipc::Publisher       m_publisher;

            PublisherWrapper( UInt64 serviceId, UInt64 instanceId,
                              const String& shmPath,
                              lap::core::ipc::Publisher&& publisher ) noexcept
                : m_iServiceId( serviceId )
                , m_iInstanceId( instanceId )
                , m_strShmPath( shmPath )
                , m_publisher( ::std::move( publisher ) )
            {}
        };

    } // namespace detail

    // ====================================================================
    // CCoreIPCServiceManager
    // ====================================================================

    /**
     * @brief   Service lifecycle manager for the CoreIPC binding
     *
     * @details Owns the publisher map. Each OfferService creates a shared-memory
     *          segment and a Publisher, registers the service in the registry.
     *          StopOfferService reverses those steps.
     *
     * @note    NOT thread-safe — caller (facade) must serialise access
     */
    class CCoreIPCServiceManager
    {
    public:
        /**
         * @brief   Construct with references to shared resources
         * @param   config          Binding configuration (read-only after init)
         * @param   mapShmSegments  Shared SHM segment map (facade-owned)
         * @param   pServiceRegistry Registry proxy (facade-owned)
         * @param   metrics         Transport metrics (facade-owned)
         */
        CCoreIPCServiceManager(
            const CoreIPCConfig& config,
            ShmSegmentMap& mapShmSegments,
            SharedHandle< registry::CRegistryProxy >& pServiceRegistry,
            TransportMetrics& metrics ) noexcept;

        ~CCoreIPCServiceManager() noexcept = default;

        // Rule of Five — non-copyable, non-movable
        CCoreIPCServiceManager( const CCoreIPCServiceManager& )             = delete;
        CCoreIPCServiceManager& operator=( const CCoreIPCServiceManager& )  = delete;
        CCoreIPCServiceManager( CCoreIPCServiceManager&& )                  = delete;
        CCoreIPCServiceManager& operator=( CCoreIPCServiceManager&& )       = delete;

    public:
        // ================================================================
        // Service Lifecycle
        // ================================================================

        Result< void > OfferService( UInt64 serviceId,
                                      UInt64 instanceId ) noexcept;

        Result< void > StopOfferService( UInt64 serviceId,
                                          UInt64 instanceId ) noexcept;

        Result< Vector< UInt64 > > FindService( UInt64 serviceId ) noexcept;

    public:
        // ================================================================
        // Internal Accessors (for EventManager)
        // ================================================================

        /**
         * @brief   Get a publisher by composite service key
         * @return  Pointer to PublisherWrapper, or nullptr if not found
         */
        detail::PublisherWrapper* GetPublisher( UInt64 serviceKey ) noexcept;

        /**
         * @brief   Clear all publishers (called during Shutdown)
         */
        void Clear() noexcept;

    private:
        // ================================================================
        // Shared Resource References (facade-owned)
        // ================================================================

        const CoreIPCConfig&        m_config;
        ShmSegmentMap&              m_mapShmSegments;
        SharedHandle< registry::CRegistryProxy >& m_pServiceRegistry;
        TransportMetrics&           m_metrics;

        // ================================================================
        // Owned State
        // ================================================================

        /// Publishers: serviceKey -> PublisherWrapper
        Map< UInt64, UniqueHandle< detail::PublisherWrapper > > m_mapPublishers;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_CORE_IPC_CCOREIPCSVCMGR_HPP
