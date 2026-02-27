/**
 * @file        CTypesGenerator.cpp
 * @author      Aii
 * @brief       C++ Types Code Generator Implementation
 * @date        2026/02/09
 * @details     Generates <Service>Types.hpp containing:
 *              - typeCollection enums → enum class with k-prefix values
 *              - typeCollection structs → POD structs with project types
 *              - typeCollection typedefs → using aliases
 *              - Method output structs (multi-output methods)
 *              - Broadcast event structs
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CTypesGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <iostream>
#include <filesystem>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== CTypesGenerator Implementation ====================

    Bool CTypesGenerator::Generate( const FidlModel& model,
                                    const GeneratorConfig& config ) {
        auto nsSegments = ExtractNamespaceSegments( model.packageName,
                                                    config.namespacePrefix );

        for ( const auto& iface : model.interfaces ) {
            CCodeWriter w;

            String filename = iface.name + "Types.hpp";
            String guard = buildGuardMacro( nsSegments, iface.name + "Types" );

            // File header
            writeFileHeader( w, filename,
                "Auto-generated types for " + iface.name + " service",
                model.sourceFile, config.author );
            w.Line();

            // Include guard
            writeGuardOpen( w, guard );
            w.Line();

            // Includes
            w.Line( "// ==================== Cross-Module Headers ====================" );
            w.Line( "#include <com/ComTypes.hpp>" );
            w.Line( "#include <core/CFuture.hpp>" );
            w.Line();
            w.Line( "// ==================== Serialization Headers ====================" );
            w.Line( "#include \"serialization/CSerializationTraits.hpp\"" );
            w.Line();
            w.Line( "// ==================== Standard Library Headers ====================" );
            w.Line( "#include <vector>" );
            w.Line();

            // Namespaces
            writeNamespaceOpen( w, nsSegments );
            writeLapComUsings( w, nsSegments );
            w.Line();

            // Generate typeCollections
            for ( const auto& tc : model.typeCollections ) {
                generateTypeCollection( w, tc );
            }

            // Generate interface-local types
            if ( !iface.enums.empty() || !iface.structs.empty() || !iface.typedefs.empty() ) {
                w.Indent();
                w.Line( "// ==================== Interface Types: " + iface.name + " ====================" );
                w.Line();

                for ( const auto& e : iface.enums ) {
                    generateEnum( w, e );
                }
                for ( const auto& s : iface.structs ) {
                    generateStruct( w, s );
                }
                for ( const auto& t : iface.typedefs ) {
                    generateTypedef( w, t );
                }
                w.Dedent();
            }

            // Generate method output structs
            w.Indent();
            for ( const auto& method : iface.methods ) {
                if ( method.outArgs.size() > 1 ) {
                    generateMethodOutput( w, method );
                }
            }

            // Generate broadcast event structs
            for ( const auto& broadcast : iface.broadcasts ) {
                generateBroadcastEvent( w, broadcast );
            }

            // Generate ADL serialization traits
            generateSerializationTraits( w, iface, model );

            w.Dedent();

            // [SWS_CM_11501] — common inner namespace with service identification
            generateCommonClass( w, iface, model, config );

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

    // ==================== Type Generation ====================

    void CTypesGenerator::generateEnum( CCodeWriter& w,
                                        const EnumDef& enumDef ) const noexcept {
        w.Line( "/**" );
        w.Line( " * @brief " + enumDef.name + " enumeration" );
        w.Line( " */" );
        w.Line( "enum class " + enumDef.name + " : Int32 {" );
        w.Indent();

        for ( ::std::size_t i = 0; i < enumDef.enumerators.size(); ++i ) {
            const auto& e = enumDef.enumerators[i];
            String valueName = ToEnumValueName( e.name );
            String line = valueName;

            // Column-align values
            while ( line.size() < 24 ) {
                line += ' ';
            }
            line += "= " + ::std::to_string( e.value );

            if ( i + 1 < enumDef.enumerators.size() ) {
                line += ",";
            }

            w.Line( line );
        }

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    void CTypesGenerator::generateStruct( CCodeWriter& w,
                                          const StructDef& structDef ) const noexcept {
        w.Line( "/**" );
        w.Line( " * @brief " + structDef.name + " data structure" );
        w.Line( " */" );

        String decl = "struct " + structDef.name;
        if ( !structDef.extends.empty() ) {
            TypeRef baseRef;
            baseRef.name = structDef.extends;
            decl += " : public " + resolveCppType( baseRef );
        }
        w.Line( decl + " {" );
        w.Indent();

        for ( const auto& field : structDef.fields ) {
            String typeName = resolveCppType( field.type );
            // Pad type name for alignment (at least one space)
            String line = typeName;
            while ( line.size() < 28 ) {
                line += ' ';
            }
            if ( line.size() == typeName.size() ) {
                line += ' ';
            }
            line += field.name + ";";
            w.Line( line );
        }

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    void CTypesGenerator::generateTypedef( CCodeWriter& w,
                                           const TypedefDef& typedefDef ) const noexcept {
        String targetType = resolveCppType( typedefDef.targetType );
        w.Line( "using " + typedefDef.name + " = " + targetType + ";" );
        w.Line();
    }

    void CTypesGenerator::generateArray( CCodeWriter& w,
                                         const ArrayDef& arrayDef ) const noexcept {
        String elemType = resolveCppType( arrayDef.elementType );
        w.Line( "using " + arrayDef.name + " = ::std::vector< " + elemType + " >;" );
        w.Line();
    }

    void CTypesGenerator::generateMap( CCodeWriter& w,
                                       const MapDef& mapDef ) const noexcept {
        String keyType = resolveCppType( mapDef.keyType );
        String valType = resolveCppType( mapDef.valueType );
        w.Line( "using " + mapDef.name + " = ::std::unordered_map< "
                + keyType + ", " + valType + " >;" );
        w.Line();
    }

    void CTypesGenerator::generateMethodOutput( CCodeWriter& w,
                                                const MethodDef& method ) const noexcept {
        String structName = ToPascalCase( method.name ) + "Output";
        w.Line( "/**" );
        w.Line( " * @brief Output type for method " + method.name );
        w.Line( " */" );
        w.Line( "struct " + structName + " {" );
        w.Indent();

        for ( const auto& arg : method.outArgs ) {
            String typeName = resolveCppType( arg.type );
            String line = typeName;
            while ( line.size() < 28 ) {
                line += ' ';
            }
            if ( line.size() == typeName.size() ) {
                line += ' ';
            }
            line += arg.name + ";";
            w.Line( line );
        }

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    void CTypesGenerator::generateBroadcastEvent( CCodeWriter& w,
                                                  const BroadcastDef& broadcast ) const noexcept {
        String structName = ToPascalCase( broadcast.name ) + "Event";
        w.Line( "/**" );
        w.Line( " * @brief Event data for broadcast " + broadcast.name );
        w.Line( " * @note [SWS_CM_00700] — Event communication" );
        w.Line( " */" );
        w.Line( "struct " + structName + " {" );
        w.Indent();

        for ( const auto& arg : broadcast.outArgs ) {
            String typeName = resolveCppType( arg.type );
            String line = typeName;
            while ( line.size() < 28 ) {
                line += ' ';
            }
            if ( line.size() == typeName.size() ) {
                line += ' ';
            }
            line += arg.name + ";";
            w.Line( line );
        }

        w.Dedent();
        w.Line( "};" );
        w.Line();
    }

    void CTypesGenerator::generateTypeCollection( CCodeWriter& w,
                                                  const TypeCollection& tc ) const noexcept {
        w.Indent();
        w.Line( "// ==================== Type Collection: " + tc.name + " ====================" );
        w.Line();

        // Open typeCollection as a namespace
        w.Line( "namespace " + tc.name );
        w.Line( "{" );
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

        for ( const auto& a : tc.arrays ) {
            generateArray( w, a );
        }

        for ( const auto& m : tc.maps ) {
            generateMap( w, m );
        }

        // ADL Serialize/Deserialize for structs (must be in same namespace for ADL)
        if ( !tc.structs.empty() || !tc.enums.empty() ) {
            w.Line( "// --- ADL serialization (in-namespace for ADL) ---" );
            w.Line();

            for ( const auto& s : tc.structs ) {
                generateStructSerialize( w, s.name, s.fields );
                generateStructDeserialize( w, s.name, s.fields );
            }

            for ( const auto& e : tc.enums ) {
                w.Line( "inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const " + e.name + "& v ) noexcept {" );
                w.Indent();
                w.Line( "return s.Serialize( static_cast< Int32 >( v ) );" );
                w.Dedent();
                w.Line( "}" );
                w.Line();
                w.Line( "inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, " + e.name + "& v ) noexcept {" );
                w.Indent();
                w.Line( "Int32 tmp = 0;" );
                w.Line( "auto r = d.Deserialize( tmp );" );
                w.Line( "if ( r.HasValue() ) { v = static_cast< " + e.name + " >( tmp ); }" );
                w.Line( "return r;" );
                w.Dedent();
                w.Line( "}" );
                w.Line();
            }
        }

        w.Line( "} // namespace " + tc.name );
        w.Line();
        w.Dedent();
    }

    // ==================== ADL Serialization Traits ====================

    void CTypesGenerator::generateStructSerialize( CCodeWriter& w,
        const String& structName,
        const ::std::vector< Field >& fields ) const noexcept {
        w.Line( "inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const " + structName + "& v ) noexcept {" );
        w.Indent();
        for ( const auto& f : fields ) {
            w.Line( "auto r_" + f.name + " = ::lap::com::serialization::SerializeValue( s, v." + f.name + " );" );
            w.Line( "if ( !r_" + f.name + ".HasValue() ) { return r_" + f.name + "; }" );
        }
        w.Line( "return Result< void >::FromValue();" );
        w.Dedent();
        w.Line( "}" );
        w.Line();
    }

    void CTypesGenerator::generateStructDeserialize( CCodeWriter& w,
        const String& structName,
        const ::std::vector< Field >& fields ) const noexcept {
        w.Line( "inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, " + structName + "& v ) noexcept {" );
        w.Indent();
        for ( const auto& f : fields ) {
            w.Line( "auto r_" + f.name + " = ::lap::com::serialization::DeserializeValue( d, v." + f.name + " );" );
            w.Line( "if ( !r_" + f.name + ".HasValue() ) { return r_" + f.name + "; }" );
        }
        w.Line( "return Result< void >::FromValue();" );
        w.Dedent();
        w.Line( "}" );
        w.Line();
    }

    void CTypesGenerator::generateSerializationTraits( CCodeWriter& w,
        const Interface& iface, const FidlModel& model ) const noexcept {
        w.Line();
        w.Line( "// ==================== ADL Serialization Traits ====================" );
        w.Line( "// Required by CSerializationTraits.hpp for non-primitive types" );
        w.Line();

        // Note: typeCollection struct/enum serialization is generated inside
        // the typeCollection namespace block (for correct ADL resolution).
        // Here we only generate serialization for interface-level types.

        // Serialization for broadcast event structs
        for ( const auto& broadcast : iface.broadcasts ) {
            String eventName = ToPascalCase( broadcast.name ) + "Event";
            generateStructSerialize( w, eventName, broadcast.outArgs );
            generateStructDeserialize( w, eventName, broadcast.outArgs );
        }

        // Serialization for method output structs (multi-output only)
        for ( const auto& method : iface.methods ) {
            if ( method.outArgs.size() > 1 ) {
                String outputName = ToPascalCase( method.name ) + "Output";
                generateStructSerialize( w, outputName, method.outArgs );
                generateStructDeserialize( w, outputName, method.outArgs );
            }
        }
    }

    void CTypesGenerator::generateCommonClass( CCodeWriter& w,
        const Interface& iface, const FidlModel& model,
        const GeneratorConfig& config ) const noexcept {

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

        w.Line();
        w.Line( "// [SWS_CM_11501] \xE2\x80\x94 common inner namespace" );
        w.Line( "namespace common" );
        w.Line( "{" );
        w.Line();

        w.Indent();
        w.Line( "/**" );
        w.Line( " * @brief Common service identification for " + iface.name + " [SWS_CM_01010]" );
        if ( iface.version.IsValid() ) {
            w.Line( " * @version " + iface.version.ToString() );
        }
        w.Line( " */" );
        w.Line( "class " + iface.name + " {" );
        w.Line( "public:" );
        w.Indent();

        // [SWS_CM_11506] — serviceIdentifier
        w.Line( "static constexpr UInt16 kServiceId = 0x"
                + ToHexValue( serviceId )
                + ";  ///< [SWS_CM_11506]" );
        w.Line( "static constexpr const Char* kServiceName = \"" + iface.name + "\";" );
        w.Line( "static constexpr const Char* kSchemaHash  = \"" + schemaHash + "\";" );

        // [SWS_CM_11507] — serviceVersion
        if ( iface.version.IsValid() ) {
            w.Line( "static constexpr UInt32 kVersionMajor = "
                    + ::std::to_string( iface.version.major )
                    + ";  ///< [SWS_CM_11507]" );
            w.Line( "static constexpr UInt32 kVersionMinor = "
                    + ::std::to_string( iface.version.minor ) + ";" );
        }

        w.Dedent();
        w.Line( "};" );
        w.Dedent();

        w.Line();
        w.Line( "} // namespace common" );
    }

} // namespace generator
} // namespace com
} // namespace lap
