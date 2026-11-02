use crate::{
    Action, Instance, Snapshot,
    host::{Host, Visibility},
    settings::Settings,
};
use egui::{Align2, Color32, RichText};
use eroverlay_abi as abi;
use eroverlay_ui::{fonts, input::Input, meshes::Meshes};
use std::{
    collections::BTreeSet,
    path::PathBuf,
    sync::{Arc, atomic::Ordering},
};

enum Row {
    Header { group: usize, progress: String },
    Boss(usize),
}

fn status_width(ui: &egui::Ui) -> f32 {
    (ui.text_style_height(&egui::TextStyle::Body) + 6.).max(24.)
}

fn chevron(painter: &egui::Painter, rect: egui::Rect, open: bool, color: Color32) {
    let center = rect.center();
    let r = rect.height().min(rect.width()) * 0.15;
    let points = if open {
        [
            egui::vec2(-r, -r * 0.5),
            egui::vec2(0., r * 0.5),
            egui::vec2(r, -r * 0.5),
        ]
    } else {
        [
            egui::vec2(-r * 0.5, -r),
            egui::vec2(r * 0.5, 0.),
            egui::vec2(-r * 0.5, r),
        ]
    };
    painter.add(egui::Shape::line(
        points.into_iter().map(|p| center + p).collect(),
        egui::Stroke::new((rect.height() * 0.055).max(1.), color),
    ));
}

fn group_header(
    ui: &mut egui::Ui,
    name: &str,
    progress: &str,
    open: bool,
    current: bool,
    height: f32,
) -> egui::Response {
    let (rect, response) = ui.allocate_exact_size(
        egui::vec2(ui.available_width(), height),
        egui::Sense::click(),
    );
    response.widget_info(|| {
        egui::WidgetInfo::selected(
            egui::WidgetType::CollapsingHeader,
            ui.is_enabled(),
            open,
            name,
        )
    });
    let visuals = ui.visuals();
    let bg = if response.hovered() || response.has_focus() {
        visuals.widgets.hovered.weak_bg_fill
    } else if current {
        visuals.selection.bg_fill.gamma_multiply(0.16)
    } else if open {
        visuals
            .widgets
            .noninteractive
            .weak_bg_fill
            .gamma_multiply(0.35)
    } else {
        Color32::TRANSPARENT
    };
    let painter = ui.painter();
    painter.rect_filled(rect, 3., bg);
    if current {
        let x = rect.left() + 1.;
        painter.line_segment(
            [
                egui::pos2(x, rect.top() + 4.),
                egui::pos2(x, rect.bottom() - 4.),
            ],
            egui::Stroke::new(2.0_f32, visuals.selection.stroke.color),
        );
    }
    if response.has_focus() {
        painter.rect_stroke(rect, 3., visuals.selection.stroke, egui::StrokeKind::Inside);
    }
    let padding = height * 0.22;
    chevron(
        painter,
        egui::Rect::from_min_size(
            rect.min + egui::vec2(padding, 0.),
            egui::vec2(height, height),
        ),
        open,
        visuals.text_color(),
    );
    let text_left = rect.left() + height + padding;
    let right = rect.right() - padding;
    let font = egui::TextStyle::Body.resolve(ui.style());
    let count = painter.layout_no_wrap(progress.into(), font, visuals.weak_text_color());
    let count_left = (right - count.size().x).max(text_left);
    let text_width = (count_left - ui.spacing().item_spacing.x - text_left).max(0.);
    let title = egui::WidgetText::from(RichText::new(name).strong()).into_galley(
        ui,
        Some(egui::TextWrapMode::Truncate),
        text_width,
        egui::TextStyle::Body,
    );
    let title_rect = egui::Rect::from_min_max(
        egui::pos2(text_left, rect.top()),
        egui::pos2(text_left + text_width, rect.bottom()),
    );
    painter
        .with_clip_rect(title_rect.intersect(ui.clip_rect()))
        .galley(
            egui::pos2(text_left, rect.center().y - title.size().y * 0.5),
            title,
            visuals.text_color(),
        );
    let count_rect = egui::Rect::from_min_max(
        egui::pos2(count_left, rect.top()),
        egui::pos2(right.max(count_left), rect.bottom()),
    );
    painter
        .with_clip_rect(count_rect.intersect(ui.clip_rect()))
        .galley(
            egui::pos2(count_left, rect.center().y - count.size().y * 0.5),
            count,
            visuals.weak_text_color(),
        );
    response.on_hover_text(name)
}

