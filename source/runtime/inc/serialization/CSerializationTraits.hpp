/**
 * @file        CSerializationTraits.hpp
 * @author      Aii
 * @brief       ADL-based serialization traits for proxy/skeleton data path
 * @date        2026/02/07
 * @details     Provides type-erased serialization/deserialization helpers that bridge
 *              the gap between template SampleType parameters and the primitive-typed
 *              ISerializer/IDeserializer interfaces.
 *
 *              For primitive types (bool, int, float, etc.): directly dispatches to
 *              the serializer's overloaded Serialize()/Deserialize() methods.
 *
 *              For user-defined types: relies on ADL (Argument-Dependent Lookup)
 *              free functions provided by generated service contract code:
 *                  void Serialize( ISerializer& s, const UserType& value );
 *                  void Deserialize( IDeserializer& d, UserType& value );
 *
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   ARCHITECTURE_SUMMARY.md §6 Serialization
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Initial serialization traits
 * </table>
 */
#ifndef LAP_COM_CSERIALIZATION_TRAITS_HPP
#define LAP_COM_CSERIALIZATION_TRAITS_HPP

// ==================== Project-Internal Headers ====================
#include "ISerializer.hpp"
#include "IDeserializer.hpp"

// ==================== Standard Library Headers ====================
#include <type_traits>
#include <vector>

namespace lap
{
namespace com
{
namespace serialization
{
    // ====================================================================
    // Primitive type detection trait
    // ====================================================================

    /**
     * @brief Type trait: is T a primitive type supported by ISerializer?
     * @tparam T Type to check
     */
    template< typename T  >
    struct IsPrimitiveSerialization : std::false_type {};

    // Specializations for all ISerializer-supported primitives
    template< > struct IsPrimitiveSerialization< Bool >                  : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::Int8 >      : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::Int16 >     : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::Int32 >     : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::Int64 >     : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::UInt8 >     : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::UInt16 >    : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::UInt32 >    : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::UInt64 >    : std::true_type {};
    template< > struct IsPrimitiveSerialization< Float >                : std::true_type {};
    template< > struct IsPrimitiveSerialization< Double >               : std::true_type {};
    template< > struct IsPrimitiveSerialization< lap::core::String >    : std::true_type {};

    // ====================================================================
    // SerializeValue — serialize a value into an ISerializer
    // ====================================================================

    /**
     * @brief Serialize a primitive type (dispatches to ISerializer overload)
     * @tparam T Primitive type (bool, int, float, String, etc.)
     * @param serializer Target serializer
     * @param value Value to serialize
     * @return Result< void >
     */
    template< typename T  >
    typename std::enable_if<
        IsPrimitiveSerialization< T >::value,
        Result< void >
    >::type
    SerializeValue( ISerializer& serializer, const T& value ) noexcept
    {
        return serializer.Serialize( value );
    }

    /**
     * @brief Serialize a user-defined type via ADL free function
     * @tparam T User-defined struct/class type
     * @param serializer Target serializer
     * @param value Value to serialize
     * @return Result< void >
     *
     * @details Requires a free function in the same namespace as T:
     *          Result< void > Serialize( ISerializer& s, const T& value );
     *
     * @note Generated service contract code provides this for each data type.
     */
    template< typename T  >
    typename std::enable_if<
        !IsPrimitiveSerialization< T >::value,
        Result< void >
    >::type
    SerializeValue( ISerializer& serializer, const T& value ) noexcept
    {
        // ADL lookup: Serialize( ISerializer&, const T& )
        return Serialize( serializer, value );
    }

    // ====================================================================
    // DeserializeValue — deserialize a value from an IDeserializer
    // ====================================================================

    /**
     * @brief Deserialize a primitive type (dispatches to IDeserializer overload)
     * @tparam T Primitive type
     * @param deserializer Source deserializer
     * @param value Output value
     * @return Result< void >
     */
    template< typename T  >
    typename std::enable_if<
        IsPrimitiveSerialization< T >::value,
        Result< void >
    >::type
    DeserializeValue( IDeserializer& deserializer, T& value ) noexcept
    {
        return deserializer.Deserialize( value );
    }

    /**
     * @brief Deserialize a user-defined type via ADL free function
     * @tparam T User-defined struct/class type
     * @param deserializer Source deserializer
     * @param value Output value
     * @return Result< void >
     *
     * @details Requires a free function in the same namespace as T:
     *          Result< void > Deserialize( IDeserializer& d, T& value );
     */
    template< typename T  >
    typename std::enable_if<
        !IsPrimitiveSerialization< T >::value,
        Result< void >
    >::type
    DeserializeValue( IDeserializer& deserializer, T& value ) noexcept
    {
        // ADL lookup: Deserialize( IDeserializer&, T& )
        return Deserialize( deserializer, value );
    }

    // ====================================================================
    // std::vector< T > serialization (length-prefixed)
    // ====================================================================

    /**
     * @brief Serialize a std::vector of primitives
     * @details Writes UInt32 length prefix followed by each element.
     *          Handles ByteBuffer (vector<UInt8>) and other typed vectors.
     */
    template< typename T, typename Alloc >
    Result< void > SerializeValue(
        ISerializer& serializer,
        const ::std::vector< T, Alloc >& vec ) noexcept
    {
        auto rLen = serializer.Serialize( static_cast< UInt32 >( vec.size() ) );
        if ( !rLen.HasValue() ) { return rLen; }

        for ( const auto& elem : vec )
        {
            auto r = SerializeValue( serializer, elem );
            if ( !r.HasValue() ) { return r; }
        }
        return Result< void >::FromValue();
    }

    /**
     * @brief Deserialize a std::vector of primitives
     * @details Reads UInt32 length prefix, then each element.
     */
    template< typename T, typename Alloc >
    Result< void > DeserializeValue(
        IDeserializer& deserializer,
        ::std::vector< T, Alloc >& vec ) noexcept
    {
        UInt32 len = 0;
        auto rLen = deserializer.Deserialize( len );
        if ( !rLen.HasValue() ) { return rLen; }

        vec.resize( static_cast< typename ::std::vector< T, Alloc >::size_type >( len ) );
        for ( UInt32 i = 0; i < len; ++i )
        {
            auto r = DeserializeValue( deserializer, vec[i] );
            if ( !r.HasValue() ) { return r; }
        }
        return Result< void >::FromValue();
    }

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CSERIALIZATION_TRAITS_HPP
