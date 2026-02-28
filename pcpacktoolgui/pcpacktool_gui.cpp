// pcpacktool_gui.cpp - Ultimate Spider-Man PCPACK GUI Tool (Pure Win32)
// =========================================================================
// Zero external dependencies. Uses native Win32 controls: ListView, menus,
// common dialogs, status bar, drag-and-drop.
//
// Build (MinGW):
//   g++ -std=c++17 -O2 -DUNICODE -D_UNICODE pcpacktool_gui.cpp -o pcpacktool_gui.exe ^
//       -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -ldwmapi -luxtheme -lole32 -mwindows
//
// Build (MSVC):
//   cl /std:c++17 /O2 /DUNICODE /D_UNICODE pcpacktool_gui.cpp ^
//      gdi32.lib comctl32.lib comdlg32.lib shell32.lib dwmapi.lib uxtheme.lib ole32.lib ^
//      user32.lib /link /SUBSYSTEM:WINDOWS
//
// Or use the provided CMakeLists.txt.

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <memory>

namespace fs = std::filesystem;

// ============================================================================
//  PCPACK Structures
// ============================================================================

#pragma pack(push, 1)

struct resource_versions {
    uint32_t field_0, field_4, field_8, field_C, field_10;
};
static_assert(sizeof(resource_versions) == 0x14, "");

struct resource_pack_header {
    resource_versions field_0;
    uint32_t          field_14;
    uint32_t          directory_offset;
    uint32_t          res_dir_mash_size; // base for payload
    uint32_t          field_20;
    uint32_t          field_24;
    uint32_t          field_28;
};
static_assert(sizeof(resource_pack_header) == 0x2C, "");

struct generic_mash_header {
    int32_t safety_key;
    int32_t field_4;
    int32_t field_8;
    int16_t class_id;
    int16_t field_E;
};
static_assert(sizeof(generic_mash_header) == 0x10, "");

struct string_hash { uint32_t source_hash_code; };
struct resource_key { string_hash m_hash; uint32_t m_type; };

struct resource_location {
    resource_key field_0;
    uint32_t     m_offset;
    uint32_t     m_size;
};
static_assert(sizeof(resource_location) == 0x10, "");

template<typename T>
struct mashable_vector_t {
    uint32_t m_data;
    uint16_t m_size;
    uint8_t  m_shared;
    uint8_t  field_7;
};
static_assert(sizeof(mashable_vector_t<uint32_t>) == 8, "");

struct tlresource_location {
    string_hash name;
    uint8_t     type;
    uint8_t     pad[3];
    uint32_t    offset;
};
static_assert(sizeof(tlresource_location) == 0x0C, "");

struct resource_directory {
    mashable_vector_t<int32_t>             parents;
    mashable_vector_t<resource_location>   resource_locations;
    mashable_vector_t<tlresource_location> texture_locations;
    mashable_vector_t<tlresource_location> mesh_file_locations;
    mashable_vector_t<tlresource_location> mesh_locations;
    mashable_vector_t<tlresource_location> morph_file_locations;
    mashable_vector_t<tlresource_location> morph_locations;
    mashable_vector_t<tlresource_location> material_file_locations;
    mashable_vector_t<tlresource_location> material_locations;
    mashable_vector_t<tlresource_location> anim_file_locations;
    mashable_vector_t<tlresource_location> anim_locations;
    mashable_vector_t<tlresource_location> scene_anim_locations;
    mashable_vector_t<tlresource_location> skeleton_locations;
    mashable_vector_t<int32_t>             field_68;
    mashable_vector_t<int32_t>             field_70;
    int32_t pack_slot;
    int32_t base;
    int32_t field_80;
    int32_t field_84;
    int32_t field_88;
    int32_t type_start_idxs[70];
    int32_t type_end_idxs[70];
};
static_assert(sizeof(resource_directory) == 0x2BC, "");

#pragma pack(pop)

// ============================================================================
//  Type Extension Table
// ============================================================================

static const char* resource_type_ext[] = {
    ".NONE",".PCANIM",".PCSKEL",".ALS",".ENT",".ENTEXT",".DDS",".DDSMP",
    ".IFL",".DESC",".ENS",".SPL",".AB",".QP",".TRIG",".PCSX",".INST",
    ".FDF",".PANEL",".TXT",".ICN",".PCMESH",".PCMORPH",".PCMAT",".COLL",
    ".PCPACK",".PCSANIM",".MSN",".MARKER",".HH",".WAV",".WBK",".M2V",
    "M2V",".PFX",".CSV",".CLE",".LIT",".GRD",".GLS",".LOD",".SIN",
    ".GV",".SV",".TOKENS",".DSG",".PATH",".PTRL",".LANG",".SLF",
    ".VISEME",".PCMESHDEF",".PCMORPHDEF",".PCMATDEF",".MUT",".ASG",
    ".BAI",".CUT",".INTERACT",".CSV",".CSV","._ENTID_","._ANIMID_",
    "._REGIONID_","._AI_GENERIC_ID_","._RADIOMSG_","._GOAL_",
    "._IFC_ATTRIBUTE_","._SIGNAL_","._PACKGROUP_"
};
static constexpr int NUM_RESOURCE_TYPES = sizeof(resource_type_ext) / sizeof(resource_type_ext[0]);

static COLORREF get_type_color(const char* ext) {
    if (strcmp(ext, ".DDS") == 0 || strcmp(ext, ".DDSMP") == 0)   return RGB(232, 164, 74);
    if (strcmp(ext, ".PCMESH") == 0 || strcmp(ext, ".PCMESHDEF") == 0) return RGB(74, 232, 138);
    if (strcmp(ext, ".PCMAT") == 0 || strcmp(ext, ".PCMATDEF") == 0)   return RGB(138, 74, 232);
    if (strcmp(ext, ".PCANIM") == 0 || strcmp(ext, ".PCSANIM") == 0)   return RGB(232, 74, 106);
    if (strcmp(ext, ".PCSKEL") == 0)     return RGB(74, 138, 232);
    if (strcmp(ext, ".PCMORPH") == 0 || strcmp(ext, ".PCMORPHDEF") == 0) return RGB(232, 74, 232);
    if (strcmp(ext, ".WAV") == 0 || strcmp(ext, ".WBK") == 0) return RGB(74, 232, 232);
    if (strcmp(ext, ".PCSX") == 0)       return RGB(232, 232, 74);
    if (strcmp(ext, ".ALS") == 0)        return RGB(255, 136, 85);
    if (strcmp(ext, ".ENT") == 0 || strcmp(ext, ".ENTEXT") == 0) return RGB(85, 255, 136);
    return RGB(160, 160, 170);
}

// ============================================================================
//  Helpers
// ============================================================================

static std::unordered_map<uint32_t, std::string> g_hashDict;      // hash -> name
static std::unordered_map<std::string, uint32_t> g_nameToHash;    // name -> hash (reverse)

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::toupper(c); });
    return s;
}
static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static void load_hash_dictionary(const fs::path& path) {
    if (path.empty() || !fs::exists(path)) return;
    g_hashDict.clear();
    g_nameToHash.clear();

    std::ifstream f(path);
    if (!f) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string hex_str, name;
        if (!(iss >> hex_str >> name)) continue;

        if (hex_str.size() >= 2 && hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) {
            uint32_t v = (uint32_t)std::stoul(hex_str, nullptr, 16);
            g_hashDict[v] = name;

            // reverse lookup (case-insensitive)
            g_nameToHash[name] = v;
            std::string lower = to_lower(name);
            g_nameToHash[lower] = v;
        }
    }
}

static const char* get_ext(uint32_t type) {
    return (type < (uint32_t)NUM_RESOURCE_TYPES) ? resource_type_ext[type] : ".UNK";
}

static std::string get_filename(uint32_t hash, uint32_t type) {
    auto it = g_hashDict.find(hash);
    char hex[16]; snprintf(hex, sizeof(hex), "0x%08X", hash);
    return ((it != g_hashDict.end()) ? it->second : std::string(hex)) + get_ext(type);
}

static std::string sanitize_filename(const std::string& name) {
    std::string r = name;
    for (char& c : r) {
        if (c < 32 || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '/' || c == '\\') c = '_';
    }
    return r;
}

static std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path.string());
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    if (sz > 0) f.read((char*)data.data(), sz);
    return data;
}

static void write_file(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write: " + path.string());
    if (!data.empty()) f.write((const char*)data.data(), data.size());
}

static size_t align_up(size_t x, size_t a) {
    if (a <= 1) return x;
    size_t m = x % a;
    return m ? x + (a - m) : x;
}

