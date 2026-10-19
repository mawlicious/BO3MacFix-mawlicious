#!/usr/bin/env python3
"""Generate paired Windows/Mac wire schemas from BO3 Mac reflection metadata.

No SDK sources are required. The captured metadata is authoritative for Mac
offsets. We reconstruct both ABIs, verify the Mac result exactly, and reject
types for which an explicit wire representation has not been implemented.
"""
import argparse
import json
from pathlib import Path
import struct

SCALARS = {1: (1, 1), 2: (1, 1), 3: (1, 1), 4: (1, 1), 5: (2, 2), 6: (2, 2),
           7: (4, 4), 8: (4, 4), 9: (8, 8), 10: (8, 8), 11: (4, 4),
           12: (16, 16), 13: (16, 16), 14: (48, 16), 15: (48, 16),
           16: (48, 16), 17: (64, 16), 18: (64, 16), 30: (8, 8), 32: (2, 2)}


def align(value, alignment):
    return (value + alignment - 1) & -alignment


def virtual_names(packfile):
    if len(packfile) < 64 or struct.unpack_from('<I', packfile)[0] != 0x57e0e057:
        raise ValueError('Expected Havok packfile')
    count = struct.unpack_from('<I', packfile, 20)[0]
    table = 64 + (16 if struct.unpack_from('<h', packfile, 62)[0] == 16 else 0)
    sections = [struct.unpack_from('<7I', packfile, table + i * 64 + 20) for i in range(count)]
    names = set()
    for start, _, _, virt, exports, _, _ in sections:
        at = virt
        while at + 4 <= exports:
            value = struct.unpack_from('<I', packfile, start + at)[0]
            if value == 0xffffffff:
                at += 4
                continue
            _, section, offset = struct.unpack_from('<3I', packfile, start + at)
            where = sections[section][0] + offset
            names.add(packfile[where:packfile.index(b'\0', where)].decode('ascii'))
            at += 12
    return names


def clump_roots(path):
    data = Path(path).read_bytes()
    if len(data) < 48:
        raise ValueError(f'{path}: truncated clump')
    sizes = struct.unpack_from('<10I', data, 4)
    at = 48
    names = set()
    for size in sizes:
        if size:
            if at + size > len(data):
                raise ValueError(f'{path}: truncated clump buffer')
            names.update(virtual_names(data[at:at + size]))
        at += size
    if at != len(data):
        raise ValueError(f'{path}: unexpected clump trailer')
    return names


