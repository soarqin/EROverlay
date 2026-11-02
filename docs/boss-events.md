# Boss event discovery and revival evidence

The catalogue is discovered from the game's enabled **Display Boss Health Bar**
instructions, not from a bundled list of NPCs or from an NPC stat/category flag.
`GameAreaParam` supplies useful defeat, map and position facts, but a row there
does not establish that the character has a named bottom-of-screen health bar.
The PARAM and current-language text readers are described in
[Boss data sources](boss-data-sources.md).

The current product uses the user-approved old **207-entry scope**, keeping the
two Hinterland encounters separate. The [207-entry scope](boss-207-scope-2026-11-02.md)
describes the three external cross-location relationships and conservative
scene validation. The earlier [battle-count audit](boss-battle-count-2026-11-02.md)
records **211 static candidates**, rather than 211 confirmed playable battles.
One common-event template references entity `1038540800`, which is absent from
the archived scene; its name does not establish a first Fallingstar Beast fight.

## Event contract

`crates/game/src/boss_events.rs` accepts decompressed PC EMEVD v205 bytes. It
validates the file header, table extents, instruction argument ranges, parameter
patch targets and supplied parameter extents before using them. Explicit limits
cover source bytes, events, instructions, argument size, expanded instances and
expanded instruction count. This code performs no file IO or native calls.

An enabled `2003[11]` instruction is the positive Boss evidence. Its 16-byte
arguments are `enabled:u8@0`, `entity:u32@4`, `slot:u16@8` and `NpcName ID:u32@12`.
Disabled bars, zero entity/name IDs and the invalid name ID are excluded.
Here, enabled means the instruction's argument is `1`. Discovery expands
constructor calls statically; it does not execute every quest, network or
control-flow condition to prove that a bar appears in the current playthrough.

The reader instantiates native constructors `0/50`, `100/150` and `200/250`.
It follows `2000[0]` and `2000[6]`, patches each target instruction from the
caller's actual argument bytes, and preserves the runtime field-map variant.
Linked resources precede the map resource when resolving an event definition.
Ordinary event completion flags use event ID plus the nonnegative signed slot;
common calls use native map/variant scope and the file-order ordinal of unique
opcode-6 argument offsets. This ordinal includes opcode 6 in other banks, as
the original native implementation does. Missing event definitions remain
explicit evidence gaps.

A health-bar instruction is not enough to identify a persistent defeat flag.
The reader also finds `2003[12]` Boss-defeat instructions. A candidate flag must
be both conditionally read and written true in that same instantiated defeat
event. It then joins a display by its exact entity, the event's explicit
dead/HP condition targets, or a shared exact conditional flag. Display guards
take precedence over other writes that repair earlier quest progression.
For proxy bars, the display event's explicit dead/HP/AI targets can establish
the relationship. Conflicting candidates stay unknown; numerical similarity
between IDs never establishes a relationship.
These are structural event associations, not a full event-interpreter proof.
Raw event results can represent surrender or withdrawal without killing the
character. The selected scope applies the explicit rules below: Patches'
initial surrender counts, while the early dragon withdrawal is not a separate
completed progress entry.
`GameAreaParam` is an optional exact flag-to-map/position lookup, not a required
membership list or an authority for substituting similar-looking flag IDs.

Examples from the original resources:

| Battle | Positive bar evidence | Persistent completion evidence |
|---|---|---|
| Godrick | `m10_00_00_00`, event `10002810`, instruction 72, entity `10000800`, NpcName `904750000` | Defeat event `10002800` reads and writes `10000800`; the displaying event guards the same flag |
| Margit | Same resource, event `10002860`, instruction 47, NpcName `902130000` | Exact guard and defeat write `10000850` |
| Coastal Cave chiefs | Event `31152810` enables entity `31150800`/NpcName `904120310` and entity `31150801`/NpcName `904120311` | Both belong to completion flag `31150800`; the catalogue counts one battle |
| Godfrey / Hoarah Loux | Multiple phase display events and different names | Shared persistent defeat flag `11050800`; one progress entry |
| Snowfield Erdtree Avatar | The bar is displayed on proxy entity `1052560803` | The display event explicitly checks the real encounter's AI/death targets; its defeat event proves `1052560800` |