static std::string format_size(uint64_t bytes) {
    char buf[64];
    if (bytes < 1024) snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else if (bytes < 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

static std::string format_hex(uint32_t v) {
    char buf[16]; snprintf(buf, sizeof(buf), "0x%08X", v);
    return buf;
}

static int type_from_ext_ci(const std::string& ext) {
    std::string u = to_upper(ext);
    for (int i = 0; i < NUM_RESOURCE_TYPES; ++i) {
        std::string te = resource_type_ext[i] ? resource_type_ext[i] : "";
        if (to_upper(te) == u) return i;
    }
    return -1;
}

struct ParsedName {
    bool ok = false;
    uint32_t hash = 0;
    uint32_t type = 0;
};

static ParsedName parse_folder_filename(const fs::path& p) {
    ParsedName r;
    std::string ext = p.extension().string();
    int t = type_from_ext_ci(ext);
    if (t < 0) return r;

    std::string stem = p.stem().string();

    // allow "0x12345678"
    if (stem.size() >= 2 && stem[0] == '0' && (stem[1] == 'x' || stem[1] == 'X')) {
        try {
            r.hash = (uint32_t)std::stoul(stem, nullptr, 16);
            r.type = (uint32_t)t;
            r.ok = true;
            return r;
        } catch (...) {
            return r;
        }
    }

    // dictionary name -> hash (case-insensitive)
    {
        auto it = g_nameToHash.find(stem);
        if (it != g_nameToHash.end()) {
            r.hash = it->second;
            r.type = (uint32_t)t;
            r.ok = true;
            return r;
        }
        std::string lower = to_lower(stem);
        it = g_nameToHash.find(lower);
        if (it != g_nameToHash.end()) {
            r.hash = it->second;
            r.type = (uint32_t)t;
            r.ok = true;
            return r;
        }
    }
    return r;
}

static uint64_t make_key(uint32_t hash, uint32_t type) {
    return (uint64_t(type) << 32) | uint64_t(hash);
}

// ============================================================================
//  Parse PCPACK
// ============================================================================

struct ResourceEntry {
    int         index;
    uint32_t    hash;
    uint32_t    type;
    uint32_t    offset;
    uint32_t    size;
    std::string filename;
    std::string ext;
};

struct ParsedPack {
    std::vector<uint8_t> raw;
    std::string source_path;

    resource_pack_header pack_header;
    generic_mash_header  mash_header;
    resource_directory   dir;

    std::vector<int32_t>             parents;
    std::vector<resource_location>   res_locs;
    std::vector<tlresource_location> textures, mesh_files, meshes;
    std::vector<tlresource_location> morph_files, morphs;
    std::vector<tlresource_location> material_files, materials;
    std::vector<tlresource_location> anim_files, anims;
    std::vector<tlresource_location> scene_anims, skeletons;

    std::vector<ResourceEntry> entries;
    uint32_t base() const { return pack_header.res_dir_mash_size; }
};

static ParsedPack parse_pcpack(const fs::path& path) {
    ParsedPack P;
    P.raw = read_file(path);
    P.source_path = path.string();

    if (P.raw.size() < sizeof(resource_pack_header))
        throw std::runtime_error("File too small");

    memcpy(&P.pack_header, P.raw.data(), sizeof(P.pack_header));
    uint32_t dir_off = P.pack_header.directory_offset;
    if (dir_off + sizeof(generic_mash_header) + sizeof(resource_directory) > P.raw.size())
        throw std::runtime_error("Invalid directory offset");

    memcpy(&P.mash_header, &P.raw[dir_off], sizeof(P.mash_header));
    memcpy(&P.dir, &P.raw[dir_off + sizeof(generic_mash_header)], sizeof(P.dir));

    size_t pos = dir_off + sizeof(generic_mash_header) + sizeof(resource_directory);
    auto ra = [&]() { pos = align_up(pos, 8); pos = align_up(pos, 4); };

    auto ri32 = [&](uint16_t n) {
        ra(); std::vector<int32_t> v(n);
        if (n) { memcpy(v.data(), &P.raw[pos], n * 4); pos += n * 4; }
        pos = align_up(pos, 4); return v;
    };
    auto rrl = [&](uint16_t n) {
        ra(); std::vector<resource_location> v(n);
        if (n) { memcpy(v.data(), &P.raw[pos], n * sizeof(resource_location)); pos += n * sizeof(resource_location); }
        pos = align_up(pos, 4); return v;
    };
    auto rtl = [&](uint16_t n) {
        ra(); std::vector<tlresource_location> v(n);
        if (n) { memcpy(v.data(), &P.raw[pos], n * sizeof(tlresource_location)); pos += n * sizeof(tlresource_location); }
        pos = align_up(pos, 4); return v;
    };

    P.parents        = ri32(P.dir.parents.m_size);
    P.res_locs       = rrl(P.dir.resource_locations.m_size);
    P.textures       = rtl(P.dir.texture_locations.m_size);
    P.mesh_files     = rtl(P.dir.mesh_file_locations.m_size);
    P.meshes         = rtl(P.dir.mesh_locations.m_size);
    P.morph_files    = rtl(P.dir.morph_file_locations.m_size);
    P.morphs         = rtl(P.dir.morph_locations.m_size);
    P.material_files = rtl(P.dir.material_file_locations.m_size);
    P.materials      = rtl(P.dir.material_locations.m_size);
    P.anim_files     = rtl(P.dir.anim_file_locations.m_size);
    P.anims          = rtl(P.dir.anim_locations.m_size);
    P.scene_anims    = rtl(P.dir.scene_anim_locations.m_size);
    P.skeletons      = rtl(P.dir.skeleton_locations.m_size);

    P.entries.resize(P.res_locs.size());
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        auto& e = P.entries[i];
        auto& rl = P.res_locs[i];
        e.index = (int)i;
        e.hash = rl.field_0.m_hash.source_hash_code;
        e.type = rl.field_0.m_type;
        e.offset = rl.m_offset;
        e.size = rl.m_size;
        e.filename = sanitize_filename(get_filename(e.hash, e.type));
        e.ext = get_ext(e.type);
    }
    return P;
}

// ============================================================================
//  Export
// ============================================================================

static void do_export_all(const ParsedPack& P, const fs::path& out_dir) {
    fs::create_directories(out_dir);
    std::ofstream manifest(out_dir / "_manifest.txt");
    manifest << "# PCPACK Manifest\n# base=" << P.base() << "\n# resources=" << P.res_locs.size() << "\n\n";

    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        const auto& rl = P.res_locs[i];
        uint64_t start = (uint64_t)P.base() + rl.m_offset;
        uint64_t end = start + rl.m_size;
        if (end > P.raw.size()) continue;
        std::ofstream of(out_dir / P.entries[i].filename, std::ios::binary);
        if (of) of.write((const char*)&P.raw[start], rl.m_size);
        manifest << i << " 0x" << std::hex << rl.field_0.m_hash.source_hash_code
                 << " " << std::dec << rl.field_0.m_type
                 << " 0x" << std::hex << rl.m_offset << " 0x" << rl.m_size
                 << " " << P.entries[i].filename << "\n";
    }
}

static void do_export_single(const ParsedPack& P, int index, const fs::path& out_path) {
    const auto& rl = P.res_locs[index];
    uint64_t start = (uint64_t)P.base() + rl.m_offset;
    if (start + rl.m_size > P.raw.size()) throw std::runtime_error("Out of bounds");
    std::ofstream of(out_path, std::ios::binary);
    if (!of) throw std::runtime_error("Cannot write");
    of.write((const char*)&P.raw[start], rl.m_size);
}

// ============================================================================
//  Import / Rebuild (replace existing by index)
// ============================================================================

