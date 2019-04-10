#include <deque>
#include <pthread.h>
#include <emscripten.h>
#include "RequestWorker.h"

namespace http {
    // javascript intensifies
    EM_JS(void, js_worker_init, (int maxRequests), {
        console.log('http::js_worker_init: initializing worker');
        if (Module.requestWorker) throw new Error('Request worker already exists?');
        Module.requestWorker = {
            Request: class Request {
                constructor(id, url, activeRequests) {
                    this.id = id;
                    this.url = UTF8ToString(url);
                    this.activeRequests = activeRequests;
                    this.method = 'GET';
                    this.postData = null;
                    this.headers = new Headers();
                    this.controller = new AbortController();
                    this.contentLength = null;
                    this.loaded = null;
                    this.status = null;

                    console.log('Module.requestWorker.Request[' + this.id + ']#constructor(' + this.url + ')');
                }
                addHeader(name, val) {
                    name = UTF8ToString(name);
                    val = UTF8ToString(val);
                    console.log('Module.requestWorker.Request[' + this.id + ']#addHeader: ' + name + ': ' + val);
                    this.headers.append(name, val);
                }
                addFormData(name, val, length) {
                    if (!this.postData) {
                        this.method = 'POST';
                        this.postData = new FormData();
                    }
                    name = UTF8ToString(name);
                    console.log('Module.requestWorker.Request[' + this.id + ']#addFormData: ' + name);
                    let split = name.split(':');
                    if (split[1]) {
                        console.log('filename found', split);
                        // a filename was specified
                        // go make a blob
                        name = split[0];
                        let filename = split[1];
                        // buffer with a copy of the file
                        let typedArray = new Uint8Array(Module.HEAPU8.buffer, val, length).slice();
                        console.log('typedArray', typedArray);
                        let blob = new Blob([typedArray.buffer], { type: 'application/octet-stream' });
                        console.log('blob', blob);
                        this.postData.append(name, blob, filename);
                        console.log('added to formData');
                    } else this.postData.append(name, UTF8ToString(val));
                }
                abort() {
                    console.log('Module.requestWorker.Request[' + this.id + ']#abort()');
                    this.controller.abort();
                }
                sendProgressUpdate() {
                    console.log('Module.requestWorker.Request[' + this.id + ']#sendProgressUpdate ' + this.loaded + ' ' + this.contentLength);
                    Module._rwProgress(this.id, this.loaded, this.contentLength);
                }
                sendSuccess(data) {
                    console.log('Module.requestWorker.Request[' + this.id + ']#run: success');
                    Module._rwFinish(this.id, this.status, ...Module.requestWorker.toCBuffer(data));
                    this.activeRequests[this.id] = null;
                }
                sendError() {
                    console.log('Module.requestWorker.Request[' + this.id + ']#run: error');
                    Module._rwError(this.id);
                    this.activeRequests[this.id] = null;
                }
                run() {
                    console.log('Module.requestWorker.Request[' + this.id + ']#run: sending request');
                    fetch(this.url, {
                        method: this.method,
                        body: this.postData,
                        headers: this.headers,
                        signal: this.controller.signal
                    }).then(response => {
                        this.status = response.status;
                        console.log('Module.requestWorker.Request[' + this.id + ']#run: status ' + this.status);
                        this.contentLength = response.headers.get('content-length');
                        if (!this.contentLength) this.contentLength = 0;
                        else this.contentLength = parseInt(this.contentLength, 10);
                        let self = this;
                        this.loaded = 0;
                        let reader = response.body.getReader();
                        let stream = new ReadableStream({
                            start(controller) {
                                function read() {
                                    reader.read().then(({done, value}) => {
                                        if (done) return controller.close();
                                        self.loaded += value.byteLength;
                                        self.sendProgressUpdate();
                                        controller.enqueue(value);
                                        read();
                                    }).catch(err => {
                                        console.error('Fetch: progress stream error');
                                        console.error(err);
                                        controller.error(err);
                                    });
                                }
                                read();
                            }
                        });
                        return new Response(stream, { headers: response.headers });
                    }).then(output => output.arrayBuffer())
                    .then(data => this.sendSuccess(data))
                    .catch(err => {
                        // network error!
                        this.sendError();
                        console.error('Fetch: network error on id ' + this.id);
                        console.error(err);
                    });
                }
            },
            activeRequests: new Array(maxRequests).fill(null),
            // note: FREE THESE
            toCBuffer(arrayBuffer) {
                let allocated = _malloc(arrayBuffer.byteLength);
                let view = new Uint8Array(arrayBuffer);
                for (let i = 0; i < view.length; i++) {
                    Module.HEAPU8[allocated + i] = view[i];
                }
                return [allocated, arrayBuffer.byteLength];
            },
            // note: strings passed through here are actually pointers, call UTF8ToString
            addRequest(id, url) {
                this.activeRequests[id] = new this.Request(id, url, this.activeRequests);
            },
            removeRequest(id) {
                this.activeRequests[id] = null;
            },
            cancelRequest(id) {
                this.activeRequests[id].abort();
                this.removeRequest(id);
            },
            addHeader(id, name, val) {
                this.activeRequests[id].addHeader(name, val);
            },
            addFormData(id, name, val, length) {
                this.activeRequests[id].addFormData(name, val, length);
            },
            runRequest(id) {
                this.activeRequests[id].run();
            }
        };
    });

