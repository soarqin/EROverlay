# Boss portraits

165 base game boss portraits, named after the boss flag id: `<flag_id>.jpg`.
Only base game bosses can be drawn in a Random Run, so only these 165 files are ever used.

The overlay looks for `<flag_id>.png` first and falls back to `<flag_id>.jpg`, so dropping a
PNG in here overrides the shipped image for that boss. Any image the overlay cannot find is
simply skipped, so a partial set works fine. Flag ids are language independent, so one set of
images covers every language.

Images are scaled to 256px on the long edge and saved as baseline JPEG. Baseline matters:
the overlay decodes with stb_image, which does not support progressive JPEG or WebP.

## Source

Sourced from the Elden Ring Fextralife wiki, via the boss name to image mapping published by
<https://mikshuli.github.io/er-bosslist-rando/>. The wiki is the copyright holder for these
images; they are bundled here for convenience.

## Name mapping

The "Wiki name" column is filled in only where the wiki's name differs from the overlay's boss
name, which happens because the two lists group and disambiguate multi-boss fights differently.
The mapping below is what was used to assign each image to a flag id.

### Limgrave

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `18000850.jpg` | Soldier of Godrick | Stranded Graveyard |  |
| `1042360800.jpg` | Tree Sentinel | Church of Elleh |  |
| `31150800.jpg` | Demi-Human Chief(x2) | Coastal Cave | Demi-Human Chiefs |
| `30020800.jpg` | Erdtree Burial Watchdog | Stormfoot Catacombs | Erdtree Burial Watchdog (Limgrave) |
| `31030800.jpg` | Beastman of Farum Azula | Groveside Cave | Beastman of Farum Azula (Solo) |
| `32010800.jpg` | Stonedigger Troll | Limgrave Tunnels | Stonedigger Troll (Limgrave) |
| `1044360800.jpg` | Mad Pumpkin Head | Waypoint Ruins | Mad Pumpkin Head (Limgrave) |
| `1044350800.jpg` | Bloodhound Knight Darriwil | Forlorn Hound Evergaol |  |
| `31000800.jpg` | Patches | Murkwater Cave |  |
| `30040800.jpg` | Grave Warden Duelist | Murkwater Catacombs | Grave Warden Duelist (Limgrave) |
| `31170800.jpg` | Guardian Golem | Highroad Cave |  |
| `30110800.jpg` | Black Knife Assassin | Deathtouched Catacombs | Black Knife Assassin (Limgrave) |
| `18000800.jpg` | Ulcerated Tree Spirit | Fringefolk Hero's Grave | Ulcerated Tree Spirit (Limgrave) |
| `1043360800.jpg` | Flying Dragon Agheel | Agheel Lake |  |
| `1045390800.jpg` | Tibia Mariner | Summonwater Village | Tibia Mariner (Limgrave) |
| `1042370800.jpg` | Crucible Knight | Stormhill Evergaol | Crucible Knight (Limgrave) |
| `1043370800.jpg` | Night's Cavalry |  | Night's Cavalry (Limgrave) |
| `1042380800.jpg` | Deathbird |  | Deathbird (Limgrave) |
| `1042380850.jpg` | Bell Bearing Hunter | Warmaster's Shack | Bell Bearing Hunter (Limgrave) |

### Weeping Peninsula

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `31010800.jpg` | Runebear | Earthbore Cave |  |
| `32000800.jpg` | Scaly Misbegotten | Morne Tunnel |  |
| `30010800.jpg` | Erdtree Burial Watchdog | Impaler's Catacombs | Erdtree Burial Watchdog (Weeping) |
| `30000800.jpg` | Cemetery Shade | Tombsward Catacombs | Cemetery Shade (Weeping) |
| `31020800.jpg` | Miranda the Blighted Bloom | Tombsward Cave |  |
| `1043330800.jpg` | Erdtree Avatar | Minor Erdtree | Erdtree Avatar (Weeping) |
| `1042330800.jpg` | Ancient Hero of Zamor | Weeping Evergaol | Ancient Hero of Zamor (Weeping) |
| `1044320850.jpg` | Night's Cavalry | Castle Morne Rampart | Night's Cavalry (Weeping) |
| `1044320800.jpg` | Deathbird |  | Deathbird (Weeping) |
| `1043300800.jpg` | Leonine Misbegotten | Morne Moangrave |  |