static std::vector<uint8_t> do_import(
    ParsedPack& P,
    const std::unordered_map<int, std::vector<uint8_t>>& replacements,
    size_t align_val)
{
    struct NR { uint32_t new_offset, new_size; std::vector<uint8_t> data; };
    std::vector<NR> nres(P.res_locs.size());

    uint32_t cursor = 0;
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        const auto& rl = P.res_locs[i];
        auto it = replacements.find((int)i);
        if (it != replacements.end()) {
            nres[i].data = it->second;
            nres[i].new_size = (uint32_t)it->second.size();
        } else {
            uint64_t s = (uint64_t)P.base() + rl.m_offset;
            nres[i].data.assign(&P.raw[s], &P.raw[s] + rl.m_size);
            nres[i].new_size = rl.m_size;
        }
        cursor = (uint32_t)align_up(cursor, align_val);
        nres[i].new_offset = cursor;
        cursor += nres[i].new_size;
    }

    auto utl = [&](uint32_t old_off) -> uint32_t {
        for (size_t i = 0; i < P.res_locs.size(); ++i) {
            uint32_t rs = P.res_locs[i].m_offset, re = rs + P.res_locs[i].m_size;
            if (old_off >= rs && old_off < re)
                return nres[i].new_offset + (old_off - rs);
        }
        return old_off;
    };
    auto uv = [&](std::vector<tlresource_location>& v) { for (auto& t : v) t.offset = utl(t.offset); };

    uv(P.textures); uv(P.mesh_files); uv(P.meshes);
    uv(P.morph_files); uv(P.morphs);
    uv(P.material_files); uv(P.materials);
    uv(P.anim_files); uv(P.anims);
    uv(P.scene_anims); uv(P.skeletons);

    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        P.res_locs[i].m_offset = nres[i].new_offset;
        P.res_locs[i].m_size = nres[i].new_size;
    }

    std::vector<uint8_t> out;
    out.resize(sizeof(resource_pack_header));
    memcpy(out.data(), &P.pack_header, sizeof(P.pack_header));
    if (out.size() < P.pack_header.directory_offset) out.resize(P.pack_header.directory_offset, 0);
    out.insert(out.end(), (uint8_t*)&P.mash_header, (uint8_t*)&P.mash_header + sizeof(P.mash_header));
    out.insert(out.end(), (uint8_t*)&P.dir, (uint8_t*)&P.dir + sizeof(P.dir));

    auto ea = [&](size_t a, uint8_t f = 0xE3) {
        size_t w = align_up(out.size(), a);
        if (w > out.size()) out.insert(out.end(), w - out.size(), f);
    };
    auto ei = [&](const std::vector<int32_t>& v) {
        ea(8); ea(4);
        if (!v.empty()) { auto* p = (const uint8_t*)v.data(); out.insert(out.end(), p, p + v.size() * 4); }
        ea(4);
    };
    auto er = [&](const std::vector<resource_location>& v) {
        ea(8); ea(4);
        if (!v.empty()) { auto* p = (const uint8_t*)v.data(); out.insert(out.end(), p, p + v.size() * sizeof(resource_location)); }
        ea(4);
    };
    auto et = [&](const std::vector<tlresource_location>& v) {
        ea(8); ea(4);
        if (!v.empty()) { auto* p = (const uint8_t*)v.data(); out.insert(out.end(), p, p + v.size() * sizeof(tlresource_location)); }
        ea(4);
    };

    ei(P.parents); er(P.res_locs);
    et(P.textures); et(P.mesh_files); et(P.meshes);
    et(P.morph_files); et(P.morphs);
    et(P.material_files); et(P.materials);
    et(P.anim_files); et(P.anims);
    et(P.scene_anims); et(P.skeletons);

    if (out.size() < P.base()) out.resize(P.base(), 0xE3);

    for (size_t i = 0; i < nres.size(); ++i) {
        size_t s = (size_t)P.base() + nres[i].new_offset;
        if (out.size() < s + nres[i].new_size) out.resize(s + nres[i].new_size, 0);
        if (nres[i].new_size)
            memcpy(&out[s], nres[i].data.data(), nres[i].new_size);
    }
    return out;
}

// ============================================================================
//  Reimport (Folder Sync + Add New + Reorder by type then hash)
// ============================================================================
//  Reimport (Folder Sync + Add New + Reorder by type then hash)
//   - NEW: also adds TL entries for newly added resources (when applicable)
//   - NEW: patches pack header res_dir_mash_size (+ directory_offset safety)
// ============================================================================

