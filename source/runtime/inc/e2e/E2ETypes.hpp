/**
 * @file        E2ETypes.hpp
 * @author      Aii
 * @brief       End-to-End protection profile configuration types
 * @date        2026/02/07
 * @details     E2E profile configuration structures (SWS_CM Section 10.2).
 *              Split from E2EProtection.hpp following ISP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from E2EProtection.hpp (ISP refactoring)
 * </table>
 */
#ifndef LAP_COM_E2E_TYPES_HPP
#define LAP_COM_E2E_TYPES_HPP

// ==================== Cross-Module Headers ====================
#include <core/CTypedef.hpp>

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
        lap::core::UInt16 dataId{ 0 };          ///< Unique identifier for data element
        lap::core::UInt32 maxDeltaCounter{ 0 };  ///< Maximum allowed counter delta

        virtual ~E2EProfileConfig() = default;
    };

    /**
     * @brief E2E Profile 1 configuration
     * @note SWS_CM_01001 - Profile for small data (up to 240 bytes)
     */
    struct E2EProfile1Config : public E2EProfileConfig
    {
        lap::core::UInt8  counterOffset{ 0 };  ///< Bit offset of counter in payload
        lap::core::UInt8  crcOffset{ 0 };      ///< Bit offset of CRC in payload
        lap::core::UInt16 dataLength{ 0 };     ///< Length of data in bits
    };

    /**
     * @brief E2E Profile 2 configuration
     * @note SWS_CM_01002 - Profile for medium data (up to 4GB)
     */
    struct E2EProfile2Config : public E2EProfileConfig
    {
        lap::core::UInt16 dataLength{ 0 };     ///< Length of data in bytes
    };

    /**
     * @brief E2E Profile 4 configuration
     * @note SWS_CM_01003 - Profile for large data with timestamps
     */
    struct E2EProfile4Config : public E2EProfileConfig
    {
        lap::core::UInt32 minDataLength{ 0 };  ///< Minimum data length in bytes
        lap::core::UInt32 maxDataLength{ 0 };  ///< Maximum data length in bytes
        lap::core::UInt16 offset{ 0 };         ///< Offset of E2E header in bytes
    };

} // namespace e2e
} // namespace com
} // namespace lap

#endif // LAP_COM_E2E_TYPES_HPP
