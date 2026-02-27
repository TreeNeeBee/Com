/**
 * @file        CProxyGenerator.cpp
 * @author      Aii
 * @brief       AUTOSAR Service Proxy Code Generator Implementation
 * @date        2026/02/09
 * @details     Generates <Service>Proxy.hpp deriving from ProxyBase with:
 *              - ProxyEvent<T> members for broadcasts
 *              - ProxyMethod<Output, Args...> members for methods
 *              - ProxyField<T> members for attributes
 *              - Named constructor Create(HandleType)
 *              - onBindingContextReady hook for context propagation
 * @copyright   Copyright (c) 2026
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.3.8
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CProxyGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <iostream>
#include <filesystem>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== CProxyGenerator Implementation ====================

    Bool CProxyGenerator::Generate( const FidlModel& model,
                                    const GeneratorConfig& config ) {
        auto nsSegments = ExtractNamespaceSegments( model.packageName,
                                                    config.namespacePrefix );

        for ( const auto& iface : model.interfaces ) {
            CCodeWriter w;
            String className = iface.name + "Proxy";
            String filename  = className + ".hpp";
            String guard = buildGuardMacro( nsSegments, className );

            // File header
            writeFileHeader( w, filename,
                "Auto-generated service proxy for " + iface.name + " [SWS_CM_00004]",
                model.sourceFile, config.author );
            w.Line();

            writeGuardOpen( w, guard );
            w.Line();

            // Includes
            w.Line( "// ==================== Project-Internal Headers ====================" );
            w.Line( "#include \"" + iface.name + "Types.hpp\"" );
            w.Line();
            w.Line( "// ==================== Runtime Headers ====================" );
            w.Line( "#include \"ComTypes.hpp\"" );
            w.Line( "#include \"ProxyBase.hpp\"" );
            w.Line( "#include \"ServiceHandleType.hpp\"" );
            w.Line( "#include \"Runtime.hpp\"" );
            w.Line( "#include \"BindingManager.hpp\"" );
            w.Line( "#include \"proxy/ProxyEvent.hpp\"" );
            w.Line( "#include \"proxy/ProxyMethod.hpp\"" );
            w.Line( "#include \"proxy/ProxyField.hpp\"" );
            w.Line();

            // Namespaces
            writeNamespaceOpen( w, nsSegments );
            writeLapComUsings( w, nsSegments );
            w.Line();

            // [SWS_CM_01007] — proxy inner namespace
            w.Line( "// [SWS_CM_01007] — proxy inner namespace" );
            w.Line( "namespace proxy" );
            w.Line( "{" );
            w.Line();

            // Generate per-element final classes [SWS_CM_00005, SWS_CM_00191, SWS_CM_00007]
            generateEventClasses( w, iface );
            generateMethodClasses( w, iface );
            generateFieldClasses( w, iface );

            // Generate proxy class
            generateProxyClass( w, iface, model, config, nsSegments );

            w.Line();
            w.Line( "} // namespace proxy" );

            w.Line();
            writeNamespaceClose( w, nsSegments );
            w.Line();
            writeGuardClose( w, guard );

            // Write to file
            String outPath = config.outputDir + "/" + filename;
            ::std::filesystem::create_directories( config.outputDir );
            if ( !w.WriteToFile( outPath ) ) {
                ::std::cerr << "Error: Failed to write " << outPath << "\n";
                return false;
            }
            ::std::cout << "  Generated: " << outPath << "\n";
        }

        return true;
    }

    void CProxyGenerator::generateProxyClass( CCodeWriter& w,
                                              const Interface& iface,
                                              const FidlModel& model,
                                              const GeneratorConfig& config,
                                              const ::std::vector< String >& nsSegments ) const noexcept {
        String className = iface.name + "Proxy";

        // Compute service ID
        UInt16 serviceId = config.serviceIdOverride;
        if ( serviceId == 0 ) {
            serviceId = CSchemaHash::GenerateServiceId(
                model.packageName + "." + iface.name );
        }

        // Compute schema hash
        String schemaHash = config.schemaHashOverride.empty()
            ? CSchemaHash::Compute( model )
            : config.schemaHashOverride;

        w.Indent();

        // Class documentation
        w.Line( "/**" );
        w.Line( " * @brief Service proxy for " + iface.name + " [SWS_CM_00004]" );
        w.Line( " * @note Auto-generated — non-copyable, move-only, named constructor" );
        if ( iface.version.IsValid() ) {
            w.Line( " * @version " + iface.version.ToString() );
        }
        w.Line( " */" );
        w.Line( "class " + className + " final : public ::lap::com::ProxyBase {" );

        // ==================== Public Section ====================
        w.Line( "public:" );
        w.Indent();

        // Type aliases
        w.Line( "using HandleType = ::lap::com::ServiceHandleType< " + className + " >;" );
        w.Line();

        // Service identification constants
        w.Line( "// ==================== Service Identification ====================" );
        w.Line( "static constexpr UInt16 kServiceId = 0x"
                + ToHexValue( serviceId )
                + ";" );
        w.Line( "static constexpr const Char* kServiceName = \"" + iface.name + "\";" );
        w.Line( "static constexpr const Char* kSchemaHash  = \"" + schemaHash + "\";" );
        if ( iface.version.IsValid() ) {
            w.Line( "static constexpr UInt32 kVersionMajor = "
                    + ::std::to_string( iface.version.major ) + ";" );
            w.Line( "static constexpr UInt32 kVersionMinor = "
                    + ::std::to_string( iface.version.minor ) + ";" );
        }
        w.Line();

        // Named constructor
        generateCreateMethod( w, className );
        w.Line();

        // Destructor
        w.Line( "~" + className + "() noexcept override = default;" );
        w.Line();

        // Move semantics
        w.Line( "// Move-only [SWS_CM_11554, SWS_CM_11552]" );
        w.Line( className + "( " + className + "&& ) noexcept = default;" );
        w.Line( className + "& operator=( " + className + "&& ) noexcept = default;" );
        w.Line();
        w.Line( "// Non-copyable [SWS_CM_11553, SWS_CM_11551]" );
        w.Line( className + "( const " + className + "& ) = delete;" );
        w.Line( className + "& operator=( const " + className + "& ) = delete;" );
        w.Line();

        // Handle accessor
        w.Line( "/**" );
        w.Line( " * @brief Get the handle used to create this proxy [SWS_CM_10383]" );
        w.Line( " */" );
        w.Line( "HandleType GetHandle() const noexcept { return m_handle; }" );
        w.Line();

        // ==================== Events (broadcasts) ====================
        if ( !iface.broadcasts.empty() ) {
            w.Line( "// ==================== Events [SWS_CM_99445] ====================" );
            UInt32 eventId = 1;
            for ( const auto& bc : iface.broadcasts ) {
                String className2 = ToPascalCase( bc.name );
                w.Line( "events::" + className2 + " "
                        + ToCamelCase( bc.name ) + ";  ///< Event ID " + ::std::to_string( eventId++ ) );
            }
            w.Line();
        }

        // ==================== Methods ====================
        if ( !iface.methods.empty() ) {
            w.Line( "// ==================== Methods [SWS_CM_99447] ====================" );
            for ( const auto& method : iface.methods ) {
                String methodClass = ToPascalCase( method.name );
                String comment = method.isFireAndForget ? "  ///< (fire-and-forget)" : "";
                w.Line( "methods::" + methodClass + " " + ToCamelCase( method.name ) + ";" + comment );
            }
            w.Line();
        }

        // ==================== Fields (attributes) ====================
        if ( !iface.attributes.empty() ) {
            w.Line( "// ==================== Fields [SWS_CM_99446] ====================" );
            for ( const auto& attr : iface.attributes ) {
                String fieldClass = ToPascalCase( attr.name );
                // Determine constructor params from FIDL attribute modifiers
                String hasGetter  = "true";
                String hasSetter  = attr.isReadonly ? "false" : "true";
                String hasNotifier = attr.isNotify ? "true" : "false";
                String comment = attr.isReadonly ? " ///< @note readonly" : "";
                w.Line( "fields::" + fieldClass + " "
                        + ToCamelCase( attr.name )
                        + "{ " + hasGetter + ", " + hasSetter + ", " + hasNotifier + " };"
                        + comment );
            }
            w.Line();
        }

        w.Dedent();

        // ==================== Protected Section ====================
        w.Line( "protected:" );
        w.Indent();

        // Protected constructor
        w.Line( "/**" );
        w.Line( " * @brief Protected constructor (use Create() factory)" );
        w.Line( " */" );
        w.Line( "explicit " + className + "( const HandleType& handle ) noexcept" );
        w.Line( "    : ::lap::com::ProxyBase()" );
        w.Line( "    , m_handle( handle )" );
        w.Line( "{}" );
        w.Line();

        // onBindingContextReady hook
        generateBindingContextHook( w, iface );

        w.Dedent();

        // ==================== Private Section ====================
        w.Line( "private:" );
        w.Indent();
        w.Line( "HandleType m_handle;" );
        w.Dedent();

        w.Line( "};" );
        w.Dedent();
    }

    void CProxyGenerator::generateCreateMethod( CCodeWriter& w,
                                                const String& className ) const noexcept {
        w.Line( "/**" );
        w.Line( " * @brief Named constructor — create proxy from service handle [SWS_CM_10438]" );
        w.Line( " * @param handle Service handle obtained from FindService" );
        w.Line( " * @return Result containing proxy instance or error" );
        w.Line( " */" );
        w.Line( "static Result< " + className + " > Create( const HandleType& handle ) noexcept {" );
        w.Indent();

        w.Line( "if ( !handle.IsValid() ) {" );
        w.Indent();
        w.Line( "return Result< " + className + " >::FromError(" );
        w.Line( "    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        w.Line( className + " proxy( handle );" );
        w.Line( "proxy.NotifyServiceStateChange( ServiceState::kAvailable );" );
        w.Line();

        w.Line( "// Acquire binding and build context" );
        w.Line( "auto serviceId  = static_cast< ::lap::core::UInt64 >( kServiceId );" );
        w.Line( "auto instanceId = static_cast< ::lap::core::UInt64 >( handle.GetInstanceId() );" );
        w.Line( "auto& bindingMgr = ::lap::com::Runtime::GetBindingManager();" );
        w.Line( "auto* pBinding = bindingMgr.SelectBinding( serviceId, instanceId );" );
        w.Line();
        w.Line( "::lap::com::CBindingContext context;" );
        w.Line( "context.pBinding   = pBinding;" );
        w.Line( "context.serviceId  = serviceId;" );
        w.Line( "context.instanceId = instanceId;" );
        w.Line( "context.elementId  = 0;" );
        w.Line();
        w.Line( "proxy.setBindingContext( context );" );
        w.Line();
        w.Line( "return Result< " + className + " >::FromValue( ::std::move( proxy ) );" );

        w.Dedent();
        w.Line( "}" );
    }

    void CProxyGenerator::generateBindingContextHook( CCodeWriter& w,
                                                      const Interface& iface ) const noexcept {
        w.Line( "/**" );
        w.Line( " * @brief Propagate binding context to all sub-components" );
        w.Line( " */" );
        w.Line( "void onBindingContextReady( const ::lap::com::CBindingContext& context ) noexcept override {" );
        w.Indent();
        w.Line( "::lap::com::CBindingContext subCtx = context;" );
        w.Line();

        // Events
        UInt32 elementId = 1;
        for ( const auto& bc : iface.broadcasts ) {
            w.Line( "subCtx.elementId = " + ::std::to_string( elementId++ ) + ";  // " + bc.name );
            w.Line( "PropagateBindingContext( " + ToCamelCase( bc.name ) + ", subCtx );" );
        }

        // Methods (element IDs starting at 0x100)
        elementId = 0x100;
        for ( const auto& method : iface.methods ) {
            w.Line( "subCtx.elementId = 0x" + ToHexValue( elementId )
                    + ";  // " + method.name );
            w.Line( "PropagateBindingContext( " + ToCamelCase( method.name ) + ", subCtx );" );
            ++elementId;
        }

        // Fields (element IDs starting at 0x200)
        elementId = 0x200;
        for ( const auto& attr : iface.attributes ) {
            w.Line( "subCtx.elementId = 0x" + ToHexValue( elementId )
                    + ";  // " + attr.name );
            w.Line( "PropagateBindingContext( " + ToCamelCase( attr.name ) + ", subCtx );" );
            ++elementId;
        }

        w.Dedent();
        w.Line( "}" );
    }

    // ==================== Per-Element Class Generators ====================

    void CProxyGenerator::generateEventClasses( CCodeWriter& w,
                                                const Interface& iface ) const noexcept {
        if ( iface.broadcasts.empty() ) { return; }

        w.Line( "// [SWS_CM_98447] — events sub-namespace" );
        w.Line( "namespace events" );
        w.Line( "{" );
        w.Line();

        for ( const auto& bc : iface.broadcasts ) {
            String eventType = ToPascalCase( bc.name ) + "Event";
            String className = ToPascalCase( bc.name );
            String baseType  = "::lap::com::ProxyEvent< " + eventType + " >";

            w.Indent();
            w.Line( "/**" );
            w.Line( " * @brief " + className + " event [SWS_CM_00005]" );
            w.Line( " */" );
            w.Line( "class " + className + " final : public " + baseType + " {" );
            w.Line( "public:" );
            w.Indent();
            w.Line( "using SampleType = " + eventType + ";" );
            w.Line( "using " + baseType + "::ProxyEvent;" );
            w.Dedent();
            w.Line( "};" );
            w.Line();
            w.Dedent();
        }

        w.Line( "} // namespace events" );
        w.Line();
    }

    void CProxyGenerator::generateMethodClasses( CCodeWriter& w,
                                                 const Interface& iface ) const noexcept {
        if ( iface.methods.empty() ) { return; }

        w.Line( "// [SWS_CM_01015] — methods sub-namespace" );
        w.Line( "namespace methods" );
        w.Line( "{" );
        w.Line();

        for ( const auto& method : iface.methods ) {
            String className = ToPascalCase( method.name );

            // Build argument types
            String argTypes;
            for ( ::std::size_t i = 0; i < method.inArgs.size(); ++i ) {
                if ( i > 0 ) { argTypes += ", "; }
                argTypes += resolveCppType( method.inArgs[i].type );
            }

            String baseType;
            String baseName;
            if ( method.isFireAndForget ) {
                baseName = "ProxyFireAndForgetMethod";
                baseType = "::lap::com::" + baseName + "< " + argTypes + " >";
            } else {
                baseName = "ProxyMethod";
                String outputType;
                if ( method.outArgs.empty() ) {
                    outputType = "void";
                } else if ( method.outArgs.size() == 1 ) {
                    outputType = resolveCppType( method.outArgs[0].type );
                } else {
                    outputType = ToPascalCase( method.name ) + "Output";
                }
                baseType = "::lap::com::" + baseName + "< " + outputType
                    + ( argTypes.empty() ? "" : ", " + argTypes ) + " >";
            }

            w.Indent();
            w.Line( "/**" );
            w.Line( " * @brief " + className + " method [SWS_CM_00191]" );
            w.Line( " */" );
            w.Line( "class " + className + " final : public " + baseType + " {" );
            w.Line( "public:" );
            w.Indent();
            w.Line( "using " + baseType + "::" + baseName + ";" );
            w.Dedent();
            w.Line( "};" );
            w.Line();
            w.Dedent();
        }

        w.Line( "} // namespace methods" );
        w.Line();
    }

    void CProxyGenerator::generateFieldClasses( CCodeWriter& w,
                                                const Interface& iface ) const noexcept {
        if ( iface.attributes.empty() ) { return; }

        w.Line( "// [SWS_CM_98444] — fields sub-namespace" );
        w.Line( "namespace fields" );
        w.Line( "{" );
        w.Line();

        for ( const auto& attr : iface.attributes ) {
            String fieldType = resolveCppType( attr.type );
            String className = ToPascalCase( attr.name );
            String baseType  = "::lap::com::ProxyField< " + fieldType + " >";

            w.Indent();
            w.Line( "/**" );
            w.Line( " * @brief " + className + " field [SWS_CM_00007]" );
            w.Line( " */" );
            w.Line( "class " + className + " final : public " + baseType + " {" );
            w.Line( "public:" );
            w.Indent();
            w.Line( "using " + baseType + "::ProxyField;" );
            w.Dedent();
            w.Line( "};" );
            w.Line();
            w.Dedent();
        }

        w.Line( "} // namespace fields" );
        w.Line();
    }

} // namespace generator
} // namespace com
} // namespace lap
