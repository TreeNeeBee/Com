/**
 * @file        CSkeletonGenerator.hpp
 * @author      Aii
 * @brief       AUTOSAR Service Skeleton Code Generator from Franca IDL
 * @date        2026/02/09
 * @details     Generates <Service>Skeleton.hpp with SkeletonBase-derived class containing
 *              SkeletonEvent, SkeletonMethod, SkeletonField members per SWS_CM_00002.
 * @copyright   Copyright (c) 2026
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.4.5
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CSKELETONGENERATOR_HPP
#define LAP_COM_GENERATOR_CSKELETONGENERATOR_HPP

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
     * @brief Generates AUTOSAR-compliant service skeleton classes from Franca IDL
     * @note [SWS_CM_00002] — Skeleton is non-copyable, move-only
     */
    class CSkeletonGenerator : public IGenerator {
    public:
        CSkeletonGenerator() noexcept = default;
        ~CSkeletonGenerator() noexcept override = default;

        // Non-copyable, non-movable [Rule of Five]
        CSkeletonGenerator( const CSkeletonGenerator& )            = delete;
        CSkeletonGenerator& operator=( const CSkeletonGenerator& ) = delete;
        CSkeletonGenerator( CSkeletonGenerator&& )                  = delete;
        CSkeletonGenerator& operator=( CSkeletonGenerator&& )       = delete;

        /**
         * @brief Generate <Interface>Skeleton.hpp files
         * @param model  Parsed FIDL model
         * @param config Generator configuration
         * @return true on success
         */
        Bool Generate( const FidlModel& model,
                       const GeneratorConfig& config ) override;

    private:
        void generateSkeletonClass( CCodeWriter& w, const Interface& iface,
                                    const FidlModel& model,
                                    const GeneratorConfig& config,
                                    const ::std::vector< String >& nsSegments ) const noexcept;

        void generateOfferServiceImpl( CCodeWriter& w,
                                       const String& className ) const noexcept;

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

#endif // LAP_COM_GENERATOR_CSKELETONGENERATOR_HPP
