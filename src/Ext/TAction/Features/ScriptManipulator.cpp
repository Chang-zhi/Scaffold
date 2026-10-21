#include "ScriptManipulator.h"

#include <Ext/ScriptType/Body.h>
#include <Ext/TeamType/Body.h>

#include <TeamClass.h>
#include <ScriptClass.h>

#include <Interop/Services/ScenarioVariables.h>
#include <Utilities/Debug.h>

#include <string>
#include <cstdlib>

// ============================================================================
// Helper: lookup helpers - INI param → "0"+num → Find
// ============================================================================
static std::string MakeID(int param) { return "0" + std::to_string(param); }
static ScriptTypeClass* FindScript(int param) { return ScriptTypeClass::Find(MakeID(param).c_str()); }
static ScriptTypeClass* FindScript(const char* text) { return text && text[0] ? ScriptTypeClass::Find(text) : nullptr; }
static TeamTypeClass* FindTeam(int param) { return TeamTypeClass::Find(MakeID(param).c_str()); }

// ============================================================================
// Helper: backup script content before modification.
// The backup is taken lazily, right before the first modification.
// Returns ExtData for IsModified flag.
// ============================================================================
static ScriptTypeExt::ExtData* CaptureOriginalScriptContent(ScriptTypeClass* pScript)
{
	if (!pScript)
		return nullptr;

	auto const pExt = ScriptTypeExt::ExtMap.FindOrAllocate(pScript);

	if (pExt->OriginalActionsCount <= 0)
		pExt->CaptureOriginal();

	return pExt;
}

// ============================================================================
// Reset all TeamClass instances that use a given ScriptType
// ============================================================================
void ScriptManipulator::ResetTeamsUsingScript(ScriptTypeClass* pScript)
{
	if (!pScript)
		return;

	int nReset = 0;

	for (int i = 0; i < TeamClass::Array.Count; ++i)
	{
		auto const pTeam = TeamClass::Array.GetItem(i);
		if (!pTeam || !pTeam->CurrentScript)
			continue;

		if (pTeam->CurrentScript->Type != pScript)
			continue;

		++nReset;
		Debug::Log("[Scaffold] ResetTeamsUsingScript: Team #%d [%s] Script.CurrentMission=%d->0\n",
			i, pTeam->Type->ID, pTeam->CurrentScript->CurrentMission);

		// Set to -1 so NextMission() increments to 0 on next tick (action 0 will execute)
		pTeam->CurrentScript->CurrentMission = -1;
		pTeam->StepCompleted = true;
	}

	Debug::Log("[Scaffold] ResetTeamsUsingScript: Script [%s] reset %d teams\n",
		pScript->ID, nReset);
}

// ============================================================================
// 650: Clear script content
// ============================================================================
void ScriptManipulator::ClearScript(TActionClass* pThis)
{
	ScriptTypeClass* const pScript = FindScript(pThis->Param3);
	if (!pScript)
	{
		Debug::Log("[Scaffold] ClearScript: Param3=%d -> ScriptType not found!\n", pThis->Param3);
		return;
	}

	auto const pExt = CaptureOriginalScriptContent(pScript);

	Debug::Log("[Scaffold] ClearScript: Script [%s] Param3=%d ActionsCount=%d IsModified=%d\n",
		pScript->ID, pThis->Param3, pScript->ActionsCount, pExt->IsModified);
	pScript->ActionsCount = 0;
	for (int i = 0; i < ScriptTypeExt::ScriptActionCount; ++i)
		pScript->ScriptActions[i] = { 0, 0 };
	pExt->IsModified = true;

	Debug::Log("[Scaffold] ClearScript: Script [%s] cleared, ActionsCount=0 IsModified=1\n",
		pScript->ID);

	ResetTeamsUsingScript(pScript);
}

// ============================================================================
// 651: Copy script from source to destination
// ============================================================================
void ScriptManipulator::CopyScript(TActionClass* pThis)
{
	auto const pSrc = FindScript(pThis->Param3);
	auto const pDst = FindScript(pThis->Param4);

	if (!pSrc || !pDst)
		return;

	auto const pDstExt = CaptureOriginalScriptContent(pDst);

	int count = pSrc->ActionsCount;
	if (count > ScriptTypeExt::ScriptActionCount)
		count = ScriptTypeExt::ScriptActionCount;

	Debug::Log("[Scaffold] CopyScript: Src=[%s](%d actions) Dst=[%s] Param3=%d Param4=%d\n",
		pSrc->ID, pSrc->ActionsCount, pDst->ID, pThis->Param3, pThis->Param4);

	pDst->ActionsCount = count;
	for (int i = 0; i < count; ++i)
	{
		pDst->ScriptActions[i] = pSrc->ScriptActions[i];
	}
	// Clear remaining slots to prevent stale data leaks
	for (int i = count; i < ScriptTypeExt::ScriptActionCount; ++i)
	{
		pDst->ScriptActions[i] = { 0, 0 };
	}

	pDstExt->IsModified = true;
	ResetTeamsUsingScript(pDst);
}

