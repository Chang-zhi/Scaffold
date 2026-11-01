#pragma once

#include <GeneralStructures.h>

class AbstractClass;
class ObjectClass;
class FootClass;
class EventClass;
class TacticalClass;

class SelectionService
{
public:
	static bool Enabled();
	static void Configure();
	static void Install();
	static void ReceiveBatch(const EventClass& event);
	static void Clear();
	static bool CompleteLoad();
	static void PointerGotInvalid(void* pointer, bool removed);
	static bool AddSelectable(TacticalClass* tactical, ObjectClass* object, int x, int y);
	static bool CanSelect();
	static bool UseGroundedFlightDestination(FootClass* foot, const CellStruct* requested, CellStruct* destination);
	static ObjectClass* GetMoveRepresentative(ObjectClass* original, const CellStruct* cell, bool checkFog);
	static void BeforeEvent();
	static void PumpCommands();
};
