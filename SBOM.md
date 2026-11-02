# Source SBOM

Source repository inventory only; completeness is UNKNOWN. Syft inventories tracked source/package manifests. Gitlinks and declared vendored/model evidence are inspected at immutable pins. CMake declarations are conditional requirements, not resolved installations. This is NOT proof of operating-system packages, static-link closure, build-time resolution, or the contents of release binaries.

Generation: **success**

## Source identity

wave-owl/fldigi-feldhell-core `e3d7c65f748f791b26a4b471f20050eb0f10cfdf`

- sbom:role: source-root
- sbom:git-commit: e3d7c65f748f791b26a4b471f20050eb0f10cfdf
- sbom:git-tree: 6ca8f5f566245395a8de88a9e650a155a19f8b7f
- sbom:source-repository: https://github.com/wave-owl/fldigi-feldhell-core
- sbom:release-tags: ["v0.1.0"]
- sbom:license-evidence: [{"path": "LICENSE", "sha256": "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"}]
- sbom:license-resolution: unresolved; manual review required

## Generation errors

None.

## Components and unresolved licenses

| Component | Version | Role | License |
| --- | --- | --- | --- |
| wave-owl/fldigi-feldhell-core | e3d7c65f748f791b26a4b471f20050eb0f10cfdf | source-root | unresolved; manual review required |
| wave-owl/fldigi-dsp-core | 6314a0ddd9389911114a6045e56c3e44f734f482 | gitlink | unresolved; manual review required |
| src/feldhell_common.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/feldhell_fonts.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/feldhell_rx.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| src/feldhell_tx.c | e7c3ab3709ad62a7d91829364eb48a05fa325c0d | declared-vendored-source | unresolved; manual review required |
| actions/checkout | v4 | Syft source/package discovery | unresolved |
| actions/checkout | v4 | Syft source/package discovery | unresolved |
| tracked-source/.github/workflows/ci.yml | unresolved | Syft source/package discovery | unresolved |
| tracked-source/dsp/.github/workflows/ci.yml | unresolved | Syft source/package discovery | unresolved |

License evidence paths/hashes, source manifests, model hashes and conditional
CMake declarations are recorded in SBOM.cdx.json. Declared upstream pins do not
prove vendored equivalence. No dependency installation or build was performed.
