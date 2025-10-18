// pcpack_tool.cpp — Ultimate Spider-Man PCPACK importer/exporter
// C++17 single-file. Mirrors the provided Python logic (read_pcpack + build_pack).
// - Export: dumps every resource payload using names from string_hash_dictionary.txt + platform ext table.
// - Import: writes a new pack by copying headers + resource_directory blob, re-emitting vector data
//           with the same alignment scheme, then rewriting payloads at their original offsets.
//           Pads file end to match original size.
//
// Notes:
// * We DO NOT attempt to rebuild or re-link the in-file pointers; we preserve the original directory
//   and only replace payload bytes (this is exactly what your Python build did).
// * Align rules replicate Python rebase(): align 8 then 4, then arrays, using 0xE3 fill bytes in the mash area.
// * Offsets for payloads are computed as base + res_loc.m_offset where base = pack_header.res_dir_mash_size.
// * TL vectors are parsed to validate structure, but they’re not used for export/import decisions.
//
// Author: ChatGPT (GPT-5 Thinking)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;

static inline void fail(const char* msg) {
    throw std::runtime_error(msg);
}

static inline void ensure(bool cond, const char* msg) {
    if (!cond) fail(msg);
}

template<class T>
static inline T read_le(const uint8_t* p) {
    // Pack structs are little-endian on PC — but we use packed C structs so this is just helper if needed.
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

// -------- File helpers
static inline std::vector<uint8_t> read_file_bin(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) fail(("Cannot open file: " + p.string()).c_str());
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf((size_t)sz);
    if (sz > 0) f.read((char*)buf.data(), (std::streamsize)buf.size());
    return buf;
}

static inline void write_file_bin(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary);
    if (!f) fail(("Cannot write file: " + p.string()).c_str());
    if (!data.empty()) f.write((const char*)data.data(), (std::streamsize)data.size());
}

// -------- Packed structs (mirror Python ctypes sizes)
#pragma pack(push,1)

struct resource_versions { // 0x14
    uint32_t field_0;
    uint32_t field_4;
    uint32_t field_8;
    uint32_t field_C;
    uint32_t field_10;
};
static_assert(sizeof(resource_versions) == 0x14, "resource_versions size mismatch");

struct resource_pack_header { // 0x2C
    resource_versions field_0;   // 0x00..0x13
    uint32_t          field_14;  // 0x14
    uint32_t          directory_offset;      // 0x18
    uint32_t          res_dir_mash_size;     // 0x1C (base)
    uint32_t          field_20;  // 0x20
    uint32_t          field_24;  // 0x24
    uint32_t          field_28;  // 0x28
};
static_assert(sizeof(resource_pack_header) == 0x2C, "resource_pack_header size mismatch");

struct generic_mash_header { // 0x10
    int32_t safety_key;
    int32_t field_4;
    int32_t field_8;   // offset to mash data (relative to this header's base area)
    int16_t class_id;
    int16_t field_E;
};
static_assert(sizeof(generic_mash_header) == 0x10, "generic_mash_header size mismatch");

struct string_hash { // 0x4
    uint32_t source_hash_code;
    bool operator==(const string_hash& o) const { return source_hash_code == o.source_hash_code; }
    bool operator!=(const string_hash& o) const { return !(*this == o); }
};

struct resource_key { // 0x8
    string_hash m_hash; // 0x0
    uint32_t    m_type; // 0x4
};

struct resource_location { // 0x10
    resource_key field_0; // 0x00
    uint32_t     m_offset;// 0x08 (relative to base)
    uint32_t     m_size;  // 0x0C
};
static_assert(sizeof(resource_location) == 0x10, "resource_location size mismatch");

template<typename T>
struct mashable_vector_t { // 8 bytes
    // Matches your Python: (T* m_data; uint16_t m_size; bool m_shared; bool field_7)
    // We only trust m_size + field_7 flags; m_data is an in-file pointer we DO NOT dereference.
    uint32_t m_data;   // on-disk pointer/offset placeholder (ignored)
    uint16_t m_size;
    uint8_t  m_shared;
    uint8_t  field_7;
};
static_assert(sizeof(mashable_vector_t<uint32_t>) == 8, "mashable_vector_t size mismatch");

struct tlresource_location { // 0xC
    string_hash name;   // 0x0
    uint8_t     type;   // 0x4 (c_char in Python)
    uint8_t     pad[3]; // padding to keep sizes consistent
    uint32_t    offset; // 0x8
};
static_assert(sizeof(tlresource_location) == 0x0C, "tlresource_location size mismatch");

