use eroverlay_abi as abi;

#[derive(Clone, Copy)]
pub struct Host(pub abi::HostApi);
// SAFETY: the host promises its immutable table/user remain valid through destroy;
// render only invokes its documented nonblocking render-safe callbacks.
unsafe impl Send for Host {}
unsafe impl Sync for Host {}

impl Host {
    pub fn log(self, message: &str) {
        if let Some(log) = self.0.log {
            unsafe { log(self.0.user, 1, abi::ByteSlice::new(message.as_bytes())) };
        }
    }
    pub fn config(self, key: &str) -> Option<String> {
        let key = format!("minimap.{key}");
        self.config_raw(&key)
    }
    pub fn config_raw(self, key: &str) -> Option<String> {
        let get = self.0.config_get?;
        let mut data = [0u8; 4096];
        let n = unsafe {
            get(
                self.0.user,
                abi::ByteSlice::new(key.as_bytes()),
                data.as_mut_ptr(),
                data.len(),
            )
        };
        if n > data.len() {
            return None;
        }
        String::from_utf8(data[..n].to_vec()).ok()
    }
    pub fn key(self, key: u32) -> bool {
        key != 0
            && self
                .0
                .key_pressed
                .is_some_and(|f| unsafe { f(self.0.user, key) != 0 })
    }
    pub fn map(self) -> Option<abi::MapState> {
        let mut value = abi::MapState::default();
        let f = self.0.read_map_state?;
        (unsafe { f(self.0.user, &mut value) } == abi::OK).then_some(value)
    }
    pub fn visible(self) -> bool {
        let mut value = abi::DisplayState::default();
        self.0
            .read_display_state
            .is_some_and(|f| unsafe { f(self.0.user, &mut value) == abi::OK })
            && value.valid != 0
            && value.loading_or_logo == 0
            && value.full_screen_map == 0
    }
    pub fn layout(self) -> abi::GameLayout {
        let mut value = abi::GameLayout {
            size: size_of::<abi::GameLayout>() as u32,
            grace_stride: 0x350,
            grace_normal_offset: 0x348,
            alternate_icons: 1,
            map_mask: 7,
            ..Default::default()
        };
        if let Some(f) = self.0.read_game_layout {
            unsafe { f(self.0.user, &mut value) };
        }
        value
    }
    pub fn read(self, address: usize, out: &mut [u8]) -> bool {
        address != 0
            && self.0.read_memory.is_some_and(|f| unsafe {
                f(self.0.user, address, out.as_mut_ptr(), out.len()) == abi::OK
            })
    }
    pub fn bytes(self, address: usize, len: usize) -> Option<Vec<u8>> {
        if len > 4 * 1024 * 1024 {
            return None;
        }
        let mut bytes = vec![0; len];
        self.read(address, &mut bytes).then_some(bytes)
    }
    pub fn u8(self, address: usize) -> Option<u8> {
        let mut b = [0];
        self.read(address, &mut b).then_some(b[0])
    }
    pub fn u32(self, address: usize) -> Option<u32> {
        let mut b = [0; 4];
        self.read(address, &mut b).then_some(u32::from_le_bytes(b))
    }
    pub fn u64(self, address: usize) -> Option<u64> {
        let mut b = [0; 8];
        self.read(address, &mut b).then_some(u64::from_le_bytes(b))
    }
    pub fn pointer(self, address: usize) -> Option<usize> {
        usize::try_from(self.u64(address)?).ok().filter(|p| *p != 0)
    }
    pub fn flag(self, id: u32) -> bool {
        self.0
            .read_event_flag
            .is_some_and(|f| unsafe { f(self.0.user, id) != 0 })
    }
    pub fn param(self, group: u32) -> usize {
        self.0
            .find_param_table
            .map_or(0, |f| unsafe { f(self.0.user, group) })
    }
    pub fn request(self, path: &str, part: &str) -> u64 {
        let request = abi::FileRequest {
            size: size_of::<abi::FileRequest>() as u32,
            flags: if path.ends_with(".dcx") { 0x40 } else { 0 },
            path: abi::ByteSlice::new(path.as_bytes()),
            part: abi::ByteSlice::new(part.as_bytes()),
            max_bytes: abi::MAX_FILE_BYTES as u64,
        };
        self.0
            .request_file
            .map_or(0, |f| unsafe { f(self.0.user, &request) })
    }
    pub fn poll(self, token: u64) -> Option<Result<Vec<u8>, ()>> {
        let poll = self.0.poll_file?;
        let mut view = abi::FileView::default();
        match unsafe { poll(self.0.user, token, &mut view) } {
            abi::OK => {}
            // The core uses a nonblocking service lock. A busy poll has not
            // completed or failed the request; keep its token for a later poll.
            abi::ERR_BUSY => return None,
            _ => return Some(Err(())),
        }
        match view.status {
            abi::FILE_PENDING => None,
            abi::FILE_READY
                if view.bytes.len <= abi::MAX_FILE_BYTES
                    && (view.bytes.len == 0 || !view.bytes.data.is_null()) =>
            {
                // SAFETY: poll's borrowed bytes stay valid through release_file; consumed synchronously.
                let bytes = if view.bytes.len == 0 {
                    Vec::new()
                } else {
                    unsafe { std::slice::from_raw_parts(view.bytes.data, view.bytes.len) }.to_vec()
                };
                Some(Ok(bytes))
            }
            _ => Some(Err(())),
        }
    }
    pub fn release_file(self, token: u64) {
        if token != 0
            && let Some(f) = self.0.release_file
        {
            unsafe { f(self.0.user, token) }
        }
    }
    pub fn texture(self, dds: &[u8]) -> u64 {
        self.0
            .texture_create
            .map_or(0, |f| unsafe { f(self.0.user, abi::ByteSlice::new(dds)) })
    }
    pub fn rgba(self, width: u32, height: u32, rgba: &[u8]) -> u64 {
        self.0.texture_create_rgba.map_or(0, |f| unsafe {
            f(self.0.user, width, height, abi::ByteSlice::new(rgba))
        })
    }
    pub fn texture_status(self, id: u64) -> Option<abi::TextureView> {
        let mut view = abi::TextureView::default();
        let f = self.0.texture_poll?;
        (unsafe { f(self.0.user, id, &mut view) } == abi::OK).then_some(view)
    }
    pub fn release_texture(self, id: u64) {
        if id != 0
            && let Some(f) = self.0.texture_release
        {
            unsafe { f(self.0.user, id) }
        }
    }
}

pub struct Texture {
    pub id: u64,
    pub width: u32,
    pub height: u32,
    pub bytes: u64,
    host: Host,
}
impl Texture {
    pub fn poll(host: Host, id: u64) -> Option<Result<Self, ()>> {
        let v = host.texture_status(id)?;
        match v.status {
            abi::TEXTURE_READY => Some(Ok(Self {
                id,
                width: v.width,
                height: v.height,
                bytes: v.memory_bytes,
                host,
            })),
            abi::TEXTURE_FAILED => Some(Err(())),
            _ => None,
        }
    }
    #[cfg(test)]
    pub fn ready(host: Host, id: u64) -> Option<Self> {
        Self::poll(host, id)?.ok()
    }
}
impl Drop for Texture {
    fn drop(&mut self) {
        self.host.release_texture(self.id)
    }
}

pub fn chord(text: &str) -> u32 {
    abi::parse_chord(text)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn key_chords_preserve_modifiers() {
        assert_eq!(chord("CTRL+M"), abi::MOD_CTRL | u32::from(b'M'));
        assert_eq!(chord("F8"), 0x77);
        assert_eq!(chord("none"), 0);
        assert_eq!(chord("unknown"), 0);
    }
}
