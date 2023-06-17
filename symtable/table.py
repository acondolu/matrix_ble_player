import subprocess

SYMBOLS = ["Serial", "_ZN5Print6printfEPKcz"]

gperf_input = b"struct entry { const char* name; int index; };\n%%\n" + "\n".join("%s, %d" % (SYMBOLS[i], i) for i in range(len(SYMBOLS))).encode("ascii")

gperf_proc = subprocess.Popen(["gperf", "-t", "-C", "-G", "-LANSI-C"], stdout=subprocess.PIPE, stdin=subprocess.PIPE)
gperf_output = gperf_proc.communicate(input=gperf_input)[0].decode("ascii")

asms = "\n".join('asm(".global %s");' % sym for sym in SYMBOLS)
externs = "\n".join('extern void* %s;' % sym for sym in SYMBOLS)
commas = ",".join(SYMBOLS)

OUT = """
// #include "hello.h"

namespace {
#include <cstring>
#include <cstddef>
{{GPERF_OUTPUT}}
}

{{ASMS}}
{{EXTERNS}}

namespace {
  static void* values[] = {{{COMMAS}}};
}

namespace SymbolTable {

  uint32_t* lookup(const char* key) {
    const struct entry* result = in_word_set(key, strlen(key));
    if (result == nullptr) return nullptr;
    return (uint32_t*) values[result->index];
  }

}
"""

print(OUT.replace("{{ASMS}}", asms).replace("{{EXTERNS}}", externs).replace("{{GPERF_OUTPUT}}", gperf_output).replace("{{COMMAS}}", commas))