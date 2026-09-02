// Peace Walker Sandbox Unlocker v0.8-experimental-compat
// Experimental cross-version build for Metal Gear Solid: Peace Walker
// Master Collection (PC).
//
// IMPORTANT:
// - No exact EXE hash or hard-coded code RVAs are required.
// - The plugin signature-scans the loaded module for the save-state accessor
//   and read-only metadata helper wrappers.
// - The save layout itself is still assumed to match the launch build:
//     availability count/records: state + E2B8/E2BC, stride 0x18
//     R&D count/records:          state + BD38/BD3C, stride 0x1C
//     uniform availability:       state + 1C009 + uniform_id
//     notification queue:         state + 140E4, 50 * 0x10
// - Before any write, the availability table is structurally validated using
//   multiple known content IDs (Stealth Gun specs, Jungle Fatigues, FOX, etc.).
// - If signatures/layout validation fail, NOTHING is modified.
//
// This is intentionally EXPERIMENTAL because it was built without the v1.3.1
// executable. It is designed to survive address shifts/relinks, not arbitrary
// save-layout changes.

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed int i32;
typedef long long i64;
typedef u64 usize;
typedef void* HANDLE;
typedef void* HMODULE;
typedef u32 DWORD;
typedef int BOOL;

#define DLL_PROCESS_ATTACH 1

