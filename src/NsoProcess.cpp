#include <iostream>
#include <iomanip>
#include <fnd/SimpleTextOutput.h>
#include <fnd/OffsetAdjustedIFile.h>
#include <fnd/Vec.h>
#include <fnd/lz4.h>
#include "NsoProcess.h"

nstool::NsoProcess::NsoProcess():
	mFile(),
	mCliOutputMode(true, false, false, false),
	mVerify(false),
	mIs64BitInstruction(true),
	mListApi(false),
	mListSymbols(false)
{
}

void nstool::NsoProcess::process()
{
	importHeader();
	importCodeSegments();
	if (mCliOutputMode.show_basic_info)
		displayNsoHeader();

	processRoMeta();
}

void nstool::NsoProcess::setInputFile(const std::shared_ptr<tc::io::IStream>& file)
{
	mFile = file;
}

void nstool::NsoProcess::setCliOutputMode(CliOutputMode type)
{
	mCliOutputMode = type;
}

void nstool::NsoProcess::setVerifyMode(bool verify)
{
	mVerify = verify;
}

void nstool::NsoProcess::setIs64BitInstruction(bool flag)
{
	mRoMeta.setIs64BitInstruction(flag);
}

void nstool::NsoProcess::setListApi(bool listApi)
{
	mRoMeta.setListApi(listApi);
}

void nstool::NsoProcess::setListSymbols(bool listSymbols)
{
	mRoMeta.setListSymbols(listSymbols);
}

const RoMetadataProcess& nstool::NsoProcess::getRoMetadataProcess() const
{
	return mRoMeta;
}

void nstool::NsoProcess::importHeader()
{
	tc::ByteData scratch;

	if (*mFile == nullptr)
	{
		throw tc::Exception(kModuleName, "No file reader set.");
	}

	if ((*mFile)->size() < sizeof(nn::hac::sNsoHeader))
	{
		throw tc::Exception(kModuleName, "Corrupt NSO: file too small");
	}

	scratch.alloc(sizeof(nn::hac::sNsoHeader));
	(*mFile)->read(scratch.data(), 0, scratch.size());

	mHdr.fromBytes(scratch.data(), scratch.size());
}

