/**
 * @file        CE2EFactory.hpp
 * @author      Aii
 * @brief       Factory for creating E2E Protector/Checker instances
 * @date        2026/02/07
 * @details     Factory Method pattern: Creates concrete E2E protector/checker
 *              instances based on the profile configuration type.
 *              Decouples client code from concrete E2E profile classes.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — Factory Method pattern
 * </table>
 */
#ifndef LAP_COM_CE2E_FACTORY_HPP
#define LAP_COM_CE2E_FACTORY_HPP

// ==================== Project-Internal Headers ====================
#include "IE2EProtector.hpp"
#include "IE2EChecker.hpp"
#include "E2ETypes.hpp"
#include "CE2EProfile1Protector.hpp"
#include "CE2EProfile1Checker.hpp"

// ==================== Standard Library Headers ====================
#include <memory>

namespace lap
{
namespace com
{
namespace e2e
{
    /**
     * @brief Factory for creating E2E Protector/Checker instances
     *
     * @details Implements the Factory Method pattern.
     *          Usage:
     *          @code
     *          E2EProfile1Config config;
     *          config.dataId = 0x1234;
     *          config.dataLength = 64;
     *          config.counterOffset = 56;
     *          config.crcOffset = 0;
     *
     *          auto protector = CE2EFactory::CreateProtector(config);
     *          auto checker   = CE2EFactory::CreateChecker(config);
     *          @endcode
     */
    class CE2EFactory final
    {
    public:
        // Static-only class — no instantiation
        CE2EFactory()                              = delete;
        ~CE2EFactory()                             = delete;
        CE2EFactory( const CE2EFactory& )          = delete;
        CE2EFactory& operator=( const CE2EFactory& ) = delete;

        /**
         * @brief Create a Profile 1 protector
         * @param config Profile 1 configuration
         * @return Unique pointer to protector instance
         */
        static UniqueHandle< IE2EProtector > CreateProtector(
            const E2EProfile1Config& config ) noexcept
        {
            return MakeUnique< CE2EProfile1Protector > ( config );
        }

        /**
         * @brief Create a Profile 1 checker
         * @param config Profile 1 configuration
         * @return Unique pointer to checker instance
         */
        static UniqueHandle< IE2EChecker > CreateChecker(
            const E2EProfile1Config& config ) noexcept
        {
            return MakeUnique< CE2EProfile1Checker > ( config );
        }

        // Future: Overloads for Profile 2, 4, etc.
        // static UniqueHandle< IE2EProtector > CreateProtector(
        //     const E2EProfile2Config& config) noexcept;
        // static UniqueHandle< IE2EProtector > CreateProtector(
        //     const E2EProfile4Config& config) noexcept;
    };

} // namespace e2e
} // namespace com
} // namespace lap

#endif // LAP_COM_CE2E_FACTORY_HPP
