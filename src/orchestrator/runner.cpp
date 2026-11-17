// runner.cpp — fork/exec with both streams to files.
#include "heavenzip/runner.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include <vector>
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
  // CreateProcess with explicit handles, not `std::system`.
  //
  // The shell version built one string — `prog args > out 2> err` — and handed it to
  // cmd.exe, whose quoting and redirection rules are its own. On the Windows runner
  // that produced exit 1 with an empty stderr log: the program never ran, and the
  // redirect files stayed empty, so the failure said nothing about why. Handles remove
  // the shell from the path entirely.
  //
  // Windows takes one command line, not an argv array, so the tokens are quoted and
  // joined. Every token here was built by this program, and quoting each one is what
  // keeps a path with spaces in it a single argument.
  std::string line;
  for (const std::string& token : argv) {
    if (!line.empty()) line += ' ';
    line += quote(token);
  }

  // Inheritable handles, so the child can use them.
  SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE out = CreateFileA(stdout_path.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE err = CreateFileA(stderr_path.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE || err == INVALID_HANDLE_VALUE) {
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    throw std::runtime_error("cannot open the step's log files: " + stdout_path.string());
  }

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = out;
  startup.hStdError = err;

  PROCESS_INFORMATION child{};
  std::vector<char> command_line(line.begin(), line.end());
  command_line.push_back('\0');
  // Not `started`: this function already uses that name for the steady_clock reading
  // at the top, and MSVC refuses the redefinition outright.
  const BOOL spawned = CreateProcessA(nullptr, command_line.data(), nullptr, nullptr,
                                      TRUE, 0, nullptr, nullptr, &startup, &child);
  CloseHandle(out);
  CloseHandle(err);
  if (!spawned) {
    const DWORD why = GetLastError();
    throw std::runtime_error("cannot start `" + argv.front() + "` (error " +
                             std::to_string(why) + ")");
  }
  WaitForSingleObject(child.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(child.hProcess, &code);
  CloseHandle(child.hProcess);
  CloseHandle(child.hThread);
  result.exit_code = static_cast<int>(code);
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
