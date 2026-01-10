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

#include "../src/S3AccessInfo.hh"
#include "../src/S3PresignedUrl.hh"
#include "../src/shortfile.hh"

#include <XrdSys/XrdSysLogger.hh>
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <string>

using namespace XrdOfsRedirect;

class PresignedUrlTest : public testing::Test {
  protected:
	PresignedUrlTest()
		: m_log(new XrdSysLogger(2, 0)), m_err(m_log.get(), "test_") {}

	void SetUp() override {
		// Create temporary key files for testing
		m_accessKeyFile = "/tmp/test_access_key";
		m_secretKeyFile = "/tmp/test_secret_key";

		std::ofstream akf(m_accessKeyFile);
		akf << "AKIAIOSFODNN7EXAMPLE";
		akf.close();

		std::ofstream skf(m_secretKeyFile);
		skf << "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
		skf.close();
	}

	void TearDown() override {
		// Remove temporary files
		unlink(m_accessKeyFile.c_str());
		unlink(m_secretKeyFile.c_str());
	}

	std::unique_ptr<XrdSysLogger> m_log;
	XrdSysError m_err;
	std::string m_accessKeyFile;
	std::string m_secretKeyFile;
};

// Test: Generate presigned URL with path-style addressing
TEST_F(PresignedUrlTest, GeneratePathStyleUrl) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://s3.amazonaws.com");
	ai.setS3BucketName("mybucket");
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	EXPECT_TRUE(generatePresignedUrl(ai, "myobject/file.txt", 3600, m_err,
									 presignedUrl));

	// Verify URL structure
	EXPECT_TRUE(presignedUrl.find("https://s3.amazonaws.com/mybucket/") !=
				std::string::npos);
	EXPECT_TRUE(presignedUrl.find("myobject/file.txt") != std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Algorithm=AWS4-HMAC-SHA256") !=
				std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Credential=") != std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Date=") != std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Expires=3600") != std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-SignedHeaders=host") !=
				std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Signature=") != std::string::npos);
}

// Test: Generate presigned URL with virtual-style addressing
TEST_F(PresignedUrlTest, GenerateVirtualStyleUrl) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://s3.amazonaws.com");
	ai.setS3BucketName("mybucket");
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("virtual");

	std::string presignedUrl;
	EXPECT_TRUE(generatePresignedUrl(ai, "myobject/file.txt", 3600, m_err,
									 presignedUrl));

	// Virtual style should have bucket in host
	EXPECT_TRUE(presignedUrl.find("https://mybucket.s3.amazonaws.com/") !=
				std::string::npos);
	EXPECT_TRUE(presignedUrl.find("myobject/file.txt") != std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Signature=") != std::string::npos);
}

// Test: Generate presigned URL with custom endpoint (MinIO style)
TEST_F(PresignedUrlTest, GenerateMinioStyleUrl) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("http://localhost:9000");
	ai.setS3BucketName("testbucket");
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	EXPECT_TRUE(
		generatePresignedUrl(ai, "testfile.dat", 7200, m_err, presignedUrl));

	// Verify URL structure for MinIO-style endpoint
	EXPECT_TRUE(
		presignedUrl.find("http://localhost:9000/testbucket/testfile.dat") !=
		std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Expires=7200") != std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Signature=") != std::string::npos);
}

// Test: URL encoding of special characters in object name
TEST_F(PresignedUrlTest, UrlEncodingSpecialChars) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://s3.amazonaws.com");
	ai.setS3BucketName("mybucket");
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	// Object name with special characters that need encoding
	EXPECT_TRUE(generatePresignedUrl(ai, "path/to/file with spaces.txt", 3600,
									 m_err, presignedUrl));

	// Space should be encoded as %20 in path
	EXPECT_TRUE(presignedUrl.find("%20") != std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Signature=") != std::string::npos);
}

// Test: Failure when access key file is missing
TEST_F(PresignedUrlTest, MissingAccessKeyFile) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://s3.amazonaws.com");
	ai.setS3BucketName("mybucket");
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile("/nonexistent/access_key");
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	EXPECT_FALSE(
		generatePresignedUrl(ai, "myobject", 3600, m_err, presignedUrl));
}

// Test: Failure when secret key file is missing
TEST_F(PresignedUrlTest, MissingSecretKeyFile) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://s3.amazonaws.com");
	ai.setS3BucketName("mybucket");
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile("/nonexistent/secret_key");
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	EXPECT_FALSE(
		generatePresignedUrl(ai, "myobject", 3600, m_err, presignedUrl));
}

// Test: Service URL with trailing path
TEST_F(PresignedUrlTest, ServiceUrlWithTrailingPath) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://gateway.example.com/s3/");
	ai.setS3BucketName("mybucket");
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	EXPECT_TRUE(
		generatePresignedUrl(ai, "myobject.txt", 3600, m_err, presignedUrl));

	// Should include the path prefix
	EXPECT_TRUE(
		presignedUrl.find("gateway.example.com/s3/mybucket/myobject.txt") !=
		std::string::npos);
	EXPECT_TRUE(presignedUrl.find("X-Amz-Signature=") != std::string::npos);
}

// Test: Default region when not specified
TEST_F(PresignedUrlTest, DefaultRegion) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://s3.amazonaws.com");
	ai.setS3BucketName("mybucket");
	// No region set - should default to us-east-1
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	EXPECT_TRUE(
		generatePresignedUrl(ai, "myobject", 3600, m_err, presignedUrl));

	// URL should be generated with default region in the credential
	EXPECT_TRUE(presignedUrl.find("X-Amz-Credential=") != std::string::npos);
	// The region appears URL-encoded in the credential: %2Fus-east-1%2F
	EXPECT_TRUE(presignedUrl.find("%2Fus-east-1%2F") != std::string::npos);
}

// Test: Bucket name empty (root bucket access)
TEST_F(PresignedUrlTest, EmptyBucketName) {
	S3AccessInfo ai;
	ai.setS3ServiceUrl("https://mybucket.s3.amazonaws.com");
	// No bucket name set - assume bucket is in the URL (virtual style)
	ai.setS3Region("us-east-1");
	ai.setS3AccessKeyFile(m_accessKeyFile);
	ai.setS3SecretKeyFile(m_secretKeyFile);
	ai.setS3UrlStyle("path");

	std::string presignedUrl;
	EXPECT_TRUE(
		generatePresignedUrl(ai, "myobject", 3600, m_err, presignedUrl));

	// Should work with empty bucket name
	EXPECT_TRUE(
		presignedUrl.find("https://mybucket.s3.amazonaws.com/myobject") !=
		std::string::npos);
}
