#!/usr/bin/env python3
"""Signs MeleeUnlocked.app for the hardened runtime, and can notarize it and put it in a disk image.

    python3 port/mac/sign_app.py build-mac-full-arm64-squad/MeleeUnlocked.app
    python3 port/mac/sign_app.py <app> --identity "Developer ID Application: Name (TEAMID)"
    python3 port/mac/sign_app.py <app> --identity "Developer ID Application: ..." --notarize PROFILE --dmg out.dmg

Without --identity the app is signed ad hoc, with library validation off (an ad hoc signature has no
team for it to compare). An ad hoc app runs on this Mac, and on others only after the player
approves it in System Settings. For a double-click on any Mac the app needs a Developer ID
Application certificate (Apple Developer Program), then notarization by Apple. PROFILE names
notarytool credentials stored once in the keychain:

    xcrun notarytool store-credentials PROFILE --apple-id <id> --team-id <TEAMID>

Every Mach-O inside the bundle is signed first (the SDL library and the shader tools), then the app
with the hardened runtime and port/mac/MeleeUnlocked.entitlements. The game library is a Linux ELF
and stays a resource, which the app's signature seals like any other file.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import os
import plistlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENTITLEMENTS = HERE / 'MeleeUnlocked.entitlements'
MACHO = {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}


def run(*cmd, **kw):
    print('+', ' '.join(str(c) for c in cmd))
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


def nested_code(app):
    """Mach-O files in the bundle other than the main executable, deepest first."""
    main = app / 'Contents/MacOS' / plistlib.loads((app / 'Contents/Info.plist').read_bytes())['CFBundleExecutable']
    found = []
    for path in app.rglob('*'):
        if path.is_file() and not path.is_symlink() and path != main:
            with open(path, 'rb') as f:
                if f.read(4) in MACHO:
                    found.append(path)
    return sorted(found, key=lambda p: (-len(p.parts), str(p)))


def sign(app, identity='-', runtime=True):
    """Signs the nested code, then the app. runtime=False (package_full.py's local builds, which lldb
    can attach to) signs without the hardened runtime, and so without entitlements."""
    adhoc = identity == '-'
    common = ['codesign', '--force', '--sign', identity, '--timestamp=none' if adhoc else '--timestamp',
              *(['--options', 'runtime'] if runtime else [])]
    for path in nested_code(app):
        run(*common, path)
    if not runtime:
        run(*common, app)
    else:
        entitlements = plistlib.loads(ENTITLEMENTS.read_bytes())
        if adhoc:
            # Library validation loads only libraries signed by the app's own team, and an ad hoc
            # signature has no team: the app could not load its own SDL. With a Developer ID the app
            # and its libraries share the team, and validation stays on.
            entitlements['com.apple.security.cs.disable-library-validation'] = True
        with tempfile.NamedTemporaryFile(suffix='.entitlements') as f:
            f.write(plistlib.dumps(entitlements))
            f.flush()
            run(*common, '--entitlements', f.name, app)
    run('codesign', '--verify', '--strict', '--deep', app)


def notarize(path, profile):
    """Submits a zip of the app, or a disk image, and staples the ticket to it."""
    with tempfile.TemporaryDirectory() as tmp:
        upload = path
        if path.suffix == '.app':
            upload = Path(tmp) / (path.stem + '.zip')
            run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', path, upload)
        result = run('xcrun', 'notarytool', 'submit', upload, '--keychain-profile', profile, '--wait',
                     '--output-format', 'json', stdout=subprocess.PIPE, text=True)
    answer = json.loads(result.stdout)
    print(f"notarization {answer.get('id')}: {answer.get('status')}")
    if answer.get('status') != 'Accepted':
        run('xcrun', 'notarytool', 'log', answer.get('id', ''), '--keychain-profile', profile)
        raise SystemExit('notarization was not accepted')
    run('xcrun', 'stapler', 'staple', path)
    run('xcrun', 'stapler', 'validate', path)


def disk_image(app, dmg, identity):
    """A compressed disk image with the app and a link to Applications, signed when not ad hoc."""
    dmg.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        staging = Path(tmp) / 'Melee Unlocked'
        staging.mkdir()
        run('ditto', app, staging / app.name)
        os.symlink('/Applications', staging / 'Applications')
        run('hdiutil', 'create', '-volname', 'Melee Unlocked', '-srcfolder', staging, '-fs', 'HFS+',
            '-format', 'UDZO', '-ov', dmg)
    if identity != '-':
        run('codesign', '--force', '--sign', identity, '--timestamp', dmg)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('app', type=Path)
    ap.add_argument('--identity', default='-', help='a Developer ID Application identity (default: ad hoc)')
    ap.add_argument('--notarize', metavar='PROFILE', help='notarytool keychain profile; needs --identity')
    ap.add_argument('--dmg', type=Path, help='also make this disk image of the signed app')
    args = ap.parse_args()
    app = args.app.resolve()
    if not (app / 'Contents/Info.plist').is_file():
        raise SystemExit(f'{app} is not an app bundle')
    if args.notarize and args.identity == '-':
        raise SystemExit('notarization needs a Developer ID Application identity (--identity)')
    sign(app, args.identity)
    if args.notarize:
        notarize(app, args.notarize)
    if args.dmg:
        disk_image(app, args.dmg.resolve(), args.identity)
        if args.notarize:
            notarize(args.dmg.resolve(), args.notarize)
    # Gatekeeper's verdict for a download: "accepted" only once a Developer ID app is notarized.
    subprocess.run(['spctl', '--assess', '--type', 'execute', '--verbose=2', str(app)])
    return 0


if __name__ == '__main__':
    sys.exit(main())
