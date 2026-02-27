#!/usr/bin/env python3
"""
lap-gen-pipeline — LightAP 全流程代码生成工具

根据目标后端（coreipc / dds / all）执行完整的代码生成流水线：

  coreipc 流水线：
    Step 1: lap-sidl-gen --types --proxy --skeleton
              → <Interface>Types.hpp / Proxy.hpp / Skeleton.hpp
    Step 2: 生成 coreipc_config.yaml
              → 共享内存 slot 配置（供 Core IPC 注册中心使用）

  dds 流水线：
    Step 1: lap-sidl-gen --all
              → C++ 头文件 + <Interface>.idl + <Interface>_qos.xml
    Step 2: fastddsgen（可选，环境中未安装时发出警告并跳过）
              → <Interface>PubSubTypes.cxx / .h（DDS 序列化代码）

  all：同时执行 coreipc + dds

用法:
    python gen_pipeline.py -i Calculator.fidl -o gen/ --backend dds
    python gen_pipeline.py -i Calculator.fidl -o gen/ --backend coreipc
    python gen_pipeline.py -i Calculator.fidl -o gen/ --backend all \\
        --com-config config/com_config.yaml \\
        --service-deploy config/service_deploy.yaml

    # 只生成 coreipc_config.yaml（跳过 C++ 头文件）
    python gen_pipeline.py -i Calculator.fidl -o gen/ --backend coreipc --config-only

Author: Aii
Date:   2026-02-23
Version: 1.0
"""

import sys
import os
import re
import argparse
import subprocess
import shutil
import hashlib
import fnmatch
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Data classes (mirror a subset of CFidlAst.hpp)
# ---------------------------------------------------------------------------

@dataclass
class FidlVersion:
    major: int = 1
    minor: int = 0

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.0"


@dataclass
class FidlElement:
    """A method, broadcast or attribute inside an interface."""
    name: str
    kind: str          # "method" | "fire_and_forget" | "event" | "field"
    readonly: bool = False


@dataclass
class FidlInterface:
    name: str
    version: FidlVersion = field(default_factory=FidlVersion)
    elements: List[FidlElement] = field(default_factory=list)


@dataclass
class FidlModel:
    package_name: str = ""
    interface_name: str = ""   # first interface (single-interface .fidl assumed)
    version: FidlVersion = field(default_factory=FidlVersion)
    schema_hash: str = ""
    service_id: int = 0
    elements: List[FidlElement] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Minimal Franca IDL parser (Python-side, for config generation only)
# ---------------------------------------------------------------------------

