#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DefaultPawn.h"
#include "ReplicTransportComponent.h"

#include "ReplicPIENetworkTestActors.generated.h"

class USceneComponent;
class UReplicPropertyObserver;

UENUM(BlueprintType)
enum class EReplicPIETestEnum : uint8
{
	Alpha = 0,
	Beta,
	Gamma
};

UCLASS()
class REPLICEDITOR_API UReplicPIEObserverSink : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 ChangeCount = 0;

	UPROPERTY()
	TObjectPtr<UObject> LastTargetObject = nullptr;

	UPROPERTY()
	FName LastPropertyName = NAME_None;

	UFUNCTION()
	void HandleObservedChange(UObject* TargetObject, FName PropertyName);
};

UCLASS()
class REPLICEDITOR_API UReplicPIETestTransportComponent : public UReplicTransportComponent
{
	GENERATED_BODY()

public:
	void SendUncheckedPropertyRequest(AActor* TargetHostActor, FName PropertyName, const FString& SerializedValue);
	void SendUncheckedContainerRequest(
		AActor* TargetHostActor,
		FName PropertyName,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		const FString& SerializedSecondaryValue = FString());
	void SendUncheckedEventRequest(AActor* TargetHostActor, FName EventName, const TArray<FReplicNamedValue>& Arguments);
};

UCLASS()
class REPLICEDITOR_API AReplicPIENetworkPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AReplicPIENetworkPlayerController();

	UPROPERTY(VisibleAnywhere, Category = "Replic|Tests")
	TObjectPtr<UReplicPIETestTransportComponent> ReplicTransportComponent = nullptr;
};

UCLASS()
class REPLICEDITOR_API AReplicPIENetworkActor : public AActor
{
	GENERATED_BODY()

public:
	AReplicPIENetworkActor();

	UPROPERTY(VisibleAnywhere, Category = "Replic|Tests")
	TObjectPtr<USceneComponent> RootSceneComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Replic|Tests")
	TObjectPtr<UReplicTransportComponent> ReplicTransportComponent = nullptr;

	UPROPERTY()
	int32 SharedValue = 0;

	UPROPERTY()
	int32 BatchedValue = 0;

	UPROPERTY()
	int32 EventValue = 0;

	UPROPERTY()
	TArray<int32> SharedArray;

	UPROPERTY()
	TSet<FName> NameSetState;

	UPROPERTY()
	TSet<int32> IntSetState;

	UPROPERTY()
	TMap<FName, int32> NameIntMapState;

	UPROPERTY()
	TMap<int32, int32> IntIntMapState;

	UPROPERTY()
	EReplicPIETestEnum EnumState = EReplicPIETestEnum::Alpha;

	UPROPERTY()
	TObjectPtr<AReplicPIENetworkActor> LinkedActor = nullptr;

	UPROPERTY()
	TObjectPtr<AReplicPIENetworkActor> LastEventActor = nullptr;

	UPROPERTY()
	TSubclassOf<AActor> LastEventClass;

	UPROPERTY()
	bool bAllowCustomWrite = false;

	UPROPERTY()
	bool bAllowCustomEvent = false;

	UFUNCTION()
	void MarkedPulse(int32 Delta);

	UFUNCTION()
	void MarkedVectorPulse(FVector Delta);

	UFUNCTION()
	void MarkedReferencePulse(AReplicPIENetworkActor* ActorValue, TSubclassOf<AActor> ClassValue);

	UFUNCTION()
	bool CanReplicWrite_SharedValue(AActor* RequestingActor);

	UFUNCTION()
	bool CanReplicWrite_SharedArray(AActor* RequestingActor);

	UFUNCTION()
	bool CanReplicCall_MarkedPulse(AActor* RequestingActor);
};

UCLASS()
class REPLICEDITOR_API AReplicPIEComponentLoadActor : public AActor
{
	GENERATED_BODY()

public:
	AReplicPIEComponentLoadActor();

	UPROPERTY(VisibleAnywhere, Category = "Replic|Tests")
	TObjectPtr<USceneComponent> RootSceneComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Replic|Tests")
	TObjectPtr<UReplicTransportComponent> ReplicTransportComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Replic|Tests")
	TArray<TObjectPtr<USceneComponent>> TrackedComponents;
};

UCLASS()
class REPLICEDITOR_API AReplicPIENetworkGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AReplicPIENetworkGameMode();
	virtual void GetSeamlessTravelActorList(bool bToTransition, TArray<AActor*>& ActorList) override;
};
