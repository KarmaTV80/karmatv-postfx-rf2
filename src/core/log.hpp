#pragma once

#include <string>

// Messaggi nel log di ReShade (ReShade.log nella cartella Bin64 di rF2), con prefisso [KarmaTV].
namespace ktv::log
{
void info(const std::string &message);
void warning(const std::string &message);
void error(const std::string &message);
}
