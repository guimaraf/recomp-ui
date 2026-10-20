// psx_menu_provider_imgui.cpp -- SDL input and Dear ImGui presentation for
// recomp-ui's psxrecomp menu provider (recomp_ui_psx_menu.h). The model and
// its wiring to the runtime live in psx_menu_provider.cpp.
#if defined(__has_include)
#if __has_include("psx_menu_host.h")
#define RUI_PSX_HAS_MENU_HOST 1
#endif
#endif

#if defined(RUI_PSX_HAS_MENU_HOST)
#include "psx_menu_provider_internal.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#if defined(LNG_SDL3)
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include <cfloat>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include "host_nameplates.h"
#include "recomp_ui_psx_nameplates.h"

namespace rui_psx {
namespace {

ImGuiContext *g_ctx = nullptr;   // GL thread
bool g_gl_ready = false;
uint64_t g_last_ns = 0;
unsigned g_pad_held = 0;         // main thread

#if defined(LNG_SDL3)
constexpr unsigned kBack = 1u << SDL_GAMEPAD_BUTTON_BACK;
constexpr unsigned kL3 = 1u << SDL_GAMEPAD_BUTTON_LEFT_STICK;
#endif

int key_input(int key) {
    switch (key) {
    case SDLK_UP: return RECOMP_RUNTIME_UI_INPUT_UP;
    case SDLK_DOWN: return RECOMP_RUNTIME_UI_INPUT_DOWN;
    case SDLK_LEFT: return RECOMP_RUNTIME_UI_INPUT_LEFT;
    case SDLK_RIGHT: return RECOMP_RUNTIME_UI_INPUT_RIGHT;
    case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: return RECOMP_RUNTIME_UI_INPUT_ACCEPT;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: return RECOMP_RUNTIME_UI_INPUT_BACK;
    default: return -1;
    }
}

}  // namespace

int sdl_event(const void *p) {
    if (!p) return 0;
    const SDL_Event &e = *static_cast<const SDL_Event *>(p);
    std::lock_guard<std::mutex> lk(lock());
    RecompRuntimeUi *ui = model();
    if (!ui) return 0;
    const bool open = recomp_runtime_ui_is_open(ui) != 0;
#if defined(LNG_SDL3)
    if (e.type == SDL_EVENT_KEY_DOWN) {
        const SDL_Keycode k = e.key.key;
        const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
        if (!e.key.repeat && (k == SDLK_F10 || (ctrl && k == SDLK_L))) {
            input(RECOMP_RUNTIME_UI_INPUT_TOGGLE);
            return 1;
        }
        if (!open) return 0;
        const int in = key_input((int)k);
        if (in >= 0) recomp_runtime_ui_handle_input(ui, (RecompRuntimeUiInput)in, 1, e.key.repeat ? 1 : 0);
        return 1;
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        const unsigned bit = 1u << e.gbutton.button;
        const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const unsigned before = g_pad_held;
        g_pad_held = down ? (g_pad_held | bit) : (g_pad_held & ~bit);
        if (down && (g_pad_held & (kBack | kL3)) == (kBack | kL3) && (before & (kBack | kL3)) != (kBack | kL3)) {
            input(RECOMP_RUNTIME_UI_INPUT_TOGGLE);
            return 1;
        }
        if (!open) return 0;
        if (down) {
            int in = -1;
            switch (e.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: in = RECOMP_RUNTIME_UI_INPUT_UP; break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: in = RECOMP_RUNTIME_UI_INPUT_DOWN; break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: in = RECOMP_RUNTIME_UI_INPUT_LEFT; break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: in = RECOMP_RUNTIME_UI_INPUT_RIGHT; break;
            case SDL_GAMEPAD_BUTTON_SOUTH: in = RECOMP_RUNTIME_UI_INPUT_ACCEPT; break;
            case SDL_GAMEPAD_BUTTON_EAST: in = RECOMP_RUNTIME_UI_INPUT_BACK; break;
            default: break;
            }
            if (in >= 0) recomp_runtime_ui_handle_input(ui, (RecompRuntimeUiInput)in, 1, 0);
        }
        return 1;
    }
    if (!open) return 0;
    switch (e.type) {  // the game sees no input while the menu is open
    case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
        return 1;
    default:
        return 0;
    }
#else
    (void)e; (void)open;
    return 0;  // SDL2 hosts: drive it with recomp_ui_psx_menu_input()
#endif
}

void draw(int w, int h) {
    std::lock_guard<std::mutex> lk(lock());
    RecompRuntimeUi *ui = model();
    if (!ui || !recomp_runtime_ui_is_open(ui) || w <= 0 || h <= 0) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    if (!g_ctx) {
        g_ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(g_ctx);
        ImGui::GetIO().IniFilename = nullptr;
    }
    ImGui::SetCurrentContext(g_ctx);
    if (!g_gl_ready) g_gl_ready = ImGui_ImplOpenGL3_Init(nullptr);
    if (!g_gl_ready) { ImGui::SetCurrentContext(prev); return; }
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    // Keyboard/controller driven: no mouse position, so hover never moves the
    // model's selection under the player.
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    io.DeltaTime = g_last_ns && now > g_last_ns && frequency
        ? (float)((double)(now - g_last_ns) / (double)frequency) : 1.0f / 60.0f;
    g_last_ns = now;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(ui);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(prev);
}

void release_gl() {
    std::lock_guard<std::mutex> lk(lock());
    if (!g_ctx) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_ctx);
    if (g_gl_ready) ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(g_ctx);
    ImGui::SetCurrentContext(prev == g_ctx ? nullptr : prev);
    g_ctx = nullptr;
    g_gl_ready = false;
    g_last_ns = 0;
}

}  // namespace rui_psx

