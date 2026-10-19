#pragma once

class TechnoClass;
class ScaffoldStreamWriter;
class ScaffoldStreamReader;

namespace RadarMarkAuto
{
	void Sync();

	void Reset();

	// 存读档：记录里有 Id 和 Settled，读档后 1109 的"已清除"状态才能保持
	bool SaveGlobals(ScaffoldStreamWriter& Stm);
	bool LoadGlobals(ScaffoldStreamReader& Stm);

	// Swizzle 完成后重建对象记录，保持 Id 和 Settled。
	bool CompleteLoad();

	void PointerGotInvalid(void* ptr);

	void Clear();
}
