/**
 * @file        CSchemaHash.cpp
 * @author      Aii
 * @brief       Schema Hash Computation Implementation (SHA-256)
 * @date        2026/02/09
 * @details     Implements SHA-256 per FIPS 180-4 for schema hash generation.
 *              Produces a 16-character hex string from the first 64 bits.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CSchemaHash.hpp"

// ==================== Standard Library Headers ====================
#include <cstring>
#include <sstream>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== SHA-256 Constants (FIPS 180-4) ====================

    namespace
    {
        constexpr UInt32 kK[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
            0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        constexpr UInt32 kH0[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };

        inline UInt32 rotr( UInt32 x, UInt32 n ) noexcept {
            return ( x >> n ) | ( x << ( 32 - n ) );
        }

        inline UInt32 ch( UInt32 x, UInt32 y, UInt32 z ) noexcept {
            return ( x & y ) ^ ( ~x & z );
        }

        inline UInt32 maj( UInt32 x, UInt32 y, UInt32 z ) noexcept {
            return ( x & y ) ^ ( x & z ) ^ ( y & z );
        }

        inline UInt32 sigma0( UInt32 x ) noexcept {
            return rotr( x, 2 ) ^ rotr( x, 13 ) ^ rotr( x, 22 );
        }

        inline UInt32 sigma1( UInt32 x ) noexcept {
            return rotr( x, 6 ) ^ rotr( x, 11 ) ^ rotr( x, 25 );
        }

        inline UInt32 gamma0( UInt32 x ) noexcept {
            return rotr( x, 7 ) ^ rotr( x, 18 ) ^ ( x >> 3 );
        }

        inline UInt32 gamma1( UInt32 x ) noexcept {
            return rotr( x, 17 ) ^ rotr( x, 19 ) ^ ( x >> 10 );
        }

    } // anonymous namespace

    // ==================== SHA-256 Implementation ====================

    ::std::array< UInt8, 32 > CSchemaHash::sha256Raw(
        const UInt8* data, ::std::size_t length ) noexcept {
        UInt32 h[8];
        ::std::memcpy( h, kH0, sizeof( h ) );

        // Pre-processing: padding
        ::std::size_t bitLen = length * 8;
        ::std::size_t paddedLen = ( ( length + 8 ) / 64 + 1 ) * 64;

        ::std::vector< UInt8 > padded( paddedLen, 0 );
        ::std::memcpy( padded.data(), data, length );
        padded[length] = 0x80;

        // Append bit length as big-endian 64-bit
        for ( ::std::size_t i = 8; i > 0; --i ) {
            padded[paddedLen - i] = static_cast< UInt8 >(
                ( bitLen >> ( ( i - 1 ) * 8 ) ) & 0xFF );
        }

        // Process each 512-bit (64-byte) block
        for ( ::std::size_t offset = 0; offset < paddedLen; offset += 64 ) {
            UInt32 w[64];

            // Prepare message schedule
            for ( ::std::size_t i = 0; i < 16; ++i ) {
                w[i] = ( static_cast< UInt32 >( padded[offset + i * 4 + 0] ) << 24 )
                     | ( static_cast< UInt32 >( padded[offset + i * 4 + 1] ) << 16 )
                     | ( static_cast< UInt32 >( padded[offset + i * 4 + 2] ) <<  8 )
                     | ( static_cast< UInt32 >( padded[offset + i * 4 + 3] ) );
            }

            for ( Int32 i = 16; i < 64; ++i ) {
                w[i] = gamma1( w[i - 2] ) + w[i - 7]
                     + gamma0( w[i - 15] ) + w[i - 16];
            }

            // Initialize working variables
            UInt32 a = h[0], b = h[1], c = h[2], d = h[3];
            UInt32 e = h[4], f = h[5], g = h[6], hh = h[7];

            // 64 rounds
            for ( Int32 i = 0; i < 64; ++i ) {
                UInt32 t1 = hh + sigma1( e ) + ch( e, f, g ) + kK[i] + w[i];
                UInt32 t2 = sigma0( a ) + maj( a, b, c );
                hh = g;
                g  = f;
                f  = e;
                e  = d + t1;
                d  = c;
                c  = b;
                b  = a;
                a  = t1 + t2;
            }

            h[0] += a; h[1] += b; h[2] += c; h[3] += d;
            h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }

        // Output hash as bytes (big-endian)
        ::std::array< UInt8, 32 > result;
        for ( ::std::size_t i = 0; i < 8; ++i ) {
            result[i * 4 + 0] = static_cast< UInt8 >( ( h[i] >> 24 ) & 0xFF );
            result[i * 4 + 1] = static_cast< UInt8 >( ( h[i] >> 16 ) & 0xFF );
            result[i * 4 + 2] = static_cast< UInt8 >( ( h[i] >>  8 ) & 0xFF );
            result[i * 4 + 3] = static_cast< UInt8 >( ( h[i]       ) & 0xFF );
        }

        return result;
    }

    // ==================== Public API ====================

    String CSchemaHash::Compute( const FidlModel& model ) noexcept {
        String serialized = serializeModel( model );
        String fullHash = ComputeSha256( serialized );
        // Return first 16 characters (64 bits)
        return fullHash.substr( 0, 16 );
    }

    String CSchemaHash::ComputeSha256( const String& input ) noexcept {
        auto hash = sha256Raw(
            reinterpret_cast< const UInt8* >( input.data() ), input.size() );
        return ToHex( hash.data(), hash.size() );
    }

    UInt16 CSchemaHash::GenerateServiceId( const String& qualifiedName ) noexcept {
        // FNV-1a 64-bit hash
        UInt64 hash = 0xcbf29ce484222325ULL;
        for ( auto c : qualifiedName ) {
            hash ^= static_cast< UInt64 >( static_cast< UInt8 >( c ) );
            hash *= 0x100000001b3ULL;
        }

        // Fold to 16-bit, mask to valid slot range (1-1022)
        UInt16 id = static_cast< UInt16 >(
            ( ( hash >> 16 ) ^ hash ) & 0xFFFF );

        // Ensure valid range: avoid slot 0 (reserved) and slot 1023 (broadcast)
        if ( id == 0 ) { id = 1; }
        if ( id >= 1023 ) { id = static_cast< UInt16 >( id & 0x03FE ); }
        if ( id == 0 ) { id = 1; }

        return id;
    }

    // ==================== Serialization ====================

    String CSchemaHash::serializeModel( const FidlModel& model ) noexcept {
        ::std::ostringstream ss;
        ss << "{\"package\":\"" << model.packageName << "\",";
        ss << "\"typeCollections\":[";

        for ( ::std::size_t ti = 0; ti < model.typeCollections.size(); ++ti ) {
            const auto& tc = model.typeCollections[ti];
            if ( ti > 0 ) { ss << ","; }
            ss << "{\"name\":\"" << tc.name << "\",";
            ss << "\"version\":{\"major\":" << tc.version.major
               << ",\"minor\":" << tc.version.minor
               << ",\"patch\":" << tc.version.patch << "},";

            // Enums
            ss << "\"enums\":[";
            for ( ::std::size_t i = 0; i < tc.enums.size(); ++i ) {
                if ( i > 0 ) { ss << ","; }
                ss << "{\"name\":\"" << tc.enums[i].name << "\",\"values\":[";
                for ( ::std::size_t j = 0; j < tc.enums[i].enumerators.size(); ++j ) {
                    if ( j > 0 ) { ss << ","; }
                    ss << "{\"" << tc.enums[i].enumerators[j].name
                       << "\":" << tc.enums[i].enumerators[j].value << "}";
                }
                ss << "]}";
            }
            ss << "],";

            // Structs
            ss << "\"structs\":[";
            for ( ::std::size_t i = 0; i < tc.structs.size(); ++i ) {
                if ( i > 0 ) { ss << ","; }
                ss << "{\"name\":\"" << tc.structs[i].name << "\",\"fields\":[";
                for ( ::std::size_t j = 0; j < tc.structs[i].fields.size(); ++j ) {
                    if ( j > 0 ) { ss << ","; }
                    ss << "{\"type\":\"" << tc.structs[i].fields[j].type.name
                       << "\",\"name\":\"" << tc.structs[i].fields[j].name << "\"}";
                }
                ss << "]}";
            }
            ss << "]}";
        }
        ss << "],";

        // Interfaces
        ss << "\"interfaces\":[";
        for ( ::std::size_t ii = 0; ii < model.interfaces.size(); ++ii ) {
            const auto& iface = model.interfaces[ii];
            if ( ii > 0 ) { ss << ","; }
            ss << "{\"name\":\"" << iface.name << "\",";
            ss << "\"version\":{\"major\":" << iface.version.major
               << ",\"minor\":" << iface.version.minor << "},";

            // Methods
            ss << "\"methods\":[";
            for ( ::std::size_t i = 0; i < iface.methods.size(); ++i ) {
                if ( i > 0 ) { ss << ","; }
                const auto& m = iface.methods[i];
                ss << "{\"name\":\"" << m.name << "\",\"in\":[";
                for ( ::std::size_t j = 0; j < m.inArgs.size(); ++j ) {
                    if ( j > 0 ) { ss << ","; }
                    ss << "\"" << m.inArgs[j].type.name << "\"";
                }
                ss << "],\"out\":[";
                for ( ::std::size_t j = 0; j < m.outArgs.size(); ++j ) {
                    if ( j > 0 ) { ss << ","; }
                    ss << "\"" << m.outArgs[j].type.name << "\"";
                }
                ss << "]}";
            }
            ss << "],";

            // Broadcasts
            ss << "\"broadcasts\":[";
            for ( ::std::size_t i = 0; i < iface.broadcasts.size(); ++i ) {
                if ( i > 0 ) { ss << ","; }
                ss << "{\"name\":\"" << iface.broadcasts[i].name << "\"}";
            }
            ss << "],";

            // Attributes
            ss << "\"attributes\":[";
            for ( ::std::size_t i = 0; i < iface.attributes.size(); ++i ) {
                if ( i > 0 ) { ss << ","; }
                ss << "{\"name\":\"" << iface.attributes[i].name
                   << "\",\"type\":\"" << iface.attributes[i].type.name << "\"}";
            }
            ss << "]}";
        }
        ss << "]}";

        return ss.str();
    }

    // ==================== Hex Conversion ====================

    String CSchemaHash::ToHex( const UInt8* bytes, ::std::size_t count ) noexcept {
        static const Char kHexChars[] = "0123456789abcdef";
        String result;
        result.reserve( count * 2 );
        for ( ::std::size_t i = 0; i < count; ++i ) {
            result += kHexChars[( bytes[i] >> 4 ) & 0x0F];
            result += kHexChars[bytes[i] & 0x0F];
        }
        return result;
    }

} // namespace generator
} // namespace com
} // namespace lap