// We only need the parts we actually use during (un)mash. Keep exact size = 0x2BC.
struct resource_directory { // 0x2BC
    // 0x00
    mashable_vector_t<int32_t>          parents;                // vector<int>
    mashable_vector_t<resource_location> resource_locations;    // vector<resource_location>
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
    // 13 * 8 = 104 bytes so far

    // Next fields we don’t rely on but must preserve size:
    mashable_vector_t<int32_t> field_68;  // 8
    mashable_vector_t<int32_t> field_70;  // 8

    int32_t pack_slot; // 4
    int32_t base;      // 4
    int32_t field_80;  // 4
    int32_t field_84;  // 4
    int32_t field_88;  // 4

    int32_t type_start_idxs[70]; // 70*4 = 280
    int32_t type_end_idxs[70];   // 70*4 = 280
};
static_assert(sizeof(resource_directory) == 0x2BC, "resource_directory size mismatch");

#pragma pack(pop)

// ---- TLRESOURCE TYPEs (only for sanity checks)
enum {
    TLRESOURCE_TYPE_NONE = 0,
    TLRESOURCE_TYPE_TEXTURE = 1,
    TLRESOURCE_TYPE_MESH_FILE = 2,
    TLRESOURCE_TYPE_MESH = 3,
    TLRESOURCE_TYPE_MORPH_FILE = 4,
    TLRESOURCE_TYPE_MORPH = 5,
    TLRESOURCE_TYPE_MATERIAL_FILE = 6,
    TLRESOURCE_TYPE_MATERIAL = 7,
    TLRESOURCE_TYPE_ANIM_FILE = 8,
    TLRESOURCE_TYPE_ANIM = 9,
    TLRESOURCE_TYPE_SCENE_ANIM = 10,
    TLRESOURCE_TYPE_SKELETON = 11,
    TLRESOURCE_TYPE_Z = 12
};

enum {
    RESOURCE_KEY_TYPE_NONE = 0,
    RESOURCE_KEY_TYPE_MESH_FILE_STRUCT = 51,
    RESOURCE_KEY_TYPE_MATERIAL_FILE_STRUCT = 53,
    RESOURCE_KEY_TYPE_Z = 70
};

// -------- platform ext table
static const std::vector<std::string> resource_key_type_ext = {
    ".NONE",".PCANIM",".PCSKEL",".ALS",".ENT",".ENTEXT",".DDS",".DDSMP",".IFL",".DESC",".ENS",".SPL",".AB",".QP",".TRIG",".PCSX",".INST",".FDF",".PANEL",".TXT",".ICN",
    ".PCMESH",".PCMORPH",".PCMAT",".COLL",".PCPACK",".PCSANIM",".MSN",".MARKER",".HH",".WAV",".WBK",
    ".M2V","M2V",".PFX",".CSV",".CLE",".LIT",".GRD",".GLS",".LOD",".SIN",
    ".GV",".SV",".TOKENS",".DSG",".PATH",".PTRL",".LANG",".SLF",".VISEME",".PCMESHDEF",".PCMORPHDEF",".PCMATDEF",".MUT",".ASG",".BAI",".CUT",".INTERACT",".CSV",".CSV","._ENTID_","._ANIMID_","._REGIONID_","._AI_GENERIC_ID_","._RADIOMSG_","._GOAL_","._IFC_ATTRIBUTE_","._SIGNAL_","._PACKGROUP_"
};

// -------- string hash dictionary
static std::unordered_map<uint32_t, std::string> g_hashDict;

static void load_hash_dictionary(const fs::path& dictPath) {
    if (dictPath.empty()) return;
    std::ifstream f(dictPath);
    if (!f) fail(("Could not open dictionary: " + dictPath.string()).c_str());
    std::string line;
    size_t lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string hx, name;
        if (!(iss >> hx >> name)) continue;
        // Expect "0xDEADBEEF name"
        if (hx.rfind("0x", 0) == 0 || hx.rfind("0X", 0) == 0) {
            uint32_t v = (uint32_t)std::stoul(hx, nullptr, 16);
            g_hashDict[v] = name;
        }
    }
    ensure(!g_hashDict.empty(), "string_hash_dictionary.txt is empty or invalid");
}

