"""Convert existing manifest packages into bounded native asset packs; read-only inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
if __package__:
    from ..capacity import MAX_DYNAMIC_KEYS
else:
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from capacity import MAX_DYNAMIC_KEYS

ROOT = Path(__file__).resolve().parents[1]
PARTS = {'head': 0, 'body': 1, 'arms': 2, 'waist': 3, 'legs': 4}
MAGIC = b'MHMPACK1'


def encode_catalog(catalog):
    result = bytearray(b'MHMCAT01' + struct.pack('<I', len(catalog['assets'])))
    for entry in catalog['assets']:
        model, name = entry['model_id'].encode('ascii'), entry['pack'].encode('ascii')
        result.extend(struct.pack('<II', PARTS[entry['part']], len(model)))
        result.extend(model)
        result.extend(entry['model_sha256'].encode('ascii'))
        result.extend(entry['sha256'].encode('ascii'))
        result.extend(struct.pack('<I', len(name)))
        result.extend(name)
    return bytes(result)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate manifest field')
        result[key] = value
    return result


def prepare(path,override=None):
    raw = path.read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError('Manifest exceeds bound')
    manifest = json.loads(raw, object_pairs_hook=unique_object) if override is None else override
    if manifest['schema'] != 'mhw-equipment-morph-package-v1' or manifest['abi_version'] != 65536:
        raise ValueError('Unsupported manifest version')
    model = manifest['model_id'].encode('ascii')
    if not 0 < len(model) < 128 or any(c < 32 or c > 126 for c in model):
        raise ValueError('Invalid model identifier')
    part = PARTS[manifest['part']]
    inputs = [{'path': str(path.resolve()), 'sha256': hashlib.sha256(raw).hexdigest()}]

    def load(name, size, digest):
        if type(size) is not int or not 0 <= size <= 64 * 1024 * 1024:
            raise ValueError('Invalid record size')
        if not isinstance(name, str) or Path(name).name != name or '/' in name or '\\' in name or ':' in name:
            raise ValueError('Asset must be a package-local filename')
        source = (path.parent / name).resolve()
        if not source.is_relative_to(path.parent.resolve()):
            raise ValueError('Asset escapes package root')
        data = source.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('Asset size or digest differs')
        inputs.append({'path': str(source), 'sha256': digest})
        return data

    basis = load(manifest['basis'], manifest['basis_bytes'], manifest['basis_sha256'])
    if not basis:
        raise ValueError('Empty basis')
    keys = manifest['keys']
    if not 0 < len(keys) <= MAX_DYNAMIC_KEYS or len({key['id'] for key in keys}) != len(keys):
        raise ValueError('Invalid or duplicate keys')
    shading = manifest['shading']
    if shading['mode'] != 'shared-author-intent-max':
        raise ValueError('Unsupported shading mode')
    rotation_count = shading['rotation_count']
    if type(rotation_count) is not int or not 0 <= rotation_count <= 1000000:
        raise ValueError('Invalid rotation count')
    rotations = load(shading['rotations'], rotation_count * 20, shading['rotations_sha256'])
    result = bytearray(MAGIC + struct.pack('<6I', 1, part, len(basis), len(keys), rotation_count, len(model)))
    result.extend(model)
    result.extend(basis)
    for key in keys:
        ident = key['id'].encode('ascii')
        if not 0 < len(ident) < 64 or any(c < 32 or c > 126 for c in ident):
            raise ValueError('Invalid key identifier')
        if key['minimum'] != 0 or key['maximum'] != 1 or type(key['author_shading']) is not bool:
            raise ValueError('Unsupported weight range or shading flag')
        count = key['position_count']
        exclusive = key['exclusive_group']
        if type(count) is not int or not 0 <= count <= 1000000 or type(exclusive) is not int or not 0 <= exclusive <= 0xffffffff:
            raise ValueError('Invalid key record count/group')
        positions = load(key['positions'], count * 16, key['positions_sha256'])
        result.extend(struct.pack('<6I', len(ident), 0, 0x3f800000, int(key['author_shading']), exclusive, count))
        result.extend(ident)
        result.extend(positions)
    result.extend(rotations)
    if len(result) > 128 * 1024 * 1024:
        raise ValueError('Pack exceeds bound')
    return bytes(result), manifest, inputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Refusing to overwrite catalog')
    pending = [prepare(path) for path in args.manifest]
    if len(pending) > 5 or len({m['part'] for _, m, _ in pending}) != len(pending):
        raise ValueError('Catalog currently accepts one configured model per part')
    catalog = {'schema': 'mhw-equipment-morph-native-catalog-v1', 'abi_version': 65536,
               'game_installed': False, 'native_bindings_generalized': False, 'assets': []}
    for index, (packed, manifest, inputs) in enumerate(pending):
        catalog['assets'].append({'pack': f'asset-{index}.mhmpack', 'bytes': len(packed),
                                 'sha256': hashlib.sha256(packed).hexdigest(),
                                 'model_id': manifest['model_id'], 'part': manifest['part'],
                                 'model_sha256': manifest['model_sha256'],
                                 'keys': [key['id'] for key in manifest['keys']], 'inputs': inputs})
    args.output.mkdir(parents=True)
    for entry, (packed, _, _) in zip(catalog['assets'], pending):
        (args.output / entry['pack']).write_bytes(packed)
    (args.output / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2), encoding='utf-8')
    (args.output / 'catalog.mhmcatalog').write_bytes(encode_catalog(catalog))
    print(json.dumps({'prepared_assets': len(pending), 'output': str(args.output)}))


if __name__ == '__main__':
    main()