// ============================================================================
// 652: Modify script by direct parameters
// ============================================================================
void ScriptManipulator::ModifyScriptByParam(TActionClass* pThis)
{
	auto const pScript = FindScript(pThis->Text);
	Debug::Log("[Scaffold] ModifyScriptByParam: Text=[%s] Param3=%d Param4=%d Param5=%d Param6=%d\n",
		pThis->Text, pThis->Param3, pThis->Param4, pThis->Param5, pThis->Param6);
	if (!pScript)
		return;

	int lineNum = pThis->Param3;
	int actionType = pThis->Param4;
	int param1 = pThis->Param5;
	int param2 = pThis->Param6;

	if (lineNum < 0 || lineNum >= ScriptTypeExt::ScriptActionCount)
		return;

	auto const pExt = CaptureOriginalScriptContent(pScript);

	// Argument 以 16 位高/低半区分别打包 param2 / param1, 显式掩码表明只取低 16 位。
	int encodedArg = ((param2 & 0xFFFF) << 16) | (param1 & 0xFFFF);

	pScript->ScriptActions[lineNum] = { actionType, encodedArg };

	if (lineNum >= pScript->ActionsCount)
		pScript->ActionsCount = lineNum + 1;

	pExt->IsModified = true;
	ResetTeamsUsingScript(pScript);
}

// ============================================================================
// 653: Modify script using local variables from ScenarioClass
//   Text=ScriptID  Param3=lineNum  Param4=varAction  Param5=varParam1  Param6=varParam2
// ============================================================================
void ScriptManipulator::ModifyScriptByLocalVar(TActionClass* pThis)
{
	auto const pScript = FindScript(pThis->Text);
	Debug::Log("[Scaffold] ModifyScriptByLocalVar: Text=[%s] Param3=%d Param4=%d Param5=%d Param6=%d\n",
		pThis->Text, pThis->Param3, pThis->Param4, pThis->Param5, pThis->Param6);
	if (!pScript)
		return;

	// 行号同样取自变量（局部变量），与 Param4~Param6 一致。
	// 变量索引越界等失败情形显式中止, 避免越界索引静默读成 0 后被当作脚本第 0 行。
	constexpr auto scope = ScenarioVariables::Scope::Local;

	int lineNum = 0;
	if (!ScenarioVariables::TryRead(scope, pThis->Param3, lineNum))
	{
		Debug::Log("[Scaffold] ModifyScriptByLocalVar: 行号变量读取失败 Param3=%d(需索引在 [0, %d) 且变量存储可用)\n",
			pThis->Param3, ScenarioVariables::LocalCount);
		return;
	}

	if (lineNum < 0 || lineNum >= ScriptTypeExt::ScriptActionCount)
		return;

	auto const pExt = CaptureOriginalScriptContent(pScript);

	int actionType = 0;
	int param1 = 0;
	int param2Val = 0;

	if (!ScenarioVariables::TryRead(scope, pThis->Param4, actionType)
		|| !ScenarioVariables::TryRead(scope, pThis->Param5, param1)
		|| !ScenarioVariables::TryRead(scope, pThis->Param6, param2Val))
	{
		Debug::Log("[Scaffold] ModifyScriptByLocalVar: 变量读取失败 Param4~6=%d/%d/%d(需索引在 [0, %d) 且变量存储可用)\n",
			pThis->Param4, pThis->Param5, pThis->Param6, ScenarioVariables::LocalCount);
		return;
	}

	// Argument 以 16 位高/低半区分别打包 param2 / param1, 显式掩码表明只取低 16 位。
	int encodedArg = ((param2Val & 0xFFFF) << 16) | (param1 & 0xFFFF);

	pScript->ScriptActions[lineNum] = { actionType, encodedArg };

	if (lineNum >= pScript->ActionsCount)
		pScript->ActionsCount = lineNum + 1;

	pExt->IsModified = true;
	ResetTeamsUsingScript(pScript);
}

