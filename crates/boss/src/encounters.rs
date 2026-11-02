//! Explicit cross-location relationships only. Missing/invalid configuration
//! has no built-in fallback; the caller reports the error and disables links.
use serde::Deserialize;
use std::{
    collections::{BTreeMap, BTreeSet},
    io::Read,
    path::Path,
};

const MAX_BYTES: usize = 128 * 1024;
const MAX_GROUPS: usize = 256;
const MAX_EXTRA_MEMBERS: usize = 64;
const MAX_FLAGS: usize = 4096;

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
enum Progress {
    Primary,
    Any,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Configuration {
    version: u32,
    groups: Vec<RawGroup>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawGroup {
    primary: u32,
    /// Extra flags only: the primary is implicitly a member.
    members: Vec<u32>,
    progress: Progress,
}

#[derive(Clone, Debug)]
struct Group {
    primary: u32,
    #[cfg(test)]
    members: Vec<u32>,
    completion: Vec<u32>,
}
impl Group {
    fn primary(&self) -> u32 {
        self.primary
    }
    /// All members, with the canonical primary first.
    #[cfg(test)]
    fn members(&self) -> &[u32] {
        &self.members
    }
    /// Primary is one flag; Any completes when at least one listed flag is set.
    /// This is progress metadata, not a list of revival writes.
    fn completion_flags(&self) -> &[u32] {
        &self.completion
    }
}

#[derive(Clone, Debug, Default)]
pub struct Relations {
    groups: Vec<Group>,
    by_flag: BTreeMap<u32, usize>,
}
impl Relations {
    #[cfg(test)]
    fn groups(&self) -> &[Group] {
        &self.groups
    }
    fn group(&self, flag: u32) -> Option<&Group> {
        self.by_flag.get(&flag).map(|&index| &self.groups[index])
    }
    pub fn canonical(&self, flag: u32) -> u32 {
        self.group(flag).map_or(flag, Group::primary)
    }
    /// Accepts either the primary or an extra member. Unconfigured flags return
    /// None; the caller retains that flag's ordinary single-record behavior.
    pub fn completion_flags(&self, flag: u32) -> Option<&[u32]> {
        self.group(flag).map(Group::completion_flags)
    }
    fn parse(bytes: &[u8]) -> Result<Self, String> {
        if bytes.len() > MAX_BYTES {
            return Err("encounter configuration exceeds 128 KiB".into());
        }
        let config: Configuration =
            serde_json::from_slice(bytes).map_err(|error| error.to_string())?;
        if config.version != 1 {
            return Err(format!("unsupported encounter version {}", config.version));
        }
        if config.groups.len() > MAX_GROUPS {
            return Err("encounter group budget exceeded".into());
        }
        let mut result = Self::default();
        let mut primaries = BTreeSet::new();
        for raw in config.groups {
            let valid = |flag| flag != 0 && flag != u32::MAX;
            if !valid(raw.primary) {
                return Err(format!("invalid primary flag {}", raw.primary));
            }
            if !primaries.insert(raw.primary) {
                return Err(format!("duplicate primary flag {}", raw.primary));
            }
            if raw.members.is_empty() {
                return Err(format!("primary {} needs an extra member", raw.primary));
            }
            if raw.members.len() > MAX_EXTRA_MEMBERS {
                return Err("encounter member budget exceeded".into());
            }
            let mut members = vec![raw.primary];
            let mut seen = BTreeSet::new();
            for flag in raw.members {
                if !valid(flag) {
                    return Err(format!("invalid member flag {flag}"));
                }
                if flag == raw.primary {
                    return Err(format!("self relationship for flag {flag}"));
                }
                if !seen.insert(flag) {
                    return Err(format!("duplicate member flag {flag}"));
                }
                members.push(flag);
            }
            let index = result.groups.len();
            for &flag in &members {
                // One owner per flag forbids overlapping groups, chains and
                // cycles alike. There is no order-dependent transitive lookup.
                if result.by_flag.insert(flag, index).is_some() {
                    return Err(format!(
                        "flag {flag} belongs to multiple groups (overlap, chain or cycle)"
                    ));
                }
                if result.by_flag.len() > MAX_FLAGS {
                    return Err("encounter flag budget exceeded".into());
                }
            }
            let completion = match raw.progress {
                Progress::Primary => vec![raw.primary],
                Progress::Any => members.clone(),
            };
            result.groups.push(Group {
                primary: raw.primary,
                #[cfg(test)]
                members,
                completion,
            });
        }
        Ok(result)
    }
}

pub fn load(root: &Path) -> Result<Relations, String> {
    let path = root.join("configs/boss-encounters.json");
    let file =
        std::fs::File::open(&path).map_err(|error| format!("{}: {error}", path.display()))?;
    let mut bytes = Vec::new();
    // A metadata-only limit could race a growing file. Never copy more than
    // the bounded configuration plus one byte before rejecting its size.
    file.take((MAX_BYTES + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(|error| format!("{}: {error}", path.display()))?;
    Relations::parse(&bytes).map_err(|error| format!("{}: {error}", path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shipped_relationships_preserve_authorized_progress_and_revival_identity() {
        let relations =
            Relations::parse(include_bytes!("../../../configs/boss-encounters.json")).unwrap();
        assert_eq!(relations.groups().len(), 3);
        for (member, primary, completion) in [
            (1034500800, 1034420800, vec![1034420800]),
            (1037510800, 1041520800, vec![1041520800, 1037510800]),
            (31000850, 31000800, vec![31000800]),
        ] {
            let group = relations.group(member).unwrap();
            assert_eq!(relations.canonical(member), primary);
            assert_eq!(relations.canonical(primary), primary);
            assert_eq!(group.primary(), primary);
            assert_eq!(group.members(), [primary, member]);
            assert_eq!(
                relations.completion_flags(member),
                Some(completion.as_slice())
            );
            assert_eq!(
                relations.completion_flags(primary),
                Some(completion.as_slice())
            );
        }
        // Retreat is not an early kill. Extra completion membership does not
        // add a revival recipe or make a later Patches flag its primary.
        assert!(relations.group(1037510810).is_none());
        assert_eq!(relations.canonical(31000850), 31000800);
        for flag in [2050470800, 2050480860, 1038540800, 0] {
            assert_eq!(relations.canonical(flag), flag);
            assert!(relations.group(flag).is_none());
            assert!(relations.completion_flags(flag).is_none());
        }
    }

    #[test]
    fn malformed_duplicate_conflicting_chained_and_cyclic_definitions_fail_atomically() {
        for input in [
            r#"{"version":2,"groups":[]}"#,
            r#"{"version":1,"version":1,"groups":[]}"#,
            r#"{"version":1,"groups":[],"unknown":true}"#,
            r#"{"version":1,"groups":[{"primary":0,"members":[2],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[0],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":4294967295,"members":[2],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[4294967295],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[1],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2,2],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"progress":"all"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"primary":2,"members":[3],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"members":[3],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"progress":"any","progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"progress":"any","unknown":1}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"progress":"primary"},{"primary":1,"members":[3],"progress":"any"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"progress":"primary"},{"primary":3,"members":[2],"progress":"any"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"progress":"primary"},{"primary":2,"members":[3],"progress":"primary"}]}"#,
            r#"{"version":1,"groups":[{"primary":1,"members":[2],"progress":"primary"},{"primary":2,"members":[1],"progress":"primary"}]}"#,
        ] {
            assert!(
                Relations::parse(input.as_bytes()).is_err(),
                "accepted {input}"
            );
        }
        let empty = Relations::parse(br#"{"version":1,"groups":[]}"#).unwrap();
        assert!(empty.groups().is_empty());
        assert_eq!(empty.canonical(1034500800), 1034500800);
        assert!(empty.completion_flags(1034500800).is_none());
    }

    #[test]
    fn load_missing_invalid_and_oversized_files_returns_an_error_without_default_mapping() {
        let unique = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let root = std::env::temp_dir().join(format!(
            "eroverlay-encounters-{}-{unique}",
            std::process::id()
        ));
        let config = root.join("configs");
        let path = config.join("boss-encounters.json");
        assert!(load(&root).is_err());
        std::fs::create_dir_all(&config).unwrap();
        for bytes in [b"{".to_vec(), vec![b' '; MAX_BYTES + 1]] {
            std::fs::write(&path, bytes).unwrap();
            let error = load(&root).unwrap_err();
            assert!(error.contains("boss-encounters.json"));
        }
        std::fs::write(&path, br#"{"version":1,"groups":[]}"#).unwrap();
        assert!(load(&root).unwrap().groups().is_empty());
        // Only remove known individual fixture paths, without recursive delete.
        std::fs::remove_file(path).unwrap();
        std::fs::remove_dir(config).unwrap();
        std::fs::remove_dir(root).unwrap();
    }
}
