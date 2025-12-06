#pragma once

#include <cstdio>
#include <map>
#include <string>
#include <tuple>

#include "shared/unique_free_ptr.h"

#pragma pack(push, 1)
struct COFFHeader {
	uint16_t Machine;
	uint16_t NumberOfSections;
	uint32_t TimeDateStamp;
	void* PointerToSymbolTable;
	uint32_t NumberOfSymbols;
	uint16_t SizeOfOptionalHeader;
	uint16_t Characteristics;
};
#pragma pack(pop)

static_assert(sizeof(COFFHeader) == 20);

#pragma pack(push, 1)
struct SectionHeader {
	char Name[8];
	uint32_t VirtualSize;
	uint32_t VirtualAddress;
	uint32_t SizeOfRawData;
	uint32_t PointerToRawData;
	uint32_t PointerToRelocations;
	uint32_t PointerToLineNumbers;
	uint16_t NumberOfRelocations;
	uint16_t NumberOfLineNumbers;
	uint32_t Characteristics;
};
#pragma pack(pop)

static_assert(sizeof(SectionHeader) == 40);

class PE32Byte {
public:
	PE32Byte() { _bytes = nullptr; }

	inline operator const char* () { return _bytes;  }
	inline operator bool() const { return _bytes != nullptr; }

	inline PE32Byte(PE32Byte const& other) noexcept : _bytes(other._bytes) { }
	inline PE32Byte(PE32Byte&& other) noexcept : _bytes(other._bytes) { }

	inline PE32Byte& operator=(PE32Byte const& other) noexcept { _bytes = other._bytes; return *this; }
	inline PE32Byte& operator=(PE32Byte&& other) noexcept { _bytes = other._bytes; return *this; }

private:
	friend class PE32;

	PE32Byte(char* bytes) : _bytes(bytes) { }
	inline char* operator*() { return _bytes; }

	char* _bytes;
};

class PE32 {
public:
	static constexpr size_t PE_SIGNATURE_OFFSET = 0x3c;

    PE32(const char* filename);
    ~PE32();

	bool IsSectionSizeValid(SectionHeader const* header) const;
	std::tuple<const SectionHeader*, PE32Byte> GetSection(const char* section);
	PE32Byte Lookup(const char* str, const SectionHeader* header = nullptr) const;
	bool Patch(PE32Byte where, const char* with);
	bool Write();

private:
    void Validate(const char* filename);

	std::map<std::string, std::tuple<SectionHeader*, PE32Byte>> _sectionsMap;

	std::string _filename;
	size_t _size;
	COFFHeader* _coffHeader = nullptr;
	SectionHeader* _sections = nullptr;
	unique_free_ptr<char> _content;
};