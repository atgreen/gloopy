#!/usr/bin/env python3
"""Read/retarget the SFZ path inside a hosted-sfizz plugin state.

Gloopy saves a hosted plugin's state as base64 in the <PLUGIN pstate="..."> node
of a .gloopy project. For sfizz that state is a JUCE-VST3 wrapper around sfizz's
own state, and the loaded .sfz file path lives inside it as plaintext. sfizz
offers no way to set the .sfz programmatically (no parameter; the editor's file
chooser is the only UI path), so to build projects that use a specific SFZ
*without* opening the GUI, we retarget the path in an existing state.

Format (little-endian):
  outer = "VC2!" + uint32(xml_len) + xml + '\\0'
          xml = <?xml ...?><VST3PluginState><IComponent>INNER</IComponent>...
  INNER, outer payload, and this whole blob are JUCE base64 (custom alphabet,
  "<size>.<data>", LSB-first bit packing — see MemoryBlock::toBase64Encoding).
  inner (sfizz) = 8-byte header + uint32(path_len incl. NUL) + path + '\\0' + settings

Workflow to give a sfizz track a specific SFZ headlessly:
  1. Obtain one reference state that has *any* .sfz loaded (load once in the GUI
     and save, or use tools/sfizz-reference-state.txt).
  2. new = retarget(reference, "/abs/path/to/your.sfz")
  3. Substitute it into the target track's <PLUGIN ... pstate="NEW"> in the .gloopy.
  (settings — volume, polyphony, tuning, CCs — are carried over from the reference.)

CLI:
  python3 tools/sfizz-state.py get-path  STATEFILE
  python3 tools/sfizz-state.py retarget  REFERENCE_STATEFILE  /abs/path/to.sfz  > new-state.txt
"""
import re, struct, sys

_TABLE = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def juce_b64_decode(s: str) -> bytes:
    size_str, _, data = s.partition('.')
    n = int(size_str)
    out = bytearray(n)
    for pos, ch in enumerate(data):
        v = _TABLE.index(ch)
        for b in range(6):                       # LSB-first (MemoryBlock::setBitRange)
            P = pos * 6 + b
            if P >> 3 < n and (v >> b) & 1:
                out[P >> 3] |= 1 << (P & 7)
    return bytes(out)


def juce_b64_encode(data: bytes) -> str:
    n = len(data)
    nchars = ((n << 3) + 5) // 6
    out = [str(n), '.']
    for i in range(nchars):
        v = 0
        for b in range(6):
            P = i * 6 + b
            if P >> 3 < n and (data[P >> 3] >> (P & 7)) & 1:
                v |= 1 << b
        out.append(_TABLE[v])
    return ''.join(out)


def _inner_bounds(outer: bytes):
    assert outer[:4] == b'VC2!', "not a JUCE-VST3 state"
    xml_len = struct.unpack('<I', outer[4:8])[0]
    xml = outer[8:8 + xml_len]
    m = re.search(rb'<IComponent>([^<]*)</IComponent>', xml)
    return xml, m


def get_path(pstate: str) -> str:
    """Return the .sfz path referenced by a sfizz plugin state."""
    _, m = _inner_bounds(juce_b64_decode(pstate))
    inner = juce_b64_decode(m.group(1).decode())
    cnt = struct.unpack('<I', inner[8:12])[0]
    return inner[12:12 + cnt - 1].decode()


def retarget(pstate: str, new_path: str) -> str:
    """Return a new sfizz plugin state that loads NEW_PATH, keeping all settings."""
    outer = juce_b64_decode(pstate)
    xml, m = _inner_bounds(outer)
    inner = juce_b64_decode(m.group(1).decode())
    cnt = struct.unpack('<I', inner[8:12])[0]
    header, settings = inner[:8], inner[12 + cnt:]
    p = new_path.encode()
    new_inner = header + struct.pack('<I', len(p) + 1) + p + b'\x00' + settings
    new_ib = juce_b64_encode(new_inner).encode()
    new_xml = xml[:m.start(1)] + new_ib + xml[m.end(1):]
    new_outer = b'VC2!' + struct.pack('<I', len(new_xml)) + new_xml + b'\x00'
    return juce_b64_encode(new_outer)


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'get-path':
        print(get_path(open(sys.argv[2]).read().strip()))
    elif len(sys.argv) >= 4 and sys.argv[1] == 'retarget':
        print(retarget(open(sys.argv[2]).read().strip(), sys.argv[3]))
    else:
        print(__doc__.strip().splitlines()[-2].strip())
        print("usage: sfizz-state.py get-path STATEFILE | retarget REF.txt /abs/path.sfz")
        sys.exit(2)
