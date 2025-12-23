#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import sys
from pathlib import Path

BYTE_ORDER_MAP = {
    "little_endian": "BYTE_ORDER_LITTLE_ENDIAN",
    "big_endian": "BYTE_ORDER_BIG_ENDIAN",
}

VALUE_TYPE_MAP = {
    "unsigned": "SIGNAL_TYPE_UNSIGNED",
    "signed":   "SIGNAL_TYPE_SIGNED",
    "boolean":  "SIGNAL_TYPE_BOOLEAN",
}

# If non-empty, only keep messages whose CAN ID is listed here.
# Use ints (e.g. 0x118) or strings ("0x118", "280").
KEEP_MESSAGE_IDS = {
    # From main/vehicle_can_mapping.c
    0x3C2,
    0x118,
    0x39D,
    0x3F3,
    0x3F5,
    0x132,
    0x261,
    0x7FF,
    0x257,
    0x399,
    0x22E,
    0x20E,
    0x334,
    0x2E5,
    0x266,
    0x102,
    0x103,
    0x2E1,
    0x204,
    0x273,
    0x284,
    0x212,
    0x25D,
    0x252,
    0x352
}


def c_ident(name: str) -> str:
    out = []
    for ch in name:
        if ch.isalnum():
            out.append(ch)
        else:
            out.append("_")
    ident = "".join(out)
    if not ident:
        ident = "NONAME"
    if ident[0].isdigit():
        ident = "_" + ident
    return ident


def normalize_keep_ids(values) -> set:
    out = set()
    for value in values:
        if isinstance(value, str):
            if value.lower().startswith("0x"):
                out.add(int(value, 16))
            else:
                out.add(int(value))
        else:
            out.add(int(value))
    return out


def _normalize_mux_value(value):
    if isinstance(value, str):
        raw = value.strip()
        if raw.upper() == "M":
            return "M"
        lowered = raw.lower()
        if lowered.startswith("mux"):
            lowered = lowered[3:]
        elif lowered.startswith("m"):
            lowered = lowered[1:]
        if lowered.startswith("0x"):
            return int(lowered, 16)
        if lowered.isdigit():
            return int(lowered)
        return raw
    return value


def get_mux_field(sig):
    if sig.get("is_multiplexer"):
        return "M"
    for key in ("mux", "multiplexer", "mux_value", "multiplexer_id", "multiplexer_ids", "mux_ids"):
        if key in sig:
            return sig.get(key)
    return None


def expand_mux_signals(sig):
    mux = _normalize_mux_value(get_mux_field(sig))
    if mux is None or (isinstance(mux, str) and mux.upper() == "M"):
        return [sig]
    if isinstance(mux, list):
        expanded = []
        for value in mux:
            clone = dict(sig)
            clone["mux"] = _normalize_mux_value(value)
            expanded.append(clone)
        return expanded
    clone = dict(sig)
    clone["mux"] = mux
    return [clone]


def mux_info(sig):
    mux = _normalize_mux_value(get_mux_field(sig))
    if mux is None:
        return "SIGNAL_MUX_NONE", 0
    if isinstance(mux, str) and mux.upper() == "M":
        return "SIGNAL_MUX_MULTIPLEXER", 0
    return "SIGNAL_MUX_MULTIPLEXED", int(mux)


