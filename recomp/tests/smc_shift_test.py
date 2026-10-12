"""Execute self-modified MMX/SSE shifts against a bitwise reference for all imm8s."""
from pathlib import Path
from types import SimpleNamespace
import capstone
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from lift import Lifter
from compiler_compat import compatible_flags

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
lifter = Lifter.__new__(Lifter)
lifter.md = md
forms = [('psllw', '0f71f377', 16, 1, 0), ('pslld', '0f72f377', 32, 1, 0),
         ('psllq', '0f73f377', 64, 1, 0), ('psrlw', '0f71d377', 16, 0, 0),
         ('psrld', '0f72d377', 32, 0, 0), ('psrlq', '0f73d377', 64, 0, 0),
         ('psraw', '0f71e377', 16, 0, 1), ('psrad', '0f72e377', 32, 0, 1),
         ('pslldq', '660f73fb77', 128, 1, 0), ('psrldq', '660f73db77', 128, 0, 0)]
out = ['#include "rt.h"\n#include <assert.h>\n#include <stdio.h>\nuint8_t *G_MEM;\n']
cases = []
for name, raw, lane, left, arithmetic in forms:
    for xmm in ([True] if name.endswith('dq') else [False, True]):
        code = bytes.fromhex(('66' if xmm and not name.endswith('dq') else '') + raw)
        i = next(md.disasm(code, 0x1000))
        assert i.mnemonic == name
        field = i.address + i.imm_offset
        lifter.smc = {i.address: [(field, 1, 'imm')]}
        body = lifter.emit_smc(i, i.address, {}, set())
        assert 'switch' not in body and f'R8(0x{field:08x}u)' in body
        fn = f'run_{len(cases)}'
        out.append(f'static void {fn}(Ctx *c) {{ REGS; {body} SPILL; }}\n')
        cases.append(f'{{{fn}, {field}, {int(xmm)}, {lane}, {left}, {arithmetic}, {8 if name.endswith("dq") else 1}}}')
out.append('''
static void check_dword_arithmetic(void) {
    uint32_t rng=0x83491257;
    for(int trial=0;trial<16384;trial++) {
        X128 a,b,expected,actual;
        for(int k=0;k<4;k++) { rng=rng*1664525u+1013904223u;a.d[k]=rng; rng=rng*1664525u+1013904223u;b.d[k]=rng; }
        for(int sub=0;sub<2;sub++) for(int same=0;same<2;same++) {
            const X128 *src=same?&a:&b;
            for(int k=0;k<4;k++) expected.d[k]=sub?a.d[k]-src->d[k]:a.d[k]+src->d[k];
            actual=a;
            if(sub) sse_psubd(&actual,same?&actual:&b); else sse_paddd(&actual,same?&actual:&b);
            assert(!memcmp(&actual,&expected,16));
        }
    }
}
static void reference(uint8_t *dst, const uint8_t *src, int bits, int lane, unsigned n, int left, int arithmetic) {
    memset(dst, 0, bits / 8);
    for (int b = 0; b < bits; b++) {
        int first = b / lane * lane, local = b % lane;
        int source = left ? local - (int)n : local + (int)n;
        int bit = source >= 0 && source < lane ? first + source : arithmetic ? first + lane - 1 : -1;
        if (bit >= 0 && (src[bit / 8] & (1u << (bit % 8)))) dst[b / 8] |= 1u << (b % 8);
    }
}
int main(void) {
    check_dword_arithmetic();
    struct { void (*fn)(X128 *,uint64_t); int lane,left,arithmetic; } wide[] = {
        {sse_psllw,16,1,0},{sse_pslld,32,1,0},{sse_psllq,64,1,0},
        {sse_psrlw,16,0,0},{sse_psrld,32,0,0},{sse_psrlq,64,0,0},
        {sse_psraw,16,0,1},{sse_psrad,32,0,1}
    };
    uint64_t counts[]={0,1,15,16,31,32,63,64,255,256,0x100000000ull,0x8000000000000000ull,UINT64_MAX};
    for(unsigned f=0;f<sizeof wide/sizeof wide[0];f++)
    for(unsigned n=0;n<sizeof counts/sizeof counts[0];n++) {
        X128 actual={.q={0xfedcba9876543210ull,0x12345678abcdef01ull}}, expected;
        reference(expected.b,actual.b,128,wide[f].lane,counts[n]>128?128:(unsigned)counts[n],wide[f].left,wide[f].arithmetic);
        wide[f].fn(&actual,counts[n]);assert(!memcmp(&actual,&expected,16));
    }

    uint8_t mem[0x2000] = {0}; G_MEM = mem;
    struct { void (*fn)(Ctx *); unsigned field; int xmm, lane, left, arithmetic, scale; } forms[] = {
''')
out.append(',\n'.join(cases))
out.append('''
    };
    uint64_t rng = 0x8b94a92d31478123ull;
    for (unsigned f = 0; f < sizeof forms / sizeof forms[0]; f++)
    for (int trial = 0; trial < 16; trial++)
    for (unsigned count = 0; count < 256; count++) {
        Ctx c = {0};
        for (unsigned b = 0; b < sizeof c; b++) {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
            ((uint8_t *)&c)[b] = (uint8_t)rng;
        }
        Ctx expected = c;
        uint8_t *src = forms[f].xmm ? c.xmm[3].b : (uint8_t *)&c.mm[3];
        uint8_t *dst = forms[f].xmm ? expected.xmm[3].b : (uint8_t *)&expected.mm[3];
        reference(dst, src, forms[f].xmm ? 128 : 64, forms[f].lane,
                  count * forms[f].scale, forms[f].left, forms[f].arithmetic);
        mem[forms[f].field] = count;
        forms[f].fn(&c);
        assert(!memcmp(&c, &expected, sizeof c));
    }
    puts("SMC shifts: 18 forms, all 256 counts, 16 random contexts; 64-bit counts and dword arithmetic passed; context preserved");
}
''')
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'shifts.c'
    binary = Path(tmp) / 'shifts'
    source.write_text(''.join(out))
    flags = compatible_flags('clang')
    for mode in ([], ['-DRT_SSE_SCALAR']):
        subprocess.run(['clang', *flags, *mode, '-O2', '-fno-strict-aliasing',
                        '-I', str(ROOT / 'runtime'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