    RequestWorker::RequestWorker():
        active_requests_full(false),
        run(true)
    {
        printf("RequestWorker::RequestWorker()\n");
        pthread_mutex_init(&rw_mutex, NULL);
        // pthread_cond_init(&rw_cv, NULL);
        pthread_create(&worker, NULL, &RequestWorker::thread_worker_launcher, this);
        printf("RequestWorker::RequestWorker: started worker thread\n");
    }

    RequestWorker::~RequestWorker() {
        // TODO: cancel all remaining requests, etc
    }

    /** Add a request to the worker. This will also start the request. */
    void RequestWorker::start_request(Request *request) {
        printf("RequestWorker::start_request()\n");
        pthread_mutex_lock(&rw_mutex);
        pthread_mutex_lock(&request->rm_mutex);
        printf("RequestWorker::start_request: locked mutexes\n");
        if (!active_requests_full) {
            for (unsigned int i = 0; i < MAX_REQUEST_COUNT; i++) {
                if (!active_requests[i]) {
                    printf("RequestWorker::start_request: assigned id %u\n", i);
                    active_requests[i] = request;
                    request->id = i;
                    printf("RequestWorker::start_request: starting request\n");
                    execute_request(i, false);
                    pthread_mutex_unlock(&request->rm_mutex);
                    pthread_mutex_unlock(&rw_mutex);
                    printf("RequestWorker::start_request: done\n");
                    return;
                }
            }
            active_requests_full = true;
        }
        printf("RequestWorker::start_request: active_requests full, adding to queue\n");
        // active requests list is full, queue the request
        request->id = -1;
        request_queue.push_front(request);
        pthread_mutex_unlock(&request->rm_mutex);
        pthread_mutex_unlock(&rw_mutex);
    }

    /* PLEASE DON'T USE THIS */
    void RequestWorker::remove_request(Request *request) {
        pthread_mutex_lock(&rw_mutex);
        pthread_mutex_lock(&request->rm_mutex);
        if (request->id >= 0) {
            active_requests[request->id] = NULL;
        } else if (request->id == -1) {
            auto q = request_queue;
            q.erase(std::remove(q.begin(), q.end(), request), q.end());
        }
        request->id = -2;
        pthread_mutex_unlock(&request->rm_mutex);
        pthread_mutex_unlock(&rw_mutex);
    }

    void RequestWorker::cancel_request(Request *request) {
        pthread_mutex_lock(&rw_mutex);
        pthread_mutex_lock(&request->rm_mutex);
        request->rm_canceled = true;
        if (request->id <= -2); // cancel already called or request is done
        else if (request->id == -1) {
            // request is in queue, simply remove it from the queue
            auto q = request_queue;
            q.erase(std::remove(q.begin(), q.end(), request), q.end());
            request->id = -2;
            request->rm_finished = true;
            // wake up everyone waiting for the request
            pthread_cond_broadcast(&request->done_cv);
        } else if (request->id >= 0) {
            // don't fire request->done_cv here since fetch will return an error
            Task task;
            task.request = request;
            task.operation = Operation::CANCEL_REQUEST;
            work_queue.push_back(task);
        }
        pthread_mutex_unlock(&request->rm_mutex);
        // pthread_cond_signal(&rw_cv);
        pthread_mutex_unlock(&rw_mutex);
    }

    bool RequestWorker::dequeue_next(unsigned int to_id, bool lock_mutex) {
        if (lock_mutex) pthread_mutex_lock(&rw_mutex);
        if (request_queue.empty()) return false;
        printf("RequestWorker::dequeue_next: dequeue new request into id %u\n", to_id);
        Request *request = request_queue.back();
        request_queue.pop_back();
        active_requests[to_id] = request;
        pthread_mutex_lock(&request->rm_mutex);
        request->id = to_id;
        pthread_mutex_unlock(&request->rm_mutex);
        execute_request(to_id, false);
        if (lock_mutex) pthread_mutex_unlock(&rw_mutex);
        return true;
    }

    void RequestWorker::execute_request(unsigned int id, bool lock_mutex) {
        printf("RequestWorker::execute_request(%u)\n", id);
        if (lock_mutex) pthread_mutex_lock(&rw_mutex);
        Task task;
        task.request = active_requests[id];
        task.operation = Operation::START_REQUEST;
        work_queue.push_back(task);
        printf("RequestWorker::execute_request: added to queue, waking up worker\n");
        // pthread_cond_signal(&rw_cv);
        if (lock_mutex) pthread_mutex_unlock(&rw_mutex);
        printf("RequestWorker::execute_request: done\n");
    }

