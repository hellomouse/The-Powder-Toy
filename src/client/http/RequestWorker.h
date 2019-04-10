#ifndef REQUESTWORKER_H
#define REQUESTWORKER_H

#include <deque>
#include <pthread.h>
#include <emscripten.h>
#include "common/String.h"
#include "common/Singleton.h"
#include "common/tpt-thread.h"

// stupid circular dependencies
namespace http { class RequestWorker; };
#include "Request.h"

#define MAX_REQUEST_COUNT 6

namespace http {
    class RequestWorker: public Singleton<RequestWorker> {
        enum Operation {
            START_REQUEST, CANCEL_REQUEST
        };
        struct Task {
            Operation operation;
            Request *request;
        };

        // push_front and pop_back
        std::deque<Request *> request_queue;
        Request *active_requests[MAX_REQUEST_COUNT];
        pthread_t worker;
        pthread_mutex_t rw_mutex;
        // pthread_cond_t rw_cv;
        bool active_requests_full;
        bool run;
        std::vector<Task> work_queue;

        void start_request(Request *request);
        // DON'T USE THIS
        void remove_request(Request *request);
        void cancel_request(Request *request);
        // if lockMutex is passed as false, it will not acquire its own mutex
        // returns whether or not something was dequeued
        bool dequeue_next(unsigned int to_id, bool lock_mutex = true);
        void execute_request(unsigned int id, bool lock_mutex = true);
        TH_ENTRY_POINT static void *thread_worker_launcher(void *ctx);
        static void thread_worker(void *ctx);
    public:
        RequestWorker();
        ~RequestWorker();

        void evt_progress(unsigned int id, unsigned int current, unsigned int total);
        void evt_finish(unsigned int id, int status, char *data, size_t length);
        void evt_error(unsigned int id);

        // static const int blah = sizeof(Request);

        friend class Request;
    };
}

extern "C" {
    EMSCRIPTEN_KEEPALIVE void rwProgress(unsigned int id, unsigned int current, unsigned int total);
    EMSCRIPTEN_KEEPALIVE void rwFinish(unsigned int id, int status, char* data, size_t length);
    EMSCRIPTEN_KEEPALIVE void rwError(unsigned int id);
}

#endif // REQUESTWORKER_H