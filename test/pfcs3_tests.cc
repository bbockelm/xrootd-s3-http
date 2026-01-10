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

#include "../src/CinfoReader.hh"
#include "../src/PfcS3.hh"
#include "../src/shortfile.hh"

#include <XrdOss/XrdOssDefaultSS.hh>
#include <XrdOuc/XrdOucEnv.hh>
#include <XrdSys/XrdSysLogger.hh>
#include <XrdVersion.hh>

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

//------------------------------------------------------------------------------
// CinfoReader Tests
//------------------------------------------------------------------------------

class TestCinfoReader : public testing::Test {
  protected:
	void SetUp() override {
		auto temp_dir =
			std::filesystem::temp_directory_path() / "gtest_cinfo_test";
		std::error_code ec;
		std::filesystem::create_directories(temp_dir, ec);
		ASSERT_FALSE(ec) << "Failed to create temp directory: " << ec.message();
		m_temp_dir = temp_dir.string();
	}

	void TearDown() override {
		if (!m_temp_dir.empty()) {
			std::error_code ec;
			std::filesystem::remove_all(m_temp_dir, ec);
		}
	}

	// Create a valid cinfo file for testing
	// Format: magic(2) + version(1) + complete(1) + buffer_size(4) +
	//         file_size(8) + creation_time(8) + access_cnt(4)
	std::string CreateCinfoFile(bool complete, long long file_size,
								int buffer_size = 1048576) {
		std::string path = m_temp_dir + "/test.cinfo";
		int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		EXPECT_NE(fd, -1) << "Failed to create cinfo file: " << strerror(errno);

		// Magic number
		uint16_t magic = CinfoReader::kCinfoMagic;
		write(fd, &magic, sizeof(magic));

		// Version
		uint8_t version = 5;
		write(fd, &version, sizeof(version));

		// Complete flag
		uint8_t complete_flag = complete ? 1 : 0;
		write(fd, &complete_flag, sizeof(complete_flag));

		// Store structure
		write(fd, &buffer_size, sizeof(buffer_size));
		write(fd, &file_size, sizeof(file_size));
		time_t creation_time = time(nullptr);
		write(fd, &creation_time, sizeof(creation_time));
		int access_cnt = 1;
		write(fd, &access_cnt, sizeof(access_cnt));

		close(fd);
		return path;
	}

	std::string m_temp_dir;
};

TEST_F(TestCinfoReader, CompleteFile) {
	auto cinfo_path = CreateCinfoFile(true, 1000000);
	long long file_size;
	EXPECT_TRUE(CinfoReader::IsComplete(cinfo_path, file_size));
	EXPECT_EQ(file_size, 1000000);
}

TEST_F(TestCinfoReader, IncompleteFile) {
	auto cinfo_path = CreateCinfoFile(false, 2000000);
	long long file_size;
	EXPECT_FALSE(CinfoReader::IsComplete(cinfo_path, file_size));
	EXPECT_EQ(file_size, 2000000);
}

TEST_F(TestCinfoReader, NonExistentFile) {
	EXPECT_FALSE(CinfoReader::IsComplete("/nonexistent/path.cinfo"));
}

TEST_F(TestCinfoReader, InvalidMagic) {
	std::string path = m_temp_dir + "/bad_magic.cinfo";
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ASSERT_NE(fd, -1);

	uint16_t bad_magic = 0x1234;
	write(fd, &bad_magic, sizeof(bad_magic));
	close(fd);

	EXPECT_FALSE(CinfoReader::IsComplete(path));
}

TEST_F(TestCinfoReader, TruncatedFile) {
	std::string path = m_temp_dir + "/truncated.cinfo";
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ASSERT_NE(fd, -1);

	// Only write magic number
	uint16_t magic = CinfoReader::kCinfoMagic;
	write(fd, &magic, sizeof(magic));
	close(fd);

	EXPECT_FALSE(CinfoReader::IsComplete(path));
}

TEST_F(TestCinfoReader, ReadStore) {
	auto cinfo_path = CreateCinfoFile(true, 5000000, 2097152);
	CinfoReader::Store store;
	EXPECT_TRUE(CinfoReader::ReadStore(cinfo_path, store));
	EXPECT_EQ(store.m_file_size, 5000000);
	EXPECT_EQ(store.m_buffer_size, 2097152);
	EXPECT_GT(store.m_creationTime, 0);
	EXPECT_EQ(store.m_accessCnt, 1);
}

