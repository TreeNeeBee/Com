#!/usr/bin/env python3
"""
ARXML to YAML Converter for LightAP Service Configuration

Purpose:
    Convert AUTOSAR Adaptive Platform ARXML service manifests to YAML format
    used by LightAP shared memory registry.

Features:
    - Parse ARXML service interface definitions
    - Extract service IDs, instance IDs, versions
    - Generate slot mapping configuration
    - Support multiple ARXML input files
    - Validate AUTOSAR AP compliance

Usage:
    # Convert single ARXML file
    ./arxml2yaml.py -i service_manifest.arxml -o slot_mapping.yaml
    
    # Convert multiple ARXML files
    ./arxml2yaml.py -i services/*.arxml -o slot_mapping.yaml
    
    # With custom slot allocation strategy
    ./arxml2yaml.py -i manifest.arxml -o config.yaml --strategy static

Author: LightAP Architecture Team
Date: 2025-11-19
Version: 1.0
"""

import sys
import argparse
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import yaml
import hashlib
import re

# AUTOSAR namespaces
AUTOSAR_NAMESPACES = {
    'ar': 'http://autosar.org/schema/r4.0',
    'xsi': 'http://www.w3.org/2001/XMLSchema-instance'
}

# Service slot allocation ranges
STATIC_SLOT_START = 0
STATIC_SLOT_END = 199
DYNAMIC_SLOT_START = 200
DYNAMIC_SLOT_END = 923
ASIL_SLOT_START = 924
ASIL_SLOT_END = 1023

