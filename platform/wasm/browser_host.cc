// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
#include "platform/wasm/browser_host.h"

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <pthread.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "base/include/fml/message_loop.h"
#include "base/include/fml/task_runner.h"
namespace lynx::wasm {
namespace {
std::atomic<bool> render_worker{false};
std::mutex runner_mutex;
fml::RefPtr<fml::TaskRunner> engine_runner;
void InitializeLoop() {
  auto runner =
      fml::MessageLoop::EnsureInitializedForCurrentThread().GetTaskRunner();
  std::lock_guard<std::mutex> lock(runner_mutex);
  engine_runner = runner;
}
}  // namespace
bool IsRenderWorker() { return render_worker; }
void SendHostMessage(const std::string& kind, int id, const std::string& json) {
  struct Message {
    std::string kind, json;
    int id;
  };
  auto* message = new Message{kind, json, id};
  auto deliver = [](void* data) {
    std::unique_ptr<Message> message(static_cast<Message*>(data));
    EM_ASM(
        { Module.onLynxHost ?.(UTF8ToString($0), $1, UTF8ToString($2)); },
        message->kind.c_str(), message->id, message->json.c_str());
  };
  if (emscripten_is_main_runtime_thread())
    deliver(message);
  else if (!emscripten_proxy_async(emscripten_proxy_get_system_queue(),
                                   emscripten_main_runtime_thread_id(), deliver,
                                   message))
    delete message;
}
}  // namespace lynx::wasm
extern "C" EMSCRIPTEN_KEEPALIVE int host_start(int worker, const char* base_url,
                                               const char* fonts) {
  using namespace lynx::wasm;
  render_worker = worker != 0;
  if (!worker) {
    InitializeLoop();
    EM_ASM(
        {
          Module.lynxFontResources = JSON.parse(UTF8ToString($1));
          startLynxBrowserEngine(Module, UTF8ToString($0), false);
        },
        base_url, fonts);
    return 0;
  }
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
  emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas");
  struct Start {
    std::string url, fonts;
  };
  auto* start = new Start{base_url, fonts};
  pthread_t thread;
  const int result = pthread_create(
      &thread, &attr,
      [](void* data) -> void* {
        InitializeLoop();
        {
          std::unique_ptr<Start> start(static_cast<Start*>(data));
          EM_ASM(
              {
                Module.lynxFontResources = JSON.parse(UTF8ToString($1));
                startLynxBrowserEngine(Module, UTF8ToString($0), true);
              },
              start->url.c_str(), start->fonts.c_str());
        }
        emscripten_exit_with_live_runtime();
        return nullptr;
      },
      start);
  pthread_attr_destroy(&attr);
  if (result)
    delete start;
  else
    pthread_detach(thread);
  return result;
}
extern "C" EMSCRIPTEN_KEEPALIVE void host_reply(int id, const char* json) {
  lynx::wasm::SendHostMessage("reply", id, json);
}
extern "C" EMSCRIPTEN_KEEPALIVE void host_publish(const char* json) {
  lynx::wasm::SendHostMessage("state", 0, json);
}
extern "C" EMSCRIPTEN_KEEPALIVE void host_ready(const char* json) {
  lynx::wasm::SendHostMessage("ready", 0, json);
}
extern "C" EMSCRIPTEN_KEEPALIVE int host_request(int id, const char* method,
                                                 const char* args) {
  using namespace lynx::wasm;
  fml::RefPtr<fml::TaskRunner> runner;
  {
    std::lock_guard<std::mutex> lock(runner_mutex);
    runner = engine_runner;
  }
  if (!runner) return 1;
  runner->PostTask(
      [id, method = std::string(method), args = std::string(args)] {
        EM_ASM(
            {
              Module.lynxEngine.dispatch($0, UTF8ToString($1),
                                         JSON.parse(UTF8ToString($2)));
            },
            id, method.c_str(), args.c_str());
      });
  return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE void host_resource_request(int id,
                                                           const char* json) {
  lynx::wasm::SendHostMessage("resource", id, json);
}
extern "C" EMSCRIPTEN_KEEPALIVE void host_resource_cancel(int id) {
  lynx::wasm::SendHostMessage("resource-cancel", id, "{}");
}
extern "C" EMSCRIPTEN_KEEPALIVE void host_resource_response(
    int id, const uint8_t* bytes, int length, const char* mime, const char* url,
    const char* error) {
  if (length < 0 || length > 32 * 1024 * 1024 || (length && !bytes)) return;
  fml::RefPtr<fml::TaskRunner> runner;
  {
    std::lock_guard<std::mutex> lock(lynx::wasm::runner_mutex);
    runner = lynx::wasm::engine_runner;
  }
  if (!runner) return;
  // Copy before returning to Window: it frees its input immediately, while the
  // owner may be paused inside Inspector and cannot consume the response yet.
  std::vector<uint8_t> data;
  if (length) data.assign(bytes, bytes + length);
  runner->PostTask([id, data = std::move(data), mime = std::string(mime),
                    url = std::string(url), error = std::string(error)] {
    EM_ASM(
        {
          Module.lynxResourceBridge.complete($0, HEAPU8.slice($1, $1 + $2),
                                             UTF8ToString($3), UTF8ToString($4),
                                             UTF8ToString($5));
        },
        id, data.data(), data.size(), mime.c_str(), url.c_str(), error.c_str());
  });
}