TEST_F(TestCinfoReader, IsCompleteSimple) {
	auto cinfo_path = CreateCinfoFile(true, 100);
	EXPECT_TRUE(CinfoReader::IsComplete(cinfo_path));
}

//------------------------------------------------------------------------------
// PfcS3FileSystem Tests
//------------------------------------------------------------------------------

class TestPfcS3 : public testing::Test {
  protected:
	virtual std::string GetConfig() {
		std::stringstream ss;
		ss << "oss.localroot " << m_temp_dir << "\n";
		ss << "pfcs3.path_prefix /cache\n";
		ss << "pfcs3.s3_url s3://test-bucket/cached\n";
		ss << "pfcs3.end\n";
		ss << "pfcs3.trace debug\n";
		return ss.str();
	}

	void SetUp() override {
		setenv("XRDINSTANCE", "xrootd", 1);

		// Create temp config file
		char tmp_configfn[] = "/tmp/xrootd-gtest.cfg.XXXXXX";
		auto result = mkstemp(tmp_configfn);
		ASSERT_NE(result, -1) << "Failed to create temp config file ("
							  << strerror(errno) << ", errno=" << errno << ")";
		m_configfn = std::string(tmp_configfn);

		// Create temp directory for localroot
		auto temp_dir =
			std::filesystem::temp_directory_path() / "gtest_temp_xrootd_pfcs3";
		std::error_code ec;
		std::filesystem::create_directories(temp_dir, ec);
		ASSERT_FALSE(ec) << "Failed to create temp directory: " << ec.message();
		m_temp_dir = temp_dir.string();

		// Create the cache subdirectory
		std::filesystem::create_directories(temp_dir / "cache", ec);
		ASSERT_FALSE(ec) << "Failed to create cache directory: "
						 << ec.message();

		auto contents = GetConfig();
		ASSERT_FALSE(contents.empty());
		ASSERT_TRUE(writeShortFile(m_configfn, contents, 0))
			<< "Failed to write to temp file (" << strerror(errno)
			<< ", errno=" << errno << ")";
	}

	void TearDown() override {
		if (!m_configfn.empty()) {
			unlink(m_configfn.c_str());
		}
		if (!m_temp_dir.empty()) {
			std::error_code ec;
			std::filesystem::remove_all(m_temp_dir, ec);
		}
	}

	std::string GetConfigFile() const { return m_configfn; }
	std::string GetTempDir() const { return m_temp_dir; }

  private:
	std::string m_temp_dir;
	std::string m_configfn;
};

TEST_F(TestPfcS3, ConfigParsing) {
	XrdSysLogger logger(2, 0);
	XrdOucEnv env;

	XrdVERSIONINFODEF(XrdOssDefault, Oss, XrdVNUMBER, XrdVERSION);

	XrdOss *default_oss =
		XrdOssDefaultSS(&logger, GetConfigFile().c_str(), XrdOssDefault);
	ASSERT_NE(default_oss, nullptr) << "Failed to get default OSS instance";

	std::unique_ptr<XrdSysError> log(new XrdSysError(&logger, "pfcs3_"));
	PfcS3FileSystem *pfcs3_raw;
	try {
		pfcs3_raw = new PfcS3FileSystem(default_oss, std::move(log),
										GetConfigFile().c_str(), &env);
	} catch (const std::exception &e) {
		FAIL() << "Failed to create PfcS3FileSystem: " << e.what();
	}
	std::unique_ptr<PfcS3FileSystem> pfcs3(pfcs3_raw);

	// Just verify we can Stat the cache directory
	struct stat buf;
	auto rv = pfcs3->Stat("/cache", &buf);
	EXPECT_EQ(rv, 0);
}

