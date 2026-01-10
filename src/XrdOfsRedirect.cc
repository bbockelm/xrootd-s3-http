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

#include "XrdOfsRedirect.hh"
#include "S3PresignedUrl.hh"
#include "logging.hh"
#include "shortfile.hh"
#include "stl_string_utils.hh"

#include <XProtocol/XProtocol.hh>
#include <XrdCl/XrdClDefaultEnv.hh>
#include <XrdCl/XrdClFileSystem.hh>
#include <XrdOuc/XrdOucEnv.hh>
#include <XrdOuc/XrdOucGatherConf.hh>
#include <XrdSec/XrdSecEntity.hh>
#include <XrdSfs/XrdSfsAio.hh>
#include <XrdSfs/XrdSfsFlags.hh>
#include <XrdVersion.hh>

#include <algorithm>
#include <charconv>
#include <errno.h>
#include <memory>

XrdVERSIONINFO(XrdSfsGetFileSystem, OfsRedirect);
XrdVERSIONINFO(XrdSfsGetFileSystem2, OfsRedirect);

using namespace XrdOfsRedirect;

FileSystem *FileSystem::m_instance = nullptr;

//------------------------------------------------------------------------------
// XrdSfsDirectory passthrough implementation
//------------------------------------------------------------------------------
XrdSfsDirectory *FileSystem::newDir(char *user, int monid) {
	if (!m_sfs_ptr)
		return nullptr;
	return m_sfs_ptr->newDir(user, monid);
}

//------------------------------------------------------------------------------
// XrdSfsFile factory - creates File wrapper
//------------------------------------------------------------------------------
XrdSfsFile *FileSystem::newFile(char *user, int monid) {
	if (!m_sfs_ptr)
		return nullptr;
	XrdSfsFile *chain_file = m_sfs_ptr->newFile(user, monid);
	if (chain_file) {
		std::unique_ptr<XrdSfsFile> chain_file_ptr(chain_file);
		return static_cast<XrdSfsFile *>(new File(
			user, std::move(chain_file_ptr), m_eroute, m_redirectConfigs));
	}
	return nullptr;
}

int FileSystem::chksum(csFunc Func, const char *csName, const char *path,
					   XrdOucErrInfo &eInfo, const XrdSecEntity *client,
					   const char *opaque) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->chksum(Func, csName, path, eInfo, client, opaque);
}

int FileSystem::chmod(const char *Name, XrdSfsMode Mode,
					  XrdOucErrInfo &out_error, const XrdSecEntity *client,
					  const char *opaque) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->chmod(Name, Mode, out_error, client, opaque);
}

void FileSystem::Connect(const XrdSecEntity *client) {
	if (m_sfs_ptr)
		m_sfs_ptr->Connect(client);
}

void FileSystem::Disc(const XrdSecEntity *client) {
	if (m_sfs_ptr)
		m_sfs_ptr->Disc(client);
}

void FileSystem::EnvInfo(XrdOucEnv *envP) {
	if (m_sfs_ptr)
		m_sfs_ptr->EnvInfo(envP);
}

int FileSystem::exists(const char *fileName, XrdSfsFileExistence &exists_flag,
					   XrdOucErrInfo &out_error, const XrdSecEntity *client,
					   const char *opaque) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->exists(fileName, exists_flag, out_error, client, opaque);
}

int FileSystem::FAttr(XrdSfsFACtl *faReq, XrdOucErrInfo &eInfo,
					  const XrdSecEntity *client) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->FAttr(faReq, eInfo, client);
}

int FileSystem::fsctl(const int cmd, const char *args, XrdOucErrInfo &out_error,
					  const XrdSecEntity *client) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->fsctl(cmd, args, out_error, client);
}

int FileSystem::getChkPSize() {
	if (!m_sfs_ptr)
		return 0;
	return m_sfs_ptr->getChkPSize();
}

int FileSystem::getStats(char *buff, int blen) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->getStats(buff, blen);
}

const char *FileSystem::getVersion() { return XrdVERSION; }

int FileSystem::gpFile(gpfFunc &gpAct, XrdSfsGPFile &gpReq,
					   XrdOucErrInfo &eInfo, const XrdSecEntity *client) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->gpFile(gpAct, gpReq, eInfo, client);
}

int FileSystem::mkdir(const char *dirName, XrdSfsMode Mode,
					  XrdOucErrInfo &out_error, const XrdSecEntity *client,
					  const char *opaque) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->mkdir(dirName, Mode, out_error, client, opaque);
}

