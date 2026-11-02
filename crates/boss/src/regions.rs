//! Region-level presentation relations. These contain no Boss flags or names.
//! Titles are PlaceName FMG IDs; play-region prefixes match the original broad
//! sections, not the unrelated packed map-block ID used by the Minimap.
#[derive(Clone, Copy)]
pub struct Region {
    pub id: u32,
    pub prefixes: &'static [u32],
}
macro_rules! region {
    ($id:literal; $($prefix:literal),+ $(,)?) => {
        Region { id: $id, prefixes: &[$($prefix),+] }
    };
}
pub const REGIONS: &[Region] = &[
    region!(61000; 1800,3002,3004,3011,3100,3103,3115,3117,3201,3410,6100,6101),
    region!(61002; 3000,3001,3101,3102,3200,6102),
    region!(10000; 1000),
    region!(62000; 3003,3005,3006,3104,3105,3106,3202,3411,3900,3920,6200,6201),
    region!(62002; 6202),
    region!(14000; 1400),
    region!(64000; 3014,3015,3111,3121,3207,3208,6400),
    region!(64001; 3016,3110,3120,3413,6402),
    region!(63000; 3007,3008,3012,3118,3119,3204,3205,6300),
    region!(63002; 3010,3013,3412,6301),
    region!(63001; 3009,3107,3109,6302),
    region!(16000; 1600),
    region!(11000; 1100,3414,3415,3500),
    region!(63003; 6500),
    region!(65000; 3017,3018,3122,6501,6502),
    region!(13000; 1300),
    region!(65002; 3019,3020,3112,3211,6503),
    region!(15001; 1500),
    region!(12070; 1202,1207),
    region!(6664500; 1205),
    region!(12012; 1201,1204),
    region!(12030; 1203),
    region!(11050; 1105,1900),
    region!(68000; 4100,4102,4300,6800,6801,6804,6805,6806,6810,6815,6820),
    region!(69000; 4000,4001,4101,6900,6901,6902,6903,6904,6905,6906),
    region!(69400; 6940),
    region!(68300; 2200,6830,6835),
    region!(68400; 6840),
    region!(68500; 4301,6841,6850),
    region!(69200; 6920,6930,6960),
    region!(68600; 2800,4002,6860),
    region!(20010; 2001),
];

pub fn for_play_region(id: u32) -> Option<&'static Region> {
    let prefix = id / 1000;
    REGIONS.iter().find(|r| r.prefixes.contains(&prefix))
}

pub fn for_map(map: [u8; 3]) -> Option<&'static Region> {
    if [60, 61].contains(&map[0]) {
        return None;
    }
    // Whole-map relationships only. Shadow Keep spans several broad regions
    // and intentionally needs PARAM/location evidence instead.
    let id = match (map[0], map[1]) {
        (10, 1) => Some(62000),
        (12, 8 | 9) => Some(12070),
        (20, 0) => Some(68000),
        (25, 0) => Some(69000),
        _ => None,
    };
    if let Some(id) = id {
        return by_id(id);
    }
    for_play_region((u32::from(map[0]) * 100 + u32::from(map[1])) * 1000)
}

pub fn by_id(id: u32) -> Option<&'static Region> {
    REGIONS.iter().find(|r| r.id == id)
}

/// Location-level relationships missing from multiplayer/grace PARAMs.
/// Coordinates remain in WorldMapPointParam; no Boss flag or name is stored.
pub fn for_place(id: i32) -> Option<&'static Region> {
    by_id(match id {
        6100900 => 61000, // Forlorn Hound Evergaol, on Limgrave's southern ridge.
        6210900 => 62002, // Ringleader's Evergaol, above the western Liurnia shore.
        6410900 => 64001, // Sellia Evergaol, on the Dragonbarrow plateau.
        _ => return None,
    })
}

pub fn for_subcategory(id: u32) -> Option<&'static Region> {
    let id = match id {
        10000 | 10010 => 10000,
        11000 | 35000 => 11000,
        11050 | 19000 => 11050,
        12010..=12012 => 12012,
        12020 | 12070 => 12070,
        12030 => 12030,
        12050 => 6664500,
        13000 | 14000 | 16000 => id,
        15000 | 15001 => 15001,
        18000 | 61000 | 61001 => 61000,
        20000 | 6800 | 6820 => 68000,
        20010 => 20010,
        21000 | 21001 | 21010 | 6900 | 6950 => 69000,
        22000 | 6830 => 68300,
        28000 | 6860 => 68600,
        39200 | 62000 | 62001 => 62000,
        61002 => 61002,
        62002 => 62002,
        63000 => 63000,
        63001 => 63001,
        63002 => 63002,
        63003 => 63003,
        64000 | 64002 => 64000,
        64001 => 64001,
        65000 | 65001 => 65000,
        65002 => 65002,
        6840 => 68400,
        6850 | 6851 => 68500,
        6920 => 69200,
        6940 => 69400,
        _ => return None,
    };
    by_id(id)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn broad_region_keys_are_stable_and_do_not_treat_map_tiles_as_play_regions() {
        assert_eq!(REGIONS.len(), 32);
        assert_eq!(for_play_region(6100001).unwrap().id, 61000);
        assert_eq!(for_play_region(6101010).unwrap().id, 61000);
        assert_eq!(for_play_region(1000000).unwrap().id, 10000);
        assert_eq!(for_map([10, 1, 0]).unwrap().id, 62000);
        assert_eq!(for_map([30, 2, 0]).unwrap().id, 61000);
        assert_eq!(for_map([41, 2, 0]).unwrap().id, 68000);
        assert!(for_map([60, 44, 35]).is_none());
        assert!(for_play_region(0).is_none());
        assert!(for_play_region(u32::MAX).is_none());
        for (i, region) in REGIONS.iter().enumerate() {
            assert!(REGIONS[..i].iter().all(|r| r.id != region.id));
            for key in region.prefixes {
                assert!(REGIONS[..i].iter().all(|r| !r.prefixes.contains(key)));
            }
        }
    }
}