    TH_ENTRY_POINT void *RequestWorker::thread_worker_launcher(void *ctx) {
        js_worker_init(MAX_REQUEST_COUNT);
        printf("RequestWorker::thread_worker: js_worker_init done\n");
        emscripten_set_main_loop_arg(RequestWorker::thread_worker, ctx, 10, 0);
        return NULL;
    }

    void RequestWorker::thread_worker(void *ctx) {
        RequestWorker *self = (RequestWorker *)ctx;
        pthread_mutex_lock(&self->rw_mutex);
        if (!self->work_queue.size()) {
            // nothing to do
            pthread_mutex_unlock(&self->rw_mutex);
            return;
        }
        for (Task task : self->work_queue) {
            Request *request = task.request;
            pthread_mutex_lock(&request->rm_mutex);
            int id = request->id;
            printf("RequestWorker::thread_worker: task { operation=%i, id=%i }\n", task.operation, id);
            switch (task.operation) {
                case START_REQUEST:
                    printf("RequestWorker::thread_worker: handle START_REQUEST for id %i\n", id);
                    // request cancelled
                    if (request->id < 0) break;
                    printf("RequestWorker::thread_worker: adding request\n");
                    EM_ASM(Module.requestWorker.addRequest($0, $1), id, request->uri.c_str());
                    // add headers
                    printf("RequestWorker::thread_worker: adding headers\n");
                    for (auto header : request->headers) {
                        EM_ASM(Module.requestWorker.addHeader($0, $1, $2),
                            id, header.name.c_str(), header.value.c_str());
                    }
                    printf("RequestWorker::thread_worker: adding post fields\n");
                    // add form stuff
                    for (auto &field : request->post_fields) {
                        EM_ASM(Module.requestWorker.addFormData($0, $1, $2, $3),
                            id, field.first.c_str(), field.second.c_str(), field.second.size());
                    }
                    printf("RequestWorker::thread_worker: launching request\n");
                    // launch the request
                    EM_ASM(Module.requestWorker.runRequest($0), id);
                    break;
                case CANCEL_REQUEST:
                    // request already finished or cancelled
                    if (request->id < 0) break;
                    EM_ASM(Module.requestWorker.cancelRequest($0), id);
                    request->id = -2;
                    self->active_requests[id] = NULL;
                    self->active_requests_full = self->dequeue_next(id, false);
                    break;
            }
            pthread_mutex_unlock(&request->rm_mutex);
            self->work_queue.clear();
        }
        pthread_mutex_unlock(&self->rw_mutex);
    }

    void RequestWorker::evt_progress(unsigned int id, unsigned int current, unsigned int total) {
        pthread_mutex_lock(&rw_mutex);
        Request *request = active_requests[id];
        pthread_mutex_unlock(&rw_mutex);
        pthread_mutex_lock(&request->rm_mutex);
        request->rm_total = total;
        request->rm_done = current;
        pthread_mutex_unlock(&request->rm_mutex);
    }

    void RequestWorker::evt_finish(unsigned int id, int status, char *data, size_t length) {
        pthread_mutex_lock(&rw_mutex);
        Request *request = active_requests[id];
        pthread_mutex_lock(&request->rm_mutex);
        request->id = -2;
        active_requests[id] = NULL;
        active_requests_full = dequeue_next(id, false);
        pthread_mutex_unlock(&rw_mutex);
        request->status = status;
        request->response_body = ByteString(data, length);
        free(data);
        request->rm_finished = true;
        pthread_cond_broadcast(&request->done_cv);
        pthread_mutex_unlock(&request->rm_mutex);
    }

    void RequestWorker::evt_error(unsigned int id) {
        pthread_mutex_lock(&rw_mutex);
        Request *request = active_requests[id];
        pthread_mutex_lock(&request->rm_mutex);
        request->id = -2;
        active_requests[id] = NULL;
        active_requests_full = dequeue_next(id, false);
        pthread_mutex_unlock(&rw_mutex);
        request->status = 607;
        request->rm_finished = true;
        pthread_cond_broadcast(&request->done_cv);
        pthread_mutex_unlock(&request->rm_mutex);
    }
}

// helper functions to receive data from js
void rwProgress(unsigned int id, unsigned int current, unsigned int total) {
    http::RequestWorker::Ref().evt_progress(id, current, total);
}

void rwFinish(unsigned int id, int status, char* data, size_t length) {
    http::RequestWorker::Ref().evt_finish(id, status, data, length);
}

void rwError(unsigned int id) {
    http::RequestWorker::Ref().evt_error(id);
}
