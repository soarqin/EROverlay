//! Validates the native UI_SCENE contract before any hook can become reachable.
//! Uses RVAs for both live snapshots and archived PE images; no version allowlist.
use eroverlay_game::scan::PeImage;

/// Internal Rust/native bridge layout, never a plugin ABI object.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Addresses {
    pub submit: u32,
    pub enqueue: u32,
    pub consume: u32,
    pub pixel_format: u32,
    pub render_view_vtable: u32,
    pub texture_vtable: u32,
    pub swap_chains_offset: u32,
    pub viewport_offset: u32,
    pub context_environment_offset: u32,
    pub backend_offset: u32,
    pub queue_offset: u32,
    pub composite: u32,
    pub render_post_offset: u32,
    pub queue_index_offset: u32,
    pub queues_offset: u32,
}

pub fn resolve(image: &PeImage<'_>) -> Result<Addresses, &'static str> {
    let submit = image.unique(QUEUE_SUBMIT, ".text");
    if submit == 0 {
        return Err("draw queue submit signature is missing or ambiguous");
    }
    let enqueue = image.relative(submit + 0x3a, 1, 5);
    if enqueue != image.relative(submit + 0x57, 1, 5) || !image.matches(enqueue, QUEUE_CALLBACK) {
        return Err("draw queue callback contract changed");
    }
    let sort = image.relative(submit + 0x5f, 1, 5);
    let execute = image.relative(submit + 0x84, 1, 5);
    if !image.matches(sort, QUEUE_SORT) || !image.matches(execute, QUEUE_EXECUTE) {
        return Err("draw queue sort/execute contract changed");
    }
    let consume = image.relative(execute + 0xd, 1, 5);
    if !image.matches(consume, QUEUE_CONSUME) {
        return Err("draw queue flush-before-callback contract changed");
    }
    let begin = image.relative(submit + 0x30, 3, 7);
    if !image.matches(begin, BEGIN_CALLBACK) {
        return Err("draw scene begin callback changed");
    }
    let setup = image.relative(begin + 0x4a, 1, 5);
    if !image.matches(setup, SCENE_SETUP) {
        return Err("draw scene layout changed");
    }
    let factory = image.unique(RTV_CREATE, ".text");
    if factory == 0 {
        return Err("render target view format contract changed");
    }
    let composite = image.unique(UI_COMPOSITE, ".text");
    let post = image.read_u32(composite + 0x114);
    let post_again = image.read_u32(composite + 0x248);
    let composite_swap = image.read_u32(composite + 0x46);
    if composite == 0
        || post.is_none()
        || post != post_again
        || composite_swap.is_none()
        || !post.is_some_and(|value| (1..=4096).contains(&value))
    {
        return Err("native UI composition/scene target contract changed");
    }
    let native_enqueue = image.unique(NATIVE_ENQUEUE, ".text");
    let queue_index = image.read_u32(native_enqueue + 0x23);
    let index_again = image.read_u32(native_enqueue + 0x4e);
    let queues = image.read_u32(native_enqueue + 0x5e);
    if native_enqueue == 0
        || image.relative(composite + 0x4b0, 1, 5) != native_enqueue
        || queue_index.is_none()
        || queue_index != index_again
        || !queue_index.is_some_and(|value| (1..=4096).contains(&value))
        || !queues.is_some_and(|value| (1..=4096).contains(&value))
    {
        return Err("native UI composition queue layout changed");
    }
    let pixel_format = image.relative(factory + 0x64, 1, 5);
    let render_view_vtable = image.vtable(".?AVCGRenderTargetView@DLCG3@@");
    let texture_vtable = image.vtable(".?AVCGTexture2D@DLCG3@@");
    if pixel_format == 0 || render_view_vtable == 0 || texture_vtable == 0 {
        return Err("render target RTTI/format converter is missing");
    }
    let (Some(swap_chains), Some(viewport), Some(context_environment)) = (
        image.read_u32(setup + 0x4c),
        image.read_u32(setup + 0xfe),
        image.read_u32(consume + 0x25),
    ) else {
        return Err("draw scene operands are truncated");
    };
    if composite_swap != Some(swap_chains) {
        return Err("native UI compositor uses a different swap-chain layout");
    }
    let flush = image.relative(consume + 0xb2, 1, 5);
    if !image.matches(flush, "48 8B 49 ?? E9 ?? ?? ?? ??") {
        return Err("draw queue backend forwarding changed");
    }
    let Some(backend) = image.read_u8(flush + 3) else {
        return Err("draw queue backend operand is truncated");
    };
    let mut batch = image.relative(flush + 4, 1, 5);
    for _ in 0..4 {
        if image.read_u8(batch) != Some(0xe9) {
            break;
        }
        batch = image.relative(batch, 1, 5);
    }
    if !image.matches(batch + 0x3d, "48 8B 41 ?? 48 89 45 C0 41 8B D8")
        || !image.matches(batch + 0x229, "48 8B 07 4D 8B C7 8B D3 48 8B CF FF 50 50")
    {
        return Err("native ExecuteCommandLists contract changed");
    }
    let Some(queue) = image.read_u8(batch + 0x40) else {
        return Err("native ExecuteCommandLists contract changed");
    };
    if !(1..=4096).contains(&swap_chains)
        || !(1..=4096).contains(&viewport)
        || context_environment > 16384
        || backend == 0
        || queue == 0
    {
        return Err("draw scene offsets are out of range");
    }
    Ok(Addresses {
        submit,
        enqueue,
        consume,
        pixel_format,
        render_view_vtable,
        texture_vtable,
        swap_chains_offset: swap_chains,
        viewport_offset: viewport,
        context_environment_offset: context_environment,
        backend_offset: u32::from(backend),
        queue_offset: u32::from(queue),
        composite,
        render_post_offset: post.unwrap(),
        queue_index_offset: queue_index.unwrap(),
        queues_offset: queues.unwrap(),
    })
}

// IDA-decoded 1.16 functions. Address operands are masked; internal branches retained.
const QUEUE_SUBMIT: &str = "48 85 D2 0F 84 9B 00 00 00 57 48 83 EC 50 48 C7 44 24 30 FE FF FF FF 48 89 5C 24 60 48 8B FA 48 8B D9 C7 44 24 20 01 00 00 00 45 33 C9 4C 8B C1 48 8D 15 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? C7 44 24 20 FF FF FF FF 45 33 C9 4C 8B C3 48 8D 15 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8D 4C 24 38 E8 ?? ?? ?? ?? 90 48 8B 5C 24 38 44 8B 4F 30 4C 8B 47 28 48 8B D3 48 8D 4C 24 68 E8 ?? ?? ?? ?? C6 83 D8 25 00 00 00 48 8D 4C 24 38 E8 ?? ?? ?? ?? 48 8B 5C 24 60 48 83 C4 50 5F C3";

const QUEUE_CALLBACK: &str = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 8B 5C 24 60 49 8B F1 49 8B E8 4C 8B F2 48 8B F9 83 FB FF 75 1B 8B 41 38 8B D8 81 E3 00 00 80 FF 81 C3 00 00 80 00 33 D8 81 E3 00 00 80 1F 33 D8 41 B9 04 00 00 00 89 5C 24 20 B2 02 45 8D 41 14 E8 ?? ?? ?? ?? 48 89 68 08 48 8B 6C 24 48 48 89 70 10 48 8B 74 24 50 4C 89 30 B0 01 39 5F 38 0F 47 5F 38 89 5F 38 48 8B 5C 24 40 48 8B 7C 24 58 48 83 C4 30 41 5E C3";

const QUEUE_SORT: &str =
    "48 63 51 30 44 0F B6 44 24 08 48 C1 E2 04 48 03 51 28 48 8B 49 28 E9 ?? ?? ?? ??";

const QUEUE_EXECUTE: &str = "48 83 EC 38 48 8D 4C 24 20 48 83 C4 38 E9 ?? ?? ?? ??";

const QUEUE_CONSUME: &str = "45 85 C9 0F 84 BB 01 00 00 4C 8B DC 49 89 73 18 55 57 41 57 49 8D AB 78 FE FF FF 48 81 EC 70 02 00 00 48 8B 82 E8 25 00 00 45 33 FF 49 89 5B 08 49 8B F0 4D 89 73 10 45 8B C7 48 89 44 24 20 48 8B FA 44 89 BD 68 01 00 00 45 8B F1 0F 1F 40 00 48 8B 0E 0F B6 59 01 0F B6 11 48 03 D9 85 D2 0F 84 AF 00 00 00 83 EA 01 74 77 83 EA 01 74 34 83 FA 01 0F 85 07 01 00 00 45 85 C0 74 16 48 8B 4C 24 20 48 8D 54 24 28 E8 ?? ?? ?? ?? 44 89 BD 68 01 00 00 48 8B 53 14 48 8B 4B 0C FF 53 04 E9 D5 00 00 00 45 85 C0 74 16 48 8B 4C 24 20 48 8D 54 24 28 E8 ?? ?? ?? ?? 44 89 BD 68 01 00 00 4C 8B 43 14 48 8B CF 48 8B 53 0C FF 53 04 84 C0 0F 84 A4 00 00 00 48 8B CF E8 ?? ?? ?? ?? E9 97 00 00 00 45 85 C0 74 16 48 8B 4C 24 20 48 8D 54 24 28 E8 ?? ?? ?? ?? 44 89 BD 68 01 00 00 4C 8B 43 14 48 8B CF 48 8B 53 0C FF 53 04 48 8B CF E8 ?? ?? ?? ?? EB 64 48 8B 0D ?? ?? ?? ?? 48 8B 5B 04 E8 ?? ?? ?? ?? 44 38 3D ?? ?? ?? ?? 74 3E 8B 85 68 01 00 00 83 F8 48 75 1B 48 8B 4C 24 20 48 8D 54 24 28 44 8B C0 E8 ?? ?? ?? ?? 41 8B C7 89 85 68 01 00 00 48 89 5C C4 28 44 8B 85 68 01 00 00 41 FF C0 44 89 85 68 01 00 00 EB 14 48 8B 4C 24 20 48 8B D3 E8 ?? ?? ?? ?? 44 8B 85 68 01 00 00 48 83 C6 10 49 83 EE 01 0F 85 C3 FE FF FF 4C 8B B4 24 98 02 00 00 48 8B 9C 24 90 02 00 00 45 85 C0 74 0F 48 8B 4C 24 20 48 8D 54 24 28 E8 ?? ?? ?? ?? 48 8B B4 24 A0 02 00 00 48 81 C4 70 02 00 00 41 5F 5F 5D C3";

const BEGIN_CALLBACK: &str = "40 57 48 83 EC 40 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 50 48 8B FA 48 8B D1 48 8D 4C 24 28 E8 ?? ?? ?? ?? 90 48 8B 5C 24 28 48 8B 8F 30 0F 00 00 48 85 C9 74 08 48 8B D3 E8 ?? ?? ?? ?? 41 B1 01 45 33 C0 48 8B D3 48 8B CF E8 ?? ?? ?? ?? 90 48 8D 4C 24 28 E8 ?? ?? ?? ?? B0 01 48 8B 5C 24 50 48 83 C4 40 5F C3";

const SCENE_SETUP: &str = "4C 8B DC 55 56 41 56 41 57 48 81 EC A8 00 00 00 41 0F 29 73 B8 0F 29 7C 24 70 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 60 48 8B EA 4D 89 63 D0 48 8B F1 4D 89 6B C8 48 8B CD 33 D2 45 0F B6 E1 45 0F B6 E8 E8 ?? ?? ?? ?? 4C 8B B6 28 01 00 00 33 D2 49 8B 8E 00 01 00 00 48 83 C1 70 48 8B 01 FF 50 08 45 33 C9 4C 8D 44 24 30 48 8B CD 48 89 44 24 30 41 8D 51 01 E8 ?? ?? ?? ?? F3 0F 10 3D ?? ?? ?? ?? 4C 8D 44 24 48 48 C7 44 24 48 00 00 00 00 0F 57 C0 41 8B 86 28 01 00 00 0F 57 F6 0F 28 CE BA 01 00 00 00 0F 14 CF 48 8D 0C 40 48 03 C9 8B 84 CE C8 03 00 00 F3 48 0F 2A C0 F3 0F 11 44 24 50 0F 57 C0 41 8B 86 28 01 00 00 48 8D 0C 40 48 03 C9 8B 84 CE CC 03 00 00 C7 85 C8 00 00 00 01 00 00 00 F2 0F 11 4C 24 58 F3 48 0F 2A C0 F3 0F 11 44 24 54 0F 10 44 24 48 0F 11 85 CC 00 00 00 F2 0F 11 8D DC 00 00 00 48 8B 4D 08 E8 ?? ?? ?? ??";

const RTV_CREATE: &str = "48 89 5C 24 10 48 89 74 24 18 55 57 41 56 48 8B EC 48 83 EC 40 49 8B D8 4C 8B F2 48 8B F9 E8 ?? ?? ?? ?? 0F 10 03 48 8D 55 20 48 8B F0 0F 11 47 08 44 8B 4B 10 44 89 4F 18 48 8B 48 48 E8 ?? ?? ?? ?? 48 8B 08 48 89 0F 48 85 C9 75 07 B0 09 E9 CF 00 00 00 8B 0B 33 C0 48 89 45 E0 48 89 45 E8 48 89 45 F0 E8 ?? ?? ?? ?? 0F B6 4B 04 89 45 E0 E8 ?? ?? ?? ?? 89 45 E4 FF C8 83 F8 07 0F 87 9E 00 00 00 48 8D 15 ?? ?? ?? ?? 48 98 8B 8C 82 ?? ?? ?? ?? 48 03 CA FF E1 8B 43 08 48 89 45 E8 8B 43 0C EB 5F 8B 43 08 89 45 E8 EB 5A 8B 43 08 33 D2 8B 0B 89 45 E8 E8 ?? ?? ?? ?? 0F BE C8 89 4D EC EB 43 8B 43 08 33 D2 8B 0B 89 45 E8 8B 43 0C 89 45 EC 8B 43 10 89 45 F0 E8 ?? ?? ?? ?? 0F BE C8 89 4D F4 EB 20 8B 43 0C 89 45 E8 8B 43 10 89 45 EC EB 12 8B 43 08 89 45 E8 8B 43 0C 89 45 EC 8B 43 10 89 45 F0 48 8B 0E 4C 8D 45 E0 4C 8B 0F 49 8B 56 20 48 8B 01 FF 90 A0 00 00 00 32 C0 EB 02 B0 05 48 8B 5C 24 68 48 8B 74 24 70 48 83 C4 40 41 5E 5F 5D C3";

const UI_COMPOSITE: &str = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FD FF FF 48 81 EC 20 03 00 00 48 C7 45 88 FE FF FF FF 48 89 58 18 0F 29 70 B8 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 00 02 00 00 48 8B DA 48 8B F9 4C 8B B1 28 01 00 00 41 80 BE 3C 01 00 00 00 75 0E 41 80 BE 3E 01 00 00 00 0F 84 53 04 00 00 33 D2 48 8B CB E8 ?? ?? ?? ?? 8B 87 58 0F 00 00 49 8B 8C C6 A8 00 00 00 48 83 C1 70 48 8B 01 33 D2 FF 50 08 4C 8B F8 8B 8F 58 0F 00 00 49 8B 94 CE A8 00 00 00 48 8B 42 40 48 89 45 80 33 C9 48 89 4C 24 51 48 89 4C 24 59 48 89 4C 24 61 89 4C 24 69 66 89 4C 24 6D 88 4C 24 6F 66 89 4C 24 50 48 89 44 24 58 C7 44 24 60 FF FF FF FF C7 44 24 64 00 00 01 00 C7 44 24 68 10 00 00 00 88 4C 24 6C 4C 8D 44 24 50 8D 51 01 48 8B 4B 08 E8 ?? ?? ?? ?? 48 8B 87 28 01 00 00 80 B8 3C 01 00 00 00 75 0D 80 B8 3E 01 00 00 00 75 04 33 F6 EB 07 48 8B B0 00 01 00 00 48 83 C6 70 48 8B 06 48 8B CE FF 50 10 84 C0 74 40 48 8B 06 33 D2 48 8B CE FF 50 08 45 33 C0 48 8B D0 48 8B 4B 08 E8 ?? ?? ?? ?? 48 8B 06 33 D2 48 8B CE FF 50 08 48 89 44 24 40 4C 8D 44 24 40 BA 01 00 00 00 48 8B 8B C8 25 00 00 E8 ?? ?? ?? ?? 48 8B 87 40 07 00 00 80 78 08 00 0F 84 A7 02 00 00 45 33 C9 45 33 C0 49 8B D7 48 8B CB E8 ?? ?? ?? ?? 4D 8D BE 10 01 00 00 C7 83 C8 00 00 00 01 00 00 00 41 0F 10 07 0F 11 83 CC 00 00 00 F2 41 0F 10 4F 10 F2 0F 11 8B DC 00 00 00 4D 8B C7 BA 01 00 00 00 48 8B 4B 08 E8 ?? ?? ?? ?? F3 0F 10 B7 74 07 00 00 F3 0F 5E B7 68 07 00 00 0F 28 CE 48 8D 35 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 48 8B 8F C8 0E 00 00 0F 10 41 08 0F 11 45 90 8B 41 18 0F 10 05 ?? ?? ?? ?? 0F 11 45 90 F3 0F 11 75 A0 48 8D 55 90 E8 ?? ?? ?? ?? 48 8B 97 28 01 00 00 0F B6 8A 3C 01 00 00 84 C9 74 0D 80 BF B9 0E 00 00 00 75 04 B0 01 EB 02 33 C0 84 C9 75 0D 38 8A 3E 01 00 00 75 05 45 33 C0 EB 07 4C 8B 82 00 01 00 00 88 4C 24 38 88 44 24 30 48 8B 87 C8 0E 00 00 48 89 44 24 28 48 89 74 24 20 4C 8B 8F C0 0E 00 00 48 8B 92 F0 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 8B D3 48 8D 4D B0 E8 ?? ?? ?? ?? 90 48 8D 55 B0 48 8D 4C 24 50 E8 ?? ?? ?? ?? 48 8D 45 B0 48 89 44 24 70 48 8D 44 24 50 48 89 44 24 78 41 8B 86 28 01 00 00 48 8D 0C 40 48 03 C9 44 8B A4 CF C8 03 00 00 44 8B AC CF CC 03 00 00 33 C0 48 89 44 24 44 48 89 44 24 40 44 89 64 24 48 44 89 6C 24 4C F3 41 0F 10 07 F3 0F 2C C0 F3 41 0F 10 4F 04 F3 44 0F 2C F1 F3 41 0F 58 47 08 F3 0F 2C F0 F3 41 0F 58 4F 0C F3 44 0F 2C F9 0F 28 74 24 40 85 C0 7E 2F 66 0F 7F B5 F0 01 00 00 89 85 F8 01 00 00 C7 44 24 40 00 00 00 00 44 8B 4C 24 40 4C 8D 85 F0 01 00 00 48 8D 54 24 70 48 8B CB E8 ?? ?? ?? ?? 41 3B F4 7D 2F 66 0F 7F B5 F0 01 00 00 89 B5 F0 01 00 00 C7 44 24 40 00 00 00 00 44 8B 4C 24 40 4C 8D 85 F0 01 00 00 48 8D 54 24 70 48 8B CB E8 ?? ?? ?? ?? 45 85 F6 7E 30 66 0F 7F B5 F0 01 00 00 44 89 B5 FC 01 00 00 C7 44 24 40 00 00 00 00 44 8B 4C 24 40 4C 8D 85 F0 01 00 00 48 8D 54 24 70 48 8B CB E8 ?? ?? ?? ?? 45 3B FD 7D 31 66 0F 7F B5 F0 01 00 00 44 89 BD F4 01 00 00 C7 44 24 40 00 00 00 00 44 8B 4C 24 40 4C 8D 85 F0 01 00 00 48 8D 54 24 70 48 8B CB E8 ?? ?? ?? ?? 90 4C 8D 0D ?? ?? ?? ?? BA 10 00 00 00 44 8D 42 F6 48 8D 8D 60 01 00 00 E8 ?? ?? ?? ?? 90 4C 8D 0D ?? ?? ?? ?? BA 10 00 00 00 44 8D 42 F6 48 8D 8D 00 01 00 00 E8 ?? ?? ?? ?? 90 48 8B CB E8 ?? ?? ?? ?? EB 29 0F 28 05 ?? ?? ?? ?? 0F 11 85 F0 01 00 00 4D 85 FF 74 16 4C 8D 85 F0 01 00 00 49 8B D7 48 8B 8B C8 25 00 00 E8 ?? ?? ?? ?? 33 C0 48 89 44 24 51 48 89 44 24 59 48 89 44 24 61 89 44 24 69 66 89 44 24 6D 88 44 24 6F 66 89 44 24 50 48 8B 45 80 48 89 44 24 58 C7 44 24 60 FF FF FF FF C7 44 24 64 10 00 00 00 C7 44 24 68 00 00 01 00 C6 44 24 6C 00 4C 8D 44 24 50 BA 01 00 00 00 48 8B 4B 08 E8 ?? ?? ?? ?? 41 83 C8 FF 48 8B D3 48 8B CF E8 ?? ?? ?? ?? 48 8B 8D 00 02 00 00 48 33 CC E8 ?? ?? ?? ?? 48 8B 9C 24 70 03 00 00 0F 28 B4 24 10 03 00 00 48 81 C4 20 03 00 00 41 5F 41 5E 41 5D 41 5C 5F 5E 5D C3";

const NATIVE_ENQUEUE: &str = "48 89 5C 24 10 57 48 83 EC 20 48 8B D9 48 C7 44 24 30 00 00 00 00 4C 8D 89 E0 0F 00 00 41 8B F8 48 63 89 08 07 00 00 4C 8B C2 48 8D 54 24 30 48 8B 8C CB D8 06 00 00 E8 ?? ?? ?? ?? 84 C0 75 0B 48 8B 5C 24 38 48 83 C4 20 5F C3 48 63 8B 08 07 00 00 44 8B C7 48 8B 54 24 30 48 8B 8C CB E8 06 00 00 E8 ?? ?? ?? ?? 48 8B 5C 24 38 48 83 C4 20 5F C3";

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn internal_bridge_layout_and_truncated_images_fail_closed() {
        assert_eq!(size_of::<Addresses>(), 60);
        assert_eq!(std::mem::offset_of!(Addresses, queues_offset), 56);
        for bytes in [&[][..], &[0; 64][..], b"MZ".as_slice()] {
            assert!(resolve(&PeImage::new(bytes, 0x140000000)).is_err());
        }
    }

    #[test]
    #[ignore = "set EROVERLAY_SCENE_EXE to a supported archived EXE; reads data only"]
    fn archived_contract_rejects_mutations_and_survives_relocation() {
        let path = std::env::var_os("EROVERLAY_SCENE_EXE").expect("set EROVERLAY_SCENE_EXE");
        let raw = std::fs::read(path).unwrap();
        let (mut image, base) = eroverlay_game::scan::map_pe(&raw).unwrap();
        let expected = resolve(&PeImage::new(&image, base)).unwrap();
        image[0x30] ^= 1;
        assert_eq!(resolve(&PeImage::new(&image, base)), Ok(expected));
        image[0x30] ^= 1;

        let (text, _) = PeImage::new(&image, base).section(".text").unwrap();
        let duplicate = text as usize + 0x1000;
        let length = QUEUE_SUBMIT.split_ascii_whitespace().count();
        let original = image[duplicate..duplicate + length].to_vec();
        image.copy_within(
            expected.submit as usize..expected.submit as usize + length,
            duplicate,
        );
        assert!(resolve(&PeImage::new(&image, base)).is_err());
        image[duplicate..duplicate + length].copy_from_slice(&original);

        for (name, address, mask) in [
            ("redirected enqueue", expected.submit + 0x3b, 0x40),
            ("consumer control flow", expected.consume + 0x99, 1),
            (
                "native composition enqueue",
                expected.composite + 0x4b1,
                0x40,
            ),
        ] {
            image[address as usize] ^= mask;
            assert!(resolve(&PeImage::new(&image, base)).is_err(), "{name}");
            image[address as usize] ^= mask;
        }
        let (data_rva, data) = PeImage::new(&image, base).section(".data").unwrap();
        let name = b".?AVCGRenderTargetView@DLCG3@@";
        let descriptor = data_rva as usize
            + data
                .windows(name.len())
                .position(|bytes| bytes == name)
                .unwrap();
        image[descriptor] ^= 1;
        assert!(resolve(&PeImage::new(&image, base)).is_err());
        image[descriptor] ^= 1;
        assert!(resolve(&PeImage::new(&image[..64], base)).is_err());

        let delta = 0x2_0000_0000;
        eroverlay_game::scan::relocate(&mut image, delta).unwrap();
        assert_eq!(
            resolve(&PeImage::new(&image, base + delta as usize)),
            Ok(expected)
        );
    }
}
