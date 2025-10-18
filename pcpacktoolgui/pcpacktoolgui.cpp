// pcpack_tool_gui.cpp — Ultimate Spider-Man PCPACK importer/exporter (GUI)
// C++17, single translation unit. Windows (Win32) GUI.
//
// What you get:
//  - Export: dumps payloads using names from string_hash_dictionary.txt + platform ext table.
//  - Import: preserves directory/vector area, re-emits vector bytes, writes payloads at original
//            or recomputed offsets (if "Update directory" is checked). Optional payload alignment.
//  - Log window for progress/errors.
//
// Build (MinGW):
//   g++ -std=c++17 -O2 pcpack_tool_gui.cpp -municode -lole32 -luuid -lcomdlg32 -lgdi32 -o pcpack_tool_gui.exe
// Build (MSVC):
//   cl /std:c++17 /O2 pcpack_tool_gui.cpp /DUNICODE /D_UNICODE comdlg32.lib ole32.lib uuid.lib gdi32.lib user32.lib
//
// Author: ChatGPT (GPT-5 Thinking)

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
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
#include <thread>
#include <mutex>
#include <functional>

namespace fs = std::filesystem;

// ===================== Core logic (from your CLI version) =====================

static inline void fail(const char* msg) {
    throw std::runtime_error(msg);
}
static inline void ensure(bool cond, const char* msg) {
    if (!cond) fail(msg);
}
template<class T>
static inline T read_le(const uint8_t* p) { T v{}; std::memcpy(&v, p, sizeof(T)); return v; }

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

#pragma pack(push,1)
struct resource_versions { uint32_t field_0, field_4, field_8, field_C, field_10; };
static_assert(sizeof(resource_versions) == 0x14, "resource_versions size mismatch");

struct resource_pack_header {
    resource_versions field_0; // 0x00..0x13
    uint32_t          field_14;
    uint32_t          directory_offset;
    uint32_t          res_dir_mash_size;  // "base"
    uint32_t          field_20;
    uint32_t          field_24;
    uint32_t          field_28;
};
static_assert(sizeof(resource_pack_header) == 0x2C, "resource_pack_header size mismatch");

struct generic_mash_header { int32_t safety_key, field_4, field_8; int16_t class_id, field_E; };
static_assert(sizeof(generic_mash_header) == 0x10, "generic_mash_header size mismatch");

struct string_hash { uint32_t source_hash_code; };

struct resource_key { string_hash m_hash; uint32_t m_type; };

struct resource_location { resource_key field_0; uint32_t m_offset; uint32_t m_size; };
static_assert(sizeof(resource_location) == 0x10, "resource_location size mismatch");

template<typename T>
struct mashable_vector_t {
    uint32_t m_data;   // on-disk pointer placeholder (ignored)
    uint16_t m_size;
    uint8_t  m_shared;
    uint8_t  field_7;
};
static_assert(sizeof(mashable_vector_t<uint32_t>) == 8, "mashable_vector_t size mismatch");

struct tlresource_location {
    string_hash name;
    uint8_t     type;
    uint8_t     pad[3];
    uint32_t    offset;
};
static_assert(sizeof(tlresource_location) == 0x0C, "tlresource_location size mismatch");

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
    mashable_vector_t<int32_t> field_68;
    mashable_vector_t<int32_t> field_70;
    int32_t pack_slot, base, field_80, field_84, field_88;
    int32_t type_start_idxs[70];
    int32_t type_end_idxs[70];
};
static_assert(sizeof(resource_directory) == 0x2BC, "resource_directory size mismatch");
#pragma pack(pop)