// ============================================================================
// 654: Modify script using global variables from ScenarioClass
//   Text=ScriptID  Param3=lineNum  Param4=varAction  Param5=varParam1  Param6=varParam2
// ============================================================================
void ScriptManipulator::ModifyScriptByGlobalVar(TActionClass* pThis)
{
	auto const pScript = FindScript(pThis->Text);
	Debug::Log("[Scaffold] ModifyScriptByGlobalVar: Text=[%s] Param3=%d Param4=%d Param5=%d Param6=%d\n",
		pThis->Text, pThis->Param3, pThis->Param4, pThis->Param5, pThis->Param6);
	if (!pScript)
		return;

	// 行号同样取自变量（全局变量），与 Param4~Param6 一致。
	// 变量索引越界等失败情形显式中止, 避免越界索引静默读成 0 后被当作脚本第 0 行。
	constexpr auto scope = ScenarioVariables::Scope::Global;

	int lineNum = 0;
	if (!ScenarioVariables::TryRead(scope, pThis->Param3, lineNum))
	{
		Debug::Log("[Scaffold] ModifyScriptByGlobalVar: 行号变量读取失败 Param3=%d(需索引在 [0, %d) 且变量存储可用)\n",
			pThis->Param3, ScenarioVariables::GlobalCount);
		return;
	}

	if (lineNum < 0 || lineNum >= ScriptTypeExt::ScriptActionCount)
		return;

	auto const pExt = CaptureOriginalScriptContent(pScript);

	int actionType = 0;
	int param1 = 0;
	int param2Val = 0;

	if (!ScenarioVariables::TryRead(scope, pThis->Param4, actionType)
		|| !ScenarioVariables::TryRead(scope, pThis->Param5, param1)
		|| !ScenarioVariables::TryRead(scope, pThis->Param6, param2Val))
	{
		Debug::Log("[Scaffold] ModifyScriptByGlobalVar: 变量读取失败 Param4~6=%d/%d/%d(需索引在 [0, %d) 且变量存储可用)\n",
			pThis->Param4, pThis->Param5, pThis->Param6, ScenarioVariables::GlobalCount);
		return;
	}

	// Argument 以 16 位高/低半区分别打包 param2 / param1, 显式掩码表明只取低 16 位。
	int encodedArg = ((param2Val & 0xFFFF) << 16) | (param1 & 0xFFFF);

	pScript->ScriptActions[lineNum] = { actionType, encodedArg };

	if (lineNum >= pScript->ActionsCount)
		pScript->ActionsCount = lineNum + 1;

	pExt->IsModified = true;
	ResetTeamsUsingScript(pScript);
}

// ============================================================================
// Helper: lazily capture the original ScriptType index for a TeamType.
// Captured before the first rebind, so it is a no-op if already captured.
// ============================================================================
void ScriptManipulator::CaptureOriginalScriptIndex(void* pExtVoid, TeamTypeClass* pTeamType)
{
	auto const pExt = static_cast<TeamTypeExt::ExtData*>(pExtVoid);

	// Already captured
	if (pExt->OriginalScriptTypeIndex >= 0)
		return;

	if (!pTeamType->ScriptType)
		return;

	for (int j = 0; j < ScriptTypeClass::Array.Count; ++j)
	{
		if (ScriptTypeClass::Array.GetItem(j) == pTeamType->ScriptType)
		{
			pExt->OriginalScriptTypeIndex = j;
			break;
		}
	}
}

// ============================================================================
// Helper: reset all TeamClass instances of a given TeamType to re-run
// their script from action 0, optionally rebinding to a different ScriptType.
// ============================================================================
static void ResetTeamsOfType(TeamTypeClass* pTeamType, ScriptTypeClass* pBindTo = nullptr)
{
	for (int i = 0; i < TeamClass::Array.Count; ++i)
	{
		auto const pTeam = TeamClass::Array.GetItem(i);
		if (!pTeam || pTeam->Type != pTeamType)
			continue;

		if (pTeam->CurrentScript)
		{
			if (pBindTo)
				pTeam->CurrentScript->Type = pBindTo;
			pTeam->CurrentScript->CurrentMission = -1;
		}

		pTeam->StepCompleted = true;
	}
}

// ============================================================================
// 655: Rebind TeamType to a different ScriptType
// ============================================================================
void ScriptManipulator::RebindTeamTypeScript(TActionClass* pThis)
{
	auto const pTeamType = FindTeam(pThis->Param3);
	auto const pNewScript = FindScript(pThis->Param4);

	if (!pTeamType || !pNewScript)
		return;

	auto const pExt = TeamTypeExt::ExtMap.FindOrAllocate(pTeamType);
	CaptureOriginalScriptIndex(pExt, pTeamType);

	Debug::Log("[Scaffold] RebindTeamTypeScript: TeamType Param3=%d NewScript Param4=%d\n",
		pThis->Param3, pThis->Param4);

	pTeamType->ScriptType = pNewScript;
	ResetTeamsOfType(pTeamType, pNewScript);
}