class FidlParser:
    """
    Lightweight Franca IDL tokenizer + parser.
    Extracts: packageName, interface name+version, methods/broadcasts/attributes.
    Does NOT validate types — only element names and kinds are needed for config gen.
    """

    # Simple token categories
    _KW = {
        "package", "typeCollection", "interface", "version",
        "method", "broadcast", "attribute", "in", "out", "error",
        "fireAndForget", "readonly", "enumeration", "struct", "typedef",
        "major", "minor", "array", "of", "is",
    }

    def __init__(self, source: str, filename: str = "<input>") -> None:
        self._src = source
        self._filename = filename
        self._tokens: List[str] = []
        self._pos: int = 0

    # -- tokenize --

    def _tokenize(self) -> None:
        src = self._src
        i = 0
        tokens: List[str] = []
        while i < len(src):
            # skip block comment
            if src[i:i+2] == "/*":
                end = src.find("*/", i + 2)
                i = end + 2 if end != -1 else len(src)
                continue
            # skip line comment
            if src[i:i+2] == "//":
                end = src.find("\n", i)
                i = end + 1 if end != -1 else len(src)
                continue
            # skip whitespace
            if src[i].isspace():
                i += 1
                continue
            # punctuation
            if src[i] in "{}=":
                tokens.append(src[i])
                i += 1
                continue
            # string literal
            if src[i] == '"':
                j = src.find('"', i + 1)
                tokens.append(src[i:j+1])
                i = j + 1
                continue
            # number
            if src[i].isdigit() or (src[i] == '-' and i+1 < len(src) and src[i+1].isdigit()):
                j = i
                while j < len(src) and (src[j].isdigit() or src[j] in '.-'):
                    j += 1
                tokens.append(src[i:j])
                i = j
                continue
            # identifier / keyword (may contain '.')
            if src[i].isalpha() or src[i] == '_':
                j = i
                while j < len(src) and (src[j].isalnum() or src[j] in '_.'):
                    j += 1
                tokens.append(src[i:j])
                i = j
                continue
            # skip unknown
            i += 1
        self._tokens = tokens

    def _peek(self, offset: int = 0) -> str:
        idx = self._pos + offset
        return self._tokens[idx] if idx < len(self._tokens) else ""

    def _consume(self) -> str:
        tok = self._peek()
        self._pos += 1
        return tok

    def _expect(self, expected: str) -> None:
        tok = self._consume()
        if tok != expected:
            raise SyntaxError(
                f"{self._filename}: expected '{expected}', got '{tok}' "
                f"(near pos {self._pos})"
            )

    def _skip_block(self) -> None:
        """Skip from current position past the matching closing '}'."""
        depth = 0
        while self._pos < len(self._tokens):
            t = self._consume()
            if t == "{":
                depth += 1
            elif t == "}":
                depth -= 1
                if depth <= 0:
                    return

    def _parse_version(self) -> FidlVersion:
        self._expect("{")
        v = FidlVersion()
        while self._peek() != "}":
            key = self._consume()
            val = int(self._consume())
            if key == "major":
                v.major = val
            elif key == "minor":
                v.minor = val
        self._expect("}")
        return v

    def _parse_interface_body(self) -> Tuple[FidlVersion, List[FidlElement]]:
        version = FidlVersion()
        elements: List[FidlElement] = []
        self._expect("{")
        while self._peek() != "}":
            tok = self._consume()
            if tok == "version":
                version = self._parse_version()
            elif tok == "method":
                name = self._consume()
                is_ff = False
                if self._peek() == "fireAndForget":
                    self._consume()
                    is_ff = True
                kind = "fire_and_forget" if is_ff else "method"
                self._skip_block()
                elements.append(FidlElement(name=name, kind=kind))
            elif tok == "broadcast":
                name = self._consume()
                self._skip_block()
                elements.append(FidlElement(name=name, kind="event"))
            elif tok == "attribute":
                # attribute <Type> <Name> [readonly]
                _type_tok = self._consume()   # type name (may be qualified)
                name = self._consume()
                readonly = False
                if self._peek() == "readonly":
                    self._consume()
                    readonly = True
                elements.append(FidlElement(name=name, kind="field", readonly=readonly))
            elif tok in ("enumeration", "struct", "typedef", "typeCollection"):
                _name = self._consume()  # skip name
                self._skip_block()
            else:
                # unknown token — skip block if followed by '{'
                if self._peek() == "{":
                    self._skip_block()
        self._expect("}")
        return version, elements

    def parse(self) -> FidlModel:
        self._tokenize()
        model = FidlModel()

        while self._pos < len(self._tokens):
            tok = self._consume()
            if tok == "package":
                model.package_name = self._consume()
            elif tok == "typeCollection":
                _name = self._consume()
                self._skip_block()
            elif tok == "interface":
                name = self._consume()
                version, elements = self._parse_interface_body()
                if not model.interface_name:
                    model.interface_name = name
                    model.version = version
                    model.elements = elements
            else:
                pass  # top-level unknown — ignore

        return model


# ---------------------------------------------------------------------------
# Schema hash (FNV-1a, mirrors CSchemaHash::GenerateServiceId)
# ---------------------------------------------------------------------------

def _fnv1a32(data: bytes) -> int:
    FNV_PRIME  = 0x01000193
    FNV_OFFSET = 0x811c9dc5
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & 0xFFFFFFFF
    return h


def compute_schema_hash(fidl_source: str) -> str:
    """Compute a 16-hex schema hash from .fidl source (SHA-256, take first 8 bytes)."""
    return hashlib.sha256(fidl_source.encode()).hexdigest()[:16]


def compute_service_id(qualified_name: str) -> int:
    """Derive ServiceID [1, 1022] from qualified service name via FNV-1a32."""
    h = _fnv1a32(qualified_name.encode())
    return (h % 1022) + 1


