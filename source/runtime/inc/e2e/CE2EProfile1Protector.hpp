/**
 * @file        CE2EProfile1Protector.hpp
 * @author      Aii
 * @brief       E2E Profile 1 Protector implementation (CRC-8 SAE J1850)
 * @date        2026/02/07
 * @details     Concrete Strategy for E2E Profile 1 protection on outgoing data.
 *              CRC-8 polynomial: 0x1D (SAE J1850). Counter range: 0-14.
 *              Split from E2EProtection.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01030
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from E2EProtection.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_CE2E_PROFILE1_PROTECTOR_HPP
#define LAP_COM_CE2E_PROFILE1_PROTECTOR_HPP

// ==================== Project-Internal Headers ====================
#include "IE2EProtector.hpp"
#include "E2ETypes.hpp"

namespace lap
{
namespace com
{
namespace e2e
{
    /**
     * @brief E2E Profile 1 Protector implementation
     * @note SWS_CM_01030
     */
    class CE2EProfile1Protector final : public IE2EProtector
    {
    public:
        explicit CE2EProfile1Protector( const E2EProfile1Config& config ) noexcept
            : m_config( config )
            , m_counter( 0 )
        {}

        ~CE2EProfile1Protector() noexcept override = default;

        Result< void > Protect(
            lap::core::Span< lap::core::UInt8 > data ) noexcept override
        {
            if ( data.size() * 8 != m_config.dataLength )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            // Increment counter (wraps at 14 → 0)
            m_counter = ( m_counter + 1 ) % 15;

            // Calculate CRC-8 over data + counter + dataId
            lap::core::UInt8 crc = CalculateCRC8(
                data, m_counter, m_config.dataId );

            // Write counter and CRC to configured offsets
            WriteCounter( data, m_counter );
            WriteCRC( data, crc );

            return Result< void >::FromValue();
        }

        lap::core::UInt32 GetCounter() const noexcept override
        {
            return m_counter;
        }

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CE2EProfile1Protector( const CE2EProfile1Protector& )            = delete;
        CE2EProfile1Protector& operator=( const CE2EProfile1Protector& ) = delete;

    private:
        E2EProfile1Config  m_config;
        lap::core::UInt32  m_counter;

        // ================================================================
        // CRC-8 SAE J1850 (polynomial 0x1D)
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

        void WriteCounter(
            lap::core::Span< lap::core::UInt8 > data,
            lap::core::UInt8 counter ) noexcept
        {
            Size byteOffset = m_config.counterOffset / 8;
            Size bitOffset  = m_config.counterOffset % 8;

            if ( byteOffset < data.size() )
            {
                data.data()[byteOffset] =
                    static_cast< lap::core::UInt8 > (
                        ( data.data()[byteOffset] &
                          ~( 0x0F << bitOffset ) ) |
                        ( ( counter & 0x0F ) << bitOffset ) );
            }
        }

        void WriteCRC(
            lap::core::Span< lap::core::UInt8 > data,
            lap::core::UInt8 crc ) noexcept
        {
            Size byteOffset = m_config.crcOffset / 8;

            if ( byteOffset < data.size() )
            {
                data.data()[byteOffset] = crc;
            }
        }
    };

} // namespace e2e
} // namespace com
} // namespace lap

#endif // LAP_COM_CE2E_PROFILE1_PROTECTOR_HPP