// type -> extension
static const std::vector<std::string> resource_key_type_ext = {
    ".NONE",".PCANIM",".PCSKEL",".ALS",".ENT",".ENTEXT",".DDS",".DDSMP",".IFL",".DESC",".ENS",".SPL",".AB",".QP",".TRIG",".PCSX",".INST",".FDF",".PANEL",".TXT",".ICN",
    ".PCMESH",".PCMORPH",".PCMAT",".COLL",".PCPACK",".PCSANIM",".MSN",".MARKER",".HH",".WAV",".WBK",
    ".M2V","M2V",".PFX",".CSV",".CLE",".LIT",".GRD",".GLS",".LOD",".SIN",".GV",".SV",".TOKENS",".DSG",".PATH",".PTRL",".LANG",".SLF",".VISEME",
    ".PCMESHDEF",".PCMORPHDEF",".PCMATDEF",".MUT",".ASG",".BAI",".CUT",".INTERACT",".CSV",".CSV","._ENTID_","._ANIMID_","._REGIONID_","._AI_GENERIC_ID_","._RADIOMSG_","._GOAL_","._IFC_ATTRIBUTE_","._SIGNAL_","._PACKGROUP_"
};

static std::unordered_map<uint32_t, std::string> g_hashDict;
static void load_hash_dictionary(const fs::path& dictPath) {
    if (dictPath.empty()) return;
    std::ifstream f(dictPath);
    if (!f) fail(("Could not open dictionary: " + dictPath.string()).c_str());
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string hx, name;
        if (!(iss >> hx >> name)) continue;
        if (hx.rfind("0x", 0) == 0 || hx.rfind("0X", 0) == 0) {
            uint32_t v = (uint32_t)std::stoul(hx, nullptr, 16);
            g_hashDict[v] = name;
        }
    }
}

static inline size_t align_up(size_t x, size_t a) { size_t m = x % a; return m ? x + (a - m) : x; }
static inline void write_align_with(uint8_t fill, std::vector<uint8_t>& out, size_t a) {
    size_t want = align_up(out.size(), a);
    if (want > out.size()) out.insert(out.end(), want - out.size(), fill);
}
static std::string platform_ext(uint32_t type) { return (type < resource_key_type_ext.size()) ? resource_key_type_ext[type] : ".UNK"; }
static std::string platform_name(uint32_t hash, uint32_t type) {
    auto it = g_hashDict.find(hash);
    char hx[11]; std::snprintf(hx, sizeof(hx), "0x%08X", hash);
    std::string base = (it != g_hashDict.end()) ? it->second : std::string(hx);
    return base + platform_ext(type);
}

struct ParsedPack {
    resource_pack_header packHeader{};
    generic_mash_header  mashHeader{};
    resource_directory   dir{};
    std::vector<resource_location> resLocs;
    std::vector<tlresource_location> textures, mesh_files, meshes, morph_files, morphs,
        material_files, materials, anim_files, anims, scene_anims, skeletons;
    std::vector<uint8_t> wholeFile;
};
static void read_exact(std::ifstream& f, void* dst, size_t n) {
    f.read((char*)dst, (std::streamsize)n);
    if (f.gcount() != (std::streamsize)n) fail("Unexpected EOF");
}
static void parse_vectors_after_directory(std::ifstream& f, ParsedPack& P) {
    auto rebase = [&](size_t i) {
        auto pos = (size_t)f.tellg();
        size_t want = align_up(pos, i);
        if (want > pos) f.seekg((std::streamoff)want, std::ios::beg);
        };
    rebase(8); rebase(4);
    if (P.dir.parents.m_size > 0) {
        std::vector<uint32_t> parents(P.dir.parents.m_size);
        read_exact(f, parents.data(), parents.size() * sizeof(uint32_t));
    }
    rebase(4);
    auto read_tl = [&](uint16_t count, std::vector<tlresource_location>& dst) {
        rebase(8); rebase(4);
        if (count) { dst.resize(count); read_exact(f, dst.data(), count * sizeof(tlresource_location)); }
        rebase(4);
        };
    auto read_res = [&](uint16_t count, std::vector<resource_location>& dst) {
        rebase(8); rebase(4);
        if (count) { dst.resize(count); read_exact(f, dst.data(), count * sizeof(resource_location)); }
        rebase(4);
        };
    read_res(P.dir.resource_locations.m_size, P.resLocs);
    read_tl(P.dir.texture_locations.m_size, P.textures);
    read_tl(P.dir.mesh_file_locations.m_size, P.mesh_files);
    read_tl(P.dir.mesh_locations.m_size, P.meshes);
    read_tl(P.dir.morph_file_locations.m_size, P.morph_files);
    read_tl(P.dir.morph_locations.m_size, P.morphs);
    read_tl(P.dir.material_file_locations.m_size, P.material_files);
    read_tl(P.dir.material_locations.m_size, P.materials);
    read_tl(P.dir.anim_file_locations.m_size, P.anim_files);
    read_tl(P.dir.anim_locations.m_size, P.anims);
    read_tl(P.dir.scene_anim_locations.m_size, P.scene_anims);
    read_tl(P.dir.skeleton_locations.m_size, P.skeletons);
}
static ParsedPack parse_pack(const fs::path& inPath) {
    ParsedPack P; P.wholeFile = read_file_bin(inPath);
    std::ifstream f(inPath, std::ios::binary);
    if (!f) fail(("Cannot open pack: " + inPath.string()).c_str());
    read_exact(f, &P.packHeader, sizeof(P.packHeader));
    f.seekg((std::streamoff)P.packHeader.directory_offset, std::ios::beg);
    read_exact(f, &P.mashHeader, sizeof(P.mashHeader));
    read_exact(f, &P.dir, sizeof(P.dir));
    parse_vectors_after_directory(f, P);
    return P;
}

