/**
 * @file        CDdsIdlGenerator.cpp
 * @author      Aii
 * @brief       OMG IDL + DDS QoS XML generator implementation
 * @date        2026/02/09
 * @details     Converts Franca IDL models to:
 *                - OMG IDL v4.2      (<Interface>.idl)
 *                - DDS QoS XML       (<Interface>_qos.xml)   [always paired]
 *
 *              QoS values are resolved by CQosLoader in three priority levels:
 *                Level 1: per-element overrides in service_deploy.yaml
 *                Level 2: named profiles in com_config.yaml
 *                Level 3: built-in defaults
 *
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §11.3.5–§11.3.7
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * <tr><td>2026/02/09  <td>1.1      <td>Aii     <td>add QoS XML output + CQosLoader integration
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CDdsIdlGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <iostream>
#include <filesystem>
#include <sstream>
#include <cstdlib>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== CDdsIdlGenerator Implementation ====================

    Bool CDdsIdlGenerator::Generate( const FidlModel& model,
                                     const GeneratorConfig& config ) {
        // ==================== Load QoS Configuration ====================
        CQosLoader loader;
        if ( !loader.Load( config.comConfigPath, config.serviceDeployPath ) ) {
            ::std::cerr << "Error: Failed to load QoS configuration files\n";
            return false;
        }

        // Compute schema hash
        String schemaHash = config.schemaHashOverride.empty()
            ? CSchemaHash::Compute( model )
            : config.schemaHashOverride;

        // Extract module path from package name
        ::std::vector< String > modules; {
            ::std::istringstream ss( model.packageName );
            String seg;
            while ( ::std::getline( ss, seg, '.' ) ) {
                modules.push_back( seg );
            }
        }

        for ( const auto& iface : model.interfaces ) {
            CCodeWriter w;
            String filename = iface.name + ".idl";

            // Header comment
            w.Line( "// ============================================================" );
            w.Line( "// Auto-generated OMG IDL v4.2 from " + model.sourceFile );
            w.Line( "// Franca Schema Hash: " + schemaHash );
            String versionStr = !config.versionOverride.empty()
                ? config.versionOverride
                : ( iface.version.IsValid() ? iface.version.ToString() : String() );
            if ( !versionStr.empty() ) {
                w.Line( "// Version: " + versionStr );
            }
            w.Line( "// Generator: lap-sidl-gen v1.0" );
            w.Line( "// DO NOT EDIT — This file is auto-generated" );
            w.Line( "// ============================================================" );
            w.Line();

            // Open nested modules (each level adds one indent)
            for ( const auto& mod : modules ) {
                w.Line( "module " + mod + " {" );
                w.Indent();
            }
            w.Line();

            // Schema hash constant
            w.Line( "const string SCHEMA_HASH = \"" + schemaHash + "\";" );
            // Note: @version annotation is intentionally omitted — OMG IDL 4.2 does not
            // support standalone module-level annotations; version appears in the header.
            w.Line();

            // Generate typeCollections
            for ( const auto& tc : model.typeCollections ) {
                w.Line( "// ==================== Type Collection: " + tc.name + " ====================" );
                w.Line();

                for ( const auto& e : tc.enums ) {
                    generateEnum( w, e );
                }
                for ( const auto& s : tc.structs ) {
                    generateStruct( w, s );
                }
                for ( const auto& t : tc.typedefs ) {
                    generateTypedef( w, t );
                }
            }

            // Generate interface-local types
            for ( const auto& e : iface.enums ) {
                generateEnum( w, e );
            }
            for ( const auto& s : iface.structs ) {
                generateStruct( w, s );
            }

            // Generate broadcast event structs
            for ( const auto& bc : iface.broadcasts ) {
                String structName = ToPascalCase( bc.name ) + "Event";
                w.Line( "struct " + structName + " {" );
                w.Indent();
                for ( const auto& arg : bc.outArgs ) {
                    w.Line( resolveDdsType( arg.type ) + " " + arg.name + ";" );
                }
                w.Dedent();
                w.Line( "};" );
                w.Line();
            }

            // Generate method request/response structs
            for ( const auto& method : iface.methods ) {
                if ( !method.inArgs.empty() ) {
                    String reqName = ToPascalCase( method.name ) + "Request";
                    w.Line( "struct " + reqName + " {" );
                    w.Indent();
                    // DDS routing keys (SWS_CM_11506) and request correlation ID
                    w.Line( "@key unsigned long  serviceId;" );
                    w.Line( "@key unsigned long  instanceId;" );
                    w.Line( "     unsigned long long  requestId;" );
                    for ( const auto& arg : method.inArgs ) {
                        w.Line( resolveDdsType( arg.type ) + " " + arg.name + ";" );
                    }
                    w.Dedent();
                    w.Line( "};" );
                    w.Line();
                }

                if ( !method.outArgs.empty() ) {
                    String respName = ToPascalCase( method.name ) + "Response";
                    w.Line( "struct " + respName + " {" );
                    w.Indent();
                    // Correlation ID to match with the originating request
                    w.Line( "     unsigned long long  requestId;" );
                    for ( const auto& arg : method.outArgs ) {
                        w.Line( resolveDdsType( arg.type ) + " " + arg.name + ";" );
                    }
                    w.Dedent();
                    w.Line( "};" );
                    w.Line();
                }
            }

            // Close modules in reverse order, restoring one indent per level
            for ( auto it = modules.rbegin(); it != modules.rend(); ++it ) {
                w.Dedent();
                w.Line( "}; // " + *it );
            }
            w.Line();

            // Write .idl file
            String outPath = config.outputDir + "/" + filename;
            ::std::filesystem::create_directories( config.outputDir );
            if ( !w.WriteToFile( outPath ) ) {
                ::std::cerr << "Error: Failed to write " << outPath << "\n";
                return false;
            }
            ::std::cout << "  Generated: " << outPath << "\n";

            // Invoke fastddsgen to generate DDS type support C++ code
            {
                String cmd = "fastddsgen -d " + config.outputDir
                           + " -replace -flat-output-dir " + outPath;
                ::std::cout << "  Running: " << cmd << "\n";
                Int32 ret = ::std::system( cmd.c_str() );
                if ( ret != 0 ) {
                    ::std::cerr << "Error: fastddsgen failed (exit code "
                                << ret << ")\n";
                    ::std::cerr << "  Hint: ensure fastddsgen is installed and in PATH\n";
                    return false;
                }
                ::std::cout << "  fastddsgen: DDS type support generated\n";
            }

            // Always generate paired _qos.xml
            if ( !generateQosXml( iface, config, loader ) ) {
                return false;
            }
        }

        return true;
    }

    // ==================== OMG IDL Type Generation ====================

    void CDdsIdlGenerator::generateEnum( CCodeWriter& w,
                                         const EnumDef& enumDef ) const noexcept {
        w.Line( "enum " + enumDef.name + " {" );
        w.Indent();

        for ( ::std::size_t i = 0; i < enumDef.enumerators.size(); ++i ) {
            const auto& e = enumDef.enumerators[i];
            // OMG IDL 4.2 §7.4.12: enumerator values are assigned by position (0, 1, 2, ...).
            // Explicit initializers (e.g. 'OK = 0') are NOT part of the standard and are
            // rejected by fastddsgen.  The Franca IDL ordering is preserved, so values are
            // identical on both sides.
            const bool isLast = ( i + 1 == enumDef.enumerators.size() );
            w.Line( e.name + ( isLast ? "" : "," ) );
        }

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    void CDdsIdlGenerator::generateStruct( CCodeWriter& w,
                                           const StructDef& structDef ) const noexcept {
        String decl = "struct " + structDef.name + " {";
        w.Line( decl );
        w.Indent();

        for ( const auto& field : structDef.fields ) {
            w.Line( resolveDdsType( field.type ) + " " + field.name + ";" );
        }

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    void CDdsIdlGenerator::generateTypedef( CCodeWriter& w,
                                            const TypedefDef& typedefDef ) const noexcept {
        if ( typedefDef.targetType.isArray ) {
            String elemType = resolveDdsType( TypeRef{ typedefDef.targetType.name, false } );
            w.Line( "typedef sequence<" + elemType + "> " + typedefDef.name + ";" );
        } else {
            w.Line( "typedef " + resolveDdsType( typedefDef.targetType )
                    + " " + typedefDef.name + ";" );
        }
        w.Line();
    }

    String CDdsIdlGenerator::resolveDdsType( const TypeRef& ref ) const noexcept {
        String base = IsPrimitiveType( ref.name )
            ? MapFrancaToDds( ref.name )
            : ref.name;

        // Strip 'TypeCollectionName.' prefix — all types share the same OMG IDL module
        // namespace, so only the unqualified name is needed (OMG IDL uses '::' scoping,
        // not '.' scoping, and a dot-prefix from Franca is resolved here).
        const auto dotPos = base.rfind( '.' );
        if ( dotPos != String::npos ) {
            base = base.substr( dotPos + 1 );
        }

        if ( ref.isArray ) {
            return "sequence<" + base + ">";
        }
        return base;
    }

    // ==================== QoS XML Generation ====================

    void CDdsIdlGenerator::writeQosBlock( CCodeWriter&     w,
                                          const QosParams& qos ) const noexcept {
        w.Line( "<qos>" );
        w.Indent();
        w.Line( "<reliability><kind>" + qos.reliability + "</kind></reliability>" );
        w.Line( "<durability><kind>" + qos.durability + "</kind></durability>" );
        w.Line( "<historyQos>" );
        w.Indent();
        w.Line( "<kind>" + qos.history + "</kind>" );
        if ( qos.history == "KEEP_LAST" ) {
            w.Line( "<depth>" + ::std::to_string( qos.historyDepth ) + "</depth>" );
        }
        w.Dedent();
        w.Line( "</historyQos>" );
        if ( qos.deadlineMs > 0 ) {
            const Int32 ns = qos.deadlineMs * 1000000;
            w.Line( "<deadline><period><nanoseconds>"
                    + ::std::to_string( ns )
                    + "</nanoseconds></period></deadline>" );
        }
        w.Dedent();
        w.Line( "</qos>" );
    }

    void CDdsIdlGenerator::writeXmlProfile( CCodeWriter&     w,
                                            const String&    profileName,
                                            const QosParams& qos ) const noexcept {
        w.Line( "<data_writer profile_name=\"" + profileName + "\">" );
        w.Indent();
        writeQosBlock( w, qos );
        w.Dedent();
        w.Line( "</data_writer>" );
        w.Line();

        w.Line( "<data_reader profile_name=\"" + profileName + "\">" );
        w.Indent();
        writeQosBlock( w, qos );
        w.Dedent();
        w.Line( "</data_reader>" );
    }

    Bool CDdsIdlGenerator::generateQosXml( const Interface&       iface,
                                           const GeneratorConfig&  config,
                                           const CQosLoader&       loader ) const {
        CCodeWriter w;

        // XML header
        w.Line( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" );
        w.Line( "<!-- Auto-generated QoS XML for interface: " + iface.name + " -->" );
        w.Line( "<!-- DO NOT EDIT — This file is auto-generated by lap-sidl-gen v1.0 -->" );
        w.Line( "<dds>" );
        w.Indent();
        w.Line( "<profiles xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\">" );
        w.Indent();
        w.Line();

        // Resolve instance ID
        const UInt16 instanceId = loader.ResolveInstanceId(
            iface.name, config.instanceIdOverride );
        w.Line( "<!-- Interface: " + iface.name
                + "  instance_id: 0x"
                + ToHexValue( static_cast< UInt32 >( instanceId ) )
                + " -->" );
        w.Line();

        // ---- Broadcasts (Events) ----
        for ( const auto& bc : iface.broadcasts ) {
            const String profileName = iface.name + "_" + bc.name + "_event";
            const QosParams qos = loader.ResolveQoS(
                iface.name, bc.name, ElementKind::kEvent );
            w.Line( "<!-- Event: " + bc.name + " -->" );
            writeXmlProfile( w, profileName, qos );
            w.Line();
        }

        // ---- Methods ----
        for ( const auto& method : iface.methods ) {
            const ElementKind kind = method.isFireAndForget
                ? ElementKind::kFireAndForget
                : ElementKind::kMethod;
            const String suffix = method.isFireAndForget ? "_ff" : "_method";
            const String profileName = iface.name + "_" + method.name + suffix;
            const QosParams qos = loader.ResolveQoS(
                iface.name, method.name, kind );
            w.Line( "<!-- Method: " + method.name
                    + ( method.isFireAndForget ? " [fire-and-forget]" : "" ) + " -->" );
            writeXmlProfile( w, profileName, qos );
            w.Line();
        }

        // ---- Attributes (Fields) ----
        for ( const auto& attr : iface.attributes ) {
            const String profileName = iface.name + "_" + attr.name + "_field";
            const QosParams qos = loader.ResolveQoS(
                iface.name, attr.name, ElementKind::kField );
            w.Line( "<!-- Field: " + attr.name + " -->" );
            writeXmlProfile( w, profileName, qos );
            w.Line();
        }

        w.Dedent();
        w.Line( "</profiles>" );
        w.Dedent();
        w.Line( "</dds>" );

        // Write file
        const String outPath = config.outputDir + "/" + iface.name + "_qos.xml";
        if ( !w.WriteToFile( outPath ) ) {
            ::std::cerr << "Error: Failed to write " << outPath << "\n";
            return false;
        }
        ::std::cout << "  Generated: " << outPath << "\n";
        return true;
    }

} // namespace generator
} // namespace com
} // namespace lap
