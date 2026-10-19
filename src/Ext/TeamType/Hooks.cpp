#include "Body.h"

#include <Helpers/Macro.h>

// 成功和失败两个 INI 返回路径均与 Phobos 使用相同的钩子起点。
DEFINE_HOOK_AGAIN(0x6F1535, TeamTypeClass_LoadFromINI, 0xA)
DEFINE_HOOK(0x6F1528, TeamTypeClass_LoadFromINI, 0xA)
{
	GET(TeamTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0xCC);

	TeamTypeExt::ExtMap.FindOrAllocate(pItem);
	TeamTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}

// 与 Phobos 的构造钩子使用相同的起点和长度，避免补丁区间交错。
DEFINE_HOOK(0x6F08E6, TeamTypeClass_CTOR, 0x6)
{
	GET(TeamTypeClass*, pItem, ESI);
	TeamTypeExt::ExtMap.TryAllocate(pItem);
	return 0;
}

DEFINE_HOOK(0x6F08F0, TeamTypeClass_DTOR, 0x6)
{
	GET(TeamTypeClass*, pItem, ECX);
	TeamTypeExt::ExtMap.Remove(pItem);
	return 0;
}

DEFINE_HOOK_AGAIN(0x6F1B90, TeamTypeClass_SaveLoad_Prefix, 0x8)
DEFINE_HOOK(0x6F1BB0, TeamTypeClass_SaveLoad_Prefix, 0x5)
{
	GET_STACK(TeamTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);
	TeamTypeExt::ExtMap.PrepareStream(pItem, pStm);
	return 0;
}

// 位于最后一次宿主 Swizzle 调用前，只会经过成功的读取路径。
DEFINE_HOOK(0x6F1C2E, TeamTypeClass_Load_Suffix, 0x5)
{
	TeamTypeExt::ExtMap.LoadStatic();
	return 0;
}

// 保留原版 HRESULT 的错误分支，仅在宿主保存成功后追加扩展。
DEFINE_HOOK(0x6F1BA4, TeamTypeClass_Save_Suffix, 0x6)
{
	GET(HRESULT, result, EAX);
	if (SUCCEEDED(result))
		TeamTypeExt::ExtMap.SaveStatic();
	return 0;
}
