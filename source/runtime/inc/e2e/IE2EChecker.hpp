/**
 * @file        IE2EChecker.hpp
 * @author      Aii
 * @brief       E2E Checker interface (Strategy pattern — receiver side)
 * @date        2026/02/07
 * @details     Abstract interface for checking E2E protection on incoming data.
 *              Concrete strategies: CE2EProfile1Checker, future Profile 2/4/etc.
 *              Split from E2EProtection.hpp following ISP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01020
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from E2EProtection.hpp (ISP refactoring)
 * </table>
 */
#ifndef LAP_COM_IE2E_CHECKER_HPP
#define LAP_COM_IE2E_CHECKER_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CSpan.hpp>

namespace lap
{
namespace com
{
namespace e2e
{
    /**
     * @brief E2E Checker interface (Strategy pattern — receiver side)
     * @note SWS_CM_01020 - Checks E2E protection on incoming data
     */
    class IE2EChecker
    {
    public:
        virtual ~IE2EChecker() = default;

        /**
         * @brief Check E2E protection of received data
         * @param data Data buffer to check
         * @return E2E check status
         * @note SWS_CM_01021
         */
        virtual E2ECheckStatus Check(
            lap::core::Span< const lap::core::UInt8 > data ) noexcept = 0;

        /**
         * @brief Get last check status
         * @return Most recent check status
         * @note SWS_CM_01022
         */
        virtual E2ECheckStatus GetLastCheckStatus() const noexcept = 0;
    };

} // namespace e2e
} // namespace com
} // namespace lap

#endif // LAP_COM_IE2E_CHECKER_HPP
