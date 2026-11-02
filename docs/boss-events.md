# Boss event discovery and revival evidence

The catalogue is discovered from the game's enabled **Display Boss Health Bar**
instructions, not from a bundled list of NPCs or from an NPC stat/category flag.
`GameAreaParam` supplies useful defeat, map and position facts, but a row there
does not establish that the character has a named bottom-of-screen health bar.
The PARAM and current-language text readers are described in
[Boss data sources](boss-data-sources.md).

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
The catalogue treats their flags as encounter completion: surrender or
withdrawal can complete a named-bar encounter without killing its character.
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
parsed EMEVD source. Ordinary absent optional map versions are not reported as
on-screen errors. Invalid resource metadata and unavailable required links are
tracked separately. Native file ownership still ends through `release_file`.

The finished catalogue is immutable. Opening the full-screen map, changing
player/save context or temporarily unavailable game pointers does not restart
catalogue IO. A complete nonzero PARAM-pointer change or the settings action to
reload game data starts a fresh catalogue and releases obsolete pending jobs.
Rendering only reads the published catalogue and completion-state snapshot.

Known progress entries merge by their proved completion flag, preserving
multiple phase names without counting them twice. Unknown entries carry flag
zero, display `?`, do not contribute to progress totals and cannot revive.
Unknown bars merge only for the same map, runtime ceremony variant and exact
entity. Reusing an entity number in another map or variant does not join those
encounters. Duplicate bars for that same scoped entity can still retain their
multiple names.
Missing native text is shown as `NpcName#<id>`; no offline language catalogue is
bundled. The archived production-input check currently has no unknown entries
or missing Boss names.

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

At execution, the core validates the confirmation's live context, the primary
flag and every target address before the first write. It then applies auxiliary
writes in their configured order and clears the primary flag last. Repeated
identical auxiliary writes are deduplicated without changing their first
position; conflicting values reject the configuration. Each byte operation
preserves neighboring bits, but the whole recipe is not a transaction against
the concurrently running game event interpreter. Recipes are loaded when the
plugin starts; the game-data reload action does not reload this JSON.

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

The checked input set requests 1,075 distinct paths and loads 555 EMEVDs. It
produces **211 battles, 211 proved completion flags, zero unknowns and zero
missing Boss names**. All 207 old entries remain covered. Four additional real
bar encounters were outside the old manual list: the later Patches encounter
(`31000850`), Adula's earlier encounter (`1034500800`), Lansseax's earlier
encounter (`1037510800`) and the first Mt. Gelmir Fallingstar Beast
(`1038540800`). These count named-bar encounters rather than distinct NPC
species or exclusively lethal victories. Their absent revival keys leave them
read-only.

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
