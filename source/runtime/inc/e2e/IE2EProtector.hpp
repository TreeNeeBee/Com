/**
 * @file        IE2EProtector.hpp
 * @author      Aii
 * @brief       E2E Protector interface (Strategy pattern — sender side)
 * @date        2026/02/07
 * @details     Abstract interface for adding E2E protection to outgoing data.
 *              Concrete strategies: CE2EProfile1Protector, future Profile 2/4/etc.
 *              Split from E2EProtection.hpp following ISP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01010
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from E2EProtection.hpp (ISP refactoring)
 * </table>
 */
#ifndef LAP_COM_IE2E_PROTECTOR_HPP
#define LAP_COM_IE2E_PROTECTOR_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CSpan.hpp>

namespace lap
{
namespace com
{
namespace e2e
{
    /**
     * @brief E2E Protector interface (Strategy pattern — sender side)
     * @note SWS_CM_01010 - Adds E2E protection to outgoing data
     */
    class IE2EProtector
    {
    public:
        virtual ~IE2EProtector() = default;

        /**
         * @brief Protect data with E2E header (modifies buffer in-place)
         * @param data Data buffer to protect
         * @return Result indicating success or error
         * @note SWS_CM_01011
         */
        virtual Result< void > Protect(
            lap::core::Span< lap::core::UInt8 > data ) noexcept = 0;

        /**
         * @brief Get current counter value
         * @return Counter value
         * @note SWS_CM_01012
         */
        virtual lap::core::UInt32 GetCounter() const noexcept = 0;
    };

} // namespace e2e
} // namespace com
} // namespace lap

#endif // LAP_COM_IE2E_PROTECTOR_HPP
