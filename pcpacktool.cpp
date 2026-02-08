// pcpack_tool.cpp - Ultimate Spider-Man PCPACK full export/reimport with offset fixup
// Handles the 0x1020 byte header structure and updates ALL location offsets
//
// Build (MinGW/Linux):
//   g++ -std=c++17 -O2 pcpack_tool.cpp -o pcpack_tool
// Build (MSVC):
//   cl /std:c++17 /O2 pcpack_tool.cpp
//
// Usage:
//   pcpack_tool export <input.pcpack> [output_dir] [dict.txt]
//   pcpack_tool import <original.pcpack> <input_dir> <output.pcpack> [--align N]

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

namespace fs = std::filesystem;

// ==================== Structures ====================

#pragma pack(push, 1)

struct resource_versions {
    uint32_t field_0, field_4, field_8, field_C, field_10;
};
static_assert(sizeof(resource_versions) == 0x14, "");

struct resource_pack_header {
    resource_versions field_0;      // 0x00-0x13
    uint32_t          field_14;     // 0x14
    uint32_t          directory_offset;  // 0x18 - typically 0x30
    uint32_t          res_dir_mash_size; // 0x1C - base offset where payloads start (0x1020)
    uint32_t          field_20;     // 0x20
    uint32_t          field_24;     // 0x24
    uint32_t          field_28;     // 0x28
};
static_assert(sizeof(resource_pack_header) == 0x2C, "");

struct generic_mash_header {
    int32_t safety_key;   // 0x00
    int32_t field_4;      // 0x04
    int32_t field_8;      // 0x08 - total size of mash data
    int16_t class_id;     // 0x0C
    int16_t field_E;      // 0x0E
};
static_assert(sizeof(generic_mash_header) == 0x10, "");

struct string_hash {
    uint32_t source_hash_code;
};

struct resource_key {
    string_hash m_hash;
    uint32_t    m_type;
};

struct resource_location {
    resource_key field_0;
    uint32_t     m_offset;  // relative to base (res_dir_mash_size)
    uint32_t     m_size;
};
static_assert(sizeof(resource_location) == 0x10, "");

template<typename T>
struct mashable_vector_t {
    uint32_t m_data;    // on-disk pointer placeholder
    uint16_t m_size;    // element count
    uint8_t  m_shared;
    uint8_t  field_7;
};
static_assert(sizeof(mashable_vector_t<uint32_t>) == 8, "");

struct tlresource_location {
    string_hash name;
    uint8_t     type;
    uint8_t     pad[3];
    uint32_t    offset;  // relative to base
};
static_assert(sizeof(tlresource_location) == 0x0C, "");

struct resource_directory {
    mashable_vector_t<int32_t>             parents;               // 0x00
    mashable_vector_t<resource_location>   resource_locations;    // 0x08
    mashable_vector_t<tlresource_location> texture_locations;     // 0x10
    mashable_vector_t<tlresource_location> mesh_file_locations;   // 0x18
    mashable_vector_t<tlresource_location> mesh_locations;        // 0x20
    mashable_vector_t<tlresource_location> morph_file_locations;  // 0x28
    mashable_vector_t<tlresource_location> morph_locations;       // 0x30
    mashable_vector_t<tlresource_location> material_file_locations; // 0x38
    mashable_vector_t<tlresource_location> material_locations;    // 0x40
    mashable_vector_t<tlresource_location> anim_file_locations;   // 0x48
    mashable_vector_t<tlresource_location> anim_locations;        // 0x50
    mashable_vector_t<tlresource_location> scene_anim_locations;  // 0x58
    mashable_vector_t<tlresource_location> skeleton_locations;    // 0x60
    mashable_vector_t<int32_t>             field_68;              // 0x68
    mashable_vector_t<int32_t>             field_70;              // 0x70
    int32_t pack_slot;     // 0x78
    int32_t base;          // 0x7C - same as res_dir_mash_size
    int32_t field_80;      // 0x80
    int32_t field_84;      // 0x84
    int32_t field_88;      // 0x88
    int32_t type_start_idxs[70]; // 0x8C
    int32_t type_end_idxs[70];   // 0x1A4
};
static_assert(sizeof(resource_directory) == 0x2BC, "");

