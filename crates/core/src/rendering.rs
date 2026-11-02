use super::*;

impl Runtime {
    fn display_hidden(&self) -> Option<bool> {
        let mut display = DisplayState::default();
        self._api.read_display_state.and_then(|read| {
            // SAFETY: the runtime retains its host and complete POD output.
            ((unsafe { read(self._api.user, &mut display) }) == OK && display.valid != 0)
                .then_some(display.loading_or_logo != 0 || display.full_screen_map != 0)
        })
    }
    fn release_ui_input(&self) {
        if let Some(capture) = self._api.ui_input_capture {
            unsafe { capture(self._api.user, 0) };
        }
        if let Some(clear) = self._api.ui_input_clear {
            unsafe { clear(self._api.user) };
        }
    }
    fn with_presentation(&self, apply: impl FnOnce(&eroverlay_renderer::Presentation)) -> bool {
        let handle = self.presentation.try_read().ok().and_then(|p| p.clone());
        if let Some(presentation) = handle {
            if self.invalidate_presentation.swap(false, Ordering::AcqRel) {
                presentation.invalidate_all();
            }
            apply(&presentation);
            return true;
        }
        false
    }
    fn invalidate_group(&self, key: usize) {
        if !self.with_presentation(|p| p.invalidate(key)) {
            // A failed/hiding group must not reappear through an unrelated
            // early-exit replay. Conservatively retire all groups on recovery.
            self.invalidate_presentation.store(true, Ordering::Release);
        }
    }
    fn retain_upper(&self, frame: &NativeFrame, keys: &[usize]) {
        if frame.layer != LAYER_ABOVE_UI || !self.accepting.load(Ordering::Acquire) {
            return;
        }
        self.with_presentation(|p| {
            // Stop may have drained the GPU while this callback was acquiring
            // the independent presentation handle. Never submit after stop.
            if !self.accepting.load(Ordering::Acquire) {
                return;
            }
            if !p.matches_device(frame) {
                p.invalidate_all();
                return;
            }
            // SAFETY: only an owned image survives frames; the current target is borrowed.
            match unsafe { p.replay(frame, keys) } {
                Ok(stats) => {
                    if stats.retained {
                        self.continuity[4].fetch_add(1, Ordering::Relaxed);
                    }
                    if stats.dropped {
                        self.dropped.fetch_add(1, Ordering::Relaxed);
                    }
                }
                Err(_) => {
                    p.invalidate_all();
                    self.error.store(ERR_UNAVAILABLE, Ordering::Relaxed);
                }
            }
        });
    }
    fn retain_painted_upper(&self, frame: &NativeFrame, paint: &painting::Painting) {
        // Avoid a temporary key vector: group order is the plugin load order.
        for &key in &self.upper_groups {
            let packet = &paint.packets[key - 1];
            if !packet.valid || !packet.staging.has_paint() {
                self.invalidate_group(key);
            } else {
                self.retain_upper(frame, &[key]);
            }
        }
    }
    fn prepare_renderer(&self, frame: &NativeFrame) {
        let Ok(mut state) = self.render.try_lock() else {
            return;
        };
        let identity = (frame.device as usize, frame.queue as usize);
        if state.identity == identity && state.renderer.is_some() {
            // Ordinary callbacks only share-read the published handle. A writer
            // exists only before a new renderer has any valid image, or at stop.
            return;
        }
        let Ok(_exclusive) = self.callbacks.try_write() else {
            return;
        };
        let Ok(mut paint) = self.painting.try_lock() else {
            return;
        };
        self.with_presentation(|p| p.invalidate_all());
        if state.renderer.as_mut().is_some_and(|r| !r.wait_idle(0)) {
            self.dropped.fetch_add(1, Ordering::Relaxed);
            return;
        }
        if let Some(renderer) = state.renderer.as_mut()
            && !self.host.textures.reset(renderer)
        {
            self.diagnostics.reset_failure();
            return;
        }
        state.renderer = None;
        let init = self.diagnostics.stage(logging::Stage::RendererInit);
        // SAFETY: the callback borrows a validated current device, queue and RTV.
        let renderer = unsafe { Renderer::new(frame) };
        drop(init);
        let Ok(renderer) = renderer else {
            self.error.store(ERR_UNAVAILABLE, Ordering::Relaxed);
            return;
        };
        let Ok(mut published) = self.presentation.try_write() else {
            return;
        };
        *published = Some(renderer.presentation_handle());
        drop(published);
        state.renderer = Some(renderer);
        state.identity = identity;
        state.generation += 1;
        paint.reset(state.generation);
        for plugin in &self.plugins {
            plugin.reset(state.generation);
        }
        self.renderer_generation
            .store(state.generation, Ordering::Release);
        self.diagnostics.rebuild();
    }
    pub(super) fn render(&self, frame: &NativeFrame) {
        if !self.accepting.load(Ordering::Acquire) {
            return;
        }
        eroverlay_game::memory::set_tracking(self.host.log.enabled());
        let _total = self.diagnostics.stage(logging::Stage::Render);
        if frame.layer > LAYER_BELOW_UI || self.render_layers & (1 << frame.layer) == 0 {
            self.diagnostics.unconsumed_layer();
            return;
        }
        let hidden = self.display_hidden();
        if let Some(hidden) = hidden {
            self.display_hidden.store(hidden, Ordering::Release);
        }
        if hidden != Some(false) {
            // Unknown state permits visual replay, never a stale input lease.
            // This also runs when plugin/UI/render locks cannot be acquired.
            self.release_ui_input();
        }
        if self.display_hidden.load(Ordering::Acquire) {
            self.continuity[5].fetch_add(1, Ordering::Relaxed);
            self.invalidate_presentation.store(true, Ordering::Release);
            self.with_presentation(|p| p.invalidate_all());
            if let Ok(mut paint) = self.painting.try_lock() {
                paint.reset(self.renderer_generation.load(Ordering::Acquire));
            }
            return;
        }
        self.prepare_renderer(frame);
        let Ok(_shared) = self.callbacks.try_read() else {
            self.diagnostics.gate_skip();
            self.continuity[1].fetch_add(1, Ordering::Relaxed);
            if let Ok(paint) = self.painting.try_lock() {
                self.retain_painted_upper(frame, &paint);
            } else {
                self.retain_upper(frame, &self.upper_groups);
            }
            return;
        };
        if !self.accepting.load(Ordering::Acquire) {
            return;
        }
        let Ok(mut paint) = self.painting.try_lock() else {
            self.continuity[2].fetch_add(1, Ordering::Relaxed);
            self.retain_upper(frame, &self.upper_groups);
            return;
        };
        // Worker diagnostics may briefly own the GPU mutex. UI preparation is
        // independent, so an explicit hide still invalidates the cached image.
        let info = FrameInfo {
            size: size_of::<FrameInfo>() as u32,
            layer: frame.layer,
            sequence: frame.sequence,
            width: frame.width,
            height: frame.height,
            pixels_per_point: 1.0,
            delta_seconds: 1.0 / 60.0,
        };
        for index in 0..self.plugins.len() {
            if self.plugins[index].layer() == frame.layer {
                let _phase = self.diagnostics.stage(logging::Stage::RenderPlugin);
                let code = paint.prepare(index, &self.plugins[index], &info);
                if code == ERR_BUSY {
                    self.continuity[3].fetch_add(1, Ordering::Relaxed);
                } else if code != OK {
                    self.error.store(code, Ordering::Relaxed);
                    self.invalidate_group(index + 1);
                }
            }
        }
        let Ok(mut state) = self.render.try_lock() else {
            self.diagnostics.render_skip();
            self.continuity[0].fetch_add(1, Ordering::Relaxed);
            self.retain_painted_upper(frame, &paint);
            return;
        };
        if state.identity != (frame.device as usize, frame.queue as usize)
            || state.generation != paint.generation
            || state.renderer.is_none()
        {
            self.retain_painted_upper(frame, &paint);
            return;
        }
        let renderer = state.renderer.as_mut().unwrap();
        // A confirmed hide must retire old images even if its original callback
        // could not acquire the presentation lock. Do not publish past it.
        if self.invalidate_presentation.load(Ordering::Acquire) {
            self.with_presentation(|_| {});
            if self.invalidate_presentation.load(Ordering::Acquire) {
                return;
            }
        }
        renderer.reserve_above_slots(self.upper_groups.len());
        {
            let _phase = self.diagnostics.stage(logging::Stage::Transfer);
            #[cfg(feature = "layer-order-fixture")]
            let transfer = !layer_fixture::transfers_paused();
            #[cfg(not(feature = "layer-order-fixture"))]
            let transfer = true;
            if transfer {
                self.host.textures.synchronize(renderer, frame.sequence);
            }
        }
        if !self.plugins.iter().enumerate().any(|(index, plugin)| {
            plugin.layer() == frame.layer
                && paint.packets[index].valid
                && paint.packets[index].fresh
        }) {
            // A refused atlas can make every plugin temporarily busy. Continue
            // bounded uploads without painting, otherwise queued images could
            // retain the source budget that the atlas needs to be admitted.
            let _phase = self.diagnostics.stage(logging::Stage::Gpu);
            if unsafe { renderer.render(frame, &[], RenderOptions::default()) }.is_err() {
                self.error.store(ERR_UNAVAILABLE, Ordering::Relaxed);
                self.invalidate_presentation.store(true, Ordering::Release);
                self.with_presentation(|p| p.invalidate_all());
                state.identity = (0, 0);
                return;
            }
        }
        for position in 0..self.plugins.len() {
            let index = if frame.layer == LAYER_BELOW_UI {
                self.plugins.len() - 1 - position
            } else {
                position
            };
            if self.plugins[index].layer() != frame.layer {
                continue;
            }
            let packet = &mut paint.packets[index];
            if packet.valid
                && packet.fresh
                && frame.layer == LAYER_ABOVE_UI
                && !packet.staging.has_paint()
            {
                // Empty successful output is an explicit hide, including when
                // the renderer's own optional presentation lock is contended.
                self.invalidate_group(index + 1);
            }
            if !packet.valid || !packet.fresh {
                if frame.layer == LAYER_ABOVE_UI {
                    if !packet.valid || !packet.staging.has_paint() {
                        self.invalidate_group(index + 1);
                    } else {
                        self.retain_upper(frame, &[index + 1]);
                    }
                }
                // Lower transmittance must never be applied a second time to
                // the same sequence, nor replay geometry from a stale map.
                continue;
            }
            if packet.fresh {
                self.meshes
                    .fetch_add(packet.staging.mesh_count() as u64, Ordering::Relaxed);
            }
            let options = packet.staging.options;
            let meshes = packet.staging.meshes();
            let _phase = self.diagnostics.stage(logging::Stage::Gpu);
            // SAFETY: staging contains only core-owned copies for this callback.
            match unsafe { renderer.render_with_group(frame, meshes, options, index + 1) } {
                Ok(stats) => {
                    if stats.retained {
                        self.continuity[4].fetch_add(1, Ordering::Relaxed);
                    }
                    if stats.dropped {
                        self.dropped.fetch_add(1, Ordering::Relaxed);
                    }
                }
                Err(_) => {
                    self.error.store(ERR_UNAVAILABLE, Ordering::Relaxed);
                    self.with_presentation(|p| p.invalidate_all());
                    state.identity = (0, 0);
                    break;
                }
            }
        }
        self.gpu_bytes.store(
            state.renderer.as_ref().map_or(0, Renderer::committed_bytes),
            Ordering::Relaxed,
        );
        self.renders.fetch_add(1, Ordering::Relaxed);
    }
}
