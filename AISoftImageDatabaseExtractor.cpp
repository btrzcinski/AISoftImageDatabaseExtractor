// AISoftImageDatabaseExtractor.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

// DB2 is a file with some sort of header, including the file name of a JPG. The names always have (at least?) 3 null bytes
// beforehand (0x0 0x0 0x0), followed by the JPG filename, which other than ending in .jpg/JPG, has some extra stuff
// after it with unclear meaning. After each header is a JPG file, starting with the byte sequence 0xff 0xd8 0xff and ending with
// 0xff 0xd9 (as expected by JPEG standard).
// 1. Open the DB2 file.
// 2. Scan for jpg/JPG strings (indicating file extension), then look back to the first NUL byte to capture the filename.
// 3. Scan forward for 0xff 0xd8 0xff, then read until 0xff 0xd9.
// 4. Write out these contents under the captured JPG filename.
// 5. Continue scanning per #2 until DB2 file is EOF.

#include <iostream>
#include <Windows.h>

void WriteJpeg(LPCSTR jpegFileName, LPCVOID lpJpegContentsStart, DWORD jpegByteSize)
{
	HANDLE hJpegFile = CreateFileA(jpegFileName,
		GENERIC_WRITE,
		0, // no sharing
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (hJpegFile == INVALID_HANDLE_VALUE)
	{
		throw std::exception("CreateFileA failed");
	}

	WriteFile(hJpegFile, lpJpegContentsStart, jpegByteSize, nullptr, nullptr);

	CloseHandle(hJpegFile);
}

int ScanForJpegs(LPCVOID lpFileStart)
{
	int jpegsScanned = 0;

	MEMORY_BASIC_INFORMATION mappedFileInfo;
	// Ignore errors, YOLO
	VirtualQuery(lpFileStart, &mappedFileInfo, sizeof(mappedFileInfo));

	std::cerr << "Mapped file extent: " << mappedFileInfo.RegionSize << " bytes" << std::endl;

	SIZE_T ptrIndex = 2;
	char byteBuf[4];
	LPCSTR lpFileCharStart = (LPCSTR)lpFileStart;
	LPBYTE lpFileByteStart = (LPBYTE)lpFileStart;
	while (ptrIndex < mappedFileInfo.RegionSize)
	{
		// Look for final 'G' in JPG
		if (lpFileCharStart[ptrIndex] != 'G' && lpFileCharStart[ptrIndex] != 'g')
		{
			++ptrIndex;
			continue;
		}

		ZeroMemory(byteBuf, 4);
		memcpy_s(byteBuf, sizeof(byteBuf), lpFileCharStart + ptrIndex - 2, 3);
		CharUpperA(byteBuf);
		if (strcmp(byteBuf, "JPG") == 0)
		{
			// Look back for filename
			SIZE_T ptrFileNameEnd = ptrIndex;
			SIZE_T ptrFileNameStart = ptrIndex;
			while (lpFileCharStart[--ptrFileNameStart - 1] != '\0');

			// name size = (end - start) + 1 (index correction) + 1 (null terminator)
			LPSTR jpegFileName = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, 
				ptrFileNameEnd - ptrFileNameStart + 2);
			memcpy_s(jpegFileName, ptrFileNameEnd - ptrFileNameStart + 2, lpFileCharStart + ptrFileNameStart,
			         ptrFileNameEnd - ptrFileNameStart + 1);

			// Scan for magic number (0xff 0xd8 0xff)
			ptrIndex = ptrFileNameEnd + 3;
			while (!(lpFileByteStart[ptrIndex - 2] == 0xff &&
				lpFileByteStart[ptrIndex - 1] == 0xd8 &&
				lpFileByteStart[ptrIndex] == 0xff))
			{
				++ptrIndex;
			}

			// ptrIndex now points at last byte of magic number
			SIZE_T ptrJpegContentsStart = ptrIndex - 2;

			// Scan for JPG EOF (0xff 0xd9)
			while(!(lpFileByteStart[ptrIndex - 1] == 0xff &&
				lpFileByteStart[ptrIndex] == 0xd9))
			{
				++ptrIndex;
			}

			// ptrIndex now points to last byte of JPG
			SIZE_T ptrJpegContentsEnd = ptrIndex;

			if ((ptrJpegContentsEnd - ptrJpegContentsStart + 1) > ULONG_MAX)
			{
				std::cerr << "Could not write JPEG " << jpegFileName << " as it is too big to write at once" << std::endl;
			}
			DWORD jpegSize = static_cast<DWORD>(ptrJpegContentsEnd - ptrJpegContentsStart + 1);
			WriteJpeg(jpegFileName, lpFileByteStart + ptrJpegContentsStart, jpegSize);
			std::cout << "Wrote JPEG: " << jpegFileName << std::endl;

			// Move on
			++jpegsScanned;
			++ptrIndex;
			continue;
		}

		++ptrIndex;
	}

	return jpegsScanned;
}

int OpenAndExtractJpegs(LPCSTR dbFileName)
{
	HANDLE hDbFile = CreateFileA(dbFileName,
		GENERIC_READ,
		FILE_SHARE_READ, 
		nullptr,
		OPEN_EXISTING, 
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		nullptr);
	if (hDbFile == INVALID_HANDLE_VALUE)
	{
		throw std::exception("CreateFileA failed");
	}

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(hDbFile, &fileSize))
	{
		CloseHandle(hDbFile);
		throw std::exception("GetFileSizeEx failed");
	}

	std::cout << "File is " << fileSize.QuadPart << " bytes" << std::endl;

	HANDLE hFileMapping = CreateFileMappingA(
		hDbFile,
		nullptr,
		PAGE_READONLY,
		0, 0, nullptr
	);
	if (hFileMapping == nullptr)
	{
		CloseHandle(hDbFile);
		throw std::exception("CreateFileMappingA failed");
	}

	LPVOID lpFileStart = MapViewOfFile(
		hFileMapping,
		FILE_MAP_READ,
		0, 0, 0
	);
	if (lpFileStart == nullptr)
	{
		CloseHandle(hFileMapping);
		CloseHandle(hDbFile);
		throw std::exception("MapViewOfFile failed");
	}

	const int scannedJpegs = ScanForJpegs(lpFileStart);

	UnmapViewOfFile(lpFileStart);
	CloseHandle(hFileMapping);
	CloseHandle(hDbFile);
	return scannedJpegs;
}

int main(int argc, LPCSTR argv[])
{
	if (argc < 2)
	{
		std::cerr << "Specify the DB2 file to extract images from." << std::endl;
		return 1;
	}

	std::cout << "Extracting from DB2 file: " << argv[1] << std::endl;
	try
	{
		const int jpegsRead = OpenAndExtractJpegs(argv[1]);
		std::cout << "JPEGs read: " << jpegsRead << std::endl;
	} catch (const std::exception& ex)
	{
		std::cerr << "Exception: " << ex.what() << std::endl;
		LPVOID errMsgBuffer;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&errMsgBuffer,
			0, NULL
		);
		std::wcerr << L"Win32 Error message: " << static_cast<LPWSTR>(errMsgBuffer) << std::endl;
		LocalFree(errMsgBuffer);
	}

	return 0;
}
