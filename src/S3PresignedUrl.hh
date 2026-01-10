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

#include "S3AccessInfo.hh"

#include <string>

class XrdSysError;

namespace XrdOfsRedirect {

/**
 * Generate a presigned URL for an S3 object using AWS v4 query string
 * authentication.
 *
 * @param ai The S3 access information (service URL, bucket, access/secret keys,
 * etc.)
 * @param objectName The S3 object name (path within the bucket)
 * @param expirationSecs The expiration time in seconds for the presigned URL
 * @param log Error logger for reporting issues
 * @param presignedUrl Output parameter for the generated presigned URL
 * @return true on success, false on failure
 */
bool generatePresignedUrl(const S3AccessInfo &ai, const std::string &objectName,
						  int expirationSecs, XrdSysError &log,
						  std::string &presignedUrl);

} // namespace XrdOfsRedirect
