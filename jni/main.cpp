#include <jni.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <cstdlib>
#include <cmath>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "dobby.h"

struct Vec3 { float x, y, z; };
struct Color { float r, g, b, a; };

// Переменные настроек Mod Menu
static bool showMenu = true;
static bool enableStretch = true;
static float aspectRatio = 1.333f;
static bool enableFov = false;
static float customFov = 85.0f;
static int targetFps = 120;

static float timeScale = 1.0f;
static float gravityY = -9.81f;
static bool customGravity = false;

static int weatherMode = 1; // 0-нет, 1-дождь, 2-снег, 3-искры
static int particleCount = 150;
static float particleSpeed = 14.0f;
static bool customFog = false;
static float fogColor = {0.06f, 0.07f, 0.12f, 1.0f};
static float fogDensity = 0.02f;
static float screenTint = {0.0f, 0.0f, 0.0f, 0.0f};
static bool showFps = true;
static bool showDot = false;

struct Particle { float x, y, l, s, phase; };
static std::vector<Particle> parts;

// Указатели на встроенные функции Unity (работают без дампа)
typedef void* (*il2cpp_resolve_icall_t)(const char*);
typedef void* (*Camera_get_main_t)();
typedef void (*Camera_set_aspect_t)(void*, float);
typedef void (*Camera_set_fov_t)(void*, float);
typedef void (*Time_set_timeScale_t)(float);
typedef void (*Physics_set_gravity_t)(Vec3*);
typedef void (*Application_set_targetFrameRate_t)(int);
typedef void (*RenderSettings_set_fog_t)(bool);
typedef void (*RenderSettings_set_fogColor_t)(Color*);
typedef void (*RenderSettings_set_fogDensity_t)(float);

