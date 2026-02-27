/**
 * @file        CFidlLexer.hpp
 * @author      Aii
 * @brief       Franca IDL Lexical Analyzer (Tokenizer)
 * @date        2026/02/09
 * @details     Converts Franca IDL source text into a stream of tokens.
 *              Handles keywords, identifiers, numbers, strings, and symbols.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CFIDLLEXER_HPP
#define LAP_COM_GENERATOR_CFIDLLEXER_HPP

// ==================== Project-Internal Headers ====================
#include "CFidlAst.hpp"

// ==================== Standard Library Headers ====================
#include <stdexcept>
#include <vector>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== Token Type ====================

    /**
     * @brief All token types for Franca IDL
     */
    enum class TokenType : Int32 {
        // ==================== Keywords ====================
        kPackage = 0,
        kImport,
        kTypeCollection,
        kInterface,
        kVersion,
        kMajor,
        kMinor,
        kPatch,
        kStruct,
        kEnumeration,
        kTypedef,
        kIs,
        kArray,
        kOf,
        kMap,
        kTo,
        kUnion,
        kMethod,
        kBroadcast,
        kAttribute,
        kIn,
        kOut,
        kError,
        kFireAndForget,
        kReadonly,
        kNotify,
        kNoSubscriptions,
        kExtends,
        kConst,

        // ==================== Literals ====================
        kIdentifier,
        kIntegerLiteral,
        kStringLiteral,

        // ==================== Symbols ====================
        kLBrace,     ///< {
        kRBrace,     ///< }
        kLBracket,   ///< [
        kRBracket,   ///< ]
        kLParen,     ///< (
        kRParen,     ///< )
        kDot,        ///< .
        kComma,      ///< ,
        kEquals,     ///< =
        kSemicolon,  ///< ;

        // ==================== Special ====================
        kEof,
        kUnknown
    };

    /**
     * @brief Get string representation of a token type
     * @param type Token type
     * @return Human-readable name
     */
    const Char* TokenTypeName( TokenType type ) noexcept;

    // ==================== Token ====================

    /**
     * @brief Single token from Franca IDL source
     */
    struct Token {
        TokenType type   = TokenType::kUnknown;
        String    value;
        UInt32    line   = 0;
        UInt32    column = 0;
    };

    // ==================== Lexer Error ====================

    /**
     * @brief Lexer error with source location
     */
    class LexerError : public ::std::runtime_error {
    public:
        LexerError( const String& message, const String& file,
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

    // ==================== CFidlLexer ====================

    /**
     * @brief Franca IDL lexical analyzer
     * @note Thread-safe: each instance operates independently
     */
    class CFidlLexer {
    public:
        /**
         * @brief Construct lexer from source text
         * @param source    Franca IDL source text
         * @param filename  Source filename (for error reporting)
         */
        explicit CFidlLexer( const String& source,
                             const String& filename = "" ) noexcept;

        ~CFidlLexer() noexcept = default;

        /**
         * @brief Tokenize the entire source text
         * @return Vector of tokens (last token is kEof)
         * @throws LexerError on invalid input
         */
        ::std::vector< Token > Tokenize();

        // Non-copyable, non-movable
        CFidlLexer( const CFidlLexer& )            = delete;
        CFidlLexer& operator=( const CFidlLexer& ) = delete;
        CFidlLexer( CFidlLexer&& )                  = delete;
        CFidlLexer& operator=( CFidlLexer&& )       = delete;

    private:
        void skipWhitespace() noexcept;
        void skipLineComment() noexcept;
        void skipBlockComment();
        Token readIdentifierOrKeyword() noexcept;
        Token readNumber();
        Token readString();
        Token makeToken( TokenType type, const String& value ) const noexcept;

        Bool isAtEnd() const noexcept;
        Char peek() const noexcept;
        Char peekNext() const noexcept;
        Char advance() noexcept;

        String m_source;
        String m_filename;
        UInt32 m_pos    = 0;
        UInt32 m_line   = 1;
        UInt32 m_column = 1;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CFIDLLEXER_HPP
