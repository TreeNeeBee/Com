/**
 * @file        CProxyGenerator.hpp
 * @author      Aii
 * @brief       AUTOSAR Service Proxy Code Generator from Franca IDL
 * @date        2026/02/09
 * @details     Generates <Service>Proxy.hpp with ProxyBase-derived class containing
 *              ProxyEvent, ProxyMethod, ProxyField members per SWS_CM_00004.
 * @copyright   Copyright (c) 2026
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.3.8
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CPROXYGENERATOR_HPP
#define LAP_COM_GENERATOR_CPROXYGENERATOR_HPP

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
     * @brief Generates AUTOSAR-compliant service proxy classes from Franca IDL
     * @note [SWS_CM_00004] — Proxy is non-copyable, move-only, with named constructor
     */
    class CProxyGenerator : public IGenerator {
    public:
        CProxyGenerator() noexcept = default;
        ~CProxyGenerator() noexcept override = default;

        // Non-copyable, non-movable [Rule of Five]
        CProxyGenerator( const CProxyGenerator& )            = delete;
        CProxyGenerator& operator=( const CProxyGenerator& ) = delete;
        CProxyGenerator( CProxyGenerator&& )                  = delete;
        CProxyGenerator& operator=( CProxyGenerator&& )       = delete;

        /**
         * @brief Generate <Interface>Proxy.hpp files
         * @param model  Parsed FIDL model
         * @param config Generator configuration
         * @return true on success
         */
        Bool Generate( const FidlModel& model,
                       const GeneratorConfig& config ) override;

    private:
        void generateProxyClass( CCodeWriter& w, const Interface& iface,
                                 const FidlModel& model,
                                 const GeneratorConfig& config,
                                 const ::std::vector< String >& nsSegments ) const noexcept;

        void generateCreateMethod( CCodeWriter& w, const String& className ) const noexcept;

        void generateBindingContextHook( CCodeWriter& w,
                                         const Interface& iface ) const noexcept;

        void generateEventClasses( CCodeWriter& w,
                                   const Interface& iface ) const noexcept;

        void generateMethodClasses( CCodeWriter& w,
                                    const Interface& iface ) const noexcept;

        void generateFieldClasses( CCodeWriter& w,
                                   const Interface& iface ) const noexcept;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CPROXYGENERATOR_HPP