namespace {
ImGuiContext *nameplate_context = nullptr;
bool nameplate_gl_ready = false;
void nameplate_frame(int w, int h) {
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}
void plate(ImDrawList *draw, const char *name, bool right, const int r[4]) {
    const float sx = r[2] / 512.0f, sy = r[3] / 240.0f;
    const float width = 102 * sx, height = 12 * sy, slant = 7 * sx;
    const float x = right ? r[0] + r[2] - 14 * sx - width : r[0] + 14 * sx;
    const float y = r[1] + 10 * sy;
    ImVec2 points[4] = {{x, y}, {x + width - slant, y},
                       {x + width, y + height}, {x + slant, y + height}};
    if (right) {
        points[0] = ImVec2(x + slant, y); points[1] = ImVec2(x + width, y);
        points[2] = ImVec2(x + width - slant, y + height); points[3] = ImVec2(x, y + height);
    }
    ImVec2 shadow[4];
    for (int i = 0; i < 4; ++i) shadow[i] = ImVec2(points[i].x, points[i].y + 1.2f * sy);
    draw->AddConvexPolyFilled(shadow, 4, IM_COL32(0, 0, 0, 125));
    draw->AddConvexPolyFilled(points, 4, IM_COL32(22, 25, 32, 237));
    draw->AddPolyline(points, 4, IM_COL32(159, 169, 182, 215), ImDrawFlags_Closed, std::max(.8f, .45f * sy));
    ImVec2 accent[4];
    if (!right) {
        accent[0] = points[0]; accent[1] = ImVec2(x + 5 * sx, y);
        accent[2] = ImVec2(x + slant + 5 * sx, y + height); accent[3] = points[3];
    } else {
        accent[0] = ImVec2(x + width - 5 * sx, y); accent[1] = points[1];
        accent[2] = points[2]; accent[3] = ImVec2(x + width - slant - 5 * sx, y + height);
    }
    draw->AddConvexPolyFilled(accent, 4, right ? IM_COL32(35, 112, 244, 255) : IM_COL32(232, 48, 57, 255));
    draw->AddLine(ImVec2(x + slant + 2 * sx, y + sy),
                  ImVec2(x + width - slant - 2 * sx, y + sy), IM_COL32(224, 233, 244, 45));
    ImFont *font = ImGui::GetFont();
    float size = 8.3f * sy;
    ImVec2 text = font->CalcTextSizeA(size, FLT_MAX, 0, name);
    const float available = width - (slant + 16 * sx);
    if (text.x > available) {
        size *= available / text.x;
        text = font->CalcTextSizeA(size, FLT_MAX, 0, name);
    }
    const ImVec2 at(right ? x + width - slant - 8 * sx - text.x : x + slant + 8 * sx,
                    y + (height - text.y) * .5f);
    draw->AddText(font, size, ImVec2(at.x, at.y + .5f * sy), IM_COL32(0, 0, 0, 220), name);
    draw->AddText(font, size, at, IM_COL32(246, 248, 252, 255), name);
}
}

extern "C" int recomp_ui_psx_nameplates_init(const char *assets_dir) {
    if (nameplate_context) return nameplate_gl_ready;
    ImGuiContext *previous = ImGui::GetCurrentContext();
    nameplate_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(nameplate_context);
    ImGui::GetIO().IniFilename = nullptr;
    const auto font = std::filesystem::path(assets_dir) / "fonts" / "LatoLatin-Bold.ttf";
    if (std::filesystem::is_regular_file(font))
        ImGui::GetIO().Fonts->AddFontFromFileTTF(font.string().c_str(), 32.0f);
    nameplate_gl_ready = ImGui_ImplOpenGL3_Init(nullptr);
    if (nameplate_gl_ready) {
        // Prewarm shaders/font texture before gameplay instead of at first HUD.
        nameplate_frame(16, 16);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    ImGui::SetCurrentContext(previous);
    return nameplate_gl_ready;
}
extern "C" void recomp_ui_psx_nameplates_draw(const PsxHostNameplates *labels,
                                              int w, int h, const int r[4]) {
    if (!nameplate_gl_ready || !labels || !labels->visible || w <= 0 || h <= 0 || r[2] <= 0 || r[3] <= 0) return;
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(nameplate_context);
    nameplate_frame(w, h);
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2((float)r[0], (float)r[1]), ImVec2((float)(r[0]+r[2]), (float)(r[1]+r[3])), true);
    plate(draw, labels->names[0], false, r);
    plate(draw, labels->names[1], true, r);
    draw->PopClipRect();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(previous);
}
extern "C" void recomp_ui_psx_nameplates_release(void) {
    if (!nameplate_context) return;
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(nameplate_context);
    if (nameplate_gl_ready) ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(nameplate_context);
    ImGui::SetCurrentContext(previous == nameplate_context ? nullptr : previous);
    nameplate_context = nullptr;
    nameplate_gl_ready = false;
}

#endif  // RUI_PSX_HAS_MENU_HOST
