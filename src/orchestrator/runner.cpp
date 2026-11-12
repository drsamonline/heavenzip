// runner.cpp — fork/exec with both streams to files.
#include "heavenzip/runner.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#include <cstdlib>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace heavenzip {
namespace {

std::string tail_of(const fs::path& path, std::size_t lines) {
  std::ifstream in(path);
  if (!in) return {};
  std::vector<std::string> kept;
  std::string line;
  while (std::getline(in, line)) {
    kept.push_back(line);
    if (kept.size() > lines) kept.erase(kept.begin());
  }
  std::string out;
  for (const std::string& item : kept) {
    if (!out.empty()) out += '\n';
    out += item;
  }
  return out;
}

#ifdef _WIN32
std::string quote(const std::string& token) { return "\"" + token + "\""; }
#endif

}  // namespace

RunResult run_capture(const std::vector<std::string>& argv, const fs::path& stdout_path,
                      const fs::path& stderr_path) {
  if (argv.empty()) throw std::runtime_error("empty command");
  RunResult result;
  result.stdout_path = stdout_path;
  result.stderr_path = stderr_path;

  const auto started = std::chrono::steady_clock::now();

#ifdef _WIN32
  // Windows goes through the shell: CreateProcess with inherited handles is more
  // code than this phase needs, and every command here is a plain executable with
  // arguments we built ourselves. Quoted, so a path with spaces survives.
  std::string line;
  for (const std::string& token : argv) {
    if (!line.empty()) line += ' ';
    line += quote(token);
  }
  line += " > " + quote(stdout_path.string()) + " 2> " + quote(stderr_path.string());
  const int status = std::system(line.c_str());
  result.exit_code = status;
#else
  const pid_t pid = fork();
  if (pid < 0) throw std::runtime_error("fork failed");
  if (pid == 0) {
    const int out = ::open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const int err = ::open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out >= 0) { dup2(out, STDOUT_FILENO); ::close(out); }
    if (err >= 0) { dup2(err, STDERR_FILENO); ::close(err); }

    std::vector<char*> pointers;
    pointers.reserve(argv.size() + 1);
    for (const std::string& token : argv) pointers.push_back(const_cast<char*>(token.c_str()));
    pointers.push_back(nullptr);
    execvp(pointers[0], pointers.data());
    std::perror(("exec " + argv[0]).c_str());
    _exit(127);  // 127 is the shell's "command not found", and we match it
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) throw std::runtime_error("waitpid failed");
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif

  result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  result.stdout_tail = tail_of(stdout_path, 8);
  result.stderr_tail = tail_of(stderr_path, 8);
  return result;
}

}  // namespace heavenzip
