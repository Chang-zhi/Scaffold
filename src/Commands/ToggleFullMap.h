#pragma once

#include "Command.h"
#include <New/FullMap/FullMap.h>

/// <summary>
/// 战术地图开关命令。注册进原版命令系统后，
/// 玩家可在 选项 → 键盘 里给 "Toggle_full_map" 绑定任意快捷键。
/// </summary>
class ToggleFullMapCommand : public AresCommandClass
{
public:
	virtual const char* GetName() const override
	{
		return "Toggle_full_map";
	}

	virtual const wchar_t* GetUIName() const override
	{
		return StringTable::TryFetchString("CMND:UINAME_FULLMAP", L"战术地图开关 Toggle tactical map");
	}

	virtual const wchar_t* GetUICategory() const override
	{
		const wchar_t* textPtr = StringTable::TryFetchString("CMND:UICATEGORY_SCAFFOLD");

		if (!textPtr || !*textPtr)
			textPtr = L"Scaffold";

		return textPtr;
	}

	virtual const wchar_t* GetUIDescription() const override
	{
		return StringTable::TryFetchString("CMND:UIDESCR_FULLMAP",
			L"打开/关闭全屏战术地图，也可双击右上角小地图切换\nToggle FullMap; double-click the top-right minimap to open or close it");
	}

	virtual void Execute(WWKey eInput) const override
	{
		FullMap::Toggle();
	}
};