## Runtime preparation

The native executable uses `event:/%s.emevd`, `event:/common_func.emevd` and
`event:/common.emevd`. These mounted VFS paths omit `.dcx`; native file services
perform decompression. The plugin obtains its map-resource directory from
`MapDefaultInfoParam` row IDs, supplemented by actual map fields in
`GameAreaParam`, `BonfireWarpParam` and `WorldMapPointParam`. Field-area ceremony
resources use the supported `00/10/20` versions. There is no hardcoded Boss-map
or defeat-flag catalogue.

Catalogue preparation runs on the overlay update thread. At most two native
file jobs are pending. A rejected request backs off; an accepted failed request
gets one delayed retry. At most one completed resource is parsed and one map
is expanded per update. Limits are 4,096 distinct resource paths and 32 MiB of
parsed EMEVD source, plus 32 MiB of accepted decompressed scene input. Ordinary absent optional map versions are not reported as
on-screen errors. Invalid resource metadata and unavailable required links are
tracked separately. Native file ownership still ends through `release_file`.

The finished catalogue is immutable. Opening the full-screen map, changing
player/save context or temporarily unavailable game pointers does not restart
catalogue IO. A complete nonzero PARAM-pointer change or the settings action to
reload game data starts a fresh catalogue and releases obsolete pending jobs.
Rendering only reads the published catalogue and completion-state snapshot.

Known progress entries merge by their proved completion flag and the configured
cross-location relationships, preserving multiple phase names without counting
them twice. Unknown entries carry flag
zero, display `?`, do not contribute to progress totals and cannot revive.
Unknown bars merge only for the same map, runtime ceremony variant and exact
entity. Reusing an entity number in another map or variant does not join those
encounters. Duplicate bars for that same scoped entity can still retain their
multiple names.
Missing native text is shown as `NpcName#<id>`; no offline language catalogue is
bundled. The archived production-input check currently has no unknown entries
or missing Boss names.

After event discovery, candidates without a precise PARAM relationship request
scene indexes using the native area-selection rule. The archived executable's
`0x140e07940–0x140e07c39` calls `0x140e09fa0`: with
`CS.DLC.IsEnableDlcFileMount` false it selects `mapstudio:/<map>.msb`. DLC2 is
selected only when the switch is true, the packed map's highest area byte is
at most 61, and bitset `0x20000f0012700000` contains that area. Candidate areas
are 20, 21, 22, 25, 28, 40, 41, 42, 43 and 61.

The production `scene_mounts` function always selects only base for other areas,
including this audit's `m31` and `m60` scenes. It does not read the global switch,
so it conservatively requests both base and `mapstudio_dlc2:/<map>.msb` for a
possible DLC2 area, including this audit's `m61` scene. These requests use the same pending-job, parse and
cancellation lifecycle. Filtering runs once after this stage. A supported actor
or actor group in any selected scene keeps the candidate. Unknown read/parse results,
unsupported operations, proxy/foreign-map ambiguity or an exhausted request or
byte budget also keep it; failure is not proof that an actor is absent. Only
understood local operations and confirmed local scene absence can remove a
residual template. The executable establishes this selection rule; native VFS
calls, the current global-switch value and live Mod overrides have not been
verified in-game.

## External encounter scope

`configs/boss-encounters.json` contains only versioned cross-location
relationships. It does not contain a full catalogue, names, regions or positions:

| Primary | Extra member | Progress | Selected meaning |
|---|---|---|---|
| Adula late `1034420800` | Early `1034500800` | `primary` | Only the late encounter completes progress |
| Lansseax late `1041520800` | Early kill `1037510800` | `any` | Either actual kill completes progress; retreat `1037510810` does not |
| Patches initial `31000800` | Later `31000850` | `primary` | The initial fight/surrender is the progress entry; the later rematch is not separate |

