#ifndef COMMAND_LINE_HPP
#define COMMAND_LINE_HPP

#include <filesystem>

struct Config;

enum class CommandLineResult {
    Run,
    ExitSuccess,
    PrintModeList,
    Error,
};

CommandLineResult parseCommandLine(int argc, char **argv, Config &config);
std::filesystem::path findConfigPath(int argc, char **argv);

#endif