"""link_tables.py <outdir> <tag>=<relative path in the game folder>... : the global function table, import table and
image registry for a multi-image build (the main executable has the empty tag and path)."""
import sys, os, json
out = sys.argv[1]; imgs = []
for spec in sys.argv[2:]:
    tag, rel = spec.split('=', 1)
    imgs.append((tag, rel, json.load(open(os.path.join(out, (tag + '_' if tag else '') + 'image.json')))))
fns = sorted(f for _, _, j in imgs for f in j['functions'])
imps = []
for tag, rel, j in sorted(imgs, key=lambda t: t[2]['imp_base']):
    assert j['imp_base'] == len(imps), 'import bases must be contiguous'
    imps += j['imports']
with open(os.path.join(out, 'fntab.c'), 'w') as t:
    t.write('#include "rt.h"\n')
    t.writelines('void f_%08x(Ctx *c);\n' % x for x in fns)
    t.write('const struct rt_fn { uint32_t addr; GuestFn fn; } rt_fntab[] = {\n')
    t.writelines('    {0x%08xu, f_%08x},\n' % (x, x) for x in fns)
    t.write('};\nconst unsigned rt_fntab_n = %d;\n' % len(fns))
    t.write('const struct rt_imp { uint32_t iat; const char *name; } rt_imptab[] = {\n')
    t.writelines('    {0x%08xu, "%s"},\n' % (va, n) for va, n in imps)
    t.write('};\nconst unsigned rt_imptab_n = %d;\n' % len(imps))
    t.write('const struct rt_image { const char *name; uint32_t base, imp_first, imp_count; } rt_images[] = {\n')
    t.writelines('    {"%s", 0x%xu, %d, %d},\n' % (rel, j['base'], j['imp_base'], len(j['imports'])) for tag, rel, j in imgs)
    t.write('};\nconst unsigned rt_images_n = %d;\n' % len(imgs))
    # guest blocks the runtime commits at load time: arrays the lifted code moved (lift.py DF_MOVED, BLOCKS_C)
    t.write('const struct rt_block { uint32_t addr, size; const char *what; } rt_blocks[] = {\n')
    t.writelines('    {0x%08xu, 0x%xu, "%s"},\n' % tuple(b) for _, _, j in imgs for b in j.get('blocks', []))
    t.write('    {0, 0, 0}\n};\n')
print('%d functions, %d imports, %d images' % (len(fns), len(imps), len(imgs)))
