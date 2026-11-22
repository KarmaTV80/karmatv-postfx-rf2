#include "common.hpp"
#include "core/log.hpp"

namespace ktv::log
{
namespace
{
void write(reshade::log::level level, const std::string &message)
{
    const std::string text = "[KarmaTV] " + message;
    reshade::log::message(level, text.c_str());
}
}

void info(const std::string &message) { write(reshade::log::level::info, message); }
void warning(const std::string &message) { write(reshade::log::level::warning, message); }
void error(const std::string &message) { write(reshade::log::level::error, message); }
}
