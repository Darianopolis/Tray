#include <SDL3/SDL.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

template<typename Fn>
struct DeferGuard
{
    Fn fn;

    DeferGuard(Fn&& fn): fn(std::move(fn)) {}
    ~DeferGuard() { fn(); };
};

#define defer DeferGuard _ = [&]

static struct  {
    // TODO: State here
} state;

static void frame()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    defer {
        ImGui::End();
        ImGui::PopStyleVar();
    };
    bool dont_close = true;
    if (!ImGui::Begin("Tray", &dont_close, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize)) return;

    // TODO: Frame logic here

    ImGui::Button("Hello, World!");
}

int main()
{
    SDL_SetHint("SDL_VIDEO_DRIVER", "wayland");

    SDL_Init(SDL_INIT_VIDEO);

    auto window = SDL_CreateWindow("Launcher", 800, 600, SDL_WINDOW_RESIZABLE);
    auto renderer = SDL_CreateRenderer(window, nullptr);

    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // TODO: Init

    SDL_Event event;
    for (;;) {
        bool first = true;
        while (first ? SDL_WaitEvent(&event) : SDL_PollEvent(&event)) {
            first = false;
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
                break;case SDL_EVENT_QUIT:
                      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                      case SDL_EVENT_WINDOW_FOCUS_LOST:
                    goto CLOSE;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();

        frame();

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }
CLOSE:
}
