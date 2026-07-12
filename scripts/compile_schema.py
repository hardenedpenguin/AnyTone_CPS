#!/usr/bin/env python3
"""Compile dmr-tools codeplug XML into JSON schema + memory regions for anytone."""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def parse_width(spec: str) -> int:
    """Return width in bits. Bit part of NNh:M is octal (dmr-tools convention)."""
    spec = spec.strip().lower()
    if not spec:
        return 0
    if ":" in spec:
        byte_part, bit_part = spec.split(":", 1)
        byte_part = byte_part.replace("h", "") or "0"
        bits = int(byte_part, 16) * 8 + int(bit_part, 8)
        return bits
    if spec.endswith("h"):
        return int(spec[:-1], 16) * 8
    # plain decimal bits or bytes? pattern uses bare numbers as bits sometimes ("16", "7")
    # In this dialect bare numbers without h are bit counts in decimal for strings often width="16"
    # but also width="16" on string means 16 bytes when format=ascii.
    # Heuristic: callers pass context. Default treat as bits if small... 
    # For string width="16" it's bytes. Handled by caller.
    return int(spec, 10)


def parse_addr(spec: str) -> int:
    spec = spec.strip().lower().replace("h", "")
    if ":" in spec:
        spec = spec.split(":", 1)[0]
    return int(spec, 16)


def meta_name(node: ET.Element) -> str | None:
    meta = node.find("meta")
    if meta is None:
        return None
    n = meta.findtext("name")
    return n.strip() if n else None


def meta_brief(node: ET.Element) -> str | None:
    meta = node.find("meta")
    if meta is None:
        return None
    b = meta.findtext("brief")
    return b.strip() if b else None


def meta_done(node: ET.Element) -> bool:
    meta = node.find("meta")
    return meta is not None and meta.find("done") is not None


def enum_items(node: ET.Element) -> list[dict]:
    items = []
    for it in node.findall("item"):
        val = it.get("value")
        name = it.findtext("name") or ""
        items.append({"value": int(val, 0) if val else 0, "name": name.strip()})
    return items


def flatten_fields(node: ET.Element, bit_cursor: int = 0) -> tuple[list[dict], int]:
    """Walk field children of an element; return (fields, total_bits)."""
    fields: list[dict] = []
    for child in list(node):
        tag = child.tag
        if tag == "meta":
            continue
        if tag == "repeat":
            # Inline fixed repeat: prefer n="64", else min/max (allocate max).
            if child.get("n") is not None:
                count = int(child.get("n"))
            else:
                rmin = int(child.get("min", "1"))
                count = int(child.get("max", rmin))
            _, one_end = flatten_fields(child, 0)
            one_bits = one_end
            for i in range(count):
                start = bit_cursor
                sub, end = flatten_fields(child, bit_cursor)
                for f in sub:
                    f["name"] = f"{f['name']} [{i}]"
                    fields.append(f)
                advance = end - start
                if advance <= 0:
                    advance = one_bits
                bit_cursor += advance
            continue

        if tag == "element":
            # Inline nested structure (e.g. DMR APRS settings inside APRS settings)
            sub, end = flatten_fields(child, bit_cursor)
            fields.extend(sub)
            bit_cursor = end
            continue

        width_attr = child.get("width")
        if width_attr is None:
            continue

        # strings: width is bytes
        if tag == "string":
            nbytes = int(width_attr.replace("h", ""), 16) if width_attr.endswith("h") else int(width_attr)
            bits = nbytes * 8
            name = meta_name(child) or "string"
            fields.append({
                "name": name,
                "brief": meta_brief(child),
                "type": "string",
                "format": child.get("format", "ascii"),
                "bit_offset": bit_cursor,
                "bit_width": bits,
                "done": meta_done(child),
            })
            bit_cursor += bits
            continue

        bits = parse_width(width_attr)
        name = meta_name(child) or tag

        if tag in ("unused", "unknown"):
            bit_cursor += bits
            continue

        if tag == "int":
            fields.append({
                "name": name,
                "brief": meta_brief(child),
                "type": "int",
                "format": child.get("format", "unsigned"),
                "endian": child.get("endian", "little"),
                "bit_offset": bit_cursor,
                "bit_width": bits,
                "default": child.get("default"),
                "done": meta_done(child),
            })
            bit_cursor += bits
        elif tag == "enum":
            fields.append({
                "name": name,
                "brief": meta_brief(child),
                "type": "enum",
                "bit_offset": bit_cursor,
                "bit_width": bits,
                "items": enum_items(child),
                "done": meta_done(child),
            })
            bit_cursor += bits
        else:
            bit_cursor += bits

    return fields, bit_cursor