### Stormveil Castle

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `10000850.jpg` | Margit, the Fell Omen | Stormveil Castle |  |
| `10000800.jpg` | Godrick the Grafted | Stormveil Castle |  |

### Liurnia of the Lakes

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `31040800.jpg` | Cleanrot Knight | Stillwater Cave | Cleanrot Knight (Liurnia) |
| `31050800.jpg` | Bloodhound Knight | Lakeside Crystal Cave | Bloodhound Knight (Liurnia) |
| `30060800.jpg` | Erdtree Burial Watchdog | Cliffbottom Catacombs | Erdtree Burial Watchdog (Liurnia) |
| `31060800.jpg` | Crystalian (Staff) & Crystalian (Spear) | Academy Crystal Cave | Crystalian Spear & Crystalian Staff (Liurnia) |
| `32020800.jpg` | Crystalian (Ringblade) | Raya Lucaria Crystal Tunnel | Crystalian Ringblade (Liurnia) |
| `30050800.jpg` | Cemetery Shade | Black Knife Catacombs | Cemetery Shade (Liurnia) |
| `30050850.jpg` | Black Knife Assassin | Black Knife Catacombs | Black Knife Assassin (Liurnia) |
| `30030800.jpg` | Spiritcaller Snail | Road's End Catacombs | Spirit-Caller Snail (Liurnia) |
| `10010800.jpg` | Grafted Scion | Chapel of Anticipation | Grafted Scion (Liurnia, Four Belfries) |
| `1034450800.jpg` | Glintstone Dragon Smarag | Temple Quarter |  |
| `1035420800.jpg` | Omenkiller | Village of the Albinaurics |  |
| `1034480800.jpg` | Royal Revenant | Kingsrealm Ruins |  |
| `1039440800.jpg` | Tibia Mariner |  | Tibia Mariner (Liurnia) |
| `1033430800.jpg` | Erdtree Avatar | Converted Tower | Erdtree Avatar (Liurnia, Southwest) |
| `1038480800.jpg` | Erdtree Avatar | Mausoleum Compound | Erdtree Avatar (Liurnia, Northeast) |
| `1038410800.jpg` | Adan, Thief of Fire | Malefactor's Evergaol |  |
| `1033450800.jpg` | Bols, Carian Knight | Cuckoo's Evergaol |  |
| `1036500800.jpg` | Onyx Lord | Royal Grave Evergaol | Onyx Lord (Liurnia) |
| `1039430800.jpg` | Night's Cavalry | Gate Town Bridge | Night's Cavalry (Liurnia, South) |
| `1036480800.jpg` | Night's Cavalry | Bellum Church | Night's Cavalry (Liurnia, North) |
| `1037420800.jpg` | Deathbird | Scenic Isle | Deathbird (Liurnia) |
| `1036450800.jpg` | Death Rite Bird | Gate Town North | Death Rite Bird (Liurnia) |
| `1037460800.jpg` | Bell Bearing Hunter | Church of Vows | Bell Bearing Hunter (Liurnia) |
| `1035500800.jpg` | Royal Knight Loretta | Caria Manor |  |
| `39200800.jpg` | Magma Wyrm Makar | Ruin-Strewn Precipice |  |

### Moonlight Altar

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `1034420800.jpg` | Glintstone Dragon Adula | Moonlight Altar |  |
| `1033420800.jpg` | Alecto, Black Knife Ringleader | Ringleader's Evergaol |  |

### Academy of Raya Lucaria

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `14000850.jpg` | Red Wolf of Radagon |  |  |
| `14000800.jpg` | Rennala, Queen of the Full Moon |  |  |

