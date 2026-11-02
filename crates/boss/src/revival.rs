//! Only the externally configured, explicitly reviewed recipes can mutate flags.
use eroverlay_abi::FlagWrite;
use serde::{
    Deserialize, Deserializer,
    de::{self, MapAccess, Visitor},
};
use std::{collections::BTreeMap, path::Path};

#[derive(Deserialize)]
#[serde(untagged)]
enum RawFlag {
    Clear(u32),
    Set(RawWrite),
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawWrite {
    flag_id: u32,
    value: bool,
}
struct Recipes(BTreeMap<u32, Vec<RawFlag>>);
impl<'de> Deserialize<'de> for Recipes {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct RecipeVisitor;
        impl<'de> Visitor<'de> for RecipeVisitor {
            type Value = Recipes;
            fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                f.write_str("an object of unique primary flags and auxiliary write arrays")
            }
            fn visit_map<M: MapAccess<'de>>(self, mut map: M) -> Result<Recipes, M::Error> {
                let mut recipes = BTreeMap::new();
                while let Some((key, flags)) = map.next_entry::<String, Vec<RawFlag>>()? {
                    let primary = key
                        .parse::<u32>()
                        .ok()
                        .filter(|id| *id != 0 && *id != u32::MAX)
                        .ok_or_else(|| de::Error::custom("invalid primary flag"))?;
                    if flags.len() > 2048 || recipes.len() >= 4096 {
                        return Err(de::Error::custom("revival recipe budget exceeded"));
                    }
                    if recipes.insert(primary, flags).is_some() {
                        return Err(de::Error::custom("duplicate primary flag"));
                    }
                }
                Ok(Recipes(recipes))
            }
        }
        deserializer.deserialize_map(RecipeVisitor)
    }
}
#[derive(Default)]
pub struct Revival {
    recipes: BTreeMap<u32, Vec<FlagWrite>>,
}
pub struct Operation {
    pub primary: u32,
    pub extras: Vec<FlagWrite>,
}
impl Revival {
    pub fn load(root: &Path) -> Result<Self, String> {
        let path = root.join("configs/boss-revival.json");
        let metadata = path.metadata().map_err(|e| e.to_string())?;
        if metadata.len() > 1024 * 1024 {
            return Err("revival configuration exceeds 1 MiB".into());
        }
        let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
        Self::parse(&bytes)
    }
    fn parse(bytes: &[u8]) -> Result<Self, String> {
        let Recipes(object) = serde_json::from_slice(bytes).map_err(|e| e.to_string())?;
        let mut recipes = BTreeMap::new();
        for (primary, values) in object {
            let mut writes = Vec::<FlagWrite>::with_capacity(values.len());
            for entry in values {
                let (id, value) = match entry {
                    RawFlag::Clear(id) => (id, false),
                    RawFlag::Set(RawWrite { flag_id, value }) => (flag_id, value),
                };
                if id == 0 || id == u32::MAX {
                    return Err("invalid auxiliary flag".into());
                }
                if id == primary {
                    if value {
                        return Err("primary flag cannot be set true".into());
                    }
                    continue;
                }
                if let Some(old) = writes.iter().find(|w| w.id == id) {
                    if old.value != u32::from(value) {
                        return Err("conflicting auxiliary flag values".into());
                    }
                } else {
                    // Preserve author-specified arena/quest write order.
                    writes.push(FlagWrite {
                        id,
                        value: u32::from(value),
                    });
                }
            }
            recipes.insert(primary, writes);
        }
        Ok(Self { recipes })
    }
    pub fn recipe(&self, flag: u32) -> Option<&[FlagWrite]> {
        self.recipes.get(&flag).map(Vec::as_slice)
    }
    pub fn supports(&self, canonical: u32, completion: &[u32]) -> bool {
        let Some(recipe) = self.recipe(canonical) else {
            return false;
        };
        !completion.is_empty()
            && completion.contains(&canonical)
            && completion.iter().all(|flag| {
                *flag == canonical || recipe.iter().any(|w| w.id == *flag && w.value == 0)
            })
    }
    /// Keep the reviewed target/value set. The core still requires a currently
    /// true completion witness, checks every address, and clears that bit last.
    pub fn operation(
        &self,
        canonical: u32,
        completion: &[u32],
        values: &[u8],
    ) -> Option<Operation> {
        if completion.len() != values.len() || !self.supports(canonical, completion) {
            return None;
        }
        let completed = |flag| {
            completion
                .iter()
                .position(|id| *id == flag)
                .is_some_and(|i| values[i] == 1)
        };
        let primary = if completed(canonical) {
            canonical
        } else {
            *completion.iter().find(|id| completed(**id))?
        };
        let mut extras = self.recipe(canonical)?.to_vec();
        if primary != canonical {
            extras.retain(|w| w.id != primary);
            extras.push(FlagWrite {
                id: canonical,
                value: 0,
            });
        }
        Some(Operation { primary, extras })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn configured_recipe_distinguishes_empty_from_unknown_and_rejects_conflicts() {
        let r = Revival::parse(br#"{"8":[],"16":[17,{"flag_id":18,"value":true},17]}"#).unwrap();
        assert_eq!(r.recipe(8).unwrap().len(), 0);
        assert!(r.recipe(9).is_none());
        assert_eq!(r.recipe(16).unwrap().len(), 2);
        assert!(Revival::parse(br#"{"8":[9,{"flag_id":9,"value":true}]}"#).is_err());
        assert!(Revival::parse(br#"{"8":[{"flag_id":8,"value":true}]}"#).is_err());
        assert!(Revival::parse(br#"{"8":[],"8":[9]}"#).is_err());
        assert!(Revival::parse(br#"{"8":[{"flag_id":9,"flag_id":10,"value":true}]}"#).is_err());
        let ordered = Revival::parse(br#"{"8":[11,10,9,10]}"#).unwrap();
        assert_eq!(
            ordered
                .recipe(8)
                .unwrap()
                .iter()
                .map(|w| w.id)
                .collect::<Vec<_>>(),
            [11, 10, 9]
        );
    }
    #[test]
    fn alternative_completion_uses_the_same_reviewed_writes_and_a_true_witness() {
        let r = Revival::parse(br#"{"40":[30,31,42,43],"80":[81,{"flag_id":82,"value":true}]}"#)
            .unwrap();
        let direct = r.operation(40, &[40, 30], &[1, 0]).unwrap();
        assert_eq!(direct.primary, 40);
        assert_eq!(
            direct
                .extras
                .iter()
                .map(|w| (w.id, w.value))
                .collect::<Vec<_>>(),
            r.recipe(40)
                .unwrap()
                .iter()
                .map(|w| (w.id, w.value))
                .collect::<Vec<_>>()
        );
        let alternate = r.operation(40, &[40, 30], &[0, 1]).unwrap();
        assert_eq!(alternate.primary, 30);
        assert_eq!(
            alternate
                .extras
                .iter()
                .map(|w| (w.id, w.value))
                .collect::<Vec<_>>(),
            [(31, 0), (42, 0), (43, 0), (40, 0)]
        );
        let mut original = r
            .recipe(40)
            .unwrap()
            .iter()
            .map(|w| (w.id, w.value))
            .chain([(40, 0)])
            .collect::<Vec<_>>();
        let mut actual = alternate
            .extras
            .iter()
            .map(|w| (w.id, w.value))
            .chain([(alternate.primary, 0)])
            .collect::<Vec<_>>();
        original.sort_unstable();
        actual.sort_unstable();
        assert_eq!(actual, original);
        assert!(r.operation(40, &[40, 30], &[0, 2]).is_none());
        assert!(r.operation(40, &[40, 30], &[0, 0]).is_none());
        assert!(!r.supports(40, &[40, 32]));
        assert!(!r.supports(80, &[80, 82]));
        assert!(r.operation(40, &[40, 30], &[1]).is_none());
    }
}
