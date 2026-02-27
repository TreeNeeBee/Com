/**
 * @file        CSkeletonGenerator.cpp
 * @author      Aii
 * @brief       AUTOSAR Service Skeleton Code Generator Implementation
 * @date        2026/02/09
 * @details     Generates <Service>Skeleton.hpp deriving from SkeletonBase with:
 *              - SkeletonEvent<T> members for broadcasts
 *              - SkeletonMethod<Output, Args...> members for methods
 *              - SkeletonField<T> members for attributes
 *              - doOfferService / doStopOfferService overrides
 *              - onBindingContextReady hook for context propagation
 * @copyright   Copyright (c) 2026
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.4.5
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CSkeletonGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <iostream>
#include <filesystem>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== CSkeletonGenerator Implementation ====================

    Bool CSkeletonGenerator::Generate( const FidlModel& model,
                                       const GeneratorConfig& config ) {
        auto nsSegments = ExtractNamespaceSegments( model.packageName,
                                                    config.namespacePrefix );

        for ( const auto& iface : model.interfaces ) {
            CCodeWriter w;
            String className = iface.name + "Skeleton";
            String filename  = className + ".hpp";
            String guard = buildGuardMacro( nsSegments, className );

            // File header
            writeFileHeader( w, filename,
                "Auto-generated service skeleton for " + iface.name + " [SWS_CM_00002]",
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
            w.Line( "#include \"SkeletonBase.hpp\"" );
            w.Line( "#include \"Runtime.hpp\"" );
            w.Line( "#include \"BindingManager.hpp\"" );
            w.Line( "#include \"skeleton/SkeletonEvent.hpp\"" );
            w.Line( "#include \"skeleton/SkeletonMethod.hpp\"" );
            w.Line( "#include \"skeleton/SkeletonField.hpp\"" );
            w.Line();
            w.Line( "// ==================== Cross-Module Headers ====================" );
            w.Line( "#include <core/CInstanceSpecifier.hpp>" );
            w.Line( "#include <core/CFuture.hpp>" );
            w.Line();

            // Namespaces
            writeNamespaceOpen( w, nsSegments );
            writeLapComUsings( w, nsSegments );
            w.Line();

            // [SWS_CM_01006] — skeleton inner namespace
            w.Line( "// [SWS_CM_01006] — skeleton inner namespace" );
            w.Line( "namespace skeleton" );
            w.Line( "{" );
            w.Line();

            // Generate per-element classes [SWS_CM_00003, SWS_CM_00007]
            generateEventClasses( w, iface );
            generateMethodClasses( w, iface );
            generateFieldClasses( w, iface );

            // Generate skeleton class
            generateSkeletonClass( w, iface, model, config, nsSegments );

            w.Line();
            w.Line( "} // namespace skeleton" );

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

    void CSkeletonGenerator::generateSkeletonClass(
        CCodeWriter& w, const Interface& iface,
        const FidlModel& model, const GeneratorConfig& config,
        const ::std::vector< String >& nsSegments ) const noexcept {
        String className = iface.name + "Skeleton";

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
        w.Line( " * @brief Service skeleton for " + iface.name + " [SWS_CM_00002]" );
        w.Line( " * @note Auto-generated — non-copyable, move-only" );
        if ( iface.version.IsValid() ) {
            w.Line( " * @version " + iface.version.ToString() );
        }
        w.Line( " */" );
        w.Line( "class " + className + " : public ::lap::com::SkeletonBase {" );

        // ==================== Public Section ====================
        w.Line( "public:" );
        w.Indent();

        // Service identification constants
        w.Line( "// ==================== Service Identification ====================" );
        w.Line( "static constexpr UInt16 kServiceId = 0x"
                + ToHexValue( serviceId )
                + ";" );
        w.Line( "static constexpr const Char* kServiceName = \"" + iface.name + "\";" );
        w.Line( "static constexpr const Char* kSchemaHash  = \"" + schemaHash + "\";" );
        w.Line();

        // Constructor
        w.Line( "/**" );
        w.Line( " * @brief Constructor [SWS_CM_00130]" );
        w.Line( " * @param instanceSpec Instance specifier for the service" );
        w.Line( " * @param mode Method call processing mode" );
        w.Line( " */" );
        w.Line( "explicit " + className + "(" );
        w.Line( "    ::lap::core::InstanceSpecifier instanceSpec," );
        w.Line( "    MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent ) noexcept" );
        w.Line( "    : ::lap::com::SkeletonBase( ::std::move( instanceSpec ), mode )" );
        w.Line( "{}" );
        w.Line();

        // Destructor
        w.Line( "/**" );
        w.Line( " * @brief Destructor — auto-stops offering [SWS_CM_11549]" );
        w.Line( " */" );
        w.Line( "~" + className + "() noexcept override {" );
        w.Indent();
        w.Line( "if ( IsOffered() ) {" );
        w.Indent();
        w.Line( "StopOfferService();" );
        w.Dedent();
        w.Line( "}" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // Move semantics
        w.Line( "// Move-only [SWS_CM_11547, SWS_CM_11545]" );
        w.Line( className + "( " + className + "&& ) noexcept = default;" );
        w.Line( className + "& operator=( " + className + "&& ) noexcept = default;" );
        w.Line();
        w.Line( "// Non-copyable [SWS_CM_11546, SWS_CM_11544]" );
        w.Line( className + "( const " + className + "& ) = delete;" );
        w.Line( className + "& operator=( const " + className + "& ) = delete;" );
        w.Line();

        // ==================== Events (broadcasts) ====================
        if ( !iface.broadcasts.empty() ) {
            w.Line( "// ==================== Events [SWS_CM_99557] ====================" );
            for ( const auto& bc : iface.broadcasts ) {
                String className2 = ToPascalCase( bc.name );
                w.Line( "events::" + className2 + " "
                        + ToCamelCase( bc.name ) + ";" );
            }
            w.Line();
        }

        // ==================== Methods ====================
        if ( !iface.methods.empty() ) {
            w.Line( "// ==================== Methods ====================" );
            for ( const auto& method : iface.methods ) {
                String methodClass = ToPascalCase( method.name );
                String comment = method.isFireAndForget ? "  ///< (fire-and-forget)" : "";
                w.Line( "methods::" + methodClass + " " + ToCamelCase( method.name ) + ";" + comment );
            }
            w.Line();
        }

        // ==================== Fields (attributes) ====================
        if ( !iface.attributes.empty() ) {
            w.Line( "// ==================== Fields [SWS_CM_99558] ====================" );
            for ( const auto& attr : iface.attributes ) {
                String fieldClass = ToPascalCase( attr.name );
                // Determine constructor params from FIDL attribute modifiers
                String hasGetter  = "true";
                String hasSetter  = attr.isReadonly ? "false" : "true";
                String hasNotifier = attr.isNotify ? "true" : "false";
                String comment = attr.isReadonly ? "  ///< @note readonly" : "";
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

        // doOfferService
        generateOfferServiceImpl( w, className );
        w.Line();

        // onBindingContextReady
        generateBindingContextHook( w, iface );

        w.Dedent();
        w.Line( "};" );
        w.Dedent();
    }

    void CSkeletonGenerator::generateOfferServiceImpl(
        CCodeWriter& w, const String& className ) const noexcept {
        w.Line( "/**" );
        w.Line( " * @brief Offer service via Runtime → BindingManager [SWS_CM_00101]" );
        w.Line( " */" );
        w.Line( "Result< void > doOfferService() noexcept override {" );
        w.Indent();

        w.Line( "// Register via BindingManager" );
        w.Line( "auto serviceId  = static_cast< ::lap::core::UInt64 >( kServiceId );" );
        w.Line( "auto instanceId = serviceId & 0xFFFFU;" );
        w.Line();
        w.Line( "auto& bindingMgr = ::lap::com::Runtime::GetBindingManager();" );
        w.Line( "auto* pBinding = bindingMgr.SelectBinding( serviceId, instanceId );" );
        w.Line();
        w.Line( "if ( pBinding == nullptr ) {" );
        w.Indent();
        w.Line( "return Result< void >::FromError(" );
        w.Line( "    MakeErrorCode( ComErrc::kNoBindingAvailable, 0 ) );" );
        w.Dedent();
        w.Line( "}" );
        w.Line();
        w.Line( "auto result = pBinding->OfferService( serviceId, instanceId );" );
        w.Line( "if ( result.HasValue() ) {" );
        w.Indent();
        w.Line( "::lap::com::CBindingContext context;" );
        w.Line( "context.pBinding   = pBinding;" );
        w.Line( "context.serviceId  = serviceId;" );
        w.Line( "context.instanceId = instanceId;" );
        w.Line( "context.elementId  = 0;" );
        w.Line( "setBindingContext( context );" );
        w.Dedent();
        w.Line( "}" );
        w.Line();
        w.Line( "return result;" );

        w.Dedent();
        w.Line( "}" );
        w.Line();

        // doStopOfferService
        w.Line( "/**" );
        w.Line( " * @brief Stop offering service [SWS_CM_00111]" );
        w.Line( " */" );
        w.Line( "void doStopOfferService() noexcept override {" );
        w.Indent();
        w.Line( "const auto& ctx = GetBindingContext();" );
        w.Line( "if ( ctx.IsValid() ) {" );
        w.Indent();
        w.Line( "ctx.pBinding->StopOfferService( ctx.serviceId, ctx.instanceId );" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "setBindingContext( ::lap::com::CBindingContext{} );" );
        w.Dedent();
        w.Line( "}" );
    }

    void CSkeletonGenerator::generateBindingContextHook(
        CCodeWriter& w, const Interface& iface ) const noexcept {
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
            w.Line( "subCtx.elementId = " + ::std::to_string( elementId++ ) + ";" );
            w.Line( "PropagateBindingContext( " + ToCamelCase( bc.name ) + ", subCtx );" );
        }

        // Methods
        elementId = 0x100;
        for ( const auto& method : iface.methods ) {
            w.Line( "subCtx.elementId = 0x"
                    + ToHexValue( elementId ) + ";" );
            w.Line( "PropagateBindingContext( " + ToCamelCase( method.name ) + ", subCtx );" );
            ++elementId;
        }

        // Fields
        elementId = 0x200;
        for ( const auto& attr : iface.attributes ) {
            w.Line( "subCtx.elementId = 0x"
                    + ToHexValue( elementId ) + ";" );
            w.Line( "PropagateBindingContext( " + ToCamelCase( attr.name ) + ", subCtx );" );
            ++elementId;
        }

        w.Dedent();
        w.Line( "}" );
    }

    // ==================== Per-Element Class Generators ====================

    void CSkeletonGenerator::generateEventClasses( CCodeWriter& w,
                                                   const Interface& iface ) const noexcept {
        if ( iface.broadcasts.empty() ) { return; }

        w.Line( "// [SWS_CM_00003] — skeleton events sub-namespace" );
        w.Line( "namespace events" );
        w.Line( "{" );
        w.Line();

        for ( const auto& bc : iface.broadcasts ) {
            String eventType = ToPascalCase( bc.name ) + "Event";
            String className = ToPascalCase( bc.name );
            String baseType  = "::lap::com::SkeletonEvent< " + eventType + " >";

            w.Indent();
            w.Line( "/**" );
            w.Line( " * @brief " + className + " event [SWS_CM_00003]" );
            w.Line( " */" );
            w.Line( "class " + className + " final : public " + baseType + " {" );
            w.Line( "public:" );
            w.Indent();
            w.Line( "using SampleType = " + eventType + ";" );
            w.Line( "using " + baseType + "::SkeletonEvent;" );
            w.Dedent();
            w.Line( "};" );
            w.Line();
            w.Dedent();
        }

        w.Line( "} // namespace events" );
        w.Line();
    }

    void CSkeletonGenerator::generateMethodClasses( CCodeWriter& w,
                                                    const Interface& iface ) const noexcept {
        if ( iface.methods.empty() ) { return; }

        w.Line( "// skeleton methods sub-namespace" );
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
                baseName = "SkeletonFireAndForgetMethod";
                baseType = "::lap::com::" + baseName + "< " + argTypes + " >";
            } else {
                baseName = "SkeletonMethod";
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
            w.Line( " * @brief " + className + " method" );
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

    void CSkeletonGenerator::generateFieldClasses( CCodeWriter& w,
                                                   const Interface& iface ) const noexcept {
        if ( iface.attributes.empty() ) { return; }

        w.Line( "// skeleton fields sub-namespace" );
        w.Line( "namespace fields" );
        w.Line( "{" );
        w.Line();

        for ( const auto& attr : iface.attributes ) {
            String fieldType = resolveCppType( attr.type );
            String className = ToPascalCase( attr.name );
            String baseType  = "::lap::com::SkeletonField< " + fieldType + " >";

            w.Indent();
            w.Line( "/**" );
            w.Line( " * @brief " + className + " field [SWS_CM_00007]" );
            w.Line( " */" );
            w.Line( "class " + className + " final : public " + baseType + " {" );
            w.Line( "public:" );
            w.Indent();
            w.Line( "using " + baseType + "::SkeletonField;" );
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
