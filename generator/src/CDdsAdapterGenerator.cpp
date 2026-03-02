/**
 * @file        CDdsAdapterGenerator.cpp
 * @author      Aii
 * @brief       DDS Type Adapter Code Generator Implementation
 * @date        2026/02/25
 * @details     Generates <Interface>DdsAdapter.hpp (header-only) per Franca interface.
 *              Each output contains one IDdsTypeAdapter concrete class per event
 *              (broadcast), plus an inline registration function that populates
 *              CDdsTypeRegistry at startup.
 *
 *              Event adapters use fastddsgen-generated PubSubType for native CDR
 *              serialization with per-field app↔DDS type mapping.
 *
 *              Method and field adapters are NOT generated — CDdsMethodManager
 *              handles Phase 1 (ByteBuffer) and Phase 3 (typed tuple) method
 *              paths directly using requestSize/responseSize, without adapters.
 *
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §11.3.8
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/25  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CDdsAdapterGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <filesystem>
#include <iostream>
#include <sstream>

namespace lap
{
namespace com
{
namespace generator
{

    // ========================================================================
    // CDdsAdapterGenerator::Generate
    // ========================================================================

    Bool CDdsAdapterGenerator::Generate( const FidlModel&     model,
                                         const GeneratorConfig& config ) {
        auto nsSegments = ExtractNamespaceSegments( model.packageName,
                                                    config.namespacePrefix );

        // Compute DDS namespace from package name
        // e.g., "lap.examples.helloworld2" → "::lap::examples::helloworld2"
        String ddsNamespace = "::"; {
            ::std::istringstream ss( model.packageName );
            String seg;
            Bool first = true;
            while ( ::std::getline( ss, seg, '.' ) ) {
                if ( !first ) { ddsNamespace += "::"; }
                ddsNamespace += seg;
                first = false;
            }
        }

        // Fully qualified app namespace (e.g., "::helloworld2")
        String appNs = "::"; {
            Bool first = true;
            for ( const auto& seg : nsSegments ) {
                if ( !first ) { appNs += "::"; }
                appNs += seg;
                first = false;
            }
        }

        const String ddsNsAlias = "DdsTypes";

        for ( const auto& iface : model.interfaces ) {
            CCodeWriter w;
            String ifaceName = iface.name;
            String className = ifaceName + "DdsAdapter";
            String filename  = className + ".hpp";
            String guard     = buildGuardMacro( nsSegments, className );

            // ── File header ──────────────────────────────────────────────────
            writeFileHeader( w, filename,
                "Auto-generated DDS type adapter registration for " + ifaceName,
                model.sourceFile, config.author );
            w.Line();

            writeGuardOpen( w, guard );
            w.Line();

            // ── Includes ─────────────────────────────────────────────────────
            w.Line( "// ==================== DDS Binding Headers ====================" );
            w.Line( "#include \"CDdsPayload.hpp\"" );
            w.Line( "#include \"IDdsTypeAdapter.hpp\"" );
            w.Line( "#include \"CDdsTypeRegistry.hpp\"" );
            w.Line();
            w.Line( "// ==================== fastddsgen Type Headers ====================" );
            w.Line( "#include \"" + ifaceName + ".hpp\"" );
            w.Line( "#include \"" + ifaceName + "PubSubTypes.hpp\"" );
            w.Line();
            w.Line( "// ==================== Application Type Headers ====================" );
            w.Line( "#include \"" + ifaceName + "Types.hpp\"" );
            w.Line();
            w.Line( "// ==================== Standard Library Headers ====================" );
            w.Line( "#include <cstring>" );
            w.Line( "#include <cstddef>" );
            w.Line();

            // ── Namespace ────────────────────────────────────────────────────
            writeNamespaceOpen( w, nsSegments );
            w.Line();

            // ── Inner namespace: dds_adapter ─────────────────────────────────
            w.Line( "namespace dds_adapter" );
            w.Line( "{" );
            w.Line();
            w.Line( "// DDS namespace alias for typed event adapters" );
            w.Line( "namespace " + ddsNsAlias + " = " + ddsNamespace + ";" );
            w.Line();

            // ── Adapter classes ───────────────────────────────────────────────
            // Broadcasts / events: element IDs 1, 2, ...
            if ( !iface.broadcasts.empty() ) {
                w.Line( "// ================================================================" );
                w.Line( "// Event Adapters (typed CDR via fastddsgen PubSubType)" );
                w.Line( "// ================================================================" );
                w.Line();

                UInt32 elementId = 1u;
                for ( const auto& bc : iface.broadcasts ) {
                    generateEventAdapter( w, ifaceName,
                                         ToPascalCase( bc.name ), elementId,
                                         bc, model, appNs, ddsNsAlias );
                    ++elementId;
                }
            }

            // Methods and fields use DdsPayload channels directly (Phase 1/3
            // routing in CDdsMethodManager handles both ByteBuffer and typed
            // tuple paths correctly without adapters).  No adapter classes
            // or registration entries are generated for them.

            // ── Registration function ────────────────────────────────────────
            generateRegistrationFunction( w, iface, ifaceName );

            w.Line();
            w.Line( "} // namespace dds_adapter" );
            w.Line();

            writeNamespaceClose( w, nsSegments );
            w.Line();
            writeGuardClose( w, guard );

            // ── Write to disk ────────────────────────────────────────────────
            String outPath = config.outputDir + "/" + filename;
            if ( !w.WriteToFile( outPath ) ) {
                ::std::cerr << "Error: Failed to write " << outPath << "\n";
                return false;
            }
            ::std::cout << "  Generated: " << outPath << "\n";
        }

        return true;
    }

    // ========================================================================
    // generateEventAdapter  — typed CDR via fastddsgen PubSubType
    // ========================================================================

    void CDdsAdapterGenerator::generateEventAdapter(
        CCodeWriter&        w,
        const String&       ifaceName,
        const String&       elemName,
        UInt32              elementId,
        const BroadcastDef& broadcast,
        const FidlModel&    model,
        const String&       appNs,
        const String&       ddsNsAlias ) const noexcept {

        String className      = ifaceName + "_" + elemName + "_EventAdapter";
        String eventStructName = elemName + "Event";
        String ddsType        = ddsNsAlias + "::" + eventStructName;
        String appType        = appNs + "::" + eventStructName;
        String pubSubType     = ddsNsAlias + "::" + eventStructName + "PubSubType";

        w.Line( "/// @brief DDS type adapter for event '" + elemName
                + "' (elementId=0x" + ToHexValue( elementId ) + ")" );
        w.Line( "class " + className + " final" );
        w.Indent();
        w.Line( ": public ::lap::com::binding::IDdsTypeAdapter" );
        w.Dedent();
        w.Line( "{" );
        w.Line( "public:" );
        w.Indent();

        // ---- GetTypeSupport ----
        w.Line( "eprosima::fastdds::dds::TypeSupport GetTypeSupport()"
                " const noexcept override {" );
        w.Indent();
        w.Line( "return eprosima::fastdds::dds::TypeSupport(" );
        w.Indent();
        w.Line( "new " + pubSubType + "() );" );
        w.Dedent();
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // ---- CreateSample ----
        w.Line( "void* CreateSample( const void* pData," );
        w.Line( "                    ::lap::core::Size dataSize ) const override {" );
        w.Indent();
        w.Line( "auto* pDds = new " + ddsType + "();" );
        w.Line( "if ( pData != nullptr && dataSize > 0u ) {" );
        w.Indent();
        w.Line( "const auto& app = *static_cast< const " + appType
                + "* >( pData );" );
        for ( const auto& field : broadcast.outArgs ) {
            emitAppToDdsField( w, field, "app.", "pDds->",
                               appNs, ddsNsAlias, model );
        }
        w.Dedent();
        w.Line( "}" );
        w.Line( "return pDds;" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // ---- ExtractData ----
        w.Line( "const void* ExtractData( const void* pSample )"
                " const noexcept override {" );
        w.Indent();
        w.Line( "if ( pSample == nullptr ) { return nullptr; }" );
        w.Line( "const auto& dds = *static_cast< const " + ddsType
                + "* >( pSample );" );
        w.Line( "try {" );
        w.Indent();
        w.Line( "thread_local " + appType + " appBuf;" );
        for ( const auto& field : broadcast.outArgs ) {
            emitDdsToAppField( w, field, "appBuf.", "dds.",
                               appNs, ddsNsAlias, model );
        }
        w.Line( "return &appBuf;" );
        w.Dedent();
        w.Line( "} catch ( ... ) {" );
        w.Indent();
        w.Line( "return nullptr;" );
        w.Dedent();
        w.Line( "}" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // ---- FreeSample ----
        w.Line( "void FreeSample( void* pSample ) const noexcept override {" );
        w.Indent();
        w.Line( "delete static_cast< " + ddsType + "* >( pSample );" );
        w.Dedent();
        w.Line( "}" );

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    // ========================================================================
    // generateMethodAdapter
    // ========================================================================

    void CDdsAdapterGenerator::generateMethodAdapter( CCodeWriter&  w,
                                                      const String& ifaceName,
                                                      const String& elemName,
                                                      UInt32        elementId ) const noexcept {
        String className = ifaceName + "_" + elemName + "_MethodAdapter";

        w.Line( "/// @brief DDS type adapter for method '" + elemName
                + "' (elementId=0x" + ToHexValue( elementId ) + ")" );
        w.Line( "class " + className + " final" );
        w.Indent();
        w.Line( ": public ::lap::com::binding::IDdsTypeAdapter" );
        w.Dedent();
        w.Line( "{" );
        w.Line( "public:" );
        w.Indent();
        generateAdapterBody( w, className );
        w.Line();

        // Method-specific: SetRequestId / GetRequestId
        w.Line( "void SetRequestId( void* pSample," );
        w.Line( "                   ::lap::core::UInt64 requestId ) const noexcept override {" );
        w.Indent();
        w.Line( "if ( pSample != nullptr ) {" );
        w.Indent();
        w.Line( "static_cast< ::lap::com::binding::DdsPayload* >( pSample" );
        w.Line( "    )->request_id( requestId );" );
        w.Dedent();
        w.Line( "}" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        w.Line( "::lap::core::UInt64 GetRequestId(" );
        w.Line( "    const void* pSample ) const noexcept override {" );
        w.Indent();
        w.Line( "if ( pSample == nullptr ) { return 0u; }" );
        w.Line( "return static_cast< const ::lap::com::binding::DdsPayload* >(" );
        w.Line( "    pSample )->request_id();" );
        w.Dedent();
        w.Line( "}" );

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    // ========================================================================
    // generateFieldAdapter
    // ========================================================================

    void CDdsAdapterGenerator::generateFieldAdapter( CCodeWriter&  w,
                                                     const String& ifaceName,
                                                     const String& elemName,
                                                     UInt32        elementId ) const noexcept {
        String className = ifaceName + "_" + elemName + "_FieldAdapter";

        w.Line( "/// @brief DDS type adapter for field '" + elemName
                + "' (elementId=0x" + ToHexValue( elementId ) + ")" );
        w.Line( "class " + className + " final" );
        w.Indent();
        w.Line( ": public ::lap::com::binding::IDdsTypeAdapter" );
        w.Dedent();
        w.Line( "{" );
        w.Line( "public:" );
        w.Indent();
        generateAdapterBody( w, className );
        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    // ========================================================================
    // generateAdapterBody  — shared by event, method, field adapters
    // ========================================================================

    void CDdsAdapterGenerator::generateAdapterBody( CCodeWriter&  w,
                                                    const String& className ) const noexcept {
        // ---- GetTypeSupport ----
        w.Line( "eprosima::fastdds::dds::TypeSupport GetTypeSupport()"
                " const noexcept override {" );
        w.Indent();
        w.Line( "return eprosima::fastdds::dds::TypeSupport(" );
        w.Indent();
        w.Line( "new ::lap::com::binding::DdsPayloadPubSubType() );" );
        w.Dedent();
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // ---- CreateSample ----
        w.Line( "void* CreateSample( const void* pData," );
        w.Line( "                    ::lap::core::Size dataSize ) const override {" );
        w.Indent();
        w.Line( "auto* pSample = new ::lap::com::binding::DdsPayload();" );
        w.Line( "if ( pData != nullptr && dataSize > 0u ) {" );
        w.Indent();
        w.Line( "pSample->m_data.resize( dataSize );" );
        w.Line( "::std::memcpy( pSample->m_data.data(), pData, dataSize );" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "return pSample;" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // ---- ExtractData ----
        w.Line( "const void* ExtractData( const void* pSample )"
                " const noexcept override {" );
        w.Indent();
        w.Line( "if ( pSample == nullptr ) { return nullptr; }" );
        w.Line( "return static_cast< const ::lap::com::binding::DdsPayload* >(" );
        w.Line( "    pSample )->m_data.data();" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // ---- FreeSample ----
        w.Line( "void FreeSample( void* pSample ) const noexcept override {" );
        w.Indent();
        w.Line( "delete static_cast< ::lap::com::binding::DdsPayload* >( pSample );" );
        w.Dedent();
        w.Line( "}" );

        static_cast< void >( className );   // parameter reserved for future type-name use
    }

    // ========================================================================
    // generateRegistrationFunction
    // ========================================================================

    void CDdsAdapterGenerator::generateRegistrationFunction(
        CCodeWriter&     w,
        const Interface& iface,
        const String&    ifaceName ) const noexcept {

        // ---- Doxygen ----
        w.Line( "// ================================================================" );
        w.Line( "// Registration Function" );
        w.Line( "// ================================================================" );
        w.Line();
        w.Line( "/**" );
        w.Line( " * @brief  Register all DDS adapters for " + ifaceName
                + " with CDdsTypeRegistry" );
        w.Line( " * @param  serviceId  32-bit service ID (matches kServiceId in Proxy/Skeleton)" );
        w.Line( " * @note   Call once at application startup before any DDS communication." );
        w.Line( " *         The static adapter instances are zero-overhead singletons." );
        w.Line( " */" );
        w.Line( "inline void Register" + ifaceName + "DdsAdapters(" );
        w.Indent();
        w.Line( "::lap::core::UInt64 serviceId ) noexcept" );
        w.Dedent();
        w.Line( "{" );
        w.Indent();

        auto& registry = w;   // alias for readability below

        // ── Events ────────────────────────────────────────────────────────
        UInt32 elementId = 1u;
        for ( const auto& bc : iface.broadcasts ) {
            String adapterClass = ifaceName + "_" + ToPascalCase( bc.name )
                                  + "_EventAdapter";
            String varName      = "s_" + ToCamelCase( bc.name ) + "EventAdapter";
            w.Line( "static " + adapterClass + " " + varName + ";" );
            w.Line( "::lap::com::binding::CDdsTypeRegistry::Instance()" );
            w.Indent();
            w.Line( ".RegisterAdapter( serviceId, "
                    + ::std::to_string( elementId ) + "u, &" + varName + " );" );
            w.Dedent();
            w.Line();
            ++elementId;
        }
        static_cast< void >( registry );

        // Method and field adapters are intentionally NOT registered.
        // CDdsMethodManager handles Phase 1 (ByteBuffer) and Phase 3 (typed)
        // method paths without adapters, using requestSize/responseSize to
        // distinguish the two paths.

        w.Dedent();
        w.Line( "}" );
    }

    // ========================================================================
    // classifyFieldType
    // ========================================================================

    CDdsAdapterGenerator::FieldCategory CDdsAdapterGenerator::classifyFieldType(
        const TypeRef&   typeRef,
        const FidlModel& model ) const noexcept {

        const auto& name = typeRef.name;

        // Built-in primitive types
        if ( name == "Boolean" || name == "Char"
             || name == "UInt8"  || name == "UInt16" || name == "UInt32" || name == "UInt64"
             || name == "Int8"   || name == "Int16"  || name == "Int32"  || name == "Int64"
             || name == "Float"  || name == "Double" ) {
            return FieldCategory::kPrimitive;
        }
        if ( name == "String" ) { return FieldCategory::kString; }
        if ( name == "ByteArray" ) { return FieldCategory::kByteArray; }

        // Qualified type: "TypeCollection.TypeName"
        auto dotPos = name.find( '.' );
        if ( dotPos != String::npos ) {
            String collName = name.substr( 0, dotPos );
            String typeName = name.substr( dotPos + 1 );
            for ( const auto& tc : model.typeCollections ) {
                if ( tc.name == collName ) {
                    for ( const auto& e : tc.enums ) {
                        if ( e.name == typeName ) { return FieldCategory::kEnum; }
                    }
                    for ( const auto& s : tc.structs ) {
                        if ( s.name == typeName ) { return FieldCategory::kStruct; }
                    }
                }
            }
        }

        // Unqualified: check ALL type collections (e.g. struct sub-field
        // referencing an enum in the same type collection by short name)
        for ( const auto& tc : model.typeCollections ) {
            for ( const auto& e : tc.enums ) {
                if ( e.name == name ) { return FieldCategory::kEnum; }
            }
            for ( const auto& s : tc.structs ) {
                if ( s.name == name ) { return FieldCategory::kStruct; }
            }
        }

        // Unqualified: check interface-local enums/structs
        for ( const auto& iface : model.interfaces ) {
            for ( const auto& e : iface.enums ) {
                if ( e.name == name ) { return FieldCategory::kEnum; }
            }
            for ( const auto& s : iface.structs ) {
                if ( s.name == name ) { return FieldCategory::kStruct; }
            }
        }

        return FieldCategory::kPrimitive;   // safe default for unknown types
    }

    // ========================================================================
    // findStructDef
    // ========================================================================

    const StructDef* CDdsAdapterGenerator::findStructDef(
        const String&    qualifiedName,
        const FidlModel& model ) const noexcept {

        auto dotPos = qualifiedName.find( '.' );
        if ( dotPos != String::npos ) {
            String collName = qualifiedName.substr( 0, dotPos );
            String typeName = qualifiedName.substr( dotPos + 1 );
            for ( const auto& tc : model.typeCollections ) {
                if ( tc.name == collName ) {
                    for ( const auto& s : tc.structs ) {
                        if ( s.name == typeName ) { return &s; }
                    }
                }
            }
        }
        // Also check interface-local structs (unqualified name)
        for ( const auto& iface : model.interfaces ) {
            for ( const auto& s : iface.structs ) {
                if ( s.name == qualifiedName ) { return &s; }
            }
        }
        return nullptr;
    }

    // ========================================================================
    // ddsTypeName — strip typeCollection prefix
    // ========================================================================

    String CDdsAdapterGenerator::ddsTypeName(
        const String& qualifiedName ) const noexcept {

        auto dotPos = qualifiedName.find( '.' );
        if ( dotPos == String::npos ) { return qualifiedName; }
        return qualifiedName.substr( dotPos + 1 );
    }

    // ========================================================================
    // resolveFullAppTypeName — resolve app-side fully-qualified C++ type name
    //   For qualified names  ("SensorTypes.AlertLevel") → "SensorTypes::AlertLevel"
    //   For unqualified names ("AlertLevel")            → look up in type collections
    //   and interfaces, returning e.g. "SensorTypes::AlertLevel" if found in a TC.
    // ========================================================================

    String CDdsAdapterGenerator::resolveFullAppTypeName(
        const String&    typeName,
        const FidlModel& model ) const noexcept {

        // Already qualified?
        auto dotPos = typeName.find( '.' );
        if ( dotPos != String::npos ) {
            // Convert "SensorTypes.AlertLevel" → "SensorTypes::AlertLevel"
            TypeRef ref{ typeName, false };
            return ref.ToCppName();
        }

        // Unqualified: search type collections first
        for ( const auto& tc : model.typeCollections ) {
            for ( const auto& e : tc.enums ) {
                if ( e.name == typeName ) {
                    return tc.name + "::" + typeName;
                }
            }
            for ( const auto& s : tc.structs ) {
                if ( s.name == typeName ) {
                    return tc.name + "::" + typeName;
                }
            }
        }

        // Interface-local — no extra prefix needed
        return typeName;
    }

    // ========================================================================
    // emitAppToDdsField — generate one field's app → DDS mapping line(s)
    // ========================================================================

    void CDdsAdapterGenerator::emitAppToDdsField(
        CCodeWriter&     w,
        const Field&     field,
        const String&    appPrefix,
        const String&    ddsPrefix,
        const String&    appNs,
        const String&    ddsNsAlias,
        const FidlModel& model ) const noexcept {

        auto  cat   = classifyFieldType( field.type, model );
        const auto& fname = field.name;

        switch ( cat ) {
        case FieldCategory::kPrimitive:
        case FieldCategory::kString:
        case FieldCategory::kByteArray:
            // Direct assignment via DDS setter
            w.Line( ddsPrefix + fname + "( " + appPrefix + fname + " );" );
            break;

        case FieldCategory::kEnum: {
            String ddsEnumType = ddsNsAlias + "::" + ddsTypeName( field.type.name );
            w.Line( ddsPrefix + fname
                    + "( static_cast< " + ddsEnumType
                    + " >( static_cast< ::std::int32_t >( "
                    + appPrefix + fname + " ) ) );" );
            break;
        }

        case FieldCategory::kStruct: {
            const StructDef* sd = findStructDef( field.type.name, model );
            if ( sd == nullptr ) {
                w.Line( "// WARNING: struct " + field.type.name + " not resolved" );
                break;
            }
            String ddsStructType = ddsNsAlias + "::" + ddsTypeName( field.type.name );
            String tmpVar = "dds_" + fname;
            w.Line( "{" );
            w.Indent();
            w.Line( ddsStructType + " " + tmpVar + ";" );
            for ( const auto& sf : sd->fields ) {
                emitAppToDdsField( w, sf, appPrefix + fname + ".",
                                   tmpVar + ".", appNs, ddsNsAlias, model );
            }
            w.Line( ddsPrefix + fname + "( ::std::move( " + tmpVar + " ) );" );
            w.Dedent();
            w.Line( "}" );
            break;
        }
        }
    }

    // ========================================================================
    // emitDdsToAppField — generate one field's DDS → app mapping line(s)
    // ========================================================================

    void CDdsAdapterGenerator::emitDdsToAppField(
        CCodeWriter&     w,
        const Field&     field,
        const String&    appPrefix,
        const String&    ddsPrefix,
        const String&    appNs,
        const String&    ddsNsAlias,
        const FidlModel& model ) const noexcept {

        auto  cat   = classifyFieldType( field.type, model );
        const auto& fname = field.name;

        switch ( cat ) {
        case FieldCategory::kPrimitive:
        case FieldCategory::kString:
        case FieldCategory::kByteArray:
            // Direct assignment from DDS getter
            w.Line( appPrefix + fname + " = " + ddsPrefix + fname + "();" );
            break;

        case FieldCategory::kEnum: {
            String appEnumType = appNs + "::" + resolveFullAppTypeName( field.type.name, model );
            w.Line( appPrefix + fname + " = static_cast< " + appEnumType
                    + " >( static_cast< ::std::int32_t >( "
                    + ddsPrefix + fname + "() ) );" );
            break;
        }

        case FieldCategory::kStruct: {
            const StructDef* sd = findStructDef( field.type.name, model );
            if ( sd == nullptr ) {
                w.Line( "// WARNING: struct " + field.type.name + " not resolved" );
                break;
            }
            String tmpVar = "ddsRef_" + fname;
            w.Line( "{" );
            w.Indent();
            w.Line( "const auto& " + tmpVar + " = " + ddsPrefix + fname + "();" );
            for ( const auto& sf : sd->fields ) {
                emitDdsToAppField( w, sf, appPrefix + fname + ".",
                                   tmpVar + ".", appNs, ddsNsAlias, model );
            }
            w.Dedent();
            w.Line( "}" );
            break;
        }
        }
    }

} // namespace generator
} // namespace com
} // namespace lap
