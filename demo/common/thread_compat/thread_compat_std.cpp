#include "thread_compat/thread_compat.hpp"

namespace compat {

// ─── Mutex ───────────────────────────────────────────────────────────────────

Mutex::Mutex()  {}
Mutex::~Mutex() {}
void Mutex::lock()   { mtx_.lock(); }
void Mutex::unlock() { mtx_.unlock(); }

// ─── CondVar ─────────────────────────────────────────────────────────────────

CondVar::CondVar()  {}
CondVar::~CondVar() {}

// std::condition_variable_any accepts any BasicLockable (UniqueLock has lock/unlock).
void CondVar::wait(UniqueLock& lk) { cv_.wait(lk); }
void CondVar::notify_one()         { cv_.notify_one(); }
void CondVar::notify_all()         { cv_.notify_all(); }

// ─── Thread ──────────────────────────────────────────────────────────────────

Thread::Thread()  {}
Thread::~Thread() { if (joinable()) join(); }

void Thread::start_impl(IFunc* fn, size_t /*stack_words*/) {
    thread_ = std::thread([fn] {
        fn->call();
        delete fn;
    });
}

void Thread::join()          { thread_.join(); }
bool Thread::joinable() const { return thread_.joinable(); }

} // namespace compat
