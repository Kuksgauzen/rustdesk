#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <tchar.h>
#include <uni_links_desktop/uni_links_desktop_plugin.h>
#include <windows.h>

#include <algorithm>
#include <iostream>

#include "win32_desktop.h"
#include "flutter_window.h"
#include "utils.h"

typedef char** (*FUNC_RUSTDESK_CORE_MAIN)(int*);
typedef void (*FUNC_RUSTDESK_FREE_ARGS)( char**, int);
typedef int (*FUNC_RUSTDESK_GET_APP_NAME)(wchar_t*, int);
typedef int (*FUNC_RUSTDESK_IS_DISABLE_INSTALLATION)();
/// Note: `--server`, `--service` are already handled in [core_main.rs].
const std::vector<std::string> parameters_white_list = {"--install", "--cm"};

const wchar_t* getWindowClassName();

// A launch without arguments waits for the main window of an earlier launch
// for at most kMainWindowPollAttempts * kMainWindowPollIntervalMs = 10 s.
static const DWORD kMainWindowPollIntervalMs = 100;
static const int kMainWindowPollAttempts = 100;

// FindWindowW and CreateAndShow below are not atomic, so launches that overlap
// in time could all miss the main window and each open their own. Serialize
// them with a per-session mutex, as the tray does. The first launch takes it
// and holds it until the process exits; later ones wait here for its window.
// If the owner exits first, the waiting launch takes the mutex and starts.
// If neither happens in time, the launch goes on as before.
// Only called for launches without arguments, which would show the main
// window. Launches with arguments (`--connect`, `rustdesk://` links, ...)
// never wait and take the old path.
static void WaitForMainWindowOwner(const std::wstring& app_name) {
  std::wstring mutex_name = L"Local\\" + app_name + L"_main";
  ::SetLastError(0);
  HANDLE mutex = ::CreateMutexW(NULL, TRUE, mutex_name.c_str());
  DWORD error = ::GetLastError();
  if (mutex != NULL && error == ERROR_ALREADY_EXISTS) {
    for (int i = 0; i < kMainWindowPollAttempts; i++) {
      if (::FindWindowW(getWindowClassName(), app_name.c_str()) != NULL) {
        return;
      }
      DWORD wait = ::WaitForSingleObject(mutex, kMainWindowPollIntervalMs);
      if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        return;
      }
    }
  } else if (mutex == NULL && error == ERROR_ACCESS_DENIED) {
    // The mutex was created by a process with a higher integrity level or by
    // another user in the same session. It cannot be opened, so only wait for
    // the window.
    for (int i = 0; i < kMainWindowPollAttempts; i++) {
      if (::FindWindowW(getWindowClassName(), app_name.c_str()) != NULL) {
        return;
      }
      ::Sleep(kMainWindowPollIntervalMs);
    }
  }
  // Any other CreateMutexW failure (mutex == NULL): do not wait, go on as
  // before this change. When it succeeded, the handle stays open, and the
  // mutex owned, until the process exits.
}

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command)
{
  HINSTANCE hInstance = LoadLibraryA("librustdesk.dll");
  if (!hInstance)
  {
    std::cout << "Failed to load librustdesk.dll." << std::endl;
    return EXIT_FAILURE;
  }
  FUNC_RUSTDESK_CORE_MAIN rustdesk_core_main =
      (FUNC_RUSTDESK_CORE_MAIN)GetProcAddress(hInstance, "rustdesk_core_main_args");
  if (!rustdesk_core_main)
  {
    std::cout << "Failed to get rustdesk_core_main." << std::endl;
    return EXIT_FAILURE;
  }
  FUNC_RUSTDESK_FREE_ARGS free_c_args =
      (FUNC_RUSTDESK_FREE_ARGS)GetProcAddress(hInstance, "free_c_args");
  if (!free_c_args)
  {
    std::cout << "Failed to get free_c_args." << std::endl;
    return EXIT_FAILURE;
  }
  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();
  // Remove possible trailing whitespace from command line arguments
  for (auto& argument : command_line_arguments) {
    argument.erase(argument.find_last_not_of(" \n\r\t"));
  }

  int args_len = 0;
  char** c_args = rustdesk_core_main(&args_len);
  if (!c_args)
  {
    std::string args_str = "";
    for (const auto& argument : command_line_arguments) {
      args_str += (argument + " ");
    }
    // std::cout << "RustDesk [" << args_str << "], core returns false, exiting without launching Flutter app." << std::endl;
    return EXIT_SUCCESS;
  }
  std::vector<std::string> rust_args(c_args, c_args + args_len);
  free_c_args(c_args, args_len);
  FUNC_RUSTDESK_IS_DISABLE_INSTALLATION rustdesk_is_disable_installation =
      (FUNC_RUSTDESK_IS_DISABLE_INSTALLATION)GetProcAddress(hInstance, "rustdesk_is_disable_installation");
  bool is_disable_installation =
      rustdesk_is_disable_installation && rustdesk_is_disable_installation() != 0;
  const auto installParam = std::string("--install");
  // Flutter reads the original process command line, not only rust_args, so
  // remove the `--install` injected by the portable wrapper here as well. This
  // also lets `no-install.exe` continue as a portable app when installation is
  // disabled. See: https://github.com/rustdesk/rustdesk-server-pro/issues/991#issuecomment-4978376890
  if (is_disable_installation) {
    command_line_arguments.erase(
        std::remove(command_line_arguments.begin(),
                    command_line_arguments.end(),
                    installParam),
        command_line_arguments.end());
  }

  std::wstring app_name = L"RustDesk";
  FUNC_RUSTDESK_GET_APP_NAME get_rustdesk_app_name = (FUNC_RUSTDESK_GET_APP_NAME)GetProcAddress(hInstance, "get_rustdesk_app_name");
  if (get_rustdesk_app_name) {
    wchar_t app_name_buffer[512] = {0};
    if (get_rustdesk_app_name(app_name_buffer, 512) == 0) {
      app_name = std::wstring(app_name_buffer);
    }
  }

  // Only a launch that would show the main window waits. `--install`, `--cm`
  // and every other launch with arguments take the old path.
  if (command_line_arguments.empty()) {
    WaitForMainWindowOwner(app_name);
  }

  // Uri links dispatch
  HWND hwnd = ::FindWindowW(getWindowClassName(), app_name.c_str());
  if (hwnd != NULL) {
    // Allow multiple flutter instances when being executed by parameters
    // contained in whitelists.
    bool allow_multiple_instances = false;
    for (auto& whitelist_param : parameters_white_list) {
      allow_multiple_instances =
          allow_multiple_instances ||
          std::find(command_line_arguments.begin(),
                    command_line_arguments.end(),
                    whitelist_param) != command_line_arguments.end();
    }
    if (!allow_multiple_instances) {
      if (!command_line_arguments.empty()) {
        // The process launched by the browser owns the foreground permission.
        // Transfer it to the existing RustDesk process before dispatching the
        // URI so that it can bring an existing session to the foreground.
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid != 0) {
          ::AllowSetForegroundWindow(pid);
        }
        // Dispatch command line arguments
        DispatchToUniLinksDesktop(hwnd);
      } else {
        // Not called with arguments, or just open the app shortcut on desktop.
        // So we just show the main window instead.
        ::ShowWindow(hwnd, SW_NORMAL);
        ::SetForegroundWindow(hwnd);
      }
      return EXIT_FAILURE;
    }
  }

  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent())
  {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  flutter::DartProject project(L"data");
  // connection manager hide icon from taskbar
  bool is_cm_page = false;
  auto cmParam = std::string("--cm");
  if (!command_line_arguments.empty() && command_line_arguments.front().compare(0, cmParam.size(), cmParam.c_str()) == 0) {
    is_cm_page = true;
  }
  bool is_install_page = false;
  if (!command_line_arguments.empty() && command_line_arguments.front().compare(0, installParam.size(), installParam.c_str()) == 0) {
    is_install_page = true;
  }

  command_line_arguments.insert(command_line_arguments.end(), rust_args.begin(), rust_args.end());
  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);

  // Get primary monitor's work area.
  Win32Window::Point workarea_origin(0, 0);
  Win32Window::Size workarea_size(0, 0);

  Win32Desktop::GetWorkArea(workarea_origin, workarea_size);

  // Compute window bounds for default main window position: (10, 10) x(800, 600)
  Win32Window::Point relative_origin(10, 10);

  Win32Window::Point origin(workarea_origin.x + relative_origin.x, workarea_origin.y + relative_origin.y);
  Win32Window::Size size(800u, 600u);

  // Fit the window to the monitor's work area.
  Win32Desktop::FitToWorkArea(origin, size);

  std::wstring window_title;
  if (is_cm_page) {
    window_title = app_name + L" - Connection Manager";
  } else if (is_install_page) {
    window_title = app_name + L" - Install";
  } else {
    window_title = app_name;
  }
  if (!window.CreateAndShow(window_title, origin, size, !is_cm_page)) {
      return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0))
  {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  ::CoUninitialize();
  return EXIT_SUCCESS;
}