static std::vector<uint8_t> do_reimport_from_folder(
    ParsedPack& P,
    const fs::path& folder,
    size_t align_val,
    std::string* out_log)
{
    if (!fs::exists(folder) || !fs::is_directory(folder))
        throw std::runtime_error("Reimport folder does not exist or is not a directory.");

    struct OldRes { uint32_t old_off, old_size; };
    std::vector<OldRes> old(P.res_locs.size());
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        old[i].old_off = P.res_locs[i].m_offset;
        old[i].old_size = P.res_locs[i].m_size;
    }

    std::unordered_map<uint64_t, int> key_to_index;
    key_to_index.reserve(P.res_locs.size() * 2);
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        uint32_t h = P.res_locs[i].field_0.m_hash.source_hash_code;
        uint32_t t = P.res_locs[i].field_0.m_type;
        key_to_index[make_key(h, t)] = (int)i;
    }

    struct Item {
        uint32_t hash = 0;
        uint32_t type = 0;
        std::vector<uint8_t> data;
        bool has_old = false;
        uint32_t old_off = 0;
        uint32_t old_size = 0;
    };

    std::vector<Item> items;
    items.reserve(P.res_locs.size() + 256);

    // Seed from old pack (keeps old_off/old_size for TL remap)
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        Item it;
        it.hash = P.res_locs[i].field_0.m_hash.source_hash_code;
        it.type = P.res_locs[i].field_0.m_type;
        it.has_old = true;
        it.old_off = old[i].old_off;
        it.old_size = old[i].old_size;

        uint64_t s = (uint64_t)P.base() + old[i].old_off;
        uint64_t e = s + old[i].old_size;
        if (e > P.raw.size()) throw std::runtime_error("Corrupted pack: resource out of bounds.");
        it.data.assign(&P.raw[s], &P.raw[s] + old[i].old_size);
        items.push_back(std::move(it));
    }

    auto log_append = [&](const std::string& s) {
        if (out_log) { *out_log += s; *out_log += "\r\n"; }
        };

    int updated = 0, added = 0, skipped = 0;

    // Apply folder changes (update existing, add new)
    for (auto& de : fs::directory_iterator(folder)) {
        if (!de.is_regular_file()) continue;

        ParsedName pn = parse_folder_filename(de.path());
        if (!pn.ok) { skipped++; continue; }

        std::vector<uint8_t> fileData;
        try { fileData = read_file(de.path()); }
        catch (...) { skipped++; continue; }

        uint64_t key = make_key(pn.hash, pn.type);
        auto itIdx = key_to_index.find(key);
        if (itIdx != key_to_index.end()) {
            int idx = itIdx->second;
            if (idx >= 0 && idx < (int)items.size()) {
                items[idx].data = std::move(fileData);
                updated++;
            }
            else {
                skipped++;
            }
        }
        else {
            Item ni;
            ni.hash = pn.hash;
            ni.type = pn.type;
            ni.data = std::move(fileData);
            ni.has_old = false;
            items.push_back(std::move(ni));
            key_to_index[key] = (int)items.size() - 1;
            added++;
        }
    }

    log_append("Reimport folder: " + folder.string());
    log_append("Updated: " + std::to_string(updated) + ", Added: " + std::to_string(added) + ", Skipped: " + std::to_string(skipped));

    // Sort by type then hash
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.type != b.type) return a.type < b.type;
        return a.hash < b.hash;
        });

    // Rebuild resource_locations
    P.res_locs.clear();
    P.res_locs.resize(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        P.res_locs[i].field_0.m_hash.source_hash_code = items[i].hash;
        P.res_locs[i].field_0.m_type = items[i].type;
        P.res_locs[i].m_offset = 0;
        P.res_locs[i].m_size = (uint32_t)items[i].data.size();
    }

    // Update dir counts + type ranges
    P.dir.resource_locations.m_size = (uint16_t)P.res_locs.size();
    for (int t = 0; t < 70; ++t) {
        P.dir.type_start_idxs[t] = 0;
        P.dir.type_end_idxs[t] = 0; // COUNT
    }
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        uint32_t t = P.res_locs[i].field_0.m_type;
        if (t >= 70) continue;

        if (P.dir.type_end_idxs[t] == 0)
            P.dir.type_start_idxs[t] = (int32_t)i;

        P.dir.type_end_idxs[t] += 1;
    }

    // Assign new offsets in payload
    uint32_t cursor = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        cursor = (uint32_t)align_up(cursor, align_val);
        P.res_locs[i].m_offset = cursor;
        cursor += (uint32_t)items[i].data.size();
    }

    // ------------------------------------------------------------------------
    // NEW: Add TL entries for any newly added resources (if they belong to TL sets)
    // ------------------------------------------------------------------------
    auto tl_has = [](const std::vector<tlresource_location>& v, uint32_t h, uint8_t t8) {
        for (auto& x : v) {
            if (x.name.source_hash_code == h && x.type == t8) return true;
        }
        return false;
        };

    // Map resource type -> TL vector to update
    auto tl_vector_for_type = [&](uint32_t rtype) -> std::vector<tlresource_location>*{
        const char* ext = get_ext(rtype);

        // Pragmatic mapping used by USM packs:
        if (!strcmp(ext, ".DDS") || !strcmp(ext, ".DDSMP")) return &P.textures;

        if (!strcmp(ext, ".PCMESHDEF")) return &P.mesh_files;
        if (!strcmp(ext, ".PCMESH"))    return &P.meshes;

        if (!strcmp(ext, ".PCMORPHDEF")) return &P.morph_files;
        if (!strcmp(ext, ".PCMORPH"))    return &P.morphs;

        if (!strcmp(ext, ".PCMATDEF")) return &P.material_files;
        if (!strcmp(ext, ".PCMAT"))    return &P.materials;

        if (!strcmp(ext, ".PCANIM"))  return &P.anims;
        if (!strcmp(ext, ".PCSANIM")) return &P.scene_anims;

        if (!strcmp(ext, ".PCSKEL")) return &P.skeletons;

        return nullptr;
        };

    // Add TL for new items ONLY
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].has_old) continue;

        auto* vec = tl_vector_for_type(items[i].type);
        if (!vec) continue;

        // tlresource_location.type is uint8; in most packs it's the resource type index (truncated).
        uint8_t t8 = (uint8_t)items[i].type;

        if (tl_has(*vec, items[i].hash, t8))
            continue;

        tlresource_location tl{};
        tl.name.source_hash_code = items[i].hash;
        tl.type = t8;
        tl.offset = P.res_locs[i].m_offset; // offsets are relative to base/payload
        vec->push_back(tl);
    }

    // Keep TL vectors sorted (nice + stable for tools)
    auto tl_sort = [](std::vector<tlresource_location>& v) {
        std::sort(v.begin(), v.end(), [](const tlresource_location& a, const tlresource_location& b) {
            if (a.type != b.type) return a.type < b.type;
            return a.name.source_hash_code < b.name.source_hash_code;
            });
        };
    tl_sort(P.textures); tl_sort(P.mesh_files); tl_sort(P.meshes);
    tl_sort(P.morph_files); tl_sort(P.morphs);
    tl_sort(P.material_files); tl_sort(P.materials);
    tl_sort(P.anim_files); tl_sort(P.anims);
    tl_sort(P.scene_anims); tl_sort(P.skeletons);

    // Update TL counts in directory
    P.dir.texture_locations.m_size = (uint16_t)P.textures.size();
    P.dir.mesh_file_locations.m_size = (uint16_t)P.mesh_files.size();
    P.dir.mesh_locations.m_size = (uint16_t)P.meshes.size();
    P.dir.morph_file_locations.m_size = (uint16_t)P.morph_files.size();
    P.dir.morph_locations.m_size = (uint16_t)P.morphs.size();
    P.dir.material_file_locations.m_size = (uint16_t)P.material_files.size();
    P.dir.material_locations.m_size = (uint16_t)P.materials.size();
    P.dir.anim_file_locations.m_size = (uint16_t)P.anim_files.size();
    P.dir.anim_locations.m_size = (uint16_t)P.anims.size();
    P.dir.scene_anim_locations.m_size = (uint16_t)P.scene_anims.size();
    P.dir.skeleton_locations.m_size = (uint16_t)P.skeletons.size();

    // ------------------------------------------------------------------------
    // Remap OLD TL offsets -> NEW (only for existing ones). Newly added TL entries
    // already point at new offsets.
    // ------------------------------------------------------------------------
    auto old_to_new = [&](uint32_t old_off) -> uint32_t {
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].has_old) continue;
            uint32_t rs = items[i].old_off;
            uint32_t re = rs + items[i].old_size;
            if (old_off >= rs && old_off < re) {
                uint32_t h = items[i].hash, t = items[i].type;
                for (size_t j = 0; j < P.res_locs.size(); ++j) {
                    if (P.res_locs[j].field_0.m_hash.source_hash_code == h &&
                        P.res_locs[j].field_0.m_type == t) {
                        return P.res_locs[j].m_offset + (old_off - rs);
                    }
                }
                return old_off;
            }
        }
        return old_off;
        };

    auto remap_tl = [&](std::vector<tlresource_location>& v) {
        for (auto& t : v) {
            // If this TL points inside an old resource, remap.
            // If it's new, it won't match any old containment and will remain unchanged.
            t.offset = old_to_new(t.offset);
        }
        };
    remap_tl(P.textures); remap_tl(P.mesh_files); remap_tl(P.meshes);
    remap_tl(P.morph_files); remap_tl(P.morphs);
    remap_tl(P.material_files); remap_tl(P.materials);
    remap_tl(P.anim_files); remap_tl(P.anims);
    remap_tl(P.scene_anims); remap_tl(P.skeletons);

    // ------------------------------------------------------------------------
    // Serialize directory (can grow) -> recompute base/res_dir_mash_size
    // ------------------------------------------------------------------------
    std::vector<uint8_t> out;
    out.resize(sizeof(resource_pack_header), 0);

    resource_pack_header hdr = P.pack_header;

    // safety: directory_offset must be >= header size
    if (hdr.directory_offset < sizeof(resource_pack_header))
        hdr.directory_offset = (uint32_t)sizeof(resource_pack_header);

    if (out.size() < hdr.directory_offset)
        out.resize(hdr.directory_offset, 0);

    out.insert(out.end(), (uint8_t*)&P.mash_header, (uint8_t*)&P.mash_header + sizeof(P.mash_header));
    out.insert(out.end(), (uint8_t*)&P.dir, (uint8_t*)&P.dir + sizeof(P.dir));

    auto ea = [&](size_t a, uint8_t f = 0xE3) {
        size_t w = align_up(out.size(), a);
        if (w > out.size()) out.insert(out.end(), w - out.size(), f);
        };
    auto ei = [&](const std::vector<int32_t>& v) {
        ea(8); ea(4);
        if (!v.empty()) {
            auto* p = (const uint8_t*)v.data();
            out.insert(out.end(), p, p + v.size() * 4);
        }
        ea(4);
        };
    auto er = [&](const std::vector<resource_location>& v) {
        ea(8); ea(4);
        if (!v.empty()) {
            auto* p = (const uint8_t*)v.data();
            out.insert(out.end(), p, p + v.size() * sizeof(resource_location));
        }
        ea(4);
        };
    auto et = [&](const std::vector<tlresource_location>& v) {
        ea(8); ea(4);
        if (!v.empty()) {
            auto* p = (const uint8_t*)v.data();
            out.insert(out.end(), p, p + v.size() * sizeof(tlresource_location));
        }
        ea(4);
        };

    ei(P.parents);
    er(P.res_locs);
    et(P.textures); et(P.mesh_files); et(P.meshes);
    et(P.morph_files); et(P.morphs);
    et(P.material_files); et(P.materials);
    et(P.anim_files); et(P.anims);
    et(P.scene_anims); et(P.skeletons);

    uint32_t new_base = (uint32_t)align_up(out.size(), 16);
    if (out.size() < new_base) out.resize(new_base, 0xE3);

    // ------------------------------------------------------------------------
    // NEW: Patch header/base fields consistently
    // ------------------------------------------------------------------------
    hdr.res_dir_mash_size = new_base;  // payload base
    P.pack_header.res_dir_mash_size = new_base;
    P.dir.base = (int32_t)new_base;

    // Payload
    for (size_t i = 0; i < items.size(); ++i) {
        size_t s = (size_t)new_base + P.res_locs[i].m_offset;
        size_t need = s + items[i].data.size();
        if (out.size() < need) out.resize(need, 0);
        if (!items[i].data.empty())
            memcpy(&out[s], items[i].data.data(), items[i].data.size());
    }

    // Patch header at file start (updated base)
    memcpy(out.data(), &hdr, sizeof(hdr));
    return out;
}

// ============================================================================
//  Application State
// ============================================================================

struct App {
    HWND     hWnd       = nullptr;
    HWND     hList      = nullptr;
    HWND     hStatus    = nullptr;
    HWND     hTab       = nullptr;
    HWND     hLogEdit   = nullptr;
    HWND     hInfoEdit  = nullptr;
    HFONT    hFontUI    = nullptr;
    HFONT    hFontMono  = nullptr;

    std::unique_ptr<ParsedPack> pack;
    bool pack_loaded = false;

    // Replacements (index-based build)
    std::unordered_map<int, std::vector<uint8_t>> replacements;
    int align_val = 16;

    // Filtered view indices
    std::vector<int> filtered;
    std::string filter_text;
    int type_filter = -1;

    // Sort
    int  sort_col = 0;
    bool sort_asc = true;

    // Log
    std::string log_text;