# ---------------------------------------------------------------------------
# Tool discovery
# ---------------------------------------------------------------------------

def find_lap_sidl_gen(search_dirs: Optional[List[Path]] = None) -> Optional[Path]:
    """Locate lap-sidl-gen binary.  Checks search_dirs, then common build paths, then PATH."""
    candidates: List[Path] = []

    # Explicit search dirs first
    if search_dirs:
        for d in search_dirs:
            candidates.append(d / "lap_sidl_gen")
            candidates.append(d / "lap-sidl-gen")

    # Common relative locations wrt this script
    script_dir = Path(__file__).resolve().parent
    repo_root  = script_dir.parent.parent.parent  # tools/ → Com/ → modules/ → LightAP/
    candidates += [
        script_dir.parent / "generator" / "build" / "lap_sidl_gen",
        repo_root / "build" / "lap_sidl_gen",
        Path("./lap_sidl_gen"),
        Path("./lap-sidl-gen"),
    ]

    for c in candidates:
        if c.exists() and os.access(c, os.X_OK):
            return c

    # Fall back to PATH
    found = shutil.which("lap_sidl_gen") or shutil.which("lap-sidl-gen")
    return Path(found) if found else None


def find_fastddsgen() -> Optional[Path]:
    """Locate fastddsgen executable in PATH or common install locations."""
    found = shutil.which("fastddsgen")
    if found:
        return Path(found)
    for candidate in [
        Path("/usr/local/bin/fastddsgen"),
        Path("/opt/Fast-DDS/bin/fastddsgen"),
        Path("/opt/fastdds/bin/fastddsgen"),
    ]:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate
    return None


# ---------------------------------------------------------------------------
# Subprocess helper
# ---------------------------------------------------------------------------

def run_cmd(cmd: List[str], step: str) -> bool:
    """Run a command, print output, return True on success."""
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, capture_output=False, text=True)
    if result.returncode != 0:
        print(f"  [ERROR] {step} failed (exit {result.returncode})")
        return False
    return True


# ---------------------------------------------------------------------------
# CoreIPC YAML config generator
# ---------------------------------------------------------------------------

# QoS defaults per element kind  — mirrors CQosLoader built-in defaults
_COREIPC_QOS_DEFAULTS: Dict[str, Dict] = {
    "method": {
        "reliability": "RELIABLE",
        "durability":  "VOLATILE",
        "history":     "KEEP_LAST",
        "history_depth": 10,
        "timeout_ms":  5000,
        "retry_count": 0,
    },
    "fire_and_forget": {
        "reliability": "BEST_EFFORT",
        "durability":  "VOLATILE",
        "history":     "KEEP_LAST",
        "history_depth": 1,
        "timeout_ms":  0,
        "retry_count": 0,
    },
    "event": {
        "reliability": "BEST_EFFORT",
        "durability":  "VOLATILE",
        "history":     "KEEP_LAST",
        "history_depth": 1,
        "timeout_ms":  0,
        "retry_count": 0,
    },
    "field": {
        "reliability": "RELIABLE",
        "durability":  "TRANSIENT_LOCAL",
        "history":     "KEEP_LAST",
        "history_depth": 1,
        "timeout_ms":  0,
        "retry_count": 0,
    },
}

# IPCType mapping: how many publishers/subscribers per element kind
_COREIPC_IPC_TYPE: Dict[str, str] = {
    "method":          "kSPSC",   # one client → one server
    "fire_and_forget": "kSPMC",   # one client → all servers
    "event":           "kSPMC",   # one publisher → multiple subscribers
    "field":           "kSPMC",   # one publisher → multiple subscribers
}


def _write_yaml_line(lines: List[str], indent: int, text: str) -> None:
    lines.append("  " * indent + text)