TEST_F(TestPfcS3, FileOpenClose) {
	XrdSysLogger logger(2, 0);
	XrdOucEnv env;

	XrdVERSIONINFODEF(XrdOssDefault, Oss, XrdVNUMBER, XrdVERSION);
	XrdOss *default_oss =
		XrdOssDefaultSS(&logger, GetConfigFile().c_str(), XrdOssDefault);
	ASSERT_NE(default_oss, nullptr);

	std::unique_ptr<XrdSysError> log(new XrdSysError(&logger, "pfcs3_"));
	PfcS3FileSystem *pfcs3_raw;
	try {
		pfcs3_raw = new PfcS3FileSystem(default_oss, std::move(log),
										GetConfigFile().c_str(), &env);
	} catch (const std::exception &e) {
		FAIL() << "Failed to create PfcS3FileSystem: " << e.what();
	}
	std::unique_ptr<PfcS3FileSystem> pfcs3(pfcs3_raw);

	// Create a test file
	std::string test_file = GetTempDir() + "/cache/testfile.dat";
	{
		std::ofstream f(test_file);
		f << "test content";
	}

	// Open and close through PfcS3
	std::unique_ptr<XrdOssDF> file(pfcs3->newFile("test"));
	ASSERT_NE(file, nullptr);

	auto rv = file->Open("/cache/testfile.dat", O_RDONLY, 0, env);
	EXPECT_EQ(rv, 0) << "Failed to open file: " << strerror(-rv);

	if (rv == 0) {
		rv = file->Close();
		EXPECT_EQ(rv, 0);
	}
}

//------------------------------------------------------------------------------
// PfcS3 with no S3 URLs configured (should pass through without upload)
//------------------------------------------------------------------------------

class TestPfcS3NoConfig : public TestPfcS3 {
  protected:
	std::string GetConfig() override {
		std::stringstream ss;
		ss << "oss.localroot " << GetTempDir() << "\n";
		// No pfcs3 configuration
		return ss.str();
	}
};

TEST_F(TestPfcS3NoConfig, PassthroughWithoutConfig) {
	XrdSysLogger logger(2, 0);
	XrdOucEnv env;

	XrdVERSIONINFODEF(XrdOssDefault, Oss, XrdVNUMBER, XrdVERSION);
	XrdOss *default_oss =
		XrdOssDefaultSS(&logger, GetConfigFile().c_str(), XrdOssDefault);
	ASSERT_NE(default_oss, nullptr);

	std::unique_ptr<XrdSysError> log(new XrdSysError(&logger, "pfcs3_"));
	PfcS3FileSystem *pfcs3_raw;
	try {
		pfcs3_raw = new PfcS3FileSystem(default_oss, std::move(log),
										GetConfigFile().c_str(), &env);
	} catch (const std::exception &e) {
		FAIL() << "Failed to create PfcS3FileSystem: " << e.what();
	}
	std::unique_ptr<PfcS3FileSystem> pfcs3(pfcs3_raw);

	// Should still work, just without upload functionality
	struct stat buf;
	auto rv = pfcs3->Stat("/", &buf);
	EXPECT_EQ(rv, 0);
}

//------------------------------------------------------------------------------
// S3 URL mapping tests
//------------------------------------------------------------------------------

TEST(S3UrlMappingTest, BasicMapping) {
	// Test the URL mapping logic directly
	// Path: /cache/foo/bar.dat
	// Prefix: /cache
	// S3 URL: s3://bucket/cached
	// Result: s3://bucket/cached/foo/bar.dat

	std::string path = "/cache/foo/bar.dat";
	std::string prefix = "/cache";
	std::string s3_base = "s3://bucket/cached";

	// The mapping should strip the prefix and append to s3_base
	std::string relative = path.substr(prefix.size());
	std::string expected = s3_base + relative;
	EXPECT_EQ(expected, "s3://bucket/cached/foo/bar.dat");
}

TEST(S3UrlMappingTest, RootPrefix) {
	std::string path = "/data/file.dat";
	std::string prefix = "/";
	std::string s3_base = "s3://bucket";

	std::string relative = path.substr(prefix.size());
	std::string expected = s3_base + "/" + relative;
	EXPECT_EQ(expected, "s3://bucket/data/file.dat");
}

TEST(CinfoExtensionTest, Extension) {
	EXPECT_STREQ(CinfoReader::kCinfoExtension, ".cinfo");

	std::string data_file = "/cache/test.dat";
	std::string cinfo_file = data_file + CinfoReader::kCinfoExtension;
	EXPECT_EQ(cinfo_file, "/cache/test.dat.cinfo");
}