def collect_regions_and_elements(root: ET.Element) -> tuple[list[dict], list[dict]]:
    regions: list[dict] = []
    elements: list[dict] = []

    def add_region(addr: int, size: int, name: str):
        if size <= 0:
            return
        regions.append({"address": addr, "size": size, "name": name})

    def walk(node: ET.Element, base_addr: int | None):
        for child in list(node):
            tag = child.tag
            if tag == "meta":
                continue

            at = child.get("at")
            addr = parse_addr(at) if at else base_addr

            if tag == "repeat":
                name = meta_name(child) or "repeat"
                step = child.get("step")
                rmin = int(child.get("min", "1"))
                rmax = int(child.get("max", rmin))
                if at and step:
                    step_b = parse_addr(step)
                    # Estimate size of one iteration
                    # Prefer nested element size
                    one_size = 0
                    for sub in child.findall("element"):
                        fields, bits = flatten_fields(sub)
                        one_size = max(one_size, (bits + 7) // 8)
                    if one_size == 0:
                        # nested repeat (bank of channels / contacts)
                        for sub in child.findall("repeat"):
                            inner_max = int(sub.get("max", "1"))
                            for el in sub.findall("element"):
                                _, bits = flatten_fields(el)
                                el_sz = (bits + 7) // 8
                                one_size = max(one_size, inner_max * el_sz)
                    # Bank windows are sparse within `step`. Never spill past the stride
                    # (XML sometimes lists total capacity as per-bank max).
                    if one_size > step_b:
                        one_size = step_b
                    # Dense small-stride banks (zones, RX groups): dump full slots so
                    # trailing name/pad is included and adjacent regions coalesce.
                    if 0 < one_size < step_b and step_b <= 0x1000:
                        one_size = step_b
                    for i in range(rmax):
                        add_region(parse_addr(at) + i * step_b, one_size or 1, f"{name}[{i}]")
                    walk(child, None)
                elif at and not step and (child.get("n") is not None or child.get("max") is not None):
                    # Packed consecutive bank (n=… or min/max without step)
                    if child.get("n") is not None:
                        count = int(child.get("n"))
                    else:
                        count = int(child.get("max", "0"))
                    el = child.find("element")
                    if count > 0 and el is not None:
                        fields, bits = flatten_fields(el)
                        one_size = (bits + 7) // 8
                        el_name = meta_name(el) or name
                        if one_size * count > 0x01000000:
                            print(f"warning: skipping huge bank {el_name} "
                                  f"({count}×{one_size})", file=sys.stderr)
                        else:
                            elements.append({
                                "name": el_name,
                                "address": parse_addr(at),
                                "size": one_size,
                                "done": meta_done(el),
                                "brief": meta_brief(el),
                                "fields": fields,
                                "bank_count": count,
                            })
                            add_region(parse_addr(at), one_size * count, name)
                    elif count > 0:
                        # Direct repeated fields (e.g. Zone Names = ascii strings)
                        fields, bits = flatten_fields(child)
                        one_size = (bits + 7) // 8
                        if one_size > 0 and one_size * count <= 0x01000000:
                            add_region(parse_addr(at), one_size * count, name)
                else:
                    walk(child, addr)
                continue

            if tag == "element":
                name = meta_name(child) or "Element"
                fields, bits = flatten_fields(child)
                size = (bits + 7) // 8
                if addr is not None:
                    elements.append({
                        "name": name,
                        "address": addr,
                        "size": size,
                        "done": meta_done(child),
                        "brief": meta_brief(child),
                        "fields": fields,
                    })
                    add_region(addr, size, name)
                # Nested content already flattened; still walk for nested abs elements (rare)
                continue

            # Other addressed blocks (bitmap ints etc.)
            if at and tag in ("int", "enum", "string", "unused", "unknown"):
                w = child.get("width", "0")
                if tag == "string":
                    size = int(w.replace("h", ""), 16) if "h" in w else int(w)
                else:
                    size = (parse_width(w) + 7) // 8
                add_region(parse_addr(at), max(size, 1), meta_name(child) or tag)

            walk(child, addr)

    walk(root, None)
    return regions, elements


def coalesce_regions(regions: list[dict]) -> list[dict]:
    if not regions:
        return []
    ordered = sorted(regions, key=lambda r: (r["address"], r["size"]))
    out = [dict(ordered[0])]
    for r in ordered[1:]:
        prev = out[-1]
        prev_end = prev["address"] + prev["size"]
        if r["address"] <= prev_end:
            prev["size"] = max(prev_end, r["address"] + r["size"]) - prev["address"]
            if r["name"] not in prev["name"]:
                prev["name"] = prev["name"]  # keep first name
        elif r["address"] == prev_end:
            prev["size"] += r["size"]
        else:
            out.append(dict(r))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("xml", type=Path)
    ap.add_argument("-o", "--json-out", type=Path, required=True)
    ap.add_argument("--memmap-c", type=Path, help="Write C memmap include")
    ap.add_argument("--memmap-symbol", type=str, default="",
                    help="C symbol stem, e.g. D878UV_V400 → AT_REGIONS_<stem>")
    ap.add_argument("--model", default="D878UV2")
    ap.add_argument("--firmware", default="4.00")
    args = ap.parse_args()

    tree = ET.parse(args.xml)
    root = tree.getroot()
    # Drop namespace if present
    if root.tag.startswith("{"):
        # unlikely for this file
        pass

    meta = root.find("meta")
    schema = {
        "model": args.model,
        "firmware": args.firmware,
        "name": meta.findtext("name") if meta is not None else args.model,
        "regions": [],
        "elements": [],
        "categories": [],
    }

    regions, elements = collect_regions_and_elements(root)
    schema["elements"] = elements
    coalesced = coalesce_regions(regions)
    schema["regions"] = coalesced

    # UI categories: group absolute settings-like elements
    settings_names = []
    for el in elements:
        n = el["name"]
        if any(k in n.lower() for k in (
            "settings", "boot", "aprs", "alarm", "encryption", "tone",
            "key", "gps", "display", "power", "fm ", "dtmf", "roaming",
            "hot-key", "contact", "arc4", "aes"
        )) or el["address"] in {
            0x02500000, 0x02500600, 0x02501000, 0x02501400, 0x024C1000, 0x024C1400,
            0x01040000, 0x01043000, 0x025C0500, 0x02940000,
        }:
            settings_names.append(n)

    schema["categories"] = [
        {"id": "channels", "title": "Channels", "element": "Channel"},
        {"id": "contacts", "title": "Talk Groups / Contacts", "element": "Contact"},
        {"id": "radio_ids", "title": "Radio IDs", "element": "Radio Id Element"},
        {"id": "zones", "title": "Zones", "element": "Zone channel list"},
        {"id": "scan_lists", "title": "Scan Lists", "element": "Scan List"},
        {"id": "rx_groups", "title": "RX Group Lists", "element": "Group List"},
        {
            "id": "aprs_gps",
            "title": "APRS & GPS",
            "elements": [
                "APRS settings",
                "DMR APRS message",
                "APRS filter",
                "GPS roaming zone",
            ],
            "gps_from": {
                "General Settings": [
                    "Enable GPS",
                    "Enable RX DMR APRS Positions",
                    "Enable GPS test",
                    "Sent (DMR) APRS message.",
                ],
                "General Settings Extension": [
                    "GPS Modes",
                    "Enable GPS roaming.",
                ],
            },
        },
        {
            "id": "roaming",
            "title": "Roaming",
            "elements": ["Roaming channel", "Roaming zone"],
        },
        {
            "id": "hotkeys",
            "title": "Buttons & Hotkeys",
            "elements": ["Hot-Key Setting"],
            "keys_from": "General Settings",
        },
        {
            "id": "signaling",
            "title": "Signaling",
            "elements": [
                "DTMF Settings", "DTMF Contact",
                "2-Tone Settings", "2-Tone Id", "Two-Tone function",
                "5-Tone settings", "5-tone ID", "5-Tone function",
            ],
        },
        {
            "id": "encryption",
            "title": "Encryption",
            "elements": [
                "DMR Encryption Key", "ARC4 encryption key",
                "AES encryption key bitmap", "Alarm Settings",
            ],
        },
        {"id": "optional_settings", "title": "Optional Settings", "elements": settings_names},
    ]

    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(schema, indent=2) + "\n")
    print(f"wrote {args.json_out} ({len(elements)} elements, {len(coalesced)} regions, "
          f"{sum(r['size'] for r in coalesced)} bytes span)")

    if args.memmap_c:
        stem = args.memmap_symbol.strip()
        if not stem:
            # D878UV2 + 4.00 → D878UV2_V400
            fw = args.firmware.replace(".", "")
            stem = f"{args.model}_V{fw}"
        lines = [
            "/* Auto-generated from %s — do not edit */" % args.xml.name,
            f"static const struct at_region AT_REGIONS_{stem}[] = {{",
        ]
        total = 0
        kept = 0
        for r in coalesced:
            # Skip huge callsign DB for default dump (0x40xxxxxx+)
            if r["address"] >= 0x04000000:
                continue
            # Guard against pathological sizes from schema quirks
            if r["size"] <= 0 or r["size"] > 0x01000000:
                print(f"warning: skipping region 0x{r['address']:08X} size={r['size']}",
                      file=sys.stderr)
                continue
            lines.append(
                f"    {{ 0x{r['address']:08X}u, 0x{r['size']:06X}u }}, /* {r['name'][:48]} */"
            )
            total += r["size"]
            kept += 1
        lines.append("};")
        lines.append(f"static const size_t AT_REGIONS_{stem}_COUNT = {kept};")
        lines.append(f"#define AT_{stem}_DUMP_BYTES {total}u")
        args.memmap_c.write_text("\n".join(lines) + "\n")
        print(f"wrote {args.memmap_c} ({total} dump bytes excl. callsign DB)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
