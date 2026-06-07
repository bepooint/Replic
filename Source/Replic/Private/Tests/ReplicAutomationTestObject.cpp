#include "Tests/ReplicAutomationTestObject.h"

void UReplicAutomationTestObject::MarkedEvent(int32 Count, FName Item, TArray<int32> Values)
{
	LastEventCount = Count;
	LastEventItem = Item;
	LastEventValues = MoveTemp(Values);
}
