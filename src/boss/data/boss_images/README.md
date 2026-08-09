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

The English boss names in `../engus/bosses.json` follow the same wiki, so names and portraits
agree. The exception is the four Crystalian fights, which are identified by location and count
instead, because the wiki's names for them do not distinguish the encounters clearly.

### Limgrave

| File | Boss | Location |
|---|---|---|
| `18000850.jpg` | Soldier of Godrick | Stranded Graveyard |
| `1042360800.jpg` | Tree Sentinel | Church of Elleh |
| `31150800.jpg` | Demi-Human Chiefs | Coastal Cave |
| `30020800.jpg` | Erdtree Burial Watchdog (Limgrave) | Stormfoot Catacombs |
| `31030800.jpg` | Beastman of Farum Azula (Solo) | Groveside Cave |
| `32010800.jpg` | Stonedigger Troll (Limgrave) | Limgrave Tunnels |
| `1044360800.jpg` | Mad Pumpkin Head (Limgrave) | Waypoint Ruins |
| `1044350800.jpg` | Bloodhound Knight Darriwil | Forlorn Hound Evergaol |
| `31000800.jpg` | Patches | Murkwater Cave |
| `30040800.jpg` | Grave Warden Duelist (Limgrave) | Murkwater Catacombs |
| `31170800.jpg` | Guardian Golem | Highroad Cave |
| `30110800.jpg` | Black Knife Assassin (Limgrave) | Deathtouched Catacombs |
| `18000800.jpg` | Ulcerated Tree Spirit (Limgrave) | Fringefolk Hero's Grave |
| `1043360800.jpg` | Flying Dragon Agheel | Agheel Lake |
| `1045390800.jpg` | Tibia Mariner (Limgrave) | Summonwater Village |
| `1042370800.jpg` | Crucible Knight (Limgrave) | Stormhill Evergaol |
| `1043370800.jpg` | Night's Cavalry (Limgrave) |  |
| `1042380800.jpg` | Deathbird (Limgrave) |  |
| `1042380850.jpg` | Bell Bearing Hunter (Limgrave) | Warmaster's Shack |

### Weeping Peninsula

| File | Boss | Location |
|---|---|---|
| `31010800.jpg` | Runebear | Earthbore Cave |
| `32000800.jpg` | Scaly Misbegotten | Morne Tunnel |
| `30010800.jpg` | Erdtree Burial Watchdog (Weeping) | Impaler's Catacombs |
| `30000800.jpg` | Cemetery Shade (Weeping) | Tombsward Catacombs |
| `31020800.jpg` | Miranda the Blighted Bloom | Tombsward Cave |
| `1043330800.jpg` | Erdtree Avatar (Weeping) | Minor Erdtree |
| `1042330800.jpg` | Ancient Hero of Zamor (Weeping) | Weeping Evergaol |
| `1044320850.jpg` | Night's Cavalry (Weeping) | Castle Morne Rampart |
| `1044320800.jpg` | Deathbird (Weeping) |  |
| `1043300800.jpg` | Leonine Misbegotten | Morne Moangrave |

### Stormveil Castle

| File | Boss | Location |
|---|---|---|
| `10000850.jpg` | Margit, the Fell Omen | Stormveil Castle |
| `10000800.jpg` | Godrick the Grafted | Stormveil Castle |

### Liurnia of the Lakes

