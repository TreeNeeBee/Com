/**
 * @file        CFidlLexer.cpp
 * @author      Aii
 * @brief       Franca IDL Lexical Analyzer Implementation
 * @date        2026/02/09
 * @details     Tokenizes Franca IDL source text. Handles keywords, identifiers,
 *              integer literals (decimal + hex), string literals, symbols, and comments.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CFidlLexer.hpp"

// ==================== Standard Library Headers ====================
#include <cctype>
#include <unordered_map>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== Keyword Map ====================

    namespace
    {
        const ::std::unordered_map< String, TokenType >& getKeywordMap() noexcept {
            static const ::std::unordered_map< String, TokenType > kMap = {
                { "package",        TokenType::kPackage },
                { "import",         TokenType::kImport },
                { "typeCollection", TokenType::kTypeCollection },
                { "interface",      TokenType::kInterface },
                { "version",        TokenType::kVersion },
                { "major",          TokenType::kMajor },
                { "minor",          TokenType::kMinor },
                { "patch",          TokenType::kPatch },
                { "struct",         TokenType::kStruct },
                { "enumeration",    TokenType::kEnumeration },
                { "typedef",        TokenType::kTypedef },
                { "is",             TokenType::kIs },
                { "array",          TokenType::kArray },
                { "of",             TokenType::kOf },
                { "map",            TokenType::kMap },
                { "to",             TokenType::kTo },
                { "union",          TokenType::kUnion },
                { "method",         TokenType::kMethod },
                { "broadcast",      TokenType::kBroadcast },
                { "attribute",      TokenType::kAttribute },
                { "in",             TokenType::kIn },
                { "out",            TokenType::kOut },
                { "error",          TokenType::kError },
                { "fireAndForget",    TokenType::kFireAndForget },
                { "readonly",         TokenType::kReadonly },
                { "notify",           TokenType::kNotify },
                { "noSubscriptions",  TokenType::kNoSubscriptions },
                { "extends",          TokenType::kExtends },
                { "const",            TokenType::kConst },
            };
            return kMap;
        }
    } // anonymous namespace

    // ==================== TokenTypeName ====================

    const Char* TokenTypeName( TokenType type ) noexcept {
        switch ( type ) {
            case TokenType::kPackage:        return "package";
            case TokenType::kImport:         return "import";
            case TokenType::kTypeCollection: return "typeCollection";
            case TokenType::kInterface:      return "interface";
            case TokenType::kVersion:        return "version";
            case TokenType::kMajor:          return "major";
            case TokenType::kMinor:          return "minor";
            case TokenType::kPatch:          return "patch";
            case TokenType::kStruct:         return "struct";
            case TokenType::kEnumeration:    return "enumeration";
            case TokenType::kTypedef:        return "typedef";
            case TokenType::kIs:             return "is";
            case TokenType::kArray:          return "array";
            case TokenType::kOf:             return "of";
            case TokenType::kMap:            return "map";
            case TokenType::kTo:             return "to";
            case TokenType::kUnion:          return "union";
            case TokenType::kMethod:         return "method";
            case TokenType::kBroadcast:      return "broadcast";
            case TokenType::kAttribute:      return "attribute";
            case TokenType::kIn:             return "in";
            case TokenType::kOut:            return "out";
            case TokenType::kError:          return "error";
            case TokenType::kFireAndForget:    return "fireAndForget";
            case TokenType::kReadonly:         return "readonly";
            case TokenType::kNotify:           return "notify";
            case TokenType::kNoSubscriptions:  return "noSubscriptions";
            case TokenType::kExtends:          return "extends";
            case TokenType::kConst:            return "const";
            case TokenType::kIdentifier:     return "identifier";
            case TokenType::kIntegerLiteral: return "integer";
            case TokenType::kStringLiteral:  return "string";
            case TokenType::kLBrace:         return "{";
            case TokenType::kRBrace:         return "}";
            case TokenType::kLBracket:       return "[";
            case TokenType::kRBracket:       return "]";
            case TokenType::kLParen:         return "(";
            case TokenType::kRParen:         return ")";
            case TokenType::kDot:            return ".";
            case TokenType::kComma:          return ",";
            case TokenType::kEquals:         return "=";
            case TokenType::kSemicolon:      return ";";
            case TokenType::kEof:            return "EOF";
            case TokenType::kUnknown:        return "unknown";
        }
        return "unknown";
    }

    // ==================== CFidlLexer Implementation ====================

    CFidlLexer::CFidlLexer( const String& source, const String& filename ) noexcept
        : m_source( source )
        , m_filename( filename )
        , m_pos( 0 )
        , m_line( 1 )
        , m_column( 1 ) {
    }

    ::std::vector< Token > CFidlLexer::Tokenize() {
        ::std::vector< Token > tokens;
        tokens.reserve( m_source.size() / 4 );  // rough estimate

        while ( !isAtEnd() ) {
            skipWhitespace();
            if ( isAtEnd() ) {
                break;
            }

            Char c = peek();

            // ==================== Comments ====================
            if ( c == '/' && !isAtEnd() ) {
                Char next = peekNext();
                if ( next == '/' ) {
                    skipLineComment();
                    continue;
                }
                if ( next == '*' ) {
                    skipBlockComment();
                    continue;
                }
            }

            // ==================== Symbols ====================
            switch ( c ) {
                case '{':
                    tokens.push_back( makeToken( TokenType::kLBrace, "{" ) );
                    advance();
                    continue;
                case '}':
                    tokens.push_back( makeToken( TokenType::kRBrace, "}" ) );
                    advance();
                    continue;
                case '[':
                    tokens.push_back( makeToken( TokenType::kLBracket, "[" ) );
                    advance();
                    continue;
                case ']':
                    tokens.push_back( makeToken( TokenType::kRBracket, "]" ) );
                    advance();
                    continue;
                case '(':
                    tokens.push_back( makeToken( TokenType::kLParen, "(" ) );
                    advance();
                    continue;
                case ')':
                    tokens.push_back( makeToken( TokenType::kRParen, ")" ) );
                    advance();
                    continue;
                case '.':
                    tokens.push_back( makeToken( TokenType::kDot, "." ) );
                    advance();
                    continue;
                case ',':
                    tokens.push_back( makeToken( TokenType::kComma, "," ) );
                    advance();
                    continue;
                case '=':
                    tokens.push_back( makeToken( TokenType::kEquals, "=" ) );
                    advance();
                    continue;
                case ';':
                    tokens.push_back( makeToken( TokenType::kSemicolon, ";" ) );
                    advance();
                    continue;
                default:
                    break;
            }

            // ==================== Identifier or Keyword ====================
            if ( ::std::isalpha( static_cast< unsigned char >( c ) ) || c == '_' ) {
                tokens.push_back( readIdentifierOrKeyword() );
                continue;
            }

            // ==================== Number ====================
            if ( ::std::isdigit( static_cast< unsigned char >( c ) ) ) {
                tokens.push_back( readNumber() );
                continue;
            }

            // Negative number (minus followed by digit)
            if ( c == '-' && !isAtEnd() ) {
                Char next = peekNext();
                if ( ::std::isdigit( static_cast< unsigned char >( next ) ) ) {
                    tokens.push_back( readNumber() );
                    continue;
                }
            }

            // ==================== String Literal ====================
            if ( c == '"' ) {
                tokens.push_back( readString() );
                continue;
            }

            // ==================== Unknown ====================
            throw LexerError( String( "Unexpected character '" ) + c + "'",
                              m_filename, m_line, m_column );
        }

        tokens.push_back( makeToken( TokenType::kEof, "" ) );
        return tokens;
    }

    // ==================== Whitespace and Comments ====================

    void CFidlLexer::skipWhitespace() noexcept {
        while ( !isAtEnd() ) {
            Char c = peek();
            if ( c == ' ' || c == '\t' || c == '\r' ) {
                advance();
            } else if ( c == '\n' ) {
                advance();
                // line/column already updated in advance()
            } else {
                break;
            }
        }
    }

    void CFidlLexer::skipLineComment() noexcept {
        // Skip "//"
        advance();
        advance();
        while ( !isAtEnd() && peek() != '\n' ) {
            advance();
        }
    }

    void CFidlLexer::skipBlockComment() {
        UInt32 startLine = m_line;
        UInt32 startCol  = m_column;

        // Skip "/*"
        advance();
        advance();

        while ( !isAtEnd() ) {
            if ( peek() == '*' && peekNext() == '/' ) {
                advance();  // skip *
                advance();  // skip /
                return;
            }
            advance();
        }

        throw LexerError( "Unterminated block comment",
                          m_filename, startLine, startCol );
    }

    // ==================== Token Readers ====================

    Token CFidlLexer::readIdentifierOrKeyword() noexcept {
        UInt32 startCol = m_column;
        String value;

        while ( !isAtEnd() ) {
            Char c = peek();
            if ( ::std::isalnum( static_cast< unsigned char >( c ) ) || c == '_' ) {
                value += advance();
            } else {
                break;
            }
        }

        // Check keyword map
        const auto& keywords = getKeywordMap();
        auto it = keywords.find( value );
        if ( it != keywords.end() ) {
            Token tok;
            tok.type   = it->second;
            tok.value  = value;
            tok.line   = m_line;
            tok.column = startCol;
            return tok;
        }

        Token tok;
        tok.type   = TokenType::kIdentifier;
        tok.value  = value;
        tok.line   = m_line;
        tok.column = startCol;
        return tok;
    }

    Token CFidlLexer::readNumber() {
        UInt32 startCol = m_column;
        String value;

        // Handle negative sign
        if ( peek() == '-' ) {
            value += advance();
        }

        // Handle hex prefix 0x
        if ( peek() == '0' && peekNext() == 'x' ) {
            value += advance();  // '0'
            value += advance();  // 'x'
            while ( !isAtEnd() && ::std::isxdigit( static_cast< unsigned char >( peek() ) ) ) {
                value += advance();
            }
        } else {
            // Decimal
            while ( !isAtEnd() && ::std::isdigit( static_cast< unsigned char >( peek() ) ) ) {
                value += advance();
            }
        }

        if ( value.empty() || value == "-" ) {
            throw LexerError( "Invalid number literal",
                              m_filename, m_line, startCol );
        }

        Token tok;
        tok.type   = TokenType::kIntegerLiteral;
        tok.value  = value;
        tok.line   = m_line;
        tok.column = startCol;
        return tok;
    }

    Token CFidlLexer::readString() {
        UInt32 startLine = m_line;
        UInt32 startCol  = m_column;

        advance();  // skip opening quote
        String value;

        while ( !isAtEnd() && peek() != '"' ) {
            if ( peek() == '\\' ) {
                advance();  // skip backslash
                if ( isAtEnd() ) {
                    throw LexerError( "Unterminated string escape",
                                      m_filename, startLine, startCol );
                }
                Char escaped = advance();
                switch ( escaped ) {
                    case 'n':  value += '\n'; break;
                    case 't':  value += '\t'; break;
                    case '\\': value += '\\'; break;
                    case '"':  value += '"';  break;
                    default:   value += escaped; break;
                }
            } else if ( peek() == '\n' ) {
                throw LexerError( "Unterminated string literal (newline in string)",
                                  m_filename, startLine, startCol );
            } else {
                value += advance();
            }
        }

        if ( isAtEnd() ) {
            throw LexerError( "Unterminated string literal",
                              m_filename, startLine, startCol );
        }

        advance();  // skip closing quote

        Token tok;
        tok.type   = TokenType::kStringLiteral;
        tok.value  = value;
        tok.line   = startLine;
        tok.column = startCol;
        return tok;
    }

    // ==================== Token Helpers ====================

    Token CFidlLexer::makeToken( TokenType type, const String& value ) const noexcept {
        Token tok;
        tok.type   = type;
        tok.value  = value;
        tok.line   = m_line;
        tok.column = m_column;
        return tok;
    }

    Bool CFidlLexer::isAtEnd() const noexcept {
        return m_pos >= static_cast< UInt32 >( m_source.size() );
    }

    Char CFidlLexer::peek() const noexcept {
        if ( isAtEnd() ) { return '\0'; }
        return m_source[m_pos];
    }

    Char CFidlLexer::peekNext() const noexcept {
        if ( m_pos + 1 >= static_cast< UInt32 >( m_source.size() ) ) { return '\0'; }
        return m_source[m_pos + 1];
    }

    Char CFidlLexer::advance() noexcept {
        if ( isAtEnd() ) { return '\0'; }
        Char c = m_source[m_pos++];
        if ( c == '\n' ) {
            ++m_line;
            m_column = 1;
        } else {
            ++m_column;
        }
        return c;
    }

} // namespace generator
} // namespace com
} // namespace lap
