use crate::{Action, Instance, Snapshot, host::Host, settings::Settings};
use egui::{Align2, Color32, RichText};
use eroverlay_abi as abi;
use eroverlay_ui::{fonts, input::Input, meshes::Meshes};
use std::{collections::BTreeSet, path::PathBuf, sync::atomic::Ordering};

enum Row {
    Header { group: usize, text: String },
    Boss(usize),
}
pub struct Ui {
    context: egui::Context,
    definitions: egui::FontDefinitions,
    meshes: Meshes,
    input: Input,
    settings: Settings,
    settings_dirty: bool,
    chinese: bool,
    query: String,
    remaining: bool,
    selected: Option<usize>,
    closed: BTreeSet<usize>,
    rows: Vec<Row>,
    rows_dirty: bool,
    revision: u64,
    map: u32,
    current: Option<usize>,
    modal: Option<(u32, u64, String)>,
    last_sequence: Option<u64>,
    time: f64,
    style_size: f32,
}
impl Ui {
    pub fn new(host: Host, settings: &Settings) -> Self {
        let context = egui::Context::default();
        let root = PathBuf::from(host.config("runtime.directory").unwrap_or_default()).join("data");
        let windows =
            PathBuf::from(std::env::var_os("WINDIR").unwrap_or_else(|| "C:/Windows".into()))
                .join("Fonts");
        let language = host
            .config("runtime.game_language")
            .unwrap_or_else(|| "english".into());
        let charset = host.config("common.charset").unwrap_or_default();
        let paths = fonts::font_candidates(
            &root,
            &host.config("common.font").unwrap_or_default(),
            &windows,
            fonts::font_charset(&charset, &language),
        );
        let (definitions, loaded) =
            fonts::load_font_definitions(&paths, &windows.join("msyh.ttc"), |p| {
                p.metadata()
                    .is_ok_and(|m| m.len() <= 64 * 1024 * 1024)
                    .then(|| std::fs::read(p).ok())
                    .flatten()
            });
        for path in loaded {
            host.log(&format!("boss-font {}\n", path.display()));
        }
        let chinese = (language.contains("chinese") || language.starts_with("zho"))
            && fonts::supports_status_glyphs(&definitions);
        context.set_fonts(definitions.clone());
        Self {
            context,
            definitions,
            meshes: Meshes::default(),
            input: Input::default(),
            settings: settings.clone(),
            settings_dirty: false,
            chinese,
            query: String::new(),
            remaining: false,
            selected: None,
            closed: BTreeSet::new(),
            rows: Vec::new(),
            rows_dirty: true,
            revision: 0,
            map: u32::MAX,
            current: None,
            modal: None,
            last_sequence: None,
            time: 0.0,
            style_size: 0.0,
        }
    }
    pub fn reset(&mut self, host: Host) {
        self.meshes.reset(&host.0);
        self.last_sequence = None;
        self.input.clear();
        self.modal = None;
        self.context = egui::Context::default();
        self.context.set_fonts(self.definitions.clone());
        self.style_size = 0.0;
        host.capture(0);
        host.clear_input();
    }
    fn label<'a>(&self, en: &'a str, zh: &'a str) -> &'a str {
        if self.chinese { zh } else { en }
    }
    fn status<'a>(&self, status: &'a str) -> &'a str {
        if status == "Ready" {
            return "";
        }
        if !self.chinese {
            return status;
        }
        match status {
            "Waiting for game parameters" => "正在等待游戏参数",
            "Waiting for game language" => "正在等待游戏语言",
            "Loading game text and Boss events" => "正在读取游戏文本与 Boss 事件",
            "Loading Boss data; some requests timed out" => {
                "部分 Boss 资源请求超时，正在整理已读取的数据"
            }
            "Boss data unavailable; reload game data" => {
                "Boss 数据读取失败，请点击“重新读取游戏数据”重试"
            }
            "Discovering named Boss health bars" => "正在核对具名 Boss 生命条",
            "Checking Boss scene entities" => "正在核对 Boss 场景实体",
            "No named Boss health bars found" => "未发现具名 Boss 生命条，可尝试重新读取游戏数据",
            "Ready; some game resources are unavailable" => {
                "部分游戏资源不可用，目录可能不完整，可重新读取游戏数据"
            }
            _ => status,
        }
    }
    fn queue(instance: &Instance, action: Action) -> bool {
        if let Ok(mut actions) = instance.actions.try_lock()
            && actions.len() < 8
        {
            actions.push(action);
            true
        } else {
            false
        }
    }
    fn rebuild(&mut self, s: &Snapshot) {
        self.rows.clear();
        let Some(c) = s.catalog.as_ref() else { return };
        let query = self.query.to_lowercase();
        for (group, g) in c.groups.iter().enumerate() {
            if self.selected.is_some_and(|v| v != group) {
                continue;
            }
            let matches =
                |index: usize| s.states.get(index).copied().unwrap_or(2) != 1 || !self.remaining;
            let relevant: Vec<_> = c
                .rows
                .iter()
                .enumerate()
                .filter(|(i, r)| {
                    r.group == group
                        && matches(*i)
                        && (query.is_empty() || r.search.contains(&query))
                })
                .map(|(i, _)| i)
                .collect();
            if relevant.is_empty() {
                continue;
            }
            let killed = c
                .rows
                .iter()
                .enumerate()
                .filter(|(i, r)| r.group == group && r.flag != 0 && s.states.get(*i) == Some(&1))
                .count();
            let total = c
                .rows
                .iter()
                .filter(|r| r.group == group && r.flag != 0)
                .count();
            let arrow = if self.closed.contains(&group) {
                ">"
            } else {
                "v"
            };
            self.rows.push(Row::Header {
                group,
                text: format!("{arrow} {}  {killed}/{total}", g.name),
            });
            if !self.closed.contains(&group) {
                self.rows.extend(relevant.into_iter().map(Row::Boss));
            }
        }
        self.rows_dirty = false;
        self.revision = s.revision;
    }
    fn settings_panel(&mut self, ui: &mut egui::Ui) {
        let chinese = self.chinese;
        let l = |en, zh| if chinese { zh } else { en };
        egui::CollapsingHeader::new(l("Settings", "设置")).show(ui, |ui| {
            let before = self.settings.clone();
            ui.add(
                egui::Slider::new(&mut self.settings.opacity, 0.1..=1.)
                    .text(l("Opacity", "透明度")),
            );
            ui.add(
                egui::Slider::new(&mut self.settings.font_size, 12.0..=40.0)
                    .text(l("Text size", "字号")),
            );
            ui.checkbox(
                &mut self.settings.allow_revive,
                l("Allow revival", "允许复活"),
            );
            ui.checkbox(
                &mut self.settings.challenge_mode,
                l("Challenge mode", "挑战模式"),
            );
            if self.settings.challenge_mode {
                ui.horizontal(|ui| {
                    ui.label(l("Death limit", "死亡上限"));
                    ui.add(
                        egui::DragValue::new(&mut self.settings.challenge_death_count)
                            .range(0..=1_000_000),
                    );
                });
            }
            ui.label(l("Compact status template", "紧凑状态模板"));
            ui.text_edit_singleline(&mut self.settings.boss_kill_text);
            if self.settings.challenge_mode {
                ui.label(l("Challenge status template", "挑战状态模板"));
                ui.text_edit_multiline(&mut self.settings.challenge_status_text);
            }
            ui.small("{kills} {total} {deaths} {igt} {pb} {tries}  $n");
            if before != self.settings {
                self.settings.sanitize();
                self.settings_dirty = true;
            }
        });
    }
    pub fn render(&mut self, i: &Instance, s: &Snapshot, frame: &abi::FrameInfo) -> i32 {
        if self.last_sequence == Some(frame.sequence) {
            return abi::OK;
        }
        self.last_sequence = Some(frame.sequence);
        let shown = i.shown.load(Ordering::Relaxed) && i.host.visible();
        if !shown {
            i.host.capture(0);
            i.host.clear_input();
            self.input.clear();
            self.modal = None;
            return abi::OK;
        }
        let full = i.full.load(Ordering::Relaxed);
        let mut events = [abi::UiInput::default(); abi::MAX_UI_INPUT_EVENTS];
        let mut count = 0;
        if full {
            i.host
                .capture(abi::UI_CAPTURE_MOUSE | abi::UI_CAPTURE_KEYBOARD);
            if let Some(f) = i.host.0.ui_input_poll {
                // SAFETY: the host writes up to the supplied capacity and POD count.
                if unsafe { f(i.host.0.user, events.as_mut_ptr(), events.len(), &mut count) }
                    != abi::OK
                {
                    count = 0;
                }
            }
        } else {
            i.host.capture(0);
            self.input.clear();
            self.modal = None;
        }
        self.time += f64::from(frame.delta_seconds.clamp(0.001, 0.1));
        let raw = self
            .input
            .raw(frame, self.time, &events[..count.min(events.len())]);
        let options = abi::RenderOptions {
            clip_rect: [0., 0., frame.width, frame.height],
            opacity: self.settings.opacity,
            ..Default::default()
        };
        if let Some(f) = i.host.0.set_render_options {
            let result = unsafe { f(i.host.0.user, &options) };
            if result != abi::OK {
                return result;
            }
        }
        if let Some(state) = s.state
            && state.raw_map_id != self.map
        {
            self.map = state.raw_map_id;
            self.current = s.catalog.as_ref().and_then(|c| {
                c.rows
                    .iter()
                    .find(|r| r.map_id >> 8 == state.raw_map_id >> 8)
                    .map(|r| r.group)
            });
            if let Some(g) = self.current {
                self.closed.remove(&g);
            }
            self.rows_dirty = true;
        }
        // Compact mode consumes prepared status text; list work stays pending until opened.
        if full && (self.rows_dirty || self.revision != s.revision) {
            self.rebuild(s);
        }
        let ctx = self.context.clone();
        if self.style_size != self.settings.font_size {
            let mut style = (*ctx.style()).clone();
            for (kind, font) in &mut style.text_styles {
                font.size = self.settings.font_size
                    * if *kind == egui::TextStyle::Heading {
                        1.2
                    } else {
                        1.0
                    };
            }
            ctx.set_style(style);
            self.style_size = self.settings.font_size;
        }
        let mut full_open = full;
        let output=ctx.run(raw,|ctx|{
            if !full{
                if s.catalog.is_some(){
                    egui::Area::new("boss-compact".into()).anchor(Align2::RIGHT_TOP,[-10.,10.]).interactable(false).show(ctx,|ui|{
                        egui::Frame::window(ui.style()).show(ui,|ui|{ui.label(&s.compact);});
                    });
                }
                return;
            }
            if ctx.input(|input|input.key_pressed(egui::Key::Escape))&&self.modal.is_none(){full_open=false;}
            let title=self.label("Boss progress","Boss 进度");
            egui::Window::new(title).id("boss-list".into()).open(&mut full_open).collapsible(false).resizable(true)
                .default_pos([frame.width-self.settings.width-10.,10.])
                .default_size([self.settings.width,self.settings.height.min(frame.height-20.)])
                .max_size([frame.width.max(280.)-10.,frame.height.max(220.)-10.]).show(ctx,|ui|{
                    ui.label(RichText::new(&s.compact).strong());
                    ui.add(egui::ProgressBar::new(if s.total==0{0.0}else{s.kills as f32/s.total as f32}).text(format!("{}/{}",s.kills,s.total)));
                    ui.horizontal_wrapped(|ui|{
                        ui.label(format!("{}: {}",self.label("Deaths","死亡"),s.deaths));
                        if let Some(state)=s.state{let sec=state.in_game_time_ms/1000;ui.label(format!("IGT {}:{:02}:{:02}",sec/3600,sec/60%60,sec%60));}
                        if s.settings.challenge_mode{ui.label(format!("PB {}  {} {}",s.best,self.label("Tries","失败尝试"),s.tries));}
                    });
                    let status=self.status(s.status);
                    if !status.is_empty(){ui.label(status);}
                    if s.state.is_none(){ui.label(self.label("Waiting for a valid save","等待有效存档"));}
                    if let Some(result)=s.result{ui.label(if result{self.label("Revived. Travel or reload the area.","已复活。请传送或重新加载区域。")}else{self.label("Revival failed: save or flags changed. Reopen the confirmation.","复活失败：存档或旗标已变化，请重新确认。")});}
                    self.settings_panel(ui);
                    if self.settings.challenge_mode {
                        ui.small(self.label("When switching saves, start a new attempt to set its death baseline.","切换存档后，请开始本轮以设置死亡基线。"));
                        if ui.add_enabled(s.state.is_some(),egui::Button::new(self.label("Start this attempt","开始本轮"))).clicked(){Self::queue(i,Action::BeginChallenge);}
                        if s.state.is_some_and(|state|state.graveyard_reached>1){ui.small(self.label("Challenge tracking is waiting for its start flag.","挑战记录正在等待有效起始旗标。"));}
                    }
                    if ui.small_button(self.label("Reload game data","重新读取游戏数据")).clicked(){Self::queue(i,Action::Reload);self.modal=None;}
                    ui.separator();
                    let mut filter_changed=ui.add(egui::TextEdit::singleline(&mut self.query).hint_text(if self.chinese{"搜索 Boss 或地点"}else{"Search boss or location"})).changed();
                    filter_changed|=ui.checkbox(&mut self.remaining,if self.chinese{"只看未完成"}else{"Remaining only"}).changed();
                    if let Some(c)=s.catalog.as_ref(){
                        egui::ComboBox::from_id_salt("boss-region").selected_text(self.selected.and_then(|g|c.groups.get(g)).map(|g|g.name.as_str()).unwrap_or(if self.chinese{"全部地区"}else{"All regions"})).show_ui(ui,|ui|{
                            filter_changed|=ui.selectable_value(&mut self.selected,None,if self.chinese{"全部地区"}else{"All regions"}).changed();
                            for (g,group) in c.groups.iter().enumerate(){filter_changed|=ui.selectable_value(&mut self.selected,Some(g),&group.name).changed();}
                        });
                        if c.unknown_count>0{ui.small(format!("{}: {}",self.label("Unresolved completion conditions","尚未确定的完成条件"),c.unknown_count));}
                    }
                    if filter_changed{self.rows_dirty=true;self.rebuild(s);}
                    let row_height=ui.text_style_height(&egui::TextStyle::Body)+ui.spacing().item_spacing.y;
                    egui::ScrollArea::vertical().auto_shrink([false,false]).show_rows(ui,row_height,self.rows.len(),|ui,range|{
                        let Some(c)=s.catalog.as_ref()else{return};
                        for index in range{match &self.rows[index]{
                            Row::Header{group,text}=>{
                                let label=RichText::new(text).strong().color(if self.current==Some(*group){Color32::GOLD}else{ui.visuals().text_color()});
                                if ui.selectable_label(false,label).clicked(){if !self.closed.remove(group){self.closed.insert(*group);}self.rows_dirty=true;}
                            }
                            Row::Boss(row)=>{
                                let row=*row;
                                let boss=&c.rows[row];let state=s.states.get(row).copied().unwrap_or(2);
                                let status=match state{1=>"[+]",0=>"[ ]",_=>"[?]"};
                                ui.horizontal(|ui|{
                                    let enabled=self.settings.allow_revive&&state==1&&s.revivable.get(row)==Some(&1)&&s.state.is_some();
                                    if ui.add_enabled(enabled,egui::Button::new(status).small()).on_hover_text(self.label("Revive this boss","复活此 Boss")).clicked(){
                                        self.modal=Some((boss.flag,s.state.unwrap().context,boss.name.clone()));
                                    }
                                    let color=if state==1{ui.visuals().weak_text_color()}else{ui.visuals().text_color()};
                                    ui.add(egui::Label::new(RichText::new(&boss.name).color(color)).truncate()).on_hover_ui(|ui|{
                                        ui.label(&boss.name);
                                        ui.label(format!("{}: {}",self.label("Location reference","参考地点"),boss.place));
                                        ui.small(format!("Flag {}",boss.flag));
                                    });
                                });
                            }
                        }}
                    });
                });
            if let Some((flag,context,name))=self.modal.clone(){
                egui::Modal::new("boss-revival-confirm".into()).show(ctx,|ui|{
                    ui.heading(self.label("Revive boss","复活 Boss"));ui.label(name);
                    ui.label(self.label("Restore this encounter's configured arena flags and clear its defeat flag? Travel or reload afterwards. Quest-linked recipes can rewind quest or shared arena state.",
                        "恢复配置中的场地旗标并清除击败旗标？之后需要传送或重新加载。任务关联配方可能回退任务或共享场地状态。"));
                    ui.horizontal(|ui|{
                        if ui.button(self.label("Cancel","取消")).clicked(){self.modal=None;}
                        if ui.button(self.label("Revive","复活")).clicked()&&Self::queue(i,Action::Revive{flag,context}){self.modal=None;}
                    });
                });
            }
            // Game cursor visibility is independent of egui. Draw a small cursor
            // only while this full panel owns mouse capture; no ShowCursor counter.
            if let Some(pos)=ctx.pointer_latest_pos(){
                let p=ctx.layer_painter(egui::LayerId::new(egui::Order::Tooltip,"boss-cursor".into()));
                p.line_segment([pos,pos+egui::vec2(0.,16.)],egui::Stroke::new(2.0_f32,Color32::WHITE));
                p.line_segment([pos,pos+egui::vec2(10.,11.)],egui::Stroke::new(2.0_f32,Color32::WHITE));
                p.line_segment([pos+egui::vec2(0.,16.),pos+egui::vec2(10.,11.)],egui::Stroke::new(2.0_f32,Color32::WHITE));
            }
        });
        if !full_open {
            i.full.store(false, Ordering::Relaxed);
            i.host.capture(0);
            i.host.clear_input();
            self.modal = None;
        }
        // Dragging controls changes only local state. Persist once after release
        // or when closing the panel; the render callback never performs IO.
        if self.settings_dirty
            && (!ctx.input(|v| v.pointer.any_down()) || !full_open)
            && Self::queue(i, Action::Settings(self.settings.clone()))
        {
            self.settings_dirty = false;
        }
        self.meshes.submit(&i.host.0, &self.context, output)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        Update,
        challenge::Challenge,
        data::{Catalog, Data, Encounter, Group},
        revival::Revival,
    };
    use std::sync::{Arc, Mutex, atomic::AtomicBool};

    unsafe extern "C" fn visible(_: *mut std::ffi::c_void, out: *mut abi::DisplayState) -> i32 {
        // SAFETY: Ui::render supplies one live DisplayState output slot.
        unsafe {
            out.write(abi::DisplayState {
                valid: 1,
                ..Default::default()
            })
        };
        abi::OK
    }

    #[test]
    fn compact_revisions_defer_95_group_211_boss_list_until_full_render() {
        // No native hooks/GPU: call the actual Ui::render path with prepared
        // snapshots. Missing texture admission cannot change list construction.
        let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
        api.read_display_state = Some(visible);
        let host = Host(api);
        let settings = Arc::new(Settings::default());
        let instance = Instance {
            host,
            update: Mutex::new(Update {
                data: Data::new(host),
                root: PathBuf::new(),
                settings: settings.clone(),
                keys: [0; 2],
                catalog: None,
                catalog_revision: 0,
                challenge: Challenge::default(),
                revival: Revival::default(),
                next: None,
                revision: 0,
                result: None,
                flag_values: Vec::new(),
            }),
            ui: Mutex::new(Ui::new(host, &settings)),
            snapshot: Mutex::new(None),
            actions: Mutex::new(Vec::new()),
            full: AtomicBool::new(false),
            shown: AtomicBool::new(true),
        };
        let catalog = Arc::new(Catalog {
            groups: (0..95)
                .map(|group| Group {
                    id: group as u32,
                    name: format!("Group {group}"),
                })
                .collect(),
            rows: (0..211)
                .map(|row| Encounter {
                    flag: row + 1,
                    completion: vec![row + 1],
                    name: format!("Boss {row}"),
                    place: format!("Place {}", row % 95),
                    group: row as usize % 95,
                    map_id: 0x0a00_0000 + ((row % 95) << 8),
                    search: format!("boss {row}"),
                })
                .collect(),
            unknown_count: 0,
            flag_ids: (1..=211).collect(),
        });
        let mut snapshot = Snapshot {
            catalog: Some(catalog),
            states: vec![1; 211].into(),
            revivable: vec![0; 211].into(),
            state: Some(abi::BossState {
                size: size_of::<abi::BossState>() as u32,
                valid: 1,
                raw_map_id: 0x0a00_0000,
                ..Default::default()
            }),
            settings,
            kills: 211,
            total: 211,
            deaths: 0,
            best: 0,
            tries: 0,
            compact: "211/211".into(),
            status: "Ready",
            revision: 0,
            result: None,
        };
        let mut frame = abi::FrameInfo {
            size: size_of::<abi::FrameInfo>() as u32,
            layer: abi::LAYER_ABOVE_UI,
            width: 1280.,
            height: 900.,
            pixels_per_point: 1.,
            delta_seconds: 1. / 60.,
            ..Default::default()
        };
        let mut ui = instance.ui.lock().unwrap();
        for revision in 1..=4 {
            snapshot.revision = revision;
            frame.sequence += 1;
            assert_eq!(ui.render(&instance, &snapshot, &frame), abi::OK);
            assert!(ui.rows.is_empty());
            assert_eq!(
                ui.rows.capacity(),
                0,
                "compact render must not construct list rows"
            );
            assert_eq!(ui.revision, 0, "keep the list's pending snapshot revision");
            assert!(ui.rows_dirty);
        }
        instance.full.store(true, Ordering::Relaxed);
        frame.sequence += 1;
        assert_eq!(ui.render(&instance, &snapshot, &frame), abi::OK);
        assert_eq!(ui.rows.len(), 95 + 211);
        assert_eq!(ui.revision, 4);
        assert!(!ui.rows_dirty);
        assert!(matches!(&ui.rows[0],Row::Header {text,..} if text=="v Group 0  3/3"));

        instance.full.store(false, Ordering::Relaxed);
        snapshot.states = vec![0; 211].into();
        snapshot.kills = 0;
        snapshot.compact = "0/211".into();
        snapshot.state.as_mut().unwrap().raw_map_id = 0x0a00_0100;
        for revision in 5..=8 {
            snapshot.revision = revision;
            frame.sequence += 1;
            assert_eq!(ui.render(&instance, &snapshot, &frame), abi::OK);
            assert_eq!(ui.revision, 4);
            assert!(ui.rows_dirty, "map dirtiness must survive compact renders");
            assert!(matches!(&ui.rows[0],Row::Header {text,..} if text=="v Group 0  3/3"));
        }
        instance.full.store(true, Ordering::Relaxed);
        frame.sequence += 1;
        assert_eq!(ui.render(&instance, &snapshot, &frame), abi::OK);
        assert_eq!(ui.rows.len(), 95 + 211);
        assert_eq!(ui.revision, 8);
        assert!(!ui.rows_dirty);
        assert!(matches!(&ui.rows[0],Row::Header {text,..} if text=="v Group 0  0/3"));
    }
}
