#include "Tests/ReplicPIENetworkTestActors.h"

#include "ReplicTransportComponent.h"
#include "Components/SceneComponent.h"

void UReplicPIEObserverSink::HandleObservedChange(UObject* TargetObject, FName PropertyName)
{
	++ChangeCount;
	LastTargetObject = TargetObject;
	LastPropertyName = PropertyName;
}

AReplicPIENetworkPlayerController::AReplicPIENetworkPlayerController()
{
	ReplicTransportComponent = CreateDefaultSubobject<UReplicTransportComponent>(TEXT("ReplicTransportComponent"));
}

AReplicPIENetworkActor::AReplicPIENetworkActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;
	ReplicTransportComponent = CreateDefaultSubobject<UReplicTransportComponent>(TEXT("ReplicTransportComponent"));
}

void AReplicPIENetworkActor::MarkedPulse(int32 Delta)
{
	EventValue += Delta;
}

void AReplicPIENetworkActor::MarkedVectorPulse(FVector Delta)
{
	EventValue += FMath::RoundToInt(Delta.Size());
}

bool AReplicPIENetworkActor::CanReplicWrite_SharedValue(AActor* RequestingActor)
{
	return bAllowCustomWrite;
}

bool AReplicPIENetworkActor::CanReplicCall_MarkedPulse(AActor* RequestingActor)
{
	return bAllowCustomEvent;
}

AReplicPIENetworkGameMode::AReplicPIENetworkGameMode()
{
	PlayerControllerClass = AReplicPIENetworkPlayerController::StaticClass();
	DefaultPawnClass = ADefaultPawn::StaticClass();
}
