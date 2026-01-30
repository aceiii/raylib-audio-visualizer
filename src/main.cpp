#include <print>
#include <cstdio>
#include <argparse/argparse.hpp>
#include <magic_enum/magic_enum.hpp>
#include <spdlog/spdlog.h>

#include "audiovisualizer.h"


static bool set_logging_level(const std::string &level_name) {
  auto level = magic_enum::enum_cast<spdlog::level::level_enum>(level_name);
  if (level.has_value()) {
    spdlog::set_level(level.value());
    return true;
  }
  return false;
}

template <>
struct std::formatter<argparse::ArgumentParser> {
public:
  constexpr auto parse(auto &ctx) {
    return ctx.begin();
  }

  auto format(const argparse::ArgumentParser &instr, auto &ctx) const {
    std::ostringstream out;
    out << instr;
    return std::ranges::copy(std::move(out).str(), ctx.out()).out;
  }
};

auto main(int argc, char *argv[]) -> int {
  spdlog::set_level(spdlog::level::info);

  argparse::ArgumentParser program("aceboy", "0.0.1");

  program.add_argument("--log-level")
      .help("Set the verbosity for logging")
      .default_value(std::string("info"))
      .nargs(1);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::println(stderr, "{}", err.what());
    std::println(stderr, "{}", program);
    return 1;
  }

  const std::string level = program.get("--log-level");
  if (!set_logging_level(level)) {
    std::println(stderr, "Invalid argument \"{}\" - allowed options: "
                        "{{trace, debug, info, warn, err, critical, off}}", level);
    std::println(stderr, "{}", program);
    return 1;
  }

  AudioVisualizer visualizer;
  visualizer.run();

  spdlog::info("Exiting.");

  return 0;
}
