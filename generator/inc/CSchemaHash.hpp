/**
 * @file        CSchemaHash.hpp
 * @author      Aii
 * @brief       Schema Hash Computation for Franca IDL Models
 * @date        2026/02/09
 * @details     Computes SHA-256 based schema hash for interface version verification.
 *              Used in the dual-layer IDL architecture for consistency guarantees.
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §5.2 — Schema Hash in dual-layer IDL architecture
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CSCHEMAHASH_HPP
#define LAP_COM_GENERATOR_CSCHEMAHASH_HPP

// ==================== Project-Internal Headers ====================
#include "CFidlAst.hpp"

// ==================== Standard Library Headers ====================
#include <array>

namespace lap
{
namespace com
{
namespace generator
{

    /**
     * @brief Schema hash computation utility
     * @note Generates a 16-character hex string from SHA-256 of the serialized model
     */
    class CSchemaHash {
    public:
        CSchemaHash() = delete;

        /**
         * @brief Compute schema hash for a FIDL model
         * @param model Parsed FIDL model
         * @return 16-character hex string (first 64 bits of SHA-256)
         */
        static String Compute( const FidlModel& model ) noexcept;

        /**
         * @brief Compute SHA-256 hash of a string
         * @param input Input string
         * @return 64-character hex string (full SHA-256)
         */
        static String ComputeSha256( const String& input ) noexcept;

        /**
         * @brief Generate a service ID from a qualified interface name
         * @param qualifiedName Fully qualified interface name (e.g., "com.lightap.example.Calculator")
         * @return UInt16 service ID (FNV-1a hash truncated)
         */
        static UInt16 GenerateServiceId( const String& qualifiedName ) noexcept;

        /**
         * @brief Convert bytes to hex string (public — used by generators)
         * @param bytes Input bytes
         * @param count Number of bytes to convert
         * @return Hex string
         */
        static String ToHex( const UInt8* bytes, ::std::size_t count ) noexcept;

    private:
        /**
         * @brief Serialize model to a canonical JSON-like string for hashing
         * @param model FIDL model
         * @return Serialized string
         */
        static String serializeModel( const FidlModel& model ) noexcept;

        /**
         * @brief SHA-256 block processing
         * @param data Input bytes
         * @return 32-byte hash
         */
        static ::std::array< UInt8, 32 > sha256Raw(
            const UInt8* data, ::std::size_t length ) noexcept;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CSCHEMAHASH_HPP
