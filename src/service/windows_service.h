#ifndef WINDOWS_SERVICE_H_
#define WINDOWS_SERVICE_H_

#include <base/run_loop.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <windows.h>

class ChromiumThreadService {
 public:
  static constexpr LPCWSTR kServiceName = L"ChromiumThreadService";

  ChromiumThreadService();
  ~ChromiumThreadService();

  int RunInConsoleMode();

  static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
  static void WINAPI ServiceControlHandler(DWORD control);

 private:
  void Start();
  void Stop();
  void RunWorkerThread();

  static ChromiumThreadService* instance_;
  SERVICE_STATUS service_status_;
  SERVICE_STATUS_HANDLE service_status_handle_;
  std::unique_ptr<base::Thread> worker_thread_;
  bool stopping_;
};

#endif  // WINDOWS_SERVICE_H_