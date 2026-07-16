#include "Tests/ReplicPIENetworkTestActors.h"

#include "ReplicTransportComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"

void UReplicPIEObserverSink::HandleObservedChange(UObject* TargetObject, FName PropertyName)
{
	++ChangeCount;
	LastTargetObject = TargetObject;
	LastPropertyName = PropertyName;
}

void UReplicPIETestTransportComponent::SendUncheckedPropertyRequest(AActor* TargetHostActor, FName PropertyName, const FString& SerializedValue)
{
	ServerRequestPropertyWrite(TargetHostActor, FReplicTargetDescriptor(), PropertyName, SerializedValue);
}

void UReplicPIETestTransportComponent::SendUncheckedContainerRequest(
	AActor* TargetHostActor,
	FName PropertyName,
	EReplicContainerDeltaOperation Operation,
	const FString& SerializedPrimaryValue,
	const FString& SerializedSecondaryValue)
{
	ServerRequestContainerDelta(
		TargetHostActor,
		FReplicTargetDescriptor(),
		PropertyName,
		Operation,
		SerializedPrimaryValue,
		SerializedSecondaryValue);
}

void UReplicPIETestTransportComponent::SendUncheckedEventRequest(AActor* TargetHostActor, FName EventName, const TArray<FReplicNamedValue>& Arguments)
{
	ServerRequestEvent(TargetHostActor, FReplicTargetDescriptor(), EventName, Arguments);
}

AReplicPIENetworkPlayerController::AReplicPIENetworkPlayerController()
{
	ReplicTransportComponent = CreateDefaultSubobject<UReplicPIETestTransportComponent>(TEXT("ReplicTransportComponent"));
}

AReplicPIENetworkActor::AReplicPIENetworkActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;
	ReplicTransportComponent = CreateDefaultSubobject<UReplicTransportComponent>(TEXT("ReplicTransportComponent"));

	FReplicComponentTransformSettings RootTransformSettings;
	RootTransformSettings.ComponentName = RootSceneComponent->GetFName();
	RootTransformSettings.TransformSpace = EReplicTransformSpace::Relative;
	RootTransformSettings.bReplicateRotation = true;
	RootTransformSettings.bPersistentState = true;
	ReplicTransportComponent->ComponentTransformSettings.Add(RootTransformSettings);
}

void AReplicPIENetworkActor::MarkedPulse(int32 Delta)
{
	EventValue += Delta;
}

void AReplicPIENetworkActor::MarkedVectorPulse(FVector Delta)
{
	EventValue += FMath::RoundToInt(Delta.Size());
}

void AReplicPIENetworkActor::MarkedReferencePulse(AReplicPIENetworkActor* ActorValue, TSubclassOf<AActor> ClassValue)
{
	LastEventActor = ActorValue;
	LastEventClass = ClassValue;
}

bool AReplicPIENetworkActor::CanReplicWrite_SharedValue(AActor* RequestingActor)
{
	return bAllowCustomWrite;
}

bool AReplicPIENetworkActor::CanReplicWrite_SharedArray(AActor* RequestingActor)
{
	return bAllowCustomWrite;
}

bool AReplicPIENetworkActor::CanReplicCall_MarkedPulse(AActor* RequestingActor)
{
	return bAllowCustomEvent;
}

AReplicPIEComponentLoadActor::AReplicPIEComponentLoadActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;
	ReplicTransportComponent = CreateDefaultSubobject<UReplicTransportComponent>(TEXT("ReplicTransportComponent"));

	constexpr int32 ComponentCount = 24;
	TrackedComponents.Reserve(ComponentCount);
	ReplicTransportComponent->ComponentTransformSettings.Reserve(ComponentCount);
	for (int32 Index = 0; Index < ComponentCount; ++Index)
	{
		const FName ComponentName(*FString::Printf(TEXT("TrackedComponent_%02d"), Index));
		USceneComponent* Component = CreateDefaultSubobject<USceneComponent>(ComponentName);
		Component->SetupAttachment(RootSceneComponent);
		TrackedComponents.Add(Component);

		FReplicComponentTransformSettings Settings;
		Settings.ComponentName = ComponentName;
		Settings.TransformSpace = EReplicTransformSpace::Relative;
		Settings.bReplicateLocation = true;
		Settings.bPersistentState = true;
		Settings.MinUpdateIntervalSeconds = 0.0f;
		Settings.LocationThreshold = 0.0f;
		ReplicTransportComponent->ComponentTransformSettings.Add(Settings);
	}
}

AReplicPIENetworkGameMode::AReplicPIENetworkGameMode()
{
	PlayerControllerClass = AReplicPIENetworkPlayerController::StaticClass();
	DefaultPawnClass = ADefaultPawn::StaticClass();
}

void AReplicPIENetworkGameMode::GetSeamlessTravelActorList(bool bToTransition, TArray<AActor*>& ActorList)
{
	Super::GetSeamlessTravelActorList(bToTransition, ActorList);
	for (TActorIterator<AReplicPIENetworkActor> It(GetWorld()); It; ++It)
	{
		ActorList.AddUnique(*It);
	}
}
