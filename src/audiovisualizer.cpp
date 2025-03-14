#include <array>
#include <algorithm>
#include <filesystem>
#include <string>
#include <valarray>
#include <vector>
#include <raylib.h>
#include <rlImGui.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nfd.h>
#include <spdlog/spdlog.h>
#include <kiss_fft.h>

#include "audiovisualizer.h"

struct PlaylistItem {
  std::filesystem::path path;
  std::string name;
  bool is_playing;
};

const int kWindowWidth = 800;
const int kWIndowHeight = 600;
const char* kWindowTitle = "Raylib Audio Visualizer";
const int kSamplesPerUpdate = 4096;
const int kFFTSize = 4096;
const int kBarWidth = 20;

std::string format_wave_timestamp(Wave &wave, int frame_index) {
  int total_seconds = frame_index / wave.sampleRate;
  int seconds = total_seconds % 60;
  int minutes = total_seconds / 60;

  return fmt::format("{:02}:{:02}", minutes, seconds);
}

void AudioVisualizer::run() {
  InitWindow(kWindowWidth, kWIndowHeight, kWindowTitle);
  InitAudioDevice();
  SetExitKey(KEY_ESCAPE);
  SetTargetFPS(60);
  SetAudioStreamBufferSizeDefault(kSamplesPerUpdate);
  rlImGuiSetup(true);

  kiss_fft_cfg cfg = kiss_fft_alloc(kFFTSize, 0, nullptr, nullptr);
  std::vector<kiss_fft_cpx> fft_input(kFFTSize);
  std::vector<kiss_fft_cpx> fft_output(kFFTSize);
  std::valarray<float> hanning(kFFTSize);

  bool auto_play = true;
  bool should_close = false;
  bool should_loop = true;
  bool show_about = false;
  bool show_demo = false;
  bool show_playlist = true;

  std::vector<PlaylistItem> playlist;

  Wave wave;
  AudioStream stream;
  float* samples = nullptr;
  int wave_index = 0;
  std::string total_timestamp = "--:--";

  const int num_bars = kWindowWidth / kBarWidth;
  const int freqs_per_bar = kFFTSize / num_bars / 2;

  std::valarray<float> frequencies(num_bars);
  std::valarray<float> max_frequencies(num_bars);
  std::valarray<float> fall_velocity(num_bars);

  for (int i = 0; i < frequencies.size(); i++) {
    frequencies[i] = 0;
    max_frequencies[i] = 0;
    fall_velocity[i] = 0;
  }

  for (int i = 0; i < hanning.size(); i++) {
    hanning[i] = 0.5 * (1 - std::cos((2 * M_PI * i) / (hanning.size() - 1)));
  }

  float menu_height = 32;
  float panel_height = 64;
  float wavepanel_height = 128;

  RenderTexture2D waveform_texture = LoadRenderTexture(kWindowWidth, wavepanel_height);
  BeginTextureMode(waveform_texture);
  ClearBackground(BLACK);

  int half_wavepanel_height = wavepanel_height / 2;
  DrawLine(0, half_wavepanel_height, kWindowWidth, half_wavepanel_height, DARKGRAY);
  DrawLine(0, half_wavepanel_height - 8, kWindowWidth, half_wavepanel_height - 8, DARKGRAY);
  DrawLine(0, half_wavepanel_height - 24, kWindowWidth, half_wavepanel_height - 24, DARKGRAY);
  DrawLine(0, half_wavepanel_height - 48, kWindowWidth, half_wavepanel_height - 48, DARKGRAY);
  DrawLine(0, half_wavepanel_height + 8, kWindowWidth, half_wavepanel_height + 8, DARKGRAY);
  DrawLine(0, half_wavepanel_height + 24, kWindowWidth, half_wavepanel_height + 24, DARKGRAY);
  DrawLine(0, half_wavepanel_height + 48, kWindowWidth, half_wavepanel_height + 48, DARKGRAY);

  for (int i = 0; i < kWindowWidth; i += 40) {
    DrawLine(i, 0, i, wavepanel_height, DARKGRAY);
  }
  EndTextureMode();

  auto unload_wave = [&]() {
    if (!samples) {
      return;
    }

    spdlog::info("Unloading previous file.");
    StopAudioStream(stream);
    UnloadWaveSamples(samples);
    samples = nullptr;
    UnloadAudioStream(stream);
    UnloadWave(wave);
    wave_index = 0;
    total_timestamp = "--:--";
  };

  auto load_wave = [&](const std::filesystem::path wav_path) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();

    spdlog::info("Audio file loaded: {}", wav_path.string());
    wave = LoadWave(wav_path.c_str());

    if (wave.sampleSize != 32) {
      WaveFormat(&wave, wave.sampleRate, 32, wave.channels);
    }

    samples = LoadWaveSamples(wave);

    spdlog::info("Generating waveform texture");
    BeginTextureMode(waveform_texture);
    {
      ClearBackground(BLACK);

      int half_wavepanel_height = wavepanel_height / 2;
      DrawLine(0, half_wavepanel_height, kWindowWidth, half_wavepanel_height, DARKGRAY);
      DrawLine(0, half_wavepanel_height - 8, kWindowWidth, half_wavepanel_height - 8, DARKGRAY);
      DrawLine(0, half_wavepanel_height - 24, kWindowWidth, half_wavepanel_height - 24, DARKGRAY);
      DrawLine(0, half_wavepanel_height - 48, kWindowWidth, half_wavepanel_height - 48, DARKGRAY);
      DrawLine(0, half_wavepanel_height + 8, kWindowWidth, half_wavepanel_height + 8, DARKGRAY);
      DrawLine(0, half_wavepanel_height + 24, kWindowWidth, half_wavepanel_height + 24, DARKGRAY);
      DrawLine(0, half_wavepanel_height + 48, kWindowWidth, half_wavepanel_height + 48, DARKGRAY);

      for (int i = 0; i < kWindowWidth; i += 40) {
        DrawLine(i, 0, i, wavepanel_height, DARKGRAY);
      }

      int base_y = (wavepanel_height / 2);
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
    }
    EndTextureMode();

    spdlog::info("wave sampleRate:{}, sampleSize:{}, channels:{}", wave.sampleRate, wave.sampleSize, wave.channels);
    stream = LoadAudioStream(wave.sampleRate, wave.sampleSize, wave.channels);
    wave_index = 0;
    total_timestamp = format_wave_timestamp(wave, wave.frameCount);

    if (auto_play) {
      PlayAudioStream(stream);
    }
  };

  auto push_disabled_btn_flags = []() {
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
  };

  auto pop_disabled_btn_flags = []() {
    ImGui::PopItemFlag();
    ImGui::PopStyleColor();
  };

  while (!(WindowShouldClose() || should_close)) {
    Vector2 mouse = GetMousePosition();
    Vector2 mouse_delta = GetMouseDelta();

    int width = GetScreenWidth();
    int height = GetScreenHeight();
    float spectrum_height = height - panel_height - wavepanel_height; // - menu_height;

    BeginDrawing();
    ClearBackground({ 57, 58, 75, 255 });

    if (samples) {
      for (int i = 0; i < kWindowWidth / 2; i+= 1) {
        int idx = wave_index + i;
        if (idx + 1 >= wave.frameCount) {
          break;
        }

        const float scale = (spectrum_height / 2) * 0.86;
        float s1 = samples[idx * wave.channels];
        float s2 = samples[(idx + 1) * wave.channels];
        int mid_y = spectrum_height / 2;


        DrawLine(i * 2, mid_y + (s1 * scale), (i + 1) * 2, mid_y + (s2 * scale), RAYWHITE);
      }
    }

    DrawRectangle(0, spectrum_height - 2, width, 2, RED);

    for (int i = 0; i < frequencies.size(); i++) {
      float f = frequencies[i];
      int w = kBarWidth;
      int x = i * w;
      int h = f * spectrum_height;
      int y = spectrum_height - h;

      //DrawRectangleGradientV(x, y, w, h, ORANGE, RED);

      Color top_colour = ORANGE;
      Color bottom_colour = RED;

      if (f < 0.3f) {
        top_colour = MAROON;
        bottom_colour = SKYBLUE;
      }

      DrawRectangleGradientV(x, y, w, h, ColorLerp(top_colour, bottom_colour, f), bottom_colour);
    }

    for (int i = 0; i < max_frequencies.size(); i++) {
      fall_velocity[i] += GetFrameTime() * 2;
      float f = std::max(0.0f, std::max(max_frequencies[i] - (GetFrameTime() * fall_velocity[i]), frequencies[i]));
      if (f >= max_frequencies[i]) {
        fall_velocity[i] = 0;
      }
      max_frequencies[i] = f;

      int h = 3;
      int y = spectrum_height - (f * spectrum_height);
      DrawRectangle(i * kBarWidth, y, kBarWidth, h, GOLD);
    }

    Vector2 wavepanel_min { 0, height - panel_height - wavepanel_height };
    Vector2 wavepanel_max { (float)width, height - panel_height };

    DrawTexture(waveform_texture.texture, 0, wavepanel_min.y, WHITE);
    if (samples) {
      int frame_count = wave.frameCount;
      float pct = (float)wave_index / frame_count;
      int bar_x = width * pct;

      DrawRectangle(bar_x, height - panel_height - wavepanel_height, 1, 2 * panel_height, RED);

      Vector2 v1 { (float)bar_x - 4, (float)height - panel_height - wavepanel_height };
      Vector2 v2 { (float)bar_x, (float)height - panel_height - wavepanel_height + 8};
      Vector2 v3 { (float)bar_x + 4, (float)height - panel_height - wavepanel_height};
      Vector2 v4 { (float)bar_x - 4, (float)height - panel_height };
      Vector2 v5 { (float)bar_x + 4, (float)height - panel_height };
      Vector2 v6 { (float)bar_x, (float)height - panel_height - 8 };
      DrawTriangle(v1, v2, v3, RED);
      DrawTriangle(v4, v5, v6, RED);

      if (mouse.y >= wavepanel_min.y && mouse.y < wavepanel_max.y) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && (mouse_delta.x || mouse_delta.y))) {
          float pct = std::clamp((float)mouse.x / width, 0.f, 1.f);
          wave_index = pct * frame_count;
        }
      }
    }

    rlImGuiBegin();
    {
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
              unload_wave();

              std::filesystem::path path{wav_path};
              playlist.push_back({path, path.stem().string(), true});

              load_wave(path);
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

            for (int i = 0; i < frequencies.size(); i++) {
              frequencies[i] = 0;
            }

            BeginTextureMode(waveform_texture);
            ClearBackground(BLACK);
            int half_wavepanel_height = wavepanel_height / 2;
            DrawLine(0, half_wavepanel_height, kWindowWidth, half_wavepanel_height, DARKGRAY);
            DrawLine(0, half_wavepanel_height - 8, kWindowWidth, half_wavepanel_height - 8, DARKGRAY);
            DrawLine(0, half_wavepanel_height - 24, kWindowWidth, half_wavepanel_height - 24, DARKGRAY);
            DrawLine(0, half_wavepanel_height - 48, kWindowWidth, half_wavepanel_height - 48, DARKGRAY);
            DrawLine(0, half_wavepanel_height + 8, kWindowWidth, half_wavepanel_height + 8, DARKGRAY);
            DrawLine(0, half_wavepanel_height + 24, kWindowWidth, half_wavepanel_height + 24, DARKGRAY);
            DrawLine(0, half_wavepanel_height + 48, kWindowWidth, half_wavepanel_height + 48, DARKGRAY);

            for (int i = 0; i < kWindowWidth; i += 40) {
              DrawLine(i, 0, i, wavepanel_height, DARKGRAY);
            }
            EndTextureMode();
          }
          ImGui::Separator();
          if (ImGui::MenuItem("Quit")) {
            should_close = true;
          }
          ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Audio")) {
          ImGui::MenuItem("Show Playlist", nullptr, &show_playlist);
          ImGui::MenuItem("Audo-Play", nullptr, &auto_play);
          ImGui::Separator();
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

        if (ImGui::BeginMenu("Help")) {
          ImGui::MenuItem("About", nullptr, &show_about);
          ImGui::Separator();
          ImGui::MenuItem("Demo", nullptr, &show_demo);
          ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
      }


      const bool is_playing = IsAudioStreamPlaying(stream);

      ImGuiStyle &style = ImGui::GetStyle();

      ImGui::SetNextWindowSize({ (float)width, panel_height });
      ImGui::SetNextWindowPos({ 0, (float)height - panel_height }, ImGuiCond_Always);
      ImGui::Begin("Audio", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

      ImVec2 frame_padding(16.0f, 12.0f);

      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, frame_padding);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

      if (!samples) {
        push_disabled_btn_flags();
      }

      if (ImGui::Button(ICON_FA_BACKWARD_FAST)) {
        spdlog::debug("Fast backward button pressed");
        wave_index = std::clamp(wave_index - ((int)wave.sampleRate * 30), 0, (int)wave.frameCount);
      }
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_BACKWARD_STEP)) {
        spdlog::debug("Step backward button pressed");
        wave_index = std::clamp(wave_index - ((int)wave.sampleRate * 10), 0, (int)wave.frameCount);
      }

      ImGui::SameLine();
      if (is_playing) {
        push_disabled_btn_flags();
      }

      if (ImGui::Button(ICON_FA_PLAY)) {
        spdlog::debug("Play button pressed");
        PlayAudioStream(stream);
      }

      if (is_playing) {
        pop_disabled_btn_flags();
      }

      ImGui::SameLine();
      if (!is_playing) {
        push_disabled_btn_flags();
      }

      if (ImGui::Button(ICON_FA_PAUSE)) {
        spdlog::debug("Pause button pressed");
        StopAudioStream(stream);
      }

      if (!is_playing) {
        pop_disabled_btn_flags();
      }

      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_STOP)) {
        spdlog::debug("Stop button pressed");
        StopAudioStream(stream);
        wave_index = 0;
      }

      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_FORWARD_STEP)) {
        spdlog::debug("Fast forward button pressed");
        wave_index = std::clamp(wave_index + ((int)wave.sampleRate * 10), 0, (int)wave.frameCount);
      }
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_FORWARD_FAST)) {
        spdlog::debug("Step forward button pressed");
        wave_index = std::clamp(wave_index + ((int)wave.sampleRate * 30), 0, (int)wave.frameCount);
      }

      if (!samples) {
        pop_disabled_btn_flags();
      }

      ImGui::PopStyleVar();
      ImGui::PopStyleVar();

      ImGui::SameLine();

      std::string current_timestamp;
      if (samples) {
        current_timestamp = format_wave_timestamp(wave, wave_index);
      } else {
        current_timestamp = "--:--";
      }

      ImGui::Text(fmt::format("{} / {}", current_timestamp, total_timestamp).c_str());

      if (show_about) {
        if (ImGui::Begin("About Audio Visualizer", &show_about)) {
          ImGui::Text("This application was created for fun and educational purposes.");
          ImGui::Text("Developed by AceIII");
          ImGui::SameLine();
          ImGui::TextLinkOpenURL("https://github.com/aceiii");
          ImGui::NewLine();
          ImGui::Text("Uses the following libraries:");
          ImGui::BulletText("Raylib");
          ImGui::BulletText("Dear ImGui");
          ImGui::BulletText("KissFFT");
          ImGui::BulletText("rlImGui");
          ImGui::BulletText("ArgParse");
          ImGui::BulletText("SpdLog");
          ImGui::BulletText("Magic Enum");
          ImGui::BulletText("NativeFileDialog-extended");
          ImGui::BulletText("toml++");
        }
        ImGui::End();
      }

      if (show_playlist) {
        if (ImGui::Begin("Playlist", &show_playlist)) {
          if (ImGui::Button("Add")) {
            nfdchar_t* wav_path = nullptr;
            std::array<nfdfilteritem_t, 2> filter_items = {{
              {"Wave", "wav"},
              {"MP3", "mp3"}
            }};
            nfdresult_t result = NFD_OpenDialog(&wav_path, filter_items.data(), filter_items.size(), nullptr);
            if (result == NFD_OKAY) {
              std::filesystem::path path{wav_path};
              playlist.push_back({path, path.stem().string()});
            }
          }
          ImGui::SameLine();
          if (ImGui::Button("Clear")) {
          }

          ImGui::BeginChild("#Inner");
          for (int i = 0; i < playlist.size(); i += 1) {
            auto &item = playlist[i];

            ImGui::PushID(i);
            ImGui::Text(item.name.c_str());
            ImGui::SameLine();

            bool pushed = false;
            if (item.is_playing) {
              push_disabled_btn_flags();
              pushed = true;
            }

            if (ImGui::SmallButton(ICON_FA_PLAY)) {
              unload_wave();
              load_wave(item.path);
              item.is_playing = true;

              for (int j = 0; j < playlist.size(); j += 1) {
                if (i == j) {
                  continue;
                }
                playlist[j].is_playing = false;
              }
            }

            if (pushed) {
              pop_disabled_btn_flags();
            }

            ImGui::PopID();
          }
          ImGui::EndChild();
        }
        ImGui::End();
      }

      if (show_demo) {
        ImGui::ShowDemoWindow(&show_demo);
      }

      ImGui::End();
    }
    rlImGuiEnd();

    DrawFPS(width - 100, height - 24);

    EndDrawing();

    if (samples) {
      int samples_left = kSamplesPerUpdate;
      int freq_wave_index = wave_index;
      if (IsAudioStreamPlaying(stream) && IsAudioStreamProcessed(stream)) {
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

      for (int i = 0; i < kFFTSize; i++) {
        fft_input[i].r = samples[freq_wave_index * wave.channels] * hanning[i];
        fft_input[i].i = 0.0f;
        freq_wave_index = (freq_wave_index + 1) % wave.frameCount;
      }

      kiss_fft(cfg, fft_input.data(), fft_output.data());

      for (int i = 0; i < frequencies.size(); i++) {
        frequencies[i] = 0;
      }

      float max_magnitude = 0;
      for (int i = 0; i < fft_output.size(); i++) {
        auto& out = fft_output[i];
        out.r = std::sqrt(out.r * out.r + out.i * out.i);
        max_magnitude = std::max(max_magnitude, out.r);
      }

      for (int i = 0; i < frequencies.size(); i++) {
        for (int j = 0; j < freqs_per_bar; j++) {
          const float scale_magnitude = 16.f;
          float magnitude = fft_output[(i * freqs_per_bar) + j].r;
          float f = std::clamp(std::log(1 + magnitude * scale_magnitude) / std::log(1 + max_magnitude * scale_magnitude), 0.f, 1.f);
          frequencies[i] += f;
        }
      }

      for (int i = 0; i < frequencies.size(); i++) {
        frequencies[i] /= (float)freqs_per_bar;
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

  kiss_fft_free(cfg);

  UnloadRenderTexture(waveform_texture);

  rlImGuiShutdown();
  CloseAudioDevice();
  CloseWindow();
}
