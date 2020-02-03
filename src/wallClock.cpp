/*
 * Copyright 2018 Andrei Pangin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include "wallClock.h"
#include "os.h"
#include "profiler.h"
#include "stackFrame.h"


// Maximum number of threads sampled in one iteration. This limit serves as a throttle
// when generating profiling signals. Otherwise applications with too many threads may
// suffer from a big profiling overhead. Also, keeping this limit low enough helps
// to avoid contention on a spin lock inside Profiler::recordSample().
const int THREADS_PER_TICK = 8;

// Stop profiling thread with this signal. The same signal is used inside JDK to interrupt I/O operations.
const int WAKEUP_SIGNAL = SIGIO;


long WallClock::_interval;
bool WallClock::_sample_idle_threads;

void WallClock::signalHandler(int signo, siginfo_t* siginfo, void* ucontext) {
#ifdef __linux__
    // Workaround for JDK-8237858: restart the interrupted syscall manually.
    // Currently this is implemented only for poll().
    StackFrame frame(ucontext);
    if (frame.retval() == (uintptr_t)-EINTR) {
        frame.restartSyscall();
    }
#endif // __linux__

    Profiler::_instance.recordSample(ucontext, _interval, 0, NULL);
}

void WallClock::wakeupHandler(int signo) {
    // Dummy handler for interrupting syscalls
}

Error WallClock::start(Arguments& args) {
    if (args._interval < 0) {
        return Error("interval must be positive");
    }
    _interval = args._interval ? args._interval : DEFAULT_INTERVAL;
    _sample_idle_threads = strcmp(args._event, EVENT_WALL) == 0;

    OS::installSignalHandler(SIGVTALRM, signalHandler);
    OS::installSignalHandler(SIGPROF, signalHandler);
    OS::installSignalHandler(WAKEUP_SIGNAL, NULL, wakeupHandler);

    _running = true;

    if (pthread_create(&_thread, NULL, threadEntry, this) != 0) {
        return Error("Unable to create timer thread");
    }

    return Error::OK;
}

void WallClock::stop() {
    _running = false;
    pthread_kill(_thread, WAKEUP_SIGNAL);
    pthread_join(_thread, NULL);
}

void WallClock::timerLoop() {
    int self = OS::threadId();
    ThreadFilter* thread_filter = Profiler::_instance.threadFilter();
    bool thread_filter_enabled = thread_filter->enabled();
    bool sample_idle_threads = _sample_idle_threads;

    struct timespec timeout;
    timeout.tv_sec = _interval / 1000000000;
    timeout.tv_nsec = _interval % 1000000000;

    ThreadList* thread_list = NULL;

    while (_running) {
        if (thread_list == NULL) {
            thread_list = OS::listThreads();
        }

        for (int count = 0; count < THREADS_PER_TICK; ) {
            int thread_id = thread_list->next();
            if (thread_id == -1) {
                delete thread_list;
                thread_list = NULL;
                break;
            }

            if (thread_id == self || (thread_filter_enabled && !thread_filter->accept(thread_id))) {
                continue;
            }

            ThreadState state = OS::threadState(thread_id);
            if (state == THREAD_RUNNING) {
                if (OS::sendSignalToThread(thread_id, SIGPROF)) count++;
            } else if (state == THREAD_SLEEPING && sample_idle_threads) {
                if (OS::sendSignalToThread(thread_id, SIGVTALRM)) count++;
            }
        }

        nanosleep(&timeout, NULL);
    }

    delete thread_list;
}
