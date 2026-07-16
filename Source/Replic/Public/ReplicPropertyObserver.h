#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "ReplicPropertyObserver.generated.h"

class UReplicTransportComponent;
class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReplicObservedPropertyChangedSignature, UObject*, TargetObject, FName, PropertyName);

UCLASS(BlueprintType, meta = (ToolTip = "Represents an active Replic property change binding. Store this object if you want to keep listening and call Unbind when you no longer need it."))
class REPLIC_API UReplicPropertyObserver : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UReplicTransportComponent* InTransportComponent, UObject* InTargetObject, FName InPropertyName);

	UFUNCTION(BlueprintCallable, Category = "Replic|Observe", meta = (ToolTip = "Stops this Replic property observer and removes its internal binding from the transport component."))
	void Unbind();

	UFUNCTION(BlueprintPure, Category = "Replic|Observe", meta = (ToolTip = "Returns true while this observer is still bound to a valid Replic target and transport component."))
	bool IsBound() const;

	UPROPERTY(BlueprintAssignable, Category = "Replic|Observe", meta = (ToolTip = "Fires when Replic applies a different value to the observed target/property on this machine.\n\nIf PropertyName was left empty when binding, this fires for any marked property on the observed target object."))
	FReplicObservedPropertyChangedSignature OnChanged;

protected:
	virtual void BeginDestroy() override;

private:
	UFUNCTION()
	void HandleTransportPropertyChanged(UObject* ChangedTargetObject, FName ChangedPropertyName);

	UFUNCTION()
	void HandleHostActorDestroyed(AActor* DestroyedActor);

	UPROPERTY(Transient)
	TWeakObjectPtr<UReplicTransportComponent> TransportComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> TargetObject;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> HostActor;

	UPROPERTY(Transient)
	FName PropertyName = NAME_None;
};