For `any`, a true value wins over an unknown value. All false values mean
incomplete; false plus unknown remains unknown. Each journey contributes at
most one completion. Relationships only take effect when the primary has an
event-proved completion flag in the discovered input. If a Mod removes that
primary event, its remaining member keeps its own event flag instead of
inventing the old primary's completion condition.

The plugin reads this JSON at initialization. Game-data reload does not reload
relationships or revival recipes. Missing/invalid configuration disables links
and reports an error, without a compiled-in mapping. Validation rejects duplicate,
overlapping, chained, cyclic, zero, self-linked and unknown-field definitions.
Neither names nor shared global guards merge the two Hinterland records.

Item text jobs require both NpcName and PlaceName; menu jobs require GR_MenuText.
A valid binder containing only other FMGs does not satisfy those requirements.
Base-binder fallback merges only missing namespaces and preserves already
loaded Mod namespaces, including layer-zero overrides. A parsed empty FMG is
still a present namespace, and an empty string remains a valid override.
Unavailable language or still-missing namespaces count as unavailable game
resources rather than silently reporting a fully ready catalogue.

## Revival configuration

`configs/boss-revival.json` remains external. Each key is a primary completion
flag and each value contains only the reviewed auxiliary writes. For example:

```json
{
  "10000800": [10000801, 10000802, 10002802, 10002805, 10002806],
  "1042360800": [],
  "12030850": [12030852, 12032500, 12032501, 12032502, 12032503, 12032504, 12032852, 12032856, 12032858, {"flag_id": 12032859, "value": true}, 12032860, 12032861, 12032870]
}
```

This excerpt illustrates the format; consult the distributed file for the
complete recipe. Numeric entries clear a flag; objects can set a flag true.
The 38 empty arrays deliberately authorize reviewed primary-only resets.
An absent key does not authorize revival of a newly discovered/Mod battle.
The DLL contains neither this table nor fallback copies of its entries.

At execution, the plugin refreshes context and completion values. The core
validates the confirmation's live context, a currently true completion witness
and every target address before the first write. Ordinary recipes apply auxiliary
writes in their configured order and clear the primary flag last. Repeated
identical auxiliary writes are deduplicated without changing their first
position; conflicting values reject the configuration. Each byte operation
preserves neighboring bits, but the whole recipe is not a transaction against
the concurrently running game event interpreter. Recipes are loaded when the
plugin starts; the game-data reload action does not reload this JSON.

For Lansseax's early-kill-only completion, the early kill flag becomes that
currently true witness. The operation moves the canonical late flag into the
auxiliary clears and clears the early witness last. The complete target/value
set remains the original reviewed recipe plus its implicit canonical clear;
no inferred flags are added. An alternate completion is eligible only when the
reviewed recipe already clears that flag. A missing address or changed context
rejects the whole operation before any write. Patches' later flag neither blocks
the initial recipe nor becomes an extra write in it.

The initial configuration preserves all 207 recipes and 955 auxiliary writes
from the reviewed C++ source `src/boss/data/gen/revival.json` at commit
`afab7e14db2b62ae0bede980d5d0c96634321350`. Ten auxiliary writes restore true.
The reproducible exporter validates IDs, duplicates and writes to other primary
flags, and preserves the empty recipes:

```powershell
python tools/export_boss_revival.py --repository <repository-with-C++-history> --check
```

Normal builds consume the committed external JSON and do not need that history.
The extra arena, quest-entry and phase relationships are authored event
semantics; PARAM does not encode them. Preserving them externally is necessary
for shared festival arenas, Patches, Fortissax's dream, Dane's duel, Rennala's
summons and similar cases. Recipes reconstruct battle state; they do not rewind
all quest/reward/world progress. They require travel or area reload afterwards
and still require live-game verification.

## Reproducible evidence