fn boss_widgets(
    ui: &mut egui::Ui,
    name: &str,
    state: u8,
    enabled: bool,
    width: f32,
) -> (egui::Response, egui::Response) {
    ui.horizontal(|ui| {
        let guide_x = ui.cursor().left() + width * 0.2;
        let guide_top = ui.cursor().top() - ui.spacing().item_spacing.y;
        ui.painter().line_segment(
            [
                egui::pos2(guide_x, guide_top),
                egui::pos2(guide_x, guide_top + width + ui.spacing().item_spacing.y),
            ],
            egui::Stroke::new(
                1.0_f32,
                ui.visuals()
                    .widgets
                    .noninteractive
                    .bg_stroke
                    .color
                    .gamma_multiply(0.5),
            ),
        );
        ui.add_space(width * 0.5);
        let button = ui
            .add_enabled_ui(enabled, |ui| {
                ui.allocate_exact_size(egui::vec2(width, width), egui::Sense::click())
                    .1
            })
            .inner;
        button.widget_info(|| egui::WidgetInfo::labeled(egui::WidgetType::Button, enabled, name));
        let painter = ui.painter();
        let center = button.rect.center();
        let color = ui
            .visuals()
            .text_color()
            .lerp_to_gamma(ui.visuals().weak_text_color(), 0.25);
        if button.hovered() || button.has_focus() {
            painter.circle_filled(
                center,
                width * 0.43,
                ui.visuals().widgets.hovered.weak_bg_fill,
            );
        }
        if button.has_focus() {
            painter.circle_stroke(center, width * 0.43, ui.visuals().selection.stroke);
        }
        let stroke = egui::Stroke::new((width * 0.055).max(1.1), color);
        match state {
            0 => {
                painter.circle_stroke(center, width * 0.19, stroke);
            }
            1 => {
                painter.add(egui::Shape::line(
                    [
                        egui::vec2(-0.22, 0.),
                        egui::vec2(-0.05, 0.16),
                        egui::vec2(0.25, -0.19),
                    ]
                    .into_iter()
                    .map(|p| center + p * width)
                    .collect(),
                    stroke,
                ));
            }
            _ => {
                let r = width * 0.22;
                painter.add(egui::Shape::closed_line(
                    [
                        egui::vec2(0., -r),
                        egui::vec2(r, 0.),
                        egui::vec2(0., r),
                        egui::vec2(-r, 0.),
                    ]
                    .into_iter()
                    .map(|p| center + p)
                    .collect(),
                    stroke,
                ));
                painter.circle_filled(center, (width * 0.035).max(1.), color);
            }
        }
        let color = if state == 1 {
            ui.visuals()
                .text_color()
                .lerp_to_gamma(ui.visuals().weak_text_color(), 0.4)
        } else {
            ui.visuals().text_color()
        };
        let label = ui.add(egui::Label::new(RichText::new(name).color(color)).truncate());
        (button, label)
    })
    .inner
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
    selected: Option<u32>,
    closed: BTreeSet<u32>,
    rows: Vec<Row>,
    rows_dirty: bool,
    revision: u64,
    current: Option<u32>,
    catalog_generation: Option<u64>,
    fold_pending: bool,
    modal: Option<(u32, u64, String)>,
    last_sequence: Option<u64>,
    last_snapshot: Option<Arc<Snapshot>>,
    last_visible: bool,
    time: f64,
    style_size: f32,
    #[cfg(feature = "continuity-fixture")]
    probe: crate::probe::Layout,
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
            current: None,
            catalog_generation: None,
            fold_pending: true,
            modal: None,
            last_sequence: None,
            last_snapshot: None,
            last_visible: false,
            time: 0.0,
            style_size: 0.0,
            #[cfg(feature = "continuity-fixture")]
            probe: crate::probe::Layout::default(),
        }
    }
    pub fn reset(&mut self, host: Host) {
        self.meshes.reset(&host.0);
        self.last_sequence = None;
        self.last_snapshot = None;
        self.last_visible = false;
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
    #[cfg(feature = "continuity-fixture")]
    pub(crate) fn probe_state(&self) -> crate::probe::State<'_> {
        crate::probe::State {
            revision: self.revision,
            raw_map: self
                .last_snapshot
                .as_ref()
                .and_then(|s| s.state)
                .map_or(0, |s| s.raw_map_id),
            current: self.current,
            selected: self.selected,
            remaining: self.remaining,
            modal: self.modal.is_some(),
            closed: &self.closed,
            logical_rows: self.rows.len(),
            layout: &self.probe,
        }
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
            "Waiting for game region parameters" => "正在等待游戏地区参数",
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
    fn sync_region(&mut self, s: &Snapshot) {
        let Some(c) = s.catalog.as_ref() else {
            self.catalog_generation = None;
            return;
        };
        // Reload publishes an empty catalogue before the final groups. Do not
        // consume that generation's initial fold state while it is still empty.
        if c.groups.is_empty() {
            if self.catalog_generation.take().is_some() {
                self.rows_dirty = true;
            }
            return;
        }
        if self.catalog_generation != Some(s.catalog_generation) {
            self.catalog_generation = Some(s.catalog_generation);
            self.closed = c.groups.iter().map(|g| g.id).collect();
            self.selected = self
                .selected
                .filter(|id| c.groups.iter().any(|g| g.id == *id));
            self.current = None;
            self.fold_pending = true;
            self.rows_dirty = true;
        }
        let next = s
            .state
            .as_ref()
            .filter(|state| state.valid != 0)
            .and_then(|_| {
                s.region
                    .and_then(|region| c.group_for_region(region))
                    .map(|index| c.groups[index].id)
            });
        if let Some(next) = next
            && self.current != Some(next)
        {
            self.current = Some(next);
            self.fold_pending = true;
            self.rows_dirty = true;
        }
        // Preserve manual choices during a search/filter. Apply the latest valid
        // region once the normal list returns; unavailable state never resets it.
        if self.fold_pending
            && self.query.is_empty()
            && !self.remaining
            && self.selected.is_none()
            && let Some(current) = self.current
        {
            self.closed = c
                .groups
                .iter()
                .filter(|g| g.id != current)
                .map(|g| g.id)
                .collect();
            self.fold_pending = false;
            self.rows_dirty = true;
        }
    }
    fn rebuild(&mut self, s: &Snapshot) {
        self.rows.clear();
        let Some(c) = s.catalog.as_ref() else { return };
        let query = self.query.to_lowercase();
        for (group, g) in c.groups.iter().enumerate() {
            if self.selected.is_some_and(|v| v != g.id) {
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
            self.rows.push(Row::Header {
                group,
                progress: format!("{killed}/{total}"),
            });
            if !self.closed.contains(&g.id) {
                self.rows.extend(relevant.into_iter().map(Row::Boss));
            }
        }
        self.rows_dirty = false;
        self.revision = s.revision;
    }
    fn settings_panel(&mut self, ui: &mut egui::Ui) {
        let chinese = self.chinese;
        let l = |en, zh| if chinese { zh } else { en };
        egui::CollapsingHeader::new(l("Settings", "设置"))
            .icon(|ui, openness, response| {
                chevron(
                    ui.painter(),
                    response.rect,
                    openness >= 0.5,
                    ui.visuals().text_color(),
                );
            })
            .show(ui, |ui| {
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
    pub fn render_latest(&mut self, i: &Instance, frame: &abi::FrameInfo) -> i32 {
        if let Ok(snapshot) = i.snapshot.try_lock() {
            self.last_snapshot = snapshot.clone();
        }
        let Some(snapshot) = self.last_snapshot.clone() else {
            i.host.capture(0);
            return abi::ERR_BUSY;
        };
        self.render(i, &snapshot, frame)
    }
    pub fn hide(&mut self, host: Host) {
        self.last_visible = false;
        self.input.clear();
        self.modal = None;
        host.capture(0);
        host.clear_input();
    }
    pub fn render(&mut self, i: &Instance, s: &Snapshot, frame: &abi::FrameInfo) -> i32 {
        let visibility = i.host.visibility();
        let shown = i.shown.load(Ordering::Relaxed);
        if !shown || visibility == Visibility::Hidden {
            self.hide(i.host);
            return abi::OK;
        }
        if visibility == Visibility::Unknown {
            i.host.capture(0);
            i.host.clear_input();
            self.modal = None;
            if !self.last_visible {
                self.input.clear();
                return abi::OK;
            }
        }
        if self.last_sequence == Some(frame.sequence) {
            return abi::OK;
        }
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
        let full = i.full.load(Ordering::Relaxed);
        let interactive = full && visibility == Visibility::Visible;
        let mut events = [abi::UiInput::default(); abi::MAX_UI_INPUT_EVENTS];
        let mut count = 0;
        if interactive {
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
            i.host.clear_input();
            self.modal = None;
            // Release egui's held keys/buttons too, without accepting new input.
            events[0] = abi::UiInput {
                size: size_of::<abi::UiInput>() as u32,
                kind: abi::UI_INPUT_FOCUS,
                code: 0,
                ..Default::default()
            };
            count = 1;
        }
        self.time += f64::from(frame.delta_seconds.clamp(0.001, 0.1));
        let raw = self
            .input
            .raw(frame, self.time, &events[..count.min(events.len())]);
        if !interactive {
            self.input.clear();
        }
        self.sync_region(s);
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
        #[cfg(feature = "continuity-fixture")]
        self.probe.begin(frame.sequence, full, interactive);
        let output=ctx.run(raw,|ctx|{
            if !full{
                if s.catalog.is_some(){
                    egui::Area::new("boss-compact".into()).anchor(Align2::RIGHT_TOP,[-10.,10.]).interactable(false).show(ctx,|ui|{
                        egui::Frame::window(ui.style()).show(ui,|ui|{ui.label(&s.compact);});
                    });
                }
                return;
            }
            if interactive&&ctx.input(|input|input.key_pressed(egui::Key::Escape))&&self.modal.is_none(){full_open=false;}
            let title=self.label("Boss progress","Boss 进度");
            egui::Window::new(title).id("boss-list".into()).open(&mut full_open).collapsible(false).resizable(interactive).interactable(interactive)
                .default_pos([frame.width-self.settings.width-10.,10.])
                .default_size([self.settings.width,self.settings.height.min(frame.height-20.)])
                .max_size([frame.width.max(280.)-10.,frame.height.max(220.)-10.]).show(ctx,|ui|{
                    ui.label(RichText::new(&s.compact).strong());
                    ui.add(egui::ProgressBar::new(if s.total==0{0.0}else{s.kills as f32/s.total as f32}).desired_height(ui.text_style_height(&egui::TextStyle::Body)+4.).text(format!("{}/{}",s.kills,s.total)));
                    ui.horizontal_wrapped(|ui|{
                        ui.label(format!("{}: {}",self.label("Deaths","死亡"),s.deaths));
                        if let Some(state)=s.state{let sec=state.in_game_time_ms/1000;ui.label(format!("IGT {}:{:02}:{:02}",sec/3600,sec/60%60,sec%60));}
                        if s.settings.challenge_mode{ui.label(format!("PB {}  {} {}",s.best,self.label("Tries","失败尝试"),s.tries));}
                    });
                    let status=self.status(s.status);
                    if !status.is_empty(){
                        let summary=match s.status{
                            "Ready; some game resources are unavailable"=>self.label("Resource warning","资源异常"),
                            "Boss data unavailable; reload game data"=>self.label("Data unavailable","数据不可用"),
                            "Loading Boss data; some requests timed out"=>self.label("Resource timeout","读取超时"),
                            _=>status,
                        };
                        ui.add(egui::Label::new(summary).truncate()).on_hover_text(status);
                    }
                    if s.state.is_none(){ui.label(self.label("Waiting for a valid save","等待有效存档"));}
                    if let Some(result)=s.result{ui.label(if result{self.label("Revived. Travel or reload the area.","已复活。请传送或重新加载区域。")}else{self.label("Revival failed: save or flags changed. Reopen the confirmation.","复活失败：存档或旗标已变化，请重新确认。")});}
                    self.settings_panel(ui);
                    if self.settings.challenge_mode {
                        ui.small(self.label("When switching saves, start a new attempt to set its death baseline.","切换存档后，请开始本轮以设置死亡基线。"));
                        if ui.add_enabled(interactive&&s.state.is_some(),egui::Button::new(self.label("Start this attempt","开始本轮"))).clicked(){Self::queue(i,Action::BeginChallenge);}
                        if s.state.is_some_and(|state|state.graveyard_reached>1){ui.small(self.label("Challenge tracking is waiting for its start flag.","挑战记录正在等待有效起始旗标。"));}
                    }
                    let reload=ui.small_button(self.label("Reload data","重新读取")).on_hover_text(self.label("Reload game data","重新读取游戏数据"));
                    #[cfg(feature = "continuity-fixture")]
                    {self.probe.reload_rect=crate::probe::rect(reload.rect);}
                    if reload.clicked()&&interactive{Self::queue(i,Action::Reload);self.modal=None;}
                    ui.separator();
                    let search=ui.add(egui::TextEdit::singleline(&mut self.query).hint_text(if self.chinese{"搜索 Boss 或地点"}else{"Search boss or location"}));
                    let remaining=ui.checkbox(&mut self.remaining,if self.chinese{"只看未完成"}else{"Remaining only"});
                    let mut filter_changed=search.changed()||remaining.changed();
                    #[cfg(feature = "continuity-fixture")]
                    {self.probe.search_rect=crate::probe::rect(search.rect);self.probe.remaining_rect=crate::probe::rect(remaining.rect);}
                    if let Some(c)=s.catalog.as_ref(){
                        let region=egui::ComboBox::from_id_salt("boss-region").icon(|ui,rect,visuals,_|{chevron(ui.painter(),rect,true,visuals.fg_stroke.color);}).selected_text(self.selected.and_then(|id|c.groups.iter().find(|g|g.id==id)).map(|g|g.name.as_str()).unwrap_or(if self.chinese{"全部地区"}else{"All regions"})).show_ui(ui,|ui|{
                            filter_changed|=ui.selectable_value(&mut self.selected,None,if self.chinese{"全部地区"}else{"All regions"}).changed();
                            for group in &c.groups{filter_changed|=ui.selectable_value(&mut self.selected,Some(group.id),&group.name).changed();}
                        });
                        #[cfg(feature = "continuity-fixture")]
                        {self.probe.region_rect=crate::probe::rect(region.response.rect);}
                        #[cfg(not(feature = "continuity-fixture"))]
                        let _=region;
                        if c.unknown_count>0{ui.small(format!("{}: {}",self.label("Unresolved completion conditions","尚未确定的完成条件"),c.unknown_count));}
                    }
                    if filter_changed{self.sync_region(s);self.rows_dirty=true;self.rebuild(s);}
                    let status_width=status_width(ui);
                    egui::ScrollArea::vertical().auto_shrink([false,false]).show_rows(ui,status_width,self.rows.len(),|ui,range|{
                        let Some(c)=s.catalog.as_ref()else{return};
                        for index in range{match &self.rows[index]{
                            Row::Header{group,progress}=>{
                                let id=c.groups[*group].id;
                                let header=ui.push_id(("boss-group",id),|ui|group_header(ui,&c.groups[*group].name,progress,!self.closed.contains(&id),self.current==Some(id),status_width)).inner;
                                #[cfg(feature = "continuity-fixture")]
                                self.probe.header(id,&header);
                                if header.clicked()&&interactive{if !self.closed.remove(&id){self.closed.insert(id);}self.rows_dirty=true;}
                            }
                            Row::Boss(row)=>{
                                let row=*row;
                                let boss=&c.rows[row];let state=s.states.get(row).copied().unwrap_or(2);
                                let enabled=interactive&&self.settings.allow_revive&&state==1&&s.revivable.get(row)==Some(&1)&&s.state.is_some();
                                let (status,name)=ui.push_id(("boss-row",row),|ui|boss_widgets(ui,&boss.name,state,enabled,status_width)).inner;
                                #[cfg(feature = "continuity-fixture")]
                                self.probe.boss_row(boss,c.groups[boss.group].id,state,&status,&name);
                                let hint=match (state,enabled){
                                    (1,true)=>self.label("Defeated. Click to revive this boss.","已击杀，点击复活此 Boss"),
                                    (1,false)=>self.label("Defeated. Revival is unavailable or disabled.","已击杀，复活未启用或当前不可用"),
                                    (0,_)=>self.label("Not defeated","未击杀"),
                                    _=>self.label("Completion state unknown","完成状态未知"),
                                };
                                if status.on_hover_text(hint).on_disabled_hover_text(hint).clicked()&&enabled{
                                    self.modal=Some((boss.flag,s.state.unwrap().context,boss.name.clone()));
                                }
                                name.on_hover_ui(|ui|{
                                    ui.label(&boss.name);
                                    ui.label(format!("{}: {}",self.label("Location reference","参考地点"),boss.place));
                                    ui.small(format!("Flag {}",boss.flag));
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
            if let Some(pos)=ctx.pointer_latest_pos().filter(|_|interactive){
                let p=ctx.layer_painter(egui::LayerId::new(egui::Order::Tooltip,"boss-cursor".into()));
                p.line_segment([pos,pos+egui::vec2(0.,16.)],egui::Stroke::new(2.0_f32,Color32::WHITE));
                p.line_segment([pos,pos+egui::vec2(10.,11.)],egui::Stroke::new(2.0_f32,Color32::WHITE));
                p.line_segment([pos+egui::vec2(0.,16.),pos+egui::vec2(10.,11.)],egui::Stroke::new(2.0_f32,Color32::WHITE));
            }
        });
        // A built egui frame cannot consume input twice, even if mesh submission
        // then fails. The core retains the last complete paint for that case.
        self.last_sequence = Some(frame.sequence);
        self.last_visible = true;
        if !interactive {
            full_open = full;
        }
        if !full_open {
            i.full.store(false, Ordering::Relaxed);
            i.host.capture(0);
            i.host.clear_input();
            self.modal = None;
        }
        // Dragging controls changes only local state. Persist once after release
        // or when closing the panel; the render callback never performs IO.
        if interactive
            && self.settings_dirty
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
    use std::{
        cell::{Cell, RefCell},
        ffi::c_void,
        sync::{Arc, Mutex, atomic::AtomicBool},
    };

    struct Callbacks {
        display: Cell<abi::DisplayState>,
        display_code: Cell<i32>,
        capture: Cell<u32>,
        clears: Cell<usize>,
        events: RefCell<Vec<abi::UiInput>>,
        options_code: Cell<i32>,
        mesh_code: Cell<i32>,
        meshes: Cell<usize>,
        state: Cell<Option<abi::BossState>>,
        state_reads: Cell<usize>,
        flag_reads: Cell<usize>,
        revivals: Cell<usize>,
    }
    struct Harness {
        callbacks: Box<Callbacks>,
        instance: Box<Instance>,
        frame: abi::FrameInfo,
    }
    impl Harness {
        fn new() -> Self {
            unsafe fn callbacks<'a>(user: *mut c_void) -> &'a Callbacks {
                // SAFETY: the harness keeps this boxed callback state alive.
                unsafe { &*user.cast::<Callbacks>() }
            }
            unsafe extern "C" fn display(user: *mut c_void, out: *mut abi::DisplayState) -> i32 {
                let c = unsafe { callbacks(user) };
                // SAFETY: Host::visibility supplies a complete output slot.
                unsafe { out.write(c.display.get()) };
                c.display_code.get()
            }
            unsafe extern "C" fn capture(user: *mut c_void, mask: u32) -> i32 {
                unsafe { callbacks(user) }.capture.set(mask);
                abi::OK
            }
            unsafe extern "C" fn clear(user: *mut c_void) -> i32 {
                let c = unsafe { callbacks(user) };
                c.clears.set(c.clears.get() + 1);
                c.events.borrow_mut().clear();
                abi::OK
            }
            unsafe extern "C" fn input(
                user: *mut c_void,
                out: *mut abi::UiInput,
                len: usize,
                count: *mut usize,
            ) -> i32 {
                let c = unsafe { callbacks(user) };
                let mut events = c.events.borrow_mut();
                let n = len.min(events.len());
                // SAFETY: Ui::render owns len writable events and one count slot.
                unsafe {
                    std::ptr::copy_nonoverlapping(events.as_ptr(), out, n);
                    count.write(n);
                }
                events.drain(..n);
                abi::OK
            }
            unsafe extern "C" fn options(user: *mut c_void, _: *const abi::RenderOptions) -> i32 {
                unsafe { callbacks(user) }.options_code.get()
            }
            unsafe extern "C" fn texture(_: *mut c_void, _: u32, _: u32, _: abi::ByteSlice) -> u64 {
                1
            }
            unsafe extern "C" fn poll(_: *mut c_void, _: u64, out: *mut abi::TextureView) -> i32 {
                // SAFETY: Meshes supplies one complete TextureView slot.
                unsafe {
                    out.write(abi::TextureView {
                        status: abi::TEXTURE_READY,
                        ..Default::default()
                    })
                };
                abi::OK
            }
            unsafe extern "C" fn mesh(user: *mut c_void, _: *const abi::Mesh) -> i32 {
                let c = unsafe { callbacks(user) };
                c.meshes.set(c.meshes.get() + 1);
                c.mesh_code.get()
            }
            unsafe extern "C" fn state(user: *mut c_void, out: *mut abi::BossState) -> i32 {
                let c = unsafe { callbacks(user) };
                c.state_reads.set(c.state_reads.get() + 1);
                let Some(value) = c.state.get() else {
                    return abi::ERR_UNAVAILABLE;
                };
                // SAFETY: Host::state supplies one complete BossState slot.
                unsafe { out.write(value) };
                abi::OK
            }
            unsafe extern "C" fn flags(
                user: *mut c_void,
                _: *const u32,
                out: *mut u8,
                len: usize,
            ) -> i32 {
                let c = unsafe { callbacks(user) };
                c.flag_reads.set(c.flag_reads.get() + 1);
                // SAFETY: Host::flags supplies len writable output bytes.
                unsafe { std::slice::from_raw_parts_mut(out, len).fill(1) };
                abi::OK
            }
            unsafe extern "C" fn revive(
                user: *mut c_void,
                _: u64,
                _: u32,
                _: *const abi::FlagWrite,
                _: usize,
            ) -> i32 {
                let c = unsafe { callbacks(user) };
                c.revivals.set(c.revivals.get() + 1);
                abi::OK
            }
            let callbacks = Box::new(Callbacks {
                display: Cell::new(abi::DisplayState {
                    valid: 1,
                    ..Default::default()
                }),
                display_code: Cell::new(abi::OK),
                capture: Cell::new(0),
                clears: Cell::new(0),
                events: RefCell::new(Vec::new()),
                options_code: Cell::new(abi::OK),
                mesh_code: Cell::new(abi::OK),
                meshes: Cell::new(0),
                state: Cell::new(None),
                state_reads: Cell::new(0),
                flag_reads: Cell::new(0),
                revivals: Cell::new(0),
            });
            // SAFETY: this table consists of integers and nullable pointers. The
            // synchronous test uses only these populated callbacks.
            let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
            api.user = (&*callbacks as *const Callbacks).cast_mut().cast();
            api.read_display_state = Some(display);
            api.ui_input_capture = Some(capture);
            api.ui_input_clear = Some(clear);
            api.ui_input_poll = Some(input);
            api.set_render_options = Some(options);
            api.texture_create_rgba = Some(texture);
            api.texture_poll = Some(poll);
            api.submit_mesh = Some(mesh);
            api.read_boss_state = Some(state);
            api.read_event_flags = Some(flags);
            api.revive_boss = Some(revive);
            let host = Host(api);
            let settings = Arc::new(Settings {
                allow_revive: true,
                ..Default::default()
            });
            let catalog = Arc::new(Catalog {
                groups: vec![Group {
                    id: 10_000,
                    name: "Test group".into(),
                }],
                rows: vec![Encounter {
                    flag: 10,
                    completion: vec![10],
                    name: "Test boss".into(),
                    place: "Test place".into(),
                    group: 0,
                    map_id: 0x0a00_0000,
                    search: "test boss".into(),
                }],
                unknown_count: 0,
                flag_ids: vec![10],
            });
            let snapshot = Arc::new(Snapshot {
                catalog: Some(catalog.clone()),
                catalog_generation: 1,
                region: Some(1_000_000),
                states: vec![1].into(),
                revivable: vec![1].into(),
                state: Some(abi::BossState {
                    size: size_of::<abi::BossState>() as u32,
                    valid: 1,
                    context: 42,
                    raw_map_id: 0x0a00_0000,
                    in_game_time_ms: 10_000,
                    ..Default::default()
                }),
                settings: settings.clone(),
                kills: 1,
                total: 1,
                deaths: 0,
                best: 0,
                tries: 0,
                compact: "1/1".into(),
                status: "Ready",
                revision: 1,
                result: None,
            });
            let instance = Box::new(Instance {
                host,
                update: Mutex::new(Update {
                    data: Data::new(host),
                    root: PathBuf::new(),
                    settings: settings.clone(),
                    keys: [0; 2],
                    catalog: Some(catalog),
                    catalog_revision: 0,
                    challenge: Challenge::default(),
                    revival: Revival::default(),
                    next: None,
                    revision: 0,
                    result: None,
                    flag_values: Vec::new(),
                }),
                ui: Mutex::new(Ui::new(host, &settings)),
                snapshot: Mutex::new(Some(snapshot)),
                actions: Mutex::new(Vec::new()),
                full: AtomicBool::new(true),
                shown: AtomicBool::new(true),
                hide_pending: AtomicBool::new(false),
            });
            Self {
                callbacks,
                instance,
                frame: abi::FrameInfo {
                    size: size_of::<abi::FrameInfo>() as u32,
                    layer: abi::LAYER_ABOVE_UI,
                    width: 1280.,
                    height: 900.,
                    pixels_per_point: 1.,
                    delta_seconds: 1. / 60.,
                    ..Default::default()
                },
            }
        }
        fn render(&mut self) -> i32 {
            self.frame.sequence += 1;
            self.render_same_sequence()
        }
        fn render_same_sequence(&self) -> i32 {
            self.callbacks.meshes.set(0);
            // SAFETY: the boxed instance and complete frame live throughout render.
            unsafe {
                crate::render(
                    (&*self.instance as *const Instance).cast_mut().cast(),
                    &self.frame,
                )
            }
        }
        fn warm(&mut self) {
            for _ in 0..3 {
                assert_eq!(self.render(), abi::OK);
            }
            assert!(self.callbacks.meshes.get() > 0);
        }
    }

    #[test]
    fn actual_egui_status_column_aligns_all_states_and_long_names_at_each_font_size() {
        let h = Harness::new();
        let context = h.instance.ui.lock().unwrap().context.clone();
        for (size, scale) in [(12., 1.), (20., 1.), (40., 1.), (20., 1.5), (20., 2.)] {
            context.set_pixels_per_point(scale);
            context.set_style(egui::Style {
                text_styles: [egui::TextStyle::Body, egui::TextStyle::Button]
                    .map(|kind| (kind, egui::FontId::proportional(size)))
                    .into(),
                ..Default::default()
            });
            let mut positions = Vec::new();
            let raw = egui::RawInput {
                screen_rect: Some(egui::Rect::from_min_size(
                    egui::Pos2::ZERO,
                    egui::vec2(280., 500.),
                )),
                ..Default::default()
            };
            let _ = context.run(raw, |ctx| {
                egui::CentralPanel::default().show(ctx, |ui| {
                    let width = status_width(ui);
                    for state in [0, 1, 2] {
                        let (status, name) = boss_widgets(
                            ui,
                            "A very long Boss name that must keep its hover text",
                            state,
                            state == 1,
                            width,
                        );
                        positions.push((status.rect, name.rect));
                    }
                });
            });
            assert_eq!(positions.len(), 3);
            for (status, name) in &positions {
                assert_eq!(status.width(), positions[0].0.width());
                assert_eq!(name.left(), positions[0].1.left());
                assert!(name.left() > status.right());
                assert!(
                    name.width() > 0. && name.width() < 280.,
                    "name={name:?}, status={status:?}, size={size}"
                );
            }
        }
    }

    #[test]
    fn actual_group_header_reserves_progress_and_a_full_row_hit_rect_at_narrow_widths() {
        let h = Harness::new();
        let context = h.instance.ui.lock().unwrap().context.clone();
        let name = "A long region title that must truncate before its progress";
        for size in [12., 20., 40.] {
            context.set_style(egui::Style {
                text_styles: [(egui::TextStyle::Body, egui::FontId::proportional(size))].into(),
                ..Default::default()
            });
            for width in [280., 440.] {
                let mut rects = Vec::new();
                let output = context.run(
                    egui::RawInput {
                        screen_rect: Some(egui::Rect::from_min_size(
                            egui::Pos2::ZERO,
                            egui::vec2(width, 500.),
                        )),
                        ..Default::default()
                    },
                    |ctx| {
                        egui::CentralPanel::default().show(ctx, |ui| {
                            let height = status_width(ui);
                            for (open, current) in [(false, false), (true, false), (true, true)] {
                                let available = ui.available_width();
                                let header =
                                    group_header(ui, name, "12/207", open, current, height);
                                assert_eq!(header.rect.width(), available);
                                assert_eq!(header.rect.height(), height);
                                rects.push(header.rect);
                            }
                        });
                    },
                );
                let texts: Vec<_> = output
                    .shapes
                    .iter()
                    .filter_map(|shape| {
                        if let egui::Shape::Text(text) = &shape.shape {
                            Some((shape.clip_rect, text))
                        } else {
                            None
                        }
                    })
                    .collect();
                assert_eq!(texts.len(), 6);
                for (index, pair) in texts.as_chunks::<2>().0.iter().enumerate() {
                    let (title_clip, title) = pair[0];
                    let (progress_clip, progress) = pair[1];
                    assert_eq!(title.galley.job.text, name);
                    assert_eq!(progress.galley.job.text, "12/207");
                    assert!(title_clip.right() < progress_clip.left());
                    assert!(progress.pos.x + progress.galley.size().x <= rects[index].right());
                    if width == 280. {
                        assert!(title.galley.elided);
                    }
                }
            }
        }
    }

    fn regional_snapshot(h: &Harness) -> Snapshot {
        let snapshot = h.instance.snapshot.lock().unwrap().take().unwrap();
        let mut s = Arc::try_unwrap(snapshot).ok().unwrap();
        let groups = [61_000, 10_000, 62_000]
            .map(|id| Group {
                id,
                name: format!("Region {id}"),
            })
            .into();
        let rows = (0..3)
            .map(|group| Encounter {
                flag: group as u32 + 10,
                completion: vec![group as u32 + 10],
                name: format!("Boss {group}"),
                place: String::new(),
                group,
                map_id: 0x3c00_0000,
                search: format!("boss {group}"),
            })
            .collect();
        s.catalog = Some(Arc::new(Catalog {
            groups,
            rows,
            ..Default::default()
        }));
        s.region = Some(6_100_000);
        s.states = vec![0; 3].into();
        s
    }

    #[test]
    fn region_changes_close_other_groups_once_and_unknown_preserves_manual_choices() {
        let h = Harness::new();
        let mut s = regional_snapshot(&h);
        let mut ui = h.instance.ui.lock().unwrap();
        ui.sync_region(&s);
        assert_eq!(ui.current, Some(61_000));
        assert_eq!(ui.closed, BTreeSet::from([10_000, 62_000]));
        ui.closed.remove(&10_000);
        ui.closed.insert(61_000);
        let manual = ui.closed.clone();
        for raw_map in [0x3c2a_2400, 0x3c2b_2400, 0x3c2b_2401] {
            s.state.as_mut().unwrap().raw_map_id = raw_map;
            ui.sync_region(&s);
            assert_eq!(ui.closed, manual, "same region, different tiles/variants");
        }
        let state = s.state.take();
        s.region = None;
        ui.sync_region(&s);
        assert_eq!(ui.closed, manual);
        s.state = state;
        s.region = Some(u32::MAX);
        ui.sync_region(&s);
        assert_eq!(ui.closed, manual, "unrecognized region is not a transition");
        s.region = Some(6_100_000);
        ui.sync_region(&s);
        assert_eq!(
            ui.closed, manual,
            "recovering the same region preserves manual choices"
        );
        s.region = Some(6_200_000);
        ui.sync_region(&s);
        assert_eq!(ui.current, Some(62_000));
        assert_eq!(ui.closed, BTreeSet::from([10_000, 61_000]));
    }

    #[test]
    fn first_catalog_reload_and_filters_apply_only_the_latest_valid_region() {
        let h = Harness::new();
        let mut s = regional_snapshot(&h);
        let catalog = s.catalog.take();
        let mut ui = h.instance.ui.lock().unwrap();
        ui.sync_region(&s);
        assert_eq!(ui.current, None);
        s.catalog = catalog;
        ui.sync_region(&s);
        assert_eq!(ui.current, Some(61_000));
        ui.closed.clear();
        ui.query = "boss".into();
        s.region = Some(1_000_000);
        ui.sync_region(&s);
        s.region = Some(6_200_000);
        ui.sync_region(&s);
        assert!(ui.closed.is_empty(), "search must not collapse its results");
        ui.query.clear();
        ui.remaining = true;
        ui.sync_region(&s);
        assert!(ui.closed.is_empty());
        ui.remaining = false;
        ui.selected = Some(61_000);
        ui.sync_region(&s);
        assert!(ui.closed.is_empty());
        ui.selected = None;
        ui.sync_region(&s);
        assert_eq!(ui.closed, BTreeSet::from([10_000, 61_000]));
        ui.closed.clear();
        s.revision += 1;
        ui.sync_region(&s);
        assert!(
            ui.closed.is_empty(),
            "progress/text updates preserve manual folds"
        );
        s.catalog_generation += 1;
        ui.sync_region(&s);
        assert_eq!(
            ui.closed,
            BTreeSet::from([10_000, 61_000]),
            "reload in the same region reinitializes once"
        );
        ui.closed.clear();
        ui.sync_region(&s);
        assert!(ui.closed.is_empty());
        s.catalog_generation += 1;
        s.region = None;
        ui.sync_region(&s);
        assert_eq!(ui.current, None);
        assert_eq!(ui.closed.len(), 3);
        s.region = Some(1_000_000);
        ui.sync_region(&s);
        assert_eq!(ui.closed, BTreeSet::from([61_000, 62_000]));
    }

    #[test]
    fn an_empty_reload_publication_does_not_consume_initial_folds_before_ready_without_a_save() {
        let h = Harness::new();
        let mut s = regional_snapshot(&h);
        let mut ui = h.instance.ui.lock().unwrap();
        ui.sync_region(&s);
        ui.closed.clear();
        let catalog = s.catalog.replace(Arc::new(Catalog::default()));
        s.catalog_generation += 1;
        s.state = None;
        s.region = None;
        ui.sync_region(&s);
        assert_eq!(ui.catalog_generation, None);
        s.catalog = catalog;
        ui.sync_region(&s);
        assert_eq!(ui.closed, BTreeSet::from([61_000, 10_000, 62_000]));
        assert_eq!(ui.current, None);
        s.state = Some(abi::BossState {
            valid: 1,
            ..Default::default()
        });
        s.region = Some(6_100_000);
        ui.sync_region(&s);
        assert_eq!(ui.closed, BTreeSet::from([10_000, 62_000]));
    }

    #[test]
    fn snapshot_contention_keeps_the_published_panel_and_first_snapshot_remains_retryable() {
        let mut h = Harness::new();
        h.warm();
        let old = h.instance.ui.lock().unwrap().last_snapshot.clone().unwrap();
        let slot = h.instance.snapshot.lock().unwrap();
        h.frame.sequence += 1;
        assert_eq!(h.render_same_sequence(), abi::OK);
        assert!(
            h.callbacks.meshes.get() > 0,
            "a busy snapshot must not erase the panel"
        );
        assert!(Arc::ptr_eq(
            &old,
            h.instance
                .ui
                .lock()
                .unwrap()
                .last_snapshot
                .as_ref()
                .unwrap()
        ));
        drop(slot);
        let mut first = Harness::new();
        let snapshot = first.instance.snapshot.lock().unwrap().take();
        assert_eq!(first.render(), abi::ERR_BUSY);
        assert_eq!(first.instance.ui.lock().unwrap().last_sequence, None);
        *first.instance.snapshot.lock().unwrap() = snapshot;
        assert_eq!(first.render_same_sequence(), abi::OK);
        assert!(first.instance.ui.lock().unwrap().last_snapshot.is_some());
    }

    #[test]
    fn explicit_hide_precedes_a_busy_ui_and_reopening_unknown_does_not_restore_the_panel() {
        let mut h = Harness::new();
        h.warm();
        h.frame.sequence += 1;
        let ui = h.instance.ui.lock().unwrap();
        h.instance.shown.store(false, Ordering::Relaxed);
        assert_eq!(h.render_same_sequence(), abi::OK);
        assert_eq!(h.callbacks.meshes.get(), 0);
        assert_eq!(h.callbacks.capture.get(), 0);
        assert!(h.instance.hide_pending.load(Ordering::Relaxed));
        drop(ui);
        h.instance.shown.store(true, Ordering::Relaxed);
        h.callbacks.display_code.set(abi::ERR_UNAVAILABLE);
        assert_eq!(h.render(), abi::OK);
        assert_eq!(h.callbacks.meshes.get(), 0);
        assert!(!h.instance.hide_pending.load(Ordering::Relaxed));
        assert!(!h.instance.ui.lock().unwrap().last_visible);
        h.callbacks.display_code.set(abi::OK);
        assert_eq!(h.render(), abi::OK);
        assert!(h.callbacks.meshes.get() > 0);
    }

    #[test]
    fn unknown_display_preserves_only_an_existing_panel_without_input_and_known_hides_clear_it() {
        let mut h = Harness::new();
        h.callbacks.display_code.set(abi::ERR_UNAVAILABLE);
        assert_eq!(h.render(), abi::OK);
        assert_eq!(h.callbacks.meshes.get(), 0);
        assert!(!h.instance.ui.lock().unwrap().last_visible);
        h.callbacks.display_code.set(abi::OK);
        h.warm();
        assert_eq!(
            h.callbacks.capture.get(),
            abi::UI_CAPTURE_MOUSE | abi::UI_CAPTURE_KEYBOARD
        );
        for code in [abi::ERR_UNAVAILABLE, abi::ERR_BUSY, abi::OK] {
            h.instance.ui.lock().unwrap().modal = Some((10, 42, "Test boss".into()));
            h.callbacks.events.borrow_mut().push(abi::UiInput {
                size: size_of::<abi::UiInput>() as u32,
                kind: abi::UI_INPUT_KEY,
                code: 0x1b,
                flags: abi::UI_INPUT_DOWN,
                ..Default::default()
            });
            h.callbacks.display_code.set(code);
            h.callbacks.display.set(abi::DisplayState::default());
            assert_eq!(h.render(), abi::OK);
            assert!(h.callbacks.meshes.get() > 0);
            assert_eq!(h.callbacks.capture.get(), 0);
            assert!(h.callbacks.events.borrow().is_empty());
            assert!(h.instance.actions.lock().unwrap().is_empty());
            let ui = h.instance.ui.lock().unwrap();
            assert!(ui.modal.is_none());
            assert!(!ui.context.input(|input| input.focused));
            assert!(h.instance.full.load(Ordering::Relaxed));
        }
        for display in [
            abi::DisplayState {
                valid: 1,
                loading_or_logo: 1,
                full_screen_map: 0,
            },
            abi::DisplayState {
                valid: 1,
                loading_or_logo: 0,
                full_screen_map: 1,
            },
        ] {
            h.callbacks.display_code.set(abi::OK);
            h.callbacks.display.set(display);
            assert_eq!(h.render(), abi::OK);
            assert_eq!(h.callbacks.meshes.get(), 0);
            h.callbacks.display_code.set(abi::ERR_UNAVAILABLE);
            assert_eq!(h.render(), abi::OK);
            assert_eq!(
                h.callbacks.meshes.get(),
                0,
                "unknown cannot reopen a known hidden panel"
            );
            h.callbacks.display_code.set(abi::OK);
            h.callbacks.display.set(abi::DisplayState {
                valid: 1,
                ..Default::default()
            });
            assert_eq!(h.render(), abi::OK);
            assert!(h.callbacks.meshes.get() > 0);
        }
        h.instance.shown.store(false, Ordering::Relaxed);
        assert_eq!(h.render(), abi::OK);
        assert_eq!(h.callbacks.meshes.get(), 0);
        h.instance.shown.store(true, Ordering::Relaxed);
        h.callbacks.display_code.set(abi::ERR_UNAVAILABLE);
        assert_eq!(h.render(), abi::OK);
        assert_eq!(h.callbacks.meshes.get(), 0);
        h.callbacks.display_code.set(abi::OK);
        assert_eq!(h.render(), abi::OK);
        assert!(h.callbacks.meshes.get() > 0);
        h.callbacks.events.borrow_mut().push(abi::UiInput {
            size: size_of::<abi::UiInput>() as u32,
            kind: abi::UI_INPUT_KEY,
            code: 0x1b,
            flags: abi::UI_INPUT_DOWN,
            ..Default::default()
        });
        assert_eq!(h.render(), abi::OK);
        assert!(!h.instance.full.load(Ordering::Relaxed));
        assert_eq!(h.callbacks.capture.get(), 0);
        h.instance.full.store(true, Ordering::Relaxed);
        assert_eq!(h.render(), abi::OK);
        assert!(h.callbacks.meshes.get() > 0);
    }

    #[test]
    fn preframe_failure_can_retry_but_a_built_frame_never_advances_twice() {
        let mut h = Harness::new();
        h.warm();
        let before = h.instance.ui.lock().unwrap().context.cumulative_frame_nr();
        h.callbacks.options_code.set(abi::ERR_BUSY);
        assert_eq!(h.render(), abi::ERR_BUSY);
        assert_eq!(
            h.instance.ui.lock().unwrap().context.cumulative_frame_nr(),
            before
        );
        h.callbacks.options_code.set(abi::OK);
        assert_eq!(h.render_same_sequence(), abi::OK);
        assert!(h.callbacks.meshes.get() > 0);
        let built = h.instance.ui.lock().unwrap().context.cumulative_frame_nr();
        assert!(built > before);
        assert_eq!(h.render_same_sequence(), abi::OK);
        assert_eq!(
            h.instance.ui.lock().unwrap().context.cumulative_frame_nr(),
            built
        );
        h.callbacks.mesh_code.set(abi::ERR_BUSY);
        assert_eq!(h.render(), abi::ERR_BUSY);
        let built = h.instance.ui.lock().unwrap().context.cumulative_frame_nr();
        h.callbacks.mesh_code.set(abi::OK);
        assert_eq!(h.render_same_sequence(), abi::OK);
        assert_eq!(
            h.instance.ui.lock().unwrap().context.cumulative_frame_nr(),
            built
        );
        assert_eq!(
            h.callbacks.meshes.get(),
            0,
            "the core replays paint after post-build failure"
        );
    }

    #[test]
    fn cached_visual_state_never_authorizes_revival_without_a_fresh_matching_context() {
        let mut h = Harness::new();
        h.warm();
        for fresh in [
            None,
            Some(abi::BossState {
                size: size_of::<abi::BossState>() as u32,
                valid: 1,
                context: 43,
                in_game_time_ms: 10_000,
                ..Default::default()
            }),
        ] {
            h.callbacks.state.set(fresh);
            h.callbacks.flag_reads.set(0);
            h.instance.actions.lock().unwrap().push(Action::Revive {
                flag: 10,
                context: 42,
            });
            // SAFETY: the boxed instance remains valid through the real update callback.
            assert_eq!(
                unsafe { crate::update((&*h.instance as *const Instance).cast_mut().cast()) },
                abi::OK
            );
            assert_eq!(h.callbacks.revivals.get(), 0);
            assert_eq!(
                h.callbacks.flag_reads.get(),
                usize::from(fresh.is_some()),
                "only normal fresh progress reads are permitted"
            );
            assert_eq!(h.instance.update.lock().unwrap().result, Some(false));
        }
        assert!(h.callbacks.state_reads.get() >= 2);
    }

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
        // snapshots and accepted atlas handles; test only list construction.
        unsafe extern "C" fn atlas(_: *mut c_void, _: u32, _: u32, _: abi::ByteSlice) -> u64 {
            1
        }
        let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
        api.read_display_state = Some(visible);
        api.texture_create_rgba = Some(atlas);
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
            hide_pending: AtomicBool::new(false),
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
            catalog_generation: 1,
            region: None,
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
        // This stress catalogue has synthetic groups, already manually opened.
        // Region lifecycle is covered separately with real major-region keys.
        ui.catalog_generation = Some(1);
        ui.fold_pending = false;
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
        assert!(matches!(&ui.rows[0],Row::Header {progress,..} if progress=="3/3"));

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
            assert!(
                ui.revision != snapshot.revision,
                "pending changes must survive compact renders"
            );
            assert!(matches!(&ui.rows[0],Row::Header {progress,..} if progress=="3/3"));
        }
        instance.full.store(true, Ordering::Relaxed);
        frame.sequence += 1;
        assert_eq!(ui.render(&instance, &snapshot, &frame), abi::OK);
        assert_eq!(ui.rows.len(), 95 + 211);
        assert_eq!(ui.revision, 8);
        assert!(!ui.rows_dirty);
        assert!(matches!(&ui.rows[0],Row::Header {progress,..} if progress=="0/3"));
    }
}
