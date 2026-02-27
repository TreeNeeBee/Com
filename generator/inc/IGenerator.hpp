/**
 * @file        IGenerator.hpp
 * @author      Aii
 * @brief       Code Generator Interface and Utilities
 * @date        2026/02/09
 * @details     Defines the IGenerator interface for all code generators
 *              and the CCodeWriter utility for structured code output.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_IGENERATOR_HPP
#define LAP_COM_GENERATOR_IGENERATOR_HPP

// ==================== Project-Internal Headers ====================
#include "CFidlAst.hpp"

// ==================== Standard Library Headers ====================
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== Franca → C++ Type Mapping ====================

    /**
     * @brief Map Franca IDL primitive types to C++ project types
     * @param francaType Franca type name
     * @return C++ type name (project alias)
     */
    inline String MapFrancaToCpp( const String& francaType ) noexcept {
        static const ::std::unordered_map< String, String > kTypeMap = {
            { "UInt8",      "UInt8" },
            { "UInt16",     "UInt16" },
            { "UInt32",     "UInt32" },
            { "UInt64",     "UInt64" },
            { "Int8",       "Int8" },
            { "Int16",      "Int16" },
            { "Int32",      "Int32" },
            { "Int64",      "Int64" },
            { "Float",      "Float" },
            { "Float32",    "Float" },
            { "Float64",    "Double" },
            { "Double",     "Double" },
            { "Boolean",    "Bool" },
            { "String",     "String" },
            { "ByteBuffer", "::std::vector< UInt8 >" },
            { "ByteArray",  "::std::vector< UInt8 >" },
        };

        auto it = kTypeMap.find( francaType );
        if ( it != kTypeMap.end() ) {
            return it->second;
        }
        return francaType;
    }

    /**
     * @brief Map Franca IDL primitive types to OMG IDL types
     * @param francaType Franca type name
     * @return OMG IDL type name
     */
    inline String MapFrancaToDds( const String& francaType ) noexcept {
        static const ::std::unordered_map< String, String > kTypeMap = {
            { "UInt8",      "octet" },
            { "UInt16",     "unsigned short" },
            { "UInt32",     "unsigned long" },
            { "UInt64",     "unsigned long long" },
            { "Int8",       "char" },
            { "Int16",      "short" },
            { "Int32",      "long" },
            { "Int64",      "long long" },
            { "Float",      "float" },
            { "Float32",    "float" },
            { "Float64",    "double" },
            { "Double",     "double" },
            { "Boolean",    "boolean" },
            { "String",     "string" },
            { "ByteBuffer", "sequence<octet>" },
            { "ByteArray",  "sequence<octet>" },
        };

        auto it = kTypeMap.find( francaType );
        if ( it != kTypeMap.end() ) {
            return it->second;
        }
        return francaType;
    }

    /**
     * @brief Check if a Franca type name is a primitive type
     * @param typeName Type name to check
     * @return true if primitive
     */
    inline Bool IsPrimitiveType( const String& typeName ) noexcept {
        static const ::std::unordered_map< String, Bool > kPrimitives = {
            { "UInt8", true },   { "UInt16", true },  { "UInt32", true },
            { "UInt64", true },  { "Int8", true },    { "Int16", true },
            { "Int32", true },   { "Int64", true },   { "Float", true },
            { "Float32", true }, { "Float64", true },
            { "Double", true },  { "Boolean", true }, { "String", true },
            { "ByteBuffer", true },
            { "ByteArray",  true },
        };
        return kPrimitives.count( typeName ) > 0;
    }

    // ==================== CCodeWriter ====================

    /**
     * @brief Structured code output writer with indentation support
     * @note Follows project formatting rules: 4-space indent, K&R braces
     */
    class CCodeWriter {
    public:
        CCodeWriter() noexcept = default;

        /**
         * @brief Write an indented line
         * @param line Text content (no trailing newline needed)
         */
        void Line( const String& line = "" ) {
            if ( line.empty() ) {
                m_stream << "\n";
            } else {
                writeIndent();
                m_stream << line << "\n";
            }
        }

        /**
         * @brief Write raw text without indentation
         * @param text Raw text
         */
        void Raw( const String& text ) {
            m_stream << text;
        }

        /**
         * @brief Increase indentation level
         */
        void Indent() noexcept { ++m_indentLevel; }

        /**
         * @brief Decrease indentation level
         */
        void Dedent() noexcept {
            if ( m_indentLevel > 0 ) { --m_indentLevel; }
        }

        /**
         * @brief Get the accumulated output
         * @return Output string
         */
        String GetOutput() const { return m_stream.str(); }

        /**
         * @brief Clear the output buffer
         */
        void Clear() noexcept {
            m_stream.str( "" );
            m_stream.clear();
            m_indentLevel = 0;
        }

        /**
         * @brief Write output to a file
         * @param filePath Output file path
         * @return true on success, false on failure
         */
        Bool WriteToFile( const String& filePath ) const {
            ::std::ofstream ofs( filePath );
            if ( !ofs.is_open() ) {
                return false;
            }
            ofs << m_stream.str();
            return ofs.good();
        }

    private:
        void writeIndent() {
            for ( UInt32 i = 0; i < m_indentLevel; ++i ) {
                m_stream << "    ";
            }
        }

        ::std::ostringstream m_stream;
        UInt32               m_indentLevel = 0;
    };

    // ==================== Utility Functions ====================

    /**
     * @brief Convert a Franca IDL name to PascalCase
     * @param name Input name
     * @return PascalCase name
     */
    inline String ToPascalCase( const String& name ) noexcept {
        if ( name.empty() ) { return name; }
        String result = name;
        result[0] = static_cast< Char >( ::std::toupper( result[0] ) );
        return result;
    }

    /**
     * @brief Convert a Franca IDL name to camelCase
     * @param name Input name
     * @return camelCase name
     */
    inline String ToCamelCase( const String& name ) noexcept {
        if ( name.empty() ) { return name; }
        String result = name;
        result[0] = static_cast< Char >( ::std::tolower( result[0] ) );
        return result;
    }

    /**
     * @brief Convert a name to UPPER_SNAKE_CASE
     * @param name Input name (PascalCase or camelCase)
     * @return UPPER_SNAKE_CASE name
     */
    inline String ToUpperSnake( const String& name ) noexcept {
        String result;
        result.reserve( name.size() + 4 );
        for ( ::std::size_t i = 0; i < name.size(); ++i ) {
            if ( i > 0 && ::std::isupper( name[i] ) && ::std::islower( name[i - 1] ) ) {
                result += '_';
            }
            result += static_cast< Char >( ::std::toupper( name[i] ) );
        }
        return result;
    }

    /**
     * @brief Convert Franca enum value name to k-prefixed PascalCase
     * @param name Franca enum value (e.g., "ADD", "DIVISION_BY_ZERO")
     * @return k-prefixed name (e.g., "kAdd", "kDivisionByZero")
     */
    inline String ToEnumValueName( const String& name ) noexcept {
        String result = "k";
        Bool nextUpper = true;
        for ( auto c : name ) {
            if ( c == '_' ) {
                nextUpper = true;
                continue;
            }
            if ( nextUpper ) {
                result += static_cast< Char >( ::std::toupper( c ) );
                nextUpper = false;
            } else {
                result += static_cast< Char >( ::std::tolower( c ) );
            }
        }
        return result;
    }

    /**
     * @brief Extract namespace segments from a package name
     * @param packageName Franca package name (e.g., "com.lightap.example")
     * @param prefix      Namespace prefix (e.g., "lap::com")
     * @return Namespace segments (e.g., ["lap", "com", "example"])
     */
    inline ::std::vector< String > ExtractNamespaceSegments(
        const String& packageName, const String& prefix ) noexcept {
        // Parse prefix
        ::std::vector< String > segments; {
            ::std::istringstream ss( prefix );
            String seg;
            while ( ::std::getline( ss, seg, ':' ) ) {
                if ( !seg.empty() ) {
                    segments.push_back( seg );
                }
            }
        }

        // Parse package name into dot-separated parts
        ::std::vector< String > pkgParts; {
            ::std::istringstream ss( packageName );
            String seg;
            while ( ::std::getline( ss, seg, '.' ) ) {
                pkgParts.push_back( seg );
            }
        }

        // Skip first 2 vendor segments (e.g., "org" and "lap"), append the rest.
        // Deduplicate: skip any package segment already at the end of the prefix.
        for ( ::std::size_t i = 2; i < pkgParts.size(); ++i ) {
            // Avoid duplicating the last prefix segment
            if ( !segments.empty() && segments.back() == pkgParts[i] ) {
                continue;
            }
            segments.push_back( pkgParts[i] );
        }

        // If package had ≤ 2 segments, use the last one (if not already present)
        if ( pkgParts.size() <= 2 && !pkgParts.empty() ) {
            if ( segments.empty() || segments.back() != pkgParts.back() ) {
                segments.push_back( pkgParts.back() );
            }
        }

        return segments;
    }

    /**
     * @brief Convert an integer value to hex string (value-based, endian-safe)
     * @param value Integer value
     * @param width Minimum hex digit width (with leading zeros)
     * @return Hex string (e.g., "0100" for value=256, width=4)
     */
    inline String ToHexValue( UInt32 value, UInt32 width = 4 ) noexcept {
        ::std::ostringstream ss;
        ss << ::std::hex << ::std::setfill( '0' )
           << ::std::setw( static_cast< int >( width ) ) << value;
        return ss.str();
    }

    // ==================== IGenerator Interface ====================

    /**
     * @brief Abstract interface for code generators
     */
    class IGenerator {
    public:
        virtual ~IGenerator() noexcept = default;

        // Non-copyable, non-movable
        IGenerator( const IGenerator& )            = delete;
        IGenerator& operator=( const IGenerator& ) = delete;
        IGenerator( IGenerator&& )                  = delete;
        IGenerator& operator=( IGenerator&& )       = delete;

        /**
         * @brief Generate output files from a FIDL model
         * @param model  Parsed FIDL model
         * @param config Generator configuration
         * @return true on success, false on failure
         */
        virtual Bool Generate( const FidlModel& model,
                               const GeneratorConfig& config ) = 0;

    protected:
        IGenerator() noexcept = default;

        /**
         * @brief Write the standard file header comment
         * @param w      Code writer
         * @param filename Output filename
         * @param brief  Brief description
         * @param source Source .fidl file
         * @param author Author name
         */
        void writeFileHeader( CCodeWriter& w, const String& filename,
                              const String& brief, const String& source,
                              const String& author ) const noexcept {
            w.Line( "/**" );
            w.Line( " * @file        " + filename );
            w.Line( " * @author      " + author );
            w.Line( " * @brief       " + brief );
            w.Line( " * @date        2026/02/09" );
            w.Line( " * @details     Auto-generated from " + source + " by lap-sidl-gen v1.0" );
            w.Line( " * @copyright   Copyright (c) 2026" );
            w.Line( " * @note        DO NOT EDIT — This file is auto-generated" );
            w.Line( " *" );
            w.Line( " * <table>" );
            w.Line( " * <tr><th>Date        <th>Version  <th>Author  <th>Description" );
            w.Line( " * <tr><td>2026/02/09  <td>1.0      <td>" + author + "     <td>Auto-generated" );
            w.Line( " * </table>" );
            w.Line( " */" );
        }

        /**
         * @brief Write include guard opening
         * @param w     Code writer
         * @param guard Guard macro name
         */
        void writeGuardOpen( CCodeWriter& w, const String& guard ) const noexcept {
            w.Line( "#ifndef " + guard );
            w.Line( "#define " + guard );
        }

        /**
         * @brief Write include guard closing
         * @param w     Code writer
         * @param guard Guard macro name
         */
        void writeGuardClose( CCodeWriter& w, const String& guard ) const noexcept {
            w.Line( "#endif // " + guard );
        }

        /**
         * @brief Open nested namespaces
         * @param w        Code writer
         * @param segments Namespace segments (e.g., ["lap", "com", "example"])
         */
        void writeNamespaceOpen( CCodeWriter& w,
                                 const ::std::vector< String >& segments ) const noexcept {
            for ( const auto& ns : segments ) {
                w.Line( "namespace " + ns );
                w.Line( "{" );
            }
        }

        /**
         * @brief Emit lap::com type using-declarations into the current namespace.
         *        Required when the generated namespace is NOT nested under lap::com,
         *        so that unqualified project types (String, UInt32, Result, …) resolve.
         * @param w        Code writer
         * @param segments Namespace segments of the generated namespace
         */
        void writeLapComUsings( CCodeWriter& w,
                                const ::std::vector< String >& segments ) const noexcept {
            // Determine whether we are already inside lap::com by checking
            // the first two segments.
            bool insideLapCom = segments.size() >= 2
                             && segments[0] == "lap"
                             && segments[1] == "com";
            if ( insideLapCom ) {
                return; // types already visible via outer namespace
            }
            // Emit using declarations so that unqualified types resolve.
            w.Line( "// ==================== LAP/COM Type Aliases ====================");
            w.Line( "using lap::core::Result;" );
            w.Line( "using lap::core::Optional;" );
            w.Line( "using lap::core::String;" );
            w.Line( "using lap::core::StringView;" );
            w.Line( "using lap::core::Bool;" );
            w.Line( "using lap::core::Char;" );
            w.Line( "using lap::core::UInt8;" );
            w.Line( "using lap::core::UInt16;" );
            w.Line( "using lap::core::UInt32;" );
            w.Line( "using lap::core::UInt64;" );
            w.Line( "using lap::core::Int32;" );
            w.Line( "using lap::core::Int64;" );
            w.Line( "using lap::core::Float;" );
            w.Line( "using lap::core::Double;" );
            w.Line( "using ::lap::core::Future;" );
            w.Line( "using ::lap::com::MethodCallProcessingMode;" );
            w.Line( "using ::lap::com::ComErrc;" );
            w.Line( "using ::lap::com::MakeErrorCode;" );            w.Line( "using ::lap::com::ServiceState;" );            w.Line( "using ByteArray = ::std::vector< UInt8 >;" );
            w.Line();
        }

        /**
         * @brief Close nested namespaces
         * @param w        Code writer
         * @param segments Namespace segments
         */
        void writeNamespaceClose( CCodeWriter& w,
                                  const ::std::vector< String >& segments ) const noexcept {
            for ( auto it = segments.rbegin(); it != segments.rend(); ++it ) {
                w.Line( "} // namespace " + *it );
            }
        }

        /**
         * @brief Build an include guard macro from namespace segments + filename
         * @param segments Namespace segments
         * @param filename Filename (without extension)
         * @return Guard macro (e.g., "LAP_COM_EXAMPLE_CALCULATOR_TYPES_HPP")
         */
        String buildGuardMacro( const ::std::vector< String >& segments,
                                const String& filename ) const noexcept {
            String guard;
            for ( const auto& s : segments ) {
                String upper = s;
                ::std::transform( upper.begin(), upper.end(), upper.begin(),
                    []( Char c ) { return static_cast< Char >( ::std::toupper( c ) ); } );
                guard += upper + "_";
            }
            String upperFile = filename;
            ::std::transform( upperFile.begin(), upperFile.end(), upperFile.begin(),
                []( Char c ) { return static_cast< Char >( ::std::toupper( c ) ); } );
            guard += upperFile + "_HPP";
            return guard;
        }

        /**
         * @brief Resolve a TypeRef to its C++ representation
         * @param ref TypeRef from AST
         * @return C++ type string
         */
        String resolveCppType( const TypeRef& ref ) const noexcept {
            String base = IsPrimitiveType( ref.name )
                ? MapFrancaToCpp( ref.name )
                : ref.ToCppName();

            if ( ref.isArray ) {
                return "::std::vector< " + base + " >";
            }
            return base;
        }
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_IGENERATOR_HPP
