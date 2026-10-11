// Minimal Dear ImGui front-end for map_password.h.
//
//     tm-map-password-reset [map]
//
// Open a map with the button, drag & drop, or the command line. Edits stay in
// memory until Save / Save as.
#include "portable-file-dialogs.h"  // before GLFW: pulls in <windows.h> on Windows
#ifdef _WIN32
#include <shellapi.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <optional>

#include "map_password.h"

namespace fs = std::filesystem;

static const ImVec4 GREEN{0.4f, 0.9f, 0.4f, 1.0f};
static const ImVec4 RED{1.0f, 0.45f, 0.45f, 1.0f};
static const ImVec4 GREY{0.65f, 0.65f, 0.65f, 1.0f};
static const std::vector<std::string> MAP_FILTER = {"Trackmania maps", "*.Gbx *.gbx", "All files", "*"};

struct App {
    std::optional<tmpw::MapGbx> map;
    std::string path;  // UTF-8
    bool dirty = false;
    std::string status = "Open or drop a .Map.Gbx";
    ImVec4 status_col = GREY;

    char check_pw[256] = {};
    char new_pw[256] = {};
    bool show_pw = false;
    std::string checked_pw;  // last check: input, hash it ran against, result
    tmpw::Hash checked_hash{};
    int checked = -1;

    std::unique_ptr<pfd::open_file> open_dlg, donor_dlg;
    std::unique_ptr<pfd::save_file> save_dlg;
    std::function<void()> pending;  // runs once unsaved changes are discarded
    bool quit = false;

    static std::string name(const std::string& p) { return fs::u8path(p).filename().u8string(); }
    void set_status(std::string s, ImVec4 c = GREY) { status = std::move(s); status_col = c; }

    void load(const std::string& p) {
        try {
            map.emplace(fs::u8path(p));
            path = p;
            dirty = false;
            checked = -1;
            set_status("Loaded " + name(p));
        } catch (const std::exception& e) {
            set_status("Failed to load " + name(p) + ": " + e.what(), RED);
        }
    }

    void save(const std::string& p) {
        try {
            map->save(fs::u8path(p));
            path = p;
            dirty = false;
            set_status("Saved " + name(p), GREEN);
        } catch (const std::exception& e) {
            set_status(std::string("Save failed: ") + e.what(), RED);
        }
    }

    void edit(const tmpw::Hash& h, const char* msg) {
        map->set_hashed_password(h);
        dirty = true;
        set_status(std::string(msg) + " (unsaved)");
    }

    void unless_unsaved(std::function<void()> action) {
        if (dirty) pending = std::move(action);
        else action();
    }

    void open_dialog() {
        open_dlg = std::make_unique<pfd::open_file>("Open map", path, MAP_FILTER);
    }

    void poll_dialogs() {
        if (open_dlg && open_dlg->ready(0)) {
            auto r = open_dlg->result();
            open_dlg.reset();
            if (!r.empty()) load(r[0]);
        }
        if (donor_dlg && donor_dlg->ready(0)) {
            auto r = donor_dlg->result();
            donor_dlg.reset();
            if (!r.empty()) {
                try {
                    tmpw::MapGbx donor(fs::u8path(r[0]));
                    edit(donor.hashed_password(), ("Copied password from " + name(r[0])).c_str());
                } catch (const std::exception& e) {
                    set_status("Failed to load " + name(r[0]) + ": " + e.what(), RED);
                }
            }
        }
        if (save_dlg && save_dlg->ready(0)) {
            auto r = save_dlg->result();
            save_dlg.reset();
            if (!r.empty()) save(r);
        }
    }

    void row(const char* label, const std::string& value, const ImVec4* col = nullptr) {
        ImGui::TextColored(GREY, "%s", label);
        ImGui::SameLine(ImGui::GetFontSize() * 6);
        if (col) ImGui::TextColored(*col, "%s", value.c_str());
        else ImGui::TextUnformatted(value.c_str());
    }

