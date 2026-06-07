#include "ReplicTypes.h"

#include "ReplicTransportComponent.h"

FString FReplicTargetDescriptor::ToKey() const
{
	return FString::Printf(TEXT("%d|%s|%s"), static_cast<int32>(Kind), *ObjectName.ToString(), *ClassPath.ToString());
}

void FReplicStateArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	if (!Owner)
	{
		return;
	}

	for (const int32 Index : AddedIndices)
	{
		Owner->HandleReplicatedStateEntryChanged(Items[Index]);
	}
}

void FReplicStateArray::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	if (!Owner)
	{
		return;
	}

	for (const int32 Index : ChangedIndices)
	{
		Owner->HandleReplicatedStateEntryChanged(Items[Index]);
	}
}

void FReplicStateArray::PostReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	if (!Owner)
	{
		return;
	}

	for (const int32 Index : RemovedIndices)
	{
		Owner->HandleReplicatedStateEntryRemoved(Index);
	}
}
