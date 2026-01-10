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

#ifndef __XRDOFSREDIRECT_HH_
#define __XRDOFSREDIRECT_HH_

#include "S3AccessInfo.hh"

#include <XrdOuc/XrdOucTrace.hh>
#include <XrdSfs/XrdSfsInterface.hh>
#include <XrdSys/XrdSysError.hh>
#include <XrdVersion.hh>

#include <map>
#include <memory>
#include <string>

class XrdOucEnv;
class XrdSysLogger;

// Forward declaration of plugin entry point (global scope)
XrdSfsFileSystem *XrdSfsGetFileSystem_Internal(XrdSfsFileSystem *,
											   XrdSysLogger *, const char *,
											   XrdOucEnv *);

namespace XrdOfsRedirect {

// Forward declarations
class FileSystem;
class File;

using unique_sfs_ptr = std::unique_ptr<XrdSfsFile>;

// Configuration for an S3 redirect endpoint
struct S3RedirectConfig {
	std::string prefix;		  // URL path prefix to match
	S3AccessInfo accessInfo;  // S3 access information
	int expirationSecs{3600}; // Default 1 hour expiration for presigned URLs
};

class File final : public XrdSfsFile {

	friend class FileSystem;

  public:
	virtual int open(const char *fileName, XrdSfsFileOpenMode openMode,
					 mode_t createMode, const XrdSecEntity *client,
					 const char *opaque = 0) override;

	virtual int close() override;

	virtual int checkpoint(cpAct act, struct iov *range = 0,
						   int n = 0) override;

	using XrdSfsFile::fctl;
	virtual int fctl(const int cmd, const char *args,
					 XrdOucErrInfo &out_error) override;

	virtual const char *FName() override;

	virtual int getMmap(void **Addr, off_t &Size) override;

	virtual XrdSfsXferSize pgRead(XrdSfsFileOffset offset, char *buffer,
								  XrdSfsXferSize rdlen, uint32_t *csvec,
								  uint64_t opts = 0) override;

	virtual XrdSfsXferSize pgRead(XrdSfsAio *aioparm,
								  uint64_t opts = 0) override;

	virtual XrdSfsXferSize pgWrite(XrdSfsFileOffset offset, char *buffer,
								   XrdSfsXferSize rdlen, uint32_t *csvec,
								   uint64_t opts = 0) override;

	virtual XrdSfsXferSize pgWrite(XrdSfsAio *aioparm,
								   uint64_t opts = 0) override;

	virtual int read(XrdSfsFileOffset fileOffset, // Preread only
					 XrdSfsXferSize amount) override;

	virtual XrdSfsXferSize read(XrdSfsFileOffset fileOffset, char *buffer,
								XrdSfsXferSize buffer_size) override;

	virtual int read(XrdSfsAio *aioparm) override;

	virtual XrdSfsXferSize write(XrdSfsFileOffset fileOffset,
								 const char *buffer,
								 XrdSfsXferSize buffer_size) override;

	virtual int write(XrdSfsAio *aioparm) override;

	virtual int sync() override;

	virtual int sync(XrdSfsAio *aiop) override;

	virtual int stat(struct stat *buf) override;

	virtual int truncate(XrdSfsFileOffset fileOffset) override;

	virtual int getCXinfo(char cxtype[4], int &cxrsz) override;

	virtual int SendData(XrdSfsDio *sfDio, XrdSfsFileOffset offset,
						 XrdSfsXferSize size) override;

  private:
	File(const char *, unique_sfs_ptr, XrdSysError &,
		 const std::map<std::string, S3RedirectConfig> &redirectConfigs);

	virtual ~File();

	// Check if object exists on S3 and generate redirect URL if it does
	// Returns: 0 if redirect should happen (presignedUrl is set)
	//          > 0 if passthrough to underlying FS (object not on S3)
	//          < 0 on error
	int checkS3AndRedirect(const char *fileName, std::string &presignedUrl);

	bool m_is_open{false};
	std::unique_ptr<XrdSfsFile> m_sfs;
	XrdSysError &m_eroute;
	const std::map<std::string, S3RedirectConfig> &m_redirectConfigs;
};

class FileSystem final : public XrdSfsFileSystem {

	friend XrdSfsFileSystem * ::XrdSfsGetFileSystem_Internal(XrdSfsFileSystem *,
															 XrdSysLogger *,
															 const char *,
															 XrdOucEnv *);

  public:
	virtual XrdSfsDirectory *newDir(char *user = 0, int monid = 0) override;

	virtual XrdSfsFile *newFile(char *user = 0, int monid = 0) override;

	virtual int chksum(csFunc Func, const char *csName, const char *path,
					   XrdOucErrInfo &eInfo, const XrdSecEntity *client = 0,
					   const char *opaque = 0) override;

	virtual int chmod(const char *Name, XrdSfsMode Mode,
					  XrdOucErrInfo &out_error, const XrdSecEntity *client,
					  const char *opaque = 0) override;

	virtual void Connect(const XrdSecEntity *client = 0) override;

	virtual void Disc(const XrdSecEntity *client = 0) override;

	virtual void EnvInfo(XrdOucEnv *envP) override;

	virtual int exists(const char *fileName, XrdSfsFileExistence &exists_flag,
					   XrdOucErrInfo &out_error, const XrdSecEntity *client,
					   const char *opaque = 0) override;

	virtual int FAttr(XrdSfsFACtl *faReq, XrdOucErrInfo &eInfo,
					  const XrdSecEntity *client = 0) override;

	virtual int fsctl(const int cmd, const char *args, XrdOucErrInfo &out_error,
					  const XrdSecEntity *client) override;

	virtual int getChkPSize() override;

	virtual int getStats(char *buff, int blen) override;

	virtual const char *getVersion() override;

	virtual int gpFile(gpfFunc &gpAct, XrdSfsGPFile &gpReq,
					   XrdOucErrInfo &eInfo,
					   const XrdSecEntity *client = 0) override;

	virtual int mkdir(const char *dirName, XrdSfsMode Mode,
					  XrdOucErrInfo &out_error, const XrdSecEntity *client,
					  const char *opaque = 0) override;

	virtual int prepare(XrdSfsPrep &pargs, XrdOucErrInfo &out_error,
						const XrdSecEntity *client = 0) override;

	virtual int rem(const char *path, XrdOucErrInfo &out_error,
					const XrdSecEntity *client, const char *info = 0) override;

	virtual int remdir(const char *dirName, XrdOucErrInfo &out_error,
					   const XrdSecEntity *client,
					   const char *info = 0) override;

	virtual int rename(const char *oldFileName, const char *newFileName,
					   XrdOucErrInfo &out_error, const XrdSecEntity *client,
					   const char *infoO = 0, const char *infoN = 0) override;

	virtual int stat(const char *Name, struct stat *buf,
					 XrdOucErrInfo &out_error, const XrdSecEntity *client,
					 const char *opaque = 0) override;

	virtual int stat(const char *Name, mode_t &mode, XrdOucErrInfo &out_error,
					 const XrdSecEntity *client,
					 const char *opaque = 0) override;

	virtual int truncate(const char *Name, XrdSfsFileOffset fileOffset,
						 XrdOucErrInfo &out_error,
						 const XrdSecEntity *client = 0,
						 const char *opaque = 0) override;

  private:
	static void Initialize(FileSystem *&fs, XrdSfsFileSystem *native_fs,
						   XrdSysLogger *lp, const char *config_file,
						   XrdOucEnv *envP);

	int Configure(const char *configfn, XrdSfsFileSystem *native_fs,
				  XrdOucEnv *envP);

	// Find matching S3 redirect config for a path
	const S3RedirectConfig *findRedirectConfig(const std::string &path) const;

	FileSystem();

	virtual ~FileSystem();

	static FileSystem *m_instance;
	XrdSysError m_eroute;
	XrdOucTrace m_trace;
	XrdSfsFileSystem *m_sfs_ptr;
	bool m_initialized;
	XrdVersionInfo *myVersion;

	// Map of path prefix -> S3 redirect configuration
	std::map<std::string, S3RedirectConfig> m_redirectConfigs;
};

} // namespace XrdOfsRedirect

#endif // __XRDOFSREDIRECT_HH_
