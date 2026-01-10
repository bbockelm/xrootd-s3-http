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

#include "PfcS3.hh"
#include "CinfoReader.hh"
#include "logging.hh"

#include <XrdCl/XrdClFile.hh>
#include <XrdOuc/XrdOucEnv.hh>
#include <XrdOuc/XrdOucGatherConf.hh>
#include <XrdSec/XrdSecEntity.hh>
#include <XrdSys/XrdSysError.hh>
#include <XrdVersion.hh>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>

using namespace XrdHTTPServer;

// Static member initialization
std::once_flag PfcS3FileSystem::m_thread_launch;
std::mutex PfcS3FileSystem::m_shutdown_lock;
std::condition_variable PfcS3FileSystem::m_shutdown_requested_cv;
std::condition_variable PfcS3FileSystem::m_shutdown_complete_cv;
bool PfcS3FileSystem::m_shutdown_requested = false;
bool PfcS3FileSystem::m_shutdown_complete = true;

//------------------------------------------------------------------------------
// PfcS3FileSystem implementation
//------------------------------------------------------------------------------

PfcS3FileSystem::PfcS3FileSystem(XrdOss *oss, std::unique_ptr<XrdSysError> log,
								 const char *configName, XrdOucEnv *envP)
	: XrdOssWrapper(*oss), m_oss(oss), m_log(std::move(log)) {
	if (!Config(configName)) {
		m_log->Emsg("Initialize", "Failed to configure the PfcS3 layer");
		throw std::runtime_error("Failed to configure the PfcS3 layer");
	}

	// Start upload threads
	std::call_once(m_thread_launch, [this] {
		{
			std::unique_lock lock(m_shutdown_lock);
			if (m_shutdown_requested) {
				m_log->Emsg("Initialize",
							"PfcS3 upload threads already requested shutdown");
				return;
			}
			m_shutdown_complete = false;
		}

		for (int i = 0; i < kNumUploadThreads; ++i) {
			m_upload_threads.emplace_back(PfcS3FileSystem::UploadThread, this);
		}
	});

	m_log->Emsg("Initialize", "PfcS3FileSystem initialized");
}

PfcS3FileSystem::PfcS3FileSystem(XrdOss *oss, std::unique_ptr<XrdSysError> log,
								 LogMask log_mask)
	: XrdOssWrapper(*oss), m_oss(oss), m_log(std::move(log)) {
	m_log->setMsgMask(log_mask);
	m_log->Emsg("Initialize", "PfcS3FileSystem initialized (test mode)");
}

PfcS3FileSystem::~PfcS3FileSystem() {
	// Signal threads to stop (handled by static Shutdown())
}

void PfcS3FileSystem::Shutdown() {
	{
		std::unique_lock lock(m_shutdown_lock);
		if (m_shutdown_complete || m_shutdown_requested) {
			return;
		}
		m_shutdown_requested = true;
	}
	m_shutdown_requested_cv.notify_all();

	// Wait for threads to finish
	std::unique_lock lock(m_shutdown_lock);
	m_shutdown_complete_cv.wait(lock, [] { return m_shutdown_complete; });
}

bool PfcS3FileSystem::Config(const char *configfn) {
	m_log->setMsgMask(LogMask::Error | LogMask::Warning);

	XrdOucGatherConf pfcs3Conf("pfcs3.", m_log.get());
	int result;
	if ((result = pfcs3Conf.Gather(configfn, XrdOucGatherConf::full_lines)) <
		0) {
		m_log->Emsg("Config", -result, "parsing config file", configfn);
		return false;
	}

	char *temporary;
	std::string value;
	std::string attribute;
	S3Config currentConfig;

	while ((temporary = pfcs3Conf.GetLine())) {
		attribute = pfcs3Conf.GetToken();

		if (attribute == "pfcs3.trace") {
			m_log->setMsgMask(0);
			if (!XrdHTTPServer::ConfigLog(pfcs3Conf, *m_log)) {
				m_log->Emsg("Config", "Failed to configure the log level");
			}
			continue;
		}

		temporary = pfcs3Conf.GetToken();

		if (attribute == "pfcs3.end") {
			// Finalize the current S3 configuration
			if (currentConfig.s3_url.empty()) {
				m_log->Emsg("Config",
							"pfcs3.end without pfcs3.s3_url specified");
				return false;
			}
			if (currentConfig.path_prefix.empty()) {
				m_log->Emsg("Config",
							"pfcs3.end without pfcs3.path_prefix specified");
				return false;
			}

			m_s3_configs.push_back(currentConfig);
			std::string msg =
				"Configured S3 upload: " + currentConfig.path_prefix + " -> " +
				currentConfig.s3_url;
			m_log->Emsg("Config", msg.c_str());
			currentConfig = S3Config();
			continue;
		}

		if (!temporary) {
			continue;
		}
		value = temporary;

		if (attribute == "pfcs3.s3_url") {
			currentConfig.s3_url = value;
			// Ensure URL ends with /
			if (!currentConfig.s3_url.empty() &&
				currentConfig.s3_url.back() != '/') {
				currentConfig.s3_url += '/';
			}
		} else if (attribute == "pfcs3.path_prefix") {
			// Normalize paths so they all start with /
			if (value[0] != '/') {
				currentConfig.path_prefix = "/" + value;
			} else {
				currentConfig.path_prefix = value;
			}
			// Ensure prefix ends with /
			if (!currentConfig.path_prefix.empty() &&
				currentConfig.path_prefix.back() != '/') {
				currentConfig.path_prefix += '/';
			}
		}
	}

	if (m_s3_configs.empty()) {
		m_log->Log(LogMask::Warning, "Config",
				   "No S3 upload configurations found - plugin will be "
				   "pass-through only");
	}

	return true;
}

XrdOssDF *PfcS3FileSystem::newDir(const char *user) {
	auto underlying = wrapPI.newDir(user);
	if (!underlying) {
		return nullptr;
	}
	return new PfcS3Dir(std::unique_ptr<XrdOssDF>(underlying), *m_log);
}

XrdOssDF *PfcS3FileSystem::newFile(const char *user) {
	auto underlying = wrapPI.newFile(user);
	if (!underlying) {
		return nullptr;
	}
	return new PfcS3File(std::unique_ptr<XrdOssDF>(underlying), *m_log, *this);
}

std::string PfcS3FileSystem::BuildS3Url(const std::string &local_path) const {
	// Find the matching configuration
	for (const auto &config : m_s3_configs) {
		if (local_path.compare(0, config.path_prefix.size(),
							   config.path_prefix) == 0) {
			// Extract the object name (path after the prefix)
			std::string object = local_path.substr(config.path_prefix.size());
			return config.s3_url + object;
		}
	}
	return "";
}

bool PfcS3FileSystem::QueueUploadIfComplete(const std::string &data_path) {
	std::string cinfo_path = data_path + CinfoReader::kCinfoExtension;

	// Check if cinfo file indicates completion
	long long file_size;
	if (!CinfoReader::IsComplete(cinfo_path, file_size)) {
		m_log->Log(LogMask::Debug, "QueueUpload",
				   "File not complete:", data_path.c_str());
		return false;
	}

	// Build S3 URL from local path
	std::string s3_url = BuildS3Url(data_path);
	if (s3_url.empty()) {
		m_log->Log(LogMask::Warning, "QueueUpload",
				   "No S3 configuration for path:", data_path.c_str());
		return false;
	}

	// Create upload task
	UploadTask task;
	task.data_path = data_path;
	task.s3_url = s3_url;
	task.file_size = file_size;

	{
		std::lock_guard<std::mutex> lock(m_queue_mutex);
		m_upload_queue.push(std::move(task));
	}
	m_queue_cv.notify_one();

	std::string upload_msg =
		"Queued file for S3 upload: " + data_path + " -> " + s3_url;
	m_log->Log(LogMask::Info, "QueueUpload", upload_msg.c_str());
	return true;
}

void PfcS3FileSystem::UploadThread(PfcS3FileSystem *fs) {
	fs->ProcessUploads();
}

void PfcS3FileSystem::ProcessUploads() {
	while (true) {
		UploadTask task;

		{
			std::unique_lock<std::mutex> lock(m_queue_mutex);
			m_queue_cv.wait(lock, [this] {
				return !m_upload_queue.empty() || m_shutdown_requested;
			});

			if (m_shutdown_requested && m_upload_queue.empty()) {
				break;
			}

			if (!m_upload_queue.empty()) {
				task = std::move(m_upload_queue.front());
				m_upload_queue.pop();
			}
		}

		if (!task.data_path.empty()) {
			if (UploadToS3(task)) {
				m_log->Log(LogMask::Info, "Upload",
						   "Successfully uploaded to S3:", task.s3_url.c_str());
			} else {
				m_log->Log(LogMask::Error, "Upload",
						   "Failed to upload to S3:", task.s3_url.c_str());
			}
		}
	}

	// Signal completion
	{
		std::lock_guard<std::mutex> lock(m_shutdown_lock);
		m_shutdown_complete = true;
	}
	m_shutdown_complete_cv.notify_all();
}

