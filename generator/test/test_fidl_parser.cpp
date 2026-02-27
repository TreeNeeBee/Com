/**
 * @file        test_fidl_parser.cpp
 * @author      Aii
 * @brief       Unit tests for CFidlLexer, CFidlParser, CSchemaHash, and all generators
 * @date        2026/02/09
 * @details     4 self-contained test functions (no external test framework).
 *              Tests run in order: TestLexer, TestParser, TestSchemaHash, TestGenerators.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CFidlLexer.hpp"
#include "CFidlParser.hpp"
#include "CSchemaHash.hpp"
#include "CTypesGenerator.hpp"
#include "CProxyGenerator.hpp"
#include "CSkeletonGenerator.hpp"
#include "CDdsIdlGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ==================== Namespace Alias ====================
namespace gen = ::lap::com::generator;

// ==================== Selective Type Imports ====================
using gen::Bool;
using gen::String;
using gen::UInt16;
using gen::FidlModel;
using gen::GeneratorConfig;
using gen::CFidlLexer;
using gen::CFidlParser;
using gen::CSchemaHash;
using gen::CTypesGenerator;
using gen::CProxyGenerator;
using gen::CSkeletonGenerator;
using gen::CDdsIdlGenerator;
using gen::TokenType;

// ==================== Test Helpers ====================
namespace
{

void TESTAssert( Bool condition, const char* msg )
{
    if ( !condition ) {
        ::std::cerr << "FAIL: " << msg << "\n";
        ::std::exit( 1 );
    }
}

String ReadFileContents( const String& path )
{
    ::std::ifstream ifs( path );
    if ( !ifs.is_open() ) { return ""; }
    ::std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

String LocateFidl()
{
    const ::std::vector< String > candidates = {
        "test/Calculator.fidl",
        "../test/Calculator.fidl",
    };
    for ( const auto& p : candidates ) {
        if ( ::std::filesystem::exists( p ) ) { return p; }
    }
    return "";
}

String GetOutputDir()
{
    // Use build directory (cwd when CTest runs the binary)
    return ::std::filesystem::current_path().string() + "/gen_test_output";
}

} // anonymous namespace

// ==================== Test Declarations ====================
void TestLexer();
void TestParser();
void TestSchemaHash();
void TestGenerators();

// ==================== main ====================
int main()
{
    ::std::cout << "=== test_fidl_parser ===\n";

    ::std::cout << "[1/4] TestLexer ... ";
    TestLexer();
    ::std::cout << "PASS\n";

    ::std::cout << "[2/4] TestParser ... ";
    TestParser();
    ::std::cout << "PASS\n";

    ::std::cout << "[3/4] TestSchemaHash ... ";
    TestSchemaHash();
    ::std::cout << "PASS\n";

    ::std::cout << "[4/4] TestGenerators ... ";
    TestGenerators();
    ::std::cout << "PASS\n";

    ::std::cout << "All 4 tests passed.\n";
    return 0;
}

// ==================== TestLexer ====================
void TestLexer()
{
    const String fidlPath = LocateFidl();
    TESTAssert( !fidlPath.empty(), "Calculator.fidl must be locatable" );
    const String source   = ReadFileContents( fidlPath );
    TESTAssert( !source.empty(), "Calculator.fidl should be readable" );

    CFidlLexer lexer( source, fidlPath );
    auto tokens = lexer.Tokenize();

    TESTAssert( tokens.size() > 10,
        "Lexer should produce > 10 tokens from Calculator.fidl" );

    TESTAssert( tokens.back().type == TokenType::kEof,
        "Last token must be kEof" );

    Bool foundPackage   = false;
    Bool foundInterface = false;
    Bool foundMethod    = false;

    for ( const auto& tok : tokens ) {
        if ( tok.type == TokenType::kPackage )   { foundPackage   = true; }
        if ( tok.type == TokenType::kInterface ) { foundInterface = true; }
        if ( tok.type == TokenType::kMethod )    { foundMethod    = true; }
    }

    TESTAssert( foundPackage,   "Lexer should emit kPackage token" );
    TESTAssert( foundInterface, "Lexer should emit kInterface token" );
    TESTAssert( foundMethod,    "Lexer should emit kMethod token" );
}

// ==================== TestParser ====================
void TestParser()
{
    const String fidlPath = LocateFidl();
    TESTAssert( !fidlPath.empty(), "Calculator.fidl must be locatable" );
    const String source   = ReadFileContents( fidlPath );
    TESTAssert( !source.empty(), "Calculator.fidl should be readable" );

    CFidlLexer  lexer( source, fidlPath );
    auto tokens = lexer.Tokenize();

    CFidlParser parser( tokens, fidlPath );
    auto model = parser.Parse();

    TESTAssert( model.packageName == "org.lap.examples",
        "packageName should be 'org.lap.examples'" );

    TESTAssert( model.typeCollections.size() == 1,
        "Should have exactly 1 typeCollection" );

    TESTAssert( model.interfaces.size() == 1,
        "Should have exactly 1 interface" );

    const auto& iface = model.interfaces[0];
    TESTAssert( iface.name == "Calculator",
        "Interface name should be 'Calculator'" );

    const auto& tc = model.typeCollections[0];
    TESTAssert( tc.name == "CalculatorTypes",
        "TypeCollection name should be 'CalculatorTypes'" );

    TESTAssert( tc.enums.size() >= 1,
        "TypeCollection should have at least 1 enumeration" );

    Bool foundErrorCode = false;
    for ( const auto& en : tc.enums ) {
        if ( en.name == "ErrorCode" ) {
            TESTAssert( en.enumerators.size() == 4,
                "ErrorCode should have 4 enumerators" );
            foundErrorCode = true;
        }
    }
    TESTAssert( foundErrorCode, "TypeCollection should contain ErrorCode enum" );

    TESTAssert( tc.structs.size() >= 2,
        "TypeCollection should have at least 2 structs" );

    Bool foundOperand = false, foundOpResult = false;
    for ( const auto& s : tc.structs ) {
        if ( s.name == "Operand" )         { foundOperand  = true; }
        if ( s.name == "OperationResult" ) { foundOpResult = true; }
    }
    TESTAssert( foundOperand,  "TypeCollection should contain Operand struct" );
    TESTAssert( foundOpResult, "TypeCollection should contain OperationResult struct" );

    TESTAssert( tc.typedefs.size() >= 1,
        "TypeCollection should have at least 1 typedef" );

    Bool foundOperandList = false;
    for ( const auto& td : tc.typedefs ) {
        if ( td.name == "OperandList" ) { foundOperandList = true; }
    }
    TESTAssert( foundOperandList, "TypeCollection should contain OperandList typedef" );

    TESTAssert( iface.methods.size() == 4,
        "Calculator should have exactly 4 methods" );

    Bool foundReset       = false;
    Bool foundDivide      = false;
    Bool divideHasError   = false;

    for ( const auto& m : iface.methods ) {
        if ( m.name == "reset" && m.isFireAndForget ) {
            foundReset = true;
        }
        if ( m.name == "divide" ) {
            foundDivide    = true;
            divideHasError = !m.errorArgs.empty();
        }
    }
    TESTAssert( foundReset,     "Calculator should have fireAndForget method 'reset'" );
    TESTAssert( foundDivide,    "Calculator should have method 'divide'" );
    TESTAssert( divideHasError, "divide should have error arguments" );

    TESTAssert( iface.broadcasts.size() == 1,
        "Calculator should have exactly 1 broadcast" );
    TESTAssert( iface.broadcasts[0].name == "resultReady",
        "Broadcast name should be 'resultReady'" );
    TESTAssert( iface.broadcasts[0].outArgs.size() == 2,
        "resultReady should have 2 outArgs" );

    TESTAssert( iface.attributes.size() == 1,
        "Calculator should have exactly 1 attribute" );
    TESTAssert( iface.attributes[0].name == "lastResult",
        "Attribute name should be 'lastResult'" );
    TESTAssert( iface.attributes[0].isReadonly,
        "lastResult should be readonly" );
}

// ==================== TestSchemaHash ====================
void TestSchemaHash()
{
    const String fidlPath = LocateFidl();
    TESTAssert( !fidlPath.empty(), "Calculator.fidl must be locatable" );
    const String source   = ReadFileContents( fidlPath );
    TESTAssert( !source.empty(), "Calculator.fidl should be readable" );

    CFidlLexer  lexer( source, fidlPath );
    auto tokens = lexer.Tokenize();

    CFidlParser parser( tokens, fidlPath );
    auto model = parser.Parse();

    const String hash1 = CSchemaHash::Compute( model );
    const String hash2 = CSchemaHash::Compute( model );
    TESTAssert( hash1 == hash2, "SchemaHash must be deterministic" );

    TESTAssert( hash1.size() == 16,
        "SchemaHash should be 16 hex characters" );

    Bool allHex = true;
    for ( auto c : hash1 ) {
        if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) ) {
            allHex = false;
        }
    }
    TESTAssert( allHex, "SchemaHash should contain only lowercase hex digits" );

    // ServiceID derived from qualified name
    const String qualName = model.packageName + "." + model.interfaces[0].name;
    UInt16 serviceId = CSchemaHash::GenerateServiceId( qualName );
    TESTAssert( serviceId >= 1 && serviceId <= 1022,
        "ServiceID must be in range [1, 1022]" );
}

// ==================== TestGenerators ====================
void TestGenerators()
{
    const String fidlPath = LocateFidl();
    const String outDir   = GetOutputDir();
    const String source   = ReadFileContents( fidlPath );
    TESTAssert( !source.empty(), "Calculator.fidl should be readable" );

    ::std::filesystem::create_directories( outDir );

    CFidlLexer  lexer( source, fidlPath );
    auto tokens = lexer.Tokenize();

    CFidlParser parser( tokens, fidlPath );
    auto model = parser.Parse();

    GeneratorConfig cfg;
    cfg.outputDir         = outDir;
    cfg.namespacePrefix   = "lap::com";
    cfg.author            = "TestAuthor";
    cfg.schemaHashOverride = CSchemaHash::Compute( model );
    cfg.versionOverride   = "";
    cfg.comConfigPath     = "";
    cfg.serviceDeployPath = "";
    cfg.slotMappingPath   = "";
    cfg.serviceIdOverride  = 0;
    cfg.instanceIdOverride = 0;
    cfg.generateProxy    = true;
    cfg.generateSkeleton = true;
    cfg.generateTypes    = true;
    cfg.generateDdsIdl   = true;

    CTypesGenerator    typesGen;
    CProxyGenerator    proxyGen;
    CSkeletonGenerator skelGen;
    CDdsIdlGenerator   idlGen;

    TESTAssert( typesGen.Generate( model, cfg ),
        "CTypesGenerator::Generate should succeed" );
    TESTAssert( proxyGen.Generate( model, cfg ),
        "CProxyGenerator::Generate should succeed" );
    TESTAssert( skelGen.Generate( model, cfg ),
        "CSkeletonGenerator::Generate should succeed" );
    TESTAssert( idlGen.Generate( model, cfg ),
        "CDdsIdlGenerator::Generate should succeed" );

    const String typesFile = outDir + "/CalculatorTypes.hpp";
    const String proxyFile = outDir + "/CalculatorProxy.hpp";
    const String skelFile  = outDir + "/CalculatorSkeleton.hpp";
    const String idlFile   = outDir + "/Calculator.idl";

    TESTAssert( ::std::filesystem::exists( typesFile ),
        "Calculator_types.hpp should be created" );
    TESTAssert( ::std::filesystem::exists( proxyFile ),
        "Calculator_proxy.hpp should be created" );
    TESTAssert( ::std::filesystem::exists( skelFile ),
        "Calculator_skeleton.hpp should be created" );
    TESTAssert( ::std::filesystem::exists( idlFile ),
        "Calculator.idl should be created" );

    const String typesContent = ReadFileContents( typesFile );
    TESTAssert( typesContent.find( "enum class ErrorCode" ) != String::npos,
        "Types file should contain 'enum class ErrorCode'" );
    TESTAssert( typesContent.find( "struct Operand" ) != String::npos,
        "Types file should contain 'struct Operand'" );
    TESTAssert( typesContent.find( "kOk" ) != String::npos,
        "Types file should contain 'kOk' enumerator" );
    TESTAssert( typesContent.find( "kDivideByZero" ) != String::npos,
        "Types file should contain 'kDivideByZero' enumerator" );
    TESTAssert( typesContent.find( "kSchemaHash" ) != String::npos,
        "Types file should contain 'kSchemaHash' constant" );

    const String proxyContent = ReadFileContents( proxyFile );
    TESTAssert( proxyContent.find( "proxy" ) != String::npos,
        "Proxy file should contain proxy namespace" );
    TESTAssert( proxyContent.find( "events" ) != String::npos,
        "Proxy file should reference events namespace" );
    TESTAssert( proxyContent.find( "methods" ) != String::npos,
        "Proxy file should reference methods namespace" );

    const String skelContent = ReadFileContents( skelFile );
    TESTAssert( skelContent.find( "skeleton" ) != String::npos,
        "Skeleton file should contain skeleton namespace" );

    const String idlContent = ReadFileContents( idlFile );
    TESTAssert( idlContent.find( "SCHEMA_HASH" ) != String::npos,
        "IDL file should contain SCHEMA_HASH constant" );
}
