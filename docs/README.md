# Com Module Documentation

> **[中文版](README_CN.md)**

> **Last Updated**: 2026-03-02  
> **AUTOSAR Standard**: Adaptive Platform R25-11

Technical documentation for the LightAP Com (Communication Management) module.

> **Quick Start**: Read [**guides/DEVELOPMENT_GUIDE.md**](guides/DEVELOPMENT_GUIDE.md) first — covers the full workflow from FIDL definition to code generation, build, and testing.

---

## Quick Navigation

### Getting Started (Recommended Reading Order)

| Step | Document | Description |
|------|----------|-------------|
| 1 | [**guides/DEVELOPMENT_GUIDE.md**](guides/DEVELOPMENT_GUIDE.md) | **Development Guide** — FIDL → Split Gen → Server/Client → CMake → Test |
| 2 | [architecture/ARCHITECTURE_SUMMARY.md](architecture/ARCHITECTURE_SUMMARY.md) | Com module architecture overview (v4.0) |
| 3 | [guides/BINDING_SELECTION_GUIDE.md](guides/BINDING_SELECTION_GUIDE.md) | Binding selection decision tree |
| 4 | [architecture/GENERATOR.md](architecture/GENERATOR.md) | lap-sidl-gen code generator architecture |

### By Use Case

| Use Case | Document |
|----------|----------|
| Develop a new service | [guides/DEVELOPMENT_GUIDE.md](guides/DEVELOPMENT_GUIDE.md) |
| Choose transport binding | [guides/BINDING_SELECTION_GUIDE.md](guides/BINDING_SELECTION_GUIDE.md) |
| DDS integration | [guides/DDS_INTEGRATION_GUIDE.md](guides/DDS_INTEGRATION_GUIDE.md) |
| Transport comparison | [architecture/TRANSPORT_MATRIX.md](architecture/TRANSPORT_MATRIX.md) |
| AUTOSAR compliance | [guides/AUTOSAR_QUICK_REFERENCE.md](guides/AUTOSAR_QUICK_REFERENCE.md) |
| Extend with new binding | [architecture/BINDING_ARCHITECTURE.md](architecture/BINDING_ARCHITECTURE.md) |

---

## Binding Status

| Binding | Status | Latency | Use Case |
|---------|--------|---------|----------|
| **CoreIPC** | ✅ Production | < 1µs (64B) | Intra-ECU, zero-copy SHM |
| **DDS** | ✅ Production | < 10µs SHM / < 30µs UDP | Cross-ECU, QoS, discovery |
| SOME/IP | ⚠️ Planned | — | AUTOSAR CP interop |
| Socket | ⚠️ Planned | — | Lightweight IPC, legacy |
| D-Bus | ⚠️ Planned | — | systemd integration |

---

## Directory Structure

### [architecture/](architecture/) — Architecture Design (7 docs)

| Document | Description |
|----------|-------------|
| `ARCHITECTURE_SUMMARY.md` | Module architecture overview (v4.0, CoreIPC + DDS) |
| `BINDING_ARCHITECTURE.md` | Binding layer design (ITransportBinding NVI interface) |
| `GENERATOR.md` | lap-sidl-gen generator architecture (v1.0) |
| `SERVICE_DISCOVERY_ARCHITECTURE.md` | Service discovery (v3.1, Dual-layer IDL) |
| `SECURITY_ARCHITECTURE_SUMMARY.md` | Security architecture summary |
| `TRANSPORT_MATRIX.md` | Transport protocol status matrix |
| `YAML_CONFIGURATION_SUMMARY.md` | YAML configuration design |

### [guides/](guides/) — Development Guides (6 docs)

| Document | Description | Priority |
|----------|-------------|----------|
| **`DEVELOPMENT_GUIDE.md`** | **Full development workflow (v2.0)** — Split Gen, App Framework, 3 examples | ⭐⭐⭐ |
| `BINDING_SELECTION_GUIDE.md` | Binding selection decision tree (CoreIPC ✅ / DDS ✅) | ⭐⭐⭐ |
| `AUTOSAR_QUICK_REFERENCE.md` | AUTOSAR AP COM API quick reference (R25-11) | ⭐⭐⭐ |
| `DDS_INTEGRATION_GUIDE.md` | DDS (Fast-DDS 3.x) integration guide | ⭐⭐ |
| `AUTOSAR_REQUIREMENTS_TRACEABILITY.md` | Requirements traceability matrix (R25-11) | ⭐⭐ |
| `AUTOSAR_R24-11_SERVICE_DISCOVERY_REFERENCE.md` | Service discovery standard reference | ⭐⭐ |

### [reports/](reports/) — Implementation Reports (21 docs)

Implementation status, compliance checks, and phase summary reports.

### [planning/](planning/) — Planning Documents (7 docs)

Development roadmap, implementation plans, and architecture update summaries.

### [archive/](archive/) — Archived Documents (12 docs)

Deprecated designs and outdated documents. For historical reference only.

---

## Recent Changes

- **2026-03-02**: Guides cleanup — 7 outdated planning docs archived; BINDING_SELECTION_GUIDE v2.0 rewritten; R23-11 → R25-11
- **2026-03-02**: DEVELOPMENT_GUIDE v2.0 — Split Gen isolation, App Framework, HelloWorld3 DDS-only example
- **2026-03-01**: All 3 HelloWorld examples migrated to split gen_server/gen_client architecture
- **2025-11-24**: Service discovery architecture upgraded to v3.1 (Dual-layer IDL: Franca + DDS)