// -------- align helpers
static inline size_t align_up(size_t x, size_t i) {
    size_t m = x % i;
    return m ? (x + (i - m)) : x;
}

static inline void write_align_with(uint8_t fillByte, std::vector<uint8_t>& out, size_t i) {
    size_t want = align_up(out.size(), i);
    if (want > out.size()) out.insert(out.end(), want - out.size(), fillByte);
}

// -------- small name helpers
static std::string platform_ext(uint32_t type) {
    if (type < resource_key_type_ext.size()) return resource_key_type_ext[type];
    return ".UNK";
}

static std::string platform_name(uint32_t hash, uint32_t type) {
    auto it = g_hashDict.find(hash);
    char hx[11]; // 0xFFFFFFFF + NUL
    std::snprintf(hx, sizeof(hx), "0x%08X", hash);
    std::string base = (it != g_hashDict.end()) ? it->second : std::string(hx);
    return base + platform_ext(type);
}

// -------- parsing the pack "directory + vectors" like Python

struct ParsedPack {
    resource_pack_header packHeader{};
    generic_mash_header  mashHeader{};
    resource_directory   dir{};
    std::vector<resource_location> resLocs;    // size = dir.resource_locations.m_size
    // TL vectors (for sanity only)
    std::vector<tlresource_location> textures, mesh_files, meshes, morph_files, morphs,
        material_files, materials, anim_files, anims, scene_anims, skeletons;

    std::vector<uint8_t> wholeFile; // original bytes to copy from
};

static void read_exact(std::ifstream& f, void* dst, size_t n) {
    f.read((char*)dst, (std::streamsize)n);
    if (f.gcount() != (std::streamsize)n) fail("Unexpected EOF");
}

// Unmash according to your Python: after resource_directory blob:
//   align 8 -> align 4 -> parents (u32 * size 1 used) -> align 4
//   Then for each vector: align 8 -> align 4 -> read array -> align 4
static void parse_vectors_after_directory(std::ifstream& f, ParsedPack& P) {
    auto rebase = [&](size_t i) {
        auto pos = (size_t)f.tellg();
        size_t want = align_up(pos, i);
        if (want > pos) {
            f.seekg((std::streamoff)(want), std::ios::beg);
        }
        };

    rebase(8);
    rebase(4);

    // parents: Python only wrote one int back; read the count though.
    if (P.dir.parents.m_size > 0) {
        // Move to current position and read m_size * 4 (but most packs have tiny parents)
        std::vector<uint32_t> parents(P.dir.parents.m_size);
        read_exact(f, parents.data(), parents.size() * sizeof(uint32_t));
    }

    rebase(4);

    auto read_tl_vec = [&](uint16_t count, std::vector<tlresource_location>& dst) {
        rebase(8);
        rebase(4);
        if (count) {
            dst.resize(count);
            read_exact(f, dst.data(), count * sizeof(tlresource_location));
        }
        rebase(4);
        };

    auto read_resloc_vec = [&](uint16_t count, std::vector<resource_location>& dst) {
        rebase(8);
        rebase(4);
        if (count) {
            dst.resize(count);
            read_exact(f, dst.data(), count * sizeof(resource_location));
        }
        rebase(4);
        };

    read_resloc_vec(P.dir.resource_locations.m_size, P.resLocs);

    read_tl_vec(P.dir.texture_locations.m_size, P.textures);
    read_tl_vec(P.dir.mesh_file_locations.m_size, P.mesh_files);
    read_tl_vec(P.dir.mesh_locations.m_size, P.meshes);
    read_tl_vec(P.dir.morph_file_locations.m_size, P.morph_files);
    read_tl_vec(P.dir.morph_locations.m_size, P.morphs);
    read_tl_vec(P.dir.material_file_locations.m_size, P.material_files);
    read_tl_vec(P.dir.material_locations.m_size, P.materials);
    read_tl_vec(P.dir.anim_file_locations.m_size, P.anim_files);
    read_tl_vec(P.dir.anim_locations.m_size, P.anims);
    read_tl_vec(P.dir.scene_anim_locations.m_size, P.scene_anims);
    read_tl_vec(P.dir.skeleton_locations.m_size, P.skeletons);
}

