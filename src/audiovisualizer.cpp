
#include <array>
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
  static Wave wave;
  static AudioStream stream;
  static float* samples = nullptr;
  static int wave_index = 0;

  while (!(WindowShouldClose() || should_close)) {
    BeginDrawing();
    ClearBackground({ 57, 58, 75, 255 });

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
      ImGui::EndMainMenuBar();
    }

    int width = GetScreenWidth();
    int height = GetScreenHeight();
    float panel_height = 240;

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
      UpdateAudioStream(stream, &samples[wave_index], kSamplesPerUpdate);
      wave_index += kSamplesPerUpdate * wave.channels;
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