static Camera_get_main_t get_main = nullptr;
static Camera_set_aspect_t set_aspect = nullptr;
static Camera_set_fov_t set_fov = nullptr;
static Time_set_timeScale_t set_timeScale = nullptr;
static Physics_set_gravity_t set_gravity = nullptr;
static Application_set_targetFrameRate_t set_fps = nullptr;
static RenderSettings_set_fog_t set_fog = nullptr;
static RenderSettings_set_fogColor_t set_fogColor = nullptr;
static RenderSettings_set_fogDensity_t set_fogDensity = nullptr;
EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    static bool init = false;
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);

    if (!init && w > 0 && h > 0) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 12.0f;
        style.FrameRounding = 8.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 0.94f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.04f, 0.52f, 1.0f, 0.8f);
        style.ScaleAllSizes(2.4f);
        io.FontGlobalScale = 2.4f;
        ImGui_ImplOpenGL3_Init("#version 100");

        for (int i = 0; i < 400; i++) {
            parts.push_back({(float)(rand() % 2600), (float)(rand() % 2600), 22.0f + rand() % 28, 0.7f + (rand() % 10) * 0.1f, (float)(rand() % 100)});
        }
        init = true;
    }

    // Применение игровых модов к движку Unity
    if (get_main) {
        void* cam = get_main();
        if (cam) {
            if (enableStretch && set_aspect) set_aspect(cam, aspectRatio);
            if (enableFov && set_fov) set_fov(cam, customFov);
        }
    }
    if (set_timeScale) set_timeScale(timeScale);
    if (set_fps) set_fps(targetFps);
    if (customGravity && set_gravity) {
        Vec3 g = {0.0f, gravityY, 0.0f};
        set_gravity(&g);
    }
    if (customFog && set_fog && set_fogColor && set_fogDensity) {
        set_fog(true);
        Color fc = {fogColor, fogColor, fogColor, fogColor};
        set_fogColor(&fc);
        set_fogDensity(fogDensity);
    }

    if (init) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImDrawList* bg = ImGui::GetBackgroundDrawList();

        // Цветофильтр экрана
        if (screenTint > 0.01f) {
            bg->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), ImGui::ColorConvertFloat4ToU32(ImVec4(screenTint, screenTint, screenTint, screenTint)));
        }

        // Отрисовка погоды (дождь / снег / искры)
        if (weatherMode > 0) {
            for (int i = 0; i < particleCount && i < parts.size(); i++) {
                auto& p = parts[i];
                if (weatherMode == 1) { // Дождь
                    p.y += particleSpeed * p.s; p.x -= 2.5f;
                    if (p.y > h) { p.y = -40; p.x = (float)(rand() % (w + 200)); }
                    bg->AddLine(ImVec2(p.x, p.y), ImVec2(p.x - 5, p.y + p.l), IM_COL32(170, 210, 255, 150), 2.0f);
                } else if (weatherMode == 2) { // Снег
                    p.y += particleSpeed * 0.3f * p.s; p.x += sinf(p.y * 0.015f + p.phase) * 1.5f;
                    if (p.y > h) { p.y = -20; p.x = (float)(rand() % w); }
                    bg->AddCircleFilled(ImVec2(p.x, p.y), 4.0f * p.s, IM_COL32(255, 255, 255, 200));
                } else if (weatherMode == 3) { // Искры
                    p.y -= particleSpeed * 0.4f * p.s; p.x += cosf(p.y * 0.02f + p.phase) * 2.0f;
                    if (p.y < -20) { p.y = h + 20; p.x = (float)(rand() % w); }
                    bg->AddCircleFilled(ImVec2(p.x, p.y), 3.5f * p.s, IM_COL32(255, 130, 30, 220));
                }
            }
        }

        if (showFps) {
            char buf;
            snprintf(buf, sizeof(buf), "NoomiMod | %.0f FPS", io.Framerate);
            bg->AddText(ImVec2(30, 30), IM_COL32(0, 255, 140, 255), buf);
        }
        if (showDot) {
            bg->AddCircleFilled(ImVec2(w * 0.5f, h * 0.5f), 5.0f, IM_COL32(255, 50, 80, 230));
        }

        // Окно Mod Menu
        ImGui::SetNextWindowSize(ImVec2(660, 580), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("NoomiClone Ultimate Client", &showMenu)) {
            if (ImGui::BeginTabBar("Tabs")) {
                if (ImGui::BeginTabItem("Экран и Камера")) {
                    ImGui::Checkbox("Растяг экрана (Stretch)", &enableStretch);
                    ImGui::SliderFloat("Соотношение", &aspectRatio, 0.75f, 2.3f, "%.2f");
                    if (ImGui::Button("4:3")) aspectRatio = 1.333f; ImGui::SameLine();
                    if (ImGui::Button("5:4")) aspectRatio = 1.25f; ImGui::SameLine();
                    if (ImGui::Button("16:10")) aspectRatio = 1.6f; ImGui::SameLine();
                    if (ImGui::Button("21:9")) aspectRatio = 2.333f; ImGui::SameLine();
                    if (ImGui::Button("16:9")) aspectRatio = 1.777f;

                    ImGui::Separator();
                    ImGui::Checkbox("Кастом FOV (Угол обзора)", &enableFov);
                    ImGui::SliderFloat("Значение FOV", &customFov, 40.0f, 140.0f, "%.0f");
                    ImGui::SliderInt("Лимит FPS", &targetFps, 30, 240);
                    ImGui::Checkbox("Счётчик FPS", &showFps); ImGui::SameLine();
                    ImGui::Checkbox("Точка по центру", &showDot);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Физика и Паркур")) {
                    ImGui::SliderFloat("Скорость времени (SlowMo)", &timeScale, 0.15f, 2.5f, "%.2fx");
                    if (ImGui::Button("Слоу-мо 0.4x")) timeScale = 0.4f; ImGui::SameLine();
                    if (ImGui::Button("Норма 1.0x")) timeScale = 1.0f; ImGui::SameLine();
                    if (ImGui::Button("Ускорение 1.6x")) timeScale = 1.6f;

                    ImGui::Separator();
                    ImGui::Checkbox("Своя гравитация", &customGravity);
                    ImGui::SliderFloat("Сила гравитации", &gravityY, -25.0f, -1.0f, "%.2f");
                    if (ImGui::Button("Луна (-3.0)")) { customGravity = true; gravityY = -3.0f; } ImGui::SameLine();
                    if (ImGui::Button("Земля (-9.81)")) { customGravity = true; gravityY = -9.81f; }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Погода и Фон")) {
                    const char* wItems[] = {"Ясно", "Дождь", "Снег", "Искры"};
                    ImGui::Combo("Погода", &weatherMode, wItems, 4);
                    ImGui::SliderInt("Кол-во частиц", &particleCount, 20, 400);
                    ImGui::SliderFloat("Скорость осадков", &particleSpeed, 4.0f, 35.0f);

                    ImGui::Separator();
                    ImGui::Checkbox("Заменить туман / атмосферу", &customFog);
                    ImGui::ColorEdit3("Цвет атмосферы", fogColor);
                    ImGui::SliderFloat("Плотность тумана", &fogDensity, 0.001f, 0.1f, "%.3f");
                    ImGui::ColorEdit4("Цветофильтр экрана", screenTint);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    return orig_eglSwapBuffers(dpy, surface);
}

void* hack_thread(void*) {
    while (!dlopen("libil2cpp.so", RTLD_NOLOAD)) sleep(1);
    void* h = dlopen("libil2cpp.so", RTLD_LAZY);
    auto res = (il2cpp_resolve_icall_t)dlsym(h, "il2cpp_resolve_icall");
    if (res) {
        get_main = (Camera_get_main_t)res("UnityEngine.Camera::get_main()");
        set_aspect = (Camera_set_aspect_t)res("UnityEngine.Camera::set_aspect(System.Single)");
        set_fov = (Camera_set_fov_t)res("UnityEngine.Camera::set_fieldOfView(System.Single)");
        set_timeScale = (Time_set_timeScale_t)res("UnityEngine.Time::set_timeScale(System.Single)");
        set_gravity = (Physics_set_gravity_t)res("UnityEngine.Physics::set_gravity_Injected(UnityEngine.Vector3&)");
        set_fps = (Application_set_targetFrameRate_t)res("UnityEngine.Application::set_targetFrameRate(System.Int32)");
        set_fog = (RenderSettings_set_fog_t)res("UnityEngine.RenderSettings::set_fog(System.Boolean)");
        set_fogColor = (RenderSettings_set_fogColor_t)res("UnityEngine.RenderSettings::set_fogColor_Injected(UnityEngine.Color&)");
        set_fogDensity = (RenderSettings_set_fogDensity_t)res("UnityEngine.RenderSettings::set_fogDensity(System.Single)");
    }
    void* egl = dlsym(dlopen("libEGL.so", RTLD_LAZY), "eglSwapBuffers");
    DobbyHook(egl, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
    return nullptr;
}

__attribute__((constructor)) void init() {
    pthread_t ptid;
    pthread_create(&ptid, nullptr, hack_thread, nullptr);
}
