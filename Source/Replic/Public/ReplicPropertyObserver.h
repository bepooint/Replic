#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "ReplicPropertyObserver.generated.h"

class UReplicTransportComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReplicObservedPropertyChangedSignature, UObject*, TargetObject, FName, PropertyName);

UCLASS(BlueprintType, meta = (ToolTip = "Represents an active Replic property change binding. Store this object if you want to keep listening and call Unbind when you no longer need it."))
class REPLIC_API UReplicPropertyObserver : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UReplicTransportComponent* InTransportComponent, UObject* InTargetObject, FName InPropertyName);

	UFUNCTION(BlueprintCallable, Category = "Replic|Observe", meta = (ToolTip = "Stops this Replic property observer and removes its internal binding from the transport component."))
	void Unbind();

	UPROPERTY(BlueprintAssignable, Category = "Replic|Observe", meta = (ToolTip = "Fires when Replic applies a different value to the observed target/property on this machine.\n\nIf PropertyName was left empty when binding, this fires for any marked property on the observed target object."))
	FReplicObservedPropertyChangedSignature OnChanged;

protected:
	virtual void BeginDestroy() override;

private:
	UFUNCTION()
	void HandleTransportPropertyChanged(UObject* ChangedTargetObject, FName ChangedPropertyName);

	UPROPERTY(Transient)
	TObjectPtr<UReplicTransportComponent> TransportComponent;

	UPROPERTY(Transient)
	TObjectPtr<UObject> TargetObject;

	UPROPERTY(Transient)
	FName PropertyName = NAME_None;
};
