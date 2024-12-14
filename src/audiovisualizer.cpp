
#include <array>
#include <algorithm>
#include <raylib.h>
#include <rlImGui.h>
#include <imgui.h>
#include <nfd.h>
#include <spdlog/spdlog.h>

#include "audiovisualizer.h"

const int kWindowWidth = 800;
const int kWIndowHeight = 600;
const char* kWindowTitle = "Raylib Audio Visualizer";
const int kSamplesPerUpdate = 4096;

// void callback(void *bufferData, unsigned int frames) {
//   spdlog::info("callback", frames);
// }

void AudioVisualizer::run() {
  InitWindow(kWindowWidth, kWIndowHeight, kWindowTitle);
  InitAudioDevice();
  SetExitKey(KEY_ESCAPE);
  SetTargetFPS(60);
  SetAudioStreamBufferSizeDefault(kSamplesPerUpdate);
  rlImGuiSetup(true);

  static bool should_close = false;
  static bool should_loop = true;
  static Wave wave;
  static AudioStream stream;
  static float* samples = nullptr;
  static int wave_index = 0;

  while (!(WindowShouldClose() || should_close)) {
    Vector2 mouse = GetMousePosition();
    Vector2 mouse_delta = GetMouseDelta();

    int width = GetScreenWidth();
    int height = GetScreenHeight();
    float panel_height = 240;
    float wavepanel_height = 128;

    BeginDrawing();
    ClearBackground({ 57, 58, 75, 255 });

    Vector2 wavepanel_min { 0, height - panel_height - wavepanel_height };
    Vector2 wavepanel_max { (float)width, height - panel_height };

    DrawRectangle(wavepanel_min.x, wavepanel_min.y, width, wavepanel_height, BLACK);
    if (samples != nullptr) {
      int base_y = wavepanel_max.y - (wavepanel_height / 2);
      int frame_count = wave.frameCount;
      float scale_y = (wavepanel_height / 2) * 0.98;
      float frames_per_pixel = (float)frame_count / width;

      int dx = 2;
      for (int x = 0; x < width - 1; x += dx) {
        float sample1 = samples[(int)(frames_per_pixel * x) * wave.channels] * scale_y;
        float sample2 = samples[(int)(std::min((int)(frames_per_pixel * (x + dx)), (int)wave.frameCount - 1) * wave.channels)] * scale_y;
        DrawLine(x, base_y + sample1, x + dx, base_y + sample2, RAYWHITE);
      }

      float pct = (float)wave_index / frame_count;
      int bar_x = width * pct;
      DrawLine(bar_x, height - panel_height - wavepanel_height, bar_x, height - panel_height, RED);

      if (mouse.y >= wavepanel_min.y && mouse.y < wavepanel_max.y) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && (mouse_delta.x || mouse_delta.y))) {
          float pct = (float)mouse.x / width;
          wave_index = pct * frame_count;
        }
      }
    }

    rlImGuiBegin();
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Load Audio File")) {
          nfdchar_t* wav_path = nullptr;
          std::array<nfdfilteritem_t, 2> filter_items = {{
            {"Wave", "wav"},
            {"MP3", "mp3"}
          }};
          nfdresult_t result = NFD_OpenDialog(&wav_path, filter_items.data(), filter_items.size(), nullptr);
          if (result == NFD_OKAY) {
            if (samples) {
              spdlog::info("Unloading previous file.");
              StopAudioStream(stream);
              UnloadWaveSamples(samples);
              samples = nullptr;
              UnloadAudioStream(stream);
              UnloadWave(wave);
              wave_index = 0;
            }


            spdlog::info("Audio file loaded: {}", wav_path);
            wave = LoadWave(wav_path);
            samples = LoadWaveSamples(wave);
            stream = LoadAudioStream(wave.sampleRate, wave.sampleSize, wave.channels);
            // SetAudioStreamCallback(stream, callback);
            wave_index = 0;
            PlayAudioStream(stream);
          } else if (NFD_CANCEL) {
            spdlog::info("Load cancelled by user.");
          } else {
            spdlog::error("Loading failed: {}", NFD_GetError());
          }
        }
        if (ImGui::MenuItem("Unload Audio File", nullptr, false, samples != nullptr)) {
          spdlog::info("Unloading wave file");
          StopAudioStream(stream);
          UnloadWave(wave);
          UnloadAudioStream(stream);
          UnloadWaveSamples(samples);
          samples = nullptr;
          wave_index = 0;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
          should_close = true;
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Audio")) {
        ImGui::MenuItem("Loop", nullptr, &should_loop);
        if (ImGui::MenuItem("Play", nullptr, false, samples != nullptr && !IsAudioStreamPlaying(stream))) {
          PlayAudioStream(stream);
        }
        if (ImGui::MenuItem("Pause",  nullptr, false, samples != nullptr && IsAudioStreamPlaying(stream))) {
          StopAudioStream(stream);
        }
        if (ImGui::MenuItem("Stop",  nullptr, false, samples != nullptr && IsAudioStreamPlaying(stream))) {
          StopAudioStream(stream);
          wave_index = 0;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("-30s", nullptr, false, samples != nullptr)) {
          wave_index = std::clamp(wave_index - ((int)wave.sampleRate * 30), 0, (int)wave.frameCount);
        }
        if (ImGui::MenuItem("-10s", nullptr, false, samples != nullptr)) {
          wave_index = std::clamp(wave_index - ((int)wave.sampleRate * 10), 0, (int)wave.frameCount);
        }
        if (ImGui::MenuItem("+10s", nullptr, false, samples != nullptr)) {
          wave_index = std::clamp(wave_index + ((int)wave.sampleRate * 10), 0, (int)wave.frameCount);
        }
        if (ImGui::MenuItem("+30s", nullptr, false, samples != nullptr)) {
          wave_index = std::clamp(wave_index + ((int)wave.sampleRate * 30), 0, (int)wave.frameCount);
        }

        ImGui::EndMenu();
      }

      ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowSize({ (float)width, panel_height });
    ImGui::SetNextWindowPos({ 0, (float)height - panel_height }, ImGuiCond_Always);
    ImGui::Begin("Audio", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if (samples) {
      ImGui::PlotLines("Wave", samples, wave.frameCount * wave.channels, 0, nullptr, -1.0f, 1.0f);
    }
    ImGui::End();

    rlImGuiEnd();
    EndDrawing();

    if (samples && IsAudioStreamProcessed(stream)) {
      int samples_left = kSamplesPerUpdate;
      while (samples_left) {
        int samples_to_write = std::min((int)wave.frameCount, wave_index + samples_left) - wave_index;
        UpdateAudioStream(stream, &samples[wave_index * wave.channels], samples_to_write);
        wave_index += samples_to_write;
        samples_left -= samples_to_write;
        if (wave_index >= wave.frameCount) {
          wave_index = 0;
          if (!should_loop) {
            StopAudioStream(stream);
          }
        }
      }
    }
  }

  if (samples) {
    StopAudioStream(stream);
    UnloadWave(wave);
    UnloadAudioStream(stream);
    UnloadWaveSamples(samples);
    samples = nullptr;
  }

  rlImGuiShutdown();
  CloseAudioDevice();
  CloseWindow();
}
