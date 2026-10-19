#include <Scaffold.h>

#include <LoadOptionsClass.h>

#include <Ext/TAction/Body.h>
#include <Ext/House/Body.h>
#include <Ext/Rules/Body.h>
#include <Ext/TaskForce/Body.h>
#include <Ext/Techno/Body.h>
#include <Ext/TechnoType/Body.h>
#include <Ext/TEvent/Body.h>
#include <Ext/WarheadType/Body.h>
#include <Ext/ScriptType/Body.h>
#include <Ext/Scenario/Body.h>
#include <Ext/TeamType/Body.h>
#include <Ext/Script/Body.h>
#include <Ext/Event/Body.h>

#include <New/TextBox/MapTextBoxClass.h>
#include <New/TextBox/TechnoTextBoxClass.h>
#include <New/TextBox/WaypointTextBoxClass.h>
#include <New/TextBox/TextBoxTypeClass.h>
#include <New/FootPath/FootPathVisualizer.h>
#include <New/RadarMark/RadarMark.h>
#include <New/ChronoTeleport/ChronoTeleport.h>
#include <New/ChoiceBox/MapChoiceBoxClass.h>
#include <New/ChoiceBox/WaypointChoiceBoxClass.h>
#include <New/ChoiceBox/ScreenChoiceBoxClass.h>
#include <New/ChoiceBox/ChoiceBoxTypeClass.h>
#include <New/TriggerGroup/TriggerGroupClass.h>
#include <New/AttachEffect/AttachEffectTypeClass.h>
#include <Utilities/Debug.h>

#include <utility>

#pragma region Implementation details

#pragma region Concepts

// a hack to check if some type can be used as a specialization of a template
template <template <class...> class Template, class... Args>
void DerivedFromSpecialization(const Template<Args...>&);

template <class T, template <class...> class Template>
concept DerivedFromSpecializationOf =
	requires(const T & t) { DerivedFromSpecialization<Template>(t); };

template<typename TExt>
concept HasExtMap = requires { { TExt::ExtMap } -> DerivedFromSpecializationOf<Container>; };

template<typename TExt>
concept ExtDataConsiderPointerInvalidation = HasExtMap<TExt> && requires{
	{ TExt::ShouldConsiderInvalidatePointer }->std::convertible_to<const bool>;
}&& TExt::ShouldConsiderInvalidatePointer == true;

template <typename T>
concept Clearable = requires { T::Clear(); };

template <typename T>
concept PointerInvalidationSubscribable =
	requires (void* ptr, bool removed) { T::PointerGotInvalid(ptr, removed); };

template <typename T>
concept GlobalSaveLoadable = requires
{
	T::LoadGlobals(std::declval<ScaffoldStreamReader&>());
	T::SaveGlobals(std::declval<ScaffoldStreamWriter&>());
};

template <typename TAction, typename TProcessed, typename... ArgTypes>
concept DispatchesAction =
	requires (ArgTypes... args) { TAction::template Process<TProcessed>(args...); };

#pragma endregion

// calls:
// T::Clear()
// T::ExtMap.Clear()
struct ClearAction
{
	template <typename T>
	static bool Process()
	{
		if constexpr (Clearable<T>)
			T::Clear();
		else if constexpr (HasExtMap<T>)
			T::ExtMap.Clear();

		return true;
	}
};

// calls:
// T::PointerGotInvalid(void*, bool)
// T::ExtMap.PointerGotInvalid(void*, bool)
struct InvalidatePointerAction
{
	template <typename T>
	static bool Process(void* ptr, bool removed)
	{
		if constexpr (PointerInvalidationSubscribable<T>)
			T::PointerGotInvalid(ptr, removed);
		else if constexpr (ExtDataConsiderPointerInvalidation<T>)
			T::ExtMap.PointerGotInvalid(ptr, removed);

		return true;
	}
};

// calls:
// T::LoadGlobals(ScaffoldStreamReader&)
struct LoadGlobalsAction
{
	template <typename T>
	static bool Process(IStream* pStm)
	{
		if constexpr (GlobalSaveLoadable<T>)
		{
			ScaffoldByteStream stm(0);
			stm.ReadBlockFromStream(pStm);
			ScaffoldStreamReader reader(stm);

			return T::LoadGlobals(reader) && reader.ExpectEndOfBlock();
		}
		else
		{
			return true;
		}
	}
};

// calls:
// T::SaveGlobals(ScaffoldStreamWriter&)
struct SaveGlobalsAction
{
	template <typename T>
	static bool Process(IStream* pStm)
	{
		if constexpr (GlobalSaveLoadable<T>)
		{
			ScaffoldByteStream stm;
			ScaffoldStreamWriter writer(stm);

			return T::SaveGlobals(writer) && stm.WriteBlockToStream(pStm);
		}
		else
		{
			return true;
		}
	}
};

struct CompleteLoadAction
{
	template <typename T>
	static bool Process()
	{
		if constexpr (requires { T::CompleteLoad(); })
			return T::CompleteLoad();
		else
			return true;
	}
};

