#include "InteropApiTable.h"

#include <Interop/Api/ApiSelect.h>

#include "InteropModule.h"
#include "ScaffoldInterop.h"

#include <Utilities/Debug.h>

namespace
{
	InteropApi::Functions s_functions {};
	InteropApi::Level s_level = InteropApi::Level::V1_0_0;

#define GEN_INTEROP_RESOLVE(isRequired, member, fnType, exportName) \
	s_functions.member = InteropApi::ResolveInteropExport<InteropApi::fnType>(hProvider, exportName);

	// 一个版本一行：清单决定解析哪些导出，版本模块决定 AE_Attach 签名。
#define GEN_INTEROP_LOAD_LEVEL(NS, LIST) \
	do \
	{ \
		LIST(GEN_INTEROP_RESOLVE) \
		s_functions.AE_Attach = NS::LoadAEAttach(hProvider); \
	} while (false)

	void LoadLevel(HMODULE hProvider, InteropApi::Level level)
	{
		switch (level)
		{
		case InteropApi::Level::V1_2_0:
			GEN_INTEROP_LOAD_LEVEL(InteropApi::V1_2_0, FOREACH_INTEROP_FN_V1_2_0);
			break;

		case InteropApi::Level::V1_1_0:
			GEN_INTEROP_LOAD_LEVEL(InteropApi::V1_1_0, FOREACH_INTEROP_FN_V1_1_0);
			break;

		case InteropApi::Level::V1_0_0:
			GEN_INTEROP_LOAD_LEVEL(InteropApi::V1_0_0, FOREACH_INTEROP_FN_V1_0_0);
			break;
		}
	}

	// 成员名与清单同源，避免字段与查找逻辑脱节。
	void Publish()
	{
#define GEN_INTEROP_PUBLISH(isRequired, member, fnType, exportName) \
	ScaffoldInterop::member = s_functions.member;

		FOREACH_INTEROP_FN_SHARED(GEN_INTEROP_PUBLISH)
		FOREACH_INTEROP_FN_VARIABLES(GEN_INTEROP_PUBLISH)

#undef GEN_INTEROP_PUBLISH

		ScaffoldInterop::AE_Attach = s_functions.AE_Attach;
	}
}

void InteropApiTable::Load(HMODULE hProvider, const InteropAPIVersion& provided)
{
	s_functions = {};
	s_level = InteropApi::SelectLevel(provided);

	LoadLevel(hProvider, s_level);
	Publish();
}

void InteropApiTable::Unload()
{
	s_functions = {};
	s_level = InteropApi::Level::V1_0_0;

	Publish();
}

bool InteropApiTable::Verify(HMODULE hProvider)
{
	bool allRequiredPresent = true;

#define GEN_INTEROP_VERIFY(isRequired, member, fnType, exportName) \
	if (!s_functions.member) \
	{ \
		if (isRequired) \
			allRequiredPresent = false; \
		\
		InteropModule::ReportUnresolvedExport(hProvider, exportName, isRequired); \
	}

	switch (s_level)
	{
	case InteropApi::Level::V1_2_0:
		FOREACH_INTEROP_FN_V1_2_0(GEN_INTEROP_VERIFY)
		break;

	case InteropApi::Level::V1_1_0:
		FOREACH_INTEROP_FN_V1_1_0(GEN_INTEROP_VERIFY)
		break;

	case InteropApi::Level::V1_0_0:
		FOREACH_INTEROP_FN_V1_0_0(GEN_INTEROP_VERIFY)
		break;
	}

#undef GEN_INTEROP_VERIFY

	// AE_Attach 可选：老提供方由版本模块转发，连 11 参版本都没有才算缺失。
	if (!s_functions.AE_Attach)
		InteropModule::ReportUnresolvedExport(hProvider, "AE_Attach", false);

	return allRequiredPresent;
}