int FileSystem::prepare(XrdSfsPrep &pargs, XrdOucErrInfo &out_error,
						const XrdSecEntity *client) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->prepare(pargs, out_error, client);
}

int FileSystem::rem(const char *path, XrdOucErrInfo &out_error,
					const XrdSecEntity *client, const char *info) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->rem(path, out_error, client, info);
}

int FileSystem::remdir(const char *dirName, XrdOucErrInfo &out_error,
					   const XrdSecEntity *client, const char *info) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->remdir(dirName, out_error, client, info);
}

int FileSystem::rename(const char *oldFileName, const char *newFileName,
					   XrdOucErrInfo &out_error, const XrdSecEntity *client,
					   const char *infoO, const char *infoN) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->rename(oldFileName, newFileName, out_error, client, infoO,
							 infoN);
}

int FileSystem::stat(const char *Name, struct stat *buf,
					 XrdOucErrInfo &out_error, const XrdSecEntity *client,
					 const char *opaque) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->stat(Name, buf, out_error, client, opaque);
}

int FileSystem::stat(const char *Name, mode_t &mode, XrdOucErrInfo &out_error,
					 const XrdSecEntity *client, const char *opaque) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->stat(Name, mode, out_error, client, opaque);
}

int FileSystem::truncate(const char *Name, XrdSfsFileOffset fileOffset,
						 XrdOucErrInfo &out_error, const XrdSecEntity *client,
						 const char *opaque) {
	if (!m_sfs_ptr)
		return SFS_ERROR;
	return m_sfs_ptr->truncate(Name, fileOffset, out_error, client, opaque);
}

//------------------------------------------------------------------------------
// FileSystem private methods
//------------------------------------------------------------------------------
FileSystem::FileSystem()
	: m_eroute(0), m_trace(&m_eroute), m_sfs_ptr(nullptr), m_initialized(false),
	  myVersion(nullptr) {}

FileSystem::~FileSystem() {}

void FileSystem::Initialize(FileSystem *&fs, XrdSfsFileSystem *native_fs,
							XrdSysLogger *lp, const char *config_file,
							XrdOucEnv *envP) {
	if (!m_instance) {
		m_instance = new FileSystem();
		m_instance->m_eroute.logger(lp);
		m_instance->m_eroute.Say(
			"Copr. 2025 Pelican Project, XrdOfsRedirect plugin v 1.0");
	}

	fs = m_instance;

	if (!m_instance->m_initialized) {
		if (m_instance->Configure(config_file, native_fs, envP) < 0) {
			m_instance->m_eroute.Emsg("Initialize",
									  "Failed to configure OfsRedirect plugin");
			fs = nullptr;
			return;
		}
		m_instance->m_initialized = true;
	}
}

const S3RedirectConfig *
FileSystem::findRedirectConfig(const std::string &path) const {
	// Find the longest matching prefix
	const S3RedirectConfig *bestMatch = nullptr;
	size_t bestMatchLen = 0;

	for (const auto &[prefix, config] : m_redirectConfigs) {
		if (path.compare(0, prefix.size(), prefix) == 0) {
			if (prefix.size() > bestMatchLen) {
				bestMatchLen = prefix.size();
				bestMatch = &config;
			}
		}
	}
	return bestMatch;
}