static ParsedPack parse_pack(const fs::path& inPath) {
    ParsedPack P;
    P.wholeFile = read_file_bin(inPath);

    std::ifstream f(inPath, std::ios::binary);
    if (!f) fail(("Cannot open pack: " + inPath.string()).c_str());

    // Header
    read_exact(f, &P.packHeader, sizeof(P.packHeader));

    // Jump to directory_offset and read mash header + directory
    f.seekg((std::streamoff)P.packHeader.directory_offset, std::ios::beg);
    read_exact(f, &P.mashHeader, sizeof(P.mashHeader));

    read_exact(f, &P.dir, sizeof(P.dir));

    // Basic checks similar to Python prints
    if (P.packHeader.field_0.field_0 == 14) {
        // USM NTSC 1.0
    }
    else if (P.packHeader.field_0.field_0 == 10) {
        // USM Prototype 2005-06-20
    }

    // Unmash vectors (just read arrays in place following directory struct)
    parse_vectors_after_directory(f, P);

    return P;
}

// ---- Export (dump files)
static void do_export(const fs::path& packPath, const fs::path& outDir) {
    auto P = parse_pack(packPath);
    const uint32_t base = P.packHeader.res_dir_mash_size;

    fs::path targetDir = outDir.empty() ? packPath.stem() : outDir;
    fs::create_directories(targetDir);

    for (size_t i = 0; i < P.resLocs.size(); ++i) {
        const auto& rl = P.resLocs[i];
        const uint32_t hash = rl.field_0.m_hash.source_hash_code;
        const uint32_t type = rl.field_0.m_type;

        uint64_t start = (uint64_t)base + rl.m_offset;
        uint64_t end = start + rl.m_size;
        ensure(end <= P.wholeFile.size(), "Payload range out of file bounds");

        std::string fname = platform_name(hash, type);
        // sanitize filename
        for (char& c : fname) {
            if (c < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        }

        fs::path outPath = targetDir / fname;
        std::ofstream of(outPath, std::ios::binary);
        if (!of) fail(("Cannot write: " + outPath.string()).c_str());
        of.write((const char*)&P.wholeFile[(size_t)start], rl.m_size);
        of.close();

        std::printf("Exported %s: [0x%08X..0x%08X)\n",
            fname.c_str(), (uint32_t)start, (uint32_t)end);
    }
    std::puts("Done.");
}

static inline size_t align_up_sz(size_t x, size_t a) {
    if (a == 0 || a == 1) return x;
    size_t m = x % a;
    return m ? (x + (a - m)) : x;
}
static bool  g_updateDir = false;
static size_t g_payloadAlign = 1;



// ---- Import (repack with original structure/offsets)

static void do_import(const fs::path& packPath,
    const fs::path& inputFolder,
    const fs::path& outPackPath) {
    auto P = parse_pack(packPath);
    const uint32_t base = P.packHeader.res_dir_mash_size;

    fs::path folder = inputFolder.empty() ? packPath.stem() : inputFolder;

    // We will emit into 'out' like before.
    std::vector<uint8_t> out;
    out.resize(sizeof(resource_pack_header), 0);
    std::memcpy(out.data(), &P.packHeader, sizeof(P.packHeader));

    // Seek to directory_offset and write mash header + directory (possibly with updated base field)
    if (out.size() < P.packHeader.directory_offset)
        out.resize(P.packHeader.directory_offset, 0);

    // Make a writable copy of the directory struct (so we can ensure base is set)
    resource_directory writableDir = P.dir;
    // Keep base consistent with header (for tooling sanity)
    writableDir.base = (int32_t)base;

    // mash header
    out.insert(out.end(),
        (const uint8_t*)&P.mashHeader,
        (const uint8_t*)&P.mashHeader + sizeof(P.mashHeader));

    // directory blob (will point to arrays we emit next)
    out.insert(out.end(),
        (const uint8_t*)&writableDir,
        (const uint8_t*)&writableDir + sizeof(writableDir));

    // Re-emit vector payload area (same as before)
    auto emit_align = [&](size_t i) { write_align_with(0xE3, out, i); };

    emit_align(8);
    emit_align(4);

    // parents: as before, write a single u32 zero if present count > 0 (we don't rely on its content)
    if (writableDir.parents.m_size > 0) {
        uint32_t zero = 0;
        out.insert(out.end(), (uint8_t*)&zero, (uint8_t*)&zero + sizeof(uint32_t));
    }
    emit_align(4);

    // If we're updating directory, we’ll mutate a working copy of resLocs with new offsets/sizes.
    std::vector<resource_location> workingResLocs = P.resLocs;

    // When --update-dir is enabled, determine new offsets/sizes from files:
    // We *do not* write payloads here yet—just compute metadata; we’ll write payloads after all vectors are serialized.
    std::vector<size_t> newStarts(workingResLocs.size(), 0);
    std::vector<size_t> newSizes(workingResLocs.size(), 0);

    if (g_updateDir) {
        size_t cursor = base; // absolute file offset where payload area starts
        for (size_t i = 0; i < workingResLocs.size(); ++i) {
            const auto& rl = workingResLocs[i];
            const std::string fname = platform_name(rl.field_0.m_hash.source_hash_code, rl.field_0.m_type);
            fs::path inFile = folder / fname;

            size_t payloadSize = (size_t)rl.m_size; // default to original
            if (fs::exists(inFile)) {
                payloadSize = (size_t)fs::file_size(inFile);
            }
            else {
                // fall back to original size if replacement missing
                std::fprintf(stderr, "Missing file, will keep original bytes: %s\n", inFile.string().c_str());
            }

            // Align cursor to requested boundary (if any)
            cursor = align_up_sz(cursor, g_payloadAlign);

            newStarts[i] = cursor;     // absolute file offset
            newSizes[i] = payloadSize;

            // advance
            cursor += payloadSize;
        }

        // Now patch resource_location entries (offset is relative to base)
        for (size_t i = 0; i < workingResLocs.size(); ++i) {
            workingResLocs[i].m_offset = (uint32_t)(newStarts[i] - base);
            workingResLocs[i].m_size = (uint32_t)newSizes[i];
        }
    }

    // Helper to write arrays for each vector
    auto emit_vec_bytes = [&](const void* data, size_t elemSize, size_t count) {
        emit_align(8);
        emit_align(4);
        if (count) {
            const uint8_t* p = (const uint8_t*)data;
            out.insert(out.end(), p, p + elemSize * count);
        }
        emit_align(4);
        };

    // IMPORTANT: emit resource_locations from the *working* vector (patched if --update-dir)
    if (!workingResLocs.empty()) {
        emit_vec_bytes(workingResLocs.data(), sizeof(resource_location), workingResLocs.size());
    }
    else {
        emit_align(8); emit_align(4); emit_align(4);
    }

    // The rest TL vectors are unchanged
    auto emit_tl = [&](const std::vector<tlresource_location>& v) {
        if (!v.empty()) emit_vec_bytes(v.data(), sizeof(tlresource_location), v.size());
        else { emit_align(8); emit_align(4); emit_align(4); }
        };

    emit_tl(P.textures);
    emit_tl(P.mesh_files);
    emit_tl(P.meshes);
    emit_tl(P.morph_files);
    emit_tl(P.morphs);
    emit_tl(P.material_files);
    emit_tl(P.materials);
    emit_tl(P.anim_files);
    emit_tl(P.anims);
    emit_tl(P.scene_anims);
    emit_tl(P.skeletons);

    // Ensure capacity up to end of last payload we will write
    size_t fileEndTarget = out.size();

    // Write payloads:
    // - If --update-dir: write at newStarts[i] (aligned), using file size or original content if missing.
    // - Else (legacy): keep original positions/sizes like before.
    if (g_updateDir) {
        for (size_t i = 0; i < workingResLocs.size(); ++i) {
            const auto& rlNew = workingResLocs[i];
            size_t start = (size_t)base + (size_t)rlNew.m_offset;  // absolute
            size_t size = (size_t)rlNew.m_size;

            // grow buffer
            if (out.size() < start) out.resize(start, 0x00);
            if (out.size() < start + size) out.resize(start + size, 0x00);

            const auto& rlOld = P.resLocs[i];
            const std::string fname = platform_name(rlOld.field_0.m_hash.source_hash_code, rlOld.field_0.m_type);
            fs::path inFile = folder / fname;

            if (fs::exists(inFile)) {
                auto data = read_file_bin(inFile);
                // clip/pad to size if needed (size equals file size by construction)
                std::memcpy(&out[start], data.data(), std::min<size_t>(data.size(), size));
                if (data.size() < size) {
                    std::memset(&out[start + data.size()], 0, size - data.size());
                }
            }
            else {
                // write original bytes from original position/size (may be smaller/larger than rlNew.m_size)
                size_t oldStart = (size_t)base + (size_t)rlOld.m_offset;
                size_t copyN = std::min<size_t>(size, (size_t)rlOld.m_size);
                std::memcpy(&out[start], &P.wholeFile[oldStart], copyN);
                if (size > copyN) {
                    std::memset(&out[start + copyN], 0, size - copyN);
                }
            }

            fileEndTarget = std::max(fileEndTarget, start + size);
            std::printf("Repacked payload -> [%#010zx .. %#010zx)\n", start, start + size);
        }
    }
    else {
        // Legacy path (unchanged): keep original positions/sizes
        if (out.size() < P.wholeFile.size()) out.resize(P.wholeFile.size(), 0);
        for (const auto& rl : P.resLocs) {
            size_t start = (size_t)base + (size_t)rl.m_offset;
            size_t end = start + (size_t)rl.m_size;
            ensure(end <= out.size(), "Payload range out of bounds in output");

            std::string fname = platform_name(rl.field_0.m_hash.source_hash_code, rl.field_0.m_type);
            fs::path inFile = folder / fname;

            if (fs::exists(inFile)) {
                auto data = read_file_bin(inFile);
                size_t n = std::min<size_t>(data.size(), rl.m_size);
                std::memcpy(&out[start], data.data(), n);
                if (rl.m_size > n) std::memset(&out[start + n], 0, rl.m_size - n);
            }
            else {
                std::memcpy(&out[start], &P.wholeFile[start], rl.m_size);
                std::fprintf(stderr, "Missing file, kept original bytes: %s\n", inFile.string().c_str());
            }
            std::printf("Wrote payload at [0x%08zX..0x%08zX)\n", start, end);
        }
        fileEndTarget = std::max(fileEndTarget, out.size());
    }

    // Trim/extend final size to exactly fileEndTarget (no need to keep original size now)
    if (out.size() < fileEndTarget) out.resize(fileEndTarget, 0x00);

    // Output filename
    fs::path outPath;
    if (outPackPath.empty()) {
        // Same directory as input, filename = <stem>._PCPACK
        outPath = packPath.parent_path() / (packPath.stem().string() + "._PCPACK");
    }
    else {
        outPath = outPackPath;
    }

    // (optional) ensure parent exists
    if (!outPath.parent_path().empty())
        fs::create_directories(outPath.parent_path());


    write_file_bin(outPath, out);
    std::printf("Done. Wrote: %s (size: %zu bytes)\n", outPath.string().c_str(), out.size());
}

// pcpacks_extract.cpp — C++17 single-file PCPACK extractor
// Usage: pcpacks_extract <pack.bin> [--out outdir] [--flat] [--dataoff 0x...]
// Finds 2nd 0xE3E3E3E3, parses directory (name_hash,type,rel_off,size), dumps payloads.

using namespace std;

static const uint32_t SENT = 0xE3E3E3E3;

struct DirEntry { uint32_t name_hash, type, rel_off, size; };

static uint32_t rd32LE(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }



// -------- CLI
static void print_help() {
    std::puts(
        R"(pcpack_tool — Ultimate Spider-Man PCPACK importer/exporter

Usage:
  pcpack_tool export <file.PCPACK> [--dict string_hash_dictionary.txt] [--outdir <dir>]
  pcpack_tool import <file.PCPACK> [--dict string_hash_dictionary.txt] [--in <dir>] [--out <file.PCPACK>]

Notes:
- Export dumps all resource payloads into the folder <file> (or --outdir).
- Import reads files from the folder (default <file>), writes a new pack preserving the directory/vectors and original size.
- If a file is missing during import, the original bytes are kept for that resource.
)");
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) { print_help(); return 0; }
        std::string cmd = argv[1];
        fs::path pack = argv[2];

        fs::path dictPath, inDir, outDir, outPack;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--dict" && i + 1 < argc) dictPath = argv[++i];
            else if (a == "--in" && i + 1 < argc) inDir = argv[++i];
            else if (a == "--outdir" && i + 1 < argc) outDir = argv[++i];
            else if (a == "--out" && i + 1 < argc) outPack = argv[++i];
            else if (a == "--update-dir") g_updateDir = true;
            else if (a == "--payload-align" && i + 1 < argc) g_payloadAlign = (size_t)std::stoul(argv[++i]);
        }

        if (!dictPath.empty()) load_hash_dictionary(dictPath);

        if (cmd == "export") {
            do_export(pack, outDir);
        }
        else if (cmd == "import") {
            do_import(pack, inDir, outPack);
        }
        else {
            print_help();
        }
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}