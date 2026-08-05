# EditContext prototype recovery notes

This directory reconstructs the shared editing layer from the published
EditContext feasibility artifacts. It is intentionally kept separate from the
platform wiring until the recovered API and behavior are covered by tests.

## Provenance

- Original mobile branch: `codex/editcontext-mobile`
- Original mobile commit recorded in the Android binary:
  `c829ddcd948b11c991aab1878f995a9970c316a5`
- Original desktop branch: `codex/text-editcontext-pc`
- Original desktop commit recorded in the release notes:
  `192c9cf25e1c1028b19bac41022871b36b579c54`
- Android artifact SHA-256:
  `ed3b4005162d312d335edcf0a852937170477f49375469eb570db1e62a57e677`
- macOS artifact SHA-256:
  `c9b81e916fc9dcb89b811e0b713326bc0435a5d3e006b536ff396961c8759024`

Neither recorded commit is reachable from the public repositories. The GitHub
release tag points to an unrelated March 2025 `develop` commit, so it cannot be
used as a source snapshot.

## Confidence levels

- Android `TextEditContext*` classes and the `AndroidText`/touch-dispatch
  integration are recovered from DEX bytecode. Names and method bodies are
  high-confidence; decompiler artifacts are normalized manually.
- Public C++ class and method names are recovered from the unstripped macOS
  binary. Data layouts and implementations in this directory are behavioral
  reconstructions and must be treated as such until verified by tests.
- The compiled demo template is preserved in the release artifacts, but its
  original TypeScript/TSX source is not recoverable byte-for-byte.

## Virtualized list boundary

The projection model can represent one selection across several text owners.
That does not by itself make a virtualized list an editing host. The recovered
Android layout collector requires every projected text/atomic segment to have a
live `LynxBaseUI`; if any item has been recycled or has not been mounted, layout
refresh fails. A list prototype therefore needs a list-owned logical document
and a partial/virtual geometry contract rather than simply attaching one
EditContext to each item.