// flags from GUI
static bool  g_updateDir = false;
static size_t g_payloadAlign = 1;

static void do_export(const fs::path& packPath, const fs::path& outDir, std::function<void(const std::string&)> log) {
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
        for (char& c : fname) { if (c < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_'; }
        fs::path outPath = targetDir / fname;

        std::ofstream of(outPath, std::ios::binary);
        if (!of) fail(("Cannot write: " + outPath.string()).c_str());
        of.write((const char*)&P.wholeFile[(size_t)start], rl.m_size);
        of.close();

        char buf[256];
        std::snprintf(buf, sizeof(buf), "Exported %s: [0x%08X..0x%08X)\r\n", fname.c_str(), (uint32_t)start, (uint32_t)end);
        log(buf);
    }
    log("Export done.\r\n");
}

static inline size_t align_up_sz(size_t x, size_t a) { if (a <= 1) return x; size_t m = x % a; return m ? x + (a - m) : x; }

static void do_import(const fs::path& packPath, const fs::path& inputFolder, const fs::path& outPackPath, std::function<void(const std::string&)> log) {
    auto P = parse_pack(packPath);
    const uint32_t base = P.packHeader.res_dir_mash_size;
    fs::path folder = inputFolder.empty() ? packPath.stem() : inputFolder;

    std::vector<uint8_t> out;
    out.resize(sizeof(resource_pack_header), 0);
    std::memcpy(out.data(), &P.packHeader, sizeof(P.packHeader));

    if (out.size() < P.packHeader.directory_offset)
        out.resize(P.packHeader.directory_offset, 0);

    resource_directory writableDir = P.dir;
    writableDir.base = (int32_t)base;

    // mash header
    out.insert(out.end(), (const uint8_t*)&P.mashHeader, (const uint8_t*)&P.mashHeader + sizeof(P.mashHeader));
    // directory
    out.insert(out.end(), (const uint8_t*)&writableDir, (const uint8_t*)&writableDir + sizeof(writableDir));

    auto emit_align = [&](size_t i) { write_align_with(0xE3, out, i); };

    emit_align(8); emit_align(4);
    if (writableDir.parents.m_size > 0) { uint32_t zero = 0; out.insert(out.end(), (uint8_t*)&zero, (uint8_t*)&zero + sizeof(uint32_t)); }
    emit_align(4);

    std::vector<resource_location> workingResLocs = P.resLocs;
    std::vector<size_t> newStarts(workingResLocs.size(), 0);
    std::vector<size_t> newSizes(workingResLocs.size(), 0);

    if (g_updateDir) {
        size_t cursor = base;
        for (size_t i = 0; i < workingResLocs.size(); ++i) {
            const auto& rl = workingResLocs[i];
            const std::string fname = platform_name(rl.field_0.m_hash.source_hash_code, rl.field_0.m_type);
            fs::path inFile = folder / fname;
            size_t payloadSize = (size_t)rl.m_size;
            if (fs::exists(inFile)) payloadSize = (size_t)fs::file_size(inFile);
            else log("Missing file, keep original: " + inFile.string() + "\r\n");
            cursor = align_up_sz(cursor, g_payloadAlign);
            newStarts[i] = cursor; newSizes[i] = payloadSize;
            cursor += payloadSize;
        }
        for (size_t i = 0; i < workingResLocs.size(); ++i) {
            workingResLocs[i].m_offset = (uint32_t)(newStarts[i] - base);
            workingResLocs[i].m_size = (uint32_t)newSizes[i];
        }
    }

    auto emit_vec_bytes = [&](const void* data, size_t elemSize, size_t count) {
        emit_align(8); emit_align(4);
        if (count) {
            const uint8_t* p = (const uint8_t*)data;
            out.insert(out.end(), p, p + elemSize * count);
        }
        emit_align(4);
        };

    if (!workingResLocs.empty()) emit_vec_bytes(workingResLocs.data(), sizeof(resource_location), workingResLocs.size());
    else { emit_align(8); emit_align(4); emit_align(4); }

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

    size_t fileEndTarget = out.size();

    if (g_updateDir) {
        for (size_t i = 0; i < workingResLocs.size(); ++i) {
            const auto& rlNew = workingResLocs[i];
            size_t start = (size_t)base + (size_t)rlNew.m_offset;
            size_t size = (size_t)rlNew.m_size;
            if (out.size() < start) out.resize(start, 0x00);
            if (out.size() < start + size) out.resize(start + size, 0x00);

            const auto& rlOld = P.resLocs[i];
            const std::string fname = platform_name(rlOld.field_0.m_hash.source_hash_code, rlOld.field_0.m_type);
            fs::path inFile = folder / fname;

            if (fs::exists(inFile)) {
                auto data = read_file_bin(inFile);
                std::memcpy(&out[start], data.data(), std::min<size_t>(data.size(), size));
                if (data.size() < size) std::memset(&out[start + data.size()], 0, size - data.size());
            }
            else {
                size_t oldStart = (size_t)base + (size_t)rlOld.m_offset;
                size_t copyN = std::min<size_t>(size, (size_t)rlOld.m_size);
                std::memcpy(&out[start], &P.wholeFile[oldStart], copyN);
                if (size > copyN) std::memset(&out[start + copyN], 0, size - copyN);
            }
            fileEndTarget = std::max(fileEndTarget, start + size);

            char buf[192];
            std::snprintf(buf, sizeof(buf), "Repacked payload -> [%#010zx .. %#010zx)\r\n", start, start + size);
            log(buf);
        }
    }
    else {
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
                log("Missing file, kept original: " + inFile.string() + "\r\n");
            }
            char buf[192];
            std::snprintf(buf, sizeof(buf), "Wrote payload at [0x%08zX..0x%08zX)\r\n", start, end);
            log(buf);
        }
        fileEndTarget = std::max(fileEndTarget, out.size());
    }
    if (out.size() < fileEndTarget) out.resize(fileEndTarget, 0x00);

    fs::path outPath = outPackPath.empty() ? (packPath.parent_path() / (packPath.stem().string() + ".NEW.PCPACK")) : outPackPath;
    if (!outPath.parent_path().empty()) fs::create_directories(outPath.parent_path());
    write_file_bin(outPath, out);

    char fin[256];
    std::snprintf(fin, sizeof(fin), "Import done. Wrote: %s (size: %zu bytes)\r\n", outPath.string().c_str(), out.size());
    log(fin);
}

