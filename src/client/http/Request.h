#ifndef REQUEST_H
#define REQUEST_H

#include <map>
#include "common/tpt-minmax.h" // for MSVC, ensures windows.h doesn't cause compile errors by defining min/max
#include "common/tpt-thread.h"
// #include <curl/curl.h>
#include "common/String.h"

// stupid circular dependencies
namespace http { class Request; };
#include "RequestWorker.h"
#undef GetUserName // pthreads defines this, breaks stuff

/*
#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7, 55, 0)
# define REQUEST_USE_CURL_OFFSET_T
#endif

#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7, 56, 0)
# define REQUEST_USE_CURL_MIMEPOST
#endif
*/

namespace http
{
	// class RequestManager;
	class Request
	{
		struct Header {
			ByteString name;
			ByteString value;
		};

		ByteString uri;
		ByteString response_body;

		// CURL *easy;
		// char error_buffer[CURL_ERROR_SIZE];

		volatile uint32_t rm_total;
		volatile uint32_t rm_done;
		volatile bool rm_finished;
		volatile bool rm_canceled;
		volatile bool rm_started;
		pthread_mutex_t rm_mutex;
		pthread_cond_t done_cv;

		// used to identify this specific request across the c++/js boundary
		// -2 means not added (or removed) to RequestWorker
		// -1 means queued
		// >= 0 means currently processing
		uint32_t id;
		int status;

		std::vector<Header> headers;
		std::map<ByteString, ByteString> post_fields;

/*
#ifdef REQUEST_USE_CURL_MIMEPOST
		curl_mime *post_fields;
#else
		curl_httppost *post_fields_first, *post_fields_last;
		std::map<ByteString, ByteString> post_fields_map;
#endif
*/

		// static size_t WriteDataHandler(char * ptr, size_t size, size_t count, void * userdata);

	public:
		Request(ByteString uri);
		virtual ~Request();

		void AddHeader(ByteString name, ByteString value);
		void AddPostData(std::map<ByteString, ByteString> data);
		void AuthHeaders(ByteString ID, ByteString session);

		void Start();
		ByteString Finish(int *status);
		void Cancel();

		void CheckProgress(int *total, int *done);
		bool CheckDone();
		bool CheckCanceled();
		bool CheckStarted();

		friend class RequestWorker;

		static ByteString Simple(ByteString uri, int *status, std::map<ByteString, ByteString> post_data = std::map<ByteString, ByteString>{});
		static ByteString SimpleAuth(ByteString uri, int *status, ByteString ID, ByteString session, std::map<ByteString, ByteString> post_data = std::map<ByteString, ByteString>{});
	};

	String StatusText(int code);

	// extern const long timeout;
	// extern ByteString proxy;
	// extern ByteString user_agent;
}

#endif // REQUEST_H