void nstool::NsoProcess::importCodeSegments()
{
	tc::ByteData scratch;
	uint32_t decompressed_len;
	fnd::sha::sSha256Hash calc_hash;

	// process text segment
	if (mHdr.getTextSegmentInfo().is_compressed)
	{
		scratch.alloc(mHdr.getTextSegmentInfo().file_layout.size);
		(*mFile)->read(scratch.data(), mHdr.getTextSegmentInfo().file_layout.offset, scratch.size());
		mTextBlob.alloc(mHdr.getTextSegmentInfo().memory_layout.size);
		fnd::lz4::decompressData(scratch.data(), (uint32_t)scratch.size(), mTextBlob.data(), (uint32_t)mTextBlob.size(), decompressed_len);
		if (decompressed_len != mTextBlob.size())
		{
			throw tc::Exception(kModuleName, "NSO text segment failed to decompress");
		}
	}
	else
	{
		mTextBlob.alloc(mHdr.getTextSegmentInfo().file_layout.size);
		(*mFile)->read(mTextBlob.data(), mHdr.getTextSegmentInfo().file_layout.offset, mTextBlob.size());
	}
	if (mHdr.getTextSegmentInfo().is_hashed)
	{
		fnd::sha::Sha256(mTextBlob.data(), mTextBlob.size(), calc_hash.bytes);
		if (calc_hash != mHdr.getTextSegmentInfo().hash)
		{
			throw tc::Exception(kModuleName, "NSO text segment failed SHA256 verification");
		}
	}

	// process ro segment
	if (mHdr.getRoSegmentInfo().is_compressed)
	{
		scratch.alloc(mHdr.getRoSegmentInfo().file_layout.size);
		(*mFile)->read(scratch.data(), mHdr.getRoSegmentInfo().file_layout.offset, scratch.size());
		mRoBlob.alloc(mHdr.getRoSegmentInfo().memory_layout.size);
		fnd::lz4::decompressData(scratch.data(), (uint32_t)scratch.size(), mRoBlob.data(), (uint32_t)mRoBlob.size(), decompressed_len);
		if (decompressed_len != mRoBlob.size())
		{
			throw tc::Exception(kModuleName, "NSO ro segment failed to decompress");
		}
	}
	else
	{
		mRoBlob.alloc(mHdr.getRoSegmentInfo().file_layout.size);
		(*mFile)->read(mRoBlob.data(), mHdr.getRoSegmentInfo().file_layout.offset, mRoBlob.size());
	}
	if (mHdr.getRoSegmentInfo().is_hashed)
	{
		fnd::sha::Sha256(mRoBlob.data(), mRoBlob.size(), calc_hash.bytes);
		if (calc_hash != mHdr.getRoSegmentInfo().hash)
		{
			throw tc::Exception(kModuleName, "NSO ro segment failed SHA256 verification");
		}
	}

	// process data segment
	if (mHdr.getDataSegmentInfo().is_compressed)
	{
		scratch.alloc(mHdr.getDataSegmentInfo().file_layout.size);
		(*mFile)->read(scratch.data(), mHdr.getDataSegmentInfo().file_layout.offset, scratch.size());
		mDataBlob.alloc(mHdr.getDataSegmentInfo().memory_layout.size);
		fnd::lz4::decompressData(scratch.data(), (uint32_t)scratch.size(), mDataBlob.data(), (uint32_t)mDataBlob.size(), decompressed_len);
		if (decompressed_len != mDataBlob.size())
		{
			throw tc::Exception(kModuleName, "NSO data segment failed to decompress");
		}
	}
	else
	{
		mDataBlob.alloc(mHdr.getDataSegmentInfo().file_layout.size);
		(*mFile)->read(mDataBlob.data(), mHdr.getDataSegmentInfo().file_layout.offset, mDataBlob.size());
	}
	if (mHdr.getDataSegmentInfo().is_hashed)
	{
		fnd::sha::Sha256(mDataBlob.data(), mDataBlob.size(), calc_hash.bytes);
		if (calc_hash != mHdr.getDataSegmentInfo().hash)
		{
			throw tc::Exception(kModuleName, "NSO data segment failed SHA256 verification");
		}
	}
}