def generate_coreipc_config(
    model: FidlModel,
    output_dir: Path,
    instance_id: int = 1,
) -> Path:
    """
    Generate coreipc_config.yaml — shared-memory slot configuration
    for the Core IPC registry.

    Format mirrors the slot_mapping.yaml consumed by arxml2yaml and
    the Core IPC ControlBlock allocation.
    """
    qualified_name = f"{model.package_name}.{model.interface_name}"
    schema_hash    = model.schema_hash or compute_schema_hash(qualified_name)
    service_id     = model.service_id  or compute_service_id(qualified_name)

    lines: List[str] = []

    # Header
    lines.append("# Auto-generated CoreIPC slot configuration")
    lines.append(f"# Source:      Franca IDL package '{model.package_name}'")
    lines.append(f"# Interface:   {model.interface_name} v{model.version}")
    lines.append(f"# SchemaHash:  {schema_hash}")
    lines.append(f"# Generator:   lap-gen-pipeline v1.0")
    lines.append(f"# AUTOSAR:     R25-11 (Core IPC binding)")
    lines.append("")

    # Service entry
    lines.append("service:")
    lines.append(f"  name:         {model.interface_name}")
    lines.append(f"  qualified:    \"{qualified_name}\"")
    lines.append(f"  service_id:   0x{service_id:04X}   # {service_id} — FNV-1a32 [{qualified_name}] % 1022 + 1")
    lines.append(f"  instance_id:  0x{instance_id:04X}   # {instance_id}")
    lines.append(f"  schema_hash:  \"{schema_hash}\"")
    lines.append(f"  version:")
    lines.append(f"    major: {model.version.major}")
    lines.append(f"    minor: {model.version.minor}")
    lines.append(f"  binding:      coreipc            # lap::core::ipc shared-memory")
    lines.append(f"  safety_level: QM")
    lines.append("")

    # Per-element slot definitions
    lines.append("  elements:")
    for idx, elem in enumerate(model.elements):
        slot_id = (service_id << 8 | idx) & 0xFFFF
        qos     = _COREIPC_QOS_DEFAULTS.get(elem.kind, _COREIPC_QOS_DEFAULTS["event"])
        ipc_type = _COREIPC_IPC_TYPE.get(elem.kind, "kSPMC")

        # Map generator kind to AUTOSAR element kind label
        kind_label_map = {
            "method":          "method",
            "fire_and_forget": "fire_and_forget",
            "event":           "event",
            "field":           "field",
        }
        kind_label = kind_label_map.get(elem.kind, elem.kind)

        lines.append(f"    - name:        {elem.name}")
        lines.append(f"      slot_id:     0x{slot_id:04X}")
        lines.append(f"      kind:        {kind_label}")
        lines.append(f"      ipc_type:    {ipc_type}    # lap::core::ipc::IPCType::{ipc_type}")
        if elem.readonly:
            lines.append(f"      readonly:    true")
        lines.append(f"      qos:")
        lines.append(f"        reliability:   {qos['reliability']}")
        lines.append(f"        durability:    {qos['durability']}")
        lines.append(f"        history:       {qos['history']}")
        lines.append(f"        history_depth: {qos['history_depth']}")
        if qos["timeout_ms"] > 0:
            lines.append(f"        timeout_ms:    {qos['timeout_ms']}")
        if qos["retry_count"] > 0:
            lines.append(f"        retry_count:   {qos['retry_count']}")
        lines.append("")

    output_path = output_dir / f"{model.interface_name}_coreipc_config.yaml"
    output_path.write_text("\n".join(lines))
    return output_path


# ---------------------------------------------------------------------------
# Pipeline steps
# ---------------------------------------------------------------------------

def step_lap_sidl_gen(
    sidl_gen: Path,
    fidl_file: Path,
    output_dir: Path,
    backend: str,
    extra_args: List[str],
) -> bool:
    """
    Step 1 (both backends): invoke lap-sidl-gen.

    coreipc → --types --proxy --skeleton
    dds     → --all  (types + proxy + skeleton + dds-idl)
    """
    output_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(sidl_gen),
        "--input",  str(fidl_file),
        "--output", str(output_dir),
    ]

    if backend == "coreipc":
        cmd += ["--types", "--proxy", "--skeleton"]
    else:  # dds or all
        cmd += ["--all"]

    cmd += extra_args
    return run_cmd(cmd, "lap-sidl-gen")


