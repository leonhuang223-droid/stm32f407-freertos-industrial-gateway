"""Analyze owned C translation units and headers using a firmware build.

Author: 兆鸣嵌入式. Requires GCC with -fanalyzer and compile_commands.json.
Results are compiler evidence, not a proof of runtime or board correctness.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import ctypes
import json
import os
import shlex
import subprocess


def split_command(item):
    if 'arguments' in item:
        return item['arguments']
    if os.name != 'nt':
        return shlex.split(item['command'])
    from ctypes import wintypes
    parser = ctypes.windll.shell32.CommandLineToArgvW
    parser.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    parser.restype = ctypes.POINTER(wintypes.LPWSTR)
    count = ctypes.c_int()
    array = parser(item['command'], ctypes.byref(count))
    if not array:
        raise OSError('Cannot parse compiler command')
    try:
        return [array[index] for index in range(count.value)]
    finally:
        free = ctypes.windll.kernel32.LocalFree
        free.argtypes = [ctypes.c_void_p]
        free.restype = ctypes.c_void_p
        free(ctypes.cast(array, ctypes.c_void_p))


def compiler_arguments(item, remove_source=False):
    args = []
    skip = False
    for argument in split_command(item):
        if skip:
            skip = False
            continue
        if argument in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
        elif argument in ('-MD', '-MMD', '-MP'):
            continue
        elif remove_source and (argument == '-c' or
                                Path(argument) == Path(item['file'])):
            continue
        else:
            args.append(argument)
    return args


def owned_source(item, root):
    path = Path(item['file'])
    try:
        relative = path.relative_to(root / 'firmware')
    except ValueError:
        return False
    return relative.suffix == '.c' and 'cubemx' not in relative.parts


def run_compiler(args, directory, source):
    result = subprocess.run(args, cwd=directory, capture_output=True)
    return {'source': source, 'returncode': result.returncode,
            'diagnostics': result.stderr.decode('utf-8', errors='replace')}


def analyze_sources(units, output, jobs):
    def analyze(pair):
        index, item = pair
        args = compiler_arguments(item)
        args += ['-o', str(output / f'unit-{index}.o'), '-fstack-usage',
                 '-fanalyzer', '-Werror', '-Wvla', '-Wshadow', '-Wformat=2',
                 '-Wstrict-prototypes', '-Wmissing-prototypes']
        if Path(item['file']).name in ('f407_interrupts.c',
                                      'f407_fault_capture.c'):
            # Fault/NMI barriers intentionally wait for hardware reset.
            args.append('-Wno-analyzer-infinite-loop')
        return run_compiler(args, item['directory'], item['file'])
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        return list(pool.map(analyze, enumerate(units)))


def analyze_headers(root, units, output, jobs):
    item = next(unit for unit in units
                if Path(unit['file']).name == 'app_rtos.c')
    base = compiler_arguments(item, remove_source=True)
    base += ['-fsyntax-only', '-Werror', '-x', 'c']
    base += ['-I' + str(path) for path in (root / 'firmware').rglob('include')
             if 'cubemx' not in path.parts]
    stub = output / 'header_probe.c'
    stub.write_text('/* Header self-containment probe. */\n', encoding='utf-8')
    headers = sorted(path for path in (root / 'firmware').rglob('*.h')
                     if 'cubemx' not in path.parts)

    def analyze(header):
        return run_compiler(base + ['-include', str(header), str(stub)],
                            item['directory'], str(header))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        return list(pool.map(analyze, headers))


def stack_frames(output, count):
    frames = []
    for index in range(count):
        path = output / f'unit-{index}.su'
        if not path.exists():
            continue
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            columns = line.split('\t')
            if len(columns) == 3:
                frames.append({'function': columns[0], 'bytes': int(columns[1]),
                               'kind': columns[2]})
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/debug'))
    parser.add_argument('--output-dir', type=Path,
                        default=Path('build/review/arm-analysis'))
    parser.add_argument('--jobs', type=int, default=4)
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if arguments.jobs < 1:
        parser.error('--jobs must be positive')
    output = arguments.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    database = json.loads((arguments.build_dir / 'compile_commands.json').read_text())
    units = [item for item in database if owned_source(item, root)]
    if not units:
        parser.error('No owned firmware C translation units found')
    sources = analyze_sources(units, output, arguments.jobs)
    headers = analyze_headers(root, units, output, arguments.jobs)
    frames = stack_frames(output, len(units))
    findings = [entry for entry in sources + headers
                if entry['returncode'] or entry['diagnostics'].strip()]
    result = {'translation_units': len(sources),
              'unique_sources': len({entry['source'] for entry in sources}),
              'headers': len(headers), 'findings': findings,
              'stack_frames': frames}
    (output / 'report.json').write_text(json.dumps(result, indent=2),
                                       encoding='utf-8')
    print(f"ARM units: {len(sources)}; headers: {len(headers)}; "
          f"diagnostics: {len(findings)}")
    for finding in findings:
        print(finding['source'])
        print(finding['diagnostics'])
    return 1 if findings else 0


if __name__ == '__main__':
    raise SystemExit(main())
