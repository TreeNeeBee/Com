/**
 * @file        E2EProtection.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform End-to-End Protection
 * @date        2025-10-30
 * @details     E2E protection profiles and checking (SWS_CM Section 10.2)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_E2E_PROTECTION_HPP
#define LAP_COM_E2E_PROTECTION_HPP

#include "ComTypes.hpp"
#include <core/CResult.hpp>
#include <core/CSpan.hpp>

namespace lap
{
namespace com
{
namespace e2e
{
    /**
     * @brief E2E Profile configuration base
     * @note SWS_CM_01000
     */
    struct E2EProfileConfig
    {
        lap::core::UInt16 dataId{0};        ///< Unique identifier for data element
        lap::core::UInt32 maxDeltaCounter{0}; ///< Maximum allowed counter delta
        
        virtual ~E2EProfileConfig() = default;
    };
    
    /**
     * @brief E2E Profile 1 configuration
     * @note SWS_CM_01001 - Profile for small data (up to 240 bytes)
     */
    struct E2EProfile1Config : public E2EProfileConfig
    {
        lap::core::UInt8 counterOffset{0};  ///< Bit offset of counter in payload
        lap::core::UInt8 crcOffset{0};      ///< Bit offset of CRC in payload
        lap::core::UInt16 dataLength{0};    ///< Length of data in bits
    };
    
    /**
     * @brief E2E Profile 2 configuration
     * @note SWS_CM_01002 - Profile for medium data (up to 4GB)
     */
    struct E2EProfile2Config : public E2EProfileConfig
    {
        lap::core::UInt16 dataLength{0};    ///< Length of data in bytes
    };
    
    /**
     * @brief E2E Profile 4 configuration
     * @note SWS_CM_01003 - Profile for large data with timestamps
     */
    struct E2EProfile4Config : public E2EProfileConfig
    {
        lap::core::UInt32 minDataLength{0}; ///< Minimum data length in bytes
        lap::core::UInt32 maxDataLength{0}; ///< Maximum data length in bytes
        lap::core::UInt16 offset{0};        ///< Offset of E2E header in bytes
    };
    
    /**
     * @brief E2E Protector interface (sender-side)
     * @note SWS_CM_01010 - Adds E2E protection to outgoing data
     */
    class E2EProtector
    {
    public:
        virtual ~E2EProtector() = default;
        
        /**
         * @brief Protect data with E2E header
         * @param data Data buffer to protect (modified in-place)
         * @return Result indicating success or error
         * @note SWS_CM_01011
         */
        virtual Result<void> Protect(lap::core::Span<lap::core::UInt8> data) noexcept = 0;
        
        /**
         * @brief Get current counter value
         * @return Counter value
         * @note SWS_CM_01012
         */
        virtual lap::core::UInt32 GetCounter() const noexcept = 0;
    };
    
    /**
     * @brief E2E Checker interface (receiver-side)
     * @note SWS_CM_01020 - Checks E2E protection on incoming data
     */
    class E2EChecker
    {
    public:
        virtual ~E2EChecker() = default;
        
        /**
         * @brief Check E2E protection of received data
         * @param data Data buffer to check
         * @return E2E check status
         * @note SWS_CM_01021
         */
        virtual E2ECheckStatus Check(lap::core::Span<const lap::core::UInt8> data) noexcept = 0;
        
        /**
         * @brief Get last check status
         * @return Most recent check status
         * @note SWS_CM_01022
         */
        virtual E2ECheckStatus GetLastCheckStatus() const noexcept = 0;
    };
    
    /**
     * @brief E2E Profile 1 Protector implementation
     * @note SWS_CM_01030
     */
    class E2EProfile1Protector : public E2EProtector
    {
    public:
        explicit E2EProfile1Protector(const E2EProfile1Config& config) noexcept
            : m_config(config), m_counter(0)
        {}
        
        Result<void> Protect(lap::core::Span<lap::core::UInt8> data) noexcept override
        {
            if (data.size() * 8 != m_config.dataLength)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            // Increment counter
            m_counter = (m_counter + 1) % 15;
            
            // Calculate CRC-8 over data + counter + dataId
            lap::core::UInt8 crc = CalculateCRC8(data, m_counter, m_config.dataId);
            
            // Write counter and CRC to data buffer at configured offsets
            WriteCounter(data, m_counter);
            WriteCRC(data, crc);
            
            return Result<void>::FromValue();
        }
        
        lap::core::UInt32 GetCounter() const noexcept override
        {
            return m_counter;
        }
        
    private:
        E2EProfile1Config m_config;
        lap::core::UInt32 m_counter;
        
        lap::core::UInt8 CalculateCRC8(lap::core::Span<const lap::core::UInt8> data,
                                       lap::core::UInt8 counter,
                                       lap::core::UInt16 dataId) const noexcept
        {
            // CRC-8 SAE J1850 polynomial: 0x1D
            lap::core::UInt8 crc = 0xFF;
            
            // Include dataId in CRC
            crc = Crc8Update(crc, static_cast<lap::core::UInt8>(dataId & 0xFF));
            crc = Crc8Update(crc, static_cast<lap::core::UInt8>((dataId >> 8) & 0xFF));
            
            // Include counter in CRC
            crc = Crc8Update(crc, counter);
            
            // Include data payload in CRC
            for (size_t i = 0; i < data.size(); ++i)
            {
                crc = Crc8Update(crc, data.data()[i]);
            }
            
            return crc ^ 0xFF;
        }
        
        lap::core::UInt8 Crc8Update(lap::core::UInt8 crc, lap::core::UInt8 data) const noexcept
        {
            crc ^= data;
            for (int i = 0; i < 8; ++i)
            {
                if (crc & 0x80)
                    crc = (crc << 1) ^ 0x1D;
                else
                    crc <<= 1;
            }
            return crc;
        }
        
        void WriteCounter(lap::core::Span<lap::core::UInt8> data, lap::core::UInt8 counter) noexcept
        {
            size_t byteOffset = m_config.counterOffset / 8;
            size_t bitOffset = m_config.counterOffset % 8;
            
            if (byteOffset < data.size())
            {
                data.data()[byteOffset] = (data.data()[byteOffset] & ~(0x0F << bitOffset)) | 
                                          ((counter & 0x0F) << bitOffset);
            }
        }
        
        void WriteCRC(lap::core::Span<lap::core::UInt8> data, lap::core::UInt8 crc) noexcept
        {
            size_t byteOffset = m_config.crcOffset / 8;
            
            if (byteOffset < data.size())
            {
                data.data()[byteOffset] = crc;
            }
        }
    };
    
    /**
     * @brief E2E Profile 1 Checker implementation
     * @note SWS_CM_01040
     */
    class E2EProfile1Checker : public E2EChecker
    {
    public:
        explicit E2EProfile1Checker(const E2EProfile1Config& config) noexcept
            : m_config(config)
            , m_lastCounter(0)
            , m_lastStatus{E2EResult::kNoNewData, 0}
        {}
        
        E2ECheckStatus Check(lap::core::Span<const lap::core::UInt8> data) noexcept override
        {
            if (data.size() * 8 != m_config.dataLength)
            {
                m_lastStatus = E2ECheckStatus{E2EResult::kError, 0};
                return m_lastStatus;
            }
            
            // Extract counter and CRC from data
            lap::core::UInt8 counter = ReadCounter(data);
            lap::core::UInt8 receivedCrc = ReadCRC(data);
            
            // Calculate expected CRC
            lap::core::UInt8 expectedCrc = CalculateCRC8(data, counter, m_config.dataId);
            
            // Check CRC
            if (receivedCrc != expectedCrc)
            {
                m_lastStatus = E2ECheckStatus{E2EResult::kError, counter};
                return m_lastStatus;
            }
            
            // Check counter sequence
            lap::core::UInt32 expectedCounter = (m_lastCounter + 1) % 15;
            if (counter == m_lastCounter)
            {
                m_lastStatus = E2ECheckStatus{E2EResult::kRepeated, counter};
            }
            else if (counter != expectedCounter)
            {
                lap::core::UInt32 delta = (counter >= m_lastCounter) ? 
                                         (counter - m_lastCounter) : 
                                         (15 - m_lastCounter + counter);
                
                if (delta > m_config.maxDeltaCounter)
                {
                    m_lastStatus = E2ECheckStatus{E2EResult::kWrongSequence, counter};
                }
                else
                {
                    m_lastStatus = E2ECheckStatus{E2EResult::kOk, counter};
                }
            }
            else
            {
                m_lastStatus = E2ECheckStatus{E2EResult::kOk, counter};
            }
            
            m_lastCounter = counter;
            return m_lastStatus;
        }
        
        E2ECheckStatus GetLastCheckStatus() const noexcept override
        {
            return m_lastStatus;
        }
        
    private:
        E2EProfile1Config m_config;
        lap::core::UInt32 m_lastCounter;
        E2ECheckStatus m_lastStatus;
        
        lap::core::UInt8 ReadCounter(lap::core::Span<const lap::core::UInt8> data) const noexcept
        {
            size_t byteOffset = m_config.counterOffset / 8;
            size_t bitOffset = m_config.counterOffset % 8;
            
            if (byteOffset < data.size())
            {
                return (data.data()[byteOffset] >> bitOffset) & 0x0F;
            }
            return 0;
        }
        
        lap::core::UInt8 ReadCRC(lap::core::Span<const lap::core::UInt8> data) const noexcept
        {
            size_t byteOffset = m_config.crcOffset / 8;
            
            if (byteOffset < data.size())
            {
                return data.data()[byteOffset];
            }
            return 0;
        }
        
        lap::core::UInt8 CalculateCRC8(lap::core::Span<const lap::core::UInt8> data,
                                       lap::core::UInt8 counter,
                                       lap::core::UInt16 dataId) const noexcept
        {
            // Same implementation as Protector
            lap::core::UInt8 crc = 0xFF;
            
            crc = Crc8Update(crc, static_cast<lap::core::UInt8>(dataId & 0xFF));
            crc = Crc8Update(crc, static_cast<lap::core::UInt8>((dataId >> 8) & 0xFF));
            crc = Crc8Update(crc, counter);
            
            for (size_t i = 0; i < data.size(); ++i)
            {
                crc = Crc8Update(crc, data.data()[i]);
            }
            
            return crc ^ 0xFF;
        }
        
        lap::core::UInt8 Crc8Update(lap::core::UInt8 crc, lap::core::UInt8 data) const noexcept
        {
            crc ^= data;
            for (int i = 0; i < 8; ++i)
            {
                if (crc & 0x80)
                    crc = (crc << 1) ^ 0x1D;
                else
                    crc <<= 1;
            }
            return crc;
        }
    };
    
} // namespace e2e
} // namespace com
} // namespace lap

#endif // LAP_COM_E2E_PROTECTION_HPP