#pragma pack(pop)

// ==================== Type Extension Table ====================

static const std::vector<std::string> resource_type_ext = {
    ".NONE",     // 0
    ".PCANIM",   // 1
    ".PCSKEL",   // 2
    ".ALS",      // 3
    ".ENT",      // 4
    ".ENTEXT",   // 5
    ".DDS",      // 6
    ".DDSMP",    // 7
    ".IFL",      // 8
    ".DESC",     // 9
    ".ENS",      // 10
    ".SPL",      // 11
    ".AB",       // 12
    ".QP",       // 13
    ".TRIG",     // 14
    ".PCSX",     // 15
    ".INST",     // 16
    ".FDF",      // 17
    ".PANEL",    // 18
    ".TXT",      // 19
    ".ICN",      // 20
    ".PCMESH",   // 21
    ".PCMORPH",  // 22
    ".PCMAT",    // 23
    ".COLL",     // 24
    ".PCPACK",   // 25
    ".PCSANIM",  // 26
    ".MSN",      // 27
    ".MARKER",   // 28
    ".HH",       // 29
    ".WAV",      // 30
    ".WBK",      // 31
    ".M2V",      // 32
    "M2V",       // 33
    ".PFX",      // 34
    ".CSV",      // 35
    ".CLE",      // 36
    ".LIT",      // 37
    ".GRD",      // 38
    ".GLS",      // 39
    ".LOD",      // 40
    ".SIN",      // 41
    ".GV",       // 42
    ".SV",       // 43
    ".TOKENS",   // 44
    ".DSG",      // 45
    ".PATH",     // 46
    ".PTRL",     // 47
    ".LANG",     // 48
    ".SLF",      // 49
    ".VISEME",   // 50
    ".PCMESHDEF",// 51
    ".PCMORPHDEF",// 52
    ".PCMATDEF", // 53
    ".MUT",      // 54
    ".ASG",      // 55
    ".BAI",      // 56
    ".CUT",      // 57
    ".INTERACT", // 58
    ".CSV",      // 59
    ".CSV",      // 60
    "._ENTID_",  // 61
    "._ANIMID_", // 62
    "._REGIONID_",// 63
    "._AI_GENERIC_ID_",// 64
    "._RADIOMSG_",// 65
    "._GOAL_",   // 66
    "._IFC_ATTRIBUTE_",// 67
    "._SIGNAL_", // 68
    "._PACKGROUP_"// 69
};

// ==================== Helpers ====================

static std::unordered_map<uint32_t, std::string> g_hashDict;

static void load_hash_dictionary(const fs::path& path) {
    if (path.empty() || !fs::exists(path)) return;
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string hex_str, name;
        if (!(iss >> hex_str >> name)) continue;
        if (hex_str.size() >= 2 && (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X'))) {
            uint32_t v = (uint32_t)std::stoul(hex_str, nullptr, 16);
            g_hashDict[v] = name;
        }
    }
    printf("Loaded %zu hash entries from dictionary\n", g_hashDict.size());
}

static std::string get_ext(uint32_t type) {
    return (type < resource_type_ext.size()) ? resource_type_ext[type] : ".UNK";
}

static std::string get_filename(uint32_t hash, uint32_t type) {
    auto it = g_hashDict.find(hash);
    char hex[16];
    snprintf(hex, sizeof(hex), "0x%08X", hash);
    std::string base = (it != g_hashDict.end()) ? it->second : std::string(hex);
    return base + get_ext(type);
}

static std::string sanitize_filename(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c < 32 || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '/' || c == '\\')
            c = '_';
    }
    return result;
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

// ==================== Parsed PCPACK ====================

struct ParsedPack {
    std::vector<uint8_t> raw;
    
    resource_pack_header pack_header;
    generic_mash_header  mash_header;
    resource_directory   dir;
    