typedef DWORD (*ThreadProc)(void*);
extern "C" {
__declspec(dllimport) HANDLE CreateThread(void*, usize, ThreadProc, void*, DWORD, DWORD*);
__declspec(dllimport) void Sleep(DWORD);
__declspec(dllimport) HANDLE CreateFileW(const u16*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL CloseHandle(HANDLE);
__declspec(dllimport) HMODULE GetModuleHandleW(const u16*);
}

static u32 str_len(const char* s) { u32 n = 0; while (s[n]) ++n; return n; }

static void write_log(const char* text) {
    static const u16 path[] = {
        'P','W','S','a','n','d','b','o','x','U','n','l','o','c','k','e','r','.','l','o','g',0
    };
    HANDLE h = CreateFileW(path, 0x40000000u, 0x00000001u, 0, 2u, 0x80u, 0);
    if (!h || (i64)(u64)h == -1) return;
    DWORD written = 0;
    WriteFile(h, text, str_len(text), &written, 0);
    CloseHandle(h);
}

static bool match_mask(const u8* p, const u8* pat, const char* mask, u32 n) {
    for (u32 i = 0; i < n; ++i) {
        if (mask[i] == 'x' && p[i] != pat[i]) return false;
    }
    return true;
}

static u8* find_pattern(u8* base, u32 size, const u8* pat, const char* mask, u32 n, u32* matches_out) {
    u8* result = 0;
    u32 matches = 0;
    if (size < n) { if (matches_out) *matches_out = 0; return 0; }
    for (u32 i = 0; i <= size - n; ++i) {
        if (match_mask(base + i, pat, mask, n)) {
            result = base + i;
            ++matches;
        }
    }
    if (matches_out) *matches_out = matches;
    return matches == 1 ? result : 0;
}

static u32 pe_image_size(u8* base) {
    if (!base || *(u16*)base != 0x5A4Du) return 0; // MZ
    u32 lfanew = *(u32*)(base + 0x3C);
    if (lfanew > 0x1000u) return 0;
    u8* pe = base + lfanew;
    if (*(u32*)pe != 0x00004550u) return 0; // PE\0\0
    u8* opt = pe + 24;
    if (*(u16*)opt != 0x020Bu) return 0; // PE32+
    u32 size = *(u32*)(opt + 56); // SizeOfImage
    if (size < 0x100000u || size > 0x40000000u) return 0;
    return size;
}

static u8* rel32_target(u8* instr, u32 disp_off, u32 instr_len) {
    i32 d = *(i32*)(instr + disp_off);
    return instr + instr_len + (i64)d;
}

static bool has_bytes_near(const u8* p, u32 n, const u8* needle, u32 needle_n) {
    if (!p || n < needle_n) return false;
    for (u32 i = 0; i <= n - needle_n; ++i) {
        bool ok = true;
        for (u32 j = 0; j < needle_n; ++j) if (p[i + j] != needle[j]) { ok = false; break; }
        if (ok) return true;
    }
    return false;
}

enum GetterClass { GETTER_UNKNOWN = 0, GETTER_INVENTORY = 1, GETTER_RND = 2 };

static GetterClass classify_getter(u8* target) {
    // Inventory descriptor getter contains: mov rax,[r8+0xB0]
    static const u8 inv_marker[] = {0x49,0x8B,0x80,0xB0,0x00,0x00,0x00};
    // R&D descriptor getter contains: cmp qword ptr [r8+0xA0],0
    static const u8 rnd_marker[] = {0x49,0x83,0xB8,0xA0,0x00,0x00,0x00,0x00};
    if (has_bytes_near(target, 48, inv_marker, sizeof(inv_marker))) return GETTER_INVENTORY;
    if (has_bytes_near(target, 48, rnd_marker, sizeof(rnd_marker))) return GETTER_RND;
    return GETTER_UNKNOWN;
}

static u8* find_metadata_wrapper(u8* base, u32 size, const u8 load4[4], GetterClass wanted) {
    // Wrapper shape:
    // sub rsp,28; call getter; test rax,rax; jne +5; add rsp,28; ret;
    // <4-byte descriptor load>; add rsp,28; ret
    static const u8 prefix[] = {
        0x48,0x83,0xEC,0x28,0xE8,0,0,0,0,0x48,0x85,0xC0,0x75,0x05,
        0x48,0x83,0xC4,0x28,0xC3
    };
    const u32 total = 28;
    u32 found = 0;
    u8* result = 0;
    if (size < total) return 0;
    for (u32 i = 0; i <= size - total; ++i) {
        u8* p = base + i;
        bool pre = true;
        for (u32 j = 0; j < sizeof(prefix); ++j) {
            if (j >= 5 && j <= 8) continue; // call rel32
            if (p[j] != prefix[j]) { pre = false; break; }
        }
        if (!pre) continue;
        if (p[19] != load4[0] || p[20] != load4[1] || p[21] != load4[2] || p[22] != load4[3]) continue;
        if (p[23] != 0x48 || p[24] != 0x83 || p[25] != 0xC4 || p[26] != 0x28 || p[27] != 0xC3) continue;
        u8* getter = rel32_target(p + 4, 1, 5);
        if (classify_getter(getter) != wanted) continue;
        result = p;
        ++found;
    }
    return found == 1 ? result : 0;
}

static bool is_dummy_uniform_devid(u32 id) {
    switch (id) {
        case 0x00B0: case 0x00B1: case 0x00B2: case 0x00B3:
        case 0x00B4: case 0x00B6: case 0x00B7: case 0x00B8:
            return true;
        default: return false;
    }
}

static bool should_unlock_availability_record(u32 id) {
    if (id >= 0x0004 && id <= 0x007D) return true;
    if (id >= 0x0083 && id <= 0x00DB) return !is_dummy_uniform_devid(id);
    if (id >= 0x00DC && id <= 0x0146) return true;
    if (id >= 0x0148 && id <= 0x0152) return true;
    if (id >= 0x0153 && id <= 0x017C) return true;
    return false;
}

static bool is_uniform_devid(u32 id) {
    return id >= 0x0083 && id <= 0x00DB && !is_dummy_uniform_devid(id);
}

static bool is_real_inventory_availability(u32 id) {
    if (id >= 0x00DC && id <= 0x0146) return true;
    if (id >= 0x0148 && id <= 0x017C) return true;
    return false;
}

static bool availability_has_id(u8* records, u32 count, u32 wanted) {
    for (u32 i = 0; i < count; ++i) {
        if (*(u32*)(records + (u64)i * 24u) == wanted) return true;
    }
    return false;
}

static bool validate_availability_layout(u8* state, u32 count) {
    if (!state || count < 300u || count > 1024u) return false;
    u8* records = state + 0xE2BC;
    // Launch build is sequential here; keep this deliberately strict so a
    // changed save structure fails closed instead of corrupting memory.
    if (*(u32*)(records + 0u * 24u) != 0x0001u) return false;
    if (*(u32*)(records + 1u * 24u) != 0x0002u) return false;
    if (*(u32*)(records + 2u * 24u) != 0x0003u) return false;
    if (!availability_has_id(records, count, 0x003Bu)) return false; // Stealth Gun specs
    if (!availability_has_id(records, count, 0x0083u)) return false; // Jungle Fatigues
    if (!availability_has_id(records, count, 0x00B9u)) return false; // FOX T-shirt
    if (!availability_has_id(records, count, 0x017Cu)) return false; // tail anchor
    return true;
}

static u32 acknowledge_stale_development_events(u8* state) {
    if (!state) return 0;
    u8* queue = state + 0x140E4;
    u32 changed = 0;
    for (u32 i = 0; i < 50; ++i) {
        u8* e = queue + (u64)i * 0x10u;
        u8 type = e[0];
        u8 flags = e[1];
        if ((type == 0x13u || type == 0x14u) && (flags & 0x02u)) {
            e[1] = (u8)((flags & (u8)~0x02u) | 0x01u);
            ++changed;
        }
    }
    return changed;
}

static DWORD worker(void*) {
    u8* base = (u8*)GetModuleHandleW(0);
    u32 image_size = pe_image_size(base);
    if (!base || !image_size) {
        write_log("PWSandboxUnlocker v0.8-exp: could not parse the PE image. Nothing was modified.\r\n");
        return 0;
    }

    // Version-independent state accessor signature. RIP displacement is wildcarded.
    static const u8 state_pat[] = {0x48,0x8B,0x05,0,0,0,0,0x48,0x05,0xBC,0xE2,0x00,0x00,0xC3};
    static const char state_mask[] = "xxx????xxxxxxx";
    u32 state_matches = 0;
    u8* state_accessor = find_pattern(base, image_size, state_pat, state_mask, sizeof(state_pat), &state_matches);
    if (!state_accessor || state_matches != 1) {
        write_log("PWSandboxUnlocker v0.8-exp: save-state signature was not unique/found. Nothing was modified.\r\n");
        return 0;
    }
    u8** state_slot = (u8**)rel32_target(state_accessor, 3, 7);
    if ((u8*)state_slot < base || (u8*)state_slot + sizeof(void*) > base + image_size) {
        write_log("PWSandboxUnlocker v0.8-exp: decoded save-state pointer was outside the module. Nothing was modified.\r\n");
        return 0;
    }

    static const u8 load_kind[4] = {0x0F,0xBE,0x40,0x0C};
    static const u8 load_qty[4]  = {0x0F,0xBF,0x40,0x08};
    u8* inv_kind_p = find_metadata_wrapper(base, image_size, load_kind, GETTER_INVENTORY);
    u8* inv_qty_p  = find_metadata_wrapper(base, image_size, load_qty,  GETTER_INVENTORY);
    u8* rnd_kind_p = find_metadata_wrapper(base, image_size, load_kind, GETTER_RND);
    u8* rnd_aux_p  = find_metadata_wrapper(base, image_size, load_qty,  GETTER_RND);
    if (!inv_kind_p || !inv_qty_p || !rnd_kind_p || !rnd_aux_p) {
        write_log("PWSandboxUnlocker v0.8-exp: metadata signatures were ambiguous/missing. Nothing was modified.\r\n");
        return 0;
    }

    typedef int (*IdIntFn)(int);
    IdIntFn inventory_kind = (IdIntFn)inv_kind_p;
    IdIntFn default_inventory_quantity = (IdIntFn)inv_qty_p;
    IdIntFn rnd_kind = (IdIntFn)rnd_kind_p;
    IdIntFn rnd_aux_value = (IdIntFn)rnd_aux_p;

    write_log(
        "PWSandboxUnlocker v0.8-exp: EXPERIMENTAL signature-scanning build loaded.\r\n"
        "No exact EXE hash/RVAs required. Waiting for a structurally validated Peace Walker save state.\r\n"
    );

    u32 stable_ready_ticks = 0;
    bool layout_accepted = false;
    bool logged_success = false;

    for (;;) {
        u8* state = *state_slot;
        if (!state) {
            stable_ready_ticks = 0;
            Sleep(100);
            continue;
        }

        u32 availability_count = *(u32*)(state + 0xE2B8);
        if (!validate_availability_layout(state, availability_count)) {
            stable_ready_ticks = 0;
            Sleep(100);
            continue;
        }

        if (stable_ready_ticks < 20) {
            ++stable_ready_ticks;
            Sleep(100);
            continue;
        }

        if (!layout_accepted) {
            write_log(
                "PWSandboxUnlocker v0.8-exp: layout validation PASSED. Applying sandbox state.\r\n"
                "WARNING: this build is experimental on game versions other than the launch executable.\r\n"
            );
            layout_accepted = true;
        }

        acknowledge_stale_development_events(state);

        u8* availability_records = state + 0xE2BC;
        u32 availability_targeted = 0, availability_complete = 0;
        u32 uniform_targeted = 0, uniform_available = 0;

        for (u32 i = 0; i < availability_count; ++i) {
            u8* r = availability_records + (u64)i * 24u;
            u32 id = *(u32*)(r + 0);
            if (!should_unlock_availability_record(id)) continue;
            ++availability_targeted;

            *(u32*)(r + 4) = 3u;
            *(u32*)(r + 8) = 100u;

            if (is_real_inventory_availability(id)) {
                int qty = default_inventory_quantity((int)id);
                if (qty < 1) qty = 1;
                if (qty > 9999) qty = 9999;
                *(u32*)(r + 0x0C) = (u32)qty;
                int kind = inventory_kind((int)id);
                if (kind >= 1 && kind <= 3)
                    *(u32*)(r + 0x10) = (*(u32*)(r + 0x10) | 1u) & ~2u;
            }

            if (*(u32*)(r + 4) == 3u && *(u32*)(r + 8) >= 100u) ++availability_complete;

            if (is_uniform_devid(id)) {
                ++uniform_targeted;
                int uniform_id = (int)(id - 0x73u);
                u8* ub = state + 0x1C009 + uniform_id;
                *ub = (u8)(*ub | 0x01u); // available, without calling version-specific code
                if ((*ub & 0x01u) != 0) ++uniform_available;
            }
        }

        u32 rnd_count = *(u32*)(state + 0xBD38);
        u32 rnd_complete = 0;
        bool rnd_valid = rnd_count < 0x55Bu;
        if (rnd_valid && rnd_count > 0) {
            u8* rnd_records = state + 0xBD3C;
            for (u32 i = 0; i < rnd_count; ++i) {
                u8* r = rnd_records + (u64)i * 0x1Cu;
                u32 id = *(u32*)(r + 0);
                // Reject obviously nonsensical IDs before writing this record.
                if (id == 0u || id > 0x10000u) { rnd_valid = false; break; }

                *(u32*)(r + 4) = 3u;
                *(u32*)(r + 8) = 100u;
                *(u32*)(r + 0x10) = (*(u32*)(r + 0x10) | 1u) & ~2u;

                int k = rnd_kind((int)id);
                if (k < 2 || k > 8) {
                    int v = rnd_aux_value((int)id);
                    if (v < 0) v = 0;
                    if (v > 9999) v = 9999;
                    *(u32*)(r + 0x0C) = (u32)v;
                }

                if (*(u32*)(r + 4) == 3u && *(u32*)(r + 8) >= 100u &&
                    ((*(u32*)(r + 0x10) & 1u) != 0u) && ((*(u32*)(r + 0x10) & 2u) == 0u))
                    ++rnd_complete;
            }
        }

        if (!rnd_valid) {
            write_log("PWSandboxUnlocker v0.8-exp: R&D layout sanity check failed. Further R&D writes stopped for this run.\r\n");
            return 0;
        }

        if (!logged_success && availability_targeted > 300 && availability_complete == availability_targeted &&
            uniform_targeted > 70 && uniform_available == uniform_targeted && rnd_count > 0 && rnd_complete == rnd_count) {
            write_log(
                "PWSandboxUnlocker v0.8-exp: SUCCESS. Signature scan + layout validation passed; sandbox state applied.\r\n"
                "If this is a newer game patch, please report the game version and attach this log when confirming compatibility.\r\n"
            );
            logged_success = true;
        }

        Sleep(100);
    }
}

extern "C" BOOL DllMain(HMODULE, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE thread = CreateThread(0, 0, worker, 0, 0, 0);
        if (thread) CloseHandle(thread);
    }
    return 1;
}
