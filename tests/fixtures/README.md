# Public compatibility fixtures

- client-schema.txt: allowlisted declarations for 29 types and two index
  collections, extracted without altering signatures/layouts from the retained
  November 2026 client report (the historical local 14412 recording).
- Extraction keeps only CLASS/COLLECTION headers and FIELD/METHOD declarations.
  Runtime snapshots, account/room values, process identities, module addresses,
  raw attachments and unrelated metadata are not published.
- stronghold-status-schema.txt: explicit static/simulated timing supplement.
- stronghold-source-schema.txt: explicit static ownership supplement, not a
  new live-client recording. Its enum values are ABI test assumptions.
- The schema test additionally supplies a mocked closed singleton and a bounds
  checking size. Neither is claimed as a newly observed live layout.

These fixtures let a clean checkout run the production binder regressions
without access to the game, the original reports or reference/. Field offsets
belong only to test evidence; production still resolves current metadata.
They do not demonstrate compatibility with future game updates or substitute
for multiplayer acceptance tests. Retain original private evidence separately;
add explicitly sourced new fixtures and negative tests when adapting a client.