Regular tests cover common-call patches, native completion scope, shared flags,
multi-bar battles, field variants, ambiguous flag evidence and malformed table
ranges. The optional resource probe runs the production event reader:

```powershell
rustc --edition 2024 tools/boss_event_probe.rs -O -o target/boss-event-probe.exe
./target/boss-event-probe.exe <original-decompressed-EMEVD-directory>
```

The complete original-resource probe reads 588 resources and finds 349 enabled
named bar instructions. Comparing each `(map, variant, entity, name, slot,
source event, instruction)` tuple with the pinned original Python EMEVD reader
produced **zero differences**. This comparison covers native parameter
expansion and ceremony variants; it does not compare render performance.

The optional catalogue check uses six real PARAMs and real current-language
item/menu BNDs from an explicit local export. It follows the **production
PARAM-derived resource plan**, rather than passing every archived EMEVD directly
to the catalogue:

```powershell
$env:EROVERLAY_BOSS_PARAM_DIR='<export-directory>'
$env:EROVERLAY_BOSS_EVENTS_DIR='<original-decompressed-EMEVD-directory>'
cargo test -p eroverlay-boss archived_catalog_uses_param_map_plan_and_current_game_text --locked -- --ignored --nocapture
```

The input directory contains `GameAreaParam.param`, `BonfireWarpParam.param`,
`WorldMapPointParam.param`, `BonfireWarpSubCategoryParam.param`,
`BonfireWarpTabParam.param`, `MapDefaultInfoParam.param`, `item_dlc02.bnd` and
`menu_dlc02.bnd`. Original resource bytes are not distributed or required by
ordinary tests/builds.

That original static check requests 1,075 candidate event paths and loads 555
EMEVDs. It produces **211 static candidates and 347 named-bar instructions**,
with no unresolved completion flags or missing Boss names in that input. The
four candidates beyond the old 207 records are the three early/later routes
listed above and the residual `1038540800` template. This remains historical
pre-scope evidence, not a claim of four additional real fights.

The selected-scope audit additionally loads the external relationship JSON and
three explicit decompressed MSBs: `m31_00_00_00.msb` (465,336 B),
`m60_38_54_00.msb` (1,238,752 B) and `m61_49_44_00.msb` (2,826,656 B), totaling
**4,530,744 B**. Production scene filtering removes the residual template's two
bar instructions, leaving **345 instructions and 207 progress entries**. All old
207 entries map one-to-one, all named members are present, no archived display
instructions are missing, and the two Hinterland records remain independent.
The explicit audit transport feeds each scene once; it does not establish both
possible DLC2 aliases' native availability. Without scene input it retains 208 candidates
instead of forcing the count to 207.

Reproduce the selected audit with independently exported assets:

```powershell
python tools/audit_boss_catalog.py --reference <pinned-C++-export> --params <PARAM-and-BND-directory> --events <decompressed-EMEVD-directory> --scope selected --scenes <decompressed-MSB-directory> --output target/boss-207-delivery-audit
```

The final output `target/boss-207-delivery-audit` contains the complete
member/event/defeat evidence, relation configuration, explicit scene inputs,
excluded-display proofs and per-candidate scene checks. The wrapper fingerprints
all actual inputs. Its default `--scope static` preserves the original static
audit. The full-directory member comparison uses 588 archived scripts against
the 555-script production plan, allowing only explicitly recorded identical
field-tile LOD aliases.

The grouping is generated from PARAM data, not the old 32 manual regions.
Nearest same-map graces provide approximate locality and subcategory membership;
maps with no suitable grace retain their map identifier. Consequently some
place/group labels differ from the original authored names. A nearby grace can
belong to another height/route, so these labels are not navigation instructions.
When no exact GameArea completion-flag match exists, the local map origin is
used for this approximation instead of an unverified NPC position.
This input set produces 95 groups, including explicit map-identifier fallbacks.

These checks establish parsing and catalogue contracts. They do not inject or
start a game, prove arena revival behavior, measure complete in-game frame
performance, or establish that every game/Mod version has identical resources.
