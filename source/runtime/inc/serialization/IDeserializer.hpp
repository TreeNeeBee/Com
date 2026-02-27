/**
 * @file        IDeserializer.hpp
 * @author      Aii
 * @brief       Abstract deserializer interface (Strategy pattern)
 * @date        2026/02/07
 * @details     Defines the Strategy interface for data deserialization.
 *              Concrete strategies: CBinaryDeserializer, future SOME/IP, DDS CDR, etc.
 *              Split from Serialization.hpp following ISP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01103
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Serialization.hpp (ISP refactoring)
 * </table>
 */
#ifndef LAP_COM_IDESERIALIZER_HPP
#define LAP_COM_IDESERIALIZER_HPP

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
     * @brief Deserializer interface (Strategy pattern)
     * @note SWS_CM_01103 - Abstract interface for data deserialization
     *
     * @details All concrete deserializers implement this interface.
     *          Selected at runtime by CSerializerFactory.
     */
    class IDeserializer
    {
    public:
        virtual ~IDeserializer() = default;

        /**
         * @brief Get serialization format
         * @return Serialization format identifier
         */
        virtual SerializationFormat GetFormat() const noexcept = 0;

        /**
         * @brief Get byte order
         * @return Byte order used by this deserializer
         */
        virtual ByteOrder GetByteOrder() const noexcept = 0;

        // ================================================================
        // Primitive Type Deserialization
        // ================================================================

        virtual Result< void > Deserialize( Bool& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::Int8& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::Int16& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::Int32& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::Int64& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::UInt8& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::UInt16& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::UInt32& value ) noexcept = 0;
        virtual Result< void > Deserialize( lap::core::UInt64& value ) noexcept = 0;
        virtual Result< void > Deserialize( Float& value ) noexcept = 0;
        virtual Result< void > Deserialize( Double& value ) noexcept = 0;

        // ================================================================
        // Complex Type Deserialization
        // ================================================================

        virtual Result< void > Deserialize( lap::core::String& value ) noexcept = 0;
        virtual Result< void > DeserializeBytes(
            lap::core::Span< lap::core::UInt8 > data,
            lap::core::UInt32 length ) noexcept = 0;

        // ================================================================
        // Stream State
        // ================================================================

        /**
         * @brief Check if more data is available in the stream
         * @return true if data remains, false otherwise
         */
        virtual Bool HasMoreData() const noexcept = 0;

        /**
         * @brief Reset deserializer to beginning of data
         */
        virtual void Reset() noexcept = 0;
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_IDESERIALIZER_HPP