int FileSystem::Configure(const char *configfn, XrdSfsFileSystem *native_fs,
						  XrdOucEnv *envP) {
	m_sfs_ptr = native_fs;

	if (!configfn || !*configfn) {
		m_eroute.Emsg("Configure", "No configuration file specified");
		return -1;
	}

	XrdOucGatherConf ofsredirectConf("ofsredirect.", &m_eroute);
	int result;
	if ((result = ofsredirectConf.Gather(configfn,
										 XrdOucGatherConf::full_lines)) < 0) {
		m_eroute.Emsg("Configure", -result, "parsing config file", configfn);
		return -1;
	}

	char *temporary;
	std::string value;
	std::string attribute;
	S3RedirectConfig currentConfig;
	std::string currentPrefix;

	m_eroute.setMsgMask(0);

	while ((temporary = ofsredirectConf.GetLine())) {
		attribute = ofsredirectConf.GetToken();

		// Handle trace directive
		if (attribute == "ofsredirect.trace") {
			if (!XrdHTTPServer::ConfigLog(ofsredirectConf, m_eroute)) {
				m_eroute.Emsg("Configure", "Failed to configure log level");
			}
			continue;
		}

		// Get the value for the attribute
		temporary = ofsredirectConf.GetToken();
		if (!temporary) {
			continue;
		}
		value = temporary;

		if (attribute == "ofsredirect.path_name") {
			// Normalize paths so that they all start with /
			if (value[0] != '/') {
				currentPrefix = "/" + value;
			} else {
				currentPrefix = value;
			}
			currentConfig.prefix = currentPrefix;
		} else if (attribute == "ofsredirect.bucket_name") {
			currentConfig.accessInfo.setS3BucketName(value);
		} else if (attribute == "ofsredirect.service_name") {
			currentConfig.accessInfo.setS3ServiceName(value);
		} else if (attribute == "ofsredirect.region") {
			currentConfig.accessInfo.setS3Region(value);
		} else if (attribute == "ofsredirect.access_key_file") {
			currentConfig.accessInfo.setS3AccessKeyFile(value);
		} else if (attribute == "ofsredirect.secret_key_file") {
			currentConfig.accessInfo.setS3SecretKeyFile(value);
		} else if (attribute == "ofsredirect.service_url") {
			currentConfig.accessInfo.setS3ServiceUrl(value);
		} else if (attribute == "ofsredirect.url_style") {
			currentConfig.accessInfo.setS3UrlStyle(value);
		} else if (attribute == "ofsredirect.expiration_secs") {
			int expSecs;
			auto parseResult = std::from_chars(
				value.data(), value.data() + value.size(), expSecs);
			if (parseResult.ec != std::errc()) {
				m_eroute.Emsg("Configure",
							  "expiration_secs must be a positive number");
				return -1;
			}
			currentConfig.expirationSecs = expSecs;
		}
	}

	// Save the configuration if we have a valid prefix and service_url
	if (!currentPrefix.empty()) {
		if (currentConfig.accessInfo.getS3ServiceUrl().empty()) {
			m_eroute.Emsg("Configure",
						  "ofsredirect.service_url not specified for prefix",
						  currentPrefix.c_str());
			return -1;
		}
		// Verify key files are readable
		std::string contents;
		if (!currentConfig.accessInfo.getS3AccessKeyFile().empty()) {
			if (!readShortFile(currentConfig.accessInfo.getS3AccessKeyFile(),
							   contents)) {
				m_eroute.Emsg("Configure",
							  "access_key_file not readable for prefix",
							  currentPrefix.c_str());
				return -1;
			}
		}
		if (!currentConfig.accessInfo.getS3SecretKeyFile().empty()) {
			if (!readShortFile(currentConfig.accessInfo.getS3SecretKeyFile(),
							   contents)) {
				m_eroute.Emsg("Configure",
							  "secret_key_file not readable for prefix",
							  currentPrefix.c_str());
				return -1;
			}
		}

		m_redirectConfigs[currentPrefix] = currentConfig;
		m_eroute.Say("OfsRedirect: Configured redirect for prefix ",
					 currentPrefix.c_str());
	}

	if (m_redirectConfigs.empty()) {
		m_eroute.Say("OfsRedirect: No redirect configurations found, plugin "
					 "will passthrough all requests");
	}

	return 0;
}

//------------------------------------------------------------------------------
// File implementation
//------------------------------------------------------------------------------
File::File(const char *user, unique_sfs_ptr sfs, XrdSysError &eroute,
		   const std::map<std::string, S3RedirectConfig> &redirectConfigs)
	: XrdSfsFile(sfs->error), // Use underlying error object as ours
	  m_sfs(std::move(sfs)), // Guaranteed to be non-null by FileSystem::newFile
	  m_eroute(eroute), m_redirectConfigs(redirectConfigs) {}

File::~File() {}

