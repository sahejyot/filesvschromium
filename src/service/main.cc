#include "windows_service.h"
#include <base/at_exit.h>
#include <base/logging.h>
#include <windows.h>

int main(int argc, char** argv) {
  base::AtExitManager at_exit;

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_STDERR;
  if (!logging::InitLogging(settings)) {
    fprintf(stderr, "Failed to initialize logging\n");
    return 1;
  }

  LOG(INFO) << "Starting ChromiumThreadService...";

  ChromiumThreadService service;
  if (argc > 1) {
    LOG(INFO) << "Running in console mode...";
    int result = service.RunInConsoleMode();
    LOG(INFO) << "Console mode exited with code: " << result;
    return result;
  }

  SERVICE_TABLE_ENTRYW service_table[] = {
      {const_cast<LPWSTR>(ChromiumThreadService::kServiceName), ChromiumThreadService::ServiceMain},
      {nullptr, nullptr}};
  if (!StartServiceCtrlDispatcherW(service_table)) {
    LOG(ERROR) << "Failed to start service dispatcher: " << GetLastError();
    return 1;
  }

  return 0;
}
// #include "windows_service.h"
// #include <windows.h>

// int main() {
//     SERVICE_TABLE_ENTRY serviceTable[] = {
//        { (LPWSTR)L"MyThreadService", ServiceMain },
//         { NULL, NULL }
//     };

//     if (!StartServiceCtrlDispatcher(serviceTable)) {
//         return GetLastError();
//     }
//     return 0;
// }

//----------------------------------------------------------------------------------------------------------
// #include "base/functional/bind.h"
// #include "base/location.h"
// #include "base/task/task_runner.h"
// #include "base/threading/thread.h"
// #include <memory>
// #include <iostream>

// void MyTask() {
//   std::cout << "Running task on thread\n";
// }

// int main() {
//   // Create and start a thread
//   auto thread = std::make_unique<base::Thread>("MyThread");
//   thread->Start();

//   // Post a task to the thread
//   thread->task_runner()->PostTask(FROM_HERE, base::BindOnce(&MyTask));

//   // Wait briefly to allow the task to run (for demo purposes)
//   base::PlatformThread::Sleep(base::Seconds(1));

//   // Clean up
//   thread->Stop();
//   return 0;
// }