// ===================== GUI (Win32) =====================

static const wchar_t* APP_CLASS = L"PCPACK_TOOL_GUI_CLASS";
static const int ID_MODE_EXPORT = 1001, ID_MODE_IMPORT = 1002;
static const int ID_EDIT_PACK = 1003, ID_BTN_PACK = 1004;
static const int ID_EDIT_DICT = 1005, ID_BTN_DICT = 1006;
static const int ID_EDIT_INDIR = 1007, ID_BTN_INDIR = 1008;
static const int ID_EDIT_OUTDIR = 1009, ID_BTN_OUTDIR = 1010;
static const int ID_EDIT_OUTPACK = 1011, ID_BTN_OUTPACK = 1012;
static const int ID_CHK_UPDATEDIR = 1013, ID_EDIT_ALIGN = 1014;
static const int ID_BTN_RUN = 1015, ID_LOG = 1016;

static HWND g_hWnd, g_log;
static std::mutex g_logMutex;

static void AppendLog(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_log) return;
    std::wstring ws; ws.reserve(s.size());
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    ws.resize(need);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), need);
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)ws.c_str());
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

static std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s; s.resize(n);
    if (n > 0) GetWindowTextW(h, s.data(), n + 1);
    return s;
}
static std::string Narrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s; s.resize(need);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), need, nullptr, nullptr);
    return s;
}

