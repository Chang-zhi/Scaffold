#pragma once

#include <Objidl.h>
#include <Ole2.h>
#include <memory>
#include <vector>

// Preserve the host's object format and Here_I_Am/Swizzle registrations.
namespace NativeSavegame
{
	constexpr size_t MaxObjectBytes = 1024 * 1024;
	struct StreamReleaser
	{
		void operator()(IStream* stream) const { stream->Release(); }
	};
	using MemoryStream = std::unique_ptr<IStream, StreamReleaser>;

	inline MemoryStream CreateStream()
	{
		IStream* stream = nullptr;
		if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
			return {};
		return MemoryStream(stream);
	}

	inline bool Save(IPersistStream* object, std::vector<unsigned char>& bytes)
	{
		bytes.clear();
		auto stream = CreateStream();
		if (!object || !stream || FAILED(object->Save(stream.get(), FALSE)))
			return false;
		STATSTG stat {};
		if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0
			|| stat.cbSize.QuadPart > MaxObjectBytes)
			return false;
		if (FAILED(stream->Seek(LARGE_INTEGER {}, STREAM_SEEK_SET, nullptr)))
			return false;
		bytes.resize(static_cast<size_t>(stat.cbSize.QuadPart));
		ULONG read = 0;
		return SUCCEEDED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read))
			&& read == bytes.size();
	}

	inline bool Load(IPersistStream* object, const std::vector<unsigned char>& bytes)
	{
		if (!object || bytes.empty() || bytes.size() > MaxObjectBytes)
			return false;
		auto stream = CreateStream();
		ULONG written = 0;
		if (!stream || FAILED(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written))
			|| written != bytes.size()
			|| FAILED(stream->Seek(LARGE_INTEGER {}, STREAM_SEEK_SET, nullptr))
			|| FAILED(object->Load(stream.get())))
			return false;
		ULARGE_INTEGER position {};
		return SUCCEEDED(stream->Seek(LARGE_INTEGER {}, STREAM_SEEK_CUR, &position))
			&& position.QuadPart == bytes.size();
	}
}
