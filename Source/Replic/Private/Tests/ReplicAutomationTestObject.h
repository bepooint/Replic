#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"

#include "ReplicAutomationTestObject.generated.h"

UENUM()
enum class EReplicAutomationEnum : uint8
{
	Normal = 0,
	Rare,
	Epic
};

USTRUCT()
struct FReplicAutomationStruct
{
	GENERATED_BODY()

	UPROPERTY()
	FName ItemName = NAME_None;

	UPROPERTY()
	int32 Amount = 0;

	UPROPERTY()
	EReplicAutomationEnum Rarity = EReplicAutomationEnum::Normal;

	UPROPERTY()
	TSubclassOf<AActor> PreviewClass;
};

UCLASS()
class UReplicAutomationTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 MarkedInt = 7;

	UPROPERTY()
	EReplicAutomationEnum MarkedEnum = EReplicAutomationEnum::Normal;

	UPROPERTY()
	TObjectPtr<UObject> MarkedObject = nullptr;

	UPROPERTY()
	TSubclassOf<AActor> MarkedClass;

	UPROPERTY()
	TArray<int32> MarkedArray;

	UPROPERTY()
	TSet<FName> MarkedSet;

	UPROPERTY()
	TMap<FName, int32> MarkedMap;

	UPROPERTY()
	FReplicAutomationStruct MarkedStruct;

	UPROPERTY()
	TArray<int32> ScratchArray;

	UPROPERTY()
	TSet<FName> ScratchSet;

	UPROPERTY()
	TMap<FName, int32> ScratchMap;

	UPROPERTY()
	FReplicAutomationStruct ScratchStruct;

	UPROPERTY()
	TObjectPtr<UObject> ScratchObject = nullptr;

	UPROPERTY()
	TSubclassOf<AActor> ScratchClass;

	UPROPERTY()
	int32 LastEventCount = 0;

	UPROPERTY()
	FName LastEventItem = NAME_None;

	UPROPERTY()
	TArray<int32> LastEventValues;

	UPROPERTY()
	TObjectPtr<UObject> LastEventObject = nullptr;

	UPROPERTY()
	TSubclassOf<AActor> LastEventClass;

	UFUNCTION()
	void MarkedEvent(int32 Count, FName Item, TArray<int32> Values);

	UFUNCTION()
	void MarkedReferenceEvent(UObject* ObjectValue, TSubclassOf<AActor> ClassValue);
};

UCLASS()
class UReplicAutomationHelperObject : public UObject
{
	GENERATED_BODY()
};
