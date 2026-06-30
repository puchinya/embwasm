#pragma once

#ifdef USE_FREERTOS
#  include "FreeRTOS.h"
#  include "task.h"
#  include "semphr.h"
#else
#  include <thread>
#  include <mutex>
#  include <condition_variable>
#endif

#include <cstddef>

// Default stack depth for compat::Thread on FreeRTOS (in 32-bit words).
// Override per-target: target_compile_definitions(tgt PRIVATE COMPAT_THREAD_STACK_SIZE=2048)
#ifndef COMPAT_THREAD_STACK_SIZE
#  define COMPAT_THREAD_STACK_SIZE 1024
#endif

namespace compat {

// ─── Mutex ───────────────────────────────────────────────────────────────────

class Mutex {
public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
#ifdef USE_FREERTOS
    SemaphoreHandle_t handle_;
#else
    std::mutex mtx_;
    friend class CondVar;
#endif
};

// ─── LockGuard ───────────────────────────────────────────────────────────────

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }

    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

// ─── UniqueLock ──────────────────────────────────────────────────────────────

class UniqueLock {
public:
    explicit UniqueLock(Mutex& m) : m_(m), locked_(true) { m_.lock(); }
    ~UniqueLock() { if (locked_) m_.unlock(); }
    void lock()   { m_.lock();   locked_ = true;  }
    void unlock() { m_.unlock(); locked_ = false; }

    UniqueLock(const UniqueLock&)            = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

private:
    Mutex& m_;
    bool locked_;
    friend class CondVar;
};

// ─── CondVar ─────────────────────────────────────────────────────────────────

class CondVar {
public:
    CondVar();
    ~CondVar();

    // Atomically releases lk and blocks until notify_one()/notify_all().
    // Re-acquires lk before returning.
    void wait(UniqueLock& lk);

    // Equivalent to: while (!pred()) wait(lk);
    template<typename Pred>
    void wait(UniqueLock& lk, Pred pred) {
        while (!pred()) wait(lk);
    }

    void notify_one();
    void notify_all();

    CondVar(const CondVar&)            = delete;
    CondVar& operator=(const CondVar&) = delete;

private:
#ifdef USE_FREERTOS
    SemaphoreHandle_t sem_;
    volatile int waiters_;
#else
    std::condition_variable_any cv_;
#endif
};

// ─── Thread ──────────────────────────────────────────────────────────────────

class Thread {
public:
    Thread();
    ~Thread();  // joins if still running

    // Start with any callable (lambda, function pointer, functor).
    // stack_words: FreeRTOS task stack depth in 32-bit words (ignored on host).
    template<typename F>
    void start(F func, size_t stack_words = COMPAT_THREAD_STACK_SIZE) {
        start_impl(new Wrapper<F>(static_cast<F&&>(func)), stack_words);
    }

    void join();
    bool joinable() const;

    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;

private:
    struct IFunc {
        virtual void call() = 0;
        virtual ~IFunc() {}
    };

    template<typename F>
    struct Wrapper : IFunc {
        F f;
        explicit Wrapper(F&& f_) : f(static_cast<F&&>(f_)) {}
        void call() override { f(); }
    };

    void start_impl(IFunc* fn, size_t stack_words);

#ifdef USE_FREERTOS
    struct TaskArgs;
    static void task_trampoline_(void* p);
    TaskHandle_t      handle_;
    SemaphoreHandle_t join_sem_;
#else
    std::thread thread_;
#endif
};

} // namespace compat