### Caelid

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `32070800.jpg` | Magma Wyrm | Gael Tunnel | Magma Wyrm (Caelid) |
| `30140800.jpg` | Erdtree Burial Watchdog (Sword) & Erdtree Burial Watchdog (Scepter) | Minor Erdtree Catacombs | Erdtree Burial Watchdog (Gelmir) |
| `31210800.jpg` | Frenzied Duelist | Gaol Cave | Frenzied Duelist (Caelid) |
| `30150800.jpg` | Cemetery Shade | Caelid Catacombs | Cemetery Shade (Caelid) |
| `32080800.jpg` | Fallingstar Beast | Sellia Crystal Tunnel | Fallingstar Beast (Caelid) |
| `31110800.jpg` | Putrid Crystalian (Ringblade) & Putrid Crystalian (Spear) & Putrid Crystalian (Staff) | Sellia Hideaway | Putrid Crystallian Trio |
| `1048400800.jpg` | Mad Pumpkin Head (Hammer) & Mad Pumpkin Head (Flail) | Caelem Ruins | Mad Pumpkin Heads (Caelid) |
| `1048370800.jpg` | Decaying Ekzykes | Church of the Plague |  |
| `1049390800.jpg` | Nox Monk & Nox Swordstress | Sellia, Town of Sorcery | Nox Swordstress & Nox Priest |
| `1049380800.jpg` | Commander O'Neil | Swamp of Aeonia |  |
| `1047400800.jpg` | Putrid Avatar | Minor Erdtree | Putrid Avatar (Caelid) |
| `1049370800.jpg` | Night's Cavalry | Caelid Highway South | Night's Cavalry (Caelid) |
| `1049370850.jpg` | Death Rite Bird | Southern Aeonia Swamp Bank | Death Rite Bird (Caelid) |
| `1051360800.jpg` | Crucible Knight & Misbegotten Warrior | Redmane Castle | Crucible Knight / Misbegotten Warrior |
| `1252380800.jpg` | Starscourge Radahn | Wailing Dunes |  |

### Greyoll's Dragonbarrow

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `31200800.jpg` | Cleanrot Knight (Spear) & Cleanrot Knight (Sickle) | Abandoned Cave | Cleanrot Knight (Caelid) |
| `31100800.jpg` | Beastman of Farum Azula (Cleaver) & Beastman of Farum Azula (Throwing Knife) | Dragonbarrow Cave | Beastman of Farum Azula (Duo) |
| `30160800.jpg` | Putrid Tree Spirit | War-Dead Catacombs | Putrid Tree Spirit (Dragonbarrow) |
| `34130800.jpg` | Godskin Apostle | Divine Tower of Caelid | Godskin Apostle (Dragonbarrow) |
| `1051430800.jpg` | Black Blade Kindred | Bestial Sanctum | Black Blade Kindred (Bestial Sanctum) |
| `1052410800.jpg` | Flying Dragon Greyll | Greyoll's Dragonbarrow |  |
| `1051400800.jpg` | Putrid Avatar | Minor Erdtree | Putrid Avatar (Dragonbarrow) |
| `1049390850.jpg` | Battlemage Hugues | Sellia Evergaol |  |
| `1052410850.jpg` | Night's Cavalry | Lenne's Rise | Night's Cavalry (Dragonbarrow) |
| `1048410800.jpg` | Bell Bearing Hunter | Isolated Merchant's Shack | Bell Bearing Hunter (Dragonbarrow) |

### Altus Plateau

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `31190850.jpg` | Necromancer Garris | Sage's Cave |  |
| `31190800.jpg` | Black Knife Assassin | Sage's Cave | Black Knife Assassin (Sage's Cave) |
| `32040800.jpg` | Stonedigger Troll | Old Altus Tunnel | Stonedigger Troll (Altus) |
| `32050800.jpg` | Crystalian (Spear) & Crystalian (Ringblade) | Altus Tunnel | Crystalian Spear & Crystalian Ringblade (Altus) |
| `1040520800.jpg` | Black Knife Assassin | Sainted Hero's Grave | Black Knife Assassin (Sainted Hero's Grave) |
| `30080800.jpg` | Ancient Hero of Zamor | Sainted Hero's Grave | Ancient Hero of Zamor (Altus) |
| `31180800.jpg` | Omenkiller & Miranda the Blighted Bloom | Perfumer's Grotto | Omenkiller / Miranda |
| `30120800.jpg` | Perfumer Tricia & Misbegotten Warrior | Unsightly Catacombs | Perfumer Tricia / Misbegotten Warrior |
| `30070800.jpg` | Erdtree Burial Watchdog | Wyndham Catacombs | Erdtree Burial Watchdog (Caelid) |
| `1041520800.jpg` | Ancient Dragon Lansseax | Rampartside Path |  |
| `1038510800.jpg` | Demi-Human Queen Gilika | Lux Ruins |  |
| `1040530800.jpg` | Sanguine Noble | Writheblood Ruins |  |
| `1042550800.jpg` | Godskin Apostle | Dominula, Windmill Village | Godskin Apostle (Altus) |
| `1041500800.jpg` | Fallingstar Beast | Starfall Crater | Fallingstar Beast (Altus) |
| `1041510800.jpg` | Tree Sentinel(x2) |  | Tree Sentinel (Duo) |
| `1038520800.jpg` | Tibia Mariner | Wyndham Ruins | Tibia Mariner (Altus) |
| `1041530800.jpg` | Wormface | Minor Erdtree |  |
| `1039500800.jpg` | Godefroy the Grafted | Golden Lineage Evergaol |  |
| `1039510800.jpg` | Night's Cavalry | Altus Highway Junction | Night's Cavalry (Altus) |
| `1039540800.jpg` | Elemer of the Briar | The Shaded Castle |  |

