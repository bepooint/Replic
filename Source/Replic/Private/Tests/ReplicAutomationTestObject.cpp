#include "Tests/ReplicAutomationTestObject.h"

void UReplicAutomationTestObject::MarkedEvent(int32 Count, FName Item, TArray<int32> Values)
{
	LastEventCount = Count;
	LastEventItem = Item;
	LastEventValues = MoveTemp(Values);
}

void UReplicAutomationTestObject::MarkedReferenceEvent(UObject* ObjectValue, TSubclassOf<AActor> ClassValue)
{
	LastEventObject = ObjectValue;
	LastEventClass = ClassValue;
}