int File::checkS3AndRedirect(const char *fileName, std::string &presignedUrl) {
	// Find matching configuration
	const S3RedirectConfig *config = nullptr;
	size_t bestMatchLen = 0;

	for (const auto &[prefix, cfg] : m_redirectConfigs) {
		std::string fileNameStr(fileName);
		if (fileNameStr.compare(0, prefix.size(), prefix) == 0) {
			if (prefix.size() > bestMatchLen) {
				bestMatchLen = prefix.size();
				config = &cfg;
			}
		}
	}

	if (!config) {
		// No matching redirect config, passthrough to underlying FS
		return 1;
	}

	// Extract the object name by removing the prefix
	std::string objectName;
	std::string fileNameStr(fileName);
	if (fileNameStr.size() > config->prefix.size()) {
		objectName = fileNameStr.substr(config->prefix.size());
		// Remove leading slash if present
		if (!objectName.empty() && objectName[0] == '/') {
			objectName = objectName.substr(1);
		}
	}

	if (objectName.empty()) {
		// Can't redirect to bucket root, passthrough
		return 1;
	}

	// Configure XrdClS3 with our credentials and settings
	auto env = XrdCl::DefaultEnv::GetEnv();
	if (!config->accessInfo.getS3AccessKeyFile().empty()) {
		env->PutString("XrdClS3AccessKeyLocation",
					   config->accessInfo.getS3AccessKeyFile());
	}
	if (!config->accessInfo.getS3SecretKeyFile().empty()) {
		env->PutString("XrdClS3SecretKeyLocation",
					   config->accessInfo.getS3SecretKeyFile());
	}
	if (!config->accessInfo.getS3Region().empty()) {
		env->PutString("XrdClS3Region", config->accessInfo.getS3Region());
	}
	if (!config->accessInfo.getS3UrlStyle().empty()) {
		env->PutString("XrdClS3UrlStyle", config->accessInfo.getS3UrlStyle());
	}

	// Build the S3 URL: s3://endpoint/bucket/object
	// Parse the service URL to extract the endpoint
	std::string serviceUrl = config->accessInfo.getS3ServiceUrl();
	std::string endpoint;

	// Remove the protocol prefix (https:// or http://)
	if (serviceUrl.find("https://") == 0) {
		endpoint = serviceUrl.substr(8);
	} else if (serviceUrl.find("http://") == 0) {
		endpoint = serviceUrl.substr(7);
	} else {
		endpoint = serviceUrl;
	}

	// Remove trailing slash if present
	if (!endpoint.empty() && endpoint.back() == '/') {
		endpoint.pop_back();
	}

	// Construct the S3 URL
	std::string s3Url = "s3://" + endpoint + "/" +
						config->accessInfo.getS3BucketName() + "/" + objectName;

	// Use XrdCl::FileSystem to check if the object exists
	XrdCl::FileSystem fs(s3Url);
	std::string statPath =
		"/" + config->accessInfo.getS3BucketName() + "/" + objectName;

	XrdCl::StatInfo *response = nullptr;
	auto st = fs.Stat(statPath, response, 30);

	if (response) {
		delete response;
	}

	if (!st.IsOK()) {
		// Check for NotFound error - in that case, passthrough to underlying FS
		if (st.errNo == kXR_NotFound) {
			return 1;
		}
		// For other errors (including "Operation not supported" when XrdClS3 is
		// not loaded), log a warning but continue to generate presigned URL and
		// let S3 decide
		std::string warnMsg = "XrdClS3 Stat failed for " + objectName + ": " +
							  st.ToString() +
							  " (proceeding with redirect anyway)";
		m_eroute.Emsg("checkS3AndRedirect", warnMsg.c_str());
	}

	// Object exists on S3, generate presigned URL
	if (!generatePresignedUrl(config->accessInfo, objectName,
							  config->expirationSecs, m_eroute, presignedUrl)) {
		m_eroute.Emsg("checkS3AndRedirect",
					  "Failed to generate presigned URL for",
					  objectName.c_str());
		return -1;
	}

	return 0;
}

int File::open(const char *fileName, XrdSfsFileOpenMode openMode,
			   mode_t createMode, const XrdSecEntity *client,
			   const char *opaque) {
	// First, let the underlying OFS handle the open (enforces security)
	int ofsResult = m_sfs->open(fileName, openMode, createMode, client, opaque);

	// If the OFS open failed, return that error
	if (ofsResult != SFS_OK) {
		return ofsResult;
	}

	// Only consider redirecting for read-only opens
	if ((openMode & (SFS_O_WRONLY | SFS_O_RDWR | SFS_O_CREAT | SFS_O_TRUNC)) ==
		0) {
		std::string presignedUrl;
		int result = checkS3AndRedirect(fileName, presignedUrl);

		if (result == 0) {
			// Close the underlying file since we're redirecting
			m_sfs->close();
			// Redirect to presigned URL
			// For HTTP redirects, use port -1 to indicate full URL redirect
			error.setErrInfo(-1, presignedUrl.c_str());
			return SFS_REDIRECT;
		} else if (result < 0) {
			// Error during S3 check - close the file and return error
			m_sfs->close();
			error.setErrInfo(EIO, "Failed to check S3 for object");
			return SFS_ERROR;
		}
		// result > 0 means passthrough - keep the file open
	}

	// File is already open from the OFS, return success
	return SFS_OK;
}

