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
        // These are high-level resource names. Unlike the game's resource
        // loader, the raw file submit API does not append the DCX extension.
        let path = if path.ends_with(".dcx") {
            path.to_owned()
        } else {
            format!("{path}.dcx")
        };
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::c_void;

    #[test]
    fn raw_native_requests_use_explicit_dcx_names_and_decompression_flag() {
        unsafe extern "C" fn request(user: *mut c_void, raw: *const abi::FileRequest) -> u64 {
            // SAFETY: the test owns both values and the synchronous request
            // callback copies the borrowed path before Host::request returns.
            let calls = unsafe { &mut *user.cast::<Vec<(String, u32)>>() };
            let raw = unsafe { &*raw };
            let bytes = unsafe { std::slice::from_raw_parts(raw.path.data, raw.path.len) };
            calls.push((std::str::from_utf8(bytes).unwrap().into(), raw.flags));
            assert_eq!(raw.max_bytes, 64 * 1024 * 1024);
            calls.len() as u64
        }
        let mut calls = Vec::<(String, u32)>::new();
        // SAFETY: HostApi contains only nullable pointers and integer fields;
        // this isolated test uses only the populated synchronous callback.
        let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
        api.user = (&mut calls as *mut Vec<(String, u32)>).cast();
        api.request_file = Some(request);
        let host = Host(api);
        for (i, path) in [
            "event:/common_func.emevd",
            "event:/m60_42_36_00.emevd",
            "msg:/zhoCN/item_dlc02.msgbnd",
            "mapstudio:/m60_38_54_00.msb",
            "mapstudio_dlc2:/m61_49_44_00.msb.dcx",
        ]
        .into_iter()
        .enumerate()
        {
            assert_eq!(host.request(path), i as u64 + 1);
            let expected = if path.ends_with(".dcx") {
                path.into()
            } else {
                format!("{path}.dcx")
            };
            assert_eq!(calls[i], (expected, 0x40));
        }
    }
}
