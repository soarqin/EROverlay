use eroverlay_abi as abi;
#[derive(Clone, Copy)]
pub struct Host(pub abi::HostApi);
// SAFETY: the immutable host table/user outlive this plugin. Render uses only
// callbacks explicitly documented as nonblocking and render-safe.
unsafe impl Send for Host {}
unsafe impl Sync for Host {}
impl Host {
    pub fn config(self, key: &str) -> Option<String> {
        let f = self.0.config_get?;
        let mut out = [0u8; 4096];
        let n = unsafe {
            f(
                self.0.user,
                abi::ByteSlice::new(key.as_bytes()),
                out.as_mut_ptr(),
                out.len(),
            )
        };
        (n <= out.len())
            .then(|| String::from_utf8(out[..n].to_vec()).ok())
            .flatten()
    }
    pub fn log(self, text: &str) {
        if let Some(f) = self.0.log {
            unsafe { f(self.0.user, 1, abi::ByteSlice::new(text.as_bytes())) };
        }
    }
    pub fn visible(self) -> bool {
        let mut v = abi::DisplayState::default();
        self.0
            .read_display_state
            .is_some_and(|f| unsafe { f(self.0.user, &mut v) } == abi::OK)
            && v.valid != 0
            && v.loading_or_logo == 0
            && v.full_screen_map == 0
    }
    pub fn key(self, key: u32) -> bool {
        key != 0
            && self
                .0
                .key_pressed
                .is_some_and(|f| unsafe { f(self.0.user, key) } != 0)
    }
    pub fn state(self) -> Option<abi::BossState> {
        let mut v = abi::BossState {
            size: size_of::<abi::BossState>() as u32,
            ..Default::default()
        };
        self.0
            .read_boss_state
            .is_some_and(|f| unsafe { f(self.0.user, &mut v) } == abi::OK)
            .then_some(v)
    }
    pub fn flags(self, ids: &[u32], out: &mut [u8]) -> bool {
        out.fill(2);
        ids.len() == out.len()
            && self.0.read_event_flags.is_some_and(
                |f| unsafe { f(self.0.user, ids.as_ptr(), out.as_mut_ptr(), ids.len()) } == abi::OK,
            )
    }
    pub fn param(self, group: u32) -> usize {
        self.0
            .find_param_table
            .map_or(0, |f| unsafe { f(self.0.user, group) })
    }
    pub fn request(self, path: &str) -> u64 {
        let request = abi::FileRequest {
            size: size_of::<abi::FileRequest>() as u32,
            flags: 0x40,
            path: abi::ByteSlice::new(path.as_bytes()),
            part: abi::ByteSlice::empty(),
            max_bytes: 64 * 1024 * 1024,
        };
        self.0
            .request_file
            .map_or(0, |f| unsafe { f(self.0.user, &request) })
    }
    pub fn poll(self, token: u64) -> Option<Result<Vec<u8>, ()>> {
        let mut v = abi::FileView::default();
        let f = self.0.poll_file?;
        match unsafe { f(self.0.user, token, &mut v) } {
            abi::ERR_BUSY => return None,
            abi::OK => {}
            _ => return Some(Err(())),
        }
        match v.status {
            abi::FILE_PENDING => None,
            abi::FILE_READY
                if v.bytes.len <= 64 * 1024 * 1024
                    && (v.bytes.len == 0 || !v.bytes.data.is_null()) =>
            {
                // SAFETY: file bytes are borrowed through release_file; copied now on update.
                Some(Ok(if v.bytes.len == 0 {
                    Vec::new()
                } else {
                    unsafe { std::slice::from_raw_parts(v.bytes.data, v.bytes.len) }.to_vec()
                }))
            }
            _ => Some(Err(())),
        }
    }
    pub fn release(self, token: u64) {
        if token != 0
            && let Some(f) = self.0.release_file
        {
            unsafe { f(self.0.user, token) };
        }
    }
    pub fn capture(self, mask: u32) {
        if let Some(f) = self.0.ui_input_capture {
            unsafe { f(self.0.user, mask) };
        }
    }
    pub fn clear_input(self) {
        if let Some(f) = self.0.ui_input_clear {
            unsafe { f(self.0.user) };
        }
    }
}
