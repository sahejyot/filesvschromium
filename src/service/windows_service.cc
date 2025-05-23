#include "windows_service.h"

#include <base/logging.h>
#include <base/threading/platform_thread.h>

ChromiumThreadService* ChromiumThreadService::instance_ = nullptr;

ChromiumThreadService::ChromiumThreadService()
    : service_status_(),
      service_status_handle_(nullptr),
      worker_thread_(std::make_unique<base::Thread>("WorkerThread")),
      stopping_(false) {
  instance_ = this;
  service_status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status_.dwCurrentState = SERVICE_START_PENDING;
  service_status_.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  LOG(INFO) << "ChromiumThreadService constructed";
}

ChromiumThreadService::~ChromiumThreadService() {
  instance_ = nullptr;
  LOG(INFO) << "ChromiumThreadService destroyed";
}

int ChromiumThreadService::RunInConsoleMode() {
  LOG(INFO) << "Entering RunInConsoleMode";
  Start();
  LOG(INFO) << "Thread started, press Enter to stop...";
  getchar();
  Stop();
  LOG(INFO) << "Exiting RunInConsoleMode";
  return 0;
}

void WINAPI ChromiumThreadService::ServiceMain(DWORD argc, LPWSTR* argv) {
  LOG(INFO) << "Entering ServiceMain";
  if (!instance_) {
    LOG(ERROR) << "Service instance not initialized";
    return;
  }

  instance_->service_status_handle_ =
      RegisterServiceCtrlHandlerW(kServiceName, ServiceControlHandler);
  if (!instance_->service_status_handle_) {
    LOG(ERROR) << "Failed to register service control handler: " << GetLastError();
    return;
  }

  instance_->service_status_.dwCurrentState = SERVICE_RUNNING;
  instance_->service_status_.dwWin32ExitCode = NO_ERROR;
  instance_->service_status_.dwServiceSpecificExitCode = 0;
  SetServiceStatus(instance_->service_status_handle_, &instance_->service_status_);
  LOG(INFO) << "Service reported as running";

  instance_->Start();
  LOG(INFO) << "ServiceMain completed";
}

void WINAPI ChromiumThreadService::ServiceControlHandler(DWORD control) {
  LOG(INFO) << "ServiceControlHandler received control: " << control;
  if (!instance_) {
    LOG(ERROR) << "Service instance not initialized";
    return;
  }

  switch (control) {
    case SERVICE_CONTROL_STOP:
      instance_->service_status_.dwCurrentState = SERVICE_STOP_PENDING;
      SetServiceStatus(instance_->service_status_handle_, &instance_->service_status_);
      instance_->Stop();
      instance_->service_status_.dwCurrentState = SERVICE_STOPPED;
      instance_->service_status_.dwWin32ExitCode = NO_ERROR;
      SetServiceStatus(instance_->service_status_handle_, &instance_->service_status_);
      break;
    default:
      SetServiceStatus(instance_->service_status_handle_, &instance_->service_status_);
      break;
  }
}

void ChromiumThreadService::Start() {
  LOG(INFO) << "Starting worker thread...";
  if (!worker_thread_->Start()) {
    LOG(ERROR) << "Failed to start WorkerThread";
    return;
  }
  LOG(INFO) << "Worker thread started with ID: " << worker_thread_->GetThreadId();
  RunWorkerThread();
}

void ChromiumThreadService::Stop() {
  stopping_ = true;
  worker_thread_->Stop();
  LOG(INFO) << "Worker thread stopped";
}

void ChromiumThreadService::RunWorkerThread() {
  // Post the task from within the thread's context
  worker_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce([]() {
        base::RunLoop run_loop;
        while (!ChromiumThreadService::instance_ ||
               !ChromiumThreadService::instance_->stopping_) {
          LOG(INFO) << "Worker thread working at time: "
                    << base::Time::Now().ToTimeT();
          base::PlatformThread::Sleep(base::Seconds(5));
        }
        run_loop.Quit();
      }));
}

// #include "windows_service.h"
// #include <windows.h>
// #include "base/threading/thread.h"
// #include "base/logging.h"

// // Globals
// SERVICE_STATUS service_status = {};
// SERVICE_STATUS_HANDLE service_status_handle = nullptr;
// base::Thread* service_thread = nullptr;
// bool running = true;

// void ReportServiceStatus(DWORD current_state, DWORD win32_exit_code, DWORD wait_hint) {
//   static DWORD checkpoint = 1;

//   service_status.dwCurrentState = current_state;
//   service_status.dwWin32ExitCode = win32_exit_code;
//   service_status.dwWaitHint = wait_hint;
//   service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
//   service_status.dwControlsAccepted = (current_state == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP;
//   service_status.dwCheckPoint = (current_state == SERVICE_RUNNING || current_state == SERVICE_STOPPED) ? 0 : checkpoint++;

//   SetServiceStatus(service_status_handle, &service_status);
// }

// void WINAPI ServiceCtrlHandler(DWORD ctrl_code) {
//   switch (ctrl_code) {
//     case SERVICE_CONTROL_STOP:
//       OutputDebugString(L"Service stop requested.\n");
//       running = false;
//       ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 1000);
//       if (service_thread) {
//         service_thread->Stop();
//         delete service_thread;
//         service_thread = nullptr;
//       }
//       ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
//       break;
//     default:
//       break;
//   }
// }

// void ServiceWorkerThread() {
//   while (running) {
//     ::Sleep(5000);
//     OutputDebugString(L"Service is running...\n");
//   }
// }

// void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
//   service_status_handle = RegisterServiceCtrlHandler(L"MyThreadService", ServiceCtrlHandler);
//   if (!service_status_handle) return;

//   ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

//   service_thread = new base::Thread("ServiceWorker");
//   base::Thread::Options options;
//   if (!service_thread->StartWithOptions(std::move(options))) {
//     OutputDebugString(L"Failed to start service thread\n");
//     ReportServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
//     return;
//   }

//   service_thread->task_runner()->PostTask(FROM_HERE, base::BindOnce(&ServiceWorkerThread));
//   ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
// }
