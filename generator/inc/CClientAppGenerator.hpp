/**
 * @file        CClientAppGenerator.hpp
 * @author      Aii
 * @brief       Client Application Framework Code Generator from Franca IDL
 * @date        2026/03/01
 * @details     Generates <Service>ClientApp.hpp — a complete client application
 *              framework class that encapsulates all boilerplate:
 *              - CoreIPC / DDS binding initialization
 *              - BindingManager registration
 *              - DDS type adapter registration
 *              - Service discovery (unified 3-step)
 *              - Proxy creation, event subscription wiring
 *              - Main loop, cleanup
 *
 *              User subclasses and implements ONLY client logic:
 *              - Event handlers (virtual, optional)
 *              - Lifecycle hooks (OnConnected / OnDisconnected / OnTick)
 *              - Method calls via convenience wrappers
 *              - Field operations via convenience wrappers
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CCLIENTAPPGENERATOR_HPP
#define LAP_COM_GENERATOR_CCLIENTAPPGENERATOR_HPP

// ==================== Project-Internal Headers ====================
#include "IGenerator.hpp"
#include "CSchemaHash.hpp"

namespace lap
{
namespace com
{
namespace generator
{

    /**
     * @brief Generates client application framework class from Franca IDL
     * @note  Encapsulates all binding/discovery boilerplate into Run()
     */
    class CClientAppGenerator : public IGenerator {
    public:
        CClientAppGenerator() noexcept = default;
        ~CClientAppGenerator() noexcept override = default;

        // Non-copyable, non-movable
        CClientAppGenerator( const CClientAppGenerator& )            = delete;
        CClientAppGenerator& operator=( const CClientAppGenerator& ) = delete;
        CClientAppGenerator( CClientAppGenerator&& )                  = delete;
        CClientAppGenerator& operator=( CClientAppGenerator&& )       = delete;

        /**
         * @brief Generate <Interface>ClientApp.hpp files
         * @param model  Parsed FIDL model
         * @param config Generator configuration
         * @return true on success
         */
        Bool Generate( const FidlModel& model,
                       const GeneratorConfig& config ) override;

    private:
        void generateEventHandlerDecls( CCodeWriter& w,
                                        const Interface& iface ) const noexcept;

        void generateMethodWrappers( CCodeWriter& w,
                                     const Interface& iface ) const noexcept;

        void generateFieldWrappers( CCodeWriter& w,
                                    const Interface& iface ) const noexcept;

        void generateRunMethod( CCodeWriter& w, const Interface& iface,
                                const FidlModel& model,
                                const GeneratorConfig& config,
                                const ::std::vector< String >& nsSegments ) const noexcept;

        void generateEventSubscriptionWiring( CCodeWriter& w,
                                              const Interface& iface ) const noexcept;

        void generateEventUnsubscription( CCodeWriter& w,
                                          const Interface& iface ) const noexcept;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CCLIENTAPPGENERATOR_HPP