def step_fastddsgen(
    fastdds: Optional[Path],
    idl_file: Path,
    output_dir: Path,
) -> bool:
    """
    Step 2 (dds backend): invoke fastddsgen on the generated .idl.
    Returns True even when fastddsgen is missing (warns and skips).
    """
    if fastdds is None:
        print("  [WARN] fastddsgen not found — skipping DDS serialization code generation.")
        print("         Install eProsima Fast DDS: https://fast-dds.docs.eprosima.com/")
        print(f"         Manual: fastddsgen -d {output_dir} {idl_file}")
        return True   # non-fatal

    return run_cmd(
        [str(fastdds), "-d", str(output_dir), "-replace", str(idl_file)],
        "fastddsgen",
    )


def step_coreipc_config(
    model: FidlModel,
    output_dir: Path,
    instance_id: int,
) -> Optional[Path]:
    """
    Step 2 (coreipc backend): generate coreipc_config.yaml.
    Returns the Path of the generated file, or None on failure.
    """
    try:
        out = generate_coreipc_config(model, output_dir, instance_id)
        return out
    except Exception as exc:  # noqa: BLE001
        print(f"  [ERROR] coreipc config generation failed: {exc}")
        return None


# ---------------------------------------------------------------------------
# Schema hash extraction from lap-sidl-gen --hash-only
# ---------------------------------------------------------------------------

def get_schema_hash_from_sidlgen(sidl_gen: Path, fidl_file: Path) -> str:
    """Run lap-sidl-gen --hash-only and capture the hash string."""
    result = subprocess.run(
        [str(sidl_gen), "--input", str(fidl_file), "--hash-only"],
        capture_output=True, text=True,
    )
    if result.returncode == 0:
        return result.stdout.strip()
    return ""


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="gen_pipeline",
        description="LightAP 全流程代码生成工具（coreipc / dds / all）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # DDS 完整流水线
  %(prog)s -i Calculator.fidl -o gen/ --backend dds

  # CoreIPC 完整流水线
  %(prog)s -i Calculator.fidl -o gen/ --backend coreipc

  # 同时生成两种后端
  %(prog)s -i Calculator.fidl -o gen/ --backend all

  # 带 QoS 配置文件
  %(prog)s -i Calculator.fidl -o gen/ --backend dds \\
      --com-config config/com_config.yaml \\
      --service-deploy config/service_deploy.yaml

  # 仅生成 coreipc_config.yaml（不调用 lap-sidl-gen）
  %(prog)s -i Calculator.fidl -o gen/ --backend coreipc --config-only
