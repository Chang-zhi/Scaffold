#pragma once

class TeamClass;
class ScaffoldStreamWriter;
class ScaffoldStreamReader;

namespace RadarMarkTeamAuto
{
	// 每帧调用；内部按逻辑帧去重，同一帧重复调用只做一次。
	void Sync();

	// 丢弃全部记录和读档暂存数据，开始一次新的加载。
	void Reset();

	// 存读档：记录里有 Id 和 Settled，读档后已清除的队伍标记不会自己长回来
	bool SaveGlobals(ScaffoldStreamWriter& Stm);
	bool LoadGlobals(ScaffoldStreamReader& Stm);

	// Swizzle 完成后重建对象记录，保持 Id 和 Settled。
	bool CompleteLoad();

	void PointerGotInvalid(void* ptr);

	void Clear();
}
