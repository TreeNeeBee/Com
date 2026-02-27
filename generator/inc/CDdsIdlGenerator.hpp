/**
 * @file        CDdsIdlGenerator.hpp
 * @author      Aii
 * @brief       OMG IDL + QoS XML generator from Franca IDL
 * @date        2026/02/09
 * @details     Converts Franca IDL models to:
 *                - OMG IDL v4.2      (<Interface>.idl)
 *                - DDS QoS XML       (<Interface>_qos.xml)   [always paired with IDL]
 *
 *              QoS values are resolved via CQosLoader with 3-level priority:
 *                Level 1: per-element overrides in service_deploy.yaml
 *                Level 2: named profiles in com_config.yaml
 *                Level 3: built-in defaults (RELIABLE / VOLATILE / KEEP_LAST-10)
 *
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §11.3.6–§11.3.7
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * <tr><td>2026/02/09  <td>1.1      <td>Aii     <td>add QoS XML output + CQosLoader integration
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CDDSIDLGENERATOR_HPP
#define LAP_COM_GENERATOR_CDDSIDLGENERATOR_HPP

// ==================== Project-Internal Headers ====================
#include "IGenerator.hpp"
#include "CQosLoader.hpp"
#include "CSchemaHash.hpp"

namespace lap
{
namespace com
{
namespace generator
{

    /**
     * @brief Generates OMG IDL v4.2 + FastDDS QoS XML files from Franca IDL models
     *
     * @details For every <Interface>.idl produced, a paired <Interface>_qos.xml is
     *          always written alongside it.  QoS parameters are resolved through
     *          CQosLoader with three priority levels (see class docs above).
     *          Instance ID resolution follows: CLI > service_deploy.yaml > default-1.
     */
    class CDdsIdlGenerator : public IGenerator {
    public:
        CDdsIdlGenerator() noexcept = default;
        ~CDdsIdlGenerator() noexcept override = default;

        // Non-copyable, non-movable [Rule of Five]
        CDdsIdlGenerator( const CDdsIdlGenerator& )            = delete;
        CDdsIdlGenerator& operator=( const CDdsIdlGenerator& ) = delete;
        CDdsIdlGenerator( CDdsIdlGenerator&& )                  = delete;
        CDdsIdlGenerator& operator=( CDdsIdlGenerator&& )       = delete;

        /**
         * @brief Generate <Interface>.idl and <Interface>_qos.xml for every interface
         * @param model  Parsed FIDL model
         * @param config Generator configuration (comConfigPath / serviceDeployPath used)
         * @return true on success (all interfaces written)
         */
        Bool Generate( const FidlModel& model,
                       const GeneratorConfig& config ) override;

    private:
        // ==================== IDL Helpers ====================
        void generateEnum( CCodeWriter& w, const EnumDef& enumDef ) const noexcept;
        void generateStruct( CCodeWriter& w, const StructDef& structDef ) const noexcept;
        void generateTypedef( CCodeWriter& w, const TypedefDef& typedefDef ) const noexcept;

        /**
         * @brief Resolve a TypeRef to its OMG IDL type name
         */
        String resolveDdsType( const TypeRef& ref ) const noexcept;

        // ==================== QoS XML Helpers ====================

        /**
         * @brief Write the inner <qos>...</qos> block shared by data_writer + data_reader
         * @param w   Code writer target
         * @param qos Resolved QoS parameters
         */
        void writeQosBlock( CCodeWriter&     w,
                            const QosParams& qos ) const noexcept;

        /**
         * @brief Write one FastDDS XML profile block (data_writer + data_reader pair)
         * @param w           Code writer target
         * @param profileName XML profile name (e.g. "Calculator_speed_event")
         * @param qos         Resolved QoS parameters
         */
        void writeXmlProfile( CCodeWriter&      w,
                              const String&     profileName,
                              const QosParams&  qos ) const noexcept;

        /**
         * @brief Generate <Interface>_qos.xml alongside the IDL file
         * @param iface       Franca interface being processed
         * @param config      Generator configuration (outputDir etc.)
         * @param loader      Pre-loaded CQosLoader for QoS resolution
         * @return true on success
         */
        Bool generateQosXml( const Interface&      iface,
                             const GeneratorConfig& config,
                             const CQosLoader&      loader ) const;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CDDSIDLGENERATOR_HPP
