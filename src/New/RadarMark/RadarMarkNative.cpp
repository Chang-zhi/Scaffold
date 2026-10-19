#include "RadarMarkNative.h"
#include "RadarMark.h"

#include <YRpp.h>
#include <AnimClass.h>
#include <AnimTypeClass.h>
#include <ConvertClass.h>
#include <HouseClass.h>
#include <ScenarioClass.h>
#include <Surface.h>
#include <TechnoClass.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

namespace
{
	bool g_Constructing = false;
	AnimClass* g_Destroying = nullptr;
	bool g_ForwardingShape = false;
	std::unordered_map<AnimTypeClass*, AnimClass*> g_VisualAnims;

	struct DrawingContext
	{
		Surface* Surface;
		RectangleStruct Clip;
		RadarArt const* Art;
		std::vector<RectangleStruct>* Rectangles;
	};
	DrawingContext* g_Drawing = nullptr;

	class ConstructionScope
	{
		int SavedID;
	public:
		ConstructionScope() : SavedID(ScenarioClass::Instance->UniqueID)
		{
			// Use the game's local-only MoveFlash identity; PB excludes -2 from sync logs.
			ScenarioClass::Instance->UniqueID = -3;
			g_Constructing = true;
		}
		~ConstructionScope()
		{
			g_Constructing = false;
			ScenarioClass::Instance->UniqueID = SavedID;
		}
	};

	AnimClass* GetVisual(AnimTypeClass* type)
	{
		if (auto found = g_VisualAnims.find(type); found != g_VisualAnims.end())
			return found->second;
		if (!ScenarioClass::Instance || !Game::IsActive)
			return nullptr;

		AnimClass* anim;
		{
			ConstructionScope scope;
			anim = GameCreate<AnimClass>(type, CoordStruct { 0, 0, 0 }, 1, 1, 0x600, 0, false);
		}
		if (!anim)
			return nullptr;
		// The constructor hook bypasses unlimbo/AI and executes provider extension setup.
		// HasExtras only suppresses PB's attached-particle initialization during that setup.
		anim->HasExtras = false;
		anim->IsInert = true;
		anim->Paused = true;
		anim->IsInLogic = false;
		AnimClass::Array.Remove(anim);
		g_VisualAnims.emplace(type, anim);
		return anim;
	}

	void ReleaseVisuals()
	{
		for (auto const& pair : g_VisualAnims)
		{
			auto anim = pair.second;
			anim->OwnerObject = nullptr;
			anim->Owner = nullptr;
			g_Destroying = anim;
			// Dispatch the original scalar-deleting destructor and its provider cleanup hooks.
			using ScalarDelete = void* (__thiscall*)(AnimClass*, unsigned int);
			auto table = *reinterpret_cast<void***>(anim);
			reinterpret_cast<ScalarDelete>(table[0x20 / sizeof(void*)])(anim, 1);
			g_Destroying = nullptr;
		}
		g_VisualAnims.clear();
	}

