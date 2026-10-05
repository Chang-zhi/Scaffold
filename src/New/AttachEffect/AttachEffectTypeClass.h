#pragma once

#include <Utilities/Enumerable.h>

class CCINIClass;
class ScaffoldStreamReader;
class ScaffoldStreamWriter;

class AttachEffectTypeClass final : public Enumerable<AttachEffectTypeClass>
{
public:
	int Key = -1;

	AttachEffectTypeClass(const char* const pTitle) : Enumerable(pTitle)
	{ }

	static void LoadFromINIList(CCINIClass* pINI);

	virtual void LoadFromINI(CCINIClass*) { }
	virtual void LoadFromStream(ScaffoldStreamReader&) { }
	virtual void SaveToStream(ScaffoldStreamWriter&) { }
};
