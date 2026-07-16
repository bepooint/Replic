#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReplicTypes.h"

#include "ReplicTransportComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReplicMarkedPropertyChangedSignature, UObject*, TargetObject, FName, PropertyName);

class USceneComponent;

UCLASS(ClassGroup = ("Networking"), BlueprintType, meta = (BlueprintSpawnableComponent, ToolTip = "Runtime Replic transport component. Add this to replicated actors that should send or own Replic state and events."))
class REPLIC_API UReplicTransportComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UReplicTransportComponent();

	static UReplicTransportComponent* FindOrCreate(AActor* HostActor);
	static UReplicTransportComponent* FindOnActor(const AActor* HostActor);

	bool RequestMarkedPropertyWrite(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const FString& SerializedValue);
	bool RequestMarkedContainerDelta(
		UObject* ContextObject,
		UObject* TargetObject,
		FName PropertyName,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		const FString& SerializedSecondaryValue);
	bool RequestMarkedEvent(UObject* ContextObject, UObject* TargetObject, FName EventName, const TArray<FReplicNamedValue>& Arguments);
	void ApplyStoredStateToObservedObject(UObject* ObservedObject);
	void ApplyComponentTransformState(const FReplicComponentTransformState& TransformState);
	void RefreshComponentTransformTracking(bool bCommitPersistentInitialState = false, bool bForceSend = false);
	bool FindVariableDefinition(const FReplicTargetDescriptor& TargetDescriptor, FName PropertyName, FReplicVariableSettings& OutSettings) const;
	bool FindEventDefinition(const FReplicTargetDescriptor& TargetDescriptor, FName EventName, FReplicEventSettings& OutSettings) const;
	bool TryGetPersistentStateDebugValue(const FReplicTargetDescriptor& TargetDescriptor, FName PropertyName, FString& OutSerializedValue) const;

	void HandleReplicatedStateEntryChanged(const FReplicStateEntry& Entry);
	void HandleReplicatedStateEntryRemoved(int32 RemovedIndex);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic|Transforms", meta = (ToolTip = "Scene components whose transform should be replicated by Replic. These entries are usually edited from each component's Details panel Replic category."))
	TArray<FReplicComponentTransformSettings> ComponentTransformSettings;

	UPROPERTY(VisibleAnywhere, Category = "Replic|Runtime", AdvancedDisplay, meta = (ToolTip = "Cooked runtime data for Replic-marked variables. Generated from Blueprint Replic metadata by the editor compiler extension."))
	TArray<FReplicMarkedVariableDefinition> MarkedVariableDefinitions;

	UPROPERTY(VisibleAnywhere, Category = "Replic|Runtime", AdvancedDisplay, meta = (ToolTip = "Cooked runtime data for Replic-marked events. Generated from Blueprint Replic metadata by the editor compiler extension."))
	TArray<FReplicMarkedEventDefinition> MarkedEventDefinitions;

	UPROPERTY(BlueprintAssignable, Category = "Replic|Observe", meta = (ToolTip = "Fires on this machine after Replic applies a different value to a marked property owned by this transport's target objects."))
	FReplicMarkedPropertyChangedSignature OnMarkedPropertyChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Server, Reliable)
	void ServerRequestPropertyWrite(
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName PropertyName,
		const FString& SerializedValue);

	UFUNCTION(Server, Reliable)
	void ServerRequestContainerDelta(
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName PropertyName,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		const FString& SerializedSecondaryValue);

	UFUNCTION(Server, Reliable)
	void ServerRequestEvent(
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName EventName,
		const TArray<FReplicNamedValue>& Arguments);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDispatchTransientProperty(
		const FReplicTargetDescriptor& TargetDescriptor,
		FName PropertyName,
		const FString& SerializedValue);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDispatchEvent(const FReplicEventMessage& EventMessage);

	UFUNCTION(Client, Reliable)
	void ClientDispatchEvent(const FReplicEventMessage& EventMessage);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDispatchComponentTransformState(const FReplicComponentTransformState& TransformState);

	UFUNCTION()
	void OnRep_ReplicatedComponentTransformStates();

private:
	void ReapplyAllPersistentState();
	USceneComponent* FindSceneComponentByName(FName ComponentName) const;
	FTransform ReadComponentTransform(const USceneComponent* SceneComponent, EReplicTransformSpace TransformSpace) const;
	FReplicComponentTransformState MakeComponentTransformState(const FReplicComponentTransformSettings& Settings, const FTransform& Transform) const;
	bool HasTrackedTransformChanged(const FReplicComponentTransformSettings& Settings, const FTransform& PreviousTransform, const FTransform& CurrentTransform) const;
	void StorePersistentComponentTransformState(const FReplicComponentTransformState& TransformState);
	int32 FindPersistentComponentTransformStateIndex(FName ComponentName) const;

	bool ApplyAuthoritativePropertyWrite(
		AActor* RequestHostActor,
		bool bRequestOriginatedOnServer,
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName PropertyName,
		const FString& SerializedValue);

	bool ApplyAuthoritativeContainerDelta(
		AActor* RequestHostActor,
		bool bRequestOriginatedOnServer,
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName PropertyName,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		const FString& SerializedSecondaryValue);

	bool ApplyAuthoritativeEvent(
		AActor* RequestHostActor,
		bool bRequestOriginatedOnServer,
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName EventName,
		const TArray<FReplicNamedValue>& Arguments);

	bool ApplySerializedPropertyToTargets(
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName PropertyName,
		const FString& SerializedValue);

	bool ExecuteSerializedEventOnTargets(
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName EventName,
		const TArray<FReplicNamedValue>& Arguments) const;

	void BroadcastMarkedPropertyChanged(UObject* TargetObject, FName PropertyName);

	bool FinalizeAuthoritativePropertyWrite(
		AActor* RequestHostActor,
		const UObject* MetadataSourceObject,
		AActor* TargetHostActor,
		const FReplicTargetDescriptor& TargetDescriptor,
		FName PropertyName,
		const FString& SerializedValue,
		const FReplicVariableSettings& Settings);

	void QueueStateWrite(const FReplicTargetDescriptor& TargetDescriptor, FName PropertyName, const FString& SerializedValue, float BatchIntervalSeconds);
	void FlushPendingStateWrites();
	void CommitPersistentState(const FReplicTargetDescriptor& TargetDescriptor, FName PropertyName, const FString& SerializedValue);
	int32 FindStateEntryIndex(const FReplicTargetDescriptor& TargetDescriptor, FName PropertyName) const;

	UPROPERTY(Replicated)
	FReplicStateArray ReplicatedStates;

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedComponentTransformStates)
	TArray<FReplicComponentTransformState> ReplicatedComponentTransformStates;

	UPROPERTY(Transient)
	TArray<FReplicPendingStateWrite> PendingStateWrites;

	UPROPERTY(Transient)
	TMap<FName, FTransform> LastObservedComponentTransforms;

	UPROPERTY(Transient)
	TMap<FName, double> LastComponentTransformSendTimes;

	FTimerHandle BatchFlushTimerHandle;
};
