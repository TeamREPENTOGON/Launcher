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

#pragma pack(push, 1)
struct DataDirectory {
	uint32_t VirtualAddress;
	uint32_t Size;
};
#pragma pack(pop)

static_assert(sizeof(DataDirectory) == 8);

#pragma pack(push, 1)
struct OptionalHeader {
	uint16_t Magic;
	uint8_t MajorLinkerVersion;
	uint8_t MinorLinkerVersion;
	uint32_t SizeOfCode;
	uint32_t SizeOfInitializedData;
	uint32_t SizeOfUninitializedData;
	uint32_t AddressOfEntryPoint;
	uint32_t BaseOfCode;
	uint32_t BaseOfData;
	uint32_t ImageBase;
	uint32_t SectionAlignment;
	uint32_t FileAlignment;
	uint16_t MajorOperatingSystemVersion;
	uint16_t MinorOperatingSystemVersion;
	uint16_t MajorImageVersion;
	uint16_t MinorImageVersion;
	uint16_t MajorSubsystemVersion;
	uint16_t MinorSubsystemVersion;
	uint32_t Win32VersionValue;
	uint32_t SizeOfImage;
	uint32_t SizeOfHeaders;
	uint32_t CheckSum;
	uint16_t Subsystem;
	uint16_t DllCharacteristics;
	uint32_t SizeOfStackReserve;
	uint32_t SizeOfStackCommit;
	uint32_t SizeOfHeapReserve;
	uint32_t SizeOfHeapCommit;
	uint32_t LoaderFlags;
	uint32_t NumberOfRvasAndSizes;
	DataDirectory ExportTable;
	DataDirectory ImportTable;
	DataDirectory ResourceTable;
	DataDirectory ExceptionTable;
	DataDirectory CertificateTable;
	DataDirectory BaseRelocationTable;
	DataDirectory DebugData;
	uint64_t Architecture;
	DataDirectory GlobalPtrData;
	DataDirectory TLSTable;
	DataDirectory LoadConfigTable;
	DataDirectory BoundImportTable;
	DataDirectory ImportAddressTable;
	DataDirectory DelayImportDescriptorData;
	DataDirectory CLRRuntimeHeaderData;
	uint64_t Reserved0;
};
#pragma pack(pop)

static_assert(sizeof(OptionalHeader) == 224);

#pragma pack(push, 1)
struct ExportDirectoryTable {
	uint32_t ExportFlags;
	uint32_t TimeDateStamp;
	uint16_t MajorVersion;
	uint16_t MinorVersion;
	uint32_t NameRVA;
	uint32_t OrdinalBase;
	uint32_t AddressTableEntries;
	uint32_t NumberOfNamePointers;
	uint32_t ExportAddressTableRVA;
	uint32_t NamePointerRVA;
	uint32_t OrdinalTableRVA;
};
#pragma pack(pop)

static_assert(sizeof(ExportDirectoryTable) == 40);

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
	inline OptionalHeader const* GetOptionalHeader() const { return _optionalHeader;  }
	const char* RVA(uint32_t, const char* base = nullptr);

private:
    void Validate(const char* filename);
	void Map();

	std::map<std::string, std::tuple<SectionHeader*, PE32Byte>> _sectionsMap;

	std::string _filename;
	size_t _size;
	COFFHeader* _coffHeader = nullptr;
	SectionHeader* _sections = nullptr;
	OptionalHeader* _optionalHeader = nullptr;
	unique_free_ptr<char> _content;
	/* Content of the file as it it was loaded in memory. This includes padding
	 * at the end of sections, and can be used to compute RVAs.
	 */
	unique_free_ptr<char> _memoryContent;
};