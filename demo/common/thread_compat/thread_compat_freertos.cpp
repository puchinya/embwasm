#include "thread_compat/thread_compat.hpp"

namespace compat {

// ─── Mutex ───────────────────────────────────────────────────────────────────

Mutex::Mutex()  : handle_(xSemaphoreCreateMutex()) {}
Mutex::~Mutex() { vSemaphoreDelete(handle_); }
void Mutex::lock()   { xSemaphoreTake(handle_, portMAX_DELAY); }
void Mutex::unlock() { xSemaphoreGive(handle_); }

// ─── CondVar ─────────────────────────────────────────────────────────────────

CondVar::CondVar()  : sem_(xSemaphoreCreateCounting(16, 0)), waiters_(0) {}
CondVar::~CondVar() { vSemaphoreDelete(sem_); }

void CondVar::wait(UniqueLock& lk) {
    taskENTER_CRITICAL();
    waiters_++;
    taskEXIT_CRITICAL();

    lk.unlock();
    xSemaphoreTake(sem_, portMAX_DELAY);
    lk.lock();

    taskENTER_CRITICAL();
    waiters_--;
    taskEXIT_CRITICAL();
}

void CondVar::notify_one() { xSemaphoreGive(sem_); }

void CondVar::notify_all() {
    taskENTER_CRITICAL();
    int n = waiters_;
    taskEXIT_CRITICAL();
    for (int i = 0; i < n; i++) xSemaphoreGive(sem_);
}

// ─── Thread ──────────────────────────────────────────────────────────────────

struct Thread::TaskArgs {
    Thread* self;
    IFunc*  fn;
};

void Thread::task_trampoline_(void* p) {
    TaskArgs* args = static_cast<TaskArgs*>(p);
    Thread*   self = args->self;
    IFunc*    fn   = args->fn;
    delete args;

    fn->call();
    delete fn;

    xSemaphoreGive(self->join_sem_);
    vTaskDelete(nullptr);
}

Thread::Thread() : handle_(nullptr), join_sem_(nullptr) {}

Thread::~Thread() {
    if (joinable()) join();
}

void Thread::start_impl(IFunc* fn, size_t stack_words) {
    join_sem_ = xSemaphoreCreateBinary();
    auto* args = new TaskArgs{this, fn};
    xTaskCreate(task_trampoline_, "compat",
                static_cast<uint16_t>(stack_words),
                args, tskIDLE_PRIORITY + 1, &handle_);
}

void Thread::join() {
    xSemaphoreTake(join_sem_, portMAX_DELAY);
    vSemaphoreDelete(join_sem_);
    join_sem_ = nullptr;
    handle_   = nullptr;
}

bool Thread::joinable() const { return handle_ != nullptr; }

} // namespace compat
