#include "ReplicPropertyObserver.h"

#include "Replic.h"
#include "ReplicSettings.h"
#include "ReplicTransportComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	bool ShouldLogObserverDebug()
	{
		const UReplicSettings* Settings = GetDefault<UReplicSettings>();
		return Settings
			&& Settings->bEnableRuntimeDebugLogs
			&& Settings->bEnableVerboseRuntimeLogs
			&& Settings->bEnableObserverDebugLogs;
	}

	void LogObserverDebug(const FString& Message)
	{
		if (ShouldLogObserverDebug())
		{
			UE_LOG(LogReplicObservers, Log, TEXT("%s"), *Message);
		}
	}
}

void UReplicPropertyObserver::Initialize(UReplicTransportComponent* InTransportComponent, UObject* InTargetObject, FName InPropertyName)
{
	TransportComponent = InTransportComponent;
	TargetObject = InTargetObject;
	PropertyName = InPropertyName;

	if (TransportComponent.IsValid())
	{
		TransportComponent->OnMarkedPropertyChanged.AddDynamic(this, &UReplicPropertyObserver::HandleTransportPropertyChanged);
		HostActor = TransportComponent->GetOwner();
		if (HostActor.IsValid())
		{
			HostActor->OnDestroyed.AddUniqueDynamic(this, &UReplicPropertyObserver::HandleHostActorDestroyed);
		}
	}

	LogObserverDebug(FString::Printf(TEXT("Replic observer bound to '%s' for property '%s'."), *GetPathNameSafe(TargetObject.Get()), *PropertyName.ToString()));
}

void UReplicPropertyObserver::Unbind()
{
	if (TransportComponent.IsValid())
	{
		TransportComponent->OnMarkedPropertyChanged.RemoveDynamic(this, &UReplicPropertyObserver::HandleTransportPropertyChanged);
	}

	if (HostActor.IsValid())
	{
		HostActor->OnDestroyed.RemoveDynamic(this, &UReplicPropertyObserver::HandleHostActorDestroyed);
	}

	TransportComponent.Reset();
	TargetObject.Reset();
	HostActor.Reset();
	PropertyName = NAME_None;

	LogObserverDebug(TEXT("Replic observer unbound."));
}

bool UReplicPropertyObserver::IsBound() const
{
	return TransportComponent.IsValid() && TargetObject.IsValid() && HostActor.IsValid();
}

void UReplicPropertyObserver::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

void UReplicPropertyObserver::HandleTransportPropertyChanged(UObject* ChangedTargetObject, FName ChangedPropertyName)
{
	// A binding is always scoped to one local target object instance. PropertyName == None means "listen to any marked
	// property on that object", which keeps the node useful for generic UI refresh logic.
	if (!TargetObject.IsValid() || ChangedTargetObject != TargetObject.Get())
	{
		return;
	}

	if (!PropertyName.IsNone() && ChangedPropertyName != PropertyName)
	{
		return;
	}

	LogObserverDebug(FString::Printf(TEXT("Replic observer received change '%s' on '%s'."), *ChangedPropertyName.ToString(), *GetPathNameSafe(ChangedTargetObject)));
	OnChanged.Broadcast(ChangedTargetObject, ChangedPropertyName);
}

void UReplicPropertyObserver::HandleHostActorDestroyed(AActor* DestroyedActor)
{
	Unbind();
}