| File | Boss | Location |
|---|---|---|
| `31040800.jpg` | Cleanrot Knight (Liurnia) | Stillwater Cave |
| `31050800.jpg` | Bloodhound Knight (Liurnia) | Lakeside Crystal Cave |
| `30060800.jpg` | Erdtree Burial Watchdog (Liurnia) | Cliffbottom Catacombs |
| `31060800.jpg` | Crystalian Duo (Academy Crystal Cave) | Academy Crystal Cave |
| `32020800.jpg` | Crystalian (Raya Lucaria Crystal Tunnel) | Raya Lucaria Crystal Tunnel |
| `30050800.jpg` | Cemetery Shade (Liurnia) | Black Knife Catacombs |
| `30050850.jpg` | Black Knife Assassin (Liurnia) | Black Knife Catacombs |
| `30030800.jpg` | Spirit-Caller Snail (Liurnia) | Road's End Catacombs |
| `10010800.jpg` | Grafted Scion (Liurnia, Four Belfries) | Chapel of Anticipation |
| `1034450800.jpg` | Glintstone Dragon Smarag | Temple Quarter |
| `1035420800.jpg` | Omenkiller | Village of the Albinaurics |
| `1034480800.jpg` | Royal Revenant | Kingsrealm Ruins |
| `1039440800.jpg` | Tibia Mariner (Liurnia) |  |
| `1033430800.jpg` | Erdtree Avatar (Liurnia, Southwest) | Converted Tower |
| `1038480800.jpg` | Erdtree Avatar (Liurnia, Northeast) | Mausoleum Compound |
| `1038410800.jpg` | Adan, Thief of Fire | Malefactor's Evergaol |
| `1033450800.jpg` | Bols, Carian Knight | Cuckoo's Evergaol |
| `1036500800.jpg` | Onyx Lord (Liurnia) | Royal Grave Evergaol |
| `1039430800.jpg` | Night's Cavalry (Liurnia, South) | Gate Town Bridge |
| `1036480800.jpg` | Night's Cavalry (Liurnia, North) | Bellum Church |
| `1037420800.jpg` | Deathbird (Liurnia) | Scenic Isle |
| `1036450800.jpg` | Death Rite Bird (Liurnia) | Gate Town North |
| `1037460800.jpg` | Bell Bearing Hunter (Liurnia) | Church of Vows |
| `1035500800.jpg` | Royal Knight Loretta | Caria Manor |
| `39200800.jpg` | Magma Wyrm Makar | Ruin-Strewn Precipice |

### Moonlight Altar

| File | Boss | Location |
|---|---|---|
| `1034420800.jpg` | Glintstone Dragon Adula | Moonlight Altar |
| `1033420800.jpg` | Alecto, Black Knife Ringleader | Ringleader's Evergaol |

### Academy of Raya Lucaria

| File | Boss | Location |
|---|---|---|
| `14000850.jpg` | Red Wolf of Radagon |  |
| `14000800.jpg` | Rennala, Queen of the Full Moon |  |

### Caelid

| File | Boss | Location |
|---|---|---|
| `32070800.jpg` | Magma Wyrm (Caelid) | Gael Tunnel |
| `30140800.jpg` | Erdtree Burial Watchdog (Gelmir) | Minor Erdtree Catacombs |
| `31210800.jpg` | Frenzied Duelist (Caelid) | Gaol Cave |
| `30150800.jpg` | Cemetery Shade (Caelid) | Caelid Catacombs |
| `32080800.jpg` | Fallingstar Beast (Caelid) | Sellia Crystal Tunnel |
| `31110800.jpg` | Putrid Crystalian Trio (Sellia Hideaway) | Sellia Hideaway |
| `1048400800.jpg` | Mad Pumpkin Heads (Caelid) | Caelem Ruins |
| `1048370800.jpg` | Decaying Ekzykes | Church of the Plague |
| `1049390800.jpg` | Nox Swordstress & Nox Priest | Sellia, Town of Sorcery |
| `1049380800.jpg` | Commander O'Neil | Swamp of Aeonia |
| `1047400800.jpg` | Putrid Avatar (Caelid) | Minor Erdtree |
| `1049370800.jpg` | Night's Cavalry (Caelid) | Caelid Highway South |
| `1049370850.jpg` | Death Rite Bird (Caelid) | Southern Aeonia Swamp Bank |
| `1051360800.jpg` | Crucible Knight / Misbegotten Warrior | Redmane Castle |
| `1252380800.jpg` | Starscourge Radahn | Wailing Dunes |

### Greyoll's Dragonbarrow

| File | Boss | Location |
|---|---|---|
| `31200800.jpg` | Cleanrot Knight (Caelid) | Abandoned Cave |
| `31100800.jpg` | Beastman of Farum Azula (Duo) | Dragonbarrow Cave |
| `30160800.jpg` | Putrid Tree Spirit (Dragonbarrow) | War-Dead Catacombs |
| `34130800.jpg` | Godskin Apostle (Dragonbarrow) | Divine Tower of Caelid |
| `1051430800.jpg` | Black Blade Kindred (Bestial Sanctum) | Bestial Sanctum |
| `1052410800.jpg` | Flying Dragon Greyll | Greyoll's Dragonbarrow |
| `1051400800.jpg` | Putrid Avatar (Dragonbarrow) | Minor Erdtree |
| `1049390850.jpg` | Battlemage Hugues | Sellia Evergaol |
| `1052410850.jpg` | Night's Cavalry (Dragonbarrow) | Lenne's Rise |
| `1048410800.jpg` | Bell Bearing Hunter (Dragonbarrow) | Isolated Merchant's Shack |

