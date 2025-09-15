#include <WinSock2.h>
#include <Windows.h>
#include <io.h>

#include <stdexcept>

#include "shared/pe32.h"
#include "shared/scoped_file.h"

PE32::PE32(const char* filename) : _filename(filename) {
    Validate(filename);
}

PE32::~PE32() {

}

bool PE32::IsSectionSizeValid(const SectionHeader* header) const {
	return header->PointerToRawData + header->SizeOfRawData <= _size;
}

bool PE32::Patch(PE32Byte where, const char* with) {
	if (*where < _content.get() || *where > (_content.get() + _size))
		return false;

	size_t size = strlen(with);
	if (*where + size > (_content.get() + _size))
		return false;

	memcpy(*where, with, size);
	return true;
}

PE32Byte PE32::Lookup(const char* str, const SectionHeader* header) const {
	size_t size = strlen(str);

	char* start = _content.get();
	char* end = start + _size;
	if (header) {
		start = _content.get() + header->PointerToRawData;
		end = start + header->SizeOfRawData;
	}

	ptrdiff_t dist = end - start;
	// If the string is longer than the available space, save some time
	if (dist < size)
		return PE32Byte(nullptr);

	char* substring = nullptr;
	while (start != (end - size + 1)) {
		if (!memcmp(start, str, size)) {
			substring = start;
			break;
		}

		++start;
	}

	return PE32Byte(substring);
}

bool PE32::Write() {
	std::string filename = _filename + ".tmp";
	FILE* file = fopen(filename.c_str(), "wb");
	if (!file) {
		return false;
	}

	ScopedFile f(file);
	if (fwrite(_content.get(), _size, 1, file) != 1) {
		return false;
	}

	fclose(file);
	f.Release();

	if (!CopyFileA(filename.c_str(), _filename.c_str(), FALSE)) {
		return false;
	}

	DeleteFileA(filename.c_str());
	return true;
}

std::tuple<const SectionHeader*, PE32Byte> PE32::GetSection(const char* section) {
	if (auto iter = _sectionsMap.find(section); iter != _sectionsMap.end()) {
		return iter->second;
	}

	for (int i = 0; i < _coffHeader->NumberOfSections; ++i) {
		SectionHeader* header = _sections + i;
		if (!strncmp(header->Name, section, sizeof(header->Name))) {
			std::tuple<SectionHeader*, PE32Byte> res = std::make_tuple(header, PE32Byte(_content.get() + header->PointerToRawData));
			_sectionsMap[section] = res;
			return res;
		}
	}

	return std::make_tuple(nullptr, PE32Byte(nullptr));
}

void PE32::Validate(const char* filename) {
	FILE* file = fopen(filename, "rb");

	if (!file)
		throw std::runtime_error("PE32: unable to open provided file");

	ScopedFile f(file);

	DWORD highOrder = 0;
	HANDLE exeHandle = (HANDLE)_get_osfhandle(_fileno(file));
	DWORD size = GetFileSize(exeHandle, &highOrder);
	_size = size;
	DWORD minSize = 0x40; // DOS header

	if (highOrder != 0)
		throw std::runtime_error("PE32: provided executable size > 4GB");

	if (size < minSize)
		throw std::runtime_error("PE32: provided executable too short (< 0x40 bytes)");

	char* content = (char*)malloc(size);
	if (!content)
		throw std::runtime_error("PE32: unable to allocate memory to read provided file");

	_content.reset(content);

	if (fread(content, size, 1, file) != 1)
		throw std::runtime_error("PE32: error while reading file in memory");

	uint32_t* peOffsetAddr = (uint32_t*)(content + PE_SIGNATURE_OFFSET);
	minSize = *peOffsetAddr + sizeof(uint32_t);
	if (size <= minSize)
		throw std::runtime_error("PE32: provided executable too short (no PE header)");

	uint32_t* peSignaturePtr = (uint32_t*)(content + *peOffsetAddr);
	if (*peSignaturePtr != 0x00004550) // "PE\0\0"
		throw std::runtime_error("PE32: provided executable is not a valid PE32 executable (invalid PE signature)");

	minSize += sizeof(COFFHeader);
	if (size < minSize)
		throw std::runtime_error("PE32: provided executable too short (no COFF header)");

	_coffHeader = (COFFHeader*)(content + *peOffsetAddr + sizeof(uint32_t));

	minSize += _coffHeader->SizeOfOptionalHeader;
	if (size < minSize)
		throw std::runtime_error("PE32: provided executable too short (malformed optional header)");

	_sections = (SectionHeader*)(content + minSize);

	minSize += _coffHeader->NumberOfSections * sizeof(SectionHeader);
	if (size < minSize)
		throw std::runtime_error("PE32: provided executable too short (malformed section table)");
}