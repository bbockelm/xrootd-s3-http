/***************************************************************
 *
 * Copyright (C) 2025, Pelican Project, Morgridge Institute for Research
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/

#pragma once

#include "logging.hh"

#include <XrdCl/XrdClFile.hh>
#include <XrdOss/XrdOssWrapper.hh>
#include <XrdSec/XrdSecEntity.hh>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Forward declarations
class XrdSysError;
class XrdOucEnv;

/**
 * PfcS3FileSystem - An OSS stacking plugin that uploads completed cached
 * files to S3.
 *
 * This plugin is intended to be used with XRootD's Proxy File Cache (PFC).
 * It intercepts file close operations and, if the corresponding .cinfo file
 * indicates the cached file is complete, uploads the file to S3.
 *
 * Configuration:
 *   pfc.osslib /path/to/libXrdOssPfcS3.so
 *
 *   pfcs3.trace [all|error|warning|info|debug|none]
 *   pfcs3.s3_url <s3_service_url>  (e.g., s3://mybucket.s3.amazonaws.com)
 *   pfcs3.path_prefix <local_path_prefix>
 *   pfcs3.end
 *
 * The plugin uses background threads to perform S3 uploads without blocking
 * the close operation. It uses XrdClS3 (from xrdcl-pelican) for the actual
 * S3 upload operations.
 */
class PfcS3FileSystem final : public XrdOssWrapper {
  public:
	PfcS3FileSystem(XrdOss *oss, std::unique_ptr<XrdSysError> log,
					const char *configName, XrdOucEnv *envP);

	// Constructor for unit testing
	PfcS3FileSystem(XrdOss *oss, std::unique_ptr<XrdSysError> log,
					XrdHTTPServer::LogMask log_mask);

	virtual ~PfcS3FileSystem();

	bool Config(const char *configfn);

	XrdOssDF *newDir(const char *user = 0) override;
	XrdOssDF *newFile(const char *user = 0) override;

	/**
	 * Queue a file for upload to S3 if its cinfo file indicates completion.
	 *
	 * @param data_path Path to the cached data file (without .cinfo extension)
	 * @return true if the file was queued for upload, false otherwise
	 */
	bool QueueUploadIfComplete(const std::string &data_path);

	/**
	 * Build an S3 URL for the given local path.
	 *
	 * @param local_path The local filesystem path
	 * @return The corresponding S3 URL, or empty string if no match
	 */
	std::string BuildS3Url(const std::string &local_path) const;

	XrdSysError &getLog() { return *m_log; }

  private:
	struct UploadTask {
		std::string data_path; // Path to the local data file
		std::string s3_url;	   // S3 URL to upload to
		long long file_size;
	};

	// S3 upload configuration
	struct S3Config {
		std::string s3_url;		 // Base S3 URL (e.g., s3://bucket.endpoint)
		std::string path_prefix; // Local path prefix to match
	};

	// Background upload thread
	static void UploadThread(PfcS3FileSystem *fs);
	void ProcessUploads();

	// Perform the actual S3 upload
	bool UploadToS3(const UploadTask &task);

	// Invoked on shutdown to clean up background threads
	static void Shutdown() __attribute__((destructor));

	XrdOss *m_oss;
	std::unique_ptr<XrdSysError> m_log;

	// S3 configurations: multiple mappings from local path prefixes to S3 URLs
	std::vector<S3Config> m_s3_configs;

	// Upload queue and thread management
	std::queue<UploadTask> m_upload_queue;
	std::mutex m_queue_mutex;
	std::condition_variable m_queue_cv;
	std::vector<std::thread> m_upload_threads;
	static constexpr int kNumUploadThreads = 4;

	// Shutdown coordination
	static std::once_flag m_thread_launch;
	static std::mutex m_shutdown_lock;
	static std::condition_variable m_shutdown_requested_cv;
	static std::condition_variable m_shutdown_complete_cv;
	static bool m_shutdown_requested;
	static bool m_shutdown_complete;
};

/**
 * PfcS3File - Wrapper for XrdOssDF that intercepts Close() to trigger
 * S3 uploads.
 */
class PfcS3File final : public XrdOssWrapDF {
  public:
	PfcS3File(std::unique_ptr<XrdOssDF> wrapDF, XrdSysError &log,
			  PfcS3FileSystem &pfcs3_fs)
		: XrdOssWrapDF(*wrapDF), m_wrapped(std::move(wrapDF)), m_log(log),
		  m_pfcs3_fs(pfcs3_fs) {}

	virtual ~PfcS3File();

	virtual int Open(const char *path, int Oflag, mode_t Mode,
					 XrdOucEnv &env) override;

	virtual int Close(long long *retsz = 0) override;

  private:
	std::unique_ptr<XrdOssDF> m_wrapped;
	XrdSysError &m_log;
	PfcS3FileSystem &m_pfcs3_fs;
	std::string m_path; // Path of the opened file
	int m_oflags{0};	// Open flags
};

/**
 * PfcS3Dir - Simple directory wrapper (pass-through).
 */
class PfcS3Dir final : public XrdOssWrapDF {
  public:
	PfcS3Dir(std::unique_ptr<XrdOssDF> wrapDF, XrdSysError &log)
		: XrdOssWrapDF(*wrapDF), m_wrapped(std::move(wrapDF)), m_log(log) {}

	virtual ~PfcS3Dir();

  private:
	std::unique_ptr<XrdOssDF> m_wrapped;
	XrdSysError &m_log;
};
