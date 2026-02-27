/**
 * @file        CTypesGenerator.hpp
 * @author      Aii
 * @brief       C++ Types Code Generator from Franca IDL
 * @date        2026/02/09
 * @details     Generates <Service>Types.hpp containing enums, structs, typedefs,
 *              method output structs, and broadcast event structs.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */
#ifndef LAP_COM_GENERATOR_CTYPESGENERATOR_HPP
#define LAP_COM_GENERATOR_CTYPESGENERATOR_HPP

// ==================== Project-Internal Headers ====================
#include "IGenerator.hpp"
#include "CSchemaHash.hpp"

namespace lap
{
namespace com
{
namespace generator
{

    /**
     * @brief Generates C++ type definition headers from Franca IDL models
     * @note Output conforms to project code style (ComTypes.hpp aliases, k-prefix enums, etc.)
     */
    class CTypesGenerator : public IGenerator {
    public:
        CTypesGenerator() noexcept = default;
        ~CTypesGenerator() noexcept override = default;

        // Non-copyable, non-movable [Rule of Five]
        CTypesGenerator( const CTypesGenerator& )            = delete;
        CTypesGenerator& operator=( const CTypesGenerator& ) = delete;
        CTypesGenerator( CTypesGenerator&& )                  = delete;
        CTypesGenerator& operator=( CTypesGenerator&& )       = delete;

        /**
         * @brief Generate <Interface>Types.hpp files
         * @param model  Parsed FIDL model
         * @param config Generator configuration
         * @return true on success
         */
        Bool Generate( const FidlModel& model,
                       const GeneratorConfig& config ) override;

    private:
        void generateEnum( CCodeWriter& w, const EnumDef& enumDef ) const noexcept;
        void generateStruct( CCodeWriter& w, const StructDef& structDef ) const noexcept;
        void generateTypedef( CCodeWriter& w, const TypedefDef& typedefDef ) const noexcept;
        void generateArray( CCodeWriter& w, const ArrayDef& arrayDef ) const noexcept;
        void generateMap( CCodeWriter& w, const MapDef& mapDef ) const noexcept;
        void generateMethodOutput( CCodeWriter& w, const MethodDef& method ) const noexcept;
        void generateBroadcastEvent( CCodeWriter& w, const BroadcastDef& broadcast ) const noexcept;
        void generateTypeCollection( CCodeWriter& w, const TypeCollection& tc ) const noexcept;
        void generateStructSerialize( CCodeWriter& w, const String& structName,
            const ::std::vector< Field >& fields ) const noexcept;
        void generateStructDeserialize( CCodeWriter& w, const String& structName,
            const ::std::vector< Field >& fields ) const noexcept;
        void generateSerializationTraits( CCodeWriter& w, const Interface& iface,
            const FidlModel& model ) const noexcept;

        void generateCommonClass( CCodeWriter& w, const Interface& iface,
            const FidlModel& model,
            const GeneratorConfig& config ) const noexcept;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CTYPESGENERATOR_HPP
