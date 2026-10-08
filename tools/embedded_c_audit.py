"""Lexical style gate for owned firmware (not a C semantic analyzer).

Author: 兆鸣嵌入式. CubeMX and third-party sources retain upstream formatting.
"""
from pathlib import Path
import argparse
import json
import re


def mask_comments_and_literals(source):
    pattern = r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern,
                  lambda match: re.sub(r'[^\n]', ' ', match.group()), source)


def scan_functions(code, relative):
    pattern = (r'^((?:static\s+|inline\s+|const\s+|volatile\s+)*[\w]+'
               r'(?:\s+|\s*\*\s*)+)(\w+)\s*\(([^;{}]*?)\)\s*\{')
    functions = []
    for match in re.finditer(pattern, code, re.M):
        if match.group(2) in ('if', 'for', 'while', 'switch'):
            continue
        depth = 1
        maximum = 1
        ending = match.end()
        while ending < len(code) and depth:
            if code[ending] == '{':
                depth += 1
                maximum = max(maximum, depth)
            elif code[ending] == '}':
                depth -= 1
            ending += 1
        first = code.count('\n', 0, match.start()) + 1
        last = code.count('\n', 0, ending) + 1
        parameters = match.group(3).strip()
        count = 0 if parameters in ('', 'void') else parameters.count(',') + 1
        functions.append({'file': relative, 'name': match.group(2),
                          'line': first, 'lines': last - first + 1,
                          'parameters': count, 'nesting': maximum - 1})
    return functions


def audit(root):
    paths = sorted(path for path in (root / 'firmware').rglob('*')
                   if path.suffix in ('.c', '.h') and
                   'cubemx' not in path.parts)
    findings = []
    functions = []
    for path in paths:
        source = path.read_text(encoding='utf-8-sig')
        relative = path.relative_to(root).as_posix()
        for number, line in enumerate(source.splitlines(), 1):
            if len(line.expandtabs(4)) > 80:
                findings.append(f'{relative}:{number}: exceeds 80 columns')
        code = mask_comments_and_literals(source)
        if path.suffix == '.h' and not re.search(r'^#ifndef\s+\w+', code, re.M):
            findings.append(f'{relative}: missing include guard')
        functions.extend(scan_functions(code, relative))
    for function in functions:
        for field, limit in (('lines', 80), ('parameters', 5), ('nesting', 4)):
            if function[field] > limit:
                findings.append(f"{function['file']}:{function['line']}: "
                                f"{function['name']} {field}="
                                f"{function[field]} exceeds {limit}")
    if not paths or not functions:
        findings.append('No owned firmware sources/functions found')
    return {'files': len(paths), 'functions': len(functions),
            'findings': findings}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument('--json', type=Path, help='Optional result file')
    arguments = parser.parse_args()
    result = audit(arguments.root.resolve())
    if arguments.json:
        arguments.json.write_text(json.dumps(result, indent=2),
                                  encoding='utf-8')
    print(f"Owned firmware: {result['files']} files, "
          f"{result['functions']} functions, "
          f"{len(result['findings'])} style violations")
    for finding in result['findings']:
        print(finding)
    return 1 if result['findings'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
