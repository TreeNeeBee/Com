/**
 * @file        main.cpp
 * @author      Aii
 * @brief       lap-sidl-gen — Franca IDL Code Generator CLI
 * @date        2026/02/09
 * @details     Command-line front-end for the Franca IDL → AUTOSAR C++ code generator.
 *              Replaces the Python-based PyFranca toolchain with a single C++ binary.
 *              Supports all 5 steps from the dual-layer IDL architecture:
 *                Step 1: Validate Franca IDL syntax        (--validate)
 *                Step 2: Compute Schema Hash               (--hash-only)
 *                Step 3: Generate AUTOSAR C++ API           (--proxy --skeleton --types)
 *                Step 4: Generate OMG IDL v4.2 + QoS XML   (--dds-idl)
 *                Step 5: Full pipeline                      (--all, default)
 * @copyright   Copyright (c) 2026
 *
 * Usage:
 *   lap-sidl-gen --input <file.fidl> --output <dir> [options]
 *   lap-sidl-gen --input <file.fidl> --validate
 *   lap-sidl-gen --input <file.fidl> --hash-only
 *
 * Options:
 *   --input, -i <path>         Input .fidl file (required)
 *   --output, -o <dir>         Output directory (required for generation)
 *   --namespace, -n <ns>       Namespace prefix (default: "")
 *   --author <name>            Author for header comments (default: "Aii")
 *   --service-id <id>          Override auto-generated service ID (hex or decimal)
 *   --schema-hash <hash>       Inject external Schema Hash (skip auto-generation)
 *   --version-string <ver>     Override version string in OMG IDL (e.g. "1.2.3")
 *   --proxy                    Generate Proxy header
 *   --skeleton                 Generate Skeleton header
 *   --types                    Generate Types header
 *   --dds-idl                  Generate OMG IDL + QoS XML
 *   --dds-adapter              Generate DDS type adapter headers
 *   --com-config <path>        Path to com_config.yaml (QoS profiles)
 *   --service-deploy <path>    Path to service_deploy.yaml (per-element QoS bindings)
 *   --slot-mapping <path>      Path to slot_mapping.yaml (optional)
 *   --instance-id <id>         Override Instance ID in QoS XML (hex or decimal, default: 0x0001)
 *   --all                      Generate all outputs (default if none specified)
 *   --validate                 Validate .fidl syntax only (no generation)
 *   --hash-only                Print Schema Hash and exit (no generation)
 *   --help, -h                 Show this help
 *   --version, -v              Show version
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * <tr><td>2026/02/09  <td>1.1      <td>Aii     <td>add --validate, --hash-only, --schema-hash, --version-string
 * <tr><td>2026/02/09  <td>1.2      <td>Aii     <td>add --com-config, --service-deploy, --slot-mapping, --instance-id
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
#include "CDdsAdapterGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <cstdlib>
#include <cstring>
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
using gen::CTypesGenerator;
using gen::CProxyGenerator;
using gen::CSkeletonGenerator;
using gen::CDdsIdlGenerator;
using gen::CDdsAdapterGenerator;
using gen::LexerError;
using gen::ParserError;
using gen::Token;

// ==================== Constants ====================
static constexpr const Char* kVersion   = "1.0.0";
static constexpr const Char* kToolName  = "lap-sidl-gen";

// ==================== File-Local Helpers ====================
namespace
{

void PrintUsage();
void PrintVersion();
String ReadFile( const String& path );

// ==================== CLI Flags ====================
struct CliOptions {
    String  inputFile;
    String  outputDir;
    String  nsPrefix;
    String  author            = "Aii";
    String  schemaHashInject;                      ///< External Schema Hash (skip auto-gen)
    String  versionString;                         ///< Override version in OMG IDL
    String  comConfigPath;                         ///< Path to com_config.yaml
    String  serviceDeployPath;                     ///< Path to service_deploy.yaml
    String  slotMappingPath;                       ///< Path to slot_mapping.yaml (optional)
    UInt16  serviceIdOverride  = 0;
    UInt16  instanceIdOverride = 0;                ///< 0 = use service_deploy or default 1
    Bool    genProxy          = false;
    Bool    genSkeleton       = false;
    Bool    genTypes          = false;
    Bool    genDdsIdl         = false;
    Bool    genDdsAdapter     = false;             ///< --dds-adapter: DDS type adapter headers
    Bool    validateOnly      = false;             ///< --validate: syntax check only
    Bool    hashOnly          = false;             ///< --hash-only: print hash and exit
    Bool    showHelp          = false;
    Bool    showVersion       = false;
};

Bool ParseArgs( Int32 argc, Char** argv, CliOptions& opts ) {
    for ( Int32 i = 1; i < argc; ++i ) {
        String arg = argv[i];

        if ( arg == "--help" || arg == "-h" ) {
            opts.showHelp = true;
            return true;
        } else if ( arg == "--version" || arg == "-v" ) {
            opts.showVersion = true;
            return true;
        } else if ( ( arg == "--input" || arg == "-i" ) && ( i + 1 < argc ) ) {
            opts.inputFile = argv[++i];
        } else if ( ( arg == "--output" || arg == "-o" ) && ( i + 1 < argc ) ) {
            opts.outputDir = argv[++i];
        } else if ( ( arg == "--namespace" || arg == "-n" ) && ( i + 1 < argc ) ) {
            opts.nsPrefix = argv[++i];
        } else if ( arg == "--author" && ( i + 1 < argc ) ) {
            opts.author = argv[++i];
        } else if ( arg == "--service-id" && ( i + 1 < argc ) ) {
            opts.serviceIdOverride = static_cast< UInt16 >(
                ::std::stoul( argv[++i], nullptr, 0 ) );
        } else if ( arg == "--schema-hash" && ( i + 1 < argc ) ) {
            opts.schemaHashInject = argv[++i];
        } else if ( arg == "--version-string" && ( i + 1 < argc ) ) {
            opts.versionString = argv[++i];
        } else if ( arg == "--validate" ) {
            opts.validateOnly = true;
        } else if ( arg == "--hash-only" ) {
            opts.hashOnly = true;
        } else if ( arg == "--proxy" ) {
            opts.genProxy = true;
        } else if ( arg == "--skeleton" ) {
            opts.genSkeleton = true;
        } else if ( arg == "--types" ) {
            opts.genTypes = true;
        } else if ( arg == "--dds-idl" ) {
            opts.genDdsIdl = true;
        } else if ( arg == "--dds-adapter" ) {
            opts.genDdsAdapter = true;
        } else if ( arg == "--com-config" && ( i + 1 < argc ) ) {
            opts.comConfigPath = argv[++i];
        } else if ( arg == "--service-deploy" && ( i + 1 < argc ) ) {
            opts.serviceDeployPath = argv[++i];
        } else if ( arg == "--slot-mapping" && ( i + 1 < argc ) ) {
            opts.slotMappingPath = argv[++i];
        } else if ( arg == "--instance-id" && ( i + 1 < argc ) ) {
            opts.instanceIdOverride = static_cast< UInt16 >(
                ::std::stoul( argv[++i], nullptr, 0 ) );
        } else if ( arg == "--all" ) {
            opts.genProxy       = true;
            opts.genSkeleton    = true;
            opts.genTypes       = true;
            opts.genDdsIdl      = true;
            opts.genDdsAdapter  = true;
        } else {
            ::std::cerr << "Error: Unknown option '" << arg << "'\n";
            return false;
        }
    }

    return true;
}

} // anonymous namespace

// ==================== Main Entry Point ====================

Int32 main( Int32 argc, Char** argv ) {
    CliOptions opts;
    if ( !ParseArgs( argc, argv, opts ) ) {
        PrintUsage();
        return 1;
    }

    if ( opts.showHelp ) {
        PrintUsage();
        return 0;
    }
    if ( opts.showVersion ) {
        PrintVersion();
        return 0;
    }

    // Validate required args
    if ( opts.inputFile.empty() ) {
        ::std::cerr << "Error: --input is required\n";
        PrintUsage();
        return 1;
    }

    // --output is NOT required for --validate and --hash-only modes
    if ( opts.outputDir.empty() && !opts.validateOnly && !opts.hashOnly ) {
        ::std::cerr << "Error: --output is required for code generation\n";
        PrintUsage();
        return 1;
    }

    // Read input file
    String source = ReadFile( opts.inputFile );
    if ( source.empty() ) {
        ::std::cerr << "Error: Failed to read input file '" << opts.inputFile << "'\n";
        return 1;
    }

    // ==================== Phase 1: Lex ====================
    ::std::vector< Token > tokens;
    try {
        CFidlLexer lexer( source, opts.inputFile );
        tokens = lexer.Tokenize();
    } catch ( const LexerError& e ) {
        ::std::cerr << "Lexer error: " << e.what() << "\n";
        return 2;
    }

    // ==================== Phase 2: Parse ====================
    FidlModel model;
    try {
        CFidlParser parser( tokens, opts.inputFile );
        model = parser.Parse();
    } catch ( const ParserError& e ) {
        ::std::cerr << "Parser error: " << e.what() << "\n";
        return 3;
    }
    model.sourceFile = opts.inputFile;

    // ==================== --validate mode ====================
    if ( opts.validateOnly ) {
        ::std::cout << "OK: " << opts.inputFile << "\n";
        ::std::cout << "  Package: " << model.packageName << "\n";
        ::std::cout << "  Interfaces: " << model.interfaces.size() << "\n";
        ::std::cout << "  Type collections: " << model.typeCollections.size() << "\n";
        return 0;
    }

    // ==================== Phase 3: Schema Hash ====================
    String hash;
    if ( !opts.schemaHashInject.empty() ) {
        hash = opts.schemaHashInject;
    } else {
        hash = CSchemaHash::Compute( model );
    }

    // ==================== --hash-only mode ====================
    if ( opts.hashOnly ) {
        ::std::cout << hash << "\n";
        return 0;
    }

    // ==================== Full generation mode ====================
    // Default: generate all if none specified
    if ( !opts.genProxy && !opts.genSkeleton && !opts.genTypes
         && !opts.genDdsIdl && !opts.genDdsAdapter ) {
        opts.genProxy       = true;
        opts.genSkeleton    = true;
        opts.genTypes       = true;
        opts.genDdsIdl      = true;
        opts.genDdsAdapter  = true;
    }

    ::std::cout << kToolName << " v" << kVersion << "\n";
    ::std::cout << "Input:  " << opts.inputFile << "\n";
    ::std::cout << "Output: " << opts.outputDir << "\n";
    ::std::cout << "Hash:   " << hash << "\n\n";

    ::std::cout << "[1/2] Parsed " << tokens.size() << " tokens\n";
    ::std::cout << "  Package: " << model.packageName << "\n";
    ::std::cout << "  Interfaces: " << model.interfaces.size() << "\n";
    ::std::cout << "  Type collections: " << model.typeCollections.size() << "\n";

    // ==================== Phase 4: Generate ====================
    ::std::cout << "[2/2] Generating code...\n";

    GeneratorConfig config;
    config.outputDir          = opts.outputDir;
    config.namespacePrefix    = opts.nsPrefix;
    config.author             = opts.author;
    config.serviceIdOverride  = opts.serviceIdOverride;
    config.instanceIdOverride = opts.instanceIdOverride;
    config.schemaHashOverride = hash;
    config.versionOverride    = opts.versionString;
    config.comConfigPath      = opts.comConfigPath;
    config.serviceDeployPath  = opts.serviceDeployPath;
    config.slotMappingPath    = opts.slotMappingPath;

    Bool allOk = true;

    if ( opts.genTypes ) {
        ::std::cout << "\n--- Types ---\n";
        CTypesGenerator typesGen;
        if ( !typesGen.Generate( model, config ) ) {
            ::std::cerr << "Error: Types generation failed\n";
            allOk = false;
        }
    }

    if ( opts.genProxy ) {
        ::std::cout << "\n--- Proxy ---\n";
        CProxyGenerator proxyGen;
        if ( !proxyGen.Generate( model, config ) ) {
            ::std::cerr << "Error: Proxy generation failed\n";
            allOk = false;
        }
    }

    if ( opts.genSkeleton ) {
        ::std::cout << "\n--- Skeleton ---\n";
        CSkeletonGenerator skelGen;
        if ( !skelGen.Generate( model, config ) ) {
            ::std::cerr << "Error: Skeleton generation failed\n";
            allOk = false;
        }
    }

    if ( opts.genDdsIdl ) {
        ::std::cout << "\n--- OMG IDL + QoS XML ---\n";
        CDdsIdlGenerator ddsGen;
        if ( !ddsGen.Generate( model, config ) ) {
            ::std::cerr << "Error: OMG IDL generation failed\n";
            allOk = false;
        }
    }

    if ( opts.genDdsAdapter ) {
        ::std::cout << "\n--- DDS Type Adapter Headers ---\n";
        CDdsAdapterGenerator adapterGen;
        if ( !adapterGen.Generate( model, config ) ) {
            ::std::cerr << "Error: DDS adapter generation failed\n";
            allOk = false;
        }
    }

    if ( allOk ) {
        ::std::cout << "\nDone. All outputs generated successfully.\n";
        return 0;
    }

    ::std::cerr << "\nSome outputs failed to generate.\n";
    return 4;
}

// ==================== Utility Functions ====================
namespace
{

void PrintUsage() {
    ::std::cout
        << "Usage: " << kToolName << " --input <file.fidl> --output <dir> [options]\n"
        << "       " << kToolName << " --input <file.fidl> --validate\n"
        << "       " << kToolName << " --input <file.fidl> --hash-only\n"
        << "\n"
        << "Modes:\n"
        << "  --validate               Validate .fidl syntax only (no generation)\n"
        << "  --hash-only              Print Schema Hash and exit (no generation)\n"
        << "  (default)                Full code generation pipeline\n"
        << "\n"
        << "Options:\n"
        << "  --input, -i <path>       Input .fidl file (required)\n"
        << "  --output, -o <dir>       Output directory (required for generation)\n"
        << "  --namespace, -n <ns>     Namespace prefix (default: \"\")\n"
        << "  --author <name>          Author for header comments (default: \"Aii\")\n"
        << "  --service-id <id>        Override auto-generated service ID (hex or decimal)\n"
        << "  --schema-hash <hash>     Inject external Schema Hash (skip auto-generation)\n"
        << "  --version-string <ver>   Override version string in OMG IDL (e.g. \"1.2.3\")\n"
        << "  --proxy                  Generate Proxy header\n"
        << "  --skeleton               Generate Skeleton header\n"
        << "  --types                  Generate Types header\n"
        << "  --dds-idl                Generate OMG IDL + QoS XML\n"
        << "  --dds-adapter            Generate DDS type adapter headers\n"
        << "  --com-config <path>      Path to com_config.yaml (QoS profiles)\n"
        << "  --service-deploy <path>  Path to service_deploy.yaml (per-element QoS bindings)\n"
        << "  --slot-mapping <path>    Path to slot_mapping.yaml (optional)\n"
        << "  --instance-id <id>       Override Instance ID in QoS XML (hex or decimal)\n"
        << "  --all                    Generate all outputs (default if none specified)\n"
        << "  --help, -h               Show this help\n"
        << "  --version, -v            Show version\n"
        << "\n"
        << "Examples:\n"
        << "  " << kToolName << " -i Calculator.fidl --validate\n"
        << "  " << kToolName << " -i Calculator.fidl --hash-only\n"
        << "  " << kToolName << " -i Calculator.fidl -o gen/ --all\n"
        << "  " << kToolName << " -i Sensor.fidl -o gen/ --proxy --types -n lap::app\n"
        << "  " << kToolName << " -i Radar.fidl -o gen/ --all --schema-hash a3f7c9e2b5d14a8c\n";
}

void PrintVersion() {
    ::std::cout << kToolName << " v" << kVersion << "\n";
}

String ReadFile( const String& path ) {
    ::std::ifstream ifs( path, ::std::ios::binary );
    if ( !ifs.is_open() ) {
        return "";
    }

    ::std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

} // anonymous namespace
