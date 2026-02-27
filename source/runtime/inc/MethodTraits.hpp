/**
 * @file        MethodTraits.hpp
 * @author      Aii
 * @brief       Compile-time traits for direct typed method path selection
 * @date        2026/02/25
 * @details     Shared by SkeletonMethod and ProxyMethod.
 *              When all argument and output types are trivially copyable and
 *              none is ByteBuffer, methods bypass CBinarySerializer and use
 *              native typed transport paths (Phase 3).
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/25  <td>1.0      <td>Aii     <td>Phase 3 direct typed method path traits
 * </table>
 */
#ifndef LAP_COM_RUNTIME_METHOD_TRAITS_HPP
#define LAP_COM_RUNTIME_METHOD_TRAITS_HPP

#include "binding/common/ITransportBinding.hpp"

#include <type_traits>

namespace lap
{
namespace com
{
    namespace detail
    {
        /**
         * @brief True when all method types are trivially copyable, none is
         *        ByteBuffer, and there is at least one argument.
         *
         * @details When this is true, SkeletonMethod/ProxyMethod bypass
         *          CBinarySerializer and use RegisterMethod<ArgsTuple, Output> /
         *          CallMethod<Output, ArgsTuple> directly (Phase 3 direct typed path).
         *
         * @tparam Output  Method return type
         * @tparam Args    Method argument types
         */
        template< typename Output, typename... Args >
        inline constexpr bool kMethodDirectPath =
            ( sizeof...( Args ) >= 1 ) &&
            std::is_trivially_copyable_v< Output > &&
            !std::is_same_v< Output, binding::ByteBuffer > &&
            ( ( std::is_trivially_copyable_v< std::decay_t< Args > > &&
                !std::is_same_v< std::decay_t< Args >, binding::ByteBuffer > ) && ... );

        /**
         * @brief Same as kMethodDirectPath but for fire-and-forget methods (no Output).
         *
         * @tparam Args  Method argument types
         */
        template< typename... Args >
        inline constexpr bool kFnFDirectPath =
            ( sizeof...( Args ) >= 1 ) &&
            ( ( std::is_trivially_copyable_v< std::decay_t< Args > > &&
                !std::is_same_v< std::decay_t< Args >, binding::ByteBuffer > ) && ... );

    } // namespace detail

} // namespace com
} // namespace lap

#endif // LAP_COM_RUNTIME_METHOD_TRAITS_HPP
