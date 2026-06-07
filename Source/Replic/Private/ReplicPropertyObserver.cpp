#include "ReplicPropertyObserver.h"

#include "Replic.h"
#include "ReplicSettings.h"
#include "ReplicTransportComponent.h"

namespace
{
	bool ShouldLogObserverDebug()
	{
		const UReplicSettings* Settings = GetDefault<UReplicSettings>();
		return Settings && Settings->bEnableRuntimeDebugLogs && Settings->bEnableObserverDebugLogs;
	}

	void LogObserverDebug(const FString& Message)
	{
		if (ShouldLogObserverDebug())
		{
			UE_LOG(LogReplic, Log, TEXT("%s"), *Message);
		}
	}
}

void UReplicPropertyObserver::Initialize(UReplicTransportComponent* InTransportComponent, UObject* InTargetObject, FName InPropertyName)
{
	TransportComponent = InTransportComponent;
	TargetObject = InTargetObject;
	PropertyName = InPropertyName;

	if (TransportComponent)
	{
		TransportComponent->OnMarkedPropertyChanged.AddDynamic(this, &UReplicPropertyObserver::HandleTransportPropertyChanged);
	}

	LogObserverDebug(FString::Printf(TEXT("Replic observer bound to '%s' for property '%s'."), *GetPathNameSafe(TargetObject), *PropertyName.ToString()));
}

void UReplicPropertyObserver::Unbind()
{
	if (TransportComponent)
	{
		TransportComponent->OnMarkedPropertyChanged.RemoveDynamic(this, &UReplicPropertyObserver::HandleTransportPropertyChanged);
	}

	TransportComponent = nullptr;
	TargetObject = nullptr;
	PropertyName = NAME_None;

	LogObserverDebug(TEXT("Replic observer unbound."));
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
	if (!TargetObject || ChangedTargetObject != TargetObject)
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
