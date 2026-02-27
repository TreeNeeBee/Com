/**
 * @file        SerializationTypes.hpp
 * @author      Aii
 * @brief       Serialization format and byte-order enumerations
 * @date        2026/02/07
 * @details     Fundamental types for serialization framework (SWS_CM Section 10.3).
 *              Split from Serialization.hpp following Interface Segregation Principle.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Serialization.hpp (ISP refactoring)
 * </table>
 */
#ifndef LAP_COM_SERIALIZATION_TYPES_HPP
#define LAP_COM_SERIALIZATION_TYPES_HPP

// ==================== Cross-Module Headers ====================
#include <core/CTypedef.hpp>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief Serialization format enumeration
     * @note SWS_CM_01100
     */
    enum class SerializationFormat : lap::core::UInt8
    {
        kSomeIp  = 0,    ///< SOME/IP serialization
        kDDS     = 1,    ///< DDS CDR serialization
        kJSON    = 2,    ///< JSON serialization
        kProtobuf = 3,   ///< Protocol Buffers serialization
        kCustom  = 255   ///< Custom serialization
    };

    /**
     * @brief Byte order enumeration
     * @note SWS_CM_01101
     */
    enum class ByteOrder : lap::core::UInt8
    {
        kBigEndian    = 0,    ///< Big-endian (network byte order)
        kLittleEndian = 1     ///< Little-endian
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_SERIALIZATION_TYPES_HPP
