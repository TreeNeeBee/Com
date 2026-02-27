/**
 * @file        CFidlParser.cpp
 * @author      Aii
 * @brief       Franca IDL Recursive Descent Parser Implementation
 * @date        2026/02/09
 * @details     Complete recursive descent parser for Franca IDL syntax.
 *              Supports all Franca IDL constructs: package, import, typeCollection,
 *              interface, struct, enumeration, typedef, array, map,
 *              method (incl. fireAndForget), broadcast, attribute (incl. readonly).
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CFidlParser.hpp"

// ==================== Standard Library Headers ====================
#include <cstdlib>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== CFidlParser Implementation ====================

    CFidlParser::CFidlParser( const ::std::vector< Token >& tokens,
                              const String& filename ) noexcept
        : m_tokens( tokens )
        , m_pos( 0 )
        , m_filename( filename ) {
    }

    FidlModel CFidlParser::Parse() {
        FidlModel model;
        model.sourceFile = m_filename;

        // Parse package declaration (required)
        if ( check( TokenType::kPackage ) ) {
            model.packageName = parsePackageDecl();
        } else {
            error( "Expected 'package' declaration at beginning of file" );
        }

        // Parse imports (optional, may be multiple)
        while ( check( TokenType::kImport ) ) {
            model.imports.push_back( parseImportDecl() );
        }

        // Parse top-level declarations
        while ( !isAtEnd() ) {
            if ( check( TokenType::kTypeCollection ) ) {
                model.typeCollections.push_back( parseTypeCollection() );
            } else if ( check( TokenType::kInterface ) ) {
                model.interfaces.push_back( parseInterface() );
            } else if ( check( TokenType::kEof ) ) {
                break;
            } else {
                error( "Expected 'typeCollection' or 'interface', got '"
                       + current().value + "'" );
            }
        }

        return model;
    }

    // ==================== Top-Level Parsing ====================

    String CFidlParser::parsePackageDecl() {
        expect( TokenType::kPackage, "package declaration" );
        String name = parseQualifiedName();
        return name;
    }

    String CFidlParser::parseImportDecl() {
        expect( TokenType::kImport, "import declaration" );
        const Token& str = expect( TokenType::kStringLiteral, "import path" );
        return str.value;
    }

    TypeCollection CFidlParser::parseTypeCollection() {
        TypeCollection tc;
        tc.location.file   = m_filename;
        tc.location.line   = current().line;
        tc.location.column = current().column;

        expect( TokenType::kTypeCollection, "typeCollection" );
        tc.name = expect( TokenType::kIdentifier, "typeCollection name" ).value;
        expect( TokenType::kLBrace, "typeCollection body" );

        while ( !check( TokenType::kRBrace ) && !isAtEnd() ) {
            if ( check( TokenType::kVersion ) ) {
                tc.version = parseVersion();
            } else if ( check( TokenType::kEnumeration ) ) {
                tc.enums.push_back( parseEnumDef() );
            } else if ( check( TokenType::kStruct ) ) {
                tc.structs.push_back( parseStructDef() );
            } else if ( check( TokenType::kTypedef ) ) {
                tc.typedefs.push_back( parseTypedefDef() );
            } else if ( check( TokenType::kArray ) ) {
                tc.arrays.push_back( parseArrayDef() );
            } else if ( check( TokenType::kMap ) ) {
                tc.maps.push_back( parseMapDef() );
            } else {
                error( "Unexpected token '" + current().value
                       + "' in typeCollection '" + tc.name + "'" );
            }
        }

        expect( TokenType::kRBrace, "typeCollection closing" );
        return tc;
    }

    Interface CFidlParser::parseInterface() {
        Interface iface;
        iface.location.file   = m_filename;
        iface.location.line   = current().line;
        iface.location.column = current().column;

        expect( TokenType::kInterface, "interface" );
        iface.name = expect( TokenType::kIdentifier, "interface name" ).value;

        // Optional 'extends' clause
        if ( check( TokenType::kExtends ) ) {
            match( TokenType::kExtends );
            iface.extends = parseQualifiedName();
        }

        expect( TokenType::kLBrace, "interface body" );

        while ( !check( TokenType::kRBrace ) && !isAtEnd() ) {
            if ( check( TokenType::kVersion ) ) {
                iface.version = parseVersion();
            } else if ( check( TokenType::kMethod ) ) {
                iface.methods.push_back( parseMethodDef() );
            } else if ( check( TokenType::kBroadcast ) ) {
                iface.broadcasts.push_back( parseBroadcastDef() );
            } else if ( check( TokenType::kAttribute ) ) {
                iface.attributes.push_back( parseAttributeDef() );
            } else if ( check( TokenType::kEnumeration ) ) {
                iface.enums.push_back( parseEnumDef() );
            } else if ( check( TokenType::kStruct ) ) {
                iface.structs.push_back( parseStructDef() );
            } else if ( check( TokenType::kTypedef ) ) {
                iface.typedefs.push_back( parseTypedefDef() );
            } else {
                error( "Unexpected token '" + current().value
                       + "' in interface '" + iface.name + "'" );
            }
        }

        expect( TokenType::kRBrace, "interface closing" );
        return iface;
    }

    // ==================== Sub-Element Parsing ====================

    Version CFidlParser::parseVersion() {
        Version ver;
        expect( TokenType::kVersion, "version" );
        expect( TokenType::kLBrace, "version body" );

        expect( TokenType::kMajor, "version major" );
        const Token& majorTok = expect( TokenType::kIntegerLiteral, "major version number" );
        ver.major = static_cast< UInt32 >( ::std::strtoul( majorTok.value.c_str(), nullptr, 0 ) );

        expect( TokenType::kMinor, "version minor" );
        const Token& minorTok = expect( TokenType::kIntegerLiteral, "minor version number" );
        ver.minor = static_cast< UInt32 >( ::std::strtoul( minorTok.value.c_str(), nullptr, 0 ) );

        // Optional patch
        if ( check( TokenType::kPatch ) ) {
            match( TokenType::kPatch );
            const Token& patchTok = expect( TokenType::kIntegerLiteral, "patch version number" );
            ver.patch = static_cast< UInt32 >( ::std::strtoul( patchTok.value.c_str(), nullptr, 0 ) );
        }

        expect( TokenType::kRBrace, "version closing" );
        return ver;
    }

    EnumDef CFidlParser::parseEnumDef() {
        EnumDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kEnumeration, "enumeration" );
        def.name = expect( TokenType::kIdentifier, "enumeration name" ).value;
        expect( TokenType::kLBrace, "enumeration body" );

        Int32 autoValue = 0;
        while ( !check( TokenType::kRBrace ) && !isAtEnd() ) {
            Enumerator e;
            e.name = expect( TokenType::kIdentifier, "enumerator name" ).value;

            if ( match( TokenType::kEquals ) ) {
                const Token& valTok = expect( TokenType::kIntegerLiteral,
                                              "enumerator value" );
                e.value = static_cast< Int32 >( ::std::strtol(
                    valTok.value.c_str(), nullptr, 0 ) );
                e.hasExplicitValue = true;
                autoValue = e.value + 1;
            } else {
                e.value = autoValue++;
                e.hasExplicitValue = false;
            }

            def.enumerators.push_back( e );

            // Optional comma separator
            match( TokenType::kComma );
        }

        expect( TokenType::kRBrace, "enumeration closing" );
        return def;
    }

    StructDef CFidlParser::parseStructDef() {
        StructDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kStruct, "struct" );
        def.name = expect( TokenType::kIdentifier, "struct name" ).value;

        // Optional 'extends' clause
        if ( check( TokenType::kExtends ) ) {
            match( TokenType::kExtends );
            def.extends = parseQualifiedName();
        }

        expect( TokenType::kLBrace, "struct body" );

        while ( !check( TokenType::kRBrace ) && !isAtEnd() ) {
            def.fields.push_back( parseField() );
            // Optional comma or semicolon separator
            match( TokenType::kComma );
            match( TokenType::kSemicolon );
        }

        expect( TokenType::kRBrace, "struct closing" );
        return def;
    }

    TypedefDef CFidlParser::parseTypedefDef() {
        TypedefDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kTypedef, "typedef" );
        def.name = expect( TokenType::kIdentifier, "typedef name" ).value;
        expect( TokenType::kIs, "typedef 'is' keyword" );

        // Handle 'array of <type>' syntax
        if ( check( TokenType::kArray ) ) {
            match( TokenType::kArray );
            expect( TokenType::kOf, "array 'of' keyword" );
            def.targetType = parseTypeRef();
            def.targetType.isArray = true;
        } else {
            def.targetType = parseTypeRef();
        }

        return def;
    }

    ArrayDef CFidlParser::parseArrayDef() {
        ArrayDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kArray, "array" );
        def.name = expect( TokenType::kIdentifier, "array name" ).value;
        expect( TokenType::kOf, "array 'of' keyword" );
        def.elementType = parseTypeRef();

        return def;
    }

    MapDef CFidlParser::parseMapDef() {
        MapDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kMap, "map" );
        def.name = expect( TokenType::kIdentifier, "map name" ).value;
        expect( TokenType::kLBrace, "map body" );
        def.keyType = parseTypeRef();
        expect( TokenType::kTo, "map 'to' keyword" );
        def.valueType = parseTypeRef();
        expect( TokenType::kRBrace, "map closing" );

        return def;
    }

    MethodDef CFidlParser::parseMethodDef() {
        MethodDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kMethod, "method" );
        def.name = expect( TokenType::kIdentifier, "method name" ).value;

        // Optional fireAndForget modifier
        if ( match( TokenType::kFireAndForget ) ) {
            def.isFireAndForget = true;
        }

        expect( TokenType::kLBrace, "method body" );

        while ( !check( TokenType::kRBrace ) && !isAtEnd() ) {
            if ( check( TokenType::kIn ) ) {
                match( TokenType::kIn );
                def.inArgs = parseFieldBlock();
            } else if ( check( TokenType::kOut ) ) {
                match( TokenType::kOut );
                def.outArgs = parseFieldBlock();
            } else if ( check( TokenType::kError ) ) {
                match( TokenType::kError );
                // error can be:
                //   error { <fields> }        — field block form
                //   error <TypeRef>           — bare type reference
                if ( check( TokenType::kLBrace ) ) {
                    def.errorArgs = parseFieldBlock();
                } else {
                    // Bare type reference: synthesize a single field
                    Field errField;
                    errField.type = parseTypeRef();
                    errField.name = "errorValue";
                    def.errorArgs.push_back( errField );
                }
            } else {
                error( "Expected 'in', 'out', or 'error' in method '"
                       + def.name + "', got '" + current().value + "'" );
            }
        }

        expect( TokenType::kRBrace, "method closing" );
        return def;
    }

    BroadcastDef CFidlParser::parseBroadcastDef() {
        BroadcastDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kBroadcast, "broadcast" );
        def.name = expect( TokenType::kIdentifier, "broadcast name" ).value;
        expect( TokenType::kLBrace, "broadcast body" );

        if ( check( TokenType::kOut ) ) {
            match( TokenType::kOut );
            def.outArgs = parseFieldBlock();
        }

        expect( TokenType::kRBrace, "broadcast closing" );
        return def;
    }

    AttributeDef CFidlParser::parseAttributeDef() {
        AttributeDef def;
        def.location.file   = m_filename;
        def.location.line   = current().line;
        def.location.column = current().column;

        expect( TokenType::kAttribute, "attribute" );
        def.type = parseTypeRef();
        def.name = expect( TokenType::kIdentifier, "attribute name" ).value;

        // Optional modifiers: readonly, notify, noSubscriptions
        if ( match( TokenType::kReadonly ) ) {
            def.isReadonly = true;
        }
        if ( match( TokenType::kNotify ) ) {
            def.isNotify = true;
        }
        if ( match( TokenType::kNoSubscriptions ) ) {
            def.isNoSubscriptions = true;
        }

        return def;
    }

    // ==================== Utility Parsing ====================

    TypeRef CFidlParser::parseTypeRef() {
        TypeRef ref;
        ref.name = expect( TokenType::kIdentifier, "type name" ).value;

        // Qualified name: identifier.identifier.identifier...
        while ( check( TokenType::kDot ) ) {
            match( TokenType::kDot );
            ref.name += "." + expect( TokenType::kIdentifier, "qualified type name" ).value;
        }

        // Array suffix: []
        if ( match( TokenType::kLBracket ) ) {
            expect( TokenType::kRBracket, "array type closing ']'" );
            ref.isArray = true;
        }

        return ref;
    }

    Field CFidlParser::parseField() {
        Field field;
        field.type = parseTypeRef();
        field.name = expect( TokenType::kIdentifier, "field name" ).value;
        return field;
    }

    ::std::vector< Field > CFidlParser::parseFieldBlock() {
        ::std::vector< Field > fields;
        expect( TokenType::kLBrace, "field block" );

        while ( !check( TokenType::kRBrace ) && !isAtEnd() ) {
            fields.push_back( parseField() );
            // Optional comma separator
            match( TokenType::kComma );
        }

        expect( TokenType::kRBrace, "field block closing" );
        return fields;
    }

    String CFidlParser::parseQualifiedName() {
        String name = expect( TokenType::kIdentifier, "qualified name" ).value;

        while ( check( TokenType::kDot ) ) {
            match( TokenType::kDot );
            name += "." + expect( TokenType::kIdentifier, "qualified name segment" ).value;
        }

        return name;
    }

    // ==================== Token Navigation ====================

    const Token& CFidlParser::current() const noexcept {
        if ( m_pos >= static_cast< UInt32 >( m_tokens.size() ) ) {
            // Return last token (should be EOF)
            return m_tokens.back();
        }
        return m_tokens[m_pos];
    }

    const Token& CFidlParser::expect( TokenType type, const String& context ) {
        if ( current().type != type ) {
            error( "Expected '" + String( TokenTypeName( type ) )
                   + "' in " + context + ", got '"
                   + current().value + "' ("
                   + TokenTypeName( current().type ) + ")" );
        }
        return m_tokens[m_pos++];
    }

    Bool CFidlParser::match( TokenType type ) noexcept {
        if ( check( type ) ) {
            ++m_pos;
            return true;
        }
        return false;
    }

    Bool CFidlParser::check( TokenType type ) const noexcept {
        return current().type == type;
    }

    Bool CFidlParser::isAtEnd() const noexcept {
        return current().type == TokenType::kEof;
    }

    // ==================== Error Reporting ====================

    [[noreturn]] void CFidlParser::error( const String& message ) const {
        throw ParserError( message, m_filename,
                           current().line, current().column );
    }

} // namespace generator
} // namespace com
} // namespace lap