    std::vector<int32_t>             parents;
    std::vector<resource_location>   res_locs;
    std::vector<tlresource_location> textures;
    std::vector<tlresource_location> mesh_files;
    std::vector<tlresource_location> meshes;
    std::vector<tlresource_location> morph_files;
    std::vector<tlresource_location> morphs;
    std::vector<tlresource_location> material_files;
    std::vector<tlresource_location> materials;
    std::vector<tlresource_location> anim_files;
    std::vector<tlresource_location> anims;
    std::vector<tlresource_location> scene_anims;
    std::vector<tlresource_location> skeletons;
    
    uint32_t base() const { return pack_header.res_dir_mash_size; }
};

static ParsedPack parse_pcpack(const fs::path& path) {
    ParsedPack P;
    P.raw = read_file(path);
    
    if (P.raw.size() < sizeof(resource_pack_header))
        throw std::runtime_error("File too small for header");
    
    memcpy(&P.pack_header, P.raw.data(), sizeof(P.pack_header));
    
    uint32_t dir_off = P.pack_header.directory_offset;
    if (dir_off + sizeof(generic_mash_header) + sizeof(resource_directory) > P.raw.size())
        throw std::runtime_error("Invalid directory offset");
    
    memcpy(&P.mash_header, &P.raw[dir_off], sizeof(P.mash_header));
    memcpy(&P.dir, &P.raw[dir_off + sizeof(generic_mash_header)], sizeof(P.dir));
    
    // Parse vector data after directory
    size_t pos = dir_off + sizeof(generic_mash_header) + sizeof(resource_directory);
    
    auto read_align = [&]() {
        pos = align_up(pos, 8);
        pos = align_up(pos, 4);
    };
    
    auto read_i32_vec = [&](uint16_t count) -> std::vector<int32_t> {
        read_align();
        std::vector<int32_t> v(count);
        if (count > 0) {
            memcpy(v.data(), &P.raw[pos], count * sizeof(int32_t));
            pos += count * sizeof(int32_t);
        }
        pos = align_up(pos, 4);
        return v;
    };
    
    auto read_res_locs = [&](uint16_t count) -> std::vector<resource_location> {
        read_align();
        std::vector<resource_location> v(count);
        if (count > 0) {
            memcpy(v.data(), &P.raw[pos], count * sizeof(resource_location));
            pos += count * sizeof(resource_location);
        }
        pos = align_up(pos, 4);
        return v;
    };
    
    auto read_tl_locs = [&](uint16_t count) -> std::vector<tlresource_location> {
        read_align();
        std::vector<tlresource_location> v(count);
        if (count > 0) {
            memcpy(v.data(), &P.raw[pos], count * sizeof(tlresource_location));
            pos += count * sizeof(tlresource_location);
        }
        pos = align_up(pos, 4);
        return v;
    };
    
    P.parents = read_i32_vec(P.dir.parents.m_size);
    P.res_locs = read_res_locs(P.dir.resource_locations.m_size);
    P.textures = read_tl_locs(P.dir.texture_locations.m_size);
    P.mesh_files = read_tl_locs(P.dir.mesh_file_locations.m_size);
    P.meshes = read_tl_locs(P.dir.mesh_locations.m_size);
    P.morph_files = read_tl_locs(P.dir.morph_file_locations.m_size);
    P.morphs = read_tl_locs(P.dir.morph_locations.m_size);
    P.material_files = read_tl_locs(P.dir.material_file_locations.m_size);
    P.materials = read_tl_locs(P.dir.material_locations.m_size);
    P.anim_files = read_tl_locs(P.dir.anim_file_locations.m_size);
    P.anims = read_tl_locs(P.dir.anim_locations.m_size);
    P.scene_anims = read_tl_locs(P.dir.scene_anim_locations.m_size);
    P.skeletons = read_tl_locs(P.dir.skeleton_locations.m_size);
    
    return P;
}

// ==================== Export ====================