    void add_log(const char* prefix, const std::string& msg) {
        log_text += prefix;
        log_text += msg;
        log_text += "\r\n";
        if (hLogEdit) {
            SetWindowTextA(hLogEdit, log_text.c_str());
            SendMessageA(hLogEdit, EM_SETSEL, (WPARAM)log_text.size(), (LPARAM)log_text.size());
            SendMessageA(hLogEdit, EM_SCROLLCARET, 0, 0);
        }
    }

    void rebuild_filtered() {
        filtered.clear();
        if (!pack_loaded) return;
        std::string fl = filter_text;
        std::transform(fl.begin(), fl.end(), fl.begin(), ::tolower);

        for (auto& e : pack->entries) {
            if (type_filter >= 0 && (int)e.type != type_filter) continue;
            if (!fl.empty()) {
                std::string fn = e.filename;
                std::transform(fn.begin(), fn.end(), fn.begin(), ::tolower);
                std::string hx = format_hex(e.hash);
                std::transform(hx.begin(), hx.end(), hx.begin(), ::tolower);
                if (fn.find(fl) == std::string::npos && hx.find(fl) == std::string::npos)
                    continue;
            }
            filtered.push_back(e.index);
        }

        // Sort
        auto& entries = pack->entries;
        int sc = sort_col;
        bool asc = sort_asc;
        std::sort(filtered.begin(), filtered.end(), [&](int a, int b) {
            auto& ea = entries[a]; auto& eb = entries[b];
            int cmp = 0;
            switch (sc) {
                case 0: cmp = ea.index - eb.index; break;
                case 1: cmp = ea.filename.compare(eb.filename); break;
                case 2: cmp = ea.ext.compare(eb.ext); break;
                case 3: cmp = (ea.hash < eb.hash) ? -1 : (ea.hash > eb.hash ? 1 : 0); break;
                case 4: cmp = (ea.offset < eb.offset) ? -1 : (ea.offset > eb.offset ? 1 : 0); break;
                case 5: cmp = (ea.size < eb.size) ? -1 : (ea.size > eb.size ? 1 : 0); break;
                default: cmp = ea.index - eb.index;
            }
            return asc ? cmp < 0 : cmp > 0;
        });

        ListView_SetItemCountEx(hList, (int)filtered.size(), LVSICF_NOSCROLL);
    }

    void update_status() {
        if (!hStatus) return;
        if (pack_loaded) {
            char buf[512];
            snprintf(buf, sizeof(buf), " %s  |  %zu resources  |  Base: %s  |  Size: %s  |  Dict: %zu  |  Replacements: %zu",
                pack->source_path.c_str(),
                pack->entries.size(),
                format_hex(pack->base()).c_str(),
                format_size(pack->raw.size()).c_str(),
                g_hashDict.size(),
                replacements.size());
            SendMessageA(hStatus, SB_SETTEXTA, 0, (LPARAM)buf);
        } else {
            SendMessageA(hStatus, SB_SETTEXTA, 0, (LPARAM)" No file loaded. Use File > Open PCPACK or drag-and-drop.");
        }
    }

    void update_info() {
        if (!hInfoEdit || !pack_loaded) return;
        auto& P = *pack;
        std::string s;
        s += "=== RESOURCE PACK HEADER ===\r\n";
        s += "Versions: " + format_hex(P.pack_header.field_0.field_0) + ", " +
             format_hex(P.pack_header.field_0.field_4) + ", " +
             format_hex(P.pack_header.field_0.field_8) + ", " +
             format_hex(P.pack_header.field_0.field_C) + ", " +
             format_hex(P.pack_header.field_0.field_10) + "\r\n";
        s += "field_14:          " + format_hex(P.pack_header.field_14) + "\r\n";
        s += "directory_offset:  " + format_hex(P.pack_header.directory_offset) + "\r\n";
        s += "base (payload):    " + format_hex(P.pack_header.res_dir_mash_size) + "\r\n";
        s += "field_20:          " + format_hex(P.pack_header.field_20) + "\r\n";
        s += "field_24:          " + format_hex(P.pack_header.field_24) + "\r\n";
        s += "field_28:          " + format_hex(P.pack_header.field_28) + "\r\n";
        s += "\r\n=== MASH HEADER ===\r\n";
        s += "safety_key:  " + format_hex(P.mash_header.safety_key) + "\r\n";
        s += "field_4:     " + format_hex(P.mash_header.field_4) + "\r\n";
        s += "field_8:     " + format_hex(P.mash_header.field_8) + "\r\n";
        s += "class_id:    " + std::to_string(P.mash_header.class_id) + "\r\n";
        s += "\r\n=== DIRECTORY VECTOR COUNTS ===\r\n";
        s += "Parents:        " + std::to_string(P.dir.parents.m_size) + "\r\n";
        s += "Resources:      " + std::to_string(P.dir.resource_locations.m_size) + "\r\n";
        s += "Textures:       " + std::to_string(P.dir.texture_locations.m_size) + "\r\n";
        s += "Mesh Files:     " + std::to_string(P.dir.mesh_file_locations.m_size) + "\r\n";
        s += "Meshes:         " + std::to_string(P.dir.mesh_locations.m_size) + "\r\n";
        s += "Morph Files:    " + std::to_string(P.dir.morph_file_locations.m_size) + "\r\n";
        s += "Morphs:         " + std::to_string(P.dir.morph_locations.m_size) + "\r\n";
        s += "Material Files: " + std::to_string(P.dir.material_file_locations.m_size) + "\r\n";
        s += "Materials:      " + std::to_string(P.dir.material_locations.m_size) + "\r\n";
        s += "Anim Files:     " + std::to_string(P.dir.anim_file_locations.m_size) + "\r\n";
        s += "Anims:          " + std::to_string(P.dir.anim_locations.m_size) + "\r\n";
        s += "Scene Anims:    " + std::to_string(P.dir.scene_anim_locations.m_size) + "\r\n";
        s += "Skeletons:      " + std::to_string(P.dir.skeleton_locations.m_size) + "\r\n";
        s += "\r\n=== DIRECTORY META ===\r\n";
        s += "pack_slot:  " + std::to_string(P.dir.pack_slot) + "\r\n";
        s += "base:       " + format_hex((uint32_t)P.dir.base) + "\r\n";
        s += "field_80:   " + format_hex(P.dir.field_80) + "\r\n";
        s += "field_84:   " + format_hex(P.dir.field_84) + "\r\n";
        s += "field_88:   " + format_hex(P.dir.field_88) + "\r\n";
        SetWindowTextA(hInfoEdit, s.c_str());
    }
};

static App g_app;

// ============================================================================
//  Menu IDs
// ============================================================================

enum {
    IDM_FILE_OPEN = 1001,
    IDM_FILE_DICT,
    IDM_FILE_EXPORT_ALL,
    IDM_FILE_EXPORT_SEL,
    IDM_FILE_QUIT,
    IDM_IMPORT_FILES,
    IDM_IMPORT_FOLDER,
    IDM_IMPORT_BUILD,
    IDM_IMPORT_REIMPORT_BUILD, // NEW
    IDM_IMPORT_CLEAR,
    IDM_CTX_EXPORT,
    IDM_CTX_REPLACE,
    IDM_CTX_REMOVE_REPL,

    IDC_LISTVIEW = 2001,
    IDC_TAB,
    IDC_FILTER_EDIT,
    IDC_STATUS,
};

// ============================================================================
//  Win32 Dialog Helpers
// ============================================================================

