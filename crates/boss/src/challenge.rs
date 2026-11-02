use eroverlay_abi::BossState;
use std::{
    path::Path,
    time::{Duration, Instant, SystemTime},
};
#[derive(Default, Clone, Debug)]
pub struct Challenge {
    pub best: u32,
    pub tries: u32,
    pub deaths_on_start: u32,
    last_deaths: Option<u32>,
    reached: Option<bool>,
    stamp: Option<SystemTime>,
    next_check: Option<Instant>,
}
impl Challenge {
    pub fn begin(&mut self, s: &BossState) {
        self.deaths_on_start = s.deaths;
        self.last_deaths = Some(s.deaths);
        self.reached = (s.graveyard_reached < 2).then_some(s.graveyard_reached == 1);
    }
    pub fn load(&mut self, root: &Path) {
        let path = root.join("Challenge.txt");
        if let Ok(meta) = path.metadata()
            && meta.len() <= 4096
        {
            self.stamp = meta.modified().ok();
            if let Ok(text) = std::fs::read_to_string(path) {
                for line in text.lines() {
                    let Some((k, v)) = line.split_once('=') else {
                        continue;
                    };
                    let Ok(v) = v.trim().parse::<u32>() else {
                        continue;
                    };
                    match k.trim() {
                        "best" => self.best = v,
                        "tries" => self.tries = v,
                        "deaths_on_start" => self.deaths_on_start = v,
                        _ => {}
                    }
                }
            }
        }
    }
    pub fn check_external(&mut self, root: &Path) {
        let now = Instant::now();
        if self.next_check.is_some_and(|next| now < next) {
            return;
        }
        self.next_check = Some(now + Duration::from_secs(1));
        let stamp = root
            .join("Challenge.txt")
            .metadata()
            .ok()
            .and_then(|v| v.modified().ok());
        if stamp.is_some() && stamp != self.stamp {
            self.load(root);
        }
    }
    pub fn deaths(&self, s: &BossState) -> u32 {
        if s.graveyard_reached != 1 {
            0
        } else {
            s.deaths.saturating_sub(self.deaths_on_start)
        }
    }
    pub fn update(&mut self, s: &BossState, kills: u32, limit: u32) -> bool {
        if s.graveyard_reached > 1 {
            return false;
        }
        let before = (self.best, self.tries, self.deaths_on_start);
        let reached = s.graveyard_reached != 0;
        if self.reached == Some(false) && reached {
            self.deaths_on_start = s.deaths;
        }
        if !reached {
            self.deaths_on_start = 0;
        }
        if self.deaths_on_start > s.deaths {
            self.deaths_on_start = 0;
        }
        let deaths = self.deaths(s);
        let previous = self
            .last_deaths
            .unwrap_or(s.deaths)
            .saturating_sub(self.deaths_on_start);
        if reached && self.reached != Some(false) && previous <= limit && deaths > limit {
            self.tries = self.tries.saturating_add(1);
        }
        if reached && deaths <= limit {
            self.best = self.best.max(kills);
        }
        self.last_deaths = Some(s.deaths);
        self.reached = Some(reached);
        before != (self.best, self.tries, self.deaths_on_start)
    }
    pub fn save(&mut self, root: &Path) -> std::io::Result<()> {
        let text = format!(
            "# Failed challenge attempts\ntries={}\n# Personal best\nbest={}\ndeaths_on_start={}\n",
            self.tries, self.best, self.deaths_on_start
        );
        crate::settings::write_atomic(root.join("Challenge.txt"), text.as_bytes())?;
        self.stamp = root
            .join("Challenge.txt")
            .metadata()
            .ok()
            .and_then(|v| v.modified().ok());
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn challenge_counts_crossing_once_and_new_save_does_not_underflow() {
        let mut c = Challenge::default();
        let mut s = BossState {
            graveyard_reached: 0,
            deaths: 5,
            ..Default::default()
        };
        c.update(&s, 0, 0);
        s.graveyard_reached = 1;
        c.update(&s, 3, 0);
        assert_eq!((c.deaths(&s), c.best, c.tries), (0, 3, 0));
        s.deaths = 7;
        c.update(&s, 4, 0);
        c.update(&s, 5, 0);
        assert_eq!((c.best, c.tries), (3, 1));
        s.deaths = 0;
        s.graveyard_reached = 0;
        c.update(&s, 0, 0);
        assert_eq!(c.deaths(&s), 0);
    }
}