### Capital Outskirts

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `34120800.jpg` | Onyx Lord | Sealed Tunnel | Onyx Lord (Leyndell) |
| `30130800.jpg` | Grave Warden Duelist | Auriza Side Tomb | Grave Warden Duelist (Leyndell) |
| `30100800.jpg` | Crucible Knight & Crucible Knight Ordovis | Auriza Hero's Grave | Crucible Knight Ordovis |
| `1045520800.jpg` | Draconic Tree Sentinel | Capital Rampart |  |
| `1044530800.jpg` | Deathbird |  | Deathbird (Leyndell) |
| `1043530800.jpg` | Bell Bearing Hunter | Hermit Merchant's Shack | Bell Bearing Hunter (Leyndell) |

### Mt. Gelmir

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `31090800.jpg` | Demi-Human Queen Margot | Volcano Cave |  |
| `31070800.jpg` | Kindred of Rot(x2) | Seethewater Cave | Kindred of Rot (Duo) |
| `30090800.jpg` | Red Wolf of the Champion | Gelmir Hero's Grave |  |
| `1035530800.jpg` | Magma Wyrm | Fort Laiedd | Magma Wyrm (Gelmir) |
| `1036540800.jpg` | Full-Grown Fallingstar Beast | Ninth Mt. Gelmir Campsite |  |
| `1037530800.jpg` | Demi-Human Queen Maggie | Hermit Village |  |
| `1037540810.jpg` | Ulcerated Tree Spirit | Minor Erdtree | Ulcerated Tree Spirit (Gelmir) |

### Volcano Manor

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `16000860.jpg` | Abductor Virgin (Swinging Sickle) & Abductor Virgin (Wheel) | Subterranean Inquisition Chamber | Abductor Virgins (Duo) |
| `16000850.jpg` | Godskin Noble | Temple of Eiglay | Godskin Noble (Gelmir) |
| `16000800.jpg` | God-Devouring Serpent & Rykard, Lord of Blasphemy | Volcano Manor | Rykard, Lord of Blasphemy |

### Leyndell, Royal Capital

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `35000850.jpg` | Esgar, Priest of Blood | Leyndell Catacombs |  |
| `34140850.jpg` | Fell Twin(x2) | Divine Tower of East Altus | Fell Twins |
| `35000800.jpg` | Mohg, the Omen | Cathedral of the Forsaken |  |
| `11000850.jpg` | Godfrey, First Elden Lord | Erdtree Sanctuary | Godfrey, First Elden Lord (Golden Shade) |
| `11000800.jpg` | Morgott, the Omen King | Elden Throne |  |

### Forbidden Lands

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `1048510800.jpg` | Night's Cavalry |  | Night's Cavalry (Consecrated) |
| `1049520800.jpg` | Black Blade Kindred |  | Black Blade Kindred (Forbidden Lands) |

