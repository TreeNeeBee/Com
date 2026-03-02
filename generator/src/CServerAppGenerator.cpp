/**
 * @file        CServerAppGenerator.cpp
 * @author      Aii
 * @brief       Server Application Framework Code Generator Implementation
 * @date        2026/03/01
 * @details     Generates <Service>ServerApp.hpp with:
 *              - Complete dual-binding server framework (CoreIPC + DDS)
 *              - Pure virtual method handlers (user implements business logic)
 *              - Pure virtual field getters/setters (user implements state)
 *              - Event send helpers + field update helpers
 *              - MakeReadyFuture<T> / MakeReadyVoidFuture utilities
 *              - Lifecycle hooks (OnStart / OnStop / OnTick)
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CServerAppGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <iostream>
#include <filesystem>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== CServerAppGenerator Implementation ====================

    Bool CServerAppGenerator::Generate( const FidlModel& model,
                                        const GeneratorConfig& config ) {
        auto nsSegments = ExtractNamespaceSegments( model.packageName,
                                                    config.namespacePrefix );

        for ( const auto& iface : model.interfaces ) {
            CCodeWriter w;
            String className = iface.name + "ServerApp";
            String filename  = className + ".hpp";
            String guard = buildGuardMacro( nsSegments, className );

            // ==================== File Header ====================
            writeFileHeader( w, filename,
                "Auto-generated server application framework for " + iface.name,
                model.sourceFile, config.author );
            w.Line();

            writeGuardOpen( w, guard );
            w.Line();

            // ==================== Includes ====================
            w.Line( "// ==================== Generated Headers ====================" );
            w.Line( "#include \"" + iface.name + "Skeleton.hpp\"" );
            if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
                w.Line( "#include \"" + iface.name + "DdsAdapter.hpp\"" );
            }
            w.Line();
            w.Line( "// ==================== Binding / Infrastructure ====================" );
            w.Line( "#include \"CoreIPCBinding.hpp\"" );
            w.Line( "#include \"CRegistryDispatcher.hpp\"" );
            if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
                w.Line( "#include \"DdsBinding.hpp\"" );
            }
            w.Line( "#include \"BindingManager.hpp\"" );
            w.Line();
            w.Line( "// ==================== Standard Library ====================" );
            w.Line( "#include <atomic>" );
            w.Line( "#include <chrono>" );
            w.Line( "#include <csignal>" );
            w.Line( "#include <functional>" );
            w.Line( "#include <iostream>" );
            w.Line( "#include <mutex>" );
            w.Line( "#include <thread>" );
            w.Line();

            // ==================== Namespaces ====================
            writeNamespaceOpen( w, nsSegments );
            writeLapComUsings( w, nsSegments );
            w.Line();

            w.Line( "namespace server_app" );
            w.Line( "{" );
            w.Line();

            // ==================== MakeReadyFuture Utilities ====================
            w.Line( "// ==================== Future Utilities ====================" );
            w.Line();
            w.Line( "template< typename T >" );
            w.Line( "inline ::lap::core::Future< T > MakeReadyFuture( T value )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "::std::promise< ::lap::core::Result< T > > p;" );
            w.Line( "p.set_value( ::lap::core::Result< T >::FromValue( ::std::move( value ) ) );" );
            w.Line( "return ::lap::core::Future< T >( ::std::move( p.get_future() ) );" );
            w.Dedent();
            w.Line( "}" );
            w.Line();
            w.Line( "inline ::lap::core::Future< void > MakeReadyVoidFuture()" );
            w.Line( "{" );
            w.Indent();
            w.Line( "::std::promise< ::lap::core::Result< void > > p;" );
            w.Line( "p.set_value( ::lap::core::Result< void >::FromValue() );" );
            w.Line( "return ::lap::core::Future< void >( ::std::move( p.get_future() ) );" );
            w.Dedent();
            w.Line( "}" );
            w.Line();

            // ==================== ServerApp Class ====================

            // Compute service ID
            String qualifiedName = model.packageName + "." + iface.name;
            UInt16 serviceId = config.serviceIdOverride;
            if ( serviceId == 0 ) {
                serviceId = static_cast< UInt16 >(
                    CSchemaHash::GenerateServiceId( qualifiedName ) );
            }
            String hash = config.schemaHashOverride.empty()
                ? CSchemaHash::Compute( model )
                : config.schemaHashOverride;

            String skelClass = iface.name + "Skeleton";
            String skelFQ    = "skeleton::" + skelClass;

            w.Line( "/**" );
            w.Line( " * @brief Server application framework for " + iface.name );
            if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
                w.Line( " * @details Encapsulates all dual-binding boilerplate (CoreIPC + DDS)." );
            } else {
                w.Line( " * @details Encapsulates all CoreIPC binding boilerplate." );
            }
            w.Line( " *          User subclasses and implements only the business logic:" );
            w.Line( " *          - Method handler callbacks (pure virtual)" );
            w.Line( " *          - Field getter/setter callbacks (pure virtual)" );
            w.Line( " *          - Event sending via helper methods" );
            w.Line( " *          - Lifecycle hooks (OnStart / OnStop / OnTick)" );
            w.Line( " *" );
            w.Line( " * Usage:" );
            w.Line( " *   class MyServer : public " + className + " {" );
            w.Line( " *       // implement pure virtual handlers ..." );
            w.Line( " *   };" );
            w.Line( " *   int main() { MyServer s; return s.Run(); }" );
            w.Line( " */" );
            w.Line( "class " + className );
            w.Line( "{" );

            // ==================== Public Section ====================
            w.Line( "public:" );
            w.Indent();

            // Virtual destructor
            w.Line( "virtual ~" + className + "() = default;" );
            w.Line();

            // ==================== Pure Virtual — Method Handlers ====================
            w.Line( "// ==================== Method Handlers (implement business logic) ====================" );
            w.Line();
            generateMethodHandlerDecls( w, iface );
            w.Line();

            // ==================== Pure Virtual — Field Handlers ====================
            w.Line( "// ==================== Field Handlers (implement state management) ====================" );
            w.Line();
            generateFieldHandlerDecls( w, iface );
            w.Line();

            // ==================== Virtual Lifecycle Hooks ====================
            w.Line( "// ==================== Lifecycle Hooks (optional overrides) ====================" );
            w.Line();
            w.Line( "/// Called after OfferService succeeds, before main loop" );
            w.Line( "virtual void OnStart() {}" );
            w.Line();
            w.Line( "/// Called after main loop exits, before shutdown" );
            w.Line( "virtual void OnStop() {}" );
            w.Line();
            w.Line( "/// Called each tick (~1s). Return false to stop the server." );
            w.Line( "virtual bool OnTick( UInt32 /* tickCount */ ) { return true; }" );
            w.Line();

            // ==================== Event Send Helpers ====================
            w.Line( "// ==================== Event Helpers (send events to subscribers) ====================" );
            w.Line();
            generateEventSendHelpers( w, iface );
            w.Line();

            // ==================== Field Update Helpers ====================
            w.Line( "// ==================== Field Notifications (push updates to subscribers) ====================" );
            w.Line();
            generateFieldUpdateHelpers( w, iface );
            w.Line();

            // ==================== Run Method ====================
            w.Line( "// ==================== Framework Entry Point ====================" );
            w.Line();
            generateRunMethod( w, iface, model, config, nsSegments );
            w.Line();

            // ==================== Control Methods ====================
            w.Line( "/// Request graceful shutdown" );
            w.Line( "void Stop() noexcept { m_running.store( false ); }" );
            w.Line();
            w.Line( "/// Check if server is running" );
            w.Line( "bool IsRunning() const noexcept { return m_running.load(); }" );
            w.Line();

            w.Dedent();

            // ==================== Protected Section ====================
            w.Line( "protected:" );
            w.Indent();
            w.Line( "/// Access the underlying skeleton (for advanced use)" );
            w.Line( skelFQ + "& GetSkeleton() { return *m_pSkeleton; }" );
            w.Line();
            w.Dedent();

            // ==================== Private Section ====================
            w.Line( "private:" );
            w.Indent();
            w.Line( "static void signalHandler_( int ) noexcept" );
            w.Line( "{" );
            w.Indent();
            w.Line( "if ( s_instance_ ) { s_instance_->m_running.store( false ); }" );
            w.Dedent();
            w.Line( "}" );
            w.Line();
            w.Line( "static inline " + className + "* s_instance_ = nullptr;" );
            w.Line( "::std::atomic< bool > m_running{ true };" );
            w.Line( skelFQ + "* m_pSkeleton = nullptr;" );
            w.Dedent();

            // Close class
            w.Line( "}; // class " + className );

            w.Line();
            w.Line( "} // namespace server_app" );
            w.Line();

            writeNamespaceClose( w, nsSegments );
            w.Line();
            writeGuardClose( w, guard );

            // Write output file
            String outPath = config.outputDir + "/" + filename;
            if ( !w.WriteToFile( outPath ) ) {
                ::std::cerr << "Error: Failed to write " << outPath << "\n";
                return false;
            }
            ::std::cout << "  Generated: " << outPath << "\n";
        }

        return true;
    }

    // ==================== Pure Virtual Method Handler Declarations ====================

    void CServerAppGenerator::generateMethodHandlerDecls( CCodeWriter& w,
                                                           const Interface& iface ) const noexcept {
        for ( const auto& method : iface.methods ) {
            String methodPascal = ToPascalCase( method.name );

            // Build argument list
            String argList;
            for ( ::std::size_t i = 0; i < method.inArgs.size(); ++i ) {
                if ( i > 0 ) { argList += ", "; }
                String cppType = resolveCppType( method.inArgs[i].type );
                argList += cppType + " " + method.inArgs[i].name;
            }

            if ( method.isFireAndForget ) {
                w.Line( "/// Handle " + method.name + " (fire-and-forget)" );
                w.Line( "virtual void On" + methodPascal + "( " + argList + " ) = 0;" );
            } else {
                // Determine output type
                String outType;
                if ( method.outArgs.empty() ) {
                    outType = "void";
                } else if ( method.outArgs.size() == 1 ) {
                    outType = resolveCppType( method.outArgs[0].type );
                } else {
                    outType = ToPascalCase( method.name ) + "Output";
                }

                String futureType = ( outType == "void" )
                    ? "::lap::core::Future< void >"
                    : "::lap::core::Future< " + outType + " >";

                w.Line( "/// Handle " + method.name + " → " + outType );
                w.Line( "virtual " + futureType + " On" + methodPascal + "( " + argList + " ) = 0;" );
            }
            w.Line();
        }
    }

    // ==================== Pure Virtual Field Handler Declarations ====================

    void CServerAppGenerator::generateFieldHandlerDecls( CCodeWriter& w,
                                                          const Interface& iface ) const noexcept {
        for ( const auto& attr : iface.attributes ) {
            String fieldPascal = ToPascalCase( attr.name );
            String cppType = resolveCppType( attr.type );

            // Getter (always present)
            w.Line( "/// Get " + attr.name + " value" );
            w.Line( "virtual ::lap::core::Future< " + cppType + " > OnGet" + fieldPascal + "() = 0;" );
            w.Line();

            // Setter (only if not readonly)
            if ( !attr.isReadonly ) {
                // Determine if type should be passed by const-ref or value
                Bool byRef = ( cppType == "String" || cppType.find( "vector" ) != String::npos
                            || cppType.find( "::" ) != String::npos );
                String paramType = byRef
                    ? "const " + cppType + "&"
                    : cppType;

                w.Line( "/// Set " + attr.name + " value" );
                w.Line( "virtual ::lap::core::Future< void > OnSet" + fieldPascal + "( " + paramType + " value ) = 0;" );
                w.Line();
            }
        }
    }

    // ==================== Event Send Helper Methods ====================

    void CServerAppGenerator::generateEventSendHelpers( CCodeWriter& w,
                                                         const Interface& iface ) const noexcept {
        for ( const auto& bc : iface.broadcasts ) {
            String bcPascal = ToPascalCase( bc.name );
            String bcCamel  = ToCamelCase( bc.name );
            String eventType = bcPascal + "Event";

            // Method 1: Send with pre-built event struct
            w.Line( "/// Send " + bc.name + " event (pre-built struct)" );
            w.Line( "void Send" + bcPascal + "( const " + eventType + "& data )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "if ( !m_pSkeleton ) { return; }" );
            w.Line( "auto sample = m_pSkeleton->" + bcCamel + ".Allocate();" );
            w.Line( "if ( sample.HasValue() )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "*sample.Value() = data;" );
            w.Line( "m_pSkeleton->" + bcCamel + ".Send( ::std::move( sample ).Value() );" );
            w.Dedent();
            w.Line( "}" );
            w.Dedent();
            w.Line( "}" );
            w.Line();

            // Method 2: Send with individual fields (if broadcast has outArgs)
            if ( !bc.outArgs.empty() ) {
                String argList;
                String assignBlock;
                for ( ::std::size_t i = 0; i < bc.outArgs.size(); ++i ) {
                    String cppType = resolveCppType( bc.outArgs[i].type );
                    String argName = bc.outArgs[i].name;
                    Bool byRef = ( cppType == "String" || cppType.find( "vector" ) != String::npos
                                || cppType.find( "::" ) != String::npos );
                    if ( i > 0 ) { argList += ", "; }
                    argList += ( byRef ? "const " + cppType + "& " : cppType + " " ) + argName;
                    assignBlock += "sample.Value()->" + argName + " = " + argName + "; ";
                }

                w.Line( "/// Send " + bc.name + " event (individual fields)" );
                w.Line( "void Send" + bcPascal + "( " + argList + " )" );
                w.Line( "{" );
                w.Indent();
                w.Line( "if ( !m_pSkeleton ) { return; }" );
                w.Line( "auto sample = m_pSkeleton->" + bcCamel + ".Allocate();" );
                w.Line( "if ( sample.HasValue() )" );
                w.Line( "{" );
                w.Indent();
                for ( const auto& arg : bc.outArgs ) {
                    w.Line( "sample.Value()->" + arg.name + " = " + arg.name + ";" );
                }
                w.Line( "m_pSkeleton->" + bcCamel + ".Send( ::std::move( sample ).Value() );" );
                w.Dedent();
                w.Line( "}" );
                w.Dedent();
                w.Line( "}" );
                w.Line();
            }
        }
    }

    // ==================== Field Update Helper Methods ====================

    void CServerAppGenerator::generateFieldUpdateHelpers( CCodeWriter& w,
                                                           const Interface& iface ) const noexcept {
        for ( const auto& attr : iface.attributes ) {
            if ( !attr.isNotify ) { continue; }

            String fieldPascal = ToPascalCase( attr.name );
            String fieldCamel  = ToCamelCase( attr.name );
            String cppType = resolveCppType( attr.type );
            Bool byRef = ( cppType == "String" || cppType.find( "vector" ) != String::npos
                        || cppType.find( "::" ) != String::npos );
            String paramType = byRef ? "const " + cppType + "&" : cppType;

            w.Line( "/// Notify subscribers of " + attr.name + " change" );
            w.Line( "void Update" + fieldPascal + "( " + paramType + " value )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "if ( m_pSkeleton ) { m_pSkeleton->" + fieldCamel + ".Update( value ); }" );
            w.Dedent();
            w.Line( "}" );
            w.Line();
        }
    }

    // ==================== Run() Method — The Complete Server Framework ====================

    void CServerAppGenerator::generateRunMethod( CCodeWriter& w,
                                                  const Interface& iface,
                                                  const FidlModel& /* model */,
                                                  const GeneratorConfig& config,
                                                  const ::std::vector< String >& /* nsSegments */ ) const noexcept {
        String skelClass = iface.name + "Skeleton";
        String skelFQ    = "skeleton::" + skelClass;
        String className = iface.name + "ServerApp";

        w.Line( "/**" );
        w.Line( " * @brief Run the server (blocks until Stop() or SIGINT/SIGTERM)" );
        w.Line( " * @return 0 on success, non-zero on error" );
        w.Line( " */" );
        w.Line( "int Run( int /* argc */ = 0, char** /* argv */ = nullptr )" );
        w.Line( "{" );
        w.Indent();

        // Signal handler
        w.Line( "// Install signal handler" );
        w.Line( "s_instance_ = this;" );
        w.Line( "::std::signal( SIGINT, &" + className + "::signalHandler_ );" );
        w.Line( "::std::signal( SIGTERM, &" + className + "::signalHandler_ );" );
        w.Line();

        w.Line( "::std::cout << \"=== \" << " + skelFQ + "::kServiceName"
                " << \" Server (" + ( HasBinding( config.bindingLayers, kBindingDds )
                    ? String( "Dual-Binding" ) : String( "CoreIPC" ) )
                + ") ===\" << ::std::endl;" );
        w.Line();

        // Phase 1: Registry Dispatcher
        w.Line( "// Phase 1 — Registry Dispatcher" );
        w.Line( "::lap::com::registry::CRegistryDispatcher dispatcher;" );
        w.Line( "auto dispInit = dispatcher.Initialize();" );
        w.Line( "if ( !dispInit.HasValue() )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ServerApp] Dispatcher init failed: \"" );
        w.Line( "            << dispInit.Error().Message() << ::std::endl;" );
        w.Line( "return 1;" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "::std::thread dispThread( [&]() { dispatcher.Run(); } );" );
        w.Line( "::std::this_thread::sleep_for( ::std::chrono::milliseconds( 300 ) );" );
        w.Line();

        // Phase 2: CoreIPC Binding
        w.Line( "// Phase 2 — CoreIPC Binding" );
        w.Line( "auto pCoreIpc = ::lap::com::MakeShared< ::lap::com::binding::CoreIPCBinding >();" );
        w.Line( "auto ipcR = pCoreIpc->Initialize();" );
        w.Line( "if ( !ipcR )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ServerApp] CoreIPC init failed: \"" );
        w.Line( "            << ipcR.Error().Message() << ::std::endl;" );
        w.Line( "dispatcher.Shutdown();" );
        w.Line( "if ( dispThread.joinable() ) { dispThread.join(); }" );
        w.Line( "return 1;" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "::std::this_thread::sleep_for( ::std::chrono::milliseconds( 200 ) );" );
        w.Line();

        // Phase 3: DDS Binding (only in dual-binding mode)
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "// Phase 3 — DDS Binding (graceful fallback if unavailable)" );
            w.Line( "auto pDds = ::lap::com::MakeShared< ::lap::com::binding::DdsBinding >();" );
            w.Line( "auto ddsR = pDds->Initialize();" );
            w.Line( "if ( !ddsR )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "::std::cerr << \"[ServerApp] DDS init failed — CoreIPC-only mode\" << ::std::endl;" );
            w.Line( "pDds.reset();" );
            w.Dedent();
            w.Line( "}" );
            w.Line();

            // Phase 3.5: SD-Proxy Bridge
            w.Line( "// Phase 3.5 — SD-Proxy Bridge (DDS ↔ SD-Proxy cache)" );
            w.Line( "if ( pDds )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "auto bridge = dispatcher.GetSDProxyBridgeFunc();" );
            w.Line( "if ( bridge ) { pDds->SetSDProxyBridge( bridge ); }" );
            w.Line( "auto pDdsCapture = pDds;" );
            w.Line( "dispatcher.GetSDProxy().SetActiveQueryCallback(" );
            w.Line( "    [pDdsCapture]( uint64_t sid ) -> ::std::vector< uint64_t >" );
            w.Line( "    {" );
            w.Line( "        auto r = pDdsCapture->FindService( sid );" );
            w.Line( "        return r.HasValue() ? r.Value() : ::std::vector< uint64_t >{};" );
            w.Line( "    } );" );
            w.Dedent();
            w.Line( "}" );
            w.Line();
        }

        // Phase 4: BindingManager
        w.Line( "// Phase 4 — BindingManager Registration" );
        w.Line( "auto& mgr = ::lap::com::binding::BindingManager::GetInstance();" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::lap::com::binding::BindingConfig cfg;" );
        w.Line( "cfg.name     = \"coreipc-server\";" );
        w.Line( "cfg.priority = ::lap::com::binding::BindingPriority::kCoreIpc;" );
        w.Line( "cfg.enabled  = true;" );
        w.Line( "auto r = mgr.RegisterBinding( cfg, pCoreIpc );" );
        w.Line( "if ( !r.HasValue() )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ServerApp] RegisterBinding(CoreIPC) failed\" << ::std::endl;" );
        w.Line( "pCoreIpc->Shutdown();" );
        w.Line( "dispatcher.Shutdown();" );
        w.Line( "if ( dispThread.joinable() ) { dispThread.join(); }" );
        w.Line( "return 1;" );
        w.Dedent();
        w.Line( "}" );
        w.Dedent();
        w.Line( "}" );
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "if ( pDds )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "::lap::com::binding::BindingConfig cfg;" );
            w.Line( "cfg.name     = \"dds-server\";" );
            w.Line( "cfg.priority = ::lap::com::binding::BindingPriority::kDds;" );
            w.Line( "cfg.enabled  = true;" );
            w.Line( "auto r = mgr.RegisterBinding( cfg, pDds );" );
            w.Line( "if ( !r.HasValue() ) { pDds->Shutdown(); pDds.reset(); }" );
            w.Dedent();
            w.Line( "}" );
        }
        w.Line();

        // Phase 5: DDS Type Adapters (only in dual-binding mode)
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "// Phase 5 — DDS Type Adapters" );
            w.Line( "if ( pDds )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "dds_adapter::Register" + iface.name + "DdsAdapters(" );
            w.Line( "    " + skelFQ + "::kServiceId );" );
            w.Dedent();
            w.Line( "}" );
            w.Line();
        }

        // Phase 6: Create Skeleton
        w.Line( "// Phase 6 — Create Skeleton" );
        w.Line( skelFQ + " skel(" );
        w.Line( "    ::lap::core::InstanceSpecifier( \"" + iface.name + "/Provider\" ) );" );
        w.Line( "m_pSkeleton = &skel;" );
        w.Line();

        // Phase 7: Register Method Handlers
        w.Line( "// Phase 7 — Wire Method Handlers → virtual callbacks" );
        generateMethodHandlerWiring( w, iface );
        w.Line();

        // Phase 8: Register Field Handlers
        w.Line( "// Phase 8 — Wire Field Handlers → virtual callbacks" );
        generateFieldHandlerWiring( w, iface );
        w.Line();

        // Phase 9: OfferService
        w.Line( "// Phase 9 — OfferService" );
        w.Line( "auto offerR = skel.OfferService();" );
        w.Line( "if ( !offerR.HasValue() )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ServerApp] OfferService failed: \"" );
        w.Line( "            << offerR.Error().Message() << ::std::endl;" );
        w.Line( "m_pSkeleton = nullptr;" );
        w.Line( "pCoreIpc->Shutdown();" );
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "if ( pDds ) { pDds->Shutdown(); }" );
        }
        w.Line( "dispatcher.Shutdown();" );
        w.Line( "if ( dispThread.joinable() ) { dispThread.join(); }" );
        w.Line( "mgr.Shutdown();" );
        w.Line( "return 1;" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "::std::cout << \"[ServerApp] Service offered.\" << ::std::endl;" );
        w.Line();

        // Phase 10: OnStart
        w.Line( "// Phase 10 — User lifecycle hook" );
        w.Line( "OnStart();" );
        w.Line();

        // Phase 11: Main loop
        w.Line( "// Phase 11 — Main loop (~1s tick)" );
        w.Line( "UInt32 tickCount = 0;" );
        w.Line( "while ( m_running.load() )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "if ( !OnTick( ++tickCount ) ) { break; }" );
        w.Line( "for ( int i = 0; i < 10 && m_running.load(); ++i )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::this_thread::sleep_for( ::std::chrono::milliseconds( 100 ) );" );
        w.Dedent();
        w.Line( "}" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // Phase 12: OnStop + Cleanup
        w.Line( "// Phase 12 — Shutdown" );
        w.Line( "OnStop();" );
        w.Line( "skel.StopOfferService();" );
        w.Line( "m_pSkeleton = nullptr;" );
        w.Line( "pCoreIpc->Shutdown();" );
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "if ( pDds ) { pDds->Shutdown(); }" );
        }
        w.Line( "dispatcher.Shutdown();" );
        w.Line( "if ( dispThread.joinable() ) { dispThread.join(); }" );
        w.Line( "mgr.Shutdown();" );
        w.Line( "::std::cout << \"[ServerApp] Goodbye.\" << ::std::endl;" );
        w.Line( "return 0;" );

        w.Dedent();
        w.Line( "}" );
    }

    // ==================== Method Handler Wiring ====================

    void CServerAppGenerator::generateMethodHandlerWiring( CCodeWriter& w,
                                                            const Interface& iface ) const noexcept {
        for ( const auto& method : iface.methods ) {
            String methodCamel  = ToCamelCase( method.name );
            String methodPascal = ToPascalCase( method.name );

            // Build lambda parameter list
            String paramList;
            String argForward;
            for ( ::std::size_t i = 0; i < method.inArgs.size(); ++i ) {
                if ( i > 0 ) { paramList += ", "; argForward += ", "; }
                String cppType = resolveCppType( method.inArgs[i].type );
                paramList += cppType + " " + method.inArgs[i].name;
                argForward += "::std::move( " + method.inArgs[i].name + " )";
            }

            if ( method.isFireAndForget ) {
                w.Line( "skel." + methodCamel + ".RegisterMethodHandler(" );
                w.Line( "    [this]( " + paramList + " )" );
                w.Line( "    { On" + methodPascal + "( " + argForward + " ); } );" );
            } else {
                // Determine output type
                String outType;
                if ( method.outArgs.empty() ) {
                    outType = "void";
                } else if ( method.outArgs.size() == 1 ) {
                    outType = resolveCppType( method.outArgs[0].type );
                } else {
                    outType = ToPascalCase( method.name ) + "Output";
                }
                String futureType = ( outType == "void" )
                    ? "::lap::core::Future< void >"
                    : "::lap::core::Future< " + outType + " >";

                w.Line( "skel." + methodCamel + ".RegisterMethodHandler(" );
                w.Line( "    [this]( " + paramList + " ) -> " + futureType );
                w.Line( "    { return On" + methodPascal + "( " + argForward + " ); } );" );
            }
        }
    }

    // ==================== Field Handler Wiring ====================

    void CServerAppGenerator::generateFieldHandlerWiring( CCodeWriter& w,
                                                           const Interface& iface ) const noexcept {
        for ( const auto& attr : iface.attributes ) {
            String fieldCamel  = ToCamelCase( attr.name );
            String fieldPascal = ToPascalCase( attr.name );
            String cppType = resolveCppType( attr.type );

            // Getter
            w.Line( "skel." + fieldCamel + ".RegisterGetHandler(" );
            w.Line( "    [this]() -> ::lap::core::Future< " + cppType + " >" );
            w.Line( "    { return OnGet" + fieldPascal + "(); } );" );

            // Setter (only if not readonly)
            if ( !attr.isReadonly ) {
                Bool byRef = ( cppType == "String" || cppType.find( "vector" ) != String::npos
                            || cppType.find( "::" ) != String::npos );
                String paramDecl = byRef
                    ? "const " + cppType + "& value"
                    : "const " + cppType + "& value";

                w.Line( "skel." + fieldCamel + ".RegisterSetHandler(" );
                w.Line( "    [this]( " + paramDecl + " ) -> ::lap::core::Future< void >" );
                w.Line( "    { return OnSet" + fieldPascal + "( value ); } );" );
            }
        }
    }

} // namespace generator
} // namespace com
} // namespace lap
