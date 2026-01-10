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

#include "S3PresignedUrl.hh"
#include "AWSv4-impl.hh"
#include "shortfile.hh"
#include "stl_string_utils.hh"

#include <XrdSys/XrdSysError.hh>

#include <openssl/hmac.h>

#include <cstring>
#include <ctime>
#include <map>
#include <sstream>

namespace XrdOfsRedirect {

namespace {

// Parse the service URL to extract host, protocol, and resource path
bool parseServiceUrl(const std::string &url, const std::string &bucket,
					 const std::string &object, const std::string &style,
					 std::string &protocol, std::string &host,
					 std::string &canonicalUri) {
	auto schemeEndIdx = url.find("://");
	if (schemeEndIdx == std::string::npos) {
		return false;
	}
	protocol = url.substr(0, schemeEndIdx);
	if (url.size() < schemeEndIdx + 3) {
		return false;
	}
	auto hostStartIdx = schemeEndIdx + 3;

	auto resourceStartIdx = url.find("/", hostStartIdx);
	if (resourceStartIdx == std::string::npos) {
		// No trailing path in URL
		if (style == "path") {
			host = url.substr(hostStartIdx);
			if (bucket.empty()) {
				canonicalUri = "/" + object;
			} else {
				canonicalUri = "/" + bucket + "/" + object;
			}
		} else {
			// Virtual style
			host = bucket + "." + url.substr(hostStartIdx);
			canonicalUri = "/" + object;
		}
	} else {
		// URL has a trailing path
		if (style == "path") {
			host = url.substr(hostStartIdx, resourceStartIdx - hostStartIdx);
			std::string resourcePrefix = url.substr(resourceStartIdx);
			if (!resourcePrefix.empty() &&
				resourcePrefix[resourcePrefix.size() - 1] == '/') {
				resourcePrefix =
					resourcePrefix.substr(0, resourcePrefix.size() - 1);
			}
			if (bucket.empty()) {
				canonicalUri = resourcePrefix + "/" + object;
			} else {
				canonicalUri = resourcePrefix + "/" + bucket + "/" + object;
			}
		} else {
			// Virtual style
			host = bucket + "." +
				   url.substr(hostStartIdx, resourceStartIdx - hostStartIdx);
			canonicalUri = url.substr(resourceStartIdx) + object;
		}
	}
	return true;
}

} // namespace

bool generatePresignedUrl(const S3AccessInfo &ai, const std::string &objectName,
						  int expirationSecs, XrdSysError &log,
						  std::string &presignedUrl) {
	// Read access key and secret key
	std::string accessKey;
	std::string secretKey;

	if (!ai.getS3AccessKeyFile().empty()) {
		if (!readShortFile(ai.getS3AccessKeyFile(), accessKey)) {
			log.Emsg("generatePresignedUrl", "Failed to read access key file");
			return false;
		}
		trim(accessKey);
	} else {
		log.Emsg("generatePresignedUrl", "Access key file not specified");
		return false;
	}

	if (!ai.getS3SecretKeyFile().empty()) {
		if (!readShortFile(ai.getS3SecretKeyFile(), secretKey)) {
			log.Emsg("generatePresignedUrl", "Failed to read secret key file");
			return false;
		}
		trim(secretKey);
	} else {
		log.Emsg("generatePresignedUrl", "Secret key file not specified");
		return false;
	}

	// Parse URL components
	std::string protocol, host, canonicalUri;
	if (!parseServiceUrl(ai.getS3ServiceUrl(), ai.getS3BucketName(), objectName,
						 ai.getS3UrlStyle(), protocol, host, canonicalUri)) {
		log.Emsg("generatePresignedUrl", "Failed to parse service URL");
		return false;
	}

	// URL-encode the canonical URI path segments
	canonicalUri = AWSv4Impl::pathEncode(canonicalUri);

	// Determine region (default to us-east-1)
	std::string region = ai.getS3Region();
	if (region.empty()) {
		region = "us-east-1";
	}

	// Get current time in UTC
	time_t now;
	time(&now);
	struct tm brokenDownTime;
	gmtime_r(&now, &brokenDownTime);

	char dateStr[9];	  // YYYYMMDD
	char datetimeStr[17]; // YYYYMMDDTHHmmssZ
	strftime(dateStr, sizeof(dateStr), "%Y%m%d", &brokenDownTime);
	strftime(datetimeStr, sizeof(datetimeStr), "%Y%m%dT%H%M%SZ",
			 &brokenDownTime);

	// Build credential scope
	std::string credentialScope;
	formatstr(credentialScope, "%s/%s/s3/aws4_request", dateStr,
			  region.c_str());

	// URL-encode the credential
	std::string credential = accessKey + "/" + credentialScope;
	std::string encodedCredential = AWSv4Impl::amazonURLEncode(credential);

	// Build the canonical query string
	// Parameters must be in alphabetical order
	std::map<std::string, std::string> queryParams;
	queryParams["X-Amz-Algorithm"] = "AWS4-HMAC-SHA256";
	queryParams["X-Amz-Credential"] = credential;
	queryParams["X-Amz-Date"] = datetimeStr;
	queryParams["X-Amz-Expires"] = std::to_string(expirationSecs);
	queryParams["X-Amz-SignedHeaders"] = "host";

	std::string canonicalQueryString =
		AWSv4Impl::canonicalizeQueryString(queryParams);

	// Build canonical headers (just host for presigned URLs)
	std::string canonicalHeaders = "host:" + host + "\n";
	std::string signedHeaders = "host";

	// Build canonical request
	// For presigned URLs, payload hash is always UNSIGNED-PAYLOAD
	std::string payloadHash = "UNSIGNED-PAYLOAD";
	std::string canonicalRequest =
		"GET\n" + canonicalUri + "\n" + canonicalQueryString + "\n" +
		canonicalHeaders + "\n" + signedHeaders + "\n" + payloadHash;

	// Hash the canonical request
	unsigned char messageDigest[EVP_MAX_MD_SIZE];
	unsigned int mdLength = 0;
	if (!AWSv4Impl::doSha256(canonicalRequest, messageDigest, &mdLength)) {
		log.Emsg("generatePresignedUrl", "Failed to hash canonical request");
		return false;
	}
	std::string canonicalRequestHash;
	AWSv4Impl::convertMessageDigestToLowercaseHex(messageDigest, mdLength,
												  canonicalRequestHash);

	// Build string to sign
	std::string stringToSign;
	formatstr(stringToSign, "AWS4-HMAC-SHA256\n%s\n%s\n%s", datetimeStr,
			  credentialScope.c_str(), canonicalRequestHash.c_str());

	// Calculate signature using HMAC chain
	std::string signature;
	if (!AWSv4Impl::createSignature(secretKey, dateStr, region, "s3",
									stringToSign, signature)) {
		log.Emsg("generatePresignedUrl", "Failed to create signature");
		return false;
	}

	// Build final presigned URL
	std::ostringstream urlStream;
	urlStream << protocol << "://" << host << canonicalUri << "?"
			  << canonicalQueryString << "&X-Amz-Signature=" << signature;

	presignedUrl = urlStream.str();
	return true;
}

} // namespace XrdOfsRedirect
