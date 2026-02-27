/**
 * @file        ISerializer.hpp
 * @author      Aii
 * @brief       Abstract serializer interface (Strategy pattern)
 * @date        2026/02/07
 * @details     Defines the Strategy interface for data serialization.
 *              Concrete strategies: CBinarySerializer, future SOME/IP, DDS CDR, etc.
 *              Split from Serialization.hpp following ISP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01102
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Serialization.hpp (ISP refactoring)
 * </table>
 */
#ifndef LAP_COM_ISERIALIZER_HPP
#define LAP_COM_ISERIALIZER_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "SerializationTypes.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CTypedef.hpp>
#include <core/CString.hpp>
#include <core/CSpan.hpp>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief Serializer interface (Strategy pattern)
     * @note SWS_CM_01102 - Abstract interface for data serialization
     *
     * @details All concrete serializers (binary, SOME/IP, DDS CDR, etc.)
     *          implement this interface. Selected at runtime by CSerializerFactory.
     */
    class ISerializer
    {
    public:
        virtual ~ISerializer() = default;

        /**
         * @brief Get serialization format
         * @return Serialization format identifier
         */
        virtual SerializationFormat GetFormat() const noexcept = 0;

        /**
         * @brief Get byte order
         * @return Byte order used by this serializer
         */
        virtual ByteOrder GetByteOrder() const noexcept = 0;

        // ================================================================
        // Primitive Type Serialization
        // ================================================================

        virtual Result< void > Serialize( Bool value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::Int8 value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::Int16 value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::Int32 value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::Int64 value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::UInt8 value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::UInt16 value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::UInt32 value ) noexcept = 0;
        virtual Result< void > Serialize( lap::core::UInt64 value ) noexcept = 0;
        virtual Result< void > Serialize( Float value ) noexcept = 0;
        virtual Result< void > Serialize( Double value ) noexcept = 0;

        // ================================================================
        // Complex Type Serialization
        // ================================================================

        virtual Result< void > Serialize( const lap::core::String& value ) noexcept = 0;
        virtual Result< void > SerializeBytes(
            lap::core::Span< const lap::core::UInt8 > data ) noexcept = 0;

        // ================================================================
        // Buffer Management
        // ================================================================

        /**
         * @brief Get serialized data buffer
         * @return Span of serialized bytes (valid until Reset())
         */
        virtual lap::core::Span< const lap::core::UInt8 > GetData() const noexcept = 0;

        /**
         * @brief Reset serializer state for reuse
         */
        virtual void Reset() noexcept = 0;
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_ISERIALIZER_HPP