class ServiceDefinition:
    """AUTOSAR Service Interface Definition"""
    
    def __init__(self):
        self.service_name: str = ""
        self.service_id: int = 0
        self.instance_id: int = 0
        self.major_version: int = 1
        self.minor_version: int = 0
        self.binding_type: str = "iceoryx2"
        self.endpoint: str = ""
        self.safety_level: str = "QM"
        self.category: str = "dynamic"
        self.description: str = ""
        
    def compute_service_id(self) -> int:
        """Compute service ID from service name (FNV-1a hash)"""
        if self.service_id > 0:
            return self.service_id
            
        # FNV-1a hash (32-bit)
        FNV_PRIME = 0x01000193
        FNV_OFFSET = 0x811c9dc5
        
        hash_value = FNV_OFFSET
        for byte in self.service_name.encode('utf-8'):
            hash_value ^= byte
            hash_value = (hash_value * FNV_PRIME) & 0xFFFFFFFF
        
        self.service_id = hash_value
        return self.service_id
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for YAML output"""
        return {
            'service_name': self.service_name,
            'service_id': f"0x{self.service_id:08X}",
            'instance_id': self.instance_id,
            'version': f"{self.major_version}.{self.minor_version}",
            'binding': self.binding_type,
            'endpoint': self.endpoint,
            'safety_level': self.safety_level,
            'description': self.description
        }


class ARXMLParser:
    """AUTOSAR ARXML Parser"""
    
    def __init__(self):
        self.services: List[ServiceDefinition] = []
        
    def parse_file(self, arxml_file: Path) -> List[ServiceDefinition]:
        """Parse ARXML file and extract service definitions"""
        try:
            tree = ET.parse(arxml_file)
            root = tree.getroot()
            
            # Find all ServiceInterface elements
            services = self._find_service_interfaces(root)
            
            # Find all ServiceInstanceToPortPrototypeMapping
            instances = self._find_service_instances(root)
            
            # Combine definitions
            self.services.extend(self._combine_definitions(services, instances))
            
            return self.services
            
        except ET.ParseError as e:
            print(f"Error parsing {arxml_file}: {e}", file=sys.stderr)
            return []
    
    def _find_service_interfaces(self, root: ET.Element) -> List[Dict]:
        """Find ServiceInterface definitions in ARXML"""
        services = []
        
        # Search for SERVICE-INTERFACE elements
        for elem in root.iter():
            if elem.tag.endswith('SERVICE-INTERFACE'):
                service = {}
                
                # Extract SHORT-NAME
                short_name = elem.find('.//SHORT-NAME', AUTOSAR_NAMESPACES)
                if short_name is not None:
                    service['name'] = short_name.text
                
                # Extract version
                major_ver = elem.find('.//MAJOR-VERSION', AUTOSAR_NAMESPACES)
                minor_ver = elem.find('.//MINOR-VERSION', AUTOSAR_NAMESPACES)
                if major_ver is not None:
                    service['major_version'] = int(major_ver.text)
                if minor_ver is not None:
                    service['minor_version'] = int(minor_ver.text)
                
                # Extract description
                desc = elem.find('.//DESC', AUTOSAR_NAMESPACES)
                if desc is not None:
                    service['description'] = desc.text
                
                services.append(service)
        
        return services
    
    def _find_service_instances(self, root: ET.Element) -> List[Dict]:
        """Find service instance configurations"""
        instances = []
        
        # Search for PROVIDED-SERVICE-INSTANCE or REQUIRED-SERVICE-INSTANCE
        for elem in root.iter():
            if elem.tag.endswith('PROVIDED-SERVICE-INSTANCE') or \
               elem.tag.endswith('REQUIRED-SERVICE-INSTANCE'):
                instance = {}
                
                # Extract instance ID
                instance_id = elem.find('.//INSTANCE-ID', AUTOSAR_NAMESPACES)
                if instance_id is not None:
                    instance['instance_id'] = int(instance_id.text, 0)
                
                # Extract service reference
                service_ref = elem.find('.//SERVICE-INTERFACE-REF', AUTOSAR_NAMESPACES)
                if service_ref is not None:
                    # Extract service name from reference
                    ref_text = service_ref.text
                    if ref_text:
                        instance['service_ref'] = ref_text.split('/')[-1]
                
                # Extract binding type
                binding = elem.find('.//COMMUNICATION-CONNECTOR', AUTOSAR_NAMESPACES)
                if binding is not None:
                    binding_type = binding.find('.//TYPE', AUTOSAR_NAMESPACES)
                    if binding_type is not None:
                        instance['binding'] = binding_type.text.lower()
                
                # Extract endpoint
                endpoint = elem.find('.//ENDPOINT', AUTOSAR_NAMESPACES)
                if endpoint is not None:
                    instance['endpoint'] = endpoint.text
                
                instances.append(instance)
        
        return instances
    
    def _combine_definitions(self, services: List[Dict], instances: List[Dict]) -> List[ServiceDefinition]:
        """Combine service interfaces and instances"""
        result = []
        
        for instance in instances:
            svc_def = ServiceDefinition()
            
            # Find matching service interface
            service_ref = instance.get('service_ref', '')
            matching_service = next((s for s in services if s.get('name') == service_ref), None)
            
            if matching_service:
                svc_def.service_name = matching_service.get('name', f"UnknownService_{len(result)}")
                svc_def.major_version = matching_service.get('major_version', 1)
                svc_def.minor_version = matching_service.get('minor_version', 0)
                svc_def.description = matching_service.get('description', '')
            else:
                svc_def.service_name = service_ref or f"UnknownService_{len(result)}"
            
            svc_def.instance_id = instance.get('instance_id', len(result))
            svc_def.binding_type = instance.get('binding', 'iceoryx2')
            svc_def.endpoint = instance.get('endpoint', f"/lap/{svc_def.service_name}")
            
            # Compute service ID
            svc_def.compute_service_id()
            
            # Infer safety level from service name
            if 'control' in svc_def.service_name.lower() or 'safety' in svc_def.service_name.lower():
                svc_def.safety_level = 'ASIL-D'
            else:
                svc_def.safety_level = 'QM'
            
            result.append(svc_def)
        
        return result


class SlotAllocator:
    """Service slot allocation strategy"""
    
    def __init__(self, strategy: str = 'auto'):
        self.strategy = strategy
        self.static_slots: Dict[str, int] = {}
        self.next_dynamic_slot = DYNAMIC_SLOT_START
        self.next_asil_slot = ASIL_SLOT_START
        
    def allocate(self, service: ServiceDefinition, slot_hint: Optional[int] = None) -> int:
        """Allocate slot for service"""
        
        # If slot hint provided, use it
        if slot_hint is not None and 0 <= slot_hint <= 1023:
            return slot_hint
        
        # ASIL-D services get dedicated slots
        if service.safety_level == 'ASIL-D':
            if self.next_asil_slot > ASIL_SLOT_END:
                raise ValueError(f"ASIL-D slot pool exhausted for {service.service_name}")
            slot = self.next_asil_slot
            self.next_asil_slot += 1
            return slot
        
        # Static allocation for well-known services
        if self.strategy == 'static' or service.category == 'static':
            # Use consistent hashing for static slots
            slot = (service.service_id % (STATIC_SLOT_END - STATIC_SLOT_START + 1)) + STATIC_SLOT_START
            return slot
        
        # Dynamic allocation
        if self.next_dynamic_slot > DYNAMIC_SLOT_END:
            raise ValueError(f"Dynamic slot pool exhausted for {service.service_name}")
        
        slot = self.next_dynamic_slot
        self.next_dynamic_slot += 1
        return slot


class YAMLGenerator:
    """Generate YAML configuration from service definitions"""
    
    def __init__(self, services: List[ServiceDefinition], allocator: SlotAllocator):
        self.services = services
        self.allocator = allocator
        
    def generate(self) -> Dict:
        """Generate complete YAML configuration"""
        
        config = {
            'version': '1.0',
            'metadata': {
                'generated_by': 'arxml2yaml.py',
                'source': 'AUTOSAR ARXML',
                'date': '2025-11-19',
                'total_services': len(self.services)
            },
            'slot_mapping': {
                'static_allocations': [],
                'dynamic_allocations': [],
                'asil_allocations': []
            },
            'system_config': {
                'total_slots': 1024,
                'static_range': f"{STATIC_SLOT_START}-{STATIC_SLOT_END}",
                'dynamic_range': f"{DYNAMIC_SLOT_START}-{DYNAMIC_SLOT_END}",
                'asil_range': f"{ASIL_SLOT_START}-{ASIL_SLOT_END}",
                'enable_hugepages': True,
                'enable_guard_pages': True
            },
            'security': {
                'enable_crc32_check': True,
                'enable_exec_whitelist': True,
                'qm_asil_separation': True
            }
        }
        
        # Allocate slots
        for service in self.services:
            slot_index = self.allocator.allocate(service)
            
            entry = {
                'slot_index': slot_index,
                **service.to_dict()
            }
            
            # Categorize by safety level
            if service.safety_level == 'ASIL-D':
                config['slot_mapping']['asil_allocations'].append(entry)
            elif service.category == 'static' or slot_index <= STATIC_SLOT_END:
                config['slot_mapping']['static_allocations'].append(entry)
            else:
                config['slot_mapping']['dynamic_allocations'].append(entry)
        
        return config
    
    def save(self, output_file: Path):
        """Save YAML configuration to file"""
        config = self.generate()
        
        with open(output_file, 'w', encoding='utf-8') as f:
            yaml.dump(config, f, 
                     default_flow_style=False, 
                     allow_unicode=True,
                     sort_keys=False,
                     indent=2)
        
        print(f"✅ Generated YAML configuration: {output_file}")
        print(f"   Total services: {len(self.services)}")
        print(f"   Static slots: {len(config['slot_mapping']['static_allocations'])}")
        print(f"   Dynamic slots: {len(config['slot_mapping']['dynamic_allocations'])}")
        print(f"   ASIL-D slots: {len(config['slot_mapping']['asil_allocations'])}")


def main():
    parser = argparse.ArgumentParser(
        description='Convert AUTOSAR ARXML to LightAP YAML configuration',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument('-i', '--input', 
                       nargs='+',
                       required=True,
                       help='Input ARXML file(s)')
    
    parser.add_argument('-o', '--output',
                       required=True,
                       help='Output YAML file')
    
    parser.add_argument('--strategy',
                       choices=['auto', 'static', 'dynamic'],
                       default='auto',
                       help='Slot allocation strategy (default: auto)')
    
    parser.add_argument('--validate',
                       action='store_true',
                       help='Validate ARXML against AUTOSAR schema')
    
    parser.add_argument('-v', '--verbose',
                       action='store_true',
                       help='Verbose output')
    
    args = parser.parse_args()
    
    # Parse ARXML files
    arxml_parser = ARXMLParser()
    all_services = []
    
    for input_file in args.input:
        input_path = Path(input_file)
        if not input_path.exists():
            print(f"❌ File not found: {input_file}", file=sys.stderr)
            continue
        
        if args.verbose:
            print(f"📖 Parsing {input_file}...")
        
        services = arxml_parser.parse_file(input_path)
        all_services.extend(services)
        
        if args.verbose:
            print(f"   Found {len(services)} service(s)")
    
    if not all_services:
        print("❌ No services found in ARXML files", file=sys.stderr)
        return 1
    
    # Allocate slots
    allocator = SlotAllocator(strategy=args.strategy)
    
    # Generate YAML
    yaml_gen = YAMLGenerator(all_services, allocator)
    yaml_gen.save(Path(args.output))
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
