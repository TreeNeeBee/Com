/**
 * @file        ServiceHandleType.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Service Handle Definition
 * @date        2025-10-30
 * @details     Service instance identification and handle management (SWS_CM Section 8.1)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_SERVICE_HANDLE_TYPE_HPP
#define LAP_COM_SERVICEHANDLETYPE_HPP

#include "ComTypes.hpp"

namespace lap
{
namespace com
{
    /**
     * @brief Service Handle representing a service instance
     * @tparam ServiceInterface Type of the service interface
     * @note SWS_CM_00301 - Handle for service identification
     */
    template<typename ServiceInterface>
    class ServiceHandleType
    {
    public:
        /**
         * @brief Default constructor
         * @note SWS_CM_00302
         */
        ServiceHandleType() noexcept = default;
        
        /**
         * @brief Constructor with instance identifier
         * @param instanceId Instance identifier of the service
         * @param version Service version
         * @note SWS_CM_00303
         */
        explicit ServiceHandleType(InstanceIdentifierType instanceId,
                                  ServiceVersionType version = ServiceVersionType{}) noexcept
            : m_instanceId(instanceId)
            , m_version(version)
        {}
        
        /**
         * @brief Copy constructor
         * @note SWS_CM_00304
         */
        ServiceHandleType(const ServiceHandleType&) noexcept = default;
        
        /**
         * @brief Move constructor
         * @note SWS_CM_00305
         */
        ServiceHandleType(ServiceHandleType&&) noexcept = default;
        
        /**
         * @brief Copy assignment operator
         * @note SWS_CM_00306
         */
        ServiceHandleType& operator=(const ServiceHandleType&) noexcept = default;
        
        /**
         * @brief Move assignment operator
         * @note SWS_CM_00307
         */
        ServiceHandleType& operator=(ServiceHandleType&&) noexcept = default;
        
        /**
         * @brief Destructor
         * @note SWS_CM_00308
         */
        ~ServiceHandleType() noexcept = default;
        
        /**
         * @brief Get the instance identifier
         * @return Instance identifier
         * @note SWS_CM_00310
         */
        InstanceIdentifierType GetInstanceId() const noexcept
        {
            return m_instanceId;
        }
        
        /**
         * @brief Get the service version
         * @return Service version
         * @note SWS_CM_00311
         */
        ServiceVersionType GetVersion() const noexcept
        {
            return m_version;
        }
        
        /**
         * @brief Equality comparison operator
         * @param other Other service handle
         * @return true if equal, false otherwise
         * @note SWS_CM_00312
         */
        bool operator==(const ServiceHandleType& other) const noexcept
        {
            return m_instanceId == other.m_instanceId && m_version == other.m_version;
        }
        
        /**
         * @brief Inequality comparison operator
         * @param other Other service handle
         * @return true if not equal, false otherwise
         * @note SWS_CM_00313
         */
        bool operator!=(const ServiceHandleType& other) const noexcept
        {
            return !(*this == other);
        }
        
        /**
         * @brief Less-than comparison operator (for ordered containers)
         * @param other Other service handle
         * @return true if this < other, false otherwise
         * @note SWS_CM_00314
         */
        bool operator<(const ServiceHandleType& other) const noexcept
        {
            if (m_instanceId != other.m_instanceId)
                return m_instanceId < other.m_instanceId;
            return m_version < other.m_version;
        }
        
        /**
         * @brief Check if handle is valid
         * @return true if handle is valid, false otherwise
         * @note SWS_CM_00315
         */
        bool IsValid() const noexcept
        {
            return m_instanceId != 0;
        }
        
    private:
        InstanceIdentifierType m_instanceId{0};     ///< Instance identifier
        ServiceVersionType m_version{};             ///< Service version
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_SERVICE_HANDLE_TYPE_HPP
