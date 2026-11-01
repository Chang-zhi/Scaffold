#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Selection::Atomic
{
	constexpr std::size_t MaxPayload = 104;
	constexpr std::size_t MaxBytes = 4 * 1024 * 1024;
	constexpr std::size_t MaxRecords = 65535;
	constexpr std::size_t ChunkBytes = 88;

#pragma pack(push, 1)
	struct Chunk
	{
		std::uint32_t ID;
		std::uint32_t Total;
		std::uint32_t Offset;
		std::uint16_t Count;
		std::uint16_t Records;
		std::uint8_t Data[ChunkBytes];
	};
#pragma pack(pop)
	static_assert(sizeof(Chunk) == MaxPayload);

	struct Record
	{
		std::uint8_t Type = 0;
		std::uint8_t House = 0;
		std::vector<std::uint8_t> Payload;
	};

	inline bool Encode(const std::vector<Record>& records, std::vector<std::uint8_t>& bytes)
	{
		bytes.clear();
		if (records.empty() || records.size() > MaxRecords)
			return false;
		for (const auto& record : records)
		{
			if (record.Payload.size() > MaxPayload || bytes.size() + 3 + record.Payload.size() > MaxBytes)
			{
				bytes.clear();
				return false;
			}
			bytes.push_back(record.Type);
			bytes.push_back(record.House);
			bytes.push_back(static_cast<std::uint8_t>(record.Payload.size()));
			bytes.insert(bytes.end(), record.Payload.begin(), record.Payload.end());
		}
		return true;
	}

	inline bool Decode(const std::vector<std::uint8_t>& bytes, std::size_t count, std::vector<Record>& records)
	{
		records.clear();
		if (!count || count > MaxRecords || bytes.empty() || bytes.size() > MaxBytes)
			return false;
		for (std::size_t offset = 0; offset < bytes.size(); )
		{
			if (bytes.size() - offset < 3 || records.size() >= count)
				return false;
			Record record;
			record.Type = bytes[offset++];
			record.House = bytes[offset++];
			const std::size_t size = bytes[offset++];
			if (size > MaxPayload || size > bytes.size() - offset)
				return false;
			record.Payload.assign(bytes.begin() + offset, bytes.begin() + offset + size);
			records.push_back(std::move(record));
			offset += size;
		}
		return records.size() == count;
	}

	inline std::vector<Chunk> Split(std::uint32_t id, const std::vector<std::uint8_t>& bytes, std::size_t records)
	{
		std::vector<Chunk> chunks;
		if (!id || bytes.empty() || bytes.size() > MaxBytes || !records || records > MaxRecords)
			return chunks;
		for (std::size_t offset = 0; offset < bytes.size(); offset += ChunkBytes)
		{
			Chunk chunk {};
			chunk.ID = id;
			chunk.Total = static_cast<std::uint32_t>(bytes.size());
			chunk.Offset = static_cast<std::uint32_t>(offset);
			chunk.Count = static_cast<std::uint16_t>(std::min(ChunkBytes, bytes.size() - offset));
			chunk.Records = static_cast<std::uint16_t>(records);
			std::copy_n(bytes.begin() + offset, chunk.Count, chunk.Data);
			chunks.push_back(chunk);
		}
		return chunks;
	}

	enum class Result { Rejected, Waiting, Complete };

	// Native synchronized event streams are ordered per house. A partial batch
	// has no simulation effects. Only its final synchronized fragment commits.
	class Receiver
	{
		struct Assembly
		{
			std::uint32_t ID = 0;
			std::uint32_t Total = 0;
			std::uint16_t Records = 0;
			std::vector<std::uint8_t> Bytes;
		};
		std::array<Assembly, 128> Assemblies;

	public:
		Result Accept(unsigned int house, const Chunk& chunk, std::vector<Record>& ready)
		{
			ready.clear();
			if (house >= this->Assemblies.size() || !chunk.ID || !chunk.Records
				|| !chunk.Total || chunk.Total > MaxBytes || !chunk.Count || chunk.Count > ChunkBytes
				|| chunk.Offset > chunk.Total || chunk.Count > chunk.Total - chunk.Offset)
				return Result::Rejected;
			auto& assembly = this->Assemblies[house];
			if (!chunk.Offset)
			{
				assembly = {};
				assembly.ID = chunk.ID;
				assembly.Total = chunk.Total;
				assembly.Records = chunk.Records;
			}
			if (assembly.ID != chunk.ID || assembly.Total != chunk.Total || assembly.Records != chunk.Records
				|| assembly.Bytes.size() != chunk.Offset)
				return Result::Rejected;
			assembly.Bytes.insert(assembly.Bytes.end(), chunk.Data, chunk.Data + chunk.Count);
			if (assembly.Bytes.size() != assembly.Total)
				return Result::Waiting;
			const bool valid = Decode(assembly.Bytes, assembly.Records, ready);
			assembly = {};
			if (!valid)
				ready.clear();
			return valid ? Result::Complete : Result::Rejected;
		}

		void Clear() { this->Assemblies = {}; }
	};
}