bool PfcS3FileSystem::UploadToS3(const UploadTask &task) {
	// Open the local data file
	int fd = open(task.data_path.c_str(), O_RDONLY);
	if (fd < 0) {
		m_log->Log(LogMask::Error, "Upload",
				   "Failed to open data file:", task.data_path.c_str(),
				   strerror(errno));
		return false;
	}

	// RAII for file descriptor
	struct FdGuard {
		int fd;
		~FdGuard() {
			if (fd >= 0)
				close(fd);
		}
	} guard{fd};

	// Read the file content
	std::vector<char> buffer(task.file_size);
	ssize_t total_read = 0;
	while (total_read < task.file_size) {
		ssize_t n =
			read(fd, buffer.data() + total_read, task.file_size - total_read);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			m_log->Log(LogMask::Error, "Upload",
					   "Failed to read data file:", task.data_path.c_str(),
					   strerror(errno));
			return false;
		}
		if (n == 0)
			break; // EOF
		total_read += n;
	}

	// Use XrdCl::File to upload to S3
	// The XrdClS3 plugin handles s3:// URLs automatically
	XrdCl::File s3File;

	// Set the expected file size for the upload
	std::string sizeStr = std::to_string(total_read);
	s3File.SetProperty("DataSize", sizeStr);

	// Open the S3 file for writing
	auto status = s3File.Open(task.s3_url, XrdCl::OpenFlags::Write);
	if (!status.IsOK()) {
		m_log->Log(LogMask::Error, "Upload",
				   "Failed to open S3 URL:", task.s3_url.c_str(),
				   status.GetErrorMessage().c_str());
		return false;
	}

	// Write the data
	status = s3File.Write(0, total_read, buffer.data());
	if (!status.IsOK()) {
		std::string err_msg = "Failed to write to S3: " + task.s3_url + " - " +
							  status.GetErrorMessage();
		m_log->Log(LogMask::Error, "Upload", err_msg.c_str());
		auto closeStatus = s3File.Close();
		(void)closeStatus;
		return false;
	}

	// Close the file
	status = s3File.Close();
	if (!status.IsOK()) {
		m_log->Log(LogMask::Error, "Upload",
				   "Failed to close S3 file:", task.s3_url.c_str(),
				   status.GetErrorMessage().c_str());
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
// PfcS3File implementation
//------------------------------------------------------------------------------

PfcS3File::~PfcS3File() {}

int PfcS3File::Open(const char *path, int Oflag, mode_t Mode, XrdOucEnv &env) {
	m_path = path;
	m_oflags = Oflag;
	return wrapDF.Open(path, Oflag, Mode, env);
}

int PfcS3File::Close(long long *retsz) {
	int result = wrapDF.Close(retsz);

	// Only consider upload if file was opened for reading
	// (PFC opens cached files for reading when serving to clients)
	if (result == 0 && !m_path.empty()) {
		// Check if this is a cinfo file - skip those
		if (m_path.find(CinfoReader::kCinfoExtension) == std::string::npos) {
			m_pfcs3_fs.QueueUploadIfComplete(m_path);
		}
	}

	return result;
}

//------------------------------------------------------------------------------
// PfcS3Dir implementation
//------------------------------------------------------------------------------

PfcS3Dir::~PfcS3Dir() {}

//------------------------------------------------------------------------------
// Plugin entry point
//------------------------------------------------------------------------------

extern "C" {

XrdVERSIONINFO(XrdOssAddStorageSystem2, PfcS3);

XrdOss *XrdOssAddStorageSystem2(XrdOss *curr_oss, XrdSysLogger *logger,
								const char *config_fn, const char *parms,
								XrdOucEnv *envP) {

	std::unique_ptr<XrdSysError> log(new XrdSysError(logger, "pfcs3_"));
	try {
		return new PfcS3FileSystem(curr_oss, std::move(log), config_fn, envP);
	} catch (std::runtime_error &re) {
		XrdSysError tmp_log(logger, "pfcs3_");
		tmp_log.Emsg("Initialize",
					 "Encountered a runtime failure when initializing the "
					 "PfcS3 filesystem:",
					 re.what());
		return nullptr;
	}
}
}
