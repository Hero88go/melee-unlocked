# Run inside gdb: DECOMP_INFO_IN=request.json DECOMP_INFO_OUT=info.json \
#   gdb -batch -x tools/tmce/gdb_decomp_info.py build-sourceport-gcc/melee_game.dbg
# request: {"types": [...], "functions": [...], "globals": [...]}
# out: types as tools/fieldmap/gdb_struct_map.py writes them; functions with the class of the return
# value and of each parameter ('i' integer or pointer, 'f' float, 'd' double, 'sN' struct of N bytes,
# 'v' void, '...' variadic); globals with their type name, size and whether they are file-local.
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'fieldmap'))
os.environ['ALIAS_MAIN'] = '0'
os.environ.setdefault('FIELDMAP_TYPES', '')
os.environ.setdefault('FIELDMAP_OUT', os.devnull)
import gdb  # noqa: E402
import gdb_layout as L  # noqa: E402
import gdb_struct_map as S  # noqa: E402  (imports only; FIELDMAP_TYPES empty = no work)

req = json.load(open(os.environ['DECOMP_INFO_IN']))
out = {'types': {}, 'functions': {}, 'globals': {}}


def cls(t):
    s = t.strip_typedefs()
    c = s.code
    if c == gdb.TYPE_CODE_VOID:
        return 'v'
    if c == gdb.TYPE_CODE_FLT:
        return 'f' if s.sizeof == 4 else 'd'
    if c in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        return 's%d' % s.sizeof
    return 'i'


def decomp_type(name):
    """The decomp's type of this name. TM-CE, compiled into the same DLL, declares types with some
    of the same names (ColorOverlay, CollData, HSD_Archive...); a plain lookup can return its
    MexTK twin, so only a declaration from a source file outside tmce/ is accepted."""
    domains = (gdb.SYMBOL_STRUCT_DOMAIN, gdb.SYMBOL_VAR_DOMAIN)
    for fn in CU_FUNCS:
        s = gdb.lookup_global_symbol(fn) or gdb.lookup_static_symbol(fn)
        if s is None or s.symtab is None:
            continue
        for block in (s.symtab.static_block(), s.symtab.global_block()):
            for dom in domains:
                try:
                    sym = gdb.lookup_symbol(name, block, dom)[0]
                except gdb.error:
                    sym = None
                if sym is None or sym.symtab is None:
                    continue
                path = sym.symtab.filename.replace('\\', '/')
                if '/tmce/' in path:
                    continue
                return sym.type
    return None


# one function from each area of the decomp: their compile units see the decomp's own types
CU_FUNCS = ['Fighter_ChangeMotionState', 'ftCo_Damage_CalcAngle', 'mnCharSel_802640A0', 'mnEvent_8024D15C',
            'gmMainLib_8015CF5C', 'onEnterVs', 'gm_801A4BD4', 'lbArchive_LoadSymbols', 'mpCheckFloor',
            'Camera_SetModeToFixed', 'it_8026E15C', 'grStadium_801D4548', 'HSD_JObjLoadJoint',
            'HSD_SisLib_803A6754', 'DevText_SetBGColor', 'lbColl_80008FC8', 'lbDvd_GetPreloadCacheScene',
            'gmVsMelee_GetKOCounts', 'Player_GetEntity', 'HSD_PadRenew', 'efSync_Spawn', 'HSD_CObjSetCurrent']

for name in req.get('types', []):
    t = decomp_type(name) or decomp_type('struct ' + name)
    if t is None:
        print('missing type', name)
        continue
    rows = []
    S.members(t, 0, 0, '', rows)
    out['types'][name] = dict(console_size=L.cl(t)[0], native_size=t.strip_typedefs().sizeof, members=rows)

for name in req.get('functions', []):
    sym = gdb.lookup_global_symbol(name) or gdb.lookup_static_symbol(name)
    if sym is None:
        out['functions'][name] = None
        continue
    ft = sym.type.strip_typedefs()
    if ft.code != gdb.TYPE_CODE_FUNC:
        out['functions'][name] = {'not_function': str(sym.type)}
        continue
    params = []
    variadic = False
    for f in ft.fields():
        if f.type is None:
            variadic = True
            continue
        params.append(cls(f.type))
    try:
        variadic = variadic or ft.fields() and ft.fields()[-1].type is None
    except Exception:
        pass
    out['functions'][name] = dict(ret=cls(ft.target()), params=params, text=str(sym.type),
                                  static=not bool(gdb.lookup_global_symbol(name)))

for name in req.get('globals', []):
    sym = gdb.lookup_global_symbol(name)
    local = False
    if sym is None:
        sym = gdb.lookup_static_symbol(name)
        local = True
    if sym is None:
        out['globals'][name] = None
        continue
    t = sym.type
    out['globals'][name] = dict(type=str(t), size=t.strip_typedefs().sizeof, local=local,
                                target=str(t.strip_typedefs().target()) if t.strip_typedefs().code == gdb.TYPE_CODE_PTR else None)

json.dump(out, open(os.environ['DECOMP_INFO_OUT'], 'w'), indent=0)
print('types', len(out['types']), 'functions', len(out['functions']), 'globals', len(out['globals']))