void nstool::NsoProcess::displayNsoHeader()
{
	std::cout << "[NSO Header]" << std::endl;
	std::cout << "  ModuleId:           " << fnd::SimpleTextOutput::arrayToString(mHdr.getModuleId().data, nn::hac::nso::kModuleIdSize, false, "") << std::endl;
	if (mCliOutputMode.show_layout)
	{
		std::cout << "  Program Segments:" << std::endl;
		std::cout << "     .module_name:" << std::endl;
		std::cout << "      FileOffset:     0x" << std::hex << mHdr.getModuleNameInfo().offset << std::endl;
		std::cout << "      FileSize:       0x" << std::hex << mHdr.getModuleNameInfo().size << std::endl;
		std::cout << "    .text:" << std::endl;
		std::cout << "      FileOffset:     0x" << std::hex << mHdr.getTextSegmentInfo().file_layout.offset << std::endl;
		std::cout << "      FileSize:       0x" << std::hex << mHdr.getTextSegmentInfo().file_layout.size << (mHdr.getTextSegmentInfo().is_compressed? " (COMPRESSED)" : "") << std::endl;
		std::cout << "    .ro:" << std::endl;
		std::cout << "      FileOffset:     0x" << std::hex << mHdr.getRoSegmentInfo().file_layout.offset << std::endl;
		std::cout << "      FileSize:       0x" << std::hex << mHdr.getRoSegmentInfo().file_layout.size << (mHdr.getRoSegmentInfo().is_compressed? " (COMPRESSED)" : "") << std::endl;
		std::cout << "    .data:" << std::endl;
		std::cout << "      FileOffset:     0x" << std::hex << mHdr.getDataSegmentInfo().file_layout.offset << std::endl;
		std::cout << "      FileSize:       0x" << std::hex << mHdr.getDataSegmentInfo().file_layout.size << (mHdr.getDataSegmentInfo().is_compressed? " (COMPRESSED)" : "") << std::endl;
	}
	std::cout << "  Program Sections:" << std::endl;
	std::cout << "     .text:" << std::endl;
	std::cout << "      MemoryOffset:   0x" << std::hex << mHdr.getTextSegmentInfo().memory_layout.offset << std::endl;
	std::cout << "      MemorySize:     0x" << std::hex << mHdr.getTextSegmentInfo().memory_layout.size << std::endl;
	if (mHdr.getTextSegmentInfo().is_hashed && mCliOutputMode.show_extended_info)
	{
		std::cout << "      Hash:           " << fnd::SimpleTextOutput::arrayToString(mHdr.getTextSegmentInfo().hash.bytes, 32, false, "") << std::endl;
	}
	std::cout << "    .ro:" << std::endl;
	std::cout << "      MemoryOffset:   0x" << std::hex << mHdr.getRoSegmentInfo().memory_layout.offset << std::endl;
	std::cout << "      MemorySize:     0x" << std::hex << mHdr.getRoSegmentInfo().memory_layout.size << std::endl;
	if (mHdr.getRoSegmentInfo().is_hashed && mCliOutputMode.show_extended_info)
	{
		std::cout << "      Hash:           " << fnd::SimpleTextOutput::arrayToString(mHdr.getRoSegmentInfo().hash.bytes, 32, false, "") << std::endl;
	}
	if (mCliOutputMode.show_extended_info)
	{
		std::cout << "    .api_info:" << std::endl;
		std::cout << "      MemoryOffset:   0x" << std::hex <<  mHdr.getRoEmbeddedInfo().offset << std::endl;
		std::cout << "      MemorySize:     0x" << std::hex << mHdr.getRoEmbeddedInfo().size << std::endl;
		std::cout << "    .dynstr:" << std::endl;
		std::cout << "      MemoryOffset:   0x" << std::hex << mHdr.getRoDynStrInfo().offset << std::endl;
		std::cout << "      MemorySize:     0x" << std::hex << mHdr.getRoDynStrInfo().size << std::endl;
		std::cout << "    .dynsym:" << std::endl;
		std::cout << "      MemoryOffset:   0x" << std::hex << mHdr.getRoDynSymInfo().offset << std::endl;
		std::cout << "      MemorySize:     0x" << std::hex << mHdr.getRoDynSymInfo().size << std::endl;
	}
	
	std::cout << "    .data:" << std::endl;
	std::cout << "      MemoryOffset:   0x" << std::hex << mHdr.getDataSegmentInfo().memory_layout.offset << std::endl;
	std::cout << "      MemorySize:     0x" << std::hex << mHdr.getDataSegmentInfo().memory_layout.size << std::endl;
	if (mHdr.getDataSegmentInfo().is_hashed && mCliOutputMode.show_extended_info)
	{
		std::cout << "      Hash:           " << fnd::SimpleTextOutput::arrayToString(mHdr.getDataSegmentInfo().hash.bytes, 32, false, "") << std::endl;
	}
	std::cout << "    .bss:" << std::endl;
	std::cout << "      MemorySize:     0x" << std::hex << mHdr.getBssSize() << std::endl;
}

void nstool::NsoProcess::processRoMeta()
{
	if (mRoBlob.size())
	{
		// setup ro metadata
		mRoMeta.setApiInfo(mHdr.getRoEmbeddedInfo().offset, mHdr.getRoEmbeddedInfo().size);
		mRoMeta.setDynSym(mHdr.getRoDynSymInfo().offset, mHdr.getRoDynSymInfo().size);
		mRoMeta.setDynStr(mHdr.getRoDynStrInfo().offset, mHdr.getRoDynStrInfo().size);
		mRoMeta.setRoBinary(mRoBlob);
		mRoMeta.setCliOutputMode(mCliOutputMode);
		mRoMeta.process();
	}
}