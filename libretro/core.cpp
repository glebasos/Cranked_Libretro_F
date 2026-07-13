#include "Cranked.hpp"
#include "libretro.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <random>
#include <algorithm>

// Use libzippp to extract archives when needed
#include "libzippp.h"

using namespace cranked;
namespace fs = std::filesystem;

static Cranked *instance;
static shared_ptr<Rom> rom;

static retro_log_callback logging;
static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

// Track any temporary extraction directory we create (if needed later)
static fs::path s_temp_extract_dir;

// Core options
static struct retro_core_option_v2_category option_cats[] = {
    {
        "crank",
        "Crank Settings",
        "Configure crank input behavior"
    },
    {
        "video",
        "Video Settings",
        "Configure display appearance"
    },
    { NULL, NULL, NULL },
};

static struct retro_core_option_v2_definition option_defs[] = {
    {
        "cranked_crank_docked",
        "Initial Crank State",
        NULL,
        "Whether the crank starts docked (unavailable) or undocked (available) when a game loads.",
        NULL,
        "crank",
        {
            { "undocked", "Undocked (Available)" },
            { "docked", "Docked (Unavailable)" },
            { NULL, NULL },
        },
        "undocked"
    },
    {
        "cranked_crank_angle",
        "Initial Crank Angle",
        NULL,
        "The initial angle of the crank in degrees (0-360). Default is 90 degrees.",
        NULL,
        "crank",
        {
            { "0", "0°" },
            { "45", "45°" },
            { "90", "90° (Default)" },
            { "135", "135°" },
            { "180", "180°" },
            { "225", "225°" },
            { "270", "270°" },
            { "315", "315°" },
            { NULL, NULL },
        },
        "90"
    },
    {
        "cranked_palette",
        "Display Palette",
        NULL,
        "Color palette for the display. Choose from various monochrome and color schemes.",
        NULL,
        "video",
        {
            { "bw", "Black & White (Default)" },
            { "wb", "White & Black (Inverted)" },
            { "gray", "Gray & Black" },
            { "green", "Green (Game Boy)" },
            { "amber", "Amber" },
            { "blue", "Blue" },
            { NULL, NULL },
        },
        "bw"
    },
    { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

static struct retro_core_options_v2 options = {
    option_cats,
    option_defs
};

// Helper function to apply display palette
static void apply_palette() {
    if (!instance) return;

    struct retro_variable var = { "cranked_palette", NULL };
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (strcmp(var.value, "wb") == 0) {
            // White & Black (Inverted)
            instance->graphics.displayBufferOffColor = 0x00FFFFFF;
            instance->graphics.displayBufferOnColor = 0x00000000;
        } else if (strcmp(var.value, "gray") == 0) {
            // Gray & Black
            instance->graphics.displayBufferOffColor = 0x00000000;
            instance->graphics.displayBufferOnColor = 0x00AAAAAA;
        } else if (strcmp(var.value, "green") == 0) {
            // Green (Game Boy)
            instance->graphics.displayBufferOffColor = 0x000F380F;
            instance->graphics.displayBufferOnColor = 0x009BBC0F;
        } else if (strcmp(var.value, "amber") == 0) {
            // Amber
            instance->graphics.displayBufferOffColor = 0x00402000;
            instance->graphics.displayBufferOnColor = 0x00FFAA00;
        } else if (strcmp(var.value, "blue") == 0) {
            // Blue
            instance->graphics.displayBufferOffColor = 0x00001040;
            instance->graphics.displayBufferOnColor = 0x0060A0FF;
        } else {
            // Black & White (Default)
            instance->graphics.displayBufferOffColor = 0x00000000;
            instance->graphics.displayBufferOnColor = 0x00FFFFFF;
        }
    }
}

static fs::path make_temp_extract_dir() {
    auto base = fs::temp_directory_path();
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    for (int i = 0; i < 10; i++) {
        auto candidate = base / (std::string("cranked_libretro_") + std::to_string(dist(gen)));
        std::error_code ec;
        if (fs::create_directory(candidate, ec))
            return candidate;
    }
    throw std::runtime_error("Cranked: failed to create temp extraction directory");
}

static bool path_is_within(const fs::path &parent, const fs::path &child) {
    std::error_code ec;
    auto p = fs::weakly_canonical(parent, ec);
    auto c = fs::weakly_canonical(child, ec);
    if (ec) return false;
    auto pit = p.begin();
    auto cit = c.begin();
    for (; pit != p.end(); ++pit, ++cit) {
        if (cit == c.end() || *pit != *cit) return false;
    }
    return true;
}

static fs::path extract_zip_to_temp(const fs::path &zip_path) {
    libzippp::ZipArchive zip(zip_path.string());
    if (!zip.open(libzippp::ZipArchive::ReadOnly))
        throw std::runtime_error("Cranked: failed to open zip");

    auto out_dir = make_temp_extract_dir();
    log_cb(RETRO_LOG_INFO, "Cranked: extracting to temp: %s\n", out_dir.u8string().c_str());

    for (auto &entry : zip.getEntries()) {
        auto name = entry.getName();
        // Normalize separators
        std::replace(name.begin(), name.end(), '\\', '/');
        auto dest = out_dir / fs::path(name).relative_path();
        // ZipSlip guard
        if (!path_is_within(out_dir, dest)) {
            zip.close();
            throw std::runtime_error("Cranked: ZipSlip detected");
        }
        if (entry.isDirectory()) {
            std::error_code ec;
            fs::create_directories(dest, ec);
            continue;
        }
        // Ensure parent dir exists
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        auto data = entry.readAsBinary();
        std::ofstream of(dest, std::ios::binary);
        if (!of.is_open()) {
            delete[] (char*)data;
            zip.close();
            throw std::runtime_error("Cranked: failed to create output file");
        }
        of.write((const char*)data, (std::streamsize)entry.getSize());
        of.close();
        delete[] (char*)data;
    }
    zip.close();
    return out_dir;
}

static bool ends_with_case_insensitive(const std::string &s, const std::string &suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = (char)std::tolower((unsigned char)s[s.size() - suffix.size() + i]);
        char b = (char)std::tolower((unsigned char)suffix[i]);
        if (a != b) return false;
    }
    return true;
}

static void fallback_log(retro_log_level level, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

static void emulatorCallback(Cranked &context) {
    // libretro controls the frame timing, no additional work needed here
}

static void updateInputs() {
    int32 inputs = 0;

    // Map libretro inputs to Playdate buttons
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))      // A button
        inputs |= (int)PDButtons::A;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))      // B button
        inputs |= (int)PDButtons::B;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
        inputs |= (int)PDButtons::Up;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
        inputs |= (int)PDButtons::Down;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
        inputs |= (int)PDButtons::Left;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
        inputs |= (int)PDButtons::Right;

    instance->currentInputs = inputs;

    // Crank control: Use right analog stick X-axis for crank rotation
    // Range: -32768 to 32767, map to 0-360 degrees
    int16_t analog_x = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
    int16_t analog_y = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

    static int crank_debug_counter = 0;
    static float last_angle = 0.0f;

    // Calculate angle from analog stick position
    if (analog_x != 0 || analog_y != 0) {
        float angle = atan2f((float)analog_y, (float)analog_x) * 180.0f / 3.14159265f;
        if (angle < 0) angle += 360.0f;
        instance->crankAngle = angle;
        instance->crankDocked = false;
        if (crank_debug_counter++ % 60 == 0) {
            fprintf(stderr, "Crank: analog stick active, angle=%.1f, x=%d, y=%d\n", angle, analog_x, analog_y);
        }
        last_angle = angle;
    }

    // Alternative: Use L/R shoulder buttons to rotate crank incrementally
    static float button_crank_angle = 0.0f;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R)) {
        button_crank_angle += 5.0f;  // Rotate right
        if (button_crank_angle >= 360.0f) button_crank_angle -= 360.0f;
        instance->crankAngle = button_crank_angle;
        instance->crankDocked = false;
        last_angle = button_crank_angle;
    }
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L)) {
        button_crank_angle -= 5.0f;  // Rotate left
        if (button_crank_angle < 0.0f) button_crank_angle += 360.0f;
        instance->crankAngle = button_crank_angle;
        instance->crankDocked = false;
        last_angle = button_crank_angle;
    }

    // Dock/undock crank with L3 (left stick press)
    static bool prev_l3 = false;
    bool l3 = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
    if (l3 && !prev_l3) {
        instance->crankDocked = !instance->crankDocked;
        fprintf(stderr, "Crank: toggled dock state, docked=%d\n", instance->crankDocked);
    }
    prev_l3 = l3;
}

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>

// Diagnostic aid: print a symbolized stack trace on access violations, since
// crashes inside the frontend process otherwise vanish without a trace
static LONG WINAPI crankedCrashHandler(PEXCEPTION_POINTERS info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    static bool reported = false;
    if (reported)
        return EXCEPTION_CONTINUE_SEARCH;
    reported = true;
    fprintf(stderr, "CRANKED ACCESS VIOLATION at %p accessing %p\n",
            info->ExceptionRecord->ExceptionAddress, (void *)info->ExceptionRecord->ExceptionInformation[1]);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip; frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 32; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &frame, &ctx,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        DWORD64 pc = frame.AddrPC.Offset;
        if (!pc)
            break;
        char buf[sizeof(SYMBOL_INFO) + 256]{};
        auto sym = (SYMBOL_INFO *)buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp{};
        IMAGEHLP_LINE64 line{sizeof(IMAGEHLP_LINE64)};
        DWORD lineDisp{};
        if (SymFromAddr(proc, pc, &disp, sym)) {
            if (SymGetLineFromAddr64(proc, pc, &lineDisp, &line))
                fprintf(stderr, "  #%02d %s+0x%llx (%s:%lu)\n", i, sym->Name, disp, line.FileName, line.LineNumber);
            else
                fprintf(stderr, "  #%02d %s+0x%llx\n", i, sym->Name, disp);
        } else
            fprintf(stderr, "  #%02d %p\n", i, (void *)pc);
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void retro_init() {
    fprintf(stderr, "Cranked core: retro_init() called\n");
#ifdef _WIN32
    AddVectoredExceptionHandler(1, crankedCrashHandler);
#endif
    instance = new Cranked;
    instance->config.updateCallback = emulatorCallback;
    instance->graphics.displayBufferNativeEndian = true;

    // Disable internal frame timing - libretro controls frame rate
    instance->graphics.framerate = 0;
    fprintf(stderr, "Cranked core: retro_init() completed\n");
}

void retro_deinit() {
    delete instance;
    instance = nullptr;
    rom.reset();
}

void retro_reset() {
    fprintf(stderr, "Cranked core: retro_reset() called\n");
    if (instance && instance->rom) {
        instance->reset();
        instance->start();
    }
}

void retro_run() {
    static int frame_count = 0;
    if (frame_count < 5) {
        fprintf(stderr, "Cranked core: retro_run() called (frame %d)\n", frame_count);
        frame_count++;
    }

    if (!instance || !instance->rom) {
        // No game loaded, render a blank frame
        static uint32 blank_frame[DISPLAY_HEIGHT][DISPLAY_WIDTH] = {};
        video_cb(blank_frame, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH * sizeof(uint32));
        if (frame_count == 1) {
            fprintf(stderr, "Cranked core: No game loaded, rendering blank frame\n");
        }
        return;
    }

    // Update inputs from libretro
    input_poll_cb();
    updateInputs();

    // Apply display palette (check for option changes)
    apply_palette();

    // Lightweight debug: print input bitmask occasionally
    if ((frame_count % 60) == 0) {
        fprintf(stderr, "Cranked core: inputs=0x%02x\n", instance ? instance->currentInputs : 0);
    }

    // Update the emulator (this also flushes the display buffer internally)
    try {
        instance->update();
    } catch (const std::exception &ex) {
        log_cb(RETRO_LOG_ERROR, "Cranked update error: %s\n", ex.what());
        // Continue running, don't crash
    }

    // Send frame to frontend (displayBufferRGBA is already in XRGB8888 format)
    // displayBufferRGBA is now a 1D array to ensure no padding/stride issues
    video_cb(instance->graphics.displayBufferRGBA, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_WIDTH * sizeof(uint32));

    // Pump the audio mixer: 44100 Hz / 60 fps = 735 frames per video frame
    constexpr int AUDIO_FRAMES = 735;
    static int16_t audioBuffer[AUDIO_FRAMES * 2];
    instance->audio.sampleAudio(audioBuffer, AUDIO_FRAMES);
    audio_batch_cb(audioBuffer, AUDIO_FRAMES);
}

bool retro_load_game(const retro_game_info *info) {
    fprintf(stderr, "\n");
    fprintf(stderr, "=====================================\n");
    fprintf(stderr, "CRANKED: retro_load_game() CALLED!!!\n");
    fprintf(stderr, "=====================================\n");
    fprintf(stderr, "\n");

    auto fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        log_cb(RETRO_LOG_ERROR, "XRGB8888 pixel format is not supported by frontend\n");
        fprintf(stderr, "Cranked core: Pixel format not supported\n");
        return false;
    }

    if (!info || !info->path) {
        log_cb(RETRO_LOG_ERROR, "No game path provided\n");
        fprintf(stderr, "Cranked core: No game path provided\n");
        return false;
    }

    const char *cpath = info->path;
    fs::path input_path = fs::path(cpath);

    // Log raw input path (Unicode-safe via u8string where available)
    std::string input_path_log = input_path.string();
    log_cb(RETRO_LOG_INFO, "Cranked: raw input path: %s\n", input_path_log.c_str());
    fprintf(stderr, "Cranked core: raw input path: %s\n", input_path_log.c_str());

    // Resolve canonical path when possible (best-effort)
    fs::path resolved_path = input_path;
    try { resolved_path = fs::canonical(input_path); } catch (...) {}
    std::string resolved_path_log = resolved_path.string();
    log_cb(RETRO_LOG_INFO, "Cranked: resolved path: %s\n", resolved_path_log.c_str());
    fprintf(stderr, "Cranked core: resolved path: %s\n", resolved_path_log.c_str());

    // Determine how we will load the content:
    // - If directory and ends with .pdx, use directly
    // - If directory and not .pdx, search for nested .pdx
    // - If file and ends with .zip or .pdx.zip, extract to temp then find .pdx
    fs::path bundle_path = resolved_path;
    bool is_dir = false;
    std::error_code ec;
    is_dir = fs::is_directory(bundle_path, ec);

    if (is_dir) {
        // If user picked the parent directory, search for a nested .pdx dir
        auto filename_str = bundle_path.filename().string();
        if (!ends_with_case_insensitive(filename_str, ".pdx")) {
            bool found = false;
            for (auto &entry : fs::directory_iterator(bundle_path, ec)) {
                if (!entry.is_directory()) continue;
                auto name = entry.path().filename().string();
                if (ends_with_case_insensitive(name, ".pdx")) {
                    bundle_path = entry.path();
                    found = true;
                    break;
                }
            }
            if (!found) {
                log_cb(RETRO_LOG_ERROR, "Cranked: no .pdx directory found under: %s\n", resolved_path_log.c_str());
                return false;
            }
        }
        log_cb(RETRO_LOG_INFO, "Cranked: using .pdx directory: %s\n", bundle_path.string().c_str());
        fprintf(stderr, "Cranked core: using .pdx directory: %s\n", bundle_path.string().c_str());
    } else {
        // Not a directory; treat as archive or single-file bundle the core can handle
        const bool is_zip = ends_with_case_insensitive(resolved_path_log, ".zip") ||
                            ends_with_case_insensitive(resolved_path_log, ".pdx.zip");
        if (is_zip) {
            try {
                s_temp_extract_dir = extract_zip_to_temp(resolved_path);
                // Find a .pdx directory within the extracted contents
                fs::path found;
                std::error_code ec2;
                for (auto &e : fs::recursive_directory_iterator(s_temp_extract_dir, ec2)) {
                    if (!e.is_directory()) continue;
                    auto n = e.path().filename().string();
                    if (ends_with_case_insensitive(n, ".pdx")) { found = e.path(); break; }
                }
                if (found.empty()) {
                    log_cb(RETRO_LOG_ERROR, "Cranked: extracted zip but no .pdx dir found\n");
                    return false;
                }
                bundle_path = found;
                log_cb(RETRO_LOG_INFO, "Cranked: extracted bundle path: %s\n", bundle_path.string().c_str());
                fprintf(stderr, "Cranked core: extracted bundle path: %s\n", bundle_path.string().c_str());
            } catch (const std::exception &ex) {
                log_cb(RETRO_LOG_ERROR, "Cranked: zip extraction failed: %s\n", ex.what());
                return false;
            }
        } else {
            // Allow direct file path (e.g., main.pdz packed builds) – Rom handles this
            log_cb(RETRO_LOG_INFO, "Cranked: using file bundle: %s\n", resolved_path_log.c_str());
        }
    }

    try {
        // Load the ROM using only filesystem paths (no in-memory ZIP)
        log_cb(RETRO_LOG_INFO, "Cranked: calling instance->load()...\n");
        instance->load(bundle_path.string());
        log_cb(RETRO_LOG_INFO, "Cranked: instance->load() completed\n");

        if (!instance->rom) {
            log_cb(RETRO_LOG_ERROR, "Failed to load ROM: instance->rom is null\n");
            return false;
        }
        log_cb(RETRO_LOG_INFO, "ROM object created successfully\n");

        // Apply core options before starting
        struct retro_variable var_docked = { "cranked_crank_docked", NULL };
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var_docked) && var_docked.value) {
            if (strcmp(var_docked.value, "docked") == 0) {
                // Start with crank docked (unavailable)
                instance->crankDocked = true;
                instance->previousCrankDocked = true;
                log_cb(RETRO_LOG_INFO, "Cranked: crank will start docked (unavailable)\n");
            } else {
                // Start with crank undocked (available) - this triggers crankUndocked callback
                instance->crankDocked = false;
                instance->previousCrankDocked = true;
                log_cb(RETRO_LOG_INFO, "Cranked: crank will start undocked (available)\n");
            }
        }

        // Apply initial crank angle
        struct retro_variable var_angle = { "cranked_crank_angle", NULL };
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var_angle) && var_angle.value) {
            float angle = (float)atof(var_angle.value);
            instance->crankAngle = angle;
            instance->previousCrankAngle = angle;
            log_cb(RETRO_LOG_INFO, "Cranked: initial crank angle set to %.0f degrees\n", angle);
        }

        // Apply display palette
        apply_palette();

        // Start the emulator
        log_cb(RETRO_LOG_INFO, "Cranked: starting emulator...\n");
        instance->start();
        log_cb(RETRO_LOG_INFO, "Cranked: emulator started\n");

        const char *name = instance->rom->getName();
        const char *author = instance->rom->getAuthor();
        log_cb(RETRO_LOG_INFO, "Successfully loaded: %s by %s\n",
               name ? name : "Unknown",
               author ? author : "Unknown");

        return true;
    } catch (const std::exception &ex) {
        log_cb(RETRO_LOG_ERROR, "Cranked: failed to load game: %s\n", ex.what());
        instance->unload();
        return false;
    }
}

void retro_unload_game() {
    if (instance) {
        try {
            instance->unload();
            log_cb(RETRO_LOG_INFO, "Game unloaded successfully\n");
        } catch (const std::exception &ex) {
            log_cb(RETRO_LOG_ERROR, "Error unloading game: %s\n", ex.what());
        }
    }

    // Cleanup any temporary extraction directory if we ever created one
    if (!s_temp_extract_dir.empty()) {
        std::error_code ec;
        if (fs::exists(s_temp_extract_dir, ec)) {
            fs::remove_all(s_temp_extract_dir, ec);
            if (ec)
                log_cb(RETRO_LOG_WARN, "Cranked: failed to remove temp dir: %s (%d)\n", s_temp_extract_dir.u8string().c_str(), (int)ec.value());
            else
                log_cb(RETRO_LOG_INFO, "Cranked: removed temp dir: %s\n", s_temp_extract_dir.u8string().c_str());
        }
        s_temp_extract_dir.clear();
    }
}

uint retro_api_version(void) {
    fprintf(stderr, "Cranked core: retro_api_version() called, returning %u\n", RETRO_API_VERSION);
    return RETRO_API_VERSION;
}

void retro_set_controller_port_device(uint port, uint device) {

}

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Cranked";
    info->library_version = "v1";
    info->need_fullpath = true;
    info->block_extract = true;  // Frontend does NOT extract; core opens archive from path
    info->valid_extensions = "zip|pdx";  // Accept .zip (including .pdx.zip) and .pdx directories if frontend allows

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "Cranked core: retro_get_system_info() called\n");
    fprintf(stderr, "Cranked core: retro_get_system_info() called, extensions: %s, block_extract=%d\n",
            info->valid_extensions, info->block_extract);
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    fprintf(stderr, "Cranked core: retro_get_system_av_info() called\n");
    info->timing = {.fps = 60.0, .sample_rate = 44100.0};

    info->geometry = {
            .base_width = DISPLAY_WIDTH,
            .base_height = DISPLAY_HEIGHT,
            .max_width = DISPLAY_WIDTH,
            .max_height = DISPLAY_HEIGHT,
            .aspect_ratio = (float) DISPLAY_WIDTH / DISPLAY_HEIGHT,
    };
    fprintf(stderr, "Cranked core: retro_get_system_av_info() completed (fps=%.1f, %dx%d)\n",
            info->timing.fps, DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void retro_set_environment(retro_environment_t cb) {
    fprintf(stderr, "Cranked core: retro_set_environment() called\n");
    environ_cb = cb;

    bool no_content = false;  // IMPORTANT: We NEED content (game files)
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;
    else
        log_cb = fallback_log;

    // Set core options
    unsigned version = 0;
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2) {
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options);
    }

    fprintf(stderr, "Cranked core: retro_set_environment() completed, log_cb = %p, no_content = %d\n",
            (void*)log_cb, no_content);
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
    video_cb = cb;
}

uint retro_get_region() {
    return RETRO_REGION_NTSC;
}

bool retro_load_game_special(uint type, const retro_game_info *info, size_t num) {
    fprintf(stderr, "\n");
    fprintf(stderr, "=====================================\n");
    fprintf(stderr, "CRANKED: retro_load_game_special() CALLED!!!\n");
    fprintf(stderr, "Type: %u, Num: %zu\n", type, num);
    fprintf(stderr, "=====================================\n");
    fprintf(stderr, "\n");
    return false;
}

size_t retro_serialize_size() {
    return 0;
}

bool retro_serialize(void *data, size_t size) {
    return true;
}

bool retro_unserialize(const void *data, size_t size) {
    return true;
}

void *retro_get_memory_data(uint id) {
    return nullptr;
}

size_t retro_get_memory_size(uint id) {
    return 0;
}

void retro_cheat_reset() {

}

void retro_cheat_set(uint index, bool enabled, const char *code) {

}
