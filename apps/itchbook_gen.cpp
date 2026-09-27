#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cli.hpp"
#include "itchbook/sim/feed_generator.hpp"

namespace {

constexpr std::string_view kUsage = R"(usage: itchbook-gen <output.itch> [options]

Writes a synthetic, internally consistent Nasdaq TotalView-ITCH 5.0 capture.

options:
  --events N        number of order events            (default 1000000)
  --instruments N   number of instruments             (default 8)
  --seed N          RNG seed for reproducible output  (default 42)
)";

int run(const itchbook::cli::Args& args) {
  const auto positional = args.positional();
  if (positional.size() != 1 || args.flag("--help")) {
    std::fputs(kUsage.data(), stderr);
    return 2;
  }
  const std::string path(positional[0]);

  itchbook::sim::FeedConfig config;
  config.events = args.option<std::size_t>("--events").value_or(config.events);
  config.instruments = args.option<std::size_t>("--instruments").value_or(config.instruments);
  config.seed = args.option<std::uint64_t>("--seed").value_or(config.seed);
  if (config.instruments == 0 || config.instruments >= itchbook::kMaxInstruments) {
    throw std::invalid_argument("--instruments must be in [1, 65535]");
  }

  const auto start = std::chrono::steady_clock::now();
  const auto feed = itchbook::sim::generate_feed(config);
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(feed.data()), static_cast<std::streamsize>(feed.size()));
  if (!out) {
    throw std::runtime_error("failed to write " + path);
  }
  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

  itchbook::cli::print("wrote {} ({:.1f} MiB, {} events, {} instruments) in {:.2f} s\n", path,
                       static_cast<double>(feed.size()) / (1024.0 * 1024.0), config.events,
                       config.instruments, elapsed.count());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(itchbook::cli::Args(argc, argv));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "itchbook-gen: %s\n", e.what());
    return 1;
  }
}
