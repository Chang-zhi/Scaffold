#pragma once

class TeamClass;
class TechnoClass;

class AttachEffectService
{
public:
	// 按 [AttachEffectTypes] 的键解析 AE 类型名; 找不到或提供方缺失时返回 nullptr。
	static const char* ResolveName(int key);

	// 施加 AE, 返回新建实例数。
	static int ApplyToTeam(TeamClass* pTeam, int nameIndex, int durationOverride);
	static int ApplyToTechno(TechnoClass* pTechno, int nameKey, int durationOverride);

	// 按 AE 类型名移除, 返回移除的实例数。
	static int RemoveFromTeam(TeamClass* pTeam, int nameIndex);
	static int RemoveFromTechno(TechnoClass* pTechno, int nameKey);

	// 按分组名移除(组名就是 Phobos 写在附加效果类型上的 Groups= 标签), 返回移除的实例数。
	static int RemoveGroupsFromTeam(TeamClass* pTeam, const char* groupName);
	static int RemoveGroupsFromTechno(TechnoClass* pTechno, const char* groupName);

	// 移除全部 AE, 返回移除的实例数。
	static int RemoveAllFromTeam(TeamClass* pTeam);
	static int RemoveAllFromTechno(TechnoClass* pTechno);
};