class Generator:
    def __init__(self, records):
        # Native runtime descriptors live above the image's original __DATA.
        # Earlier string matches include false hkAabb/hkReferencedObject entries.
        self.records = {r['address']: r for r in records if int(r['address'], 16) >= 0x110000000}
        self.names = {}
        for record in self.records.values():
            self.names[record['name']] = record
        self.layouts = {}
        self.scope = {}

    def reference(self, address):
        try:
            return self.records[address]
        except KeyError:
            raise ValueError(f'Missing runtime class address {address}') from None

    def select(self, name):
        if name in self.scope:
            return
        if name not in self.names:
            raise ValueError(f'Missing root class {name}')
        record = self.names[name]
        self.scope[name] = record
        if int(record['parent'], 16):
            self.select(self.reference(record['parent'])['name'])
        for member in record['members']:
            if int(member['cls'], 16):
                self.select(self.reference(member['cls'])['name'])

    def shape(self, member, mac):
        kind, subtype = member['type'], member['subtype']
        if kind in (24, 31):
            kind = subtype
        if kind in SCALARS:
            return SCALARS[kind]
        if kind in (20, 21, 29, 33):
            return 8, 8
        if kind in (22, 26):
            return 16, 8
        if kind == 25:
            record = self.reference(member['cls'])
            layout = self.layout(record, mac)
            return layout['size'], layout['alignment']
        raise ValueError(f"Unsupported type {member['type']}/{subtype}: {member['name']}")

    def layout(self, record, mac):
        key = record['name'], mac
        if key in self.layouts:
            return self.layouts[key]
        if record['name'] == 'hkBaseObject':
            result = dict(size=8, used=8, alignment=8, fields=[])
            self.layouts[key] = result
            return result
        if int(record['parent'], 16):
            parent = self.layout(self.reference(record['parent']), mac)
            cursor = parent['used'] if mac else parent['size']
            alignment = parent['alignment']
            fields = list(parent['fields'])
        else:
            cursor, alignment, fields = 0, 1, []
        for member in record['members']:
            size, member_alignment = self.shape(member, mac)
            # Havok ALIGN_8 / ALIGN_16 reflection flags.
            if member['flags'] & 128:
                member_alignment = max(member_alignment, 8)
            if member['flags'] & 256:
                member_alignment = max(member_alignment, 16)
            cursor = align(cursor, member_alignment)
            if mac and cursor != member['offset']:
                raise ValueError(f"Mac layout mismatch {record['name']}.{member['name']}: "
                                 f"inferred {cursor}, runtime {member['offset']}")
            fields.append((member, cursor))
            cursor += size * (member['count'] or 1)
            alignment = max(alignment, member_alignment)
        size = align(cursor, alignment)
        if mac and size != record['size']:
            raise ValueError(f"Mac size mismatch {record['name']}: inferred {size}, runtime {record['size']}")
        result = dict(size=size, used=cursor, alignment=alignment, fields=fields)
        self.layouts[key] = result
        return result

    def wire(self, member, kind=None):
        kind = member['type'] if kind is None else kind
        if kind in (24, 31):
            kind = member['subtype']
        if kind in SCALARS:
            return 'HKCONV_BYTES', SCALARS[kind][0]
        if kind == 25:
            return 'HKCONV_STRUCT', 0
        if kind == 20:
            return 'HKCONV_POINTER', 0
        if kind in (29, 33):
            return 'HKCONV_STRING', 0
        if kind in (22, 26):
            return ('HKCONV_ARRAY' if kind == 22 else 'HKCONV_SIMPLE_ARRAY'), 0
        raise ValueError(f"Unsupported serialized type {kind}: {member['name']}")

    def render(self):
        lines = ['/* Generated by tools/generate_havok_schema.py from Mac game reflection.',
                 ' * Mac member offsets and class sizes verified against the runtime.',
                 ' * Windows offsets inferred using x64 MSVC base-tail padding rules. */',
                 '#ifndef BO3MACFIX_HAVOK_SCHEMA_H', '#define BO3MACFIX_HAVOK_SCHEMA_H',
                 '#include "havok_convert.h"', '']
        names = sorted(self.scope)
        for index, name in enumerate(names):
            record = self.scope[name]
            source, target = self.layout(record, False), self.layout(record, True)
            members = []
            for (member, source_offset), (_, target_offset) in zip(source['fields'], target['fields']):
                if member['flags'] & 1024:  # SERIALIZE_IGNORED still participated in layout.
                    continue
                kind, size = self.wire(member)
                element_kind = '0'
                if member['type'] in (22, 26):
                    element_kind, size = self.wire(member, member['subtype'])
                class_name = ('NULL' if not int(member['cls'], 16)
                              else json.dumps(self.reference(member['cls'])['name']))
                members.append(f'    {{{source_offset}, {target_offset}, {kind}, {element_kind}, '
                               f'{size}, {member["count"]}, {class_name}}}, /* {member["name"]} */')
            if members:
                lines += [f'static const hkconv_member hkconv_members_{index}[] = {{', *members, '};', '']
        lines += ['static const hkconv_class hkconv_schema_classes[] = {']
        for index, name in enumerate(names):
            record = self.scope[name]
            source, target = self.layout(record, False), self.layout(record, True)
            count = sum(not (m['flags'] & 1024) for m, _ in source['fields'])
            members = f'hkconv_members_{index}' if count else 'NULL'
            lines.append(f'    {{{json.dumps(name)}, {source["size"]}, {target["size"]}, '
                         f'{source["alignment"]}, {target["alignment"]}, {members}, {count}}},')
        lines += ['};', '#define HKCONV_SCHEMA_CLASS_COUNT (sizeof(hkconv_schema_classes) / sizeof(hkconv_schema_classes[0]))',
                  '#endif', '']
        return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('metadata', type=Path)
    parser.add_argument('--clump', action='append', default=[], help='Select all graph classes found in this clump')
    parser.add_argument('--root', action='append', default=[], help='Also include this root class and its dependencies')
    parser.add_argument('--hkt-dir', action='append', default=[], help='Select roots from all .hkt files in this directory')
    parser.add_argument('--output', type=Path, default=Path('source/havok_schema.h'))
    args = parser.parse_args()
    roots = set(args.root)
    for path in args.clump:
        roots.update(clump_roots(path))
    for directory in args.hkt_dir:
        for path in Path(directory).glob('*.hkt'):
            if not path.name.startswith('._'):
                roots.update(virtual_names(path.read_bytes()))
    if not roots:
        parser.error('At least one --clump or --root is required')
    try:
        generator = Generator([json.loads(line) for line in args.metadata.read_text().splitlines() if line.strip()])
        for name in sorted(roots):
            generator.select(name)
        output = generator.render()
    except (KeyError, ValueError, struct.error) as exc:
        parser.exit(1, f'Schema generation failed: {exc}\n')
    args.output.write_text(output)
    print(f'Generated {len(generator.scope)} verified class schemas in {args.output}')


if __name__ == '__main__':
    main()
