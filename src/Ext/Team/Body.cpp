#include "Body.h"

#include <Utilities/SavegameDef.h>
#include <Utilities/Stream.h>

TeamExt::ExtContainer TeamExt::ExtMap;

// =============================
// load / save

void TeamExt::ExtData::LoadFromStream(ScaffoldStreamReader& Stm)
{
	Extension<TeamClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
    
	this->TimedMissionLastFrame = -1;
}

void TeamExt::ExtData::SaveToStream(ScaffoldStreamWriter& Stm)
{
	Extension<TeamClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

template <typename T>
void TeamExt::ExtData::Serialize(T& Stm)
{
	Stm.Process(this->ScatterAttackGroups)
		.Process(this->LastProcessedMission)
		.Process(this->TimedMissionNode)
		.Process(this->TimedMissionLeft);
}

void TeamExt::ExtData::InvalidatePointer(void* ptr, bool bRemoved)
{
	// 从分散攻击的分组中移除失效成员，防止悬垂指针
	for (auto& group : this->ScatterAttackGroups)
	{
		group.erase(std::remove(group.begin(), group.end(), static_cast<FootClass*>(ptr)), group.end());
	}
}

// =============================
// container

TeamExt::ExtContainer::ExtContainer() : Container("TeamClass")
{ }

TeamExt::ExtContainer::~ExtContainer() = default;