static void do_export(const fs::path& pack_path, const fs::path& out_dir, const fs::path& dict_path) {
    load_hash_dictionary(dict_path);
    
    printf("Parsing %s...\n", pack_path.string().c_str());
    ParsedPack P = parse_pcpack(pack_path);
    
    printf("PCPACK Info:\n");
    printf("  Directory offset: 0x%X\n", P.pack_header.directory_offset);
    printf("  Base (payload start): 0x%X (%u)\n", P.base(), P.base());
    printf("  Resource locations: %zu\n", P.res_locs.size());
    printf("  Texture locations: %zu\n", P.textures.size());
    printf("  Mesh file locations: %zu\n", P.mesh_files.size());
    printf("  Mesh locations: %zu\n", P.meshes.size());
    printf("  Material locations: %zu\n", P.materials.size());
    printf("  Anim file locations: %zu\n", P.anim_files.size());
    printf("  Anim locations: %zu\n", P.anims.size());
    printf("  Skeleton locations: %zu\n", P.skeletons.size());
    
    fs::path target_dir = out_dir.empty() ? pack_path.stem() : out_dir;
    fs::create_directories(target_dir);
    
    // Export manifest file for reimport
    fs::path manifest_path = target_dir / "_manifest.txt";
    std::ofstream manifest(manifest_path);
    manifest << "# PCPACK Manifest\n";
    manifest << "# base=" << P.base() << "\n";
    manifest << "# resources=" << P.res_locs.size() << "\n\n";
    
    printf("\nExporting %zu resources to %s\n", P.res_locs.size(), target_dir.string().c_str());
    
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        const auto& rl = P.res_locs[i];
        uint32_t hash = rl.field_0.m_hash.source_hash_code;
        uint32_t type = rl.field_0.m_type;
        uint64_t start = (uint64_t)P.base() + rl.m_offset;
        uint64_t end = start + rl.m_size;
        
        if (end > P.raw.size()) {
            printf("  [%zu] WARNING: payload out of bounds (0x%llX > 0x%zX)\n",
                   i, (unsigned long long)end, P.raw.size());
            continue;
        }
        
        std::string fname = sanitize_filename(get_filename(hash, type));
        fs::path out_path = target_dir / fname;
        
        std::ofstream of(out_path, std::ios::binary);
        if (!of) {
            printf("  [%zu] ERROR: cannot write %s\n", i, fname.c_str());
            continue;
        }
        of.write((const char*)&P.raw[start], rl.m_size);
        of.close();
        
        // Write to manifest: index hash type offset size filename
        manifest << i << " 0x" << std::hex << hash << " " << std::dec << type
                 << " 0x" << std::hex << rl.m_offset << " 0x" << rl.m_size
                 << " " << fname << "\n";
        
        printf("  [%zu] %s (0x%X bytes at offset 0x%X)\n",
               i, fname.c_str(), rl.m_size, rl.m_offset);
    }
    
    manifest.close();
    printf("\nExport complete. Manifest written to %s\n", manifest_path.string().c_str());
}

// ==================== Import ====================

