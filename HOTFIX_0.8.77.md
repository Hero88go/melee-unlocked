# Melee Unlocked 0.8.77 hotfix

- Fix Classic large-team introductions missing portraits or displaying a black half-screen. EFB depth copies and texture depth now render correctly; D3D11 clears the EFB after presentation.
- Permit supported stage model skins online when their collision, transforms, parameters, and animation data match the clean stage. Texture-only skins retain their existing validation. Unsupported or gameplay-changing replacements fall back to the standard stage online.
- Add the optional **Random installed stage skin each match** setting under PC Settings > Mods. It picks skins for the selected stage, filters the online pool, preserves fixed selections, and uses host entropy without consuming game RNG.
- Fix the Static Recomp return path implicated by `crash-1791249437596.md` (0.8.76). A RAM hook may return into a generated Slippi continuation; the interpreter previously rejected that valid branch.

Validated: Classic Stage 8 Team Jigglypuff intro on D3D11 and D3D12; cosmetic, stage safety, GPU and native content transition regressions; Static Gecko return-path regression on AVX2 and SSE2; 1,198 interpreter/translation comparisons with zero failures; Source/Static local Slippi match with zero checksum mismatches.

The crash report provides a precise rejected return path, but not a complete player reproduction. The return regression is tested; the original full session is not reproduced. Stage compatibility remains conservative, including rejection of model archives with unresolved external symbols. This update does not enable new Slippi stage layouts. No ACE or other 0.9 changes are included.
