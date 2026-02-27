/**
 * @file        CE2EProfile1Checker.hpp
 * @author      Aii
 * @brief       E2E Profile 1 Checker implementation (CRC-8 SAE J1850)
 * @date        2026/02/07
 * @details     Concrete Strategy for E2E Profile 1 checking on incoming data.
 *              Validates CRC-8 and counter sequence.
 *              Split from E2EProtection.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01040
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from E2EProtection.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_CE2E_PROFILE1_CHECKER_HPP
#define LAP_COM_CE2E_PROFILE1_CHECKER_HPP

// ==================== Project-Internal Headers ====================
#include "IE2EChecker.hpp"
#include "E2ETypes.hpp"

namespace lap
{
namespace com
{
namespace e2e
{
    /**
     * @brief E2E Profile 1 Checker implementation
     * @note SWS_CM_01040
     */
    class CE2EProfile1Checker final : public IE2EChecker
    {
    public:
        explicit CE2EProfile1Checker( const E2EProfile1Config& config ) noexcept
            : m_config( config )
            , m_lastCounter( 0 )
            , m_lastStatus{ E2EResult::kNoNewData, 0 }
        {}

        ~CE2EProfile1Checker() noexcept override = default;

        E2ECheckStatus Check(
            lap::core::Span< const lap::core::UInt8 > data ) noexcept override
        {
            if ( data.size() * 8 != m_config.dataLength )
            {
                m_lastStatus = E2ECheckStatus{ E2EResult::kError, 0 };
                return m_lastStatus;
            }

            // Extract counter and CRC from data
            lap::core::UInt8 counter    = ReadCounter( data );
            lap::core::UInt8 receivedCrc = ReadCRC( data );

            // Calculate expected CRC
            lap::core::UInt8 expectedCrc = CalculateCRC8(
                data, counter, m_config.dataId );

            // Validate CRC
            if ( receivedCrc != expectedCrc )
            {
                m_lastStatus = E2ECheckStatus{ E2EResult::kError, counter };
                return m_lastStatus;
            }

            // Validate counter sequence
            lap::core::UInt32 expectedCounter =
                ( m_lastCounter + 1 ) % 15;

            if ( counter == m_lastCounter )
            {
                m_lastStatus = E2ECheckStatus{
                    E2EResult::kRepeated, counter };
            }
            else if ( counter != expectedCounter )
            {
                lap::core::UInt32 delta =
                    ( counter >= m_lastCounter )
                        ? ( counter - m_lastCounter )
                        : ( 15 - m_lastCounter + counter );

                if ( delta > m_config.maxDeltaCounter )
                {
                    m_lastStatus = E2ECheckStatus{
                        E2EResult::kWrongSequence, counter };
                }
                else
                {
                    m_lastStatus = E2ECheckStatus{
                        E2EResult::kOk, counter };
                }
            }
            else
            {
                m_lastStatus = E2ECheckStatus{
                    E2EResult::kOk, counter };
            }

            m_lastCounter = counter;
            return m_lastStatus;
        }

        E2ECheckStatus GetLastCheckStatus() const noexcept override
        {
            return m_lastStatus;
        }

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CE2EProfile1Checker( const CE2EProfile1Checker& )            = delete;
        CE2EProfile1Checker& operator=( const CE2EProfile1Checker& ) = delete;

    private:
        E2EProfile1Config  m_config;
        lap::core::UInt32  m_lastCounter;
        E2ECheckStatus     m_lastStatus;

        // ================================================================
        // CRC-8 SAE J1850 (shared algorithm with Protector)
        // ================================================================

        static lap::core::UInt8 Crc8Update(
            lap::core::UInt8 crc,
            lap::core::UInt8 data ) noexcept
        {
            crc ^= data;
            for ( lap::core::Int32 i = 0; i < 8; ++i )
            {
                if ( crc & 0x80 )
                {
                    crc = static_cast< lap::core::UInt8 > (
                        ( crc << 1 ) ^ 0x1D );
                }
                else
                {
                    crc = static_cast< lap::core::UInt8 > ( crc << 1 );
                }
            }
            return crc;
        }

        lap::core::UInt8 CalculateCRC8(
            lap::core::Span< const lap::core::UInt8 > data,
            lap::core::UInt8 counter,
            lap::core::UInt16 dataId ) const noexcept
        {
            lap::core::UInt8 crc = 0xFF;

            crc = Crc8Update( crc,
                static_cast< lap::core::UInt8 > ( dataId & 0xFF ) );
            crc = Crc8Update( crc,
                static_cast< lap::core::UInt8 > ( ( dataId >> 8 ) & 0xFF ) );
            crc = Crc8Update( crc, counter );

            for ( Size i = 0; i < data.size(); ++i )
            {
                crc = Crc8Update( crc, data.data()[i] );
            }

            return static_cast< lap::core::UInt8 > ( crc ^ 0xFF );
        }

        lap::core::UInt8 ReadCounter(
            lap::core::Span< const lap::core::UInt8 > data ) const noexcept
        {
            Size byteOffset = m_config.counterOffset / 8;
            Size bitOffset  = m_config.counterOffset % 8;

            if ( byteOffset < data.size() )
            {
                return static_cast< lap::core::UInt8 > (
                    ( data.data()[byteOffset] >> bitOffset ) & 0x0F );
            }
            return 0;
        }

        lap::core::UInt8 ReadCRC(
            lap::core::Span< const lap::core::UInt8 > data ) const noexcept
        {
            Size byteOffset = m_config.crcOffset / 8;

            if ( byteOffset < data.size() )
            {
                return data.data()[byteOffset];
            }
            return 0;
        }
    };

} // namespace e2e
} // namespace com
} // namespace lap

#endif // LAP_COM_CE2E_PROFILE1_CHECKER_HPP
