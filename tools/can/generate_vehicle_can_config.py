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


def generate(config_json_path: Path, out_header_path: Path) -> None:
    data = json.loads(config_json_path.read_text(encoding="utf-8"))

    messages = data.get("messages", [])
    if not messages:
        raise SystemExit("Aucun message dans JSON (can_config.messages est vide)")

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

        sigs = msg.get("signals", [])
        if not sigs:
            continue

        msg_ident = f"MSG_{c_ident(msg_name)}"
        sig_array_name = f"signals_{msg_ident}"

        signal_arrays.append((sig_array_name, sigs))
        message_defs.append((msg_ident, msg_name, msg_id, sig_array_name, len(sigs)))

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

            lines.append("    {")
            lines.append(f'        .name       = "{s_name}",')
            lines.append(f"        .start_bit  = {int(start_bit)},")
            lines.append(f"        .length     = {int(length)},")
            lines.append(f"        .byte_order = {byte_order},")
            lines.append(f"        .value_type = {value_type},")
            lines.append(f"        .factor     = {float(factor):.6f}f,")
            lines.append(f"        .offset     = {float(offset):.6f}f,")
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