// ============================================================================
// 656: Reset TeamType script binding to original
// ============================================================================
void ScriptManipulator::ResetTeamTypeScript(TActionClass* pThis)
{
	auto const pTeamType = FindTeam(pThis->Param3);
	if (!pTeamType)
		return;

	Debug::Log("[Scaffold] ResetTeamTypeScript: Param3=%d\n", pThis->Param3);

	auto const pExt = TeamTypeExt::ExtMap.FindOrAllocate(pTeamType);
	CaptureOriginalScriptIndex(pExt, pTeamType);

	if (pExt->OriginalScriptTypeIndex < 0)
		return;

	auto const pOriginalScript = ScriptTypeClass::Array.GetItem(pExt->OriginalScriptTypeIndex);
	pTeamType->ScriptType = pOriginalScript;
	ResetTeamsOfType(pTeamType, pOriginalScript);
}

// ============================================================================
// 657: Reset ALL TeamType script bindings to original
// ============================================================================
void ScriptManipulator::ResetAllTeamTypeScripts()
{
	Debug::Log("[Scaffold] ResetAllTeamTypeScripts\n");

	for (int i = 0; i < TeamTypeClass::Array.Count; ++i)
	{
		auto const pTeamType = TeamTypeClass::Array.GetItem(i);
		if (!pTeamType)
			continue;

		auto const pExt = TeamTypeExt::ExtMap.FindOrAllocate(pTeamType);
		CaptureOriginalScriptIndex(pExt, pTeamType);

		if (pExt->OriginalScriptTypeIndex < 0)
			continue;

		pTeamType->ScriptType = ScriptTypeClass::Array.GetItem(pExt->OriginalScriptTypeIndex);
	}

	// Reset all teams: rebind to their (now restored) TeamType's ScriptType
	for (int i = 0; i < TeamClass::Array.Count; ++i)
	{
		auto const pTeam = TeamClass::Array.GetItem(i);
		if (!pTeam)
			continue;

		if (pTeam->CurrentScript)
		{
			pTeam->CurrentScript->Type = pTeam->Type->ScriptType;
			pTeam->CurrentScript->CurrentMission = -1;
		}

		pTeam->StepCompleted = true;
	}
}

// ============================================================================
// Helper: restore script content from backup, if it was modified.
// Returns true if restoration actually happened.
// ============================================================================
static bool RestoreOriginalScriptContent(ScriptTypeClass* pScript)
{
	if (!pScript)
		return false;

	auto const pExt = ScriptTypeExt::ExtMap.Find(pScript);
	if (!pExt)
		return false;

	pExt->RestoreOriginal();
	return true;
}

// ============================================================================
// 658: Restore a single script content to its original (INI-defined) state
// ============================================================================
void ScriptManipulator::RestoreScriptContent(TActionClass* pThis)
{
	auto const pScript = FindScript(pThis->Param3);
	Debug::Log("[Scaffold] RestoreScriptContent: Param3=%d\n", pThis->Param3);

	if (RestoreOriginalScriptContent(pScript))
		ResetTeamsUsingScript(pScript);
}

// ============================================================================
// 659: Restore ALL modified script contents to original state
// ============================================================================
void ScriptManipulator::RestoreAllScriptContents()
{
	Debug::Log("[Scaffold] RestoreAllScriptContents\n");

	for (int i = 0; i < ScriptTypeClass::Array.Count; ++i)
	{
		auto const pScript = ScriptTypeClass::Array.GetItem(i);
		if (!pScript)
			continue;

		if (RestoreOriginalScriptContent(pScript))
			ResetTeamsUsingScript(pScript);
	}
}

// ============================================================================
// 660: Seek/jump script execution line for all instances of a TeamType
//   Param3=TeamType index  Param4=target line number (0-based, 0=first action)
// ============================================================================
void ScriptManipulator::SeekTeamTypeScript(TActionClass* pThis)
{
	auto const pTeamType = FindTeam(pThis->Param3);
	if (!pTeamType)
		return;

	int const targetLine = pThis->Param4;
	int const seekTo = (targetLine <= 0) ? -1 : (targetLine - 1);

	Debug::Log("[Scaffold] SeekTeamTypeScript: TeamType [%s] targetLine=%d seekTo=%d\n",
		pTeamType->ID, targetLine, seekTo);

	for (int i = 0; i < TeamClass::Array.Count; ++i)
	{
		auto const pTeam = TeamClass::Array.GetItem(i);
		if (!pTeam || pTeam->Type != pTeamType)
			continue;

		if (pTeam->CurrentScript)
			pTeam->CurrentScript->CurrentMission = seekTo;

		pTeam->StepCompleted = true;
	}
}

