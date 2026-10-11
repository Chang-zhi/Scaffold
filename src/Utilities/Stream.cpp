#include "Stream.h"
#include "Debug.h"

#include <SwizzleManagerClass.h>

#include <Objidl.h>

#include <limits>

ScaffoldByteStream::ScaffoldByteStream(size_t Reserve) : Data(), CurrentOffset(0)
{
	this->Data.reserve(Reserve);
}

ScaffoldByteStream::~ScaffoldByteStream() = default;

bool ScaffoldByteStream::ReadFromStream(IStream* pStm, const size_t Length)
{
	auto size = this->Data.size();
	if (!pStm || Length > (this->Data.max_size() - size))
		return false;

	this->Data.resize(size + Length);
	auto pv = reinterpret_cast<void*>(this->Data.data());

	ULONG out = 0;
	auto success = pStm->Read(pv, Length, &out);
	bool result(SUCCEEDED(success) && out == Length);

	if (!result)
		this->Data.resize(size);

	return result;
}

bool ScaffoldByteStream::WriteToStream(IStream* pStm) const
{
	const size_t Length(this->Data.size());
	auto pcv = reinterpret_cast<const void*>(this->Data.data());

	ULONG out = 0;
	auto success = pStm->Write(pcv, Length, &out);

	return SUCCEEDED(success) && out == Length;
}

bool ScaffoldByteStream::Read(data_t* Value, size_t Size)
{
	if (this->CurrentOffset <= this->Data.size()
		&& Size <= this->Data.size() - this->CurrentOffset)
	{
		auto Position = &this->Data[this->CurrentOffset];
		std::memcpy(Value, Position, Size);
		this->CurrentOffset += Size;
		return true;
	}

	if (Size > (std::numeric_limits<size_t>::max)() - this->CurrentOffset)
		this->CurrentOffset = (std::numeric_limits<size_t>::max)();
	else
		this->CurrentOffset += Size;

	return false;
}

void ScaffoldByteStream::Write(const data_t* Value, size_t Size)
{
	this->Data.insert(this->Data.end(), Value, Value + Size);
}

size_t ScaffoldByteStream::ReadBlockFromStream(IStream* pStm)
{
	ULONG out = 0;
	size_t Length = 0;

	if (pStm && SUCCEEDED(pStm->Read(&Length, sizeof(Length), &out)) && out == sizeof(Length))
	{
		if (this->ReadFromStream(pStm, Length))
			return Length;
	}

	return 0;
}

bool ScaffoldByteStream::WriteBlockToStream(IStream* pStm) const
{
	ULONG out = 0;
	const size_t Length = this->Data.size();

	if (pStm && SUCCEEDED(pStm->Write(&Length, sizeof(Length), &out)) && out == sizeof(Length))
		return this->WriteToStream(pStm);

	return false;
}

bool ScaffoldStreamReader::RegisterChange(void* newPtr)
{
	static_assert(sizeof(long) == sizeof(void*), "long and void* need to be of same size.");

	long oldPtr = 0;
	if (this->Load(oldPtr))
	{
		if (SUCCEEDED(SwizzleManagerClass::Instance.Here_I_Am(oldPtr, newPtr)))
			return true;

		this->EmitSwizzleWarning(oldPtr, newPtr, stream_debugging_t());
	}

	return false;
}

void ScaffoldStreamReader::EmitExpectEndOfBlockWarning(std::true_type) const
{
	Debug::Log("ScaffoldStreamReader - Read %X bytes instead of %X!\n",
		this->stream->Offset(), this->stream->Size());
}

void ScaffoldStreamReader::EmitLoadWarning(size_t size, std::true_type) const
{
	Debug::Log("ScaffoldStreamReader - Could not read data of length %u at %X of %X.\n",
		size, this->stream->Offset() - size, this->stream->Size());
}

void ScaffoldStreamReader::EmitExpectWarning(unsigned int found, unsigned int expect, std::true_type) const
{
	Debug::Log("ScaffoldStreamReader - Found %X, expected %X\n", found, expect);
}

void ScaffoldStreamReader::EmitSwizzleWarning(long id, void* pointer, std::true_type) const
{
	Debug::Log("ScaffoldStreamReader - Could not register change from %X to %p\n", id, pointer);
}
