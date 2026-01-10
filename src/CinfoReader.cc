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

#include "CinfoReader.hh"

#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Version constants from XrdPfcInfo.hh
constexpr int kCinfoVersion = 6;
constexpr int kMinSupportedVersion = 4;

// Structure sizes for different versions
// The Store structure is followed by variable-length data in v4+
constexpr size_t kStoreSize = sizeof(int) +		  // m_buffer_size
							  sizeof(long long) + // m_file_size
							  sizeof(time_t) +	  // m_creationTime
							  sizeof(int);		  // m_accessCnt

// Read data from file descriptor
bool ReadExact(int fd, void *buffer, size_t size) {
	char *buf = static_cast<char *>(buffer);
	size_t total_read = 0;
	while (total_read < size) {
		ssize_t n = read(fd, buf + total_read, size - total_read);
		if (n <= 0) {
			return false;
		}
		total_read += n;
	}
	return true;
}

} // namespace

bool CinfoReader::IsComplete(const std::string &cinfo_path,
							 long long &file_size) {
	Store store;
	bool is_complete;
	if (!ReadCinfo(cinfo_path, store, is_complete)) {
		return false;
	}
	file_size = store.m_file_size;
	return is_complete;
}

bool CinfoReader::IsComplete(const std::string &cinfo_path) {
	long long file_size;
	return IsComplete(cinfo_path, file_size);
}

bool CinfoReader::ReadStore(const std::string &cinfo_path, Store &store) {
	bool is_complete;
	return ReadCinfo(cinfo_path, store, is_complete);
}

bool CinfoReader::ReadCinfo(const std::string &cinfo_path, Store &store,
							bool &is_complete) {
	is_complete = false;

	int fd = open(cinfo_path.c_str(), O_RDONLY);
	if (fd < 0) {
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

	// Read and check magic number
	uint16_t magic;
	if (!ReadExact(fd, &magic, sizeof(magic))) {
		return false;
	}
	if (magic != kCinfoMagic) {
		return false;
	}

	// Read version
	uint8_t version;
	if (!ReadExact(fd, &version, sizeof(version))) {
		return false;
	}
	if (version < kMinSupportedVersion || version > kCinfoVersion) {
		// Unsupported version
		return false;
	}

	// Read the complete flag (next byte after version)
	uint8_t complete_flag;
	if (!ReadExact(fd, &complete_flag, sizeof(complete_flag))) {
		return false;
	}
	is_complete = (complete_flag != 0);

	// Read the Store structure
	// In the actual XrdPfc implementation, Store is at a fixed offset
	// after the header. The format is:
	// - uint16_t magic (2 bytes)
	// - uint8_t version (1 byte)
	// - uint8_t complete (1 byte)
	// - Store struct (variable size depending on platform)

	if (!ReadExact(fd, &store.m_buffer_size, sizeof(store.m_buffer_size))) {
		return false;
	}
	if (!ReadExact(fd, &store.m_file_size, sizeof(store.m_file_size))) {
		return false;
	}
	if (!ReadExact(fd, &store.m_creationTime, sizeof(store.m_creationTime))) {
		return false;
	}
	if (!ReadExact(fd, &store.m_accessCnt, sizeof(store.m_accessCnt))) {
		return false;
	}

	return true;
}
