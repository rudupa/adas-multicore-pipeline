#include "core/config_loader.h"
#include "pipeline/pipeline.h"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

double to_ms(adas::TimePoint t, adas::TimePoint origin) {
    return std::chrono::duration<double, std::milli>(t - origin).count();
}

ImVec4 glyph_color(char glyph) {
    switch (glyph) {
        case '#': return ImVec4(0.60f, 0.80f, 1.00f, 1.00f); // sensor
        case 'P': return ImVec4(0.45f, 0.95f, 0.45f, 1.00f); // preprocess
        case 'D': return ImVec4(1.00f, 0.80f, 0.20f, 1.00f); // detection
        case 'T': return ImVec4(1.00f, 0.50f, 0.30f, 1.00f); // tracking
        case '~': return ImVec4(0.70f, 0.70f, 1.00f, 1.00f); // throttle
        case 'X': return ImVec4(1.00f, 0.25f, 0.25f, 1.00f); // drop
        default:  return ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/pipeline_config.json";
    if (argc > 1) config_path = argv[1];

    adas::PipelineConfig cfg;
    try {
        cfg = adas::load_config(config_path);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Failed to load config: %s\n", ex.what());
        return 1;
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "ADAS Pipeline Viewer (ImGui + ImPlot)", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    adas::Pipeline pipeline(cfg);

    std::atomic<bool> sim_running{true};
    std::atomic<bool> sim_done{false};

    std::thread sim_thread([&]() {
        pipeline.start();
        std::this_thread::sleep_for(std::chrono::seconds(cfg.run_duration_seconds));
        pipeline.stop();
        sim_running = false;
        sim_done = true;
    });

    float view_window_ms = 1500.0f;
    bool auto_scroll = true;

    std::vector<double> throughput_t;
    std::vector<double> throughput_fps;
    auto hist_t0 = std::chrono::steady_clock::now();
    auto last_sample = hist_t0;
    size_t last_completed = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto snap = pipeline.visualizer().snapshot();

        double max_ms = 0.0;
        for (const auto& e : snap.events) {
            max_ms = std::max(max_ms, to_ms(e.end, snap.origin));
        }
        for (const auto& m : snap.markers) {
            max_ms = std::max(max_ms, to_ms(m.time, snap.origin));
        }
        if (max_ms < 1.0) max_ms = 1.0;

        std::set<uint64_t> completed_frames;
        size_t drops = 0;
        size_t throttles = 0;
        for (const auto& e : snap.events) {
            if (e.glyph == 'T') completed_frames.insert(e.frame_id);
            if (e.glyph == '~') ++throttles;
        }
        for (const auto& m : snap.markers) {
            if (m.glyph == 'X') ++drops;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_sample >= std::chrono::seconds(1)) {
            const double tsec = std::chrono::duration<double>(now - hist_t0).count();
            const size_t curr = completed_frames.size();
            const double dt = std::chrono::duration<double>(now - last_sample).count();
            const double fps = dt > 0.0 ? static_cast<double>(curr - last_completed) / dt : 0.0;
            throughput_t.push_back(tsec);
            throughput_fps.push_back(fps);
            last_completed = curr;
            last_sample = now;
        }

        ImGui::Begin("ADAS Controls");
        ImGui::Text("Simulation: %s", sim_running ? "Running" : "Stopped");
        ImGui::Text("Completed frames: %zu", completed_frames.size());
        ImGui::Text("Dropped frames: %zu", drops);
        ImGui::Text("Throttle events: %zu", throttles);
        ImGui::SliderFloat("View window (ms)", &view_window_ms, 200.0f, 5000.0f, "%.0f");
        ImGui::Checkbox("Auto-scroll", &auto_scroll);
        if (sim_done) {
            ImGui::Separator();
            ImGui::Text("Run complete. You can inspect the timeline and close the window.");
        }
        ImGui::End();

        ImGui::Begin("Throughput");
        if (ImPlot::BeginPlot("Completed FPS", ImVec2(-1, 220))) {
            ImPlot::SetupAxes("Time (s)", "Frames/s", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            if (!throughput_t.empty()) {
                ImPlot::PlotLine("fps", throughput_t.data(), throughput_fps.data(), static_cast<int>(throughput_t.size()));
            }
            ImPlot::EndPlot();
        }
        ImGui::End();

        ImGui::Begin("Pipeline Timeline");
        if (ImPlot::BeginPlot("Events", ImVec2(-1, -1))) {
            std::vector<double> y_ticks;
            std::vector<const char*> y_labels;
            std::map<std::string, int> lane_to_idx;
            for (int i = 0; i < static_cast<int>(snap.lane_order.size()); ++i) {
                lane_to_idx[snap.lane_order[i]] = i;
                y_ticks.push_back(static_cast<double>(i));
                y_labels.push_back(snap.lane_order[i].c_str());
            }

            const double x_end = max_ms;
            const double x_start = auto_scroll ? std::max(0.0, x_end - static_cast<double>(view_window_ms)) : 0.0;

            ImPlot::SetupAxes("Time (ms)", "Lane", ImPlotAxisFlags_None, ImPlotAxisFlags_NoGridLines);
            ImPlot::SetupAxisTicks(ImAxis_Y1, y_ticks.data(), static_cast<int>(y_ticks.size()), y_labels.data(), false);
            ImPlot::SetupAxisLimits(ImAxis_X1, x_start, x_start + static_cast<double>(view_window_ms), ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, static_cast<double>(std::max<size_t>(1, snap.lane_order.size())), ImGuiCond_Always);

            int idx = 0;
            for (const auto& e : snap.events) {
                auto it = lane_to_idx.find(e.lane);
                if (it == lane_to_idx.end()) continue;

                const double s = to_ms(e.start, snap.origin);
                const double t = to_ms(e.end, snap.origin);
                if (t < x_start || s > x_start + static_cast<double>(view_window_ms)) continue;

                const double y = static_cast<double>(it->second);
                double xs[2] = {s, t};
                double ys[2] = {y, y};

                ImPlot::SetNextLineStyle(glyph_color(e.glyph), 5.0f);
                std::string id = "evt##" + std::to_string(idx++);
                ImPlot::PlotLine(id.c_str(), xs, ys, 2);
            }

            idx = 0;
            for (const auto& m : snap.markers) {
                auto it = lane_to_idx.find(m.lane);
                if (it == lane_to_idx.end()) continue;
                const double x = to_ms(m.time, snap.origin);
                if (x < x_start || x > x_start + static_cast<double>(view_window_ms)) continue;
                const double y = static_cast<double>(it->second);
                double xs[1] = {x};
                double ys[1] = {y};
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, glyph_color(m.glyph), 1.0f, glyph_color(m.glyph));
                std::string id = "mk##" + std::to_string(idx++);
                ImPlot::PlotScatter(id.c_str(), xs, ys, 1);
            }

            ImPlot::EndPlot();
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (sim_thread.joinable()) sim_thread.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
