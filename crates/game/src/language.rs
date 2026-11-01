//! Initialization-only lookup through the already loaded official Steam API.
use crate::memory::read_memory;

fn copy_language(address: usize) -> Option<String> {
    let mut bytes = [0u8; 64];
    for i in 0..bytes.len() {
        if !read_memory(address.checked_add(i)?, &mut bytes[i..i + 1]) {
            return None;
        }
        if bytes[i] == 0 {
            if i == 0 {
                return None;
            }
            return Some(std::str::from_utf8(&bytes[..i]).ok()?.to_owned());
        }
    }
    None
}

/// Returns Steam's raw language name (e.g. `english`, `schinese`, `koreana`).
/// Does not initialize Steam, load a module, scan memory, or cache a pointer.
/// Call only during initialization, outside DllMain and render callbacks.
pub fn game_language() -> Option<String> {
    #[cfg(windows)]
    {
        use std::ffi::c_void;
        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn GetModuleHandleExW(flags: u32, name: *const u16, module: *mut *mut c_void) -> i32;
            fn GetProcAddress(module: *mut c_void, name: *const u8) -> *mut c_void;
            fn FreeLibrary(module: *mut c_void) -> i32;
        }
        struct Module(*mut c_void);
        impl Drop for Module {
            fn drop(&mut self) {
                // SAFETY: successful GetModuleHandleExW acquired this reference.
                unsafe { FreeLibrary(self.0) };
            }
        }
        let name = (*b"steam_api64.dll\0").map(u16::from);
        let mut module = std::ptr::null_mut();
        // A refcounted lookup of an existing module, never LoadLibrary/SteamAPI_Init.
        if unsafe { GetModuleHandleExW(0, name.as_ptr(), &mut module) } == 0 {
            return None;
        }
        let module = Module(module);
        let apps = unsafe { GetProcAddress(module.0, c"SteamAPI_SteamApps_v008".as_ptr().cast()) };
        let language = unsafe {
            GetProcAddress(
                module.0,
                c"SteamAPI_ISteamApps_GetCurrentGameLanguage"
                    .as_ptr()
                    .cast(),
            )
        };
        if apps.is_null() || language.is_null() {
            return None;
        }
        // SAFETY: exact S_API/cdecl signatures in the vendored first-party
        // deps/steamworks/include/steam/steam_api_flat.h (ISteamApps v008).
        let apps: unsafe extern "C" fn() -> *mut c_void = unsafe { std::mem::transmute(apps) };
        let language: unsafe extern "C" fn(*mut c_void) -> *const u8 =
            unsafe { std::mem::transmute(language) };
        let apps = unsafe { apps() };
        if apps.is_null() {
            return None;
        }
        // Module remains retained through the bounded OS-validated copy.
        copy_language(unsafe { language(apps) } as usize)
    }
    #[cfg(not(windows))]
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn copies_terminated_foreign_language_and_rejects_bad_ranges() {
        assert_eq!(
            copy_language(c"tchinese".as_ptr() as usize).as_deref(),
            Some("tchinese")
        );
        assert_eq!(
            copy_language(c"english".as_ptr() as usize).as_deref(),
            Some("english")
        );
        assert!(copy_language(0).is_none());
        assert!(copy_language(usize::MAX).is_none());
        assert!(copy_language(c"".as_ptr() as usize).is_none());
        assert!(copy_language([b'x'; 64].as_ptr() as usize).is_none());
        assert!(copy_language([0xffu8, 0].as_ptr() as usize).is_none());
        let mut max = [b'x'; 64];
        max[63] = 0;
        assert_eq!(copy_language(max.as_ptr() as usize).unwrap().len(), 63);
    }
}