	void __fastcall DrawShapeOnRadar(Surface* source, ConvertClass* palette, SHPStruct* image, int frame,
		const Point2D* position, const RectangleStruct* bounds, BlitterFlags flags, int remap,
		int zAdjust, ZGradient gradient, int brightness, int tint, SHPStruct* zShape,
		int zFrame, int xOffset, int yOffset)
	{
		auto& context = *g_Drawing;
		if (!image || !position)
			return;
		if (context.Art->Palette)
			palette = context.Art->Palette;
		if (!palette)
			return;

		auto data = image->GetData();
		if (!data || data->Frames <= 0)
			return;
		frame %= data->Frames;
		if (frame < 0)
			frame += data->Frames;
		RectangleStruct rectangle = image->GetFrameBounds(frame);
		int const halfWidth = (flags & BlitterFlags::Centered) != BlitterFlags::None ? data->Width / 2 : 0;
		int const halfHeight = (flags & BlitterFlags::Centered) != BlitterFlags::None ? data->Height / 2 : 0;
		rectangle.X += position->X - halfWidth;
		rectangle.Y += position->Y - halfHeight;
		if (rectangle.Width > 0 && rectangle.Height > 0)
			context.Rectangles->push_back(rectangle);

		// Keep provider-selected palettes, opacity, tint and centered positioning.
		// UI surfaces have no battlefield depth/alpha buffers.
		flags = static_cast<BlitterFlags>(static_cast<unsigned int>(flags) & ~0xF800u);
		if (context.Art->Opaque)
			flags = static_cast<BlitterFlags>((static_cast<unsigned int>(flags) & ~0x7u)
				| static_cast<unsigned int>(BlitterFlags::Plain));
		if (context.Art->Remap >= 0)
			remap = context.Art->Remap;
		brightness = static_cast<int>(std::clamp<std::int64_t>(
			static_cast<std::int64_t>(brightness) * context.Art->Brightness / 1000, 0, 2000));

		static const auto identity = []
		{
			std::array<byte, 256> values {};
			for (size_t i = 0; i < values.size(); ++i)
				values[i] = static_cast<byte>(i);
			return values;
		}();
		DWORD const savedRemap = palette->CurrentZRemap;
		palette->CurrentZRemap = reinterpret_cast<DWORD>(identity.data());
		g_ForwardingShape = true;
		CC_Draw_Shape(context.Surface, palette, image, frame, position, &context.Clip,
			flags, remap, 0, ZGradient::Ground, brightness, tint, nullptr, 0, 0, 0);
		g_ForwardingShape = false;
		palette->CurrentZRemap = savedRemap;
	}
}

RadarMarkNative::FrameScope::FrameScope()
{
	ReleaseVisuals();
}

RadarMarkNative::FrameScope::~FrameScope()
{
	ReleaseVisuals();
}

bool RadarMarkNative::Draw(Surface* surface, RectangleStruct const& clip, Point2D position,
	CoordStruct world, AnimTypeClass* type, int frame, TechnoClass* target,
	HouseClass* owner, RadarArt const& art, std::vector<RectangleStruct>& rectangles)
{
	if (!surface || !type)
		return false;
	auto anim = GetVisual(type);
	if (!anim)
		return false;
	anim->Owner = owner;
	// Direct, temporary drawing context; SetOwnerObject would alter the target's animation links.
	anim->OwnerObject = target;
	anim->Location = target ? CoordStruct { 0, 0, 0 } : world;
	anim->Animation.Value = std::max(0, frame - type->Start);
	anim->Animation.HasChanged = true;
	anim->AnimFlags = BlitterFlags::Centered | BlitterFlags::bf_400;
	anim->Invisible = false;
	anim->IsFogged = false;
	anim->UnderTemporal = false;
	anim->TranslucencyLevel = 0;
	position += art.Offset;
	DrawingContext context { surface, clip, &art, &rectangles };
	g_Drawing = &context;
	RectangleStruct bounds = clip;
	// RING1 has a special battlefield Direct3D path; the radar is a 2D UI surface.
	bool const savedDirect3D = Game::bDirect3DIsUseable;
	Game::bDirect3DIsUseable = false;
	anim->DrawIt(&position, &bounds);
	Game::bDirect3DIsUseable = savedDirect3D;
	g_Drawing = nullptr;
	anim->OwnerObject = nullptr;
	anim->Owner = nullptr;
	return true;
}

DWORD RadarMarkNative::ConstructVisual(AnimClass* anim)
{
	if (!g_Constructing)
		return 0;
	anim->IsInert = true;
	anim->HasExtras = true;
	anim->LoopDelay = 1;
	anim->RemainingIterations = 1;
	anim->Paused = true;
	// Preserve the provider constructor hook at 0x4226F6. Skip native gameplay setup,
	// RandomRate, particles, occupation bits, world registration and Start().
	return 0x4226F6;
}

DWORD RadarMarkNative::DestructVisual(AnimClass* anim)
{
	// Runs after the provider's 0x422967 destructor hook. Skip world-cell mutation.
	return anim && anim == g_Destroying ? 0x4229CF : 0;
}

DWORD RadarMarkNative::RedirectShape()
{
	return g_Drawing && !g_ForwardingShape ? reinterpret_cast<DWORD>(&DrawShapeOnRadar) : 0;
}
