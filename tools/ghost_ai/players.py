"""Explicit demonstration identities. Array order of deployed players is a model ABI."""
PLAYERS = {
    'mang0': {'code': 'MANG#0', 'css_character': 20, 'internal_character': 22},
    'zain': {'code': 'ZAIN#0', 'css_character': 9, 'internal_character': 18},
    'hbox': {'code': 'HBOX#305', 'css_character': 15, 'internal_character': 15},
    'cody': {'code': 'IBDW#0', 'css_character': 2, 'internal_character': 1},
    # Owner excluded Ginger for popularity. aMSa adds Yoshi and has 15,552 main-character
    # games in the surveyed profile. This is a collection identity, not a deployed policy.
    'amsa': {'code': 'AMSA#0', 'css_character': 17, 'internal_character': 14},
}


def normalize_code(code):
    return (code or '').replace('＃', '#').strip().upper()


def verified_port(start_players, pro):
    """Resolve by exact known code, never infer a person's identity from their character."""
    want = PLAYERS[pro]
    matches = [i for i, p in enumerate(start_players)
               if normalize_code(p.netplay.code if p.netplay else '') == want['code']]
    if len(matches) != 1:
        return None
    i = matches[0]
    if int(start_players[i].character) != want['css_character']:
        return None
    return i
