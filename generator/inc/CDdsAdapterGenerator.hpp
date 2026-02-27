/**
 * @file        CDdsAdapterGenerator.hpp
 * @author      Aii
 * @brief       DDS Type Adapter Code Generator
 * @date        2026/02/25
 * @details     Generates <Interface>DdsAdapter.hpp per Franca interface.
 *              Each output file contains:
 *                - One IDdsTypeAdapter concrete class per service element
 *                  (one per broadcast, method, and attribute)
 *                - Typed CDR event adapters via fastddsgen PubSubType
 *                - DdsPayload-based serialization for methods/fields
 *                - SetRequestId/GetRequestId overrides on method adapters
 *                - A "Register<Interface>DdsAdapters(serviceId)" inline function
 *                  that populates CDdsTypeRegistry at startup
 *
 *              Element ID scheme (matches CProxyGenerator / CSkeletonGenerator):
 *                broadcasts   : 0x001 .. 0x0FF  (start at 1)
 *                methods      : 0x100 .. 0x1FF  (start at 0x100)
 *                attributes   : 0x200 .. 0x2FF  (start at 0x200)
 *
 *              Phase 2 upgrade path: replace DdsPayloadPubSubType with the
 *              fastddsgen-generated <Topic>PubSubType, and replace the memcpy
 *              body with CDR field mapping.
 *
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §11.3.8 — DDS Adapter Generation
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/25  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CDDADAPTERGENERATOR_HPP
#define LAP_COM_GENERATOR_CDDADAPTERGENERATOR_HPP

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
     * @brief Generates DDS type adapter headers from Franca IDL models
     *
     * @details For each Franca interface `Foo`, writes `FooDdsAdapter.hpp`.
     *          The generated adapters use fastddsgen PubSubType for events
     *          (native CDR serialization with field mapping), and DdsPayload
     *          for methods/fields.  They enable:
     *            - Correct request-id handling on method adapters
     *            - Topic-specific TypeSupport registration via CDdsTypeRegistry
     *            - A clear upgrade path to native fastddsgen TypeSupport
     *
     * @note    The generated DdsAdapter.hpp is header-only.  Include it in
     *          the application entry point and call:
     *            Register<Interface>DdsAdapters(serviceId);
     */
    class CDdsAdapterGenerator : public IGenerator {
    public:
        CDdsAdapterGenerator() noexcept = default;
        ~CDdsAdapterGenerator() noexcept override = default;

        // Non-copyable, non-movable [Rule of Five]
        CDdsAdapterGenerator( const CDdsAdapterGenerator& )            = delete;
        CDdsAdapterGenerator& operator=( const CDdsAdapterGenerator& ) = delete;
        CDdsAdapterGenerator( CDdsAdapterGenerator&& )                  = delete;
        CDdsAdapterGenerator& operator=( CDdsAdapterGenerator&& )       = delete;

        /**
         * @brief Generate <Interface>DdsAdapter.hpp for every interface in the model
         * @param model  Parsed FIDL model
         * @param config Generator configuration (outputDir, namespacePrefix, author used)
         * @return true on success (all interface files written)
         */
        Bool Generate( const FidlModel& model,
                       const GeneratorConfig& config ) override;

    private:
        // ==================== Per-Element Adapter Class Generators ====================

        /**
         * @brief Emit typed event adapter class for one broadcast element
         * @param w           Code writer target
         * @param ifaceName   Interface name (used as class name prefix)
         * @param elemName    PascalCase broadcast/event name
         * @param elementId   Numeric element ID (1-based for events)
         * @param broadcast   Broadcast AST node (contains outArgs for field mapping)
         * @param model       Full FIDL model (for struct/enum resolution)
         * @param appNs       Fully qualified app namespace (e.g., "::helloworld2")
         * @param ddsNsAlias  DDS namespace alias (e.g., "DdsTypes")
         */
        void generateEventAdapter( CCodeWriter&        w,
                                   const String&       ifaceName,
                                   const String&       elemName,
                                   UInt32              elementId,
                                   const BroadcastDef& broadcast,
                                   const FidlModel&    model,
                                   const String&       appNs,
                                   const String&       ddsNsAlias ) const noexcept;

        /**
         * @brief Emit method adapter class for one method element
         * @param w         Code writer target
         * @param ifaceName Interface name (used as class name prefix)
         * @param elemName  Method name
         * @param elementId Numeric element ID (0x100-based for methods)
         */
        void generateMethodAdapter( CCodeWriter&  w,
                                    const String& ifaceName,
                                    const String& elemName,
                                    UInt32        elementId ) const noexcept;

        /**
         * @brief Emit field adapter class for one attribute element
         * @param w         Code writer target
         * @param ifaceName Interface name (used as class name prefix)
         * @param elemName  Attribute name
         * @param elementId Numeric element ID (0x200-based for fields)
         */
        void generateFieldAdapter( CCodeWriter&  w,
                                   const String& ifaceName,
                                   const String& elemName,
                                   UInt32        elementId ) const noexcept;

        /**
         * @brief Emit the shared IDdsTypeAdapter body common to all adapter types
         *        (GetTypeSupport, CreateSample, ExtractData, FreeSample).
         *        Does NOT emit SetRequestId/GetRequestId overrides.
         * @param w         Code writer target
         * @param className Fully formed adapter class name
         */
        void generateAdapterBody( CCodeWriter&  w,
                                  const String& className ) const noexcept;

        /**
         * @brief Emit the registration inline function for the whole interface
         * @param w          Code writer target
         * @param iface      Interface AST node
         * @param ifaceName  PascalCase interface name
         */
        void generateRegistrationFunction( CCodeWriter&     w,
                                           const Interface& iface,
                                           const String&    ifaceName ) const noexcept;

        // ==================== Typed Event Adapter Helpers ====================

        /**
         * @brief Classify a field type for adapter code generation
         */
        enum class FieldCategory : UInt8 {
            kPrimitive,   ///< UInt32, Int64, Float, Double, Boolean, etc.
            kString,      ///< String (std::string on both sides)
            kByteArray,   ///< ByteArray (std::vector<uint8_t> on both sides)
            kEnum,        ///< Franca enumeration
            kStruct       ///< Franca struct (needs recursive field mapping)
        };

        FieldCategory classifyFieldType( const TypeRef&   typeRef,
                                         const FidlModel& model ) const noexcept;

        /**
         * @brief Find a struct definition by qualified name (e.g., "TypeColl.StructName")
         * @return Pointer to StructDef, or nullptr if not found
         */
        const StructDef* findStructDef( const String&    qualifiedName,
                                        const FidlModel& model ) const noexcept;

        /**
         * @brief Strip typeCollection prefix to get DDS-side type name
         *        e.g., "HelloWorld2Types.ServerStatus" → "ServerStatus"
         */
        String ddsTypeName( const String& qualifiedName ) const noexcept;

        /**
         * @brief Emit one field's app → DDS mapping code
         */
        void emitAppToDdsField( CCodeWriter&     w,
                                const Field&     field,
                                const String&    appPrefix,
                                const String&    ddsPrefix,
                                const String&    appNs,
                                const String&    ddsNsAlias,
                                const FidlModel& model ) const noexcept;

        /**
         * @brief Emit one field's DDS → app mapping code
         */
        void emitDdsToAppField( CCodeWriter&     w,
                                const Field&     field,
                                const String&    appPrefix,
                                const String&    ddsPrefix,
                                const String&    appNs,
                                const String&    ddsNsAlias,
                                const FidlModel& model ) const noexcept;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CDDADAPTERGENERATOR_HPP