def generate(config_json_path: Path, out_header_path: Path) -> None:
    data = json.loads(config_json_path.read_text(encoding="utf-8"))

    messages = data.get("messages", [])
    if not messages:
        raise SystemExit("Aucun message dans JSON (can_config.messages est vide)")

    keep_ids = normalize_keep_ids(KEEP_MESSAGE_IDS)

    lines = []
    lines.append("// Auto-généré depuis %s" % config_json_path.name)
    veh = data.get("vehicle", {})
    desc = veh.get("description", "")
    lines.append("// Description: %s" % desc)
    lines.append("")
    lines.append("#ifndef VEHICLE_CAN_UNIFIED_CONFIG_GENERATED_H")
    lines.append("#define VEHICLE_CAN_UNIFIED_CONFIG_GENERATED_H")
    lines.append("")
    lines.append('#include "vehicle_can_unified_config.h"')
    lines.append("")

    message_defs = []
    signal_arrays = []

    for msg in messages:
        msg_name = msg.get("name", "NONAME")
        msg_id_str = msg.get("id")
        if msg_id_str is None:
            continue

        if isinstance(msg_id_str, str) and msg_id_str.startswith("0x"):
            msg_id = int(msg_id_str, 16)
        else:
            msg_id = int(msg_id_str)

        if keep_ids and msg_id not in keep_ids:
            continue

        sigs = msg.get("signals", [])
        if not sigs:
            continue

        expanded_sigs = []
        for sig in sigs:
            expanded_sigs.extend(expand_mux_signals(sig))

        msg_ident = f"MSG_{c_ident(msg_name)}"
        sig_array_name = f"signals_{msg_ident}"

        signal_arrays.append((sig_array_name, expanded_sigs))
        message_defs.append((msg_ident, msg_name, msg_id, sig_array_name, len(expanded_sigs)))

    for sig_array_name, sigs in signal_arrays:
        lines.append(f"// Signaux pour {sig_array_name}")
        lines.append(f"static const can_signal_def_t {sig_array_name}[] = {{")
        for sig in sigs:
            s_name = sig.get("name", "NONAME")
            start_bit = sig.get("start_bit", 0)
            length = sig.get("length", 1)
            byte_order = BYTE_ORDER_MAP.get(sig.get("byte_order", "little_endian"), "BYTE_ORDER_LITTLE_ENDIAN")
            value_type = VALUE_TYPE_MAP.get(sig.get("value_type", "unsigned"), "SIGNAL_TYPE_UNSIGNED")
            factor = sig.get("factor", 1.0)
            offset = sig.get("offset", 0.0)
            mux_type, mux_value = mux_info(sig)

            lines.append("    {")
            lines.append(f'        .name       = "{s_name}",')
            lines.append(f"        .start_bit  = {int(start_bit)},")
            lines.append(f"        .length     = {int(length)},")
            lines.append(f"        .byte_order = {byte_order},")
            lines.append(f"        .value_type = {value_type},")
            lines.append(f"        .factor     = {float(factor):.6f}f,")
            lines.append(f"        .offset     = {float(offset):.6f}f,")
            lines.append(f"        .mux_type   = {mux_type},")
            lines.append(f"        .mux_value  = {int(mux_value)},")
            lines.append("    },")
        lines.append("};")
        lines.append("")

    lines.append("// Tableau global des messages CAN gérés")
    lines.append("const can_message_def_t g_can_messages[] = {")
    for msg_ident, msg_name, msg_id, sig_array_name, sig_count in message_defs:
        lines.append("    {")
        lines.append(f"        .id           = 0x{msg_id:X},")
        lines.append(f'        .name         = "{msg_name}",')
        lines.append(f"        .signals      = {sig_array_name},")
        lines.append(f"        .signal_count = {sig_count},")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append(f"const uint16_t g_can_message_count = {len(message_defs)};")
    lines.append("")
    lines.append("#endif // VEHICLE_CAN_UNIFIED_CONFIG_GENERATED_H")
    lines.append("")

    out_header_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Header généré: {out_header_path}")


def main(argv=None) -> None:
    argv = list(sys.argv[1:] if argv is None else argv)
    if len(argv) != 2:
        print("Usage: generate_vehicle_can_config.py Model3CAN.json vehicle_can_unified_config.generated.h")
        raise SystemExit(1)

    src = Path(argv[0])
    dst = Path(argv[1])
    if not src.is_file():
        raise SystemExit(f"JSON introuvable: {src}")

    generate(src, dst)


if __name__ == "__main__":
    main()