    void frame() {
        poll_dialogs();
        bool dialog_open = open_dlg || donor_dlg || save_dlg;
        const float field = ImGui::GetFontSize() * 14;

        ImGui::BeginDisabled(dialog_open);

        if (ImGui::Button("Open map...")) unless_unsaved([this] { open_dialog(); });
        ImGui::SameLine();
        if (map) ImGui::Text("%s%s", name(path).c_str(), dirty ? " *" : "");
        else ImGui::TextColored(GREY, "(or drag a map onto this window)");

        if (map) {
            tmpw::Hash stored = map->hashed_password();
            tmpw::Hash h = map->effective_password();
            bool locked = !tmpw::is_empty(h);
            bool crc_ok = map->crc_valid();
            char crc[32];
            std::snprintf(crc, sizeof crc, "%08x  (%s)", map->crc(), crc_ok ? "valid" : "INVALID");

            ImGui::SeparatorText("Map");
            row("UID", map->uid());
            row("Password", !crc_ok ? "locked, unrecoverable (CRC mismatch)" : locked ? "set" : "none",
                locked ? &RED : &GREEN);
            row("Hash", tmpw::to_hex(stored.data(), stored.size()));
            row("CRC", crc, crc_ok ? nullptr : &RED);
            if (!crc_ok) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(GREY, "The game discards a hash whose CRC doesn't match and locks the map "
                                         "with a password nobody knows. Remove password (or set one) to fix it.");
                ImGui::PopTextWrapPos();
            }

            ImGui::SeparatorText("Check password");
            ImGui::SetNextItemWidth(field);
            bool enter = ImGui::InputText("##check", check_pw, sizeof check_pw, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Check") || enter) {
                checked_pw = check_pw;
                checked_hash = h;
                checked = tmpw::check_password(check_pw, h);
            }
            if (checked >= 0 && checked_pw == check_pw && checked_hash == h) {
                ImGui::SameLine();
                ImGui::TextColored(checked ? GREEN : RED, checked ? "correct" : "wrong");
            }

            ImGui::SeparatorText("Change password");
            ImGui::SetNextItemWidth(field);
            ImGui::InputText("##new", new_pw, sizeof new_pw, show_pw ? 0 : ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            ImGui::Checkbox("Show", &show_pw);
            ImGui::BeginDisabled(!new_pw[0]);
            if (ImGui::Button("Set password")) edit(tmpw::hash_password(new_pw), "Password set");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!locked);
            if (ImGui::Button("Remove password")) edit(tmpw::Hash{}, "Password removed");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Copy from map..."))
                donor_dlg = std::make_unique<pfd::open_file>("Copy password from map", path, MAP_FILTER);

            ImGui::SeparatorText("Save");
            ImGui::BeginDisabled(!dirty);
            if (ImGui::Button("Save (overwrite)")) save(path);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Save as...")) save_dlg = std::make_unique<pfd::save_file>("Save map as", path, MAP_FILTER);
        }

        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(status_col, "%s", status.c_str());
        ImGui::PopTextWrapPos();

        if (pending) ImGui::OpenPopup("Unsaved changes");
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Discard unsaved changes to %s?", name(path).c_str());
            if (ImGui::Button("Discard")) {
                dirty = false;
                auto action = std::move(pending);
                pending = nullptr;
                action();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                pending = nullptr;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
};

static App g_app;
static std::string g_dropped;

static void draw(GLFWwindow* window) {
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    g_app.frame();
    ImGui::End();
    ImGui::Render();

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

int main(int argc, char** argv) {
    glfwSetErrorCallback([](int err, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", err, desc); });
    if (!glfwInit()) return 1;

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow((int)(540 * scale), (int)(380 * scale), "Trackmania Map Password Reset", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, [](GLFWwindow*, int n, const char** paths) {
        if (n > 0) g_dropped = paths[0];
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nothing worth persisting
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 16.0f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    // Windows (and macOS) block the event loop while the window is being resized and
    // ask for repaints instead; draw from there so the UI tracks the new size live
    glfwSetWindowRefreshCallback(window, draw);

#ifdef _WIN32
    (void)argc, (void)argv;  // argv is in the ANSI code page; take the UTF-16 command line instead
    int wargc;
    if (wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc)) {
        if (wargc > 1) g_app.load(fs::path(wargv[1]).u8string());
        LocalFree(wargv);
    }
#else
    if (argc > 1) g_app.load(argv[1]);
#endif

    int burst = 0;
    while (!g_app.quit) {
        // idle cheaply: sleep until input, then draw a few frames so ImGui settles
        if (--burst <= 0) {
            glfwWaitEventsTimeout(0.25);
            burst = 3;
        } else {
            glfwPollEvents();
        }
        if (glfwWindowShouldClose(window)) {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            g_app.unless_unsaved([] { g_app.quit = true; });
        }
        if (!g_dropped.empty()) {
            std::string p = std::move(g_dropped);
            g_dropped.clear();
            g_app.unless_unsaved([p] { g_app.load(p); });
        }
        draw(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
