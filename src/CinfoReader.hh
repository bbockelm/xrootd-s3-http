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

#include <cstdint>
#include <string>
#include <vector>

/**
 * CinfoReader - Read and parse XrdPfc cinfo files to determine cache
 * completion status.
 *
 * The cinfo file format is used by XrdPfc (XRootD Proxy File Cache) to track
 * which blocks of a file have been cached. This reader parses that format
 * to determine if a file has been completely cached.
 *
 * Format overview:
 * - Magic number (0x52FC) followed by version number
 * - Store structure with buffer_size, file_size, creation_time, access_cnt
 * - For version >= 4: variable-length array of cksum and access stat data
 * - Bitmap data tracking synced and written blocks
 * - The m_complete flag indicates all blocks are synced
 */
class CinfoReader {
  public:
	// Extension for cinfo files
	static constexpr const char *kCinfoExtension = ".cinfo";
	static constexpr uint16_t kCinfoMagic = 0x52FC;

	// Store structure from XrdPfcInfo.hh
	struct Store {
		int m_buffer_size{0};	  // Buffer size for this file
		long long m_file_size{0}; // Original file size
		time_t m_creationTime{0}; // When the cache entry was created
		int m_accessCnt{0};		  // Number of accesses
	};

	/**
	 * Check if a cinfo file indicates the cached file is complete.
	 *
	 * @param cinfo_path Path to the .cinfo file
	 * @param file_size Output parameter for the original file size
	 * @return true if the file is completely cached, false otherwise
	 */
	static bool IsComplete(const std::string &cinfo_path, long long &file_size);

	/**
	 * Check if a cinfo file indicates the cached file is complete.
	 *
	 * @param cinfo_path Path to the .cinfo file
	 * @return true if the file is completely cached, false otherwise
	 */
	static bool IsComplete(const std::string &cinfo_path);

	/**
	 * Read the cinfo file and return the store structure.
	 *
	 * @param cinfo_path Path to the .cinfo file
	 * @param store Output parameter for the store structure
	 * @return true if the cinfo file was read successfully, false otherwise
	 */
	static bool ReadStore(const std::string &cinfo_path, Store &store);

  private:
	/**
	 * Read and parse a cinfo file.
	 *
	 * @param cinfo_path Path to the .cinfo file
	 * @param store Output parameter for the store structure
	 * @param is_complete Output parameter for whether the file is complete
	 * @return true if the cinfo file was read successfully, false otherwise
	 */
	static bool ReadCinfo(const std::string &cinfo_path, Store &store,
						  bool &is_complete);
};