### Mountaintops of the Giants

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `30170800.jpg` | Ancient Hero of Zamor | Giant-Conquering Hero's Grave | Ancient Hero of Zamor (Mountaintops) |
| `30180800.jpg` | Ulcerated Tree Spirit | Giants' Mountaintop Catacombs | Ulcerated Tree Spirit (Mountaintops) |
| `31220800.jpg` | Godskin Apostle & Godskin Noble & Spiritcaller Snail | Spiritcaller Cave | Spirit-Caller Snail (Mountaintops) |
| `1254560800.jpg` | Borealis the Freezing Fog | Freezing Lake |  |
| `1052560800.jpg` | Erdtree Avatar | Minor Erdtree | Erdtree Avatar (Mountaintops) |
| `1053560800.jpg` | Roundtable Knight Vyke | Lord Contender's Evergaol | Vyke, Knight of the Roundtable |
| `1050570800.jpg` | Death Rite Bird |  | Death Rite Bird (Mountaintops) |
| `1051570800.jpg` | Commander Niall | Castle Sol |  |
| `1052520800.jpg` | Fire Giant | Flame Peak |  |

### Crumbling Farum Azula

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `13000850.jpg` | Godskin Duo | Dragon Temple Altar |  |
| `13000830.jpg` | Dragonlord Placidusax | Beside the Great Bridge |  |
| `13000800.jpg` | Maliketh, the Black Blade |  | Beast Clergyman / Maliketh, the Black Blade |

### Consecrated Snowfield

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `30200800.jpg` | Stray Mimic Tear | Hidden Path to the Haligtree |  |
| `30190800.jpg` | Putrid Grave Warden Duelist | Consecrated Snowfield Catacombs | Putrid Grave Warden Duelist (Consecrated) |
| `32110800.jpg` | Astel, Stars of Darkness | Yelough Anix Tunnel |  |
| `31120800.jpg` | Misbegotten Crusader | Cave of the Forlorn |  |
| `1050560800.jpg` | Great Wyrm Theodorix |  |  |
| `1050570850.jpg` | Putrid Avatar | Minor Erdtree | Putrid Avatar (Consecrated) |
| `1248550800.jpg` | Night's Cavalry (Glaive) & Night's Cavalry (Flail) | Inner Consecrated Snowfield | Night's Cavalry (Forbidden Lands) |
| `1048570800.jpg` | Death Rite Bird |  | Death Rite Bird (Consecrated) |

### Miquella's Haligtree

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `15000850.jpg` | Loretta, Knight of the Haligtree |  |  |
| `15000800.jpg` | Malenia, Blade of Miquella & Malenia, Goddess of Rot |  | Malenia, Blade of Miquella |

### Siofra River

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `12020830.jpg` | Dragonkin Soldier | Siofra River Bank | Dragonkin Soldier (Siofra) |
| `12080800.jpg` | Ancestor Spirit | Siofra River Bank |  |
| `12020850.jpg` | Mimic Tear |  |  |
| `12090800.jpg` | Regal Ancestor Spirit | Hallowhorn Grounds |  |
| `12020800.jpg` | Valiant Gargoyle & Valiant Gargoyle (Twinblade) | Siofra Aqueduct | Valiant Gargoyles |

### Mohgwyn Dynasty Mausoleum

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `12050800.jpg` | Mohg, Lord of Blood |  |  |

### Ainsel River

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `12010800.jpg` | Dragonkin Soldier of Nokstella |  |  |
| `12010850.jpg` | Dragonkin Soldier |  | Dragonkin Soldier (Lake of Rot) |
| `12040800.jpg` | Astel, Naturalborn of the Void | Grand Cloister |  |

### Deeproot Depths

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `12030390.jpg` | Crucible Knight Siluria |  |  |
| `12030800.jpg` | Fia's Champion & Sorcerer Rogier & Lionel the Lionhearted |  | Fia's Champions |
| `12030850.jpg` | Lichdragon Fortissax |  |  |

### Leyndell, Ashen Capital

| File | Boss | Location | Wiki name |
|---|---|---|---|
| `11050850.jpg` | Sir Gideon Ofnir, the All-Knowing | Erdtree Sanctuary |  |
| `11050800.jpg` | Godfrey, First Elden Lord | Elden Throne | Godfrey, First Elden Lord / Hoarah Loux |
| `19000800.jpg` | Radagon of the Golden Order & Elden Beast | Fractured Marika | Radagon of the Golden Order |