static std::string open_file_dialog(HWND parent, const char* filter, const char* title) {
    char path[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? path : "";
}

static std::string save_file_dialog(HWND parent, const char* filter, const char* title, const char* defext) {
    char path[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? path : "";
}

static std::string browse_folder(HWND parent, const char* title) {
    char path[MAX_PATH] = {0};
    BROWSEINFOA bi = {};
    bi.hwndOwner = parent;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
    if (pidl && SHGetPathFromIDListA(pidl, path)) {
        CoTaskMemFree(pidl);
        return path;
    }
    if (pidl) CoTaskMemFree(pidl);
    return "";
}

// ============================================================================
//  Actions
// ============================================================================

static void action_open_pcpack(HWND hwnd) {
    std::string path = open_file_dialog(hwnd,
        "PCPACK Files\0*.pcpack;*.PCPACK\0All Files\0*.*\0",
        "Open PCPACK File");
    if (path.empty()) return;
    try {
        g_app.pack = std::make_unique<ParsedPack>(parse_pcpack(path));
        g_app.pack_loaded = true;
        g_app.replacements.clear();
        g_app.rebuild_filtered();
        g_app.update_status();
        g_app.update_info();
        g_app.add_log("[OK] ", "Loaded " + path + " (" + std::to_string(g_app.pack->entries.size()) + " resources)");
    } catch (const std::exception& e) {
        g_app.add_log("[ERR] ", std::string("Load failed: ") + e.what());
        MessageBoxA(hwnd, e.what(), "Error Loading PCPACK", MB_ICONERROR);
    }
}

static void action_load_dict(HWND hwnd) {
    std::string path = open_file_dialog(hwnd,
        "Dictionary Files\0*.txt\0All Files\0*.*\0",
        "Load Hash Dictionary");
    if (path.empty()) return;
    load_hash_dictionary(path);
    g_app.add_log("[OK] ", "Loaded " + std::to_string(g_hashDict.size()) + " hash entries from " + path);
    if (g_app.pack_loaded) {
        try {
            g_app.pack = std::make_unique<ParsedPack>(parse_pcpack(g_app.pack->source_path));
            g_app.rebuild_filtered();
            g_app.update_info();
            g_app.add_log("[OK] ", "Re-parsed with dictionary");
        } catch (...) {}
    }
    g_app.update_status();
}

static void action_export_all(HWND hwnd) {
    if (!g_app.pack_loaded) return;
    std::string dir = browse_folder(hwnd, "Select export folder");
    if (dir.empty()) return;
    try {
        do_export_all(*g_app.pack, dir);
        g_app.add_log("[OK] ", "Exported " + std::to_string(g_app.pack->entries.size()) + " resources to " + dir);
    } catch (const std::exception& e) {
        g_app.add_log("[ERR] ", std::string("Export failed: ") + e.what());
    }
}

static void action_export_selected(HWND hwnd) {
    if (!g_app.pack_loaded) return;
    int sel = ListView_GetNextItem(g_app.hList, -1, LVNI_SELECTED);
    if (sel < 0) return;

    std::vector<int> sels;
    while (sel >= 0) {
        if (sel < (int)g_app.filtered.size()) sels.push_back(g_app.filtered[sel]);
        sel = ListView_GetNextItem(g_app.hList, sel, LVNI_SELECTED);
    }

    if (sels.size() == 1) {
        auto& e = g_app.pack->entries[sels[0]];
        std::string path = save_file_dialog(hwnd, "All Files\0*.*\0", "Export Resource", "");
        if (!path.empty()) {
            try { do_export_single(*g_app.pack, sels[0], path); g_app.add_log("[OK] ", "Exported " + e.filename); }
            catch (const std::exception& ex) { g_app.add_log("[ERR] ", ex.what()); }
        }
    } else {
        std::string dir = browse_folder(hwnd, "Select export folder for selected resources");
        if (dir.empty()) return;
        fs::create_directories(dir);
        int ok = 0;
        for (int idx : sels) {
            try { do_export_single(*g_app.pack, idx, fs::path(dir) / g_app.pack->entries[idx].filename); ok++; }
            catch (...) {}
        }
        g_app.add_log("[OK] ", "Exported " + std::to_string(ok) + "/" + std::to_string(sels.size()) + " to " + dir);
    }
}

static void action_import_files(HWND hwnd) {
    if (!g_app.pack_loaded) return;
    std::string path = open_file_dialog(hwnd, "All Files\0*.*\0", "Select replacement file");
    if (path.empty()) return;
    fs::path fp(path);
    std::string fname = fp.filename().string();
    for (auto& e : g_app.pack->entries) {
        if (e.filename == fname) {
            g_app.replacements[e.index] = read_file(fp);
            g_app.add_log("[OK] ", "Queued [" + std::to_string(e.index) + "] " + fname +
                " (" + format_size(g_app.replacements[e.index].size()) + ")");
            g_app.update_status();
            ListView_RedrawItems(g_app.hList, 0, (int)g_app.filtered.size() - 1);
            return;
        }
    }
    g_app.add_log("[WARN] ", "No matching resource for: " + fname);
}

static void action_import_folder(HWND hwnd) {
    if (!g_app.pack_loaded) return;
    std::string dir = browse_folder(hwnd, "Select folder with replacement files");
    if (dir.empty()) return;
    int count = 0;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        for (auto& e : g_app.pack->entries) {
            if (e.filename == fname) {
                g_app.replacements[e.index] = read_file(entry.path());
                count++; break;
            }
        }
    }
    g_app.add_log("[OK] ", "Found " + std::to_string(count) + " matching files in " + dir);
    g_app.update_status();
    ListView_RedrawItems(g_app.hList, 0, (int)g_app.filtered.size() - 1);
}



static void action_build(HWND hwnd) {
    if (!g_app.pack_loaded || g_app.replacements.empty()) return;
    std::string path = save_file_dialog(hwnd,
        "PCPACK Files\0*.pcpack;*.PCPACK\0All Files\0*.*\0",
        "Save rebuilt PCPACK", "PCPACK");
    if (path.empty()) return;
    try {
        ParsedPack P = parse_pcpack(g_app.pack->source_path);
        auto result = do_import(P, g_app.replacements, (size_t)g_app.align_val);
        write_file(path, result);
        g_app.add_log("[OK] ", "Built " + path + " (" + format_size(result.size()) + ") with " +
            std::to_string(g_app.replacements.size()) + " replacement(s)");
        MessageBoxA(hwnd, ("Built successfully!\n" + path + "\n" + format_size(result.size())).c_str(),
            "Build Complete", MB_ICONINFORMATION);
    } catch (const std::exception& e) {
        g_app.add_log("[ERR] ", std::string("Build failed: ") + e.what());
        MessageBoxA(hwnd, e.what(), "Build Error", MB_ICONERROR);
    }
}

static void action_reimport_build(HWND hwnd) {
    if (!g_app.pack_loaded) return;

    std::string dir = browse_folder(hwnd, "Select folder to reimport (sync + add new + reorder)");
    if (dir.empty()) return;

    std::string outPath = save_file_dialog(hwnd,
        "PCPACK Files\0*.pcpack;*.PCPACK\0All Files\0*.*\0",
        "Save rebuilt PCPACK (Reimport)", "PCPACK");
    if (outPath.empty()) return;

    try {
        ParsedPack P = parse_pcpack(g_app.pack->source_path);

        std::string repLog;
        auto result = do_reimport_from_folder(P, dir, (size_t)g_app.align_val, &repLog);
        write_file(outPath, result);

        g_app.add_log("[OK] ", "Reimport build OK: " + outPath + " (" + format_size(result.size()) + ")");
        if (!repLog.empty()) g_app.add_log("[INFO] ", repLog);

        MessageBoxA(hwnd,
            ("Reimport build complete!\n\nOutput:\n" + outPath + "\n\nSize: " + format_size(result.size())).c_str(),
            "Reimport Build Complete", MB_ICONINFORMATION);
    } catch (const std::exception& e) {
        g_app.add_log("[ERR] ", std::string("Reimport build failed: ") + e.what());
        MessageBoxA(hwnd, e.what(), "Reimport Build Error", MB_ICONERROR);
    }
}

// ============================================================================
//  Drop handler
// ============================================================================

static void handle_drop(HWND hwnd, HDROP hDrop) {
    UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; i++) {
        char path[MAX_PATH];
        DragQueryFileA(hDrop, i, path, MAX_PATH);
        std::string sp(path);
        std::string upper = sp;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        if (upper.size() > 7 && upper.substr(upper.size() - 7) == ".PCPACK") {
            try {
                g_app.pack = std::make_unique<ParsedPack>(parse_pcpack(sp));
                g_app.pack_loaded = true;
                g_app.replacements.clear();
                g_app.rebuild_filtered();
                g_app.update_status();
                g_app.update_info();
                g_app.add_log("[OK] ", "Loaded " + sp);
            } catch (const std::exception& e) {
                g_app.add_log("[ERR] ", std::string("Load failed: ") + e.what());
            }
        }
        else if (upper.size() > 4 && upper.substr(upper.size() - 4) == ".TXT") {
            load_hash_dictionary(sp);
            g_app.add_log("[OK] ", "Loaded " + std::to_string(g_hashDict.size()) + " hash entries");
            if (g_app.pack_loaded) {
                try {
                    g_app.pack = std::make_unique<ParsedPack>(parse_pcpack(g_app.pack->source_path));
                    g_app.rebuild_filtered();
                    g_app.update_info();
                } catch (...) {}
            }
            g_app.update_status();
        }
        else if (g_app.pack_loaded) {
            fs::path fp(sp);
            std::string fname = fp.filename().string();
            for (auto& e : g_app.pack->entries) {
                if (e.filename == fname) {
                    g_app.replacements[e.index] = read_file(fp);
                    g_app.add_log("[OK] ", "Queued replacement [" + std::to_string(e.index) + "] " + fname);
                    g_app.update_status();
                    ListView_RedrawItems(g_app.hList, 0, (int)g_app.filtered.size() - 1);
                    break;
                }
            }
        }
    }
    DragFinish(hDrop);
}