static void do_import(const fs::path& orig_pack, const fs::path& input_dir,
                      const fs::path& out_pack, size_t align_val) {
    printf("Parsing original pack %s...\n", orig_pack.string().c_str());
    ParsedPack P = parse_pcpack(orig_pack);
    
    printf("Original PCPACK base: 0x%X\n", P.base());
    printf("Processing %zu resources...\n", P.res_locs.size());
    
    // Build old offset -> resource index mapping for tlresource_location updates
    std::unordered_map<uint32_t, size_t> old_offset_to_idx;
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        old_offset_to_idx[P.res_locs[i].m_offset] = i;
    }
    
    // Calculate new offsets and sizes
    struct NewResource {
        uint32_t new_offset;
        uint32_t new_size;
        std::vector<uint8_t> data;
        bool from_file;
    };
    std::vector<NewResource> new_resources(P.res_locs.size());
    
    uint32_t cursor = 0;  // offset relative to base
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        const auto& rl = P.res_locs[i];
        uint32_t hash = rl.field_0.m_hash.source_hash_code;
        uint32_t type = rl.field_0.m_type;
        
        std::string fname = sanitize_filename(get_filename(hash, type));
        fs::path in_file = input_dir / fname;
        
        if (fs::exists(in_file)) {
            new_resources[i].data = read_file(in_file);
            new_resources[i].new_size = (uint32_t)new_resources[i].data.size();
            new_resources[i].from_file = true;
            printf("  [%zu] %s: from file (%u bytes)\n", i, fname.c_str(), new_resources[i].new_size);
        } else {
            // Keep original data
            uint64_t start = (uint64_t)P.base() + rl.m_offset;
            new_resources[i].data.resize(rl.m_size);
            memcpy(new_resources[i].data.data(), &P.raw[start], rl.m_size);
            new_resources[i].new_size = rl.m_size;
            new_resources[i].from_file = false;
            printf("  [%zu] %s: kept original (%u bytes)\n", i, fname.c_str(), rl.m_size);
        }
        
        cursor = (uint32_t)align_up(cursor, align_val);
        new_resources[i].new_offset = cursor;
        cursor += new_resources[i].new_size;
    }
    
    // Build offset delta map: old_offset -> new_offset
    std::unordered_map<uint32_t, uint32_t> offset_map;
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        offset_map[P.res_locs[i].m_offset] = new_resources[i].new_offset;
    }
    
    // For tlresource_locations, find which resource they belong to and compute delta
    auto update_tl_offset = [&](uint32_t old_off) -> uint32_t {
        // Find which resource this offset falls within
        for (size_t i = 0; i < P.res_locs.size(); ++i) {
            uint32_t res_start = P.res_locs[i].m_offset;
            uint32_t res_end = res_start + P.res_locs[i].m_size;
            if (old_off >= res_start && old_off < res_end) {
                // This tl belongs to resource i
                uint32_t internal_off = old_off - res_start;
                return new_resources[i].new_offset + internal_off;
            }
        }
        // No match found - keep original (might be 0 or special value)
        return old_off;
    };
    
    // Update all tlresource_location offsets
    auto update_tl_vec = [&](std::vector<tlresource_location>& vec, const char* name) {
        for (auto& tl : vec) {
            uint32_t old = tl.offset;
            tl.offset = update_tl_offset(old);
            if (old != tl.offset && vec.size() < 20) {
                printf("    %s: 0x%X -> 0x%X\n", name, old, tl.offset);
            }
        }
    };
    
    printf("\nUpdating tlresource_location offsets...\n");
    update_tl_vec(P.textures, "texture");
    update_tl_vec(P.mesh_files, "mesh_file");
    update_tl_vec(P.meshes, "mesh");
    update_tl_vec(P.morph_files, "morph_file");
    update_tl_vec(P.morphs, "morph");
    update_tl_vec(P.material_files, "material_file");
    update_tl_vec(P.materials, "material");
    update_tl_vec(P.anim_files, "anim_file");
    update_tl_vec(P.anims, "anim");
    update_tl_vec(P.scene_anims, "scene_anim");
    update_tl_vec(P.skeletons, "skeleton");
    
    // Update resource_locations
    for (size_t i = 0; i < P.res_locs.size(); ++i) {
        P.res_locs[i].m_offset = new_resources[i].new_offset;
        P.res_locs[i].m_size = new_resources[i].new_size;
    }
    
    // Now rebuild the entire file
    printf("\nRebuilding PCPACK...\n");
    
    std::vector<uint8_t> out;
    
    // Write pack header (unchanged except we'll verify base stays same)
    out.resize(sizeof(resource_pack_header));
    memcpy(out.data(), &P.pack_header, sizeof(P.pack_header));
    
    // Pad to directory offset
    if (out.size() < P.pack_header.directory_offset)
        out.resize(P.pack_header.directory_offset, 0);
    
    // Write mash header
    out.insert(out.end(), (uint8_t*)&P.mash_header, (uint8_t*)&P.mash_header + sizeof(P.mash_header));
    
    // Write directory
    out.insert(out.end(), (uint8_t*)&P.dir, (uint8_t*)&P.dir + sizeof(P.dir));
    
    // Helper to write vectors with alignment
    auto emit_align = [&](size_t a, uint8_t fill = 0xE3) {
        size_t want = align_up(out.size(), a);
        if (want > out.size()) out.insert(out.end(), want - out.size(), fill);
    };
    
    auto emit_i32_vec = [&](const std::vector<int32_t>& v) {
        emit_align(8); emit_align(4);
        if (!v.empty()) {
            const uint8_t* p = (const uint8_t*)v.data();
            out.insert(out.end(), p, p + v.size() * sizeof(int32_t));
        }
        emit_align(4);
    };
    
    auto emit_res_vec = [&](const std::vector<resource_location>& v) {
        emit_align(8); emit_align(4);
        if (!v.empty()) {
            const uint8_t* p = (const uint8_t*)v.data();
            out.insert(out.end(), p, p + v.size() * sizeof(resource_location));
        }
        emit_align(4);
    };
    
    auto emit_tl_vec = [&](const std::vector<tlresource_location>& v) {
        emit_align(8); emit_align(4);
        if (!v.empty()) {
            const uint8_t* p = (const uint8_t*)v.data();
            out.insert(out.end(), p, p + v.size() * sizeof(tlresource_location));
        }
        emit_align(4);
    };
    
    emit_i32_vec(P.parents);
    emit_res_vec(P.res_locs);
    emit_tl_vec(P.textures);
    emit_tl_vec(P.mesh_files);
    emit_tl_vec(P.meshes);
    emit_tl_vec(P.morph_files);
    emit_tl_vec(P.morphs);
    emit_tl_vec(P.material_files);
    emit_tl_vec(P.materials);
    emit_tl_vec(P.anim_files);
    emit_tl_vec(P.anims);
    emit_tl_vec(P.scene_anims);
    emit_tl_vec(P.skeletons);
    
    // Pad to base offset
    if (out.size() < P.base()) {
        out.resize(P.base(), 0xE3);
    }
    
    printf("Header area ends at 0x%zX, base is 0x%X\n", out.size(), P.base());
    
    // Write payload data
    for (size_t i = 0; i < new_resources.size(); ++i) {
        const auto& nr = new_resources[i];
        size_t start = (size_t)P.base() + nr.new_offset;
        
        // Ensure output is large enough
        if (out.size() < start + nr.new_size) {
            out.resize(start + nr.new_size, 0);
        }
        
        memcpy(&out[start], nr.data.data(), nr.new_size);
    }
    
    // Write output
    fs::path out_path = out_pack.empty() ?
        (orig_pack.parent_path() / (orig_pack.stem().string() + ".NEW.PCPACK")) : out_pack;
    
    if (!out_path.parent_path().empty())
        fs::create_directories(out_path.parent_path());
    
    write_file(out_path, out);
    
    printf("\nImport complete!\n");
    printf("  Output: %s\n", out_path.string().c_str());
    printf("  Size: %zu bytes (0x%zX)\n", out.size(), out.size());
}

