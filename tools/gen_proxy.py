# Generates winmm proxy: .def + x64 MASM jump stubs + C table of names
import pefile, sys, os
out = sys.argv[1]
pe = pefile.PE(r"C:\Windows\System32\winmm.dll")
syms = sorted(pe.DIRECTORY_ENTRY_EXPORT.symbols, key=lambda e: e.ordinal)
d = ["LIBRARY winmm", "EXPORTS"]
asm = ["; generated", "EXTERN g_proxy_table:QWORD", ".code"]
names = []
for i, e in enumerate(syms):
    stub = f"proxy_stub_{i}"
    if e.name:
        n = e.name.decode()
        d.append(f"    {n}={stub} @{e.ordinal}")
        names.append(f'"{n}"')
    else:
        d.append(f"    ordinal{e.ordinal}={stub} @{e.ordinal} NONAME")
        names.append(f"(const char*)(uintptr_t){e.ordinal}")
    asm += [f"{stub} PROC", f"    jmp qword ptr [g_proxy_table + {i*8}]", f"{stub} ENDP"]
asm.append("END")
open(os.path.join(out, "winmm.def"), "w").write("\n".join(d) + "\n")
open(os.path.join(out, "proxy_stubs.asm"), "w").write("\n".join(asm) + "\n")
open(os.path.join(out, "proxy_names.h"), "w").write(
    "// generated\n#pragma once\n#include <stdint.h>\n#define PROXY_COUNT %d\nstatic const char* const g_proxy_names[PROXY_COUNT] = {\n  %s\n};\n" % (len(names), ",\n  ".join(names)))
print("exports", len(names))