static bool BrowseForFile(HWND owner, bool save, const wchar_t* filter, std::wstring& path) {
    wchar_t buf[MAX_PATH] = { 0 };
    if (!path.empty()) wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    ofn.lpstrDefExt = L"pcpack";
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok) { path = buf; return true; }
    return false;
}

static bool BrowseForFolder(HWND owner, std::wstring& path) {
    BROWSEINFOW bi{}; bi.lpszTitle = L"Select folder";
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t buf[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, buf)) { path = buf; CoTaskMemFree(pidl); return true; }
        CoTaskMemFree(pidl);
    }
    return false;
}

static void SetEnabled(HWND h, BOOL en) { EnableWindow(h, en); }

// Worker thread wrapper
struct RunParams {
    bool doExport;
    std::wstring pack, dict, inDir, outDir, outPack;
    bool updateDir;
    size_t align;
};
static void RunWork(RunParams p) {
    try {
        AppendLog("[*] Loading dictionary (if provided)...\r\n");
        if (!p.dict.empty()) load_hash_dictionary(fs::path(Narrow(p.dict)));
        g_updateDir = p.updateDir;
        g_payloadAlign = p.align ? p.align : 1;

        if (p.doExport) {
            AppendLog("[*] Exporting...\r\n");
            do_export(fs::path(Narrow(p.pack)),
                fs::path(Narrow(p.outDir)),
                AppendLog);
        }
        else {
            AppendLog("[*] Importing...\r\n");
            do_import(fs::path(Narrow(p.pack)),
                fs::path(Narrow(p.inDir)),
                fs::path(Narrow(p.outPack)),
                AppendLog);
        }
        AppendLog("[✓] Done.\r\n");
    }
    catch (const std::exception& e) {
        AppendLog(std::string("[!] Error: ") + e.what() + "\r\n");
    }
    // Re-enable UI
    PostMessageW(g_hWnd, WM_COMMAND, MAKELONG(ID_BTN_RUN, BN_CLICKED), 0xDEADBEEF); // special unblock toggle
}

