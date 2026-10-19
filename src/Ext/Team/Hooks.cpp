#include "Body.h"

#include <Helpers/Macro.h>

// 与 Phobos 使用相同的构造/析构钩子起点，避免补丁区间冲突。
// 0x6E8B46: mov dword [esi+0Ch], offset TeamClass vftable (7 字节, ESI = this)
DEFINE_HOOK(0x6E8B46, TeamClass_CTOR, 0x7)
{
	GET(TeamClass*, pThis, ESI);

	TeamExt::ExtMap.TryAllocate(pThis);

	return 0;
}

// 0x6E8EC6: 析构中的容器移除点 (9 字节, ESI = this)
DEFINE_HOOK(0x6E8EC6, TeamClass_DTOR, 0x9)
{
	GET(TeamClass*, pThis, ESI);

	TeamExt::ExtMap.Remove(pThis);

	return 0;
}

// TeamClass::Load (0x6EC450) / Save (0x6EC540)
DEFINE_HOOK_AGAIN(0x6EC540, TeamClass_SaveLoad_Prefix, 0xC)
DEFINE_HOOK(0x6EC450, TeamClass_SaveLoad_Prefix, 0x5)
{
	GET_STACK(TeamClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	TeamExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

// 位于最后一次宿主 Swizzle 调用前，只经过成功的读取路径
// 0x6EC530: push offset dword_B0C110 (5 字节)
DEFINE_HOOK(0x6EC530, TeamClass_Load_Suffix, 0x5)
{
	TeamExt::ExtMap.LoadStatic();

	return 0;
}

// 与 Phobos 一致: 0x6EC55A 覆盖 retn 0Ch (3 字节) 与对齐字节, return 0 重放后直接返回调用方
// 入口 EAX 即宿主 Save 的 HRESULT, 失败路径同样只会到达这里
DEFINE_HOOK(0x6EC55A, TeamClass_Save_Suffix, 0x5)
{
	GET(int, result, EAX);

	if (result >= 0)
	{
		TeamExt::ExtMap.SaveStatic();
	}

	return 0;
}
