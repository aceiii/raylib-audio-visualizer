#include <array>
#include <algorithm>
#include <valarray>
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
const int kFFTSize = 128;

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

  std::valarray<float> frequencies(kFFTSize);
  for (int i = 0; i < frequencies.size(); i++) {
    frequencies[i] = ((5 + i * 2) % 24) / 32.0f;
  }

  spdlog::info("fft size {}", frequencies.size());

  while (!(WindowShouldClose() || should_close)) {
    Vector2 mouse = GetMousePosition();
    Vector2 mouse_delta = GetMouseDelta();

    int width = GetScreenWidth();
    int height = GetScreenHeight();
    float menu_height = 32;
    float panel_height = 64;
    float wavepanel_height = 128;
    float spectrum_height = height - panel_height - wavepanel_height; // - menu_height;

    BeginDrawing();
    ClearBackground({ 57, 58, 75, 255 });

    for (int i = 0; i < frequencies.size(); i++) {
      float f = frequencies[i];

      float w = width / (float)kFFTSize;
      int x = i * w;
      int h = f * spectrum_height;
      int y = spectrum_height - h;

      DrawRectangle(x, y, std::max(1.0f, w), h, RED);
    }

    Vector2 wavepanel_min { 0, height - panel_height - wavepanel_height };
    Vector2 wavepanel_max { (float)width, height - panel_height };

    DrawRectangle(wavepanel_min.x, wavepanel_min.y, width, wavepanel_height, BLACK);
    if (samples != nullptr) {
      int base_y = wavepanel_max.y - (wavepanel_height / 2);
      int frame_count = wave.frameCount;
      float scale_y = (wavepanel_height / 2) * 0.75;
      float frames_per_pixel = (float)frame_count / width;

      int dx = 1;
      for (int x = 0; x < width; x += dx) {
        int sample_index1 = (int)(frames_per_pixel * x) * wave.channels;
        int sample_index2 = (int)(frames_per_pixel * (x + dx)) * wave.channels;

        const auto [min, max] = std::minmax_element(&samples[sample_index1], &samples[sample_index2]);
        float min_sample = std::min(0.0f, *min) * scale_y;
        float max_sample = std::max(0.0f, *max) * scale_y;
        DrawRectangle(x, base_y - max_sample, dx, max_sample - min_sample, WHITE);
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
    ImGui::Begin("Audio", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::Button(ICON_FA_BACKWARD_FAST)) {
      spdlog::debug("Fast backward button pressed");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_BACKWARD_STEP)) {
      spdlog::debug("Step backward button pressed");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLAY)) {
      spdlog::debug("Play button pressed");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PAUSE)) {
      spdlog::debug("Pause button pressed");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP)) {
      spdlog::debug("Stop button pressed");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_STEP)) {
      spdlog::debug("Fast forward button pressed");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_FAST)) {
      spdlog::debug("Step forward button pressed");
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
