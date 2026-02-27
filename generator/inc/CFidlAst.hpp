/**
 * @file        CFidlAst.hpp
 * @author      Aii
 * @brief       Franca IDL Abstract Syntax Tree Node Definitions
 * @date        2026/02/09
 * @details     Defines all AST node types for representing parsed Franca IDL models.
 *              Used as the intermediate representation between parser and code generators.
 *              Part of the dual-layer IDL architecture (Franca IDL SSOT).
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 compliant code generation support
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CFIDLAST_HPP
#define LAP_COM_GENERATOR_CFIDLAST_HPP

// ==================== Standard Library Headers ====================
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== Generator-Local Type Aliases ====================
    // Self-contained aliases (build tool, no lap::core dependency)
    using Bool       = bool;
    using Char       = char;
    using Int32      = ::std::int32_t;
    using UInt8      = ::std::uint8_t;
    using UInt16     = ::std::uint16_t;
    using UInt32     = ::std::uint32_t;
    using UInt64     = ::std::uint64_t;
    using String     = ::std::string;
    using StringView = ::std::string_view;

    template< typename T >
    using UniquePtr = ::std::unique_ptr< T >;

    template< typename T >
    using SharedPtr = ::std::shared_ptr< T >;

    // ==================== Source Location ====================

    /**
     * @brief Source location for error reporting
     */
    struct SourceLocation {
        String file;
        UInt32 line   = 0;
        UInt32 column = 0;
    };

    // ==================== Version Node ====================

    /**
     * @brief Franca IDL version declaration (major.minor.patch)
     */
    struct Version {
        UInt32 major = 0;
        UInt32 minor = 0;
        UInt32 patch = 0;

        Bool IsValid() const noexcept { return major > 0 || minor > 0; }

        String ToString() const noexcept {
            return ::std::to_string( major ) + "."
                 + ::std::to_string( minor ) + "."
                 + ::std::to_string( patch );
        }
    };

    // ==================== Type Reference ====================

    /**
     * @brief Reference to a type (may be qualified, may be array)
     */
    struct TypeRef {
        String name;            ///< e.g., "Float", "CalculatorTypes.Operation"
        Bool   isArray = false;  ///< true if Type[]

        Bool IsQualified() const noexcept {
            return name.find( '.' ) != String::npos;
        }

        /**
         * @brief Convert dot-separated name to C++ qualified name
         * @return C++ name with :: separators
         */
        String ToCppName() const noexcept {
            String result = name;
            for ( auto& c : result ) {
                if ( c == '.' ) {
                    c = ':';
                }
            }
            // Replace single ':' with '::'
            String cppName;
            cppName.reserve( result.size() + 8 );
            for ( ::std::size_t i = 0; i < result.size(); ++i ) {
                if ( result[i] == ':' ) {
                    cppName += "::";
                } else {
                    cppName += result[i];
                }
            }
            return cppName;
        }
    };

    // ==================== Field (struct member / method parameter) ====================

    /**
     * @brief A typed field (used in structs, method args, broadcast args)
     */
    struct Field {
        TypeRef type;
        String  name;
    };

    // ==================== Enumerator ====================

    /**
     * @brief Single enumerator within an enumeration
     */
    struct Enumerator {
        String name;
        Int32  value            = 0;
        Bool   hasExplicitValue = false;
    };

    // ==================== Enum Definition ====================

    /**
     * @brief Franca IDL enumeration definition
     */
    struct EnumDef {
        String                      name;
        ::std::vector< Enumerator > enumerators;
        SourceLocation              location;
    };

    // ==================== Struct Definition ====================

    /**
     * @brief Franca IDL struct definition
     */
    struct StructDef {
        String                  name;
        String                  extends;  ///< Base struct (empty if none)
        ::std::vector< Field >  fields;
        SourceLocation          location;
    };

    // ==================== Typedef Definition ====================

    /**
     * @brief Franca IDL typedef (alias) definition
     */
    struct TypedefDef {
        String         name;
        TypeRef        targetType;
        SourceLocation location;
    };

    // ==================== Array Definition ====================

    /**
     * @brief Franca IDL named array definition
     */
    struct ArrayDef {
        String         name;
        TypeRef        elementType;
        SourceLocation location;
    };

    // ==================== Map Definition ====================

    /**
     * @brief Franca IDL map definition
     */
    struct MapDef {
        String         name;
        TypeRef        keyType;
        TypeRef        valueType;
        SourceLocation location;
    };

    // ==================== Method Definition ====================

    /**
     * @brief Franca IDL method definition
     * @note [SWS_CM_00800] — request/response and fire-and-forget
     */
    struct MethodDef {
        String                  name;
        Bool                    isFireAndForget = false;
        ::std::vector< Field >  inArgs;
        ::std::vector< Field >  outArgs;
        ::std::vector< Field >  errorArgs;
        SourceLocation          location;
    };

    // ==================== Broadcast Definition ====================

    /**
     * @brief Franca IDL broadcast definition (skeleton → proxy event)
     * @note [SWS_CM_00700] — event communication
     */
    struct BroadcastDef {
        String                  name;
        ::std::vector< Field >  outArgs;
        SourceLocation          location;
    };

    // ==================== Attribute Definition ====================

    /**
     * @brief Franca IDL attribute definition (field access)
     * @note [SWS_CM_00900] — field communication
     */
    struct AttributeDef {
        String         name;
        TypeRef        type;
        Bool           isReadonly = false;
        Bool           isNotify  = false;       ///< Has change notification
        Bool           isNoSubscriptions = false; ///< No subscription support
        SourceLocation location;
    };

    // ==================== Type Collection ====================

    /**
     * @brief Franca IDL typeCollection block
     */
    struct TypeCollection {
        String                          name;
        Version                         version;
        ::std::vector< EnumDef >        enums;
        ::std::vector< StructDef >      structs;
        ::std::vector< TypedefDef >     typedefs;
        ::std::vector< ArrayDef >       arrays;
        ::std::vector< MapDef >         maps;
        SourceLocation                  location;
    };

    // ==================== Interface ====================

    /**
     * @brief Franca IDL interface definition
     * @note Maps to AUTOSAR ServiceInterface [SWS_CM_00002, SWS_CM_00004]
     */
    struct Interface {
        String                          name;
        Version                         version;
        String                          extends;  ///< Base interface (empty if none)
        ::std::vector< MethodDef >      methods;
        ::std::vector< BroadcastDef >   broadcasts;
        ::std::vector< AttributeDef >   attributes;
        ::std::vector< EnumDef >        enums;
        ::std::vector< StructDef >      structs;
        ::std::vector< TypedefDef >     typedefs;
        SourceLocation                  location;
    };

    // ==================== Complete FIDL Model ====================

    /**
     * @brief Complete Franca IDL model (one .fidl file)
     */
    struct FidlModel {
        String                          packageName;
        ::std::vector< String >         imports;
        ::std::vector< TypeCollection > typeCollections;
        ::std::vector< Interface >      interfaces;
        String                          sourceFile;
    };

    // ==================== Generator Configuration ====================

    /**
     * @brief Configuration for code generation
     * @note serviceIdOverride and instanceIdOverride default to 0 (auto-generated from hash).
     *       comConfigPath / serviceDeployPath / slotMappingPath are optional; when provided,
     *       QoS values are resolved from the YAML files with per-element overrides in
     *       service_deploy.yaml taking highest priority, followed by com_config.yaml profiles,
     *       then built-in defaults.  See GENERATOR.md §11.3.6-§11.3.7 for full resolution rules.
     */
    struct GeneratorConfig {
        String outputDir          = "./generated";  ///< Output directory
        String namespacePrefix    = "lap::com";     ///< C++ namespace prefix
        String author             = "Aii";          ///< Author for file headers
        String schemaHashOverride;                  ///< If non-empty, use instead of auto-generated hash
        String versionOverride;                     ///< If non-empty, inject this version into OMG IDL
        String comConfigPath;                       ///< Path to com_config.yaml (optional)
        String serviceDeployPath;                   ///< Path to service_deploy.yaml (optional)
        String slotMappingPath;                     ///< Path to slot_mapping.yaml (optional)
        Bool   generateProxy    = true;
        Bool   generateSkeleton = true;
        Bool   generateTypes    = true;
        Bool   generateDdsIdl   = false;
        Bool   headerOnly       = true;             ///< Generate header-only code
        UInt16 serviceIdOverride  = 0;              ///< 0 = auto-generate from hash
        UInt16 instanceIdOverride = 0;              ///< 0 = auto (derived from service ID)
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CFIDLAST_HPP
