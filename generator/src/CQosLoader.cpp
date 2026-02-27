/**
 * @file        CQosLoader.cpp
 * @author      Aii
 * @brief       QoS configuration loader — YAML parser implementation
 * @date        2026/02/09
 * @details     Implements a zero-external-dependency minimal YAML parser for
 *              com_config.yaml and service_deploy.yaml.  Only standard C++17
 *              library is used.  The parser handles:
 *                - Key-value pairs at arbitrary indentation
 *                - Block-sequence items (leading "- ")
 *                - Quoted and unquoted string values
 *                - "#" line comments
 *
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CQosLoader.hpp"

// ==================== Standard Library Headers ====================
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== Static Helpers ====================

    String CQosLoader::Trim( const String& s ) noexcept {
        const ::std::size_t first = s.find_first_not_of( " \t\r\n" );
        if ( first == String::npos ) {
            return {};
        }
        const ::std::size_t last = s.find_last_not_of( " \t\r\n" );
        return s.substr( first, last - first + 1U );
    }

    String CQosLoader::Unquote( const String& s ) noexcept {
        if ( s.size() >= 2U &&
             ( ( s.front() == '"'  && s.back() == '"'  ) ||
               ( s.front() == '\'' && s.back() == '\'' ) ) ) {
            return s.substr( 1U, s.size() - 2U );
        }
        return s;
    }

    ::std::pair< String, String > CQosLoader::SplitKV( const String& line ) noexcept {
        // Strip "- " list item prefix if present
        String work = line;
        if ( work.size() >= 2U && work[0] == '-' && work[1] == ' ' ) {
            work = work.substr( 2U );
        }

        const ::std::size_t colon = work.find( ':' );
        if ( colon == String::npos ) {
            return { work, {} };
        }

        String key   = Trim( work.substr( 0U, colon ) );
        String value = Trim( work.substr( colon + 1U ) );

        // Strip inline comment from value
        const ::std::size_t hashPos = value.find( " #" );
        if ( hashPos != String::npos ) {
            value = Trim( value.substr( 0U, hashPos ) );
        }

        return { key, Unquote( value ) };
    }

    Int32 CQosLoader::IndentLevel( const String& line ) noexcept {
        Int32 count = 0;
        for ( const Char c : line ) {
            if ( c == ' ' ) {
                ++count;
            } else if ( c == '\t' ) {
                count += 2;  // treat tab as 2 spaces
            } else {
                break;
            }
        }
        return count;
    }

    ElementKind CQosLoader::ParseElementKind( const String& s ) noexcept {
        if ( s == "event" )                          { return ElementKind::kEvent; }
        if ( s == "method" )                         { return ElementKind::kMethod; }
        if ( s == "fire_and_forget" )                { return ElementKind::kFireAndForget; }
        if ( s == "field" || s == "attribute" )      { return ElementKind::kField; }
        return ElementKind::kEvent;   // safe default
    }

    QosParams CQosLoader::DefaultQosParams( ElementKind kind ) noexcept {
        QosParams p;
        switch ( kind ) {
            case ElementKind::kEvent:
                // §11.3.6: event built-in default — BEST_EFFORT / VOLATILE / KEEP_LAST-1
                p.reliability  = "BEST_EFFORT";
                p.durability   = "VOLATILE";
                p.history      = "KEEP_LAST";
                p.historyDepth = 1;
                break;
            case ElementKind::kFireAndForget:
                // fire-and-forget — BEST_EFFORT, no timeout
                p.reliability  = "BEST_EFFORT";
                p.durability   = "VOLATILE";
                p.history      = "KEEP_LAST";
                p.historyDepth = 1;
                p.timeoutMs    = 0;
                p.retryCount   = 0;
                break;
            case ElementKind::kMethod:
                // §11.3.6: method built-in default — RELIABLE / 5000ms timeout / 0 retries
                p.reliability  = "RELIABLE";
                p.durability   = "VOLATILE";
                p.history      = "KEEP_LAST";
                p.historyDepth = 10;
                p.timeoutMs    = 5000;
                p.retryCount   = 0;
                break;
            case ElementKind::kField:
                // §11.3.6: field built-in default — RELIABLE / TRANSIENT_LOCAL / KEEP_LAST-1
                p.reliability  = "RELIABLE";
                p.durability   = "TRANSIENT_LOCAL";
                p.history      = "KEEP_LAST";
                p.historyDepth = 1;
                break;
            default:
                break;
        }
        return p;
    }

    Bool CQosLoader::ApplyQosField( QosParams&    params,
                                    const String& key,
                                    const String& value ) noexcept {
        if ( key == "reliability" ) {
            params.reliability = value;
            return true;
        }
        if ( key == "durability" ) {
            params.durability = value;
            return true;
        }
        if ( key == "history" ) {
            params.history = value;
            return true;
        }
        if ( key == "history_depth" ) {
            try { params.historyDepth = ::std::stoi( value ); } catch ( ... ) {}
            return true;
        }
        if ( key == "deadline_ms" ) {
            try { params.deadlineMs = ::std::stoi( value ); } catch ( ... ) {}
            return true;
        }
        if ( key == "timeout_ms" ) {
            try { params.timeoutMs = ::std::stoi( value ); } catch ( ... ) {}
            return true;
        }
        if ( key == "retry_count" ) {
            try { params.retryCount = ::std::stoi( value ); } catch ( ... ) {}
            return true;
        }
        return false;
    }

    // ==================== com_config.yaml Parser ====================

    /**
     * @brief Parse com_config.yaml
     *
     * Expected schema (indentation is 2 spaces):
     * @code
     * qos_profiles:
     *   - name: ProfileName
     *     reliability: RELIABLE
     *     durability: VOLATILE
     *     history: KEEP_LAST
     *     history_depth: 10
     *     deadline_ms: 0
     *     timeout_ms: 5000
     *     retry_count: 3
     * @endcode
     */
    Bool CQosLoader::loadComConfig( const String& path ) {
        ::std::ifstream ifs( path );
        if ( !ifs.is_open() ) {
            ::std::cerr << "[CQosLoader] Cannot open com_config: " << path << "\n";
            return false;
        }

        enum class State : UInt8 { kRoot = 0U, kProfilesList = 1U, kProfileBody = 2U };
        State   state          = State::kRoot;
        String  currentName;
        QosParams currentParams;

        String line;
        while ( ::std::getline( ifs, line ) ) {
            // Strip carriage return
            if ( !line.empty() && line.back() == '\r' ) {
                line.pop_back();
            }

            // Skip blank lines and full-line comments
            const String trimmed = Trim( line );
            if ( trimmed.empty() || trimmed[0] == '#' ) {
                continue;
            }

            const Int32  indent     = IndentLevel( line );
            const Bool   isListItem = ( trimmed.size() >= 2U &&
                                        trimmed[0] == '-' && trimmed[1] == ' ' );
            const auto [ key, val ] = SplitKV( trimmed );

            switch ( state ) {
                case State::kRoot:
                    if ( key == "qos_profiles" ) {
                        state = State::kProfilesList;
                    }
                    break;

                case State::kProfilesList:
                    // A list item at indent=2 starts a new profile
                    if ( isListItem && indent <= 2 ) {
                        // Save previous profile if any
                        if ( !currentName.empty() ) {
                            mProfiles[currentName] = currentParams;
                        }
                        currentName   = String{};
                        currentParams = QosParams{};
                        state         = State::kProfileBody;

                        // The list-item dash itself may carry "name: X" inline
                        const String rest = Trim( trimmed.substr( 2U ) );
                        if ( !rest.empty() ) {
                            const auto [ k2, v2 ] = SplitKV( rest );
                            if ( k2 == "name" ) {
                                currentName = v2;
                            } else {
                                ApplyQosField( currentParams, k2, v2 );
                            }
                        }
                    } else if ( indent == 0 ) {
                        // Back to root level — another top-level key
                        state = State::kRoot;
                    }
                    break;

                case State::kProfileBody:
                    if ( isListItem && indent <= 2 ) {
                        // New profile — save current and start fresh
                        if ( !currentName.empty() ) {
                            mProfiles[currentName] = currentParams;
                        }
                        currentName   = String{};
                        currentParams = QosParams{};

                        const String rest = Trim( trimmed.substr( 2U ) );
                        if ( !rest.empty() ) {
                            const auto [ k2, v2 ] = SplitKV( rest );
                            if ( k2 == "name" ) {
                                currentName = v2;
                            } else {
                                ApplyQosField( currentParams, k2, v2 );
                            }
                        }
                    } else if ( indent == 0 ) {
                        // Back to root — save current profile and exit
                        if ( !currentName.empty() ) {
                            mProfiles[currentName] = currentParams;
                            currentName.clear();
                        }
                        state = State::kRoot;
                        if ( key == "qos_profiles" ) {
                            state = State::kProfilesList;
                        }
                    } else if ( !isListItem ) {
                        if ( key == "name" ) {
                            currentName = val;
                        } else {
                            ApplyQosField( currentParams, key, val );
                        }
                    }
                    break;
            }
        }

        // Save last profile
        if ( !currentName.empty() ) {
            mProfiles[currentName] = currentParams;
        }

        return true;
    }

    // ==================== service_deploy.yaml Parser ====================

    /**
     * @brief Parse service_deploy.yaml
     *
     * Expected schema (indentation is 2 spaces per level):
     * @code
     * services:
     *   - interface: Calculator
     *     instance_id: 5
     *     elements:
     *       - name: divide
     *         kind: method
     *         qos_profile: MyProfile
     *       - name: speed
     *         kind: event
     *         qos:
     *           reliability: RELIABLE
     *           durability: VOLATILE
     * @endcode
     */
    Bool CQosLoader::loadServiceDeploy( const String& path ) {
        ::std::ifstream ifs( path );
        if ( !ifs.is_open() ) {
            ::std::cerr << "[CQosLoader] Cannot open service_deploy: " << path << "\n";
            return false;
        }

        enum class State : UInt8 {
            kRoot         = 0U,
            kServicesList = 1U,
            kServiceBody  = 2U,
            kElementsList = 3U,
            kElementBody  = 4U,
            kQosBody      = 5U
        };

        State              state = State::kRoot;
        ServiceDeployEntry curService;
        ElementQosBinding  curElem;
        Bool               inElemQos   = false;
        Int32              qosIndent   = 0;

        auto saveElement = [&]() {
            if ( !curElem.elementName.empty() ) {
                curService.elements.push_back( curElem );
                curElem = ElementQosBinding{};
            }
        };

        auto saveService = [&]() {
            if ( !curService.interfaceName.empty() ) {
                saveElement();
                mServiceDeploys.push_back( curService );
                curService = ServiceDeployEntry{};
            }
        };

        String line;
        while ( ::std::getline( ifs, line ) ) {
            if ( !line.empty() && line.back() == '\r' ) {
                line.pop_back();
            }

            const String trimmed = Trim( line );
            if ( trimmed.empty() || trimmed[0] == '#' ) {
                continue;
            }

            const Int32  indent     = IndentLevel( line );
            const Bool   isListItem = ( trimmed.size() >= 2U &&
                                        trimmed[0] == '-' && trimmed[1] == ' ' );
            const auto [ key, val ] = SplitKV( trimmed );

            // Helper: parse a list item's first key-value inline (after the "- ")
            auto parseInlineKV = [&]( const String& t ) -> ::std::pair< String, String > {
                if ( t.size() >= 2U && t[0] == '-' && t[1] == ' ' ) {
                    return SplitKV( Trim( t.substr( 2U ) ) );
                }
                return SplitKV( t );
            };

            switch ( state ) {
                // ---------------------------------------------------------
                case State::kRoot:
                    if ( key == "services" ) {
                        state = State::kServicesList;
                    }
                    break;

                // ---------------------------------------------------------
                case State::kServicesList:
                    if ( isListItem && indent <= 2 ) {
                        saveService();
                        state = State::kServiceBody;
                        // Inline KV on the "- " list marker
                        const auto [ k2, v2 ] = parseInlineKV( trimmed );
                        if ( k2 == "interface" ) {
                            curService.interfaceName = v2;
                        }
                    } else if ( indent == 0 ) {
                        saveService();
                        state = State::kRoot;
                    }
                    break;

                // ---------------------------------------------------------
                case State::kServiceBody:
                    if ( isListItem && indent <= 2 ) {
                        // New service
                        saveService();
                        const auto [ k2, v2 ] = parseInlineKV( trimmed );
                        if ( k2 == "interface" ) {
                            curService.interfaceName = v2;
                        }
                    } else if ( indent == 0 ) {
                        saveService();
                        state = State::kRoot;
                        if ( key == "services" ) {
                            state = State::kServicesList;
                        }
                    } else if ( !isListItem ) {
                        if ( key == "interface" ) {
                            curService.interfaceName = val;
                        } else if ( key == "instance_id" ) {
                            try {
                                curService.instanceId =
                                    static_cast< UInt16 >( ::std::stoul( val, nullptr, 0 ) );
                            } catch ( ... ) {}
                        } else if ( key == "elements" ) {
                            state = State::kElementsList;
                        }
                    }
                    break;

                // ---------------------------------------------------------
                case State::kElementsList:
                    if ( isListItem && indent >= 4 && indent <= 6 ) {
                        saveElement();
                        state = State::kElementBody;
                        inElemQos = false;
                        const auto [ k2, v2 ] = parseInlineKV( trimmed );
                        if ( k2 == "name" ) {
                            curElem.elementName = v2;
                        }
                    } else if ( indent <= 2 ) {
                        // Back to service level — check for new service or new key
                        if ( isListItem ) {
                            saveService();
                            const auto [ k2, v2 ] = parseInlineKV( trimmed );
                            if ( k2 == "interface" ) {
                                curService.interfaceName = v2;
                            }
                            state = State::kServiceBody;
                        } else if ( indent == 0 ) {
                            saveService();
                            state = State::kRoot;
                        } else {
                            // indent 2~3: still inside service body
                            saveElement();
                            state = State::kServiceBody;
                            // Re-process as service body key
                            if ( key == "instance_id" ) {
                                try {
                                    curService.instanceId =
                                        static_cast< UInt16 >( ::std::stoul( val, nullptr, 0 ) );
                                } catch ( ... ) {}
                            } else if ( key == "elements" ) {
                                state = State::kElementsList;
                            }
                        }
                    }
                    break;

                // ---------------------------------------------------------
                case State::kElementBody:
                    if ( isListItem && indent >= 4 && indent <= 6 ) {
                        // New element
                        saveElement();
                        inElemQos = false;
                        const auto [ k2, v2 ] = parseInlineKV( trimmed );
                        if ( k2 == "name" ) {
                            curElem.elementName = v2;
                        }
                    } else if ( isListItem && indent <= 2 ) {
                        // Back to service level
                        saveService();
                        const auto [ k2, v2 ] = parseInlineKV( trimmed );
                        if ( k2 == "interface" ) {
                            curService.interfaceName = v2;
                        }
                        state = State::kServiceBody;
                    } else if ( !isListItem ) {
                        if ( inElemQos && indent >= qosIndent ) {
                            // Inside qos: sub-block
                            ApplyQosField( curElem.overrides, key, val );
                            curElem.hasOverrides = true;
                        } else {
                            inElemQos = false;
                            if ( key == "name" ) {
                                curElem.elementName = val;
                            } else if ( key == "kind" ) {
                                curElem.kind = ParseElementKind( val );
                            } else if ( key == "qos_profile" ) {
                                curElem.profileName = val;
                            } else if ( key == "qos" ) {
                                // Entering inline qos sub-block
                                inElemQos    = true;
                                qosIndent    = indent + 1;
                                state        = State::kQosBody;
                            } else if ( indent <= 2 && key.empty() ) {
                                // dedent out of element
                            }
                        }
                    }
                    break;

                // ---------------------------------------------------------
                case State::kQosBody:
                    if ( indent >= qosIndent ) {
                        ApplyQosField( curElem.overrides, key, val );
                        curElem.hasOverrides = true;
                    } else {
                        // Dedented: we're out of the qos block
                        inElemQos = false;
                        state     = State::kElementBody;

                        // Re-process this line as element body
                        if ( isListItem && indent >= 4 ) {
                            saveElement();
                            const auto [ k2, v2 ] = parseInlineKV( trimmed );
                            if ( k2 == "name" ) {
                                curElem.elementName = v2;
                            }
                        } else if ( isListItem && indent <= 2 ) {
                            saveService();
                            const auto [ k2, v2 ] = parseInlineKV( trimmed );
                            if ( k2 == "interface" ) {
                                curService.interfaceName = v2;
                            }
                            state = State::kServiceBody;
                        } else if ( !isListItem && key == "elements" ) {
                            saveElement();
                            state = State::kElementsList;
                        } else if ( !isListItem ) {
                            if ( key == "name" )         { curElem.elementName = val; }
                            else if ( key == "kind" )    { curElem.kind = ParseElementKind( val ); }
                            else if ( key == "qos_profile" ) { curElem.profileName = val; }
                        }
                    }
                    break;
            }
        }

        // Flush any pending data
        saveService();
        return true;
    }

    // ==================== Public Interface ====================

    Bool CQosLoader::Load( const String& comConfigPath,
                           const String& serviceDeployPath ) {
        if ( !comConfigPath.empty() ) {
            if ( !loadComConfig( comConfigPath ) ) {
                return false;
            }
        }
        if ( !serviceDeployPath.empty() ) {
            if ( !loadServiceDeploy( serviceDeployPath ) ) {
                return false;
            }
        }
        return true;
    }

    QosParams CQosLoader::ResolveQoS( const String& interfaceName,
                                      const String& elementName,
                                      ElementKind   kind ) const noexcept {
        // Level 1: per-element override in service_deploy.yaml
        for ( const auto& svc : mServiceDeploys ) {
            if ( svc.interfaceName != interfaceName ) {
                continue;
            }
            for ( const auto& elem : svc.elements ) {
                if ( elem.elementName != elementName ) {
                    continue;
                }
                if ( elem.hasOverrides ) {
                    return elem.overrides;
                }
                // Level 2: named profile in com_config.yaml
                if ( !elem.profileName.empty() ) {
                    const auto it = mProfiles.find( elem.profileName );
                    if ( it != mProfiles.end() ) {
                        return it->second;
                    }
                }
                // If binding exists but has neither override nor valid profile → default
                break;
            }
        }

        // Level 3: built-in kind-specific defaults (§11.3.6)
        return DefaultQosParams( kind );
    }

    UInt16 CQosLoader::ResolveInstanceId( const String& interfaceName,
                                          UInt16         cliOverride ) const noexcept {
        // CLI flag takes highest priority
        if ( cliOverride != 0U ) {
            return cliOverride;
        }

        // service_deploy.yaml
        for ( const auto& svc : mServiceDeploys ) {
            if ( svc.interfaceName == interfaceName ) {
                return svc.instanceId;
            }
        }

        // Default
        return 1U;
    }

} // namespace generator
} // namespace com
} // namespace lap