// ============================================================================
//  Create controls
// ============================================================================

static HMENU create_menu() {
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    AppendMenuA(hFile, MF_STRING, IDM_FILE_OPEN,       "Open PCPACK...\tCtrl+O");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_DICT,       "Load Dictionary...\tCtrl+D");
    AppendMenuA(hFile, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(hFile, MF_STRING, IDM_FILE_EXPORT_ALL,  "Export All...");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_EXPORT_SEL,  "Export Selected...");
    AppendMenuA(hFile, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(hFile, MF_STRING, IDM_FILE_QUIT,        "Quit\tAlt+F4");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "File");

    HMENU hImport = CreatePopupMenu();
    AppendMenuA(hImport, MF_STRING, IDM_IMPORT_FILES,          "Add Replacement File...");
    AppendMenuA(hImport, MF_STRING, IDM_IMPORT_FOLDER,         "Import from Folder...");
    AppendMenuA(hImport, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(hImport, MF_STRING, IDM_IMPORT_BUILD,          "Build PCPACK...");
    AppendMenuA(hImport, MF_STRING, IDM_IMPORT_REIMPORT_BUILD, "Reimport (Folder Sync + Reorder) -> Build...");
    AppendMenuA(hImport, MF_STRING, IDM_IMPORT_CLEAR,          "Clear All Replacements");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hImport, "Import");

    return hMenu;
}

static void create_listview(HWND parent) {
    g_app.hList = CreateWindowExA(
        0, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
        0, 0, 100, 100, parent, (HMENU)IDC_LISTVIEW,
        GetModuleHandle(nullptr), nullptr);

    ListView_SetExtendedListViewStyle(g_app.hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);

    SendMessage(g_app.hList, WM_SETFONT, (WPARAM)g_app.hFontMono, TRUE);

    // Dark mode colors
    ListView_SetBkColor(g_app.hList, RGB(16, 17, 22));
    ListView_SetTextBkColor(g_app.hList, RGB(16, 17, 22));
    ListView_SetTextColor(g_app.hList, RGB(200, 204, 212));

    LVCOLUMNA col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    col.pszText = (LPSTR)"#";        col.cx = 50;  col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_app.hList, 0, &col);
    col.pszText = (LPSTR)"Filename"; col.cx = 280; col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_app.hList, 1, &col);
    col.pszText = (LPSTR)"Type";     col.cx = 90;  col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_app.hList, 2, &col);
    col.pszText = (LPSTR)"Hash";     col.cx = 100; col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_app.hList, 3, &col);
    col.pszText = (LPSTR)"Offset";   col.cx = 100; col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_app.hList, 4, &col);
    col.pszText = (LPSTR)"Size";     col.cx = 90;  col.fmt = LVCFMT_RIGHT; ListView_InsertColumn(g_app.hList, 5, &col);
}

static void create_tab(HWND parent) {
    g_app.hTab = CreateWindowExA(
        0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, 100, parent, (HMENU)IDC_TAB,
        GetModuleHandle(nullptr), nullptr);
    SendMessage(g_app.hTab, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);

    TCITEMA ti = {};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPSTR)"Resources";   TabCtrl_InsertItem(g_app.hTab, 0, &ti);
    ti.pszText = (LPSTR)"Header Info"; TabCtrl_InsertItem(g_app.hTab, 1, &ti);
    ti.pszText = (LPSTR)"Log";         TabCtrl_InsertItem(g_app.hTab, 2, &ti);
}

static void create_children(HWND parent) {
    g_app.hFontUI = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, "Segoe UI");
    g_app.hFontMono = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH, "Consolas");

    g_app.hStatus = CreateWindowExA(0, STATUSCLASSNAMEA, "",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, parent, (HMENU)IDC_STATUS, GetModuleHandle(nullptr), nullptr);
    SendMessage(g_app.hStatus, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);

    create_tab(parent);

    HWND hFilter = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 300, 24, parent, (HMENU)IDC_FILTER_EDIT,
        GetModuleHandle(nullptr), nullptr);
    SendMessage(hFilter, WM_SETFONT, (WPARAM)g_app.hFontUI, TRUE);
    SendMessageW(hFilter, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search filename or hash...");

    create_listview(parent);

    g_app.hLogEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, parent, nullptr,
        GetModuleHandle(nullptr), nullptr);
    SendMessage(g_app.hLogEdit, WM_SETFONT, (WPARAM)g_app.hFontMono, TRUE);

    g_app.hInfoEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, parent, nullptr,
        GetModuleHandle(nullptr), nullptr);
    SendMessage(g_app.hInfoEdit, WM_SETFONT, (WPARAM)g_app.hFontMono, TRUE);

    g_app.update_status();
    g_app.add_log("[INFO] ", "PCPACK Tool ready. Drop a .PCPACK file or use File > Open.");
}

// ============================================================================
//  Layout
// ============================================================================

static void do_layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    SendMessage(g_app.hStatus, WM_SIZE, 0, 0);
    RECT sr; GetWindowRect(g_app.hStatus, &sr);
    int sh = sr.bottom - sr.top;

    int top = 0;
    int bottom = rc.bottom - sh;

    int tabH = 28;
    MoveWindow(g_app.hTab, 0, top, rc.right, tabH, TRUE);
    top += tabH;

    int filterH = 26;
    HWND hFilter = GetDlgItem(hwnd, IDC_FILTER_EDIT);
    MoveWindow(hFilter, 4, top + 2, 350, filterH - 4, TRUE);
    top += filterH;

    int tabSel = TabCtrl_GetCurSel(g_app.hTab);

    ShowWindow(g_app.hList,     tabSel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(hFilter,         tabSel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.hInfoEdit, tabSel == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.hLogEdit,  tabSel == 2 ? SW_SHOW : SW_HIDE);

    int contentH = bottom - top;

    MoveWindow(g_app.hList,     0, top, rc.right, contentH, TRUE);
    MoveWindow(g_app.hInfoEdit, 4, top, rc.right - 8, contentH - 4, TRUE);
    MoveWindow(g_app.hLogEdit,  4, top, rc.right - 8, contentH - 4, TRUE);
}

// ============================================================================
//  ListView virtual mode callbacks
// ============================================================================

static void on_lv_getdispinfo(NMLVDISPINFOA* di) {
    int row = di->item.iItem;
    if (row < 0 || row >= (int)g_app.filtered.size()) return;

    int idx = g_app.filtered[row];
    if (idx < 0 || !g_app.pack_loaded || idx >= (int)g_app.pack->entries.size()) return;
    auto& e = g_app.pack->entries[idx];

    static char buf[512];
    if (di->item.mask & LVIF_TEXT) {
        switch (di->item.iSubItem) {
            case 0: snprintf(buf, sizeof(buf), "%d", e.index); break;
            case 1:
                if (g_app.replacements.count(idx))
                    snprintf(buf, sizeof(buf), "%s  [%s]", e.filename.c_str(),
                        format_size(g_app.replacements[idx].size()).c_str());
                else
                    snprintf(buf, sizeof(buf), "%s", e.filename.c_str());
                break;
            case 2: snprintf(buf, sizeof(buf), "%s", e.ext.c_str()); break;
            case 3: snprintf(buf, sizeof(buf), "%s", format_hex(e.hash).c_str()); break;
            case 4: snprintf(buf, sizeof(buf), "%s", format_hex(e.offset).c_str()); break;
            case 5: snprintf(buf, sizeof(buf), "%s", format_size(e.size).c_str()); break;
            default: buf[0] = 0;
        }
        di->item.pszText = buf;
    }
}

static void on_lv_customdraw(NMLVCUSTOMDRAW* cd, LRESULT& result) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        result = CDRF_NOTIFYITEMDRAW;
        return;
    case CDDS_ITEMPREPAINT:
        result = CDRF_NOTIFYSUBITEMDRAW;
        return;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        int row = (int)cd->nmcd.dwItemSpec;
        if (row >= 0 && row < (int)g_app.filtered.size()) {
            int idx = g_app.filtered[row];
            auto& e = g_app.pack->entries[idx];

            if (g_app.replacements.count(idx)) {
                cd->clrTextBk = RGB(40, 32, 16);
            }

            if (cd->iSubItem == 2) {
                cd->clrText = get_type_color(e.ext.c_str());
            } else if (cd->iSubItem == 1 && g_app.replacements.count(idx)) {
                cd->clrText = RGB(232, 164, 74);
            } else {
                cd->clrText = RGB(200, 204, 212);
            }
        }
        result = CDRF_NEWFONT;
        return;
    }
    default:
        result = CDRF_DODEFAULT;
    }
}

