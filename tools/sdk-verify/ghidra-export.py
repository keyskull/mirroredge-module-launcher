# Ghidra headless export script for Mirror's Edge SDK verification.
#
# Usage (via extract-sdk-data.ps1):
#   analyzeHeadless.bat <project_dir> <project_name> -import MirrorsEdge.exe \
#       -scriptPath <this_dir> -postScript ghidra-export.py <output.json>
#
# Exports:
#   - Class names and their total sizes from the vftable comment pattern
#   - GNames/GObjects pattern locations
#   - Key vtable function indices
#   - FNV-1a code probe at offset 0x1000
#
# @category SDK_Verify

import json
import os
import sys
import struct

from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.symbol import SourceType
from ghidra.program.model.mem import MemoryAccessException
from ghidra.util.task import ConsoleTaskMonitor

FNV_OFFSET_BASIS = 0x811c9dc5
FNV_PRIME = 0x01000193


def fnv1a_32(data):
    """Compute FNV-1a 32-bit hash."""
    h = FNV_OFFSET_BASIS
    for b in data:
        h ^= b & 0xFF
        h = (h * FNV_PRIME) & 0xFFFFFFFF
    return h


def read_bytes(program, address, length):
    """Safely read bytes from program memory."""
    try:
        data = bytearray(length)
        for i in range(length):
            data[i] = program.getMemory().getByte(address.add(i))
        return bytes(data)
    except MemoryAccessException:
        return None


def find_pattern(program, base, size, pattern, mask):
    """Find byte pattern in memory range. Returns address or None."""
    mem = program.getMemory()
    try:
        for i in range(size - len(mask)):
            addr = base.add(i)
            match = True
            for j in range(len(mask)):
                b = mem.getByte(addr.add(j))
                if mask[j] == 'x' and b != pattern[j]:
                    match = False
                    break
            if match:
                return addr
    except MemoryAccessException:
        pass
    return None


def find_g_names_pattern(program):
    """Find GNames pattern: 8B 0D ?? ?? ?? ?? 8B 84 24 ?? ?? ?? ?? 8B 04 81"""
    pattern = bytes([0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
                      0x8B, 0x84, 0x24, 0x00, 0x00, 0x00, 0x00,
                      0x8B, 0x04, 0x81])
    mask = "xx????xxx????xxx"
    mem = program.getMemory()
    for block in mem.getBlocks():
        if not block.isInitialized():
            continue
        start = block.getStart()
        size = block.getSize()
        addr = find_pattern(program, start, size, pattern, mask)
        if addr:
            return addr.getOffset()
    return 0


def find_g_objects_pattern(program):
    """Find GObjects pattern: 8B 15 ?? ?? ?? ?? 8B 0C B2 8D 44 24 30"""
    pattern = bytes([0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
                      0x8B, 0x0C, 0xB2, 0x8D, 0x44, 0x24, 0x30])
    mask = "xx????xxxxxxx"
    mem = program.getMemory()
    for block in mem.getBlocks():
        if not block.isInitialized():
            continue
        start = block.getStart()
        size = block.getSize()
        addr = find_pattern(program, start, size, pattern, mask)
        if addr:
            return addr.getOffset()
    return 0


def compute_code_probe(program):
    """Compute FNV-1a of code at offset 0x1000 (4096 bytes)."""
    image_base = program.getImageBase()
    probe_addr = image_base.add(0x1000)
    data = read_bytes(program, probe_addr, 4096)
    if data:
        return fnv1a_32(data)
    return 0


