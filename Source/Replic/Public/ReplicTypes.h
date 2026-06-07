#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UObject/SoftObjectPath.h"

#include "ReplicTypes.generated.h"

class UReplicTransportComponent;

UENUM(BlueprintType)
enum class EReplicTargetKind : uint8
{
	Actor = 0,
	ActorComponent,
	Widget,
	AnimInstance
};

UENUM(BlueprintType)
enum class EReplicEventMode : uint8
{
	LocalOnly = 0,
	ServerOnly,
	OwnerOnly,
	ReplicateAll
};

UENUM(BlueprintType)
enum class EReplicPermissionMode : uint8
{
	None = 0,
	OwnerOnly,
	ServerOnly,
	Custom
};

UENUM(BlueprintType)
enum class EReplicContainerDeltaOperation : uint8
{
	AddArrayItem = 0,
	RemoveArrayItem,
	AddSetItem,
	RemoveSetItem,
	SetMapEntry,
	RemoveMapEntry
};

UENUM(BlueprintType)
enum class EReplicTransformSpace : uint8
{
	Relative = 0 UMETA(DisplayName = "Relative Transform"),
	World UMETA(DisplayName = "World Transform")
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicVariableSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "If enabled, this variable is controlled by Replic and can be written through Replic setter nodes."))
	bool bReplicateAll = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "If enabled, Replic stores the last value and re-applies it for late joiners."))
	bool bPersistentState = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "If enabled, Replic batches rapid writes instead of sending every change immediately."))
	bool bUseBatching = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ClampMin = "0.0", ToolTip = "Delay in seconds before Replic flushes a batched write for this variable."))
	float BatchIntervalSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Controls who is allowed to request writes for this variable.\n\nNone: Accept requests without an extra permission check.\nOwnerOnly: Only the owning client may request writes.\nServerOnly: Only server-initiated writes are accepted.\nCustom: A validation function on the target object must approve the request."))
	EReplicPermissionMode PermissionMode = EReplicPermissionMode::None;
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicComponentTransformSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Component name whose transform should be tracked by Replic."))
	FName ComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Controls whether Replic reads and applies this component in relative or world space."))
	EReplicTransformSpace TransformSpace = EReplicTransformSpace::Relative;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Replicate the component location in the selected transform space."))
	bool bReplicateLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Replicate the component rotation in the selected transform space."))
	bool bReplicateRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Replicate the component scale in the selected transform space."))
	bool bReplicateScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Store the latest replicated transform so late joiners receive the current component state."))
	bool bPersistentState = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ClampMin = "0.0", ToolTip = "Minimum seconds between automatic transform updates for this component. Higher values reduce bandwidth for fast-moving physics objects."))
	float MinUpdateIntervalSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ClampMin = "0.0", ToolTip = "Minimum location delta before Replic sends another location update."))
	float LocationThreshold = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ClampMin = "0.0", ToolTip = "Minimum rotation delta in degrees before Replic sends another rotation update."))
	float RotationThresholdDegrees = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ClampMin = "0.0", ToolTip = "Minimum scale delta before Replic sends another scale update."))
	float ScaleThreshold = 0.01f;

	bool HasAnyReplicatedChannel() const
	{
		return bReplicateLocation || bReplicateRotation || bReplicateScale;
	}
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicEventSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "If enabled, this custom event can be called through Replic."))
	bool bReplicateAll = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Controls who is allowed to request this custom event.\n\nNone: Accept requests without an extra permission check.\nOwnerOnly: Only the owning client may request the event.\nServerOnly: Only server-initiated calls are accepted.\nCustom: A validation function on the target object must approve the request."))
	EReplicPermissionMode PermissionMode = EReplicPermissionMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Controls how Replic dispatches the event after the server accepts it."))
	EReplicEventMode Mode = EReplicEventMode::ReplicateAll;
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicTargetDescriptor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "The kind of object Replic should resolve at runtime."))
	EReplicTargetKind Kind = EReplicTargetKind::Actor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "The object name used to resolve a component or other sub-object target on the host actor."))
	FName ObjectName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "Optional class path used for widget or animation instance target resolution."))
	FSoftClassPath ClassPath;

	bool IsValid() const
	{
		return Kind == EReplicTargetKind::Actor || ObjectName != NAME_None || ClassPath.IsValid();
	}

	FString ToKey() const;
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicMarkedVariableDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FReplicTargetDescriptor Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FName PropertyName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FReplicVariableSettings Settings;
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicMarkedEventDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FReplicTargetDescriptor Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FName EventName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FReplicEventSettings Settings;
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicNamedValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "The event parameter name that this value should be applied to."))
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic", meta = (ToolTip = "The serialized text value used by Replic to send the parameter through the network."))
	FString SerializedValue;
};

USTRUCT(BlueprintType)
struct REPLIC_API FReplicComponentTransformState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FName ComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	EReplicTransformSpace TransformSpace = EReplicTransformSpace::Relative;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	bool bReplicateLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	bool bReplicateRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	bool bReplicateScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replic")
	FVector Scale = FVector::OneVector;
};

USTRUCT()
struct REPLIC_API FReplicStateEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FReplicTargetDescriptor Target;

	UPROPERTY()
	FName PropertyName = NAME_None;

	UPROPERTY()
	FString SerializedValue;

	UPROPERTY()
	int32 Revision = 0;
};

USTRUCT()
struct REPLIC_API FReplicStateArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FReplicStateEntry> Items;

	UPROPERTY(Transient)
	TObjectPtr<UReplicTransportComponent> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<FReplicStateEntry, FReplicStateArray>(Items, DeltaParams, *this);
	}

	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
	void PostReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
};

template<>
struct TStructOpsTypeTraits<FReplicStateArray> : public TStructOpsTypeTraitsBase2<FReplicStateArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

USTRUCT()
struct REPLIC_API FReplicEventMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FReplicTargetDescriptor Target;

	UPROPERTY()
	FName EventName = NAME_None;

	UPROPERTY()
	EReplicEventMode Mode = EReplicEventMode::ReplicateAll;

	UPROPERTY()
	TArray<FReplicNamedValue> Arguments;
};

USTRUCT()
struct REPLIC_API FReplicPendingStateWrite
{
	GENERATED_BODY()

	UPROPERTY()
	FReplicTargetDescriptor Target;

	UPROPERTY()
	FName PropertyName = NAME_None;

	UPROPERTY()
	FString SerializedValue;

	UPROPERTY()
	double FlushAtTimeSeconds = 0.0;
};
