#include "AttachEffectTypeClass.h"

#include <Scaffold.h>
#include <CCINIClass.h>

#include <Utilities/Debug.h>

#include <cstdlib>
#include <cstring>

template<>
const char* Enumerable<AttachEffectTypeClass>::GetMainSection()
{
	return "AttachEffectTypes";
}

void AttachEffectTypeClass::LoadFromINIList(CCINIClass* pINI)
{
	const char* const section = GetMainSection();
	const int count = pINI->GetKeyCount(section);

	for (int i = 0; i < count; ++i)
	{
		const char* const key = pINI->GetKeyName(section, i);

		if (!pINI->ReadString(section, key, "", Scaffold::readBuffer) || !Scaffold::readBuffer[0])
			continue;

		AttachEffectTypeClass* const pType = FindOrAllocate(Scaffold::readBuffer);

		if (strspn(key, "0123456789") == strlen(key))
			pType->Key = atoi(key);
		else
			Debug::Log("[Developer warning] [%s] key \"%s\" is not a number; the actions cannot reference it\n",
				section, key);
	}

	Debug::Log("[Scaffold] AttachEffectTypeClass: %d AE types\n", static_cast<int>(Array.size()));
}