// ==================== Main ====================

static void print_usage() {
    printf("PCPACK Tool - Ultimate Spider-Man (2005) PC\n\n");
    printf("Usage:\n");
    printf("  pcpack_tool export <input.pcpack> [output_dir] [dictionary.txt]\n");
    printf("  pcpack_tool import <original.pcpack> <input_dir> <output.pcpack> [--align N]\n");
    printf("\nExport extracts all resources and creates a manifest file.\n");
    printf("Import rebuilds the PCPACK using files from input_dir, updating all offsets.\n");
    printf("\nOptions:\n");
    printf("  --align N   Align payloads to N bytes (default: 16)\n");
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            print_usage();
            return 1;
        }
        
        std::string cmd = argv[1];
        
        if (cmd == "export") {
            fs::path pack_path = argv[2];
            fs::path out_dir = (argc > 3) ? argv[3] : "";
            fs::path dict_path = (argc > 4) ? argv[4] : "";
            do_export(pack_path, out_dir, dict_path);
        }
        else if (cmd == "import") {
            if (argc < 5) {
                print_usage();
                return 1;
            }
            fs::path orig_pack = argv[2];
            fs::path input_dir = argv[3];
            fs::path out_pack = argv[4];
            
            size_t align_val = 16;
            for (int i = 5; i < argc - 1; ++i) {
                if (std::string(argv[i]) == "--align") {
                    align_val = std::stoul(argv[i + 1]);
                }
            }
            
            do_import(orig_pack, input_dir, out_pack, align_val);
        }
        else {
            print_usage();
            return 1;
        }
        
        return 0;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}