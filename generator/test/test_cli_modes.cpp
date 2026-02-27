/**
 * @file        test_cli_modes.cpp
 * @author      Aii
 * @brief       CLI integration tests for lap-sidl-gen
 * @date        2026/02/09
 * @details     Tests the --validate, --hash-only, --schema-hash, --version-string CLI modes,
 *              --com-config, --service-deploy, --slot-mapping, --instance-id QoS config paths,
 *              and the config override paths in the generators.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * <tr><td>2026/02/09  <td>1.1      <td>Aii     <td>add QoS XML + YAML config CLI tests
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CFidlAst.hpp"
#include "CFidlLexer.hpp"
#include "CFidlParser.hpp"
#include "CSchemaHash.hpp"
#include "CTypesGenerator.hpp"
#include "CProxyGenerator.hpp"
#include "CSkeletonGenerator.hpp"
#include "CDdsIdlGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ==================== Namespace Alias ====================
namespace gen = ::lap::com::generator;

// ==================== Selective Type Imports ====================
using gen::Bool;
using gen::Char;
using gen::Int32;
using gen::UInt16;
using gen::String;
using gen::FidlModel;
using gen::GeneratorConfig;
using gen::CFidlLexer;
using gen::CFidlParser;
using gen::CSchemaHash;
using gen::CQosLoader;
using gen::CProxyGenerator;
using gen::CSkeletonGenerator;
using gen::CDdsIdlGenerator;

// ==================== Test Macros ====================

#define TEST_ASSERT( expr ) \
    do { \
        if ( !( expr ) ) { \
            ::std::cerr << "FAIL: " << #expr \
                        << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while( false )

#define TEST_ASSERT_EQ( a, b ) \
    do { \
        if ( ( a ) != ( b ) ) { \
            ::std::cerr << "FAIL: " << #a << " == " << #b \
                        << "  (actual: " << ( a ) << " vs " << ( b ) << ")" \
                        << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while( false )

// ==================== Helpers ====================
namespace
{

String ReadFile( const String& path ) {
    ::std::ifstream ifs( path, ::std::ios::binary );
    if ( !ifs.is_open() ) {
        return "";
    }
    ::std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

Bool FileExists( const String& path ) {
    ::std::ifstream ifs( path );
    return ifs.good();
}

/**
 * @brief Execute a shell command and capture stdout
 * @param cmd Command to execute
 * @return Pair of (exit_code, stdout_output)
 */
::std::pair< Int32, String > ExecCommand( const String& cmd ) {
    ::std::array< Char, 4096 > buffer{};
    String output;

    String fullCmd = cmd + " 2>&1";
    FILE* pipe = ::popen( fullCmd.c_str(), "r" );
    if ( pipe == nullptr ) {
        return { -1, "" };
    }

    while ( ::fgets( buffer.data(), static_cast< Int32 >( buffer.size() ), pipe ) != nullptr ) {
        output += buffer.data();
    }

    Int32 status = ::pclose( pipe );
    Int32 exitCode = WEXITSTATUS( status );
    return { exitCode, output };
}

/// @brief Locate the lap_sidl_gen binary (same directory as test binary)
String gBinaryPath;
/// @brief Locate the test .fidl file
String gFidlPath;

Bool LocateBinary() {
    // Try common locations
    const ::std::vector< String > candidates = {
        "./lap_sidl_gen",
        "../lap_sidl_gen",
        "lap_sidl_gen",
    };

    for ( const auto& path : candidates ) {
        auto [rc, out] = ExecCommand( path + " --version" );
        if ( rc == 0 && out.find( "lap-sidl-gen" ) != String::npos ) {
            gBinaryPath = path;
            return true;
        }
    }
    return false;
}

Bool LocateFidl() {
    const ::std::vector< String > candidates = {
        "test/Calculator.fidl",
        "../test/Calculator.fidl",
    };

    for ( const auto& path : candidates ) {
        if ( FileExists( path ) ) {
            gFidlPath = path;
            return true;
        }
    }
    return false;
}

FidlModel ParseFidl( const String& path ) {
    String source = ReadFile( path );
    CFidlLexer lexer( source, path );
    auto tokens = lexer.Tokenize();
    CFidlParser parser( tokens, path );
    FidlModel model = parser.Parse();
    model.sourceFile = path;
    return model;
}

} // anonymous namespace

// ======================================================================
// Test: --validate mode
// ======================================================================
namespace
{

Bool TestValidateOk() {
    ::std::cout << "  [--validate] Valid .fidl...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " -i " + gFidlPath + " --validate" );

    TEST_ASSERT_EQ( rc, 0 );
    TEST_ASSERT( out.find( "OK:" ) != String::npos );
    TEST_ASSERT( out.find( "Package:" ) != String::npos );
    TEST_ASSERT( out.find( "Interfaces:" ) != String::npos );

    ::std::cout << "  [--validate] OK\n";
    return true;
}

Bool TestValidateBadFile() {
    ::std::cout << "  [--validate] Non-existent .fidl...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " -i /tmp/nonexistent_xyz.fidl --validate" );

    TEST_ASSERT( rc != 0 );

    ::std::cout << "  [--validate bad file] OK — exit code " << rc << "\n";
    return true;
}

Bool TestValidateNoOutput() {
    ::std::cout << "  [--validate] No --output required...\n";

    // --validate should NOT require --output
    auto [rc, out] = ExecCommand( gBinaryPath + " -i " + gFidlPath + " --validate" );

    TEST_ASSERT_EQ( rc, 0 );
    // Ensure it didn't generate any files
    TEST_ASSERT( out.find( "Generating" ) == String::npos );

    ::std::cout << "  [--validate no output] OK\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Test: --hash-only mode
// ======================================================================
namespace
{

Bool TestHashOnly() {
    ::std::cout << "  [--hash-only] Compute hash...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " -i " + gFidlPath + " --hash-only" );

    TEST_ASSERT_EQ( rc, 0 );

    // Output should be EXACTLY the 16-char hex hash + newline
    // Trim trailing whitespace
    while ( !out.empty() && ( out.back() == '\n' || out.back() == '\r' || out.back() == ' ' ) ) {
        out.pop_back();
    }
    TEST_ASSERT_EQ( out.size(), static_cast< ::std::size_t >( 16 ) );

    // Should contain only hex characters
    for ( Char c : out ) {
        Bool isHex = ( c >= '0' && c <= '9' )
                  || ( c >= 'a' && c <= 'f' )
                  || ( c >= 'A' && c <= 'F' );
        TEST_ASSERT( isHex );
    }

    // Should NOT contain any banner/header text
    auto [rc2, out2] = ExecCommand( gBinaryPath + " -i " + gFidlPath + " --hash-only" );
    while ( !out2.empty() && ( out2.back() == '\n' || out2.back() == '\r' || out2.back() == ' ' ) ) {
        out2.pop_back();
    }
    TEST_ASSERT( out2.find( "lap-sidl-gen" ) == String::npos );

    ::std::cout << "  [--hash-only] OK — hash: " << out << "\n";
    return true;
}

Bool TestHashDeterministic() {
    ::std::cout << "  [--hash-only] Deterministic...\n";

    auto [rc1, out1] = ExecCommand( gBinaryPath + " -i " + gFidlPath + " --hash-only" );
    auto [rc2, out2] = ExecCommand( gBinaryPath + " -i " + gFidlPath + " --hash-only" );

    TEST_ASSERT_EQ( rc1, 0 );
    TEST_ASSERT_EQ( rc2, 0 );
    TEST_ASSERT_EQ( out1, out2 );

    // Also verify it matches the library-level computation
    FidlModel model = ParseFidl( gFidlPath );
    String libHash = CSchemaHash::Compute( model );

    // Trim CLI output
    while ( !out1.empty() && ( out1.back() == '\n' || out1.back() == '\r' || out1.back() == ' ' ) ) {
        out1.pop_back();
    }
    TEST_ASSERT_EQ( out1, libHash );

    ::std::cout << "  [--hash-only deterministic] OK\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Test: --schema-hash injection
// ======================================================================
namespace
{

Bool TestSchemaHashInjectDdsIdl() {
    ::std::cout << "  [--schema-hash] Inject into OMG IDL...\n";

    String outDir = "/tmp/lap_test_cli_hash";
    String injectedHash = "DEADBEEF12345678";

    auto [rc, out] = ExecCommand(
        gBinaryPath + " -i " + gFidlPath
        + " -o " + outDir
        + " --dds-idl"
        + " --schema-hash " + injectedHash );

    TEST_ASSERT_EQ( rc, 0 );

    // Both IDL and QoS XML must be generated
    String idlContent = ReadFile( outDir + "/Calculator.idl" );
    TEST_ASSERT( !idlContent.empty() );
    TEST_ASSERT( idlContent.find( injectedHash ) != String::npos );
    TEST_ASSERT( idlContent.find( "SCHEMA_HASH" ) != String::npos );

    String qosContent = ReadFile( outDir + "/Calculator_qos.xml" );
    TEST_ASSERT( !qosContent.empty() );
    TEST_ASSERT( qosContent.find( "<profiles" ) != String::npos );

    ::std::cout << "  [--schema-hash inject] OK\n";
    return true;
}

Bool TestSchemaHashInjectProxy() {
    ::std::cout << "  [--schema-hash] Inject into Proxy...\n";

    String outDir = "/tmp/lap_test_cli_hash";
    String injectedHash = "AABBCCDD11223344";

    auto [rc, out] = ExecCommand(
        gBinaryPath + " -i " + gFidlPath
        + " -o " + outDir
        + " --proxy"
        + " --schema-hash " + injectedHash );

    TEST_ASSERT_EQ( rc, 0 );

    // Read the generated Proxy header
    String proxyContent = ReadFile( outDir + "/CalculatorProxy.hpp" );
    TEST_ASSERT( !proxyContent.empty() );

    // Injected hash should appear
    TEST_ASSERT( proxyContent.find( injectedHash ) != String::npos );

    // [SWS_CM_00004] Proxy must be final
    TEST_ASSERT( proxyContent.find( "final" ) != String::npos );
    // [SWS_CM_01007] proxy namespace present
    TEST_ASSERT( proxyContent.find( "namespace proxy" ) != String::npos );

    ::std::cout << "  [--schema-hash proxy] OK\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Test: --version-string override
// ======================================================================
namespace
{

Bool TestVersionStringOverride() {
    ::std::cout << "  [--version-string] Override OMG IDL version...\n";

    String outDir = "/tmp/lap_test_cli_ver";
    String customVersion = "9.8.7";

    auto [rc, out] = ExecCommand(
        gBinaryPath + " -i " + gFidlPath
        + " -o " + outDir
        + " --dds-idl"
        + " --version-string " + customVersion );

    TEST_ASSERT_EQ( rc, 0 );

    String idlContent = ReadFile( outDir + "/Calculator.idl" );
    TEST_ASSERT( !idlContent.empty() );
    TEST_ASSERT( idlContent.find( "Version: " + customVersion ) != String::npos );
    // Note: @version("...") annotation is not emitted — standalone module-level
    // annotations are invalid OMG IDL 4.2; the version appears in the header comment.

    // QoS XML must also be generated
    String qosContent = ReadFile( outDir + "/Calculator_qos.xml" );
    TEST_ASSERT( !qosContent.empty() );
    TEST_ASSERT( qosContent.find( "<profiles" ) != String::npos );

    ::std::cout << "  [--version-string] OK\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Test: Library-level config overrides
// ======================================================================
namespace
{

Bool TestConfigSchemaHashOverride() {
    ::std::cout << "  [Config] schemaHashOverride in generator...\n";

    FidlModel model = ParseFidl( gFidlPath );

    String customHash = "CAFE0123BABE4567";
    GeneratorConfig config;
    config.outputDir          = "/tmp/lap_test_cfg_hash";
    config.author             = "TestBot";
    config.schemaHashOverride = customHash;

    CProxyGenerator proxyGen;
    TEST_ASSERT( proxyGen.Generate( model, config ) );
    String proxyContent = ReadFile( config.outputDir + "/CalculatorProxy.hpp" );
    TEST_ASSERT( proxyContent.find( customHash ) != String::npos );

    CSkeletonGenerator skelGen;
    TEST_ASSERT( skelGen.Generate( model, config ) );
    String skelContent = ReadFile( config.outputDir + "/CalculatorSkeleton.hpp" );
    TEST_ASSERT( skelContent.find( customHash ) != String::npos );

    CDdsIdlGenerator ddsGen;
    TEST_ASSERT( ddsGen.Generate( model, config ) );
    String idlContent = ReadFile( config.outputDir + "/Calculator.idl" );
    TEST_ASSERT( idlContent.find( customHash ) != String::npos );

    // QoS XML is always generated alongside IDL
    String qosContent = ReadFile( config.outputDir + "/Calculator_qos.xml" );
    TEST_ASSERT( !qosContent.empty() );
    TEST_ASSERT( qosContent.find( "<profiles" ) != String::npos );

    ::std::cout << "  [Config schemaHashOverride] OK\n";
    return true;
}

Bool TestConfigVersionOverride() {
    ::std::cout << "  [Config] versionOverride in OMG IDL generator...\n";

    FidlModel model = ParseFidl( gFidlPath );

    GeneratorConfig config;
    config.outputDir       = "/tmp/lap_test_cfg_ver";
    config.author          = "TestBot";
    config.versionOverride = "42.0.1";

    CDdsIdlGenerator ddsGen;
    TEST_ASSERT( ddsGen.Generate( model, config ) );

    String idlContent = ReadFile( config.outputDir + "/Calculator.idl" );
    TEST_ASSERT( !idlContent.empty() );
    TEST_ASSERT( idlContent.find( "Version: 42.0.1" ) != String::npos );
    // Note: @version("...") annotation is not emitted — standalone module-level
    // annotations are invalid OMG IDL 4.2; the version appears in the header comment.

    // QoS XML always paired
    TEST_ASSERT( FileExists( config.outputDir + "/Calculator_qos.xml" ) );

    ::std::cout << "  [Config versionOverride] OK\n";
    return true;
}

Bool TestConfigNoOverrideUsesAuto() {
    ::std::cout << "  [Config] No override = auto-computed hash...\n";

    FidlModel model = ParseFidl( gFidlPath );
    String autoHash = CSchemaHash::Compute( model );

    GeneratorConfig config;
    config.outputDir = "/tmp/lap_test_cfg_auto";
    config.author    = "TestBot";

    CDdsIdlGenerator ddsGen;
    TEST_ASSERT( ddsGen.Generate( model, config ) );

    String idlContent = ReadFile( config.outputDir + "/Calculator.idl" );
    TEST_ASSERT( idlContent.find( autoHash ) != String::npos );

    // QoS XML always paired
    TEST_ASSERT( FileExists( config.outputDir + "/Calculator_qos.xml" ) );

    ::std::cout << "  [Config auto hash] OK — " << autoHash << "\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Test: Float64 type mapping
// ======================================================================
namespace
{

Bool TestFloat64Mapping() {
    ::std::cout << "  [Float64] OMG IDL type mapping...\n";

    FidlModel model = ParseFidl( gFidlPath );

    GeneratorConfig config;
    config.outputDir = "/tmp/lap_test_float64";
    config.author    = "TestBot";

    CDdsIdlGenerator ddsGen;
    TEST_ASSERT( ddsGen.Generate( model, config ) );

    String idlContent = ReadFile( config.outputDir + "/Calculator.idl" );
    TEST_ASSERT( !idlContent.empty() );
    TEST_ASSERT( idlContent.find( "double" ) != String::npos );

    // Raw "Float64" must not appear outside comments
    Bool hasRawFloat64 = false;
    ::std::size_t pos = 0;
    while ( ( pos = idlContent.find( "Float64", pos ) ) != String::npos ) {
        ::std::size_t lineStart = idlContent.rfind( '\n', pos );
        if ( lineStart == String::npos ) { lineStart = 0; }
        String linePrefix = idlContent.substr( lineStart, pos - lineStart );
        if ( linePrefix.find( "//" ) == String::npos ) {
            hasRawFloat64 = true;
            break;
        }
        pos += 7;
    }
    TEST_ASSERT( !hasRawFloat64 );

    // QoS XML always paired
    TEST_ASSERT( FileExists( config.outputDir + "/Calculator_qos.xml" ) );

    ::std::cout << "  [Float64 mapping] OK\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Test: --com-config / --service-deploy / --instance-id
// ======================================================================
namespace
{

/**
 * @brief Write a temporary file; returns true on success
 */
Bool WriteFile( const String& path, const String& content ) {
    ::std::ofstream ofs( path );
    if ( !ofs.is_open() ) { return false; }
    ofs << content;
    return ofs.good();
}

Bool TestComConfigQosProfiles() {
    ::std::cout << "  [--com-config] Named profile applied to element...\n";

    // Write temporary YAML config files
    const String comCfg =
        "qos_profiles:\n"
        "  - name: FastEventProfile\n"
        "    reliability: BEST_EFFORT\n"
        "    durability: VOLATILE\n"
        "    history: KEEP_LAST\n"
        "    history_depth: 1\n";

    const String svcDeploy =
        "services:\n"
        "  - interface: Calculator\n"
        "    instance_id: 0x0007\n"
        "    elements:\n"
        "      - name: resultReady\n"
        "        kind: event\n"
        "        qos_profile: FastEventProfile\n";

    TEST_ASSERT( WriteFile( "/tmp/lap_test_com_config.yaml",  comCfg ) );
    TEST_ASSERT( WriteFile( "/tmp/lap_test_svc_deploy.yaml", svcDeploy ) );

    String outDir = "/tmp/lap_test_qos_profiles";
    auto [rc, out] = ExecCommand(
        gBinaryPath
        + " -i " + gFidlPath
        + " -o " + outDir
        + " --dds-idl"
        + " --com-config /tmp/lap_test_com_config.yaml"
        + " --service-deploy /tmp/lap_test_svc_deploy.yaml" );

    TEST_ASSERT_EQ( rc, 0 );

    String qosContent = ReadFile( outDir + "/Calculator_qos.xml" );
    TEST_ASSERT( !qosContent.empty() );

    // resultReady must use the named profile (BEST_EFFORT + depth=1)
    // Find the resultReady writer block
    ::std::size_t pos = qosContent.find( "Calculator_resultReady_event" );
    TEST_ASSERT( pos != String::npos );
    // Within ~200 chars of the profile name, depth should be 1
    String block = qosContent.substr( pos, 400U );
    TEST_ASSERT( block.find( "BEST_EFFORT" ) != String::npos );
    TEST_ASSERT( block.find( "<depth>1</depth>" ) != String::npos );

    // instance_id must be 0x0007
    TEST_ASSERT( qosContent.find( "instance_id: 0x0007" ) != String::npos );

    ::std::cout << "  [--com-config] OK\n";
    return true;
}

Bool TestServiceDeployQosOverride() {
    ::std::cout << "  [--service-deploy] Per-element direct QoS override...\n";

    const String svcDeploy =
        "services:\n"
        "  - interface: Calculator\n"
        "    instance_id: 3\n"
        "    elements:\n"
        "      - name: divide\n"
        "        kind: method\n"
        "        qos:\n"
        "          reliability: RELIABLE\n"
        "          durability: TRANSIENT_LOCAL\n"
        "          history_depth: 50\n";

    TEST_ASSERT( WriteFile( "/tmp/lap_test_svc_override.yaml", svcDeploy ) );

    String outDir = "/tmp/lap_test_qos_override";
    auto [rc, out] = ExecCommand(
        gBinaryPath
        + " -i " + gFidlPath
        + " -o " + outDir
        + " --dds-idl"
        + " --service-deploy /tmp/lap_test_svc_override.yaml" );

    TEST_ASSERT_EQ( rc, 0 );

    String qosContent = ReadFile( outDir + "/Calculator_qos.xml" );
    TEST_ASSERT( !qosContent.empty() );

    // divide method must have TRANSIENT_LOCAL + depth 50
    ::std::size_t pos = qosContent.find( "Calculator_divide_method" );
    TEST_ASSERT( pos != String::npos );
    String block = qosContent.substr( pos, 400U );
    TEST_ASSERT( block.find( "TRANSIENT_LOCAL" ) != String::npos );
    TEST_ASSERT( block.find( "<depth>50</depth>" ) != String::npos );

    // instance_id=3 → 0x0003
    TEST_ASSERT( qosContent.find( "instance_id: 0x0003" ) != String::npos );

    ::std::cout << "  [--service-deploy override] OK\n";
    return true;
}

Bool TestInstanceIdCli() {
    ::std::cout << "  [--instance-id] CLI overrides service_deploy and default...\n";

    String outDir = "/tmp/lap_test_instance_id";
    auto [rc, out] = ExecCommand(
        gBinaryPath
        + " -i " + gFidlPath
        + " -o " + outDir
        + " --dds-idl"
        + " --instance-id 0x00AB" );

    TEST_ASSERT_EQ( rc, 0 );

    String qosContent = ReadFile( outDir + "/Calculator_qos.xml" );
    TEST_ASSERT( !qosContent.empty() );
    TEST_ASSERT( qosContent.find( "instance_id: 0x00ab" ) != String::npos );

    ::std::cout << "  [--instance-id] OK\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Test: Error handling
// ======================================================================
namespace
{

Bool TestMissingInput() {
    ::std::cout << "  [Error] Missing --input...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " --validate" );
    TEST_ASSERT( rc != 0 );

    ::std::cout << "  [Missing input] OK — exit " << rc << "\n";
    return true;
}

Bool TestMissingOutputForGeneration() {
    ::std::cout << "  [Error] Missing --output for generation...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " -i " + gFidlPath + " --all" );
    TEST_ASSERT( rc != 0 );

    ::std::cout << "  [Missing output] OK — exit " << rc << "\n";
    return true;
}

Bool TestHelpFlag() {
    ::std::cout << "  [--help] Show usage...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " --help" );
    TEST_ASSERT_EQ( rc, 0 );
    TEST_ASSERT( out.find( "Usage:" ) != String::npos );
    TEST_ASSERT( out.find( "--validate" ) != String::npos );
    TEST_ASSERT( out.find( "--hash-only" ) != String::npos );
    TEST_ASSERT( out.find( "--schema-hash" ) != String::npos );
    TEST_ASSERT( out.find( "--version-string" ) != String::npos );
    TEST_ASSERT( out.find( "--com-config" ) != String::npos );
    TEST_ASSERT( out.find( "--service-deploy" ) != String::npos );
    TEST_ASSERT( out.find( "--slot-mapping" ) != String::npos );
    TEST_ASSERT( out.find( "--instance-id" ) != String::npos );

    ::std::cout << "  [--help] OK\n";
    return true;
}

Bool TestVersionFlag() {
    ::std::cout << "  [--version] Show version...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " --version" );
    TEST_ASSERT_EQ( rc, 0 );
    TEST_ASSERT( out.find( "lap-sidl-gen" ) != String::npos );

    ::std::cout << "  [--version] OK\n";
    return true;
}

Bool TestUnknownOption() {
    ::std::cout << "  [Error] Unknown option...\n";

    auto [rc, out] = ExecCommand( gBinaryPath + " --bogus-flag-xyz" );
    TEST_ASSERT( rc != 0 );

    ::std::cout << "  [Unknown option] OK — exit " << rc << "\n";
    return true;
}

} // anonymous namespace

// ======================================================================
// Main
// ======================================================================

Int32 main( Int32 /* argc */, Char** /* argv */ ) {
    ::std::cout << "========== lap-sidl-gen CLI Test Suite ==========\n\n";

    // Locate binary and test data
    if ( !LocateBinary() ) {
        ::std::cerr << "Error: Could not locate lap_sidl_gen binary\n";
        return 1;
    }
    ::std::cout << "Binary: " << gBinaryPath << "\n";

    if ( !LocateFidl() ) {
        ::std::cerr << "Error: Could not locate Calculator.fidl\n";
        return 1;
    }
    ::std::cout << "Test FIDL: " << gFidlPath << "\n\n";

    Int32 passed = 0;
    Int32 failed = 0;

    auto Run = [&]( const Char* name, Bool ( *fn )() ) {
        ::std::cout << "[Test] " << name << "\n";
        if ( fn() ) {
            ++passed;
        } else {
            ++failed;
            ::std::cerr << "  *** FAILED: " << name << " ***\n";
        }
        ::std::cout << "\n";
    };

    // ---- CLI mode tests ----
    Run( "Validate OK",                  TestValidateOk );
    Run( "Validate bad file",            TestValidateBadFile );
    Run( "Validate no --output needed",  TestValidateNoOutput );
    Run( "Hash-only output",             TestHashOnly );
    Run( "Hash deterministic",           TestHashDeterministic );
    Run( "Schema-hash inject (OMG IDL)", TestSchemaHashInjectDdsIdl );
    Run( "Schema-hash inject (Proxy)",   TestSchemaHashInjectProxy );
    Run( "Version-string override",      TestVersionStringOverride );

    // ---- QoS YAML config tests ----
    Run( "com-config QoS profiles",      TestComConfigQosProfiles );
    Run( "service-deploy QoS override",  TestServiceDeployQosOverride );
    Run( "instance-id CLI override",     TestInstanceIdCli );

    // ---- Library-level config tests ----
    Run( "Config schemaHashOverride",    TestConfigSchemaHashOverride );
    Run( "Config versionOverride",       TestConfigVersionOverride );
    Run( "Config auto hash (no override)", TestConfigNoOverrideUsesAuto );

    // ---- Type mapping ----
    Run( "Float64 OMG IDL mapping",      TestFloat64Mapping );

    // ---- Error handling ----
    Run( "Missing --input",              TestMissingInput );
    Run( "Missing --output for gen",     TestMissingOutputForGeneration );
    Run( "Help flag",                    TestHelpFlag );
    Run( "Version flag",                 TestVersionFlag );
    Run( "Unknown option",              TestUnknownOption );

    // Summary
    ::std::cout << "========== Results ==========\n";
    ::std::cout << "Passed: " << passed << " / " << ( passed + failed ) << "\n";
    if ( failed > 0 ) {
        ::std::cout << "FAILED: " << failed << " test(s)\n";
        return 1;
    }

    ::std::cout << "All tests passed!\n";
    return 0;
}
