# Reference only, not built

MeshCore's companion firmware (examples/companion_radio) at tag companion-v1.18.0, commit 81dbae5,
MIT licence (LICENSE-MeshCore-MIT.txt). Copied 2026-11-10 (S494, Claude) so DS can read the companion
protocol inside its sandbox. Our vendored core in lib/meshcore is byte-identical to the same commit
(161 of 161 files compared), so this is the matching protocol code. PlatformIO builds src/ and lib/
only; nothing here is compiled. Adapt what is needed into src/, keep the MIT notice on adapted code.