// this is a complicated thing that calls methods on classes. add types to the
// instantiation of this type, and the most appropriate method for each type
// will be called with no overhead of virtual functions.
template <typename... RegisteredTypes>
struct TypeRegistry
{
	__forceinline static void Clear()
	{
		dispatch_mass_action<ClearAction>();
	}

	__forceinline static void InvalidatePointer(void* ptr, bool removed)
	{
		dispatch_mass_action<InvalidatePointerAction>(ptr, removed);
	}

	__forceinline static bool LoadGlobals(IStream* pStm)
	{
		return dispatch_mass_action<LoadGlobalsAction>(pStm);
	}

	__forceinline static bool SaveGlobals(IStream* pStm)
	{
		return dispatch_mass_action<SaveGlobalsAction>(pStm);
	}

	__forceinline static bool CompleteLoad()
	{
		return dispatch_mass_action<CompleteLoadAction>();
	}

private:
	// TAction: the method dispatcher class to call with each type
	// ArgTypes: the argument types to call the method dispatcher's Process() method
	template <typename TAction, typename... ArgTypes>
		requires (DispatchesAction<TAction, RegisteredTypes, ArgTypes...> && ...)
	__forceinline static bool dispatch_mass_action(ArgTypes... args)
	{
		// (pack expression op ...) is a fold expression which
		// unfolds the parameter pack into a full expression
		return (TAction::template Process<RegisteredTypes>(args...) && ...);
	}
};

#pragma endregion

// Add more class names as you like. Keep the order append-only for types that
// implement LoadGlobals/SaveGlobals: it is the on-disk global save stream order.
using ScaffoldTypeRegistry = TypeRegistry <
	// Ext classes
	HouseExt,
	RulesExt,
	TActionExt,
	ScenarioExt,
	TechnoExt,
	TechnoTypeExt,
	TaskForceExt,
	TEventExt,
	WarheadTypeExt,
	ScriptTypeExt,
	TeamTypeExt,
	ScriptExt,
	MapTextBoxClass,
	TechnoTextBoxClass,
	WaypointTextBoxClass,
	TextBoxTypeClass,
	ChoiceBoxTypeClass,
	MapChoiceBoxClass,
	WaypointChoiceBoxClass,
	ScreenChoiceBoxClass,
	FootPathVisualizer,
	ChronoTeleportService,
	TriggerGroupClass,
	AttachEffectTypeClass,
	EventExt,
	RadarMark
	// other classes
> ;

DEFINE_HOOK(0x7258D0, AnnounceInvalidPointer, 0x6)
{
	GET(AbstractClass* const, pInvalid, ECX);
	GET(bool const, removed, EDX);

	ScaffoldTypeRegistry::InvalidatePointer(pInvalid, removed);

	return 0;
}

DEFINE_HOOK(0x685659, Scenario_ClearClasses, 0xa)
{
	ScaffoldTypeRegistry::Clear();
	MapChoiceBoxClass::ResetFrameState();
	MapTextBoxClass::ResetFrameState();
	return 0;
}

// Ares saves its things at the end of the save
// Scaffold will save the things at the beginning of the save
// Considering how DTA gets the scenario name, I decided to save it after Rules - secsome

DEFINE_HOOK(0x67D32C, SaveGame_Scaffold, 0x5)
{
	GET(IStream*, pStm, ESI);

	if (!ScaffoldTypeRegistry::SaveGlobals(pStm))
		Debug::FatalErrorAndExit("SaveGame - Failed to save Scaffold data!\n");

	return 0;
}

DEFINE_HOOK(0x67E826, LoadGame_Scaffold, 0x6)
{
	GET(IStream*, pStm, ESI);

	if (!ScaffoldTypeRegistry::LoadGlobals(pStm))
		Debug::FatalErrorAndExit("LoadGame - Failed to load Scaffold data!\n");

	return 0;
}

// 0x67E680 calls SwizzleManager::Reset. Addresses are fixed before this call.
DEFINE_HOOK(0x67E685, LoadGame_Scaffold_RestoreGlobals, 0x5)
{
	if (!ScaffoldTypeRegistry::CompleteLoad())
		Debug::FatalErrorAndExit("LoadGame - Failed to restore Scaffold synchronization state!\n");
	return 0;
}

DEFINE_HOOK(0x67D04E, GameSave_SavegameInformation, 0x7)
{
	REF_STACK(SavegameInformation, Info, STACK_OFFSET(0x4A4, -0x3F4));

	Info.InternalVersion = Info.InternalVersion + SAVEGAME_ID;
	strncat(Info.ExecutableName.data(),
		" + Scaffold " FILE_VERSION_STR,
		Info.ExecutableName.Size - sizeof(" + Chang_zhi Custom Scaffold " FILE_VERSION_STR)
	);

	return 0;
}

DEFINE_HOOK_AGAIN(0x67FD9D, LoadOptionsClass_GetFileInfo, 0x7)
DEFINE_HOOK(0x67FDB1, LoadOptionsClass_GetFileInfo, 0x7)
{
	GET(SavegameInformation*, Info, ESI);
	Info->InternalVersion = Info->InternalVersion - SAVEGAME_ID;
	return 0;
}