int File::close() { return m_sfs->close(); }

int File::checkpoint(cpAct act, struct iov *range, int n) {
	return m_sfs->checkpoint(act, range, n);
}

int File::fctl(const int cmd, const char *args, XrdOucErrInfo &out_error) {
	return m_sfs->fctl(cmd, args, out_error);
}

const char *File::FName() { return m_sfs->FName(); }

int File::getMmap(
	void **Addr,
	off_t &Size) { // We cannot monitor mmap-based reads, so we disable them.
	error.setErrInfo(ENOTSUP, "Mmap not supported by OfsRedirect plugin.");
	return SFS_ERROR;
}

XrdSfsXferSize File::pgRead(XrdSfsFileOffset offset, char *buffer,
							XrdSfsXferSize rdlen, uint32_t *csvec,
							uint64_t opts) {
	return m_sfs->pgRead(offset, buffer, rdlen, csvec, opts);
}

XrdSfsXferSize
File::pgRead(XrdSfsAio *aioparm,
			 uint64_t opts) { // Convert AIO to synchronous for passthrough
	aioparm->Result = this->pgRead((XrdSfsFileOffset)aioparm->sfsAio.aio_offset,
								   (char *)aioparm->sfsAio.aio_buf,
								   (XrdSfsXferSize)aioparm->sfsAio.aio_nbytes,
								   aioparm->cksVec, opts);
	aioparm->doneRead();
	return SFS_OK;
}

XrdSfsXferSize File::pgWrite(XrdSfsFileOffset offset, char *buffer,
							 XrdSfsXferSize rdlen, uint32_t *csvec,
							 uint64_t opts) {
	return m_sfs->pgWrite(offset, buffer, rdlen, csvec, opts);
}

XrdSfsXferSize File::pgWrite(XrdSfsAio *aioparm, uint64_t opts) {
	return m_sfs->pgWrite(aioparm, opts);
}

int File::read(XrdSfsFileOffset fileOffset, XrdSfsXferSize amount) {
	return m_sfs->read(fileOffset, amount);
}

XrdSfsXferSize File::read(XrdSfsFileOffset fileOffset, char *buffer,
						  XrdSfsXferSize buffer_size) {
	return m_sfs->read(fileOffset, buffer, buffer_size);
}

int File::read(XrdSfsAio *aioparm) { return m_sfs->read(aioparm); }

XrdSfsXferSize File::write(XrdSfsFileOffset fileOffset, const char *buffer,
						   XrdSfsXferSize buffer_size) {
	return m_sfs->write(fileOffset, buffer, buffer_size);
}

int File::write(XrdSfsAio *aioparm) { return m_sfs->write(aioparm); }

int File::sync() { return m_sfs->sync(); }

int File::sync(XrdSfsAio *aiop) { return m_sfs->sync(aiop); }

int File::stat(struct stat *buf) { return m_sfs->stat(buf); }

int File::truncate(XrdSfsFileOffset fileOffset) {
	return m_sfs->truncate(fileOffset);
}

int File::getCXinfo(char cxtype[4], int &cxrsz) {
	return m_sfs->getCXinfo(cxtype, cxrsz);
}

int File::SendData(XrdSfsDio *sfDio, XrdSfsFileOffset offset,
				   XrdSfsXferSize size) {
	return m_sfs->SendData(sfDio, offset, size);
}

XrdSfsFileSystem *XrdSfsGetFileSystem_Internal(XrdSfsFileSystem *native_fs,
											   XrdSysLogger *lp,
											   const char *configfn,
											   XrdOucEnv *envP) {
	FileSystem *fs = nullptr;
	FileSystem::Initialize(fs, native_fs, lp, configfn, envP);
	return fs;
}

extern "C" {

XrdSfsFileSystem *XrdSfsGetFileSystem(XrdSfsFileSystem *native_fs,
									  XrdSysLogger *lp, const char *configfn) {
	return XrdSfsGetFileSystem_Internal(native_fs, lp, configfn, nullptr);
}

XrdSfsFileSystem *XrdSfsGetFileSystem2(XrdSfsFileSystem *native_fs,
									   XrdSysLogger *lp, const char *configfn,
									   XrdOucEnv *envP) {
	return XrdSfsGetFileSystem_Internal(native_fs, lp, configfn, envP);
}
}
