/**
 * @file        CClientAppGenerator.cpp
 * @author      Aii
 * @brief       Client Application Framework Code Generator Implementation
 * @date        2026/03/01
 * @details     Generates <Service>ClientApp.hpp with:
 *              - Complete dual-binding client framework (CoreIPC + DDS)
 *              - Virtual event handlers (user overrides to receive events)
 *              - Convenience method call wrappers
 *              - Convenience field operation wrappers
 *              - Lifecycle hooks (OnConnected / OnDisconnected / OnTick)
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CClientAppGenerator.hpp"

// ==================== Standard Library Headers ====================
#include <iostream>
#include <filesystem>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== CClientAppGenerator Implementation ====================

    Bool CClientAppGenerator::Generate( const FidlModel& model,
                                        const GeneratorConfig& config ) {
        auto nsSegments = ExtractNamespaceSegments( model.packageName,
                                                    config.namespacePrefix );

        for ( const auto& iface : model.interfaces ) {
            CCodeWriter w;
            String className = iface.name + "ClientApp";
            String filename  = className + ".hpp";
            String guard = buildGuardMacro( nsSegments, className );

            // ==================== File Header ====================
            writeFileHeader( w, filename,
                "Auto-generated client application framework for " + iface.name,
                model.sourceFile, config.author );
            w.Line();

            writeGuardOpen( w, guard );
            w.Line();

            // ==================== Includes ====================
            w.Line( "// ==================== Generated Headers ====================" );
            w.Line( "#include \"" + iface.name + "Proxy.hpp\"" );
            if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
                w.Line( "#include \"" + iface.name + "DdsAdapter.hpp\"" );
            }
            w.Line();
            w.Line( "// ==================== Binding / Infrastructure ====================" );
            w.Line( "#include \"CoreIPCBinding.hpp\"" );
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
            w.Line( "#include <thread>" );
            w.Line();

            // ==================== Namespaces ====================
            writeNamespaceOpen( w, nsSegments );
            writeLapComUsings( w, nsSegments );
            w.Line();

            w.Line( "namespace client_app" );
            w.Line( "{" );
            w.Line();

            // ==================== ClientApp Class ====================
            String proxyClass = iface.name + "Proxy";
            String proxyFQ    = "proxy::" + proxyClass;

            w.Line( "/**" );
            w.Line( " * @brief Client application framework for " + iface.name );
            if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
                w.Line( " * @details Encapsulates all dual-binding boilerplate (CoreIPC + DDS)." );
            } else {
                w.Line( " * @details Encapsulates all CoreIPC binding boilerplate." );
            }
            w.Line( " *          User subclasses and overrides only the needed callbacks:" );
            w.Line( " *          - Event handlers (virtual, no-op default)" );
            w.Line( " *          - Lifecycle hooks (OnConnected / OnDisconnected / OnTick)" );
            w.Line( " *          Uses convenience wrappers to call methods and access fields." );
            w.Line( " *" );
            w.Line( " * Usage:" );
            w.Line( " *   class MyClient : public " + className + " {" );
            w.Line( " *       void OnGreeting( ... ) override { ... }" );
            w.Line( " *   };" );
            w.Line( " *   int main() { MyClient c; return c.Run(); }" );
            w.Line( " */" );
            w.Line( "class " + className );
            w.Line( "{" );

            // ==================== Public Section ====================
            w.Line( "public:" );
            w.Indent();

            // Virtual destructor
            w.Line( "virtual ~" + className + "() = default;" );
            w.Line();

            // ==================== Virtual Event Handlers ====================
            w.Line( "// ==================== Event Handlers (override to receive events) ====================" );
            w.Line();
            generateEventHandlerDecls( w, iface );
            w.Line();

            // ==================== Field Change Handlers ====================
            w.Line( "// ==================== Field Notifications (override to receive updates) ====================" );
            w.Line();
            for ( const auto& attr : iface.attributes ) {
                if ( !attr.isNotify ) { continue; }
                String fieldPascal = ToPascalCase( attr.name );
                String cppType = resolveCppType( attr.type );
                Bool byRef = ( cppType == "String" || cppType.find( "vector" ) != String::npos
                            || cppType.find( "::" ) != String::npos );
                String paramType = byRef ? "const " + cppType + "&" : cppType;

                w.Line( "/// Notification: " + attr.name + " changed" );
                w.Line( "virtual void On" + fieldPascal + "Changed( " + paramType + " /* value */ ) {}" );
                w.Line();
            }

            // ==================== Virtual Lifecycle Hooks ====================
            w.Line( "// ==================== Lifecycle Hooks (optional overrides) ====================" );
            w.Line();
            w.Line( "/// Called after proxy is created and events are subscribed" );
            w.Line( "virtual void OnConnected() {}" );
            w.Line();
            w.Line( "/// Called before unsubscription and shutdown" );
            w.Line( "virtual void OnDisconnected() {}" );
            w.Line();
            w.Line( "/// Called each tick (~1s). Return false to stop the client." );
            w.Line( "virtual bool OnTick( UInt32 /* tickCount */ ) { return true; }" );
            w.Line();

            // ==================== Method Call Wrappers ====================
            w.Line( "// ==================== Method Calls (convenience wrappers) ====================" );
            w.Line();
            generateMethodWrappers( w, iface );
            w.Line();

            // ==================== Field Operation Wrappers ====================
            w.Line( "// ==================== Field Operations (convenience wrappers) ====================" );
            w.Line();
            generateFieldWrappers( w, iface );
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
            w.Line( "/// Check if client is running" );
            w.Line( "bool IsRunning() const noexcept { return m_running.load(); }" );
            w.Line();

            w.Dedent();

            // ==================== Protected Section ====================
            w.Line( "protected:" );
            w.Indent();
            w.Line( "/// Access the underlying proxy (for advanced use)" );
            w.Line( proxyFQ + "* GetProxy() { return m_pProxy; }" );
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
            w.Line( proxyFQ + "* m_pProxy = nullptr;" );
            w.Dedent();

            // Close class
            w.Line( "}; // class " + className );

            w.Line();
            w.Line( "} // namespace client_app" );
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

    // ==================== Virtual Event Handler Declarations ====================

    void CClientAppGenerator::generateEventHandlerDecls( CCodeWriter& w,
                                                          const Interface& iface ) const noexcept {
        for ( const auto& bc : iface.broadcasts ) {
            String bcPascal = ToPascalCase( bc.name );
            String eventType = bcPascal + "Event";

            // Each event handler receives the event struct
            w.Line( "/// Handle " + bc.name + " event" );
            w.Line( "virtual void On" + bcPascal + "( const " + eventType + "& /* data */ ) {}" );
            w.Line();
        }
    }

    // ==================== Method Call Wrappers ====================

    void CClientAppGenerator::generateMethodWrappers( CCodeWriter& w,
                                                       const Interface& iface ) const noexcept {
        for ( const auto& method : iface.methods ) {
            String methodCamel  = ToCamelCase( method.name );
            String methodPascal = ToPascalCase( method.name );

            // Build argument list
            String paramList;
            String argForward;
            for ( ::std::size_t i = 0; i < method.inArgs.size(); ++i ) {
                if ( i > 0 ) { paramList += ", "; argForward += ", "; }
                String cppType = resolveCppType( method.inArgs[i].type );
                Bool byRef = ( cppType == "String" || cppType.find( "vector" ) != String::npos
                            || cppType.find( "::" ) != String::npos );
                paramList += ( byRef ? "const " + cppType + "& " : cppType + " " )
                           + method.inArgs[i].name;
                argForward += method.inArgs[i].name;
            }

            if ( method.isFireAndForget ) {
                w.Line( "/// Call " + method.name + " (fire-and-forget)" );
                w.Line( "void " + methodPascal + "( " + paramList + " )" );
                w.Line( "{" );
                w.Indent();
                w.Line( "if ( m_pProxy ) { m_pProxy->" + methodCamel + "( " + argForward + " ); }" );
                w.Dedent();
                w.Line( "}" );
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

                String resultType = ( outType == "void" )
                    ? "::lap::core::Result< void >"
                    : "::lap::core::Result< " + outType + " >";

                w.Line( "/// Call " + method.name + " → " + outType );
                w.Line( resultType + " " + methodPascal + "( " + paramList + " )" );
                w.Line( "{" );
                w.Indent();
                w.Line( "if ( !m_pProxy ) { return " + resultType + "::FromError("
                        " ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }" );
                w.Line( "return m_pProxy->" + methodCamel + "( " + argForward + " );" );
                w.Dedent();
                w.Line( "}" );
            }
            w.Line();
        }
    }

    // ==================== Field Operation Wrappers ====================

    void CClientAppGenerator::generateFieldWrappers( CCodeWriter& w,
                                                      const Interface& iface ) const noexcept {
        for ( const auto& attr : iface.attributes ) {
            String fieldCamel  = ToCamelCase( attr.name );
            String fieldPascal = ToPascalCase( attr.name );
            String cppType = resolveCppType( attr.type );
            String resultType = "::lap::core::Result< " + cppType + " >";
            Bool byRef = ( cppType == "String" || cppType.find( "vector" ) != String::npos
                        || cppType.find( "::" ) != String::npos );

            // Getter
            w.Line( "/// Get " + attr.name + " value" );
            w.Line( resultType + " Get" + fieldPascal + "()" );
            w.Line( "{" );
            w.Indent();
            w.Line( "if ( !m_pProxy ) { return " + resultType + "::FromError("
                    " ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }" );
            w.Line( "return m_pProxy->" + fieldCamel + ".Get();" );
            w.Dedent();
            w.Line( "}" );
            w.Line();

            // Setter (only if not readonly)
            if ( !attr.isReadonly ) {
                String paramType = byRef
                    ? "const " + cppType + "&"
                    : cppType;

                w.Line( "/// Set " + attr.name + " value" );
                w.Line( "void Set" + fieldPascal + "( " + paramType + " value )" );
                w.Line( "{" );
                w.Indent();
                w.Line( "if ( m_pProxy ) { m_pProxy->" + fieldCamel + ".Set( value ); }" );
                w.Dedent();
                w.Line( "}" );
                w.Line();
            }
        }
    }

    // ==================== Run() Method — The Complete Client Framework ====================

    void CClientAppGenerator::generateRunMethod( CCodeWriter& w,
                                                  const Interface& iface,
                                                  const FidlModel& /* model */,
                                                  const GeneratorConfig& config,
                                                  const ::std::vector< String >& /* nsSegments */ ) const noexcept {
        String proxyClass = iface.name + "Proxy";
        String proxyFQ    = "proxy::" + proxyClass;
        String className  = iface.name + "ClientApp";

        w.Line( "/**" );
        w.Line( " * @brief Run the client (blocks until Stop() or SIGINT/SIGTERM)" );
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

        w.Line( "::std::cout << \"=== \" << " + proxyFQ + "::kServiceName"
                " << \" Client (" + ( HasBinding( config.bindingLayers, kBindingDds )
                    ? String( "Dual-Binding" ) : String( "CoreIPC" ) )
                + ") ===\" << ::std::endl;" );
        w.Line();

        // Phase 1: CoreIPC Binding
        w.Line( "// Phase 1 — CoreIPC Binding" );
        w.Line( "auto pCoreIpc = ::lap::com::MakeShared< ::lap::com::binding::CoreIPCBinding >();" );
        w.Line( "auto ipcR = pCoreIpc->Initialize();" );
        w.Line( "if ( !ipcR )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ClientApp] CoreIPC init failed: \"" );
        w.Line( "            << ipcR.Error().Message() << ::std::endl;" );
        w.Line( "return 1;" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "::std::this_thread::sleep_for( ::std::chrono::milliseconds( 200 ) );" );
        w.Line();

        // Phase 2: DDS Binding (only in dual-binding mode)
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "// Phase 2 — DDS Binding (graceful fallback if unavailable)" );
            w.Line( "auto pDds = ::lap::com::MakeShared< ::lap::com::binding::DdsBinding >();" );
            w.Line( "auto ddsR = pDds->Initialize();" );
            w.Line( "if ( !ddsR )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "::std::cerr << \"[ClientApp] DDS init failed — CoreIPC-only mode\" << ::std::endl;" );
            w.Line( "pDds.reset();" );
            w.Dedent();
            w.Line( "}" );
            w.Line();
        }

        // Phase 3: BindingManager
        w.Line( "// Phase 3 — BindingManager Registration" );
        w.Line( "auto& mgr = ::lap::com::binding::BindingManager::GetInstance();" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::lap::com::binding::BindingConfig cfg;" );
        w.Line( "cfg.name     = \"coreipc-client\";" );
        w.Line( "cfg.priority = ::lap::com::binding::BindingPriority::kCoreIpc;" );
        w.Line( "cfg.enabled  = true;" );
        w.Line( "auto r = mgr.RegisterBinding( cfg, pCoreIpc );" );
        w.Line( "if ( !r.HasValue() )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ClientApp] RegisterBinding(CoreIPC) failed\" << ::std::endl;" );
        w.Line( "pCoreIpc->Shutdown();" );
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
            w.Line( "cfg.name     = \"dds-client\";" );
            w.Line( "cfg.priority = ::lap::com::binding::BindingPriority::kDds;" );
            w.Line( "cfg.enabled  = true;" );
            w.Line( "auto r = mgr.RegisterBinding( cfg, pDds );" );
            w.Line( "if ( !r.HasValue() ) { pDds->Shutdown(); pDds.reset(); }" );
            w.Dedent();
            w.Line( "}" );
        }
        w.Line();

        // Phase 4: DDS Type Adapters (only in dual-binding mode)
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "// Phase 4 — DDS Type Adapters" );
            w.Line( "if ( pDds )" );
            w.Line( "{" );
            w.Indent();
            w.Line( "dds_adapter::Register" + iface.name + "DdsAdapters(" );
            w.Line( "    " + proxyFQ + "::kServiceId );" );
            w.Dedent();
            w.Line( "}" );
            w.Line();
        }

        // Phase 5: Service Discovery
        w.Line( "// Phase 5 — Service Discovery (unified 3-step)" );
        w.Line( "::std::cout << \"[ClientApp] Discovering service ...\" << ::std::endl;" );
        w.Line( "bool found = false;" );
        w.Line( "for ( int attempt = 0; attempt < 30 && !found && m_running.load(); ++attempt )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "auto result = pCoreIpc->FindService( " + proxyFQ + "::kServiceId );" );
        w.Line( "if ( result.HasValue() && !result.Value().empty() ) { found = true; }" );
        w.Line( "if ( !found ) { ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 200 ) ); }" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "if ( !found )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ClientApp] Service not found. Is the server running?\" << ::std::endl;" );
        w.Line( "pCoreIpc->Shutdown();" );
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "if ( pDds ) { pDds->Shutdown(); }" );
        }
        w.Line( "mgr.Shutdown();" );
        w.Line( "return 1;" );
        w.Dedent();
        w.Line( "}" );
        w.Line();

        // Phase 6: Create Proxy
        w.Line( "// Phase 6 — Create Proxy" );
        w.Line( "using HandleType = " + proxyFQ + "::HandleType;" );
        w.Line( "HandleType handle( static_cast< ::lap::com::InstanceIdentifierType >(" );
        w.Line( "    " + proxyFQ + "::kServiceId & 0xFFFFU ) );" );
        w.Line( "auto proxyResult = " + proxyFQ + "::Create( handle );" );
        w.Line( "if ( !proxyResult.HasValue() )" );
        w.Line( "{" );
        w.Indent();
        w.Line( "::std::cerr << \"[ClientApp] Proxy::Create failed\" << ::std::endl;" );
        w.Line( "pCoreIpc->Shutdown();" );
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "if ( pDds ) { pDds->Shutdown(); }" );
        }
        w.Line( "mgr.Shutdown();" );
        w.Line( "return 1;" );
        w.Dedent();
        w.Line( "}" );
        w.Line( "auto proxy = ::std::move( proxyResult ).Value();" );
        w.Line( "m_pProxy = &proxy;" );
        w.Line( "::std::cout << \"[ClientApp] Connected.\" << ::std::endl;" );
        w.Line( "::std::this_thread::sleep_for( ::std::chrono::milliseconds( 100 ) );" );
        w.Line();

        // Phase 7: Subscribe to events
        w.Line( "// Phase 7 — Subscribe to Events → virtual handlers" );
        generateEventSubscriptionWiring( w, iface );
        w.Line();

        // Phase 7.5: Subscribe to notifiable fields
        for ( const auto& attr : iface.attributes ) {
            if ( !attr.isNotify ) { continue; }
            String fieldCamel  = ToCamelCase( attr.name );
            String fieldPascal = ToPascalCase( attr.name );
            String cppType = resolveCppType( attr.type );

            w.Line( "proxy." + fieldCamel + ".Subscribe();" );
            w.Line( "proxy." + fieldCamel + ".SetReceiveHandler( [this, &proxy]() {" );
            w.Indent();
            w.Line( "auto s = proxy." + fieldCamel + ".GetNextSample();" );
            w.Line( "if ( s.HasValue() && s.Value() ) { On" + fieldPascal + "Changed( *s.Value() ); }" );
            w.Dedent();
            w.Line( "} );" );
        }
        w.Line();

        // Phase 8: OnConnected
        w.Line( "// Phase 8 — User lifecycle hook" );
        w.Line( "OnConnected();" );
        w.Line();

        // Phase 9: Main loop
        w.Line( "// Phase 9 — Main loop (~1s tick)" );
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

        // Phase 10: Cleanup
        w.Line( "// Phase 10 — Shutdown" );
        w.Line( "OnDisconnected();" );
        generateEventUnsubscription( w, iface );
        for ( const auto& attr : iface.attributes ) {
            if ( !attr.isNotify ) { continue; }
            w.Line( "proxy." + ToCamelCase( attr.name ) + ".Unsubscribe();" );
        }
        w.Line( "m_pProxy = nullptr;" );
        w.Line( "pCoreIpc->Shutdown();" );
        if ( HasBinding( config.bindingLayers, kBindingDds ) ) {
            w.Line( "if ( pDds ) { pDds->Shutdown(); }" );
        }
        w.Line( "mgr.Shutdown();" );
        w.Line( "::std::cout << \"[ClientApp] Goodbye.\" << ::std::endl;" );
        w.Line( "return 0;" );

        w.Dedent();
        w.Line( "}" );
    }

    // ==================== Event Subscription Wiring ====================

    void CClientAppGenerator::generateEventSubscriptionWiring( CCodeWriter& w,
                                                                const Interface& iface ) const noexcept {
        for ( const auto& bc : iface.broadcasts ) {
            String bcPascal = ToPascalCase( bc.name );
            String bcCamel  = ToCamelCase( bc.name );

            w.Line( "proxy." + bcCamel + ".Subscribe();" );
            w.Line( "proxy." + bcCamel + ".SetReceiveHandler( [this, &proxy]() {" );
            w.Indent();
            w.Line( "auto s = proxy." + bcCamel + ".GetNextSample();" );
            w.Line( "if ( s.HasValue() && s.Value() ) { On" + bcPascal + "( *s.Value() ); }" );
            w.Dedent();
            w.Line( "} );" );
        }
    }

    // ==================== Event Unsubscription ====================

    void CClientAppGenerator::generateEventUnsubscription( CCodeWriter& w,
                                                            const Interface& iface ) const noexcept {
        for ( const auto& bc : iface.broadcasts ) {
            w.Line( "proxy." + ToCamelCase( bc.name ) + ".Unsubscribe();" );
        }
    }

} // namespace generator
} // namespace com
} // namespace lap
