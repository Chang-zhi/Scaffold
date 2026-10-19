#include <New/RadarMark/RadarMark.h>
#include <New/RadarMark/RadarMarkAuto.h>
#include <New/RadarMark/RadarMarkTeamAuto.h>
#include <New/RadarMark/RadarMarkNative.h>

#include <Syringe.h>
#include <Helpers/Macro.h>

DEFINE_HOOK(0x656EC0, RadarClass_Render_RadarMarkBegin, 0x5)
{
	RadarMarkAuto::Sync();
	RadarMarkTeamAuto::Sync();
	RadarMarkInternal::OnRenderBegin();
	return 0;
}

DEFINE_HOOK(0x657537, RadarClass_Render_RadarMarkPaint, 0x5)
{
	RadarMarkInternal::OnRenderPaint();
	return 0;
}

// Only the scoped local visual constructor takes this branch. Provider constructor
// hooks at 0x421EA0 and 0x4226F6 still execute for genuine extension initialization.
DEFINE_HOOK(0x4220AA, AnimClass_CTOR_RadarMarkVisual, 0x6)
{
	GET(AnimClass*, anim, ESI);
	return RadarMarkNative::ConstructVisual(anim);
}

// PB has already released its AnimExt at 0x422967 before reaching this site.
DEFINE_HOOK(0x422971, AnimClass_DTOR_RadarMarkVisual, 0x6)
{
	GET(AnimClass*, anim, ESI);
	return RadarMarkNative::DestructVisual(anim);
}

// Original fastcall ABI is preserved by the forwarding function returned here.
DEFINE_HOOK(0x4AED70, CC_Draw_Shape_RadarMarkVisual, 0x6)
{
	return RadarMarkNative::RedirectShape();
}
