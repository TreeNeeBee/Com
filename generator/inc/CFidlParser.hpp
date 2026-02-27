/**
 * @file        CFidlParser.hpp
 * @author      Aii
 * @brief       Franca IDL Recursive Descent Parser
 * @date        2026/02/09
 * @details     Parses a token stream from CFidlLexer into a FidlModel AST.
 *              Supports: package, import, typeCollection, interface,
 *              struct, enumeration, typedef, array, map,
 *              method, broadcast, attribute.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CFIDLPARSER_HPP
#define LAP_COM_GENERATOR_CFIDLPARSER_HPP

// ==================== Project-Internal Headers ====================
#include "CFidlAst.hpp"
#include "CFidlLexer.hpp"

// ==================== Standard Library Headers ====================
#include <stdexcept>
#include <vector>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== Parser Error ====================

    /**
     * @brief Parser error with source location
     */
    class ParserError : public ::std::runtime_error {
    public:
        ParserError( const String& message, const String& file,
                     UInt32 line, UInt32 column )
            : ::std::runtime_error( formatMessage( message, file, line, column ) )
            , m_line( line )
            , m_column( column )
        {}

        UInt32 GetLine() const noexcept { return m_line; }
        UInt32 GetColumn() const noexcept { return m_column; }

    private:
        static String formatMessage( const String& msg, const String& file,
                                     UInt32 line, UInt32 col ) {
            return file + ":" + ::std::to_string( line ) + ":"
                 + ::std::to_string( col ) + ": error: " + msg;
        }

        UInt32 m_line;
        UInt32 m_column;
    };

    // ==================== CFidlParser ====================

    /**
     * @brief Franca IDL recursive descent parser
     * @note Produces a complete FidlModel AST from a token stream
     */
    class CFidlParser {
    public:
        /**
         * @brief Construct parser from token stream
         * @param tokens   Token stream (from CFidlLexer::Tokenize)
         * @param filename Source filename (for error reporting)
         */
        explicit CFidlParser( const ::std::vector< Token >& tokens,
                              const String& filename = "" ) noexcept;

        ~CFidlParser() noexcept = default;

        /**
         * @brief Parse tokens into a FidlModel
         * @return Complete AST model
         * @throws ParserError on syntax errors
         */
        FidlModel Parse();

        // Non-copyable, non-movable
        CFidlParser( const CFidlParser& )            = delete;
        CFidlParser& operator=( const CFidlParser& ) = delete;
        CFidlParser( CFidlParser&& )                  = delete;
        CFidlParser& operator=( CFidlParser&& )       = delete;

    private:
        // ==================== Top-Level Parsing ====================
        String          parsePackageDecl();
        String          parseImportDecl();
        TypeCollection  parseTypeCollection();
        Interface       parseInterface();

        // ==================== Sub-Element Parsing ====================
        Version         parseVersion();
        EnumDef         parseEnumDef();
        StructDef       parseStructDef();
        TypedefDef      parseTypedefDef();
        ArrayDef        parseArrayDef();
        MapDef          parseMapDef();
        MethodDef       parseMethodDef();
        BroadcastDef    parseBroadcastDef();
        AttributeDef    parseAttributeDef();

        // ==================== Utility Parsing ====================
        TypeRef                 parseTypeRef();
        Field                   parseField();
        ::std::vector< Field >  parseFieldBlock();
        String                  parseQualifiedName();

        // ==================== Token Navigation ====================
        const Token& current() const noexcept;
        const Token& expect( TokenType type, const String& context );
        Bool         match( TokenType type ) noexcept;
        Bool         check( TokenType type ) const noexcept;
        Bool         isAtEnd() const noexcept;

        // ==================== Error Reporting ====================
        [[noreturn]] void error( const String& message ) const;

        const ::std::vector< Token >& m_tokens;
        UInt32                         m_pos = 0;
        String                         m_filename;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CFIDLPARSER_HPP
