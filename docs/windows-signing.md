# Windows code signing (optional)

Unsigned programs from a downloaded zip make Windows show "Windows protected your PC" (SmartScreen)
the first time someone runs them. The packagers can sign this project's own programs with Azure
Trusted Signing (also called Artifact Signing) before they zip and hash anything. It is off until
the variables below are set; without them packaging prints `signing skipped: MU_SIGN_METADATA not
set` and works exactly as before.

What gets signed: `MeleeUnlockedLauncher.exe`, `melee_port.exe`, `melee_port_compat.exe`,
`melee_port_playback.exe`, `melee_source.exe`, `melee_source_compat.exe`, `melee_game.dll` and
`melee_game_compat.dll`, whichever are in the package (`OWN_BINARIES` in `tools/windows_signing.py`).
Third-party files (NVIDIA, Intel, AMD, Microsoft runtimes, the mod tools) are left as their vendors
shipped them; several are checked against the vendor's exact bytes.

## One-time setup

1. In the Azure portal, create a Trusted Signing account and finish identity validation, then a
   certificate profile (Public Trust). Give your Azure user the Certificate Profile Signer role on
   the account.
2. Write a `metadata.json` anywhere outside the repository:

   ```json
   {
     "Endpoint": "https://eus.codesigning.azure.net",
     "CodeSigningAccountName": "<account name>",
     "CertificateProfileName": "<certificate profile name>"
   }
   ```

   The endpoint is the one for the account's region (shown on the account's overview page).

3. Get `Azure.CodeSigning.Dlib.dll` from the `Microsoft.Trusted.Signing.Client` NuGet package
   (`nuget install Microsoft.Trusted.Signing.Client`, then `bin\x64\Azure.CodeSigning.Dlib.dll`).
4. Install the Windows 10/11 SDK signing tools for `signtool.exe`. Use a recent SDK:
   older signtool builds do not load the dlib.
5. Sign in once with `az login` (Azure CLI). For unattended builds, the dlib also accepts the
   `AZURE_TENANT_ID`, `AZURE_CLIENT_ID` and `AZURE_CLIENT_SECRET` environment credential.

## Each release

Set the variables in the shell that runs the packager, then package as usual:

```bat
set MU_SIGN_METADATA=C:\signing\metadata.json
set MU_SIGN_DLIB=C:\signing\Microsoft.Trusted.Signing.Client\bin\x64\Azure.CodeSigning.Dlib.dll
rem optional, when signtool.exe is not on PATH or in the default SDK folder:
set MU_SIGNTOOL=C:\Program Files (x86)\Windows Kits\10\bin\<SDK version>\x64\signtool.exe
python tools\package_release.py ...
```

The packager signs, then runs `signtool verify /pa` on every signed file. If signing or verification
fails, or only one of the two required variables is set, packaging stops with an error instead of
writing a zip.

SmartScreen reputation follows the signing identity, so a new certificate profile can still show
the warning for a short while after its first releases. `MeleeUnlocked.bat` cannot carry an
Authenticode signature; the launcher is the program to point players at.