def extract_known_classes(symbol_table, program):
    """Extract sizes of known UE3 classes from symbol table."""
    known_classes = {
        'UObject': 'Core.Object',
        'UField': 'Core.Field',
        'UStruct': 'Core.Struct',
        'UFunction': 'Core.Function',
        'UClass': 'Core.Class',
        'UProperty': 'Core.Property',
        'AActor': 'Engine.Actor',
        'APawn': 'Engine.Pawn',
        'AController': 'Engine.Controller',
        'APlayerController': 'Engine.PlayerController',
        'UCanvas': 'Engine.Canvas',
        'UGameViewportClient': 'Engine.GameViewportClient',
        'UEngine': 'Engine.Engine',
        'UGameEngine': 'Engine.GameEngine',
        'UWorld': 'Engine.WorldInfo',
        'AHUD': 'Engine.HUD',
        'ATdGameEngine': 'TdGame.TdGameEngine',
        'ATdPlayerController': 'TdGame.TdPlayerController',
        'ATdPawn': 'TdGame.TdPawn',
        'ATdPlayerPawn': 'TdGame.TdPlayerPawn',
    }

    classes = {}
    listing = program.getListing()
    data_manager = program.getDataTypeManager()

    # Walk all defined data types to find class structures
    for dt in data_manager.getAllDataTypes():
        if not hasattr(dt, 'getName'):
            continue
        name = dt.getName()
        if name in known_classes:
            classes[name] = {
                'ue3_name': known_classes[name],
                'size': dt.getLength(),
                'members': {},
            }

    return classes


def get_image_info(program):
    """Get basic image information."""
    mem = program.getMemory()
    image_base = program.getImageBase()
    # Find last initialized block to determine image size
    max_addr = image_base
    for block in mem.getBlocks():
        if block.isInitialized():
            end = block.getEnd()
            if end.getOffset() > max_addr.getOffset():
                max_addr = end
    size = max_addr.getOffset() - image_base.getOffset() + 1
    return {
        'image_base': image_base.getOffset(),
        'image_size': size,
        'module_name': program.getName(),
    }


def extract_vtable_info(program, class_name_z, address):
    """Try to extract vtable information for a known address."""
    # This is a heuristic - in Ghidra the class structure may have been recovered
    sym_table = program.getSymbolTable()
    vfuncs = []
    try:
        data = program.getListing().getDataAt(address)
        if data and data.isPointer():
            vtable_addr = data.getValue()
            if vtable_addr:
                # Read first 20 vtable entries
                vtable_data = read_bytes(program,
                    program.getAddressFactory().getDefaultAddressSpace().getAddress(vtable_addr), 80)
                if vtable_data:
                    for i in range(0, 80, 4):
                        entry = struct.unpack_from('<I', vtable_data, i)[0]
                        if entry != 0 and entry > program.getImageBase().getOffset():
                            vfuncs.append(entry)
    except Exception:
        pass
    return vfuncs


def export_reference_data(program, output_path):
    """Main export function."""
    monitor = ConsoleTaskMonitor()

    result = {
        'format_version': 1,
        'generated_by': 'ghidra-export.py',
        'image': get_image_info(program),
        'code_probe_fnv': compute_code_probe(program),
        'patterns': {
            'gnames': find_g_names_pattern(program),
            'gobjects': find_g_objects_pattern(program),
        },
        'classes': extract_known_classes(None, program),
        'analysis_timestamp': '',
    }

    # Write JSON output
    output_dir = os.path.dirname(output_path)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)

    with open(output_path, 'w') as f:
        json.dump(result, f, indent=2, sort_keys=True)

    print("SDK reference data exported to: {}".format(output_path))
    print("  Image size: 0x{:X}".format(result['image']['image_size']))
    print("  Code probe FNV: 0x{:08X}".format(result['code_probe_fnv']))
    print("  GNames pattern: 0x{:X}".format(result['patterns']['gnames']))
    print("  GObjects pattern: 0x{:X}".format(result['patterns']['gobjects']))
    print("  Classes extracted: {}".format(len(result['classes'])))


def run():
    """Entry point called by Ghidra headless."""
    if len(sys.argv) < 2:
        print("Usage: analyzeHeadless ... -postScript ghidra-export.py <output.json>")
        return

    output_path = sys.argv[1]
    program = getCurrentProgram()
    if not program:
        print("No program loaded")
        return

    export_reference_data(program, output_path)


if __name__ == '__main__':
    run()