static void on_lv_columnclick(NMLISTVIEW* nm) {
    if (nm->iSubItem == g_app.sort_col) g_app.sort_asc = !g_app.sort_asc;
    else { g_app.sort_col = nm->iSubItem; g_app.sort_asc = true; }
    g_app.rebuild_filtered();
}

static void on_lv_rclick(HWND hwnd) {
    int sel = ListView_GetNextItem(g_app.hList, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)g_app.filtered.size()) return;
    int idx = g_app.filtered[sel];

    HMENU hPop = CreatePopupMenu();
    AppendMenuA(hPop, MF_STRING, IDM_CTX_EXPORT,  "Export...");
    AppendMenuA(hPop, MF_STRING, IDM_CTX_REPLACE, "Replace with file...");
    if (g_app.replacements.count(idx))
        AppendMenuA(hPop, MF_STRING, IDM_CTX_REMOVE_REPL, "Remove replacement");
    AppendMenuA(hPop, MF_SEPARATOR, 0, nullptr);

    auto& e = g_app.pack->entries[idx];
    char info[256];
    snprintf(info, sizeof(info), "Hash: %s  |  Offset: %s  |  Size: %s",
        format_hex(e.hash).c_str(), format_hex(e.offset).c_str(), format_size(e.size).c_str());
    AppendMenuA(hPop, MF_STRING | MF_GRAYED, 0, info);

    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hPop, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hPop);

    if (cmd == IDM_CTX_EXPORT) {
        std::string path = save_file_dialog(hwnd, "All Files\0*.*\0", "Export Resource", "");
        if (!path.empty()) {
            try { do_export_single(*g_app.pack, idx, path); g_app.add_log("[OK] ", "Exported " + e.filename); }
            catch (const std::exception& ex) { g_app.add_log("[ERR] ", ex.what()); }
        }
    } else if (cmd == IDM_CTX_REPLACE) {
        std::string path = open_file_dialog(hwnd, "All Files\0*.*\0", "Select replacement");
        if (!path.empty()) {
            g_app.replacements[idx] = read_file(path);
            g_app.add_log("[OK] ", "Queued replacement [" + std::to_string(idx) + "] (" +
                format_size(g_app.replacements[idx].size()) + ")");
            g_app.update_status();
            ListView_RedrawItems(g_app.hList, 0, (int)g_app.filtered.size() - 1);
        }
    } else if (cmd == IDM_CTX_REMOVE_REPL) {
        g_app.replacements.erase(idx);
        g_app.add_log("[INFO] ", "Removed replacement for [" + std::to_string(idx) + "]");
        g_app.update_status();
        ListView_RedrawItems(g_app.hList, 0, (int)g_app.filtered.size() - 1);
    }
}

// ============================================================================
//  Window Procedure
// ============================================================================

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_app.hWnd = hwnd;
        create_children(hwnd);
        do_layout(hwnd);
        return 0;

    case WM_SIZE:
        do_layout(hwnd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_FILTER_EDIT && code == EN_CHANGE) {
            char buf[256] = {};
            GetDlgItemTextA(hwnd, IDC_FILTER_EDIT, buf, sizeof(buf));
            g_app.filter_text = buf;
            g_app.rebuild_filtered();
            return 0;
        }

        switch (id) {
            case IDM_FILE_OPEN:       action_open_pcpack(hwnd); break;
            case IDM_FILE_DICT:       action_load_dict(hwnd); break;
            case IDM_FILE_EXPORT_ALL: action_export_all(hwnd); break;
            case IDM_FILE_EXPORT_SEL: action_export_selected(hwnd); break;
            case IDM_FILE_QUIT:       PostQuitMessage(0); break;

            case IDM_IMPORT_FILES:    action_import_files(hwnd); break;
            case IDM_IMPORT_FOLDER:   action_import_folder(hwnd); break;
            case IDM_IMPORT_BUILD:    action_build(hwnd); break;
            case IDM_IMPORT_REIMPORT_BUILD: action_reimport_build(hwnd); break;

            case IDM_IMPORT_CLEAR:
                g_app.replacements.clear();
                g_app.add_log("[INFO] ", "Cleared all replacements");
                g_app.update_status();
                ListView_RedrawItems(g_app.hList, 0, (int)g_app.filtered.size() - 1);
                break;
        }
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->hwndFrom == g_app.hList) {
            if (nm->code == LVN_GETDISPINFOA) {
                on_lv_getdispinfo((NMLVDISPINFOA*)lParam);
                return 0;
            }
            if (nm->code == NM_CUSTOMDRAW) {
                LRESULT result = 0;
                on_lv_customdraw((NMLVCUSTOMDRAW*)lParam, result);
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, result);
                return result;
            }
            if (nm->code == LVN_COLUMNCLICK) {
                on_lv_columnclick((NMLISTVIEW*)lParam);
                return 0;
            }
            if (nm->code == NM_RCLICK) {
                on_lv_rclick(hwnd);
                return 0;
            }
            if (nm->code == NM_DBLCLK) {
                int sel = ListView_GetNextItem(g_app.hList, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)g_app.filtered.size()) {
                    int idx = g_app.filtered[sel];
                    auto& e = g_app.pack->entries[idx];
                    std::string path = save_file_dialog(hwnd, "All Files\0*.*\0", "Export Resource", "");
                    if (!path.empty()) {
                        try { do_export_single(*g_app.pack, idx, path); g_app.add_log("[OK] ", "Exported " + e.filename); }
                        catch (const std::exception& ex) { g_app.add_log("[ERR] ", ex.what()); }
                    }
                }
                return 0;
            }
        }
        if (nm->hwndFrom == g_app.hTab && nm->code == TCN_SELCHANGE) {
            do_layout(hwnd);
            return 0;
        }
        break;
    }

    case WM_DROPFILES:
        handle_drop(hwnd, (HDROP)wParam);
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(200, 204, 212));
        SetBkColor(hdc, RGB(16, 17, 22));
        static HBRUSH hBrush = CreateSolidBrush(RGB(16, 17, 22));
        return (LRESULT)hBrush;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(200, 204, 212));
        SetBkColor(hdc, RGB(16, 17, 22));
        static HBRUSH hBrush = CreateSolidBrush(RGB(16, 17, 22));
        return (LRESULT)hBrush;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(RGB(16, 17, 22));
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return 1;
    }

    case WM_DESTROY:
        if (g_app.hFontUI) DeleteObject(g_app.hFontUI);
        if (g_app.hFontMono) DeleteObject(g_app.hFontMono);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============================================================================
//  WinMain
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow);

int main(int argc, char** argv) {
    std::string cmdLine;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) cmdLine += ' ';
        std::string arg = argv[i];
        if (arg.find(' ') != std::string::npos)
            cmdLine += "\"" + arg + "\"";
        else
            cmdLine += arg;
    }
    return WinMain(GetModuleHandle(nullptr), nullptr, (LPSTR)cmdLine.c_str(), SW_SHOWDEFAULT);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    OleInitialize(nullptr);

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "PCPackToolWin32";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_ACCEPTFILES,
        "PCPackToolWin32", "PCPACK Tool - Ultimate Spider-Man (2005) PC",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
        nullptr, create_menu(), hInstance, nullptr);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (lpCmdLine && lpCmdLine[0]) {
        std::string arg = lpCmdLine;
        if (arg.front() == '"' && arg.back() == '"') arg = arg.substr(1, arg.size() - 2);
        std::string upper = arg;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper.find(".PCPACK") != std::string::npos) {
            try {
                g_app.pack = std::make_unique<ParsedPack>(parse_pcpack(arg));
                g_app.pack_loaded = true;
                g_app.rebuild_filtered();
                g_app.update_status();
                g_app.update_info();
                g_app.add_log("[OK] ", "Loaded " + arg);
            } catch (const std::exception& e) {
                g_app.add_log("[ERR] ", std::string("Load failed: ") + e.what());
            }
        }
    }

    ACCEL accel[] = {
        { FCONTROL | FVIRTKEY, 'O', IDM_FILE_OPEN },
        { FCONTROL | FVIRTKEY, 'D', IDM_FILE_DICT },
    };
    HACCEL hAccel = CreateAcceleratorTableA(accel, 2);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorA(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    DestroyAcceleratorTable(hAccel);
    OleUninitialize();
    return (int)msg.wParam;
}
