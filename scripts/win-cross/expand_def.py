import re, sys, os

# Развёртка mingw .def.in для x86_64 (non-debug): F64/F_X64/F_NON_I386/...
MACROS = {
    'F64':         lambda a: a,
    'F_X64':       lambda a: a,
    'F_X86_ANY':   lambda a: a,
    'F_NON_I386':  lambda a: a,
    'F_NON_X64':   lambda a: a,
    'F_NON_ARM64': lambda a: a,
    'F_NON_ARM64EC': lambda a: a,
    'F_NON_DEBUG': lambda a: a,
    'F_I386':      lambda a: '',
    'F32':         lambda a: '',
    'F_ARM32':     lambda a: '',
    'F_ARM64':     lambda a: '',
    'F_ARM_ANY':   lambda a: '',
    'F_LD64':      lambda a: a,
    'F_LD80':      lambda a: '',
    'F_DEBUG':     lambda a: '',
}

DEF_INCLUDE = os.environ.get('DEF_INCLUDE_PATH') or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'def-include')

def expand_file(path, stack):
    path = os.path.realpath(path)
    if path in stack:
        return ''
    stack = stack | {path}
    base = os.path.dirname(path)
    out = []
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r'#\s*include\s+"([^"]+)"', line)
        if m:
            inc = os.path.join(base, m.group(1))
            if not os.path.exists(inc):
                alt = os.path.join(DEF_INCLUDE, m.group(1))
                if os.path.exists(alt):
                    inc = alt
            out.append(expand_file(inc, stack))
            i += 1
            continue
        if re.match(r'#\s*(if|elif|else|endif|ifdef|ifndef|error|pragma|define|undef)', line):
            i += 1
            continue
        if line.strip().startswith(';') or line.strip().startswith('//'):
            i += 1
            continue
        changed = True
        while changed:
            changed = False
            for name in MACROS:
                pat = re.compile(r'\b' + name + r'\(([^()]*)\)')
                def repl(m, name=name):
                    return MACROS[name](m.group(1))
                newline = pat.sub(repl, line)
                if newline != line:
                    line = newline
                    changed = True
        if line.strip():
            out.append(line)
        i += 1
    return '\n'.join(out)

print(expand_file(sys.argv[1], set()))