### Altus Plateau

| File | Boss | Location |
|---|---|---|
| `31190850.jpg` | Necromancer Garris | Sage's Cave |
| `31190800.jpg` | Black Knife Assassin (Sage's Cave) | Sage's Cave |
| `32040800.jpg` | Stonedigger Troll (Altus) | Old Altus Tunnel |
| `32050800.jpg` | Crystalian Duo (Altus Tunnel) | Altus Tunnel |
| `1040520800.jpg` | Black Knife Assassin (Sainted Hero's Grave) | Sainted Hero's Grave |
| `30080800.jpg` | Ancient Hero of Zamor (Altus) | Sainted Hero's Grave |
| `31180800.jpg` | Omenkiller / Miranda | Perfumer's Grotto |
| `30120800.jpg` | Perfumer Tricia / Misbegotten Warrior | Unsightly Catacombs |
| `30070800.jpg` | Erdtree Burial Watchdog (Caelid) | Wyndham Catacombs |
| `1041520800.jpg` | Ancient Dragon Lansseax | Rampartside Path |
| `1038510800.jpg` | Demi-Human Queen Gilika | Lux Ruins |
| `1040530800.jpg` | Sanguine Noble | Writheblood Ruins |
| `1042550800.jpg` | Godskin Apostle (Altus) | Dominula, Windmill Village |
| `1041500800.jpg` | Fallingstar Beast (Altus) | Starfall Crater |
| `1041510800.jpg` | Tree Sentinel (Duo) |  |
| `1038520800.jpg` | Tibia Mariner (Altus) | Wyndham Ruins |
| `1041530800.jpg` | Wormface | Minor Erdtree |
| `1039500800.jpg` | Godefroy the Grafted | Golden Lineage Evergaol |
| `1039510800.jpg` | Night's Cavalry (Altus) | Altus Highway Junction |
| `1039540800.jpg` | Elemer of the Briar | The Shaded Castle |

### Capital Outskirts

| File | Boss | Location |
|---|---|---|
| `34120800.jpg` | Onyx Lord (Leyndell) | Sealed Tunnel |
| `30130800.jpg` | Grave Warden Duelist (Leyndell) | Auriza Side Tomb |
| `30100800.jpg` | Crucible Knight Ordovis | Auriza Hero's Grave |
| `1045520800.jpg` | Draconic Tree Sentinel | Capital Rampart |
| `1044530800.jpg` | Deathbird (Leyndell) |  |
| `1043530800.jpg` | Bell Bearing Hunter (Leyndell) | Hermit Merchant's Shack |

### Mt. Gelmir

| File | Boss | Location |
|---|---|---|
| `31090800.jpg` | Demi-Human Queen Margot | Volcano Cave |
| `31070800.jpg` | Kindred of Rot (Duo) | Seethewater Cave |
| `30090800.jpg` | Red Wolf of the Champion | Gelmir Hero's Grave |
| `1035530800.jpg` | Magma Wyrm (Gelmir) | Fort Laiedd |
| `1036540800.jpg` | Full-Grown Fallingstar Beast | Ninth Mt. Gelmir Campsite |
| `1037530800.jpg` | Demi-Human Queen Maggie | Hermit Village |
| `1037540810.jpg` | Ulcerated Tree Spirit (Gelmir) | Minor Erdtree |

### Volcano Manor

| File | Boss | Location |
|---|---|---|
| `16000860.jpg` | Abductor Virgins (Duo) | Subterranean Inquisition Chamber |
| `16000850.jpg` | Godskin Noble (Gelmir) | Temple of Eiglay |
| `16000800.jpg` | Rykard, Lord of Blasphemy | Volcano Manor |

### Leyndell, Royal Capital

| File | Boss | Location |
|---|---|---|
| `35000850.jpg` | Esgar, Priest of Blood | Leyndell Catacombs |
| `34140850.jpg` | Fell Twins | Divine Tower of East Altus |
| `35000800.jpg` | Mohg, the Omen | Cathedral of the Forsaken |
| `11000850.jpg` | Godfrey, First Elden Lord (Golden Shade) | Erdtree Sanctuary |
| `11000800.jpg` | Morgott, the Omen King | Elden Throne |

### Forbidden Lands

| File | Boss | Location |
|---|---|---|
| `1048510800.jpg` | Night's Cavalry (Consecrated) |  |
| `1049520800.jpg` | Black Blade Kindred (Forbidden Lands) |  |

### Mountaintops of the Giants

| File | Boss | Location |
|---|---|---|
| `30170800.jpg` | Ancient Hero of Zamor (Mountaintops) | Giant-Conquering Hero's Grave |
| `30180800.jpg` | Ulcerated Tree Spirit (Mountaintops) | Giants' Mountaintop Catacombs |
| `31220800.jpg` | Spirit-Caller Snail (Mountaintops) | Spiritcaller Cave |
| `1254560800.jpg` | Borealis the Freezing Fog | Freezing Lake |
| `1052560800.jpg` | Erdtree Avatar (Mountaintops) | Minor Erdtree |
| `1053560800.jpg` | Vyke, Knight of the Roundtable | Lord Contender's Evergaol |
| `1050570800.jpg` | Death Rite Bird (Mountaintops) |  |
| `1051570800.jpg` | Commander Niall | Castle Sol |
| `1052520800.jpg` | Fire Giant | Flame Peak |

### Crumbling Farum Azula

| File | Boss | Location |
|---|---|---|
| `13000850.jpg` | Godskin Duo | Dragon Temple Altar |
| `13000830.jpg` | Dragonlord Placidusax | Beside the Great Bridge |
| `13000800.jpg` | Beast Clergyman / Maliketh, the Black Blade |  |

### Consecrated Snowfield

| File | Boss | Location |
|---|---|---|
| `30200800.jpg` | Stray Mimic Tear | Hidden Path to the Haligtree |
| `30190800.jpg` | Putrid Grave Warden Duelist (Consecrated) | Consecrated Snowfield Catacombs |
| `32110800.jpg` | Astel, Stars of Darkness | Yelough Anix Tunnel |
| `31120800.jpg` | Misbegotten Crusader | Cave of the Forlorn |
| `1050560800.jpg` | Great Wyrm Theodorix |  |
| `1050570850.jpg` | Putrid Avatar (Consecrated) | Minor Erdtree |
| `1248550800.jpg` | Night's Cavalry (Forbidden Lands) | Inner Consecrated Snowfield |
| `1048570800.jpg` | Death Rite Bird (Consecrated) |  |

### Miquella's Haligtree

| File | Boss | Location |
|---|---|---|
| `15000850.jpg` | Loretta, Knight of the Haligtree |  |
| `15000800.jpg` | Malenia, Blade of Miquella |  |

### Siofra River

| File | Boss | Location |
|---|---|---|
| `12020830.jpg` | Dragonkin Soldier (Siofra) | Siofra River Bank |
| `12080800.jpg` | Ancestor Spirit | Siofra River Bank |
| `12020850.jpg` | Mimic Tear |  |
| `12090800.jpg` | Regal Ancestor Spirit | Hallowhorn Grounds |
| `12020800.jpg` | Valiant Gargoyles | Siofra Aqueduct |

### Mohgwyn Dynasty Mausoleum

| File | Boss | Location |
|---|---|---|
| `12050800.jpg` | Mohg, Lord of Blood |  |

### Ainsel River

| File | Boss | Location |
|---|---|---|
| `12010800.jpg` | Dragonkin Soldier of Nokstella |  |
| `12010850.jpg` | Dragonkin Soldier (Lake of Rot) |  |
| `12040800.jpg` | Astel, Naturalborn of the Void | Grand Cloister |

### Deeproot Depths

| File | Boss | Location |
|---|---|---|
| `12030390.jpg` | Crucible Knight Siluria |  |
| `12030800.jpg` | Fia's Champions |  |
| `12030850.jpg` | Lichdragon Fortissax |  |

### Leyndell, Ashen Capital

| File | Boss | Location |
|---|---|---|
| `11050850.jpg` | Sir Gideon Ofnir, the All-Knowing | Erdtree Sanctuary |
| `11050800.jpg` | Godfrey, First Elden Lord / Hoarah Loux | Elden Throne |
| `19000800.jpg` | Radagon of the Golden Order | Fractured Marika |

