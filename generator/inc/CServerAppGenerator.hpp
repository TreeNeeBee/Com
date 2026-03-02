/**
 * @file        CServerAppGenerator.hpp
 * @author      Aii
 * @brief       Server Application Framework Code Generator from Franca IDL
 * @date        2026/03/01
 * @details     Generates <Service>ServerApp.hpp — a complete server application
 *              framework class that encapsulates all boilerplate:
 *              - CRegistryDispatcher / CoreIPC / DDS binding initialization
 *              - SD-Proxy bridge wiring
 *              - BindingManager registration
 *              - DDS type adapter registration
 *              - Skeleton creation, method/field handler wiring
 *              - OfferService, main loop, shutdown
 *
 *              User subclasses and implements ONLY business logic:
 *              - Method handlers (pure virtual)
 *              - Field getters/setters (pure virtual)
 *              - Event sending (helper methods provided)
 *              - Lifecycle hooks (OnStart / OnStop / OnTick)
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CSERVERAPPGENERATOR_HPP
#define LAP_COM_GENERATOR_CSERVERAPPGENERATOR_HPP

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
     * @brief Generates server application framework class from Franca IDL
     * @note  Encapsulates all binding/discovery boilerplate into Run()
     */
    class CServerAppGenerator : public IGenerator {
    public:
        CServerAppGenerator() noexcept = default;
        ~CServerAppGenerator() noexcept override = default;

        // Non-copyable, non-movable
        CServerAppGenerator( const CServerAppGenerator& )            = delete;
        CServerAppGenerator& operator=( const CServerAppGenerator& ) = delete;
        CServerAppGenerator( CServerAppGenerator&& )                  = delete;
        CServerAppGenerator& operator=( CServerAppGenerator&& )       = delete;

        /**
         * @brief Generate <Interface>ServerApp.hpp files
         * @param model  Parsed FIDL model
         * @param config Generator configuration
         * @return true on success
         */
        Bool Generate( const FidlModel& model,
                       const GeneratorConfig& config ) override;

    private:
        void generateMethodHandlerDecls( CCodeWriter& w,
                                         const Interface& iface ) const noexcept;

        void generateFieldHandlerDecls( CCodeWriter& w,
                                        const Interface& iface ) const noexcept;

        void generateEventSendHelpers( CCodeWriter& w,
                                       const Interface& iface ) const noexcept;

        void generateFieldUpdateHelpers( CCodeWriter& w,
                                         const Interface& iface ) const noexcept;

        void generateRunMethod( CCodeWriter& w, const Interface& iface,
                                const FidlModel& model,
                                const GeneratorConfig& config,
                                const ::std::vector< String >& nsSegments ) const noexcept;

        void generateMethodHandlerWiring( CCodeWriter& w,
                                          const Interface& iface ) const noexcept;

        void generateFieldHandlerWiring( CCodeWriter& w,
                                         const Interface& iface ) const noexcept;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CSERVERAPPGENERATOR_HPP