// Layout helper
static void Place(HWND h, int x, int y, int w, int hgt) { MoveWindow(h, x, y, w, hgt, TRUE); }

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hwnd;
        CreateWindowW(L"BUTTON", L"Export", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            10, 10, 80, 20, hwnd, (HMENU)ID_MODE_EXPORT, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Import", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            100, 10, 80, 20, hwnd, (HMENU)ID_MODE_IMPORT, nullptr, nullptr);
        CheckRadioButton(hwnd, ID_MODE_EXPORT, ID_MODE_IMPORT, ID_MODE_EXPORT);

        CreateWindowW(L"STATIC", L"PCPACK:", WS_CHILD | WS_VISIBLE, 10, 40, 70, 20, hwnd, 0, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            80, 40, 420, 22, hwnd, (HMENU)ID_EDIT_PACK, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, 510, 40, 30, 22, hwnd, (HMENU)ID_BTN_PACK, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Dictionary:", WS_CHILD | WS_VISIBLE, 10, 70, 70, 20, hwnd, 0, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            80, 70, 420, 22, hwnd, (HMENU)ID_EDIT_DICT, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, 510, 70, 30, 22, hwnd, (HMENU)ID_BTN_DICT, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Input folder (Import):", WS_CHILD | WS_VISIBLE, 10, 100, 140, 20, hwnd, 0, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            150, 100, 350, 22, hwnd, (HMENU)ID_EDIT_INDIR, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, 510, 100, 30, 22, hwnd, (HMENU)ID_BTN_INDIR, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Output folder (Export):", WS_CHILD | WS_VISIBLE, 10, 130, 140, 20, hwnd, 0, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            150, 130, 350, 22, hwnd, (HMENU)ID_EDIT_OUTDIR, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, 510, 130, 30, 22, hwnd, (HMENU)ID_BTN_OUTDIR, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Output PCPACK (Import):", WS_CHILD | WS_VISIBLE, 10, 160, 140, 20, hwnd, 0, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            150, 160, 350, 22, hwnd, (HMENU)ID_EDIT_OUTPACK, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, 510, 160, 30, 22, hwnd, (HMENU)ID_BTN_OUTPACK, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Update directory (recompute offsets/sizes)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            10, 190, 280, 20, hwnd, (HMENU)ID_CHK_UPDATEDIR, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Payload align:", WS_CHILD | WS_VISIBLE, 300, 190, 90, 20, hwnd, 0, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
            390, 190, 50, 22, hwnd, (HMENU)ID_EDIT_ALIGN, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Run", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            450, 188, 90, 26, hwnd, (HMENU)ID_BTN_RUN, nullptr, nullptr);

        g_log = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            10, 220, 530, 250, hwnd, (HMENU)ID_LOG, nullptr, nullptr);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(wParam), h = HIWORD(wParam);
        if (g_log) Place(g_log, 10, 220, w - 20, h - 230);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == ID_BTN_PACK) {
            std::wstring p; if (BrowseForFile(hwnd, false, L"PCPACK\0*.pcpack;*.bin;*\0All\0*.*\0", p)) {
                SetWindowTextW(GetDlgItem(hwnd, ID_EDIT_PACK), p.c_str());
            }
        }
        else if (id == ID_BTN_DICT) {
            std::wstring p; if (BrowseForFile(hwnd, false, L"Text\0*.txt\0All\0*.*\0", p)) {
                SetWindowTextW(GetDlgItem(hwnd, ID_EDIT_DICT), p.c_str());
            }
        }
        else if (id == ID_BTN_INDIR) {
            std::wstring p; if (BrowseForFolder(hwnd, p)) {
                SetWindowTextW(GetDlgItem(hwnd, ID_EDIT_INDIR), p.c_str());
            }
        }
        else if (id == ID_BTN_OUTDIR) {
            std::wstring p; if (BrowseForFolder(hwnd, p)) {
                SetWindowTextW(GetDlgItem(hwnd, ID_EDIT_OUTDIR), p.c_str());
            }
        }
        else if (id == ID_BTN_OUTPACK) {
            std::wstring p; if (BrowseForFile(hwnd, true, L"PCPACK\0*.pcpack\0All\0*.*\0", p)) {
                SetWindowTextW(GetDlgItem(hwnd, ID_EDIT_OUTPACK), p.c_str());
            }
        }
        else if (id == ID_BTN_RUN && lParam != 0xDEADBEEF) { // normal click
            // disable inputs while running
            for (int cid : {ID_MODE_EXPORT, ID_MODE_IMPORT, ID_EDIT_PACK, ID_BTN_PACK, ID_EDIT_DICT, ID_BTN_DICT, ID_EDIT_INDIR, ID_BTN_INDIR, ID_EDIT_OUTDIR, ID_BTN_OUTDIR, ID_EDIT_OUTPACK, ID_BTN_OUTPACK, ID_CHK_UPDATEDIR, ID_EDIT_ALIGN, ID_BTN_RUN})
                SetEnabled(GetDlgItem(hwnd, cid), FALSE);

            bool doExport = (SendDlgItemMessageW(hwnd, ID_MODE_EXPORT, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RunParams p{};
            p.doExport = doExport;
            p.pack = GetText(GetDlgItem(hwnd, ID_EDIT_PACK));
            p.dict = GetText(GetDlgItem(hwnd, ID_EDIT_DICT));
            p.inDir = GetText(GetDlgItem(hwnd, ID_EDIT_INDIR));
            p.outDir = GetText(GetDlgItem(hwnd, ID_EDIT_OUTDIR));
            p.outPack = GetText(GetDlgItem(hwnd, ID_EDIT_OUTPACK));
            p.updateDir = (SendDlgItemMessageW(hwnd, ID_CHK_UPDATEDIR, BM_GETCHECK, 0, 0) == BST_CHECKED);

            wchar_t buf[32]; GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_ALIGN), buf, 32);
            p.align = _wtoi(buf); if (p.align == 0) p.align = 1;

            // sanity
            if (p.pack.empty()) {
                MessageBoxW(hwnd, L"Select a PCPACK file.", L"Missing input", MB_ICONWARNING);
                for (int cid : {ID_MODE_EXPORT, ID_MODE_IMPORT, ID_EDIT_PACK, ID_BTN_PACK, ID_EDIT_DICT, ID_BTN_DICT, ID_EDIT_INDIR, ID_BTN_INDIR, ID_EDIT_OUTDIR, ID_BTN_OUTDIR, ID_EDIT_OUTPACK, ID_BTN_OUTPACK, ID_CHK_UPDATEDIR, ID_EDIT_ALIGN, ID_BTN_RUN})
                    SetEnabled(GetDlgItem(hwnd, cid), TRUE);
                break;
            }
            if (doExport) {
                // outDir optional; will default to <pack.stem()>
            }
            else {
                // import path checks
                if (p.outPack.empty()) AppendLog("[i] Output PCPACK not set; default will be <pack>.NEW.PCPACK\r\n");
            }

            // clear log
            SetWindowTextW(g_log, L"");
            AppendLog("=== PCPACK Tool GUI ===\r\n");

            std::thread(RunWork, p).detach();
        }
        else if (id == ID_BTN_RUN && lParam == 0xDEADBEEF) {
            // worker finished -> re-enable UI
            for (int cid : {ID_MODE_EXPORT, ID_MODE_IMPORT, ID_EDIT_PACK, ID_BTN_PACK, ID_EDIT_DICT, ID_BTN_DICT, ID_EDIT_INDIR, ID_BTN_INDIR, ID_EDIT_OUTDIR, ID_BTN_OUTDIR, ID_EDIT_OUTPACK, ID_BTN_OUTPACK, ID_CHK_UPDATEDIR, ID_EDIT_ALIGN, ID_BTN_RUN})
                SetEnabled(GetDlgItem(hwnd, cid), TRUE);
        }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.lpszClassName = APP_CLASS; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(APP_CLASS, L"PCPACK Tool (Ultimate Spider-Man) — Import/Export",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 570, 540, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nCmd); UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}

int  main(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.lpszClassName = APP_CLASS; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(APP_CLASS, L"PCPACK Tool (Ultimate Spider-Man) — Import/Export",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 570, 540, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nCmd); UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