""",
    )

    p.add_argument("-i", "--input",   required=True,  help=".fidl 输入文件路径")
    p.add_argument("-o", "--output",  required=True,  help="输出目录")
    p.add_argument(
        "--backend",
        choices=["coreipc", "dds", "all"],
        default="all",
        help="生成目标后端（默认: all）",
    )

    # lap-sidl-gen passthrough options
    p.add_argument("--namespace",      metavar="NS",   help="命名空间前缀（传递给 lap-sidl-gen）")
    p.add_argument("--author",         metavar="NAME", help="生成文件的 @author")
    p.add_argument("--schema-hash",    metavar="HEX",  help="注入外部 SchemaHash")
    p.add_argument("--version-string", metavar="VER",  help="覆盖 OMG IDL 版本字符串")
    p.add_argument("--service-id",     metavar="ID",   help="覆盖 ServiceID（十进制或 0x 十六进制）")
    p.add_argument("--instance-id",    metavar="ID",   type=int, default=1,
                   help="Instance ID（用于 QoS XML 和 coreipc_config.yaml，默认: 1）")
    p.add_argument("--com-config",     metavar="FILE", help="com_config.yaml 路径")
    p.add_argument("--service-deploy", metavar="FILE", help="service_deploy.yaml 路径")
    p.add_argument("--slot-mapping",   metavar="FILE", help="slot_mapping.yaml 路径")

    # Pipeline control
    p.add_argument("--config-only", action="store_true",
                   help="仅生成配置文件（跳过 C++ 代码生成）")
    p.add_argument("--sidl-gen",    metavar="PATH",
                   help="lap-sidl-gen 可执行文件路径（自动搜索时覆盖）")
    p.add_argument("--fastddsgen",  metavar="PATH",
                   help="fastddsgen 可执行文件路径（自动搜索时覆盖）")
    p.add_argument("-v", "--verbose", action="store_true", help="详细输出")

    return p


def _collect_sidlgen_extra_args(args: argparse.Namespace) -> List[str]:
    """Build the extra arguments list forwarded to lap-sidl-gen."""
    extra: List[str] = []
    if args.namespace:
        extra += ["--namespace", args.namespace]
    if args.author:
        extra += ["--author", args.author]
    if args.schema_hash:
        extra += ["--schema-hash", args.schema_hash]
    if args.version_string:
        extra += ["--version-string", args.version_string]
    if args.service_id:
        extra += ["--service-id", args.service_id]
    if args.instance_id and args.instance_id != 1:
        extra += ["--instance-id", str(args.instance_id)]
    if args.com_config:
        extra += ["--com-config", args.com_config]
    if args.service_deploy:
        extra += ["--service-deploy", args.service_deploy]
    if args.slot_mapping:
        extra += ["--slot-mapping", args.slot_mapping]
    return extra


def run_coreipc_pipeline(
    args: argparse.Namespace,
    fidl_file: Path,
    output_dir: Path,
    fidl_source: str,
    model: FidlModel,
    sidl_gen: Optional[Path],
) -> bool:
    print("\n╔═══════════════════════════════════════╗")
    print("║  Backend: CoreIPC                     ║")
    print("╚═══════════════════════════════════════╝")
    ok = True

    # Step 1: C++ headers
    if not args.config_only:
        print("\n[CoreIPC Step 1/2] Generating C++ headers via lap-sidl-gen...")
        if sidl_gen is None:
            print("  [ERROR] lap-sidl-gen not found. Use --sidl-gen to specify path.")
            return False
        extra = _collect_sidlgen_extra_args(args)
        ok = step_lap_sidl_gen(sidl_gen, fidl_file, output_dir, "coreipc", extra)
        if not ok:
            return False

    # Step 2: coreipc_config.yaml
    print("\n[CoreIPC Step 2/2] Generating coreipc_config.yaml...")
    # Enrich model with schema hash from lap-sidl-gen (authoritative) if available
    if sidl_gen and not args.schema_hash:
        h = get_schema_hash_from_sidlgen(sidl_gen, fidl_file)
        if h:
            model.schema_hash = h
    elif args.schema_hash:
        model.schema_hash = args.schema_hash

    if not model.schema_hash:
        model.schema_hash = compute_schema_hash(fidl_source)

    qualified = f"{model.package_name}.{model.interface_name}"
    model.service_id = compute_service_id(qualified)

    cfg_path = step_coreipc_config(model, output_dir, args.instance_id)
    if cfg_path is None:
        return False
    print(f"  Generated: {cfg_path}")
    return True


def run_dds_pipeline(
    args: argparse.Namespace,
    fidl_file: Path,
    output_dir: Path,
    sidl_gen: Optional[Path],
    fastdds: Optional[Path],
) -> bool:
    print("\n╔═══════════════════════════════════════╗")
    print("║  Backend: DDS (OMG IDL + FastDDS)     ║")
    print("╚═══════════════════════════════════════╝")

    if not args.config_only:
        # Step 1: lap-sidl-gen --all
        print("\n[DDS Step 1/2] Generating C++ headers + OMG IDL + QoS XML...")
        if sidl_gen is None:
            print("  [ERROR] lap-sidl-gen not found. Use --sidl-gen to specify path.")
            return False
        extra = _collect_sidlgen_extra_args(args)
        if not step_lap_sidl_gen(sidl_gen, fidl_file, output_dir, "dds", extra):
            return False

    # Step 2: fastddsgen
    print("\n[DDS Step 2/2] Running fastddsgen on generated IDL...")
    iface_name = _guess_interface_name(fidl_file)
    idl_file   = output_dir / f"{iface_name}.idl"

    if not idl_file.exists():
        print(f"  [WARN] {idl_file} not found — skipping fastddsgen.")
        return True  # non-fatal when config-only or idl missing

    return step_fastddsgen(fastdds, idl_file, output_dir)


def _guess_interface_name(fidl_file: Path) -> str:
    """Quick heuristic: first 'interface <Name>' in the .fidl file."""
    try:
        src = fidl_file.read_text(errors="replace")
        m = re.search(r'\binterface\s+(\w+)', src)
        if m:
            return m.group(1)
    except OSError:
        pass
    return fidl_file.stem


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    fidl_file  = Path(args.input).resolve()
    output_dir = Path(args.output).resolve()

    # Validate input
    if not fidl_file.exists():
        print(f"Error: input file not found: {fidl_file}", file=sys.stderr)
        return 1

    # Read + parse .fidl
    try:
        fidl_source = fidl_file.read_text(errors="replace")
    except OSError as exc:
        print(f"Error: cannot read {fidl_file}: {exc}", file=sys.stderr)
        return 1

    try:
        fidl_parser = FidlParser(fidl_source, str(fidl_file))
        model = fidl_parser.parse()
    except SyntaxError as exc:
        print(f"Error: FIDL parse failed: {exc}", file=sys.stderr)
        return 1

    if not model.interface_name:
        print("Error: no 'interface' definition found in .fidl file", file=sys.stderr)
        return 1

    # Locate tools
    sidl_gen_path: Optional[Path] = (
        Path(args.sidl_gen) if args.sidl_gen else find_lap_sidl_gen()
    )
    fastdds_path: Optional[Path] = (
        Path(args.fastddsgen) if args.fastddsgen else find_fastddsgen()
    )

    # Header
    print("╔══════════════════════════════════════════════════╗")
    print("║           lap-gen-pipeline  v1.0                 ║")
    print("╚══════════════════════════════════════════════════╝")
    print(f"  Input:      {fidl_file}")
    print(f"  Output:     {output_dir}")
    print(f"  Backend:    {args.backend}")
    print(f"  Interface:  {model.interface_name}  ({model.package_name})")
    print(f"  Version:    v{model.version}")
    print(f"  Elements:   {len(model.elements)}  "
          f"({sum(1 for e in model.elements if e.kind=='method')} method, "
          f"{sum(1 for e in model.elements if e.kind=='fire_and_forget')} ff, "
          f"{sum(1 for e in model.elements if e.kind=='event')} event, "
          f"{sum(1 for e in model.elements if e.kind=='field')} field)")
    print(f"  lap-sidl-gen: {sidl_gen_path or 'NOT FOUND'}")
    if args.backend in ("dds", "all"):
        print(f"  fastddsgen:   {fastdds_path or 'not found (optional)'}")
    print()

    output_dir.mkdir(parents=True, exist_ok=True)
    ok = True

    backends = (
        ["coreipc", "dds"] if args.backend == "all" else [args.backend]
    )

    for backend in backends:
        if backend == "coreipc":
            if not run_coreipc_pipeline(
                args, fidl_file, output_dir, fidl_source, model, sidl_gen_path
            ):
                ok = False

        elif backend == "dds":
            if not run_dds_pipeline(
                args, fidl_file, output_dir, sidl_gen_path, fastdds_path
            ):
                ok = False

    print()
    if ok:
        print("✓ Pipeline completed successfully.")
        _print_summary(output_dir, args.backend)
        return 0

    print("✗ Pipeline completed with errors.")
    return 1


def _print_summary(output_dir: Path, backend: str) -> None:
    """Print a tree of generated files."""
    print(f"\nGenerated files in {output_dir}:")
    patterns_coreipc = ["*Types.hpp", "*Proxy.hpp", "*Skeleton.hpp", "*coreipc_config.yaml"]
    patterns_dds     = ["*Types.hpp", "*Proxy.hpp", "*Skeleton.hpp", "*.idl", "*_qos.xml",
                        "*PubSubTypes.cxx", "*PubSubTypes.h"]
    patterns = patterns_dds if backend in ("dds", "all") else patterns_coreipc

    try:
        files = sorted(output_dir.iterdir())
        shown = set()
        for pat in patterns:
            for f in files:
                if fnmatch.fnmatch(f.name, pat) and f.name not in shown:
                    print(f"  ├── {f.name}")
                    shown.add(f.name)
    except OSError:
        pass


if __name__ == "__main__":
    sys.exit(main())
