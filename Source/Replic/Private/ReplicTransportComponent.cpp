#include "ReplicTransportComponent.h"

#include "Net/UnrealNetwork.h"
#include "Replic.h"
#include "ReplicRuntimeUtils.h"
#include "ReplicSettings.h"
#include "TimerManager.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

namespace
{
	enum class EReplicDebugChannel : uint8
	{
		Writes,
		Events,
		State,
		Observers,
		Permissions
	};

	const UReplicSettings* GetReplicSettings()
	{
		return GetDefault<UReplicSettings>();
	}

	FName NormalizeComponentName(FName ComponentName)
	{
		if (ComponentName.IsNone())
		{
			return NAME_None;
		}

		FString NameString = ComponentName.ToString();
		NameString.RemoveFromEnd(UActorComponent::ComponentTemplateNameSuffix);
		return FName(*NameString);
	}

	bool IsDebugChannelEnabled(const UReplicSettings* Settings, EReplicDebugChannel Channel)
	{
		if (!Settings)
		{
			return false;
		}

		switch (Channel)
		{
		case EReplicDebugChannel::Writes:
			return Settings->bEnableWriteDebugLogs;
		case EReplicDebugChannel::Events:
			return Settings->bEnableEventDebugLogs;
		case EReplicDebugChannel::State:
			return Settings->bEnableStateDebugLogs;
		case EReplicDebugChannel::Observers:
			return Settings->bEnableObserverDebugLogs;
		case EReplicDebugChannel::Permissions:
			return Settings->bEnableDetailedPermissionLogs;
		default:
			return false;
		}
	}

	bool ShouldLogReplicRuntime(EReplicDebugChannel Channel)
	{
		if (const UReplicSettings* Settings = GetReplicSettings())
		{
			return Settings->bEnableRuntimeDebugLogs && IsDebugChannelEnabled(Settings, Channel);
		}

		return false;
	}

	bool ShouldShowReplicScreenMessages(EReplicDebugChannel Channel)
	{
		if (const UReplicSettings* Settings = GetReplicSettings())
		{
			return Settings->bEnableScreenDebugMessages && IsDebugChannelEnabled(Settings, Channel);
		}

		return false;
	}

	void EmitReplicDebugMessage(EReplicDebugChannel Channel, ELogVerbosity::Type Verbosity, const FString& Message, const FColor& ScreenColor = FColor::White, const FString& ScreenMessage = FString())
	{
		const UReplicSettings* Settings = GetReplicSettings();
		const bool bLogToOutput = ShouldLogReplicRuntime(Channel);
		const bool bLogToScreen = ShouldShowReplicScreenMessages(Channel);
		if (!bLogToOutput && !bLogToScreen)
		{
			return;
		}

		if (bLogToOutput)
		{
			switch (Verbosity)
			{
			case ELogVerbosity::Warning:
				UE_LOG(LogReplic, Warning, TEXT("%s"), *Message);
				break;
			case ELogVerbosity::Error:
				UE_LOG(LogReplic, Error, TEXT("%s"), *Message);
				break;
			default:
				UE_LOG(LogReplic, Log, TEXT("%s"), *Message);
				break;
			}
		}

		if (bLogToScreen && GEngine && Settings)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, Settings->ScreenDebugMessageDuration, ScreenColor, ScreenMessage.IsEmpty() ? Message : ScreenMessage);
		}
	}

	void LogPermissionDecision(const TCHAR* SubjectKind, FName SubjectName, const UObject* TargetObject, const AActor* RequestHostActor, const TCHAR* Reason)
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Permissions,
			ELogVerbosity::Warning,
			FString::Printf(
				TEXT("Replic denied %s '%s' on '%s' from requester '%s': %s"),
				SubjectKind,
				*SubjectName.ToString(),
				*GetPathNameSafe(TargetObject),
				*GetPathNameSafe(RequestHostActor),
				Reason),
			FColor::Red,
			FString::Printf(TEXT("Replic Denied: %s"), *SubjectName.ToString()));
	}

	void AddPermissionIdentityActor(const AActor* CandidateActor, TArray<const AActor*>& OutActors)
	{
		if (CandidateActor)
		{
			OutActors.AddUnique(CandidateActor);
		}
	}

	void GatherPermissionIdentityActors(const AActor* SourceActor, TArray<const AActor*>& OutActors)
	{
		if (!SourceActor)
		{
			return;
		}

		AddPermissionIdentityActor(SourceActor, OutActors);

		for (const AActor* OwnerActor = SourceActor->GetOwner(); OwnerActor; OwnerActor = OwnerActor->GetOwner())
		{
			AddPermissionIdentityActor(OwnerActor, OutActors);
		}

		if (const APawn* Pawn = Cast<APawn>(SourceActor))
		{
			AddPermissionIdentityActor(Pawn->GetController(), OutActors);
			AddPermissionIdentityActor(Pawn->GetPlayerState(), OutActors);
		}

		if (const AController* Controller = Cast<AController>(SourceActor))
		{
			AddPermissionIdentityActor(Controller->GetPawn(), OutActors);
			AddPermissionIdentityActor(Controller->PlayerState, OutActors);
		}

		if (const APlayerState* PlayerState = Cast<APlayerState>(SourceActor))
		{
			AddPermissionIdentityActor(Cast<AActor>(PlayerState->GetOwner()), OutActors);
		}
	}

	bool IsOwnedByRequester(const AActor* RequestHostActor, const AActor* TargetHostActor)
	{
		if (!RequestHostActor || !TargetHostActor)
		{
			return false;
		}

		if (RequestHostActor == TargetHostActor || TargetHostActor->IsOwnedBy(RequestHostActor) || RequestHostActor->IsOwnedBy(TargetHostActor))
		{
			return true;
		}

		TArray<const AActor*> RequestIdentityActors;
		TArray<const AActor*> TargetIdentityActors;
		GatherPermissionIdentityActors(RequestHostActor, RequestIdentityActors);
		GatherPermissionIdentityActors(TargetHostActor, TargetIdentityActors);

		for (const AActor* RequestIdentityActor : RequestIdentityActors)
		{
			if (TargetIdentityActors.Contains(RequestIdentityActor))
			{
				return true;
			}
		}

		return false;
	}

	bool TryInvokeCustomValidation(UObject* TargetObject, FName ValidationFunctionName, AActor* RequestHostActor, bool& bOutAllowed)
	{
		bOutAllowed = false;
		if (!TargetObject || ValidationFunctionName.IsNone())
		{
			return false;
		}

		UFunction* ValidationFunction = TargetObject->FindFunction(ValidationFunctionName);
		if (!ValidationFunction)
		{
			return false;
		}

		const FBoolProperty* ReturnProperty = nullptr;
		const FObjectPropertyBase* InputObjectProperty = nullptr;
		int32 InputParamCount = 0;

		for (TFieldIterator<FProperty> PropertyIt(ValidationFunction); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
		{
			const FProperty* FunctionProperty = *PropertyIt;
			if (FunctionProperty->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnProperty = CastField<FBoolProperty>(FunctionProperty);
				continue;
			}

			if (FunctionProperty->HasAnyPropertyFlags(CPF_OutParm))
			{
				return false;
			}

			++InputParamCount;
			InputObjectProperty = CastField<FObjectPropertyBase>(FunctionProperty);
			if (!InputObjectProperty)
			{
				return false;
			}
		}

		if (!ReturnProperty || InputParamCount > 1)
		{
			return false;
		}

		TArray<uint8> ParameterBuffer;
		ParameterBuffer.SetNumZeroed(ValidationFunction->ParmsSize);

		for (TFieldIterator<FProperty> PropertyIt(ValidationFunction); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
		{
			FProperty* FunctionProperty = *PropertyIt;
			FunctionProperty->InitializeValue_InContainer(ParameterBuffer.GetData());
		}

		bool bCanInvoke = true;
		if (InputObjectProperty)
		{
			if (!RequestHostActor || !RequestHostActor->IsA(InputObjectProperty->PropertyClass))
			{
				bCanInvoke = false;
			}
			else
			{
				InputObjectProperty->SetObjectPropertyValue_InContainer(ParameterBuffer.GetData(), RequestHostActor);
			}
		}

		if (bCanInvoke)
		{
			TargetObject->ProcessEvent(ValidationFunction, ParameterBuffer.GetData());
			bOutAllowed = ReturnProperty->GetPropertyValue_InContainer(ParameterBuffer.GetData());
		}

		for (TFieldIterator<FProperty> PropertyIt(ValidationFunction); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
		{
			FProperty* FunctionProperty = *PropertyIt;
			FunctionProperty->DestroyValue_InContainer(ParameterBuffer.GetData());
		}

		return bCanInvoke;
	}

	bool EvaluateCustomPropertyPermission(UObject* TargetObject, FName PropertyName, AActor* RequestHostActor, bool& bOutAllowed)
	{
		return TryInvokeCustomValidation(
			TargetObject,
			FName(*FString::Printf(TEXT("CanReplicWrite_%s"), *PropertyName.ToString())),
			RequestHostActor,
			bOutAllowed);
	}

	bool EvaluateCustomEventPermission(UObject* TargetObject, FName EventName, AActor* RequestHostActor, bool& bOutAllowed)
	{
		return TryInvokeCustomValidation(
			TargetObject,
			FName(*FString::Printf(TEXT("CanReplicCall_%s"), *EventName.ToString())),
			RequestHostActor,
			bOutAllowed);
	}

	const TCHAR* GetContainerDeltaOperationLabel(EReplicContainerDeltaOperation Operation)
	{
		switch (Operation)
		{
		case EReplicContainerDeltaOperation::AddArrayItem:
			return TEXT("AddArrayItem");
		case EReplicContainerDeltaOperation::RemoveArrayItem:
			return TEXT("RemoveArrayItem");
		case EReplicContainerDeltaOperation::AddSetItem:
			return TEXT("AddSetItem");
		case EReplicContainerDeltaOperation::RemoveSetItem:
			return TEXT("RemoveSetItem");
		case EReplicContainerDeltaOperation::SetMapEntry:
			return TEXT("SetMapEntry");
		case EReplicContainerDeltaOperation::RemoveMapEntry:
			return TEXT("RemoveMapEntry");
		default:
			return TEXT("Unknown");
		}
	}

	struct FScopedReplicPropertyValue
	{
		explicit FScopedReplicPropertyValue(const FProperty* InProperty)
			: Property(InProperty)
		{
			if (Property)
			{
				Storage = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
				Property->InitializeValue(Storage);
			}
		}

		~FScopedReplicPropertyValue()
		{
			if (Property && Storage)
			{
				Property->DestroyValue(Storage);
				FMemory::Free(Storage);
			}
		}

		void* Get() const
		{
			return Storage;
		}

		const FProperty* Property = nullptr;
		void* Storage = nullptr;
	};

	bool ValidateAuthoritativePropertyPermission(
		const FReplicVariableSettings& Settings,
		const UObject* MetadataSourceObject,
		FName PropertyName,
		AActor* RequestHostActor,
		AActor* TargetHostActor,
		bool bRequestOriginatedOnServer)
	{
		if (!bRequestOriginatedOnServer)
		{
			switch (Settings.PermissionMode)
			{
			case EReplicPermissionMode::None:
				break;
			case EReplicPermissionMode::OwnerOnly:
				if (!IsOwnedByRequester(RequestHostActor, TargetHostActor))
				{
					LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("OwnerOnly check failed"));
					return false;
				}
				break;
			case EReplicPermissionMode::ServerOnly:
				LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("ServerOnly check failed"));
				return false;
			case EReplicPermissionMode::Custom:
				break;
			default:
				break;
			}
		}

		if (Settings.PermissionMode == EReplicPermissionMode::Custom)
		{
			bool bAllowed = false;
			if (!EvaluateCustomPropertyPermission(const_cast<UObject*>(MetadataSourceObject), PropertyName, RequestHostActor, bAllowed))
			{
				LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("custom validation function missing or unsupported"));
				return false;
			}

			if (!bAllowed)
			{
				LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("custom validation rejected the request"));
				return false;
			}
		}

		return true;
	}

	bool ApplyArrayDelta(
		const FArrayProperty* ArrayProperty,
		void* ArrayValuePtr,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		bool& bOutChanged)
	{
		bOutChanged = false;
		if (!ArrayProperty || !ArrayValuePtr)
		{
			return false;
		}

		FScopedReplicPropertyValue ItemValue(ArrayProperty->Inner);
		if (!ItemValue.Get() || !ReplicRuntimeUtils::ImportPropertyValueFromText(ArrayProperty->Inner, ItemValue.Get(), SerializedPrimaryValue))
		{
			return false;
		}

		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayValuePtr);
		switch (Operation)
		{
		case EReplicContainerDeltaOperation::AddArrayItem:
		{
			const int32 AddedIndex = ArrayHelper.AddValue();
			ArrayProperty->Inner->CopySingleValue(ArrayHelper.GetElementPtr(AddedIndex), ItemValue.Get());
			bOutChanged = true;
			return true;
		}
		case EReplicContainerDeltaOperation::RemoveArrayItem:
		{
			for (int32 Index = ArrayHelper.Num() - 1; Index >= 0; --Index)
			{
				if (ArrayProperty->Inner->Identical(ItemValue.Get(), ArrayHelper.GetElementPtr(Index), PPF_None))
				{
					ArrayHelper.RemoveValues(Index);
					bOutChanged = true;
				}
			}
			return true;
		}
		default:
			return false;
		}
	}

	bool ApplySetDelta(
		const FSetProperty* SetProperty,
		void* SetValuePtr,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		bool& bOutChanged)
	{
		bOutChanged = false;
		if (!SetProperty || !SetValuePtr)
		{
			return false;
		}

		const FProperty* ElementProperty = SetProperty->GetElementProperty();
		FScopedReplicPropertyValue ItemValue(ElementProperty);
		if (!ItemValue.Get() || !ReplicRuntimeUtils::ImportPropertyValueFromText(ElementProperty, ItemValue.Get(), SerializedPrimaryValue))
		{
			return false;
		}

		FScriptSetHelper SetHelper(SetProperty, SetValuePtr);
		switch (Operation)
		{
		case EReplicContainerDeltaOperation::AddSetItem:
		{
			const bool bAlreadyPresent = SetHelper.FindElementIndexFromHash(ItemValue.Get()) != INDEX_NONE;
			SetHelper.AddElement(ItemValue.Get());
			bOutChanged = !bAlreadyPresent;
			return true;
		}
		case EReplicContainerDeltaOperation::RemoveSetItem:
			bOutChanged = SetHelper.RemoveElement(ItemValue.Get());
			return true;
		default:
			return false;
		}
	}

	bool ApplyMapDelta(
		const FMapProperty* MapProperty,
		void* MapValuePtr,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		const FString& SerializedSecondaryValue,
		bool& bOutChanged)
	{
		bOutChanged = false;
		if (!MapProperty || !MapValuePtr)
		{
			return false;
		}

		const FProperty* KeyProperty = MapProperty->GetKeyProperty();
		const FProperty* ValueProperty = MapProperty->GetValueProperty();
		FScopedReplicPropertyValue KeyValue(KeyProperty);
		if (!KeyValue.Get() || !ReplicRuntimeUtils::ImportPropertyValueFromText(KeyProperty, KeyValue.Get(), SerializedPrimaryValue))
		{
			return false;
		}

		FScriptMapHelper MapHelper(MapProperty, MapValuePtr);
		switch (Operation)
		{
		case EReplicContainerDeltaOperation::SetMapEntry:
		{
			FScopedReplicPropertyValue EntryValue(ValueProperty);
			if (!EntryValue.Get() || !ReplicRuntimeUtils::ImportPropertyValueFromText(ValueProperty, EntryValue.Get(), SerializedSecondaryValue))
			{
				return false;
			}

			void* ExistingValuePtr = MapHelper.FindValueFromHash(KeyValue.Get());
			if (ExistingValuePtr)
			{
				bOutChanged = !ValueProperty->Identical(ExistingValuePtr, EntryValue.Get(), PPF_None);
				ValueProperty->CopySingleValue(ExistingValuePtr, EntryValue.Get());
			}
			else
			{
				void* NewValuePtr = MapHelper.FindOrAdd(KeyValue.Get());
				ValueProperty->CopySingleValue(NewValuePtr, EntryValue.Get());
				bOutChanged = true;
			}

			return true;
		}
		case EReplicContainerDeltaOperation::RemoveMapEntry:
			bOutChanged = MapHelper.RemovePair(KeyValue.Get());
			return true;
		default:
			return false;
		}
	}

	bool BuildSerializedContainerDeltaResult(
		const FProperty* TargetProperty,
		const void* CurrentValuePtr,
		EReplicContainerDeltaOperation Operation,
		const FString& SerializedPrimaryValue,
		const FString& SerializedSecondaryValue,
		FString& OutSerializedValue,
		bool& bOutChanged)
	{
		bOutChanged = false;
		if (!TargetProperty || !CurrentValuePtr)
		{
			return false;
		}

		FScopedReplicPropertyValue WorkingValue(TargetProperty);
		if (!WorkingValue.Get())
		{
			return false;
		}

		TargetProperty->CopySingleValue(WorkingValue.Get(), CurrentValuePtr);

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(TargetProperty))
		{
			if (!ApplyArrayDelta(ArrayProperty, WorkingValue.Get(), Operation, SerializedPrimaryValue, bOutChanged))
			{
				return false;
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(TargetProperty))
		{
			if (!ApplySetDelta(SetProperty, WorkingValue.Get(), Operation, SerializedPrimaryValue, bOutChanged))
			{
				return false;
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(TargetProperty))
		{
			if (!ApplyMapDelta(MapProperty, WorkingValue.Get(), Operation, SerializedPrimaryValue, SerializedSecondaryValue, bOutChanged))
			{
				return false;
			}
		}
		else
		{
			return false;
		}

		return ReplicRuntimeUtils::ExportPropertyValueToText(TargetProperty, WorkingValue.Get(), OutSerializedValue);
	}
}

UReplicTransportComponent::UReplicTransportComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
	ReplicatedStates.Owner = this;
}

UReplicTransportComponent* UReplicTransportComponent::FindOrCreate(AActor* HostActor)
{
	if (!HostActor)
	{
		return nullptr;
	}

	if (UReplicTransportComponent* ExistingComponent = HostActor->FindComponentByClass<UReplicTransportComponent>())
	{
		return ExistingComponent;
	}

	if (!HostActor->HasAuthority())
	{
		return nullptr;
	}

	UReplicTransportComponent* NewComponent = NewObject<UReplicTransportComponent>(HostActor, TEXT("ReplicTransportComponent"));
	HostActor->AddOwnedComponent(NewComponent);
	NewComponent->RegisterComponent();
	return NewComponent;
}

UReplicTransportComponent* UReplicTransportComponent::FindOnActor(const AActor* HostActor)
{
	return HostActor ? HostActor->FindComponentByClass<UReplicTransportComponent>() : nullptr;
}

bool UReplicTransportComponent::FindVariableDefinition(const FReplicTargetDescriptor& TargetDescriptor, FName PropertyName, FReplicVariableSettings& OutSettings) const
{
	const FString TargetKey = TargetDescriptor.ToKey();
	for (const FReplicMarkedVariableDefinition& Definition : MarkedVariableDefinitions)
	{
		if (Definition.PropertyName == PropertyName && Definition.Target.ToKey() == TargetKey && Definition.Settings.bReplicateAll)
		{
			OutSettings = Definition.Settings;
			return true;
		}
	}

	return false;
}

bool UReplicTransportComponent::FindEventDefinition(const FReplicTargetDescriptor& TargetDescriptor, FName EventName, FReplicEventSettings& OutSettings) const
{
	const FString TargetKey = TargetDescriptor.ToKey();
	for (const FReplicMarkedEventDefinition& Definition : MarkedEventDefinitions)
	{
		if (Definition.EventName == EventName && Definition.Target.ToKey() == TargetKey && Definition.Settings.bReplicateAll)
		{
			OutSettings = Definition.Settings;
			return true;
		}
	}

	return false;
}

USceneComponent* UReplicTransportComponent::FindSceneComponentByName(FName ComponentName) const
{
	const AActor* HostActor = GetOwner();
	if (!HostActor || ComponentName.IsNone())
	{
		return nullptr;
	}

	const FName NormalizedTargetName = NormalizeComponentName(ComponentName);
	TInlineComponentArray<USceneComponent*> SceneComponents(HostActor);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (SceneComponent && NormalizeComponentName(SceneComponent->GetFName()) == NormalizedTargetName)
		{
			return SceneComponent;
		}
	}

	return nullptr;
}

FTransform UReplicTransportComponent::ReadComponentTransform(const USceneComponent* SceneComponent, EReplicTransformSpace TransformSpace) const
{
	if (!SceneComponent)
	{
		return FTransform::Identity;
	}

	return TransformSpace == EReplicTransformSpace::World
		? SceneComponent->GetComponentTransform()
		: SceneComponent->GetRelativeTransform();
}

FReplicComponentTransformState UReplicTransportComponent::MakeComponentTransformState(const FReplicComponentTransformSettings& Settings, const FTransform& Transform) const
{
	FReplicComponentTransformState TransformState;
	TransformState.ComponentName = NormalizeComponentName(Settings.ComponentName);
	TransformState.TransformSpace = Settings.TransformSpace;
	TransformState.bReplicateLocation = Settings.bReplicateLocation;
	TransformState.bReplicateRotation = Settings.bReplicateRotation;
	TransformState.bReplicateScale = Settings.bReplicateScale;
	TransformState.Location = Transform.GetLocation();
	TransformState.Rotation = Transform.Rotator();
	TransformState.Scale = Transform.GetScale3D();
	return TransformState;
}

bool UReplicTransportComponent::HasTrackedTransformChanged(const FReplicComponentTransformSettings& Settings, const FTransform& PreviousTransform, const FTransform& CurrentTransform) const
{
	if (Settings.bReplicateLocation && !PreviousTransform.GetLocation().Equals(CurrentTransform.GetLocation(), FMath::Max(Settings.LocationThreshold, 0.0f)))
	{
		return true;
	}

	if (Settings.bReplicateRotation && !PreviousTransform.Rotator().Equals(CurrentTransform.Rotator(), FMath::Max(Settings.RotationThresholdDegrees, 0.0f)))
	{
		return true;
	}

	if (Settings.bReplicateScale && !PreviousTransform.GetScale3D().Equals(CurrentTransform.GetScale3D(), FMath::Max(Settings.ScaleThreshold, 0.0f)))
	{
		return true;
	}

	return false;
}

int32 UReplicTransportComponent::FindPersistentComponentTransformStateIndex(FName ComponentName) const
{
	const FName NormalizedComponentName = NormalizeComponentName(ComponentName);
	return ReplicatedComponentTransformStates.IndexOfByPredicate(
		[NormalizedComponentName](const FReplicComponentTransformState& ExistingState)
		{
			return NormalizeComponentName(ExistingState.ComponentName) == NormalizedComponentName;
		});
}

void UReplicTransportComponent::StorePersistentComponentTransformState(const FReplicComponentTransformState& TransformState)
{
	bool bChanged = false;
	const int32 ExistingIndex = FindPersistentComponentTransformStateIndex(TransformState.ComponentName);
	if (ExistingIndex == INDEX_NONE)
	{
		ReplicatedComponentTransformStates.Add(TransformState);
		bChanged = true;
	}
	else
	{
		FReplicComponentTransformState& ExistingState = ReplicatedComponentTransformStates[ExistingIndex];
		bChanged =
			ExistingState.TransformSpace != TransformState.TransformSpace ||
			ExistingState.bReplicateLocation != TransformState.bReplicateLocation ||
			ExistingState.bReplicateRotation != TransformState.bReplicateRotation ||
			ExistingState.bReplicateScale != TransformState.bReplicateScale ||
			!ExistingState.Location.Equals(TransformState.Location, 0.01f) ||
			!ExistingState.Rotation.Equals(TransformState.Rotation, 0.01f) ||
			!ExistingState.Scale.Equals(TransformState.Scale, 0.0001f);

		if (bChanged)
		{
			ExistingState = TransformState;
		}
	}

	if (bChanged)
	{
		if (AActor* HostActor = GetOwner())
		{
			HostActor->ForceNetUpdate();
		}
	}
}

void UReplicTransportComponent::ApplyComponentTransformState(const FReplicComponentTransformState& TransformState)
{
	USceneComponent* SceneComponent = FindSceneComponentByName(TransformState.ComponentName);
	if (!SceneComponent)
	{
		return;
	}

	if (TransformState.TransformSpace == EReplicTransformSpace::World)
	{
		if (TransformState.bReplicateLocation)
		{
			SceneComponent->SetWorldLocation(TransformState.Location);
		}

		if (TransformState.bReplicateRotation)
		{
			SceneComponent->SetWorldRotation(TransformState.Rotation);
		}

		if (TransformState.bReplicateScale)
		{
			SceneComponent->SetWorldScale3D(TransformState.Scale);
		}
	}
	else
	{
		if (TransformState.bReplicateLocation)
		{
			SceneComponent->SetRelativeLocation(TransformState.Location);
		}

		if (TransformState.bReplicateRotation)
		{
			SceneComponent->SetRelativeRotation(TransformState.Rotation);
		}

		if (TransformState.bReplicateScale)
		{
			SceneComponent->SetRelativeScale3D(TransformState.Scale);
		}
	}
}

void UReplicTransportComponent::RefreshComponentTransformTracking(bool bCommitPersistentInitialState, bool bForceSend)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const double CurrentTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const FReplicComponentTransformSettings& Settings : ComponentTransformSettings)
	{
		if (Settings.ComponentName.IsNone() || !Settings.HasAnyReplicatedChannel())
		{
			continue;
		}

		const FName ComponentKey = NormalizeComponentName(Settings.ComponentName);
		USceneComponent* SceneComponent = FindSceneComponentByName(Settings.ComponentName);
		if (!SceneComponent)
		{
			continue;
		}

		const FTransform CurrentTransform = ReadComponentTransform(SceneComponent, Settings.TransformSpace);
		const FTransform* PreviousTransform = LastObservedComponentTransforms.Find(ComponentKey);
		const bool bHasPreviousTransform = PreviousTransform != nullptr;
		const bool bChanged = bHasPreviousTransform
			? HasTrackedTransformChanged(Settings, *PreviousTransform, CurrentTransform)
			: false;

		if (!bChanged && !(bCommitPersistentInitialState && Settings.bPersistentState))
		{
			if (!bHasPreviousTransform)
			{
				LastObservedComponentTransforms.Add(ComponentKey, CurrentTransform);
				LastComponentTransformSendTimes.Add(ComponentKey, CurrentTimeSeconds);
			}
			continue;
		}

		const double* LastSendTime = LastComponentTransformSendTimes.Find(ComponentKey);
		const bool bHasLastSendTime = LastSendTime != nullptr;
		const float MinUpdateIntervalSeconds = FMath::Max(Settings.MinUpdateIntervalSeconds, 0.0f);
		if (!bForceSend && !bCommitPersistentInitialState && bHasLastSendTime && CurrentTimeSeconds - *LastSendTime < MinUpdateIntervalSeconds)
		{
			continue;
		}

		const FReplicComponentTransformState TransformState = MakeComponentTransformState(Settings, CurrentTransform);
		if (Settings.bPersistentState)
		{
			StorePersistentComponentTransformState(TransformState);
			if (bChanged)
			{
				MulticastDispatchComponentTransformState(TransformState);
			}
		}
		else if (bChanged)
		{
			MulticastDispatchComponentTransformState(TransformState);
		}

		LastObservedComponentTransforms.Add(ComponentKey, CurrentTransform);
		LastComponentTransformSendTimes.Add(ComponentKey, CurrentTimeSeconds);
	}
}

bool UReplicTransportComponent::RequestMarkedPropertyWrite(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const FString& SerializedValue)
{
	// The requester has to resolve to a host actor first, otherwise the server cannot attribute the write to any connection.
	FReplicResolvedTarget RequestTarget;
	FReplicResolvedTarget TargetTarget;
	if (!ReplicRuntimeUtils::ResolveTarget(ContextObject, RequestTarget) || !ReplicRuntimeUtils::ResolveTarget(TargetObject, TargetTarget))
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic property write request failed for '%s': requester or target could not be resolved."), *PropertyName.ToString()),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetTarget.HostActor))
		{
			return TargetTransport->ApplyAuthoritativePropertyWrite(GetOwner(), true, TargetTarget.HostActor, TargetTarget.Descriptor, PropertyName, SerializedValue);
		}

		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic property write request failed for '%s': target transport could not be created on '%s'."), *PropertyName.ToString(), *GetPathNameSafe(TargetTarget.HostActor)),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetTarget.HostActor, TargetTarget.Descriptor, ResolvedObjects);

	const UObject* MetadataSourceObject = ResolvedObjects.Num() > 0 ? ResolvedObjects[0].Get() : TargetTarget.HostActor;
	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(const_cast<UObject*>(MetadataSourceObject), PropertyName);
	if (!TargetProperty)
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic property write request failed: property '%s' was not found on '%s'."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	FReplicVariableSettings Settings;
	if (!ReplicRuntimeUtils::TryGetVariableSettings(const_cast<UObject*>(MetadataSourceObject), PropertyName, Settings))
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic property write request failed: property '%s' on '%s' is not marked for Replic."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::OwnerOnly && !IsOwnedByRequester(RequestTarget.HostActor, TargetTarget.HostActor))
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestTarget.HostActor, TEXT("OwnerOnly preflight check failed"));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::ServerOnly)
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestTarget.HostActor, TEXT("ServerOnly preflight check failed"));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::Custom)
	{
		bool bAllowed = false;
		if (!EvaluateCustomPropertyPermission(const_cast<UObject*>(MetadataSourceObject), PropertyName, RequestTarget.HostActor, bAllowed) || !bAllowed)
		{
			LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestTarget.HostActor, TEXT("Custom preflight check failed"));
			return false;
		}
	}

	ServerRequestPropertyWrite(TargetTarget.HostActor, TargetTarget.Descriptor, PropertyName, SerializedValue);
	EmitReplicDebugMessage(
		EReplicDebugChannel::Writes,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic property write requested: '%s' on '%s' from '%s'."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject), *GetPathNameSafe(RequestTarget.HostActor)),
		FColor::Cyan,
	FString::Printf(TEXT("Write Requested: %s"), *PropertyName.ToString()));
	return true;
}

bool UReplicTransportComponent::RequestMarkedContainerDelta(
	UObject* ContextObject,
	UObject* TargetObject,
	FName PropertyName,
	EReplicContainerDeltaOperation Operation,
	const FString& SerializedPrimaryValue,
	const FString& SerializedSecondaryValue)
{
	FReplicResolvedTarget RequestTarget;
	FReplicResolvedTarget TargetTarget;
	if (!ReplicRuntimeUtils::ResolveTarget(ContextObject, RequestTarget) || !ReplicRuntimeUtils::ResolveTarget(TargetObject, TargetTarget))
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic container delta request failed for '%s' (%s): requester or target could not be resolved."), *PropertyName.ToString(), GetContainerDeltaOperationLabel(Operation)),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetTarget.HostActor))
		{
			return TargetTransport->ApplyAuthoritativeContainerDelta(
				GetOwner(),
				true,
				TargetTarget.HostActor,
				TargetTarget.Descriptor,
				PropertyName,
				Operation,
				SerializedPrimaryValue,
				SerializedSecondaryValue);
		}

		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic container delta request failed for '%s' (%s): target transport could not be created on '%s'."), *PropertyName.ToString(), GetContainerDeltaOperationLabel(Operation), *GetPathNameSafe(TargetTarget.HostActor)),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetTarget.HostActor, TargetTarget.Descriptor, ResolvedObjects);

	const UObject* MetadataSourceObject = ResolvedObjects.Num() > 0 ? ResolvedObjects[0].Get() : TargetTarget.HostActor;
	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(const_cast<UObject*>(MetadataSourceObject), PropertyName);
	if (!TargetProperty)
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic container delta request failed: property '%s' was not found on '%s'."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	FReplicVariableSettings Settings;
	if (!ReplicRuntimeUtils::TryGetVariableSettings(const_cast<UObject*>(MetadataSourceObject), PropertyName, Settings))
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic container delta request failed: property '%s' on '%s' is not marked for Replic."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Red,
			FString::Printf(TEXT("Write Failed: %s"), *PropertyName.ToString()));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::OwnerOnly && !IsOwnedByRequester(RequestTarget.HostActor, TargetTarget.HostActor))
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestTarget.HostActor, TEXT("OwnerOnly preflight check failed"));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::ServerOnly)
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestTarget.HostActor, TEXT("ServerOnly preflight check failed"));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::Custom)
	{
		bool bAllowed = false;
		if (!EvaluateCustomPropertyPermission(const_cast<UObject*>(MetadataSourceObject), PropertyName, RequestTarget.HostActor, bAllowed) || !bAllowed)
		{
			LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestTarget.HostActor, TEXT("Custom preflight check failed"));
			return false;
		}
	}

	ServerRequestContainerDelta(
		TargetTarget.HostActor,
		TargetTarget.Descriptor,
		PropertyName,
		Operation,
		SerializedPrimaryValue,
		SerializedSecondaryValue);
	EmitReplicDebugMessage(
		EReplicDebugChannel::Writes,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic container delta requested: '%s' (%s) on '%s' from '%s'."), *PropertyName.ToString(), GetContainerDeltaOperationLabel(Operation), *GetPathNameSafe(MetadataSourceObject), *GetPathNameSafe(RequestTarget.HostActor)),
		FColor::Cyan,
		FString::Printf(TEXT("Write Requested: %s"), *PropertyName.ToString()));
	return true;
}

bool UReplicTransportComponent::RequestMarkedEvent(UObject* ContextObject, UObject* TargetObject, FName EventName, const TArray<FReplicNamedValue>& Arguments)
{
	// The requester has to resolve to a host actor first, otherwise the server cannot attribute the event to any connection.
	FReplicResolvedTarget RequestTarget;
	FReplicResolvedTarget TargetTarget;
	if (!ReplicRuntimeUtils::ResolveTarget(ContextObject, RequestTarget) || !ReplicRuntimeUtils::ResolveTarget(TargetObject, TargetTarget))
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Events,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic event request failed for '%s': requester or target could not be resolved."), *EventName.ToString()),
			FColor::Red,
			FString::Printf(TEXT("Event Failed: %s"), *EventName.ToString()));
		return false;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetTarget.HostActor))
		{
			return TargetTransport->ApplyAuthoritativeEvent(GetOwner(), true, TargetTarget.HostActor, TargetTarget.Descriptor, EventName, Arguments);
		}

		EmitReplicDebugMessage(
			EReplicDebugChannel::Events,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic event request failed for '%s': target transport could not be created on '%s'."), *EventName.ToString(), *GetPathNameSafe(TargetTarget.HostActor)),
			FColor::Red,
			FString::Printf(TEXT("Event Failed: %s"), *EventName.ToString()));
		return false;
	}

	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetTarget.HostActor, TargetTarget.Descriptor, ResolvedObjects);

	const UObject* MetadataSourceObject = ResolvedObjects.Num() > 0 ? ResolvedObjects[0].Get() : TargetTarget.HostActor;
	UFunction* TargetFunction = ReplicRuntimeUtils::FindFunctionByName(const_cast<UObject*>(MetadataSourceObject), EventName);
	if (!TargetFunction)
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Events,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic event request failed: event '%s' was not found on '%s'."), *EventName.ToString(), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Red,
			FString::Printf(TEXT("Event Failed: %s"), *EventName.ToString()));
		return false;
	}

	FReplicEventSettings Settings;
	if (!ReplicRuntimeUtils::TryGetEventSettings(const_cast<UObject*>(MetadataSourceObject), EventName, Settings))
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Events,
			ELogVerbosity::Warning,
			FString::Printf(TEXT("Replic event request failed: event '%s' on '%s' is not marked for Replic."), *EventName.ToString(), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Red,
			FString::Printf(TEXT("Event Failed: %s"), *EventName.ToString()));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::OwnerOnly && !IsOwnedByRequester(RequestTarget.HostActor, TargetTarget.HostActor))
	{
		LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestTarget.HostActor, TEXT("OwnerOnly preflight check failed"));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::ServerOnly)
	{
		LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestTarget.HostActor, TEXT("ServerOnly preflight check failed"));
		return false;
	}

	if (Settings.PermissionMode == EReplicPermissionMode::Custom)
	{
		bool bAllowed = false;
		if (!EvaluateCustomEventPermission(const_cast<UObject*>(MetadataSourceObject), EventName, RequestTarget.HostActor, bAllowed) || !bAllowed)
		{
			LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestTarget.HostActor, TEXT("Custom preflight check failed"));
			return false;
		}
	}

	ServerRequestEvent(TargetTarget.HostActor, TargetTarget.Descriptor, EventName, Arguments);
	EmitReplicDebugMessage(
		EReplicDebugChannel::Events,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic event requested: '%s' on '%s' from '%s'."), *EventName.ToString(), *GetPathNameSafe(MetadataSourceObject), *GetPathNameSafe(RequestTarget.HostActor)),
		FColor::Cyan,
		FString::Printf(TEXT("Event Requested: %s"), *EventName.ToString()));
	return true;
}

void UReplicTransportComponent::ApplyStoredStateToObservedObject(UObject* ObservedObject)
{
	FReplicResolvedTarget ResolvedTarget;
	if (!ReplicRuntimeUtils::ResolveTarget(ObservedObject, ResolvedTarget))
	{
		return;
	}

	for (const FReplicStateEntry& Entry : ReplicatedStates.Items)
	{
		if (Entry.Target.ToKey() == ResolvedTarget.Descriptor.ToKey())
		{
			ApplySerializedPropertyToTargets(ResolvedTarget.HostActor, Entry.Target, Entry.PropertyName, Entry.SerializedValue);
		}
	}

	EmitReplicDebugMessage(
		EReplicDebugChannel::State,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic re-applied stored state to observed object '%s'."), *GetPathNameSafe(ObservedObject)),
		FColor::Yellow,
		TEXT("State Reapplied"));
}

void UReplicTransportComponent::HandleReplicatedStateEntryChanged(const FReplicStateEntry& Entry)
{
	ApplySerializedPropertyToTargets(GetOwner(), Entry.Target, Entry.PropertyName, Entry.SerializedValue);
}

void UReplicTransportComponent::HandleReplicatedStateEntryRemoved(int32 RemovedIndex)
{
	// v1 does not expose state deletion yet. The hook stays in place because the fast array API requires it.
}

void UReplicTransportComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UReplicTransportComponent, ReplicatedStates);
	DOREPLIFETIME(UReplicTransportComponent, ReplicatedComponentTransformStates);
}

void UReplicTransportComponent::OnRegister()
{
	Super::OnRegister();

	// Blueprint-added ReplicTransport components should always replicate their internal Replic state.
	// SetIsReplicatedByDefault covers the native default object, but this keeps placed Blueprint component
	// instances authoritative even when their template was created before the current plugin version.
	if (!GetIsReplicated())
	{
		SetIsReplicated(true);
	}

	if (!IsComponentTickEnabled())
	{
		SetComponentTickEnabled(true);
	}
}

void UReplicTransportComponent::BeginPlay()
{
	Super::BeginPlay();
	ReplicatedStates.Owner = this;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RefreshComponentTransformTracking(true);
	}

	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			// Late joiners can receive the persistent state before the local actor finished its own BeginPlay setup.
			// Re-applying on the next tick keeps the replicated state authoritative after Blueprint initialization runs.
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateWeakLambda(
					this,
					[this]()
					{
						EmitReplicDebugMessage(
							EReplicDebugChannel::State,
							ELogVerbosity::Log,
							FString::Printf(TEXT("Replic late-join state reapply scheduled for '%s'."), *GetPathNameSafe(GetOwner())),
							FColor::Yellow,
							TEXT("Late Join Reapply"));
						ReapplyAllPersistentState();
						OnRep_ReplicatedComponentTransformStates();
					}));
		}
	}
}

void UReplicTransportComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshComponentTransformTracking(false);
}

void UReplicTransportComponent::ServerRequestPropertyWrite_Implementation(
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	const FString& SerializedValue)
{
	if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetHostActor))
	{
		TargetTransport->ApplyAuthoritativePropertyWrite(GetOwner(), false, TargetHostActor, TargetDescriptor, PropertyName, SerializedValue);
	}
}

void UReplicTransportComponent::ServerRequestContainerDelta_Implementation(
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	EReplicContainerDeltaOperation Operation,
	const FString& SerializedPrimaryValue,
	const FString& SerializedSecondaryValue)
{
	if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetHostActor))
	{
		TargetTransport->ApplyAuthoritativeContainerDelta(
			GetOwner(),
			false,
			TargetHostActor,
			TargetDescriptor,
			PropertyName,
			Operation,
			SerializedPrimaryValue,
			SerializedSecondaryValue);
	}
}

void UReplicTransportComponent::ServerRequestEvent_Implementation(
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName EventName,
	const TArray<FReplicNamedValue>& Arguments)
{
	if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetHostActor))
	{
		TargetTransport->ApplyAuthoritativeEvent(GetOwner(), false, TargetHostActor, TargetDescriptor, EventName, Arguments);
	}
}

void UReplicTransportComponent::MulticastDispatchTransientProperty_Implementation(
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	const FString& SerializedValue)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		ApplySerializedPropertyToTargets(GetOwner(), TargetDescriptor, PropertyName, SerializedValue);
	}
}

void UReplicTransportComponent::MulticastDispatchEvent_Implementation(const FReplicEventMessage& EventMessage)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		ExecuteSerializedEventOnTargets(GetOwner(), EventMessage.Target, EventMessage.EventName, EventMessage.Arguments);
	}
}

void UReplicTransportComponent::ClientDispatchEvent_Implementation(const FReplicEventMessage& EventMessage)
{
	ExecuteSerializedEventOnTargets(GetOwner(), EventMessage.Target, EventMessage.EventName, EventMessage.Arguments);
}

void UReplicTransportComponent::MulticastDispatchComponentTransformState_Implementation(const FReplicComponentTransformState& TransformState)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		ApplyComponentTransformState(TransformState);
	}
}

void UReplicTransportComponent::OnRep_ReplicatedComponentTransformStates()
{
	for (const FReplicComponentTransformState& TransformState : ReplicatedComponentTransformStates)
	{
		ApplyComponentTransformState(TransformState);
	}
}

bool UReplicTransportComponent::ApplyAuthoritativePropertyWrite(
	AActor* RequestHostActor,
	bool bRequestOriginatedOnServer,
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	const FString& SerializedValue)
{
	if (!TargetHostActor)
	{
		return false;
	}

	if (TargetHostActor != GetOwner())
	{
		// Route the request onto the transport that belongs to the actual target actor.
		// This keeps persistent state and multicast dispatch owned by the object that stores the replicated value.
		if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetHostActor))
		{
			return TargetTransport != this
				? TargetTransport->ApplyAuthoritativePropertyWrite(RequestHostActor, bRequestOriginatedOnServer, TargetHostActor, TargetDescriptor, PropertyName, SerializedValue)
				: false;
		}

		return false;
	}

	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetHostActor, TargetDescriptor, ResolvedObjects);

	const UObject* MetadataSourceObject = ResolvedObjects.Num() > 0 ? ResolvedObjects[0].Get() : TargetHostActor;
	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(const_cast<UObject*>(MetadataSourceObject), PropertyName);
	if (!TargetProperty)
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("target property could not be resolved"));
		return false;
	}

	FReplicVariableSettings Settings;
	if (!ReplicRuntimeUtils::TryGetVariableSettings(const_cast<UObject*>(MetadataSourceObject), PropertyName, Settings))
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("property is not Replic-enabled"));
		return false;
	}

	if (!ValidateAuthoritativePropertyPermission(Settings, MetadataSourceObject, PropertyName, RequestHostActor, TargetHostActor, bRequestOriginatedOnServer))
	{
		return false;
	}

	return FinalizeAuthoritativePropertyWrite(
		RequestHostActor,
		MetadataSourceObject,
		TargetHostActor,
		TargetDescriptor,
		PropertyName,
		SerializedValue,
		Settings);
}

bool UReplicTransportComponent::ApplyAuthoritativeContainerDelta(
	AActor* RequestHostActor,
	bool bRequestOriginatedOnServer,
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	EReplicContainerDeltaOperation Operation,
	const FString& SerializedPrimaryValue,
	const FString& SerializedSecondaryValue)
{
	if (!TargetHostActor)
	{
		return false;
	}

	if (TargetHostActor != GetOwner())
	{
		if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetHostActor))
		{
			return TargetTransport != this
				? TargetTransport->ApplyAuthoritativeContainerDelta(
					RequestHostActor,
					bRequestOriginatedOnServer,
					TargetHostActor,
					TargetDescriptor,
					PropertyName,
					Operation,
					SerializedPrimaryValue,
					SerializedSecondaryValue)
				: false;
		}

		return false;
	}

	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetHostActor, TargetDescriptor, ResolvedObjects);

	const UObject* MetadataSourceObject = ResolvedObjects.Num() > 0 ? ResolvedObjects[0].Get() : TargetHostActor;
	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(const_cast<UObject*>(MetadataSourceObject), PropertyName);
	if (!TargetProperty)
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("target property could not be resolved"));
		return false;
	}

	FReplicVariableSettings Settings;
	if (!ReplicRuntimeUtils::TryGetVariableSettings(const_cast<UObject*>(MetadataSourceObject), PropertyName, Settings))
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("property is not Replic-enabled"));
		return false;
	}

	if (!ValidateAuthoritativePropertyPermission(Settings, MetadataSourceObject, PropertyName, RequestHostActor, TargetHostActor, bRequestOriginatedOnServer))
	{
		return false;
	}

	void* CurrentValuePtr = TargetProperty->ContainerPtrToValuePtr<void>(const_cast<UObject*>(MetadataSourceObject));
	if (!CurrentValuePtr)
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("target property value pointer could not be resolved"));
		return false;
	}

	FString SerializedValue;
	bool bChanged = false;
	if (!BuildSerializedContainerDeltaResult(TargetProperty, CurrentValuePtr, Operation, SerializedPrimaryValue, SerializedSecondaryValue, SerializedValue, bChanged))
	{
		LogPermissionDecision(TEXT("property write"), PropertyName, MetadataSourceObject, RequestHostActor, TEXT("container delta could not be applied to the target property"));
		return false;
	}

	if (!bChanged)
	{
		EmitReplicDebugMessage(
			EReplicDebugChannel::Writes,
			ELogVerbosity::Log,
			FString::Printf(TEXT("Replic container delta produced no change: '%s' (%s) on '%s'."), *PropertyName.ToString(), GetContainerDeltaOperationLabel(Operation), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Green,
			FString::Printf(TEXT("Write OK: %s"), *PropertyName.ToString()));
		return true;
	}

	return FinalizeAuthoritativePropertyWrite(
		RequestHostActor,
		MetadataSourceObject,
		TargetHostActor,
		TargetDescriptor,
		PropertyName,
		SerializedValue,
		Settings);
}

bool UReplicTransportComponent::FinalizeAuthoritativePropertyWrite(
	AActor* RequestHostActor,
	const UObject* MetadataSourceObject,
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	const FString& SerializedValue,
	const FReplicVariableSettings& Settings)
{
	ApplySerializedPropertyToTargets(TargetHostActor, TargetDescriptor, PropertyName, SerializedValue);
	EmitReplicDebugMessage(
		EReplicDebugChannel::Writes,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic property write applied: '%s' on '%s' from '%s'."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject), *GetPathNameSafe(RequestHostActor)),
		FColor::Green,
		FString::Printf(TEXT("Write OK: %s"), *PropertyName.ToString()));

	if (Settings.bPersistentState)
	{
		if (Settings.bUseBatching)
		{
			QueueStateWrite(TargetDescriptor, PropertyName, SerializedValue, Settings.BatchIntervalSeconds);
			EmitReplicDebugMessage(
				EReplicDebugChannel::State,
				ELogVerbosity::Log,
				FString::Printf(TEXT("Replic queued persistent state write: '%s' on '%s' (batch %.3fs)."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject), Settings.BatchIntervalSeconds),
				FColor::Yellow,
				FString::Printf(TEXT("State Queued: %s"), *PropertyName.ToString()));
		}
		else
		{
			CommitPersistentState(TargetDescriptor, PropertyName, SerializedValue);
		}
	}
	else
	{
		MulticastDispatchTransientProperty(TargetDescriptor, PropertyName, SerializedValue);
		EmitReplicDebugMessage(
			EReplicDebugChannel::State,
			ELogVerbosity::Log,
			FString::Printf(TEXT("Replic multicast transient property: '%s' on '%s'."), *PropertyName.ToString(), *GetPathNameSafe(MetadataSourceObject)),
			FColor::Yellow,
			FString::Printf(TEXT("Transient Sync: %s"), *PropertyName.ToString()));
	}

	return true;
}

bool UReplicTransportComponent::ApplyAuthoritativeEvent(
	AActor* RequestHostActor,
	bool bRequestOriginatedOnServer,
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName EventName,
	const TArray<FReplicNamedValue>& Arguments)
{
	if (!TargetHostActor)
	{
		return false;
	}

	if (TargetHostActor != GetOwner())
	{
		// Route the request onto the transport that belongs to the actual target actor.
		// This keeps persistent state and multicast dispatch owned by the object that stores the replicated value.
		if (UReplicTransportComponent* TargetTransport = FindOrCreate(TargetHostActor))
		{
			return TargetTransport != this
				? TargetTransport->ApplyAuthoritativeEvent(RequestHostActor, bRequestOriginatedOnServer, TargetHostActor, TargetDescriptor, EventName, Arguments)
				: false;
		}

		return false;
	}

	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetHostActor, TargetDescriptor, ResolvedObjects);

	const UObject* MetadataSourceObject = ResolvedObjects.Num() > 0 ? ResolvedObjects[0].Get() : TargetHostActor;
	UFunction* TargetFunction = ReplicRuntimeUtils::FindFunctionByName(const_cast<UObject*>(MetadataSourceObject), EventName);
	if (!TargetFunction)
	{
		LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestHostActor, TEXT("target event could not be resolved"));
		return false;
	}

	FReplicEventSettings Settings;
	if (!ReplicRuntimeUtils::TryGetEventSettings(const_cast<UObject*>(MetadataSourceObject), EventName, Settings))
	{
		LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestHostActor, TEXT("event is not Replic-enabled"));
		return false;
	}

	if (!bRequestOriginatedOnServer)
	{
		// Event requests use the same permission model as property writes, but dispatch mode is evaluated only after the
		// request has been accepted.
		switch (Settings.PermissionMode)
		{
		case EReplicPermissionMode::None:
			break;
		case EReplicPermissionMode::OwnerOnly:
			if (!IsOwnedByRequester(RequestHostActor, TargetHostActor))
			{
				LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestHostActor, TEXT("OwnerOnly check failed"));
				return false;
			}
			break;
		case EReplicPermissionMode::ServerOnly:
			LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestHostActor, TEXT("ServerOnly check failed"));
			return false;
		case EReplicPermissionMode::Custom:
			break;
		default:
			break;
		}
	}

	if (Settings.PermissionMode == EReplicPermissionMode::Custom)
	{
		bool bAllowed = false;
		if (!EvaluateCustomEventPermission(const_cast<UObject*>(MetadataSourceObject), EventName, RequestHostActor, bAllowed))
		{
			LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestHostActor, TEXT("custom validation function missing or unsupported"));
			return false;
		}

		if (!bAllowed)
		{
			LogPermissionDecision(TEXT("event"), EventName, MetadataSourceObject, RequestHostActor, TEXT("custom validation rejected the request"));
			return false;
		}
	}

	if (Settings.Mode == EReplicEventMode::LocalOnly)
	{
		// LocalOnly still routes through the authoritative gate; it just stops after server/local execution.
		const bool bExecuted = ExecuteSerializedEventOnTargets(TargetHostActor, TargetDescriptor, EventName, Arguments);
		if (bExecuted)
		{
			RefreshComponentTransformTracking(false, true);
		}
		return bExecuted;
	}

	const bool bExecuted = ExecuteSerializedEventOnTargets(TargetHostActor, TargetDescriptor, EventName, Arguments);
	if (!bExecuted)
	{
		return false;
	}

	RefreshComponentTransformTracking(false, true);
	EmitReplicDebugMessage(
		EReplicDebugChannel::Events,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic event applied: '%s' on '%s' from '%s'."), *EventName.ToString(), *GetPathNameSafe(MetadataSourceObject), *GetPathNameSafe(RequestHostActor)),
		FColor::Green,
		FString::Printf(TEXT("Event OK: %s"), *EventName.ToString()));

	FReplicEventMessage EventMessage;
	EventMessage.Target = TargetDescriptor;
	EventMessage.EventName = EventName;
	EventMessage.Mode = Settings.Mode;
	EventMessage.Arguments = Arguments;

	if (Settings.Mode == EReplicEventMode::ServerOnly)
	{
		return true;
	}

	if (Settings.Mode == EReplicEventMode::OwnerOnly)
	{
		ClientDispatchEvent(EventMessage);
		EmitReplicDebugMessage(
			EReplicDebugChannel::Events,
			ELogVerbosity::Log,
			FString::Printf(TEXT("Replic dispatched event '%s' in OwnerOnly mode."), *EventName.ToString()),
			FColor::Yellow,
			FString::Printf(TEXT("Event OwnerOnly: %s"), *EventName.ToString()));
		return true;
	}

	MulticastDispatchEvent(EventMessage);
	EmitReplicDebugMessage(
		EReplicDebugChannel::Events,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic multicast event '%s' in mode '%d'."), *EventName.ToString(), static_cast<int32>(Settings.Mode)),
		FColor::Yellow,
		FString::Printf(TEXT("Event Dispatch: %s"), *EventName.ToString()));
	return true;
}

bool UReplicTransportComponent::ApplySerializedPropertyToTargets(
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	const FString& SerializedValue)
{
	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetHostActor, TargetDescriptor, ResolvedObjects);

	bool bAppliedAny = false;
	for (const TWeakObjectPtr<UObject>& ObjectPtr : ResolvedObjects)
	{
		if (UObject* ResolvedObject = ObjectPtr.Get())
		{
			if (FProperty* Property = ReplicRuntimeUtils::FindPropertyByName(ResolvedObject, PropertyName))
			{
				FString PreviousSerializedValue;
				ReplicRuntimeUtils::ExportObjectPropertyToText(ResolvedObject, Property, PreviousSerializedValue);

				if (ReplicRuntimeUtils::ImportObjectPropertyFromText(ResolvedObject, Property, SerializedValue))
				{
					bAppliedAny = true;

					FString CurrentSerializedValue;
					ReplicRuntimeUtils::ExportObjectPropertyToText(ResolvedObject, Property, CurrentSerializedValue);
					if (PreviousSerializedValue != CurrentSerializedValue)
					{
						EmitReplicDebugMessage(
							EReplicDebugChannel::State,
							ELogVerbosity::Log,
							FString::Printf(TEXT("Replic applied local property '%s' on '%s': '%s' -> '%s'."), *PropertyName.ToString(), *GetPathNameSafe(ResolvedObject), *PreviousSerializedValue, *CurrentSerializedValue),
							FColor::Silver,
							FString::Printf(TEXT("Local Apply: %s"), *PropertyName.ToString()));
						BroadcastMarkedPropertyChanged(ResolvedObject, PropertyName);
					}
				}
			}
		}
	}

	return bAppliedAny;
}

bool UReplicTransportComponent::ExecuteSerializedEventOnTargets(
	AActor* TargetHostActor,
	const FReplicTargetDescriptor& TargetDescriptor,
	FName EventName,
	const TArray<FReplicNamedValue>& Arguments) const
{
	TArray<TWeakObjectPtr<UObject>> ResolvedObjects;
	ReplicRuntimeUtils::ResolveLocalObjects(TargetHostActor, TargetDescriptor, ResolvedObjects);

	bool bExecutedAny = false;
	for (const TWeakObjectPtr<UObject>& ObjectPtr : ResolvedObjects)
	{
		if (UObject* ResolvedObject = ObjectPtr.Get())
		{
			if (UFunction* TargetFunction = ReplicRuntimeUtils::FindFunctionByName(ResolvedObject, EventName))
			{
				EmitReplicDebugMessage(
					EReplicDebugChannel::Events,
					ELogVerbosity::Log,
					FString::Printf(TEXT("Replic executing event '%s' on '%s'."), *EventName.ToString(), *GetPathNameSafe(ResolvedObject)),
					FColor::Silver,
					FString::Printf(TEXT("Exec Event: %s"), *EventName.ToString()));
				bExecutedAny |= ReplicRuntimeUtils::InvokeFunctionBySerializedArguments(ResolvedObject, TargetFunction, Arguments);
			}
		}
	}

	return bExecutedAny;
}

void UReplicTransportComponent::QueueStateWrite(
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	const FString& SerializedValue,
	float BatchIntervalSeconds)
{
	const double FlushAtTime = GetWorld() ? (GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, BatchIntervalSeconds)) : 0.0;
	const FString StateKey = FString::Printf(TEXT("%s|%s"), *TargetDescriptor.ToKey(), *PropertyName.ToString());

	if (FReplicPendingStateWrite* ExistingWrite = PendingStateWrites.FindByPredicate(
		[&StateKey](const FReplicPendingStateWrite& PendingWrite)
		{
			return FString::Printf(TEXT("%s|%s"), *PendingWrite.Target.ToKey(), *PendingWrite.PropertyName.ToString()) == StateKey;
		}))
	{
		ExistingWrite->SerializedValue = SerializedValue;
		ExistingWrite->FlushAtTimeSeconds = FlushAtTime;
	}
	else
	{
		FReplicPendingStateWrite& PendingWrite = PendingStateWrites.AddDefaulted_GetRef();
		PendingWrite.Target = TargetDescriptor;
		PendingWrite.PropertyName = PropertyName;
		PendingWrite.SerializedValue = SerializedValue;
		PendingWrite.FlushAtTimeSeconds = FlushAtTime;
	}

	if (GetWorld())
	{
		// Only one timer is needed; every new write refreshes the scheduled flush and the queue keeps the latest value per key.
		GetWorld()->GetTimerManager().SetTimer(BatchFlushTimerHandle, this, &UReplicTransportComponent::FlushPendingStateWrites, FMath::Max(BatchIntervalSeconds, 0.01f), false);
	}
}

void UReplicTransportComponent::FlushPendingStateWrites()
{
	for (const FReplicPendingStateWrite& PendingWrite : PendingStateWrites)
	{
		CommitPersistentState(PendingWrite.Target, PendingWrite.PropertyName, PendingWrite.SerializedValue);
	}

	EmitReplicDebugMessage(
		EReplicDebugChannel::State,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic flushed %d pending state write(s) on '%s'."), PendingStateWrites.Num(), *GetPathNameSafe(GetOwner())),
		FColor::Yellow,
		TEXT("State Flush"));
	PendingStateWrites.Reset();
}

void UReplicTransportComponent::CommitPersistentState(
	const FReplicTargetDescriptor& TargetDescriptor,
	FName PropertyName,
	const FString& SerializedValue)
{
	int32 EntryIndex = FindStateEntryIndex(TargetDescriptor, PropertyName);
	if (EntryIndex == INDEX_NONE)
	{
		// Persistent state is keyed by (target descriptor + property name), so identical classes still keep per-instance values.
		FReplicStateEntry& NewEntry = ReplicatedStates.Items.AddDefaulted_GetRef();
		NewEntry.Target = TargetDescriptor;
		NewEntry.PropertyName = PropertyName;
		NewEntry.SerializedValue = SerializedValue;
		NewEntry.Revision = 1;
		ReplicatedStates.MarkItemDirty(NewEntry);
	}
	else
	{
		FReplicStateEntry& ExistingEntry = ReplicatedStates.Items[EntryIndex];
		ExistingEntry.SerializedValue = SerializedValue;
		++ExistingEntry.Revision;
		ReplicatedStates.MarkItemDirty(ExistingEntry);
	}

	EmitReplicDebugMessage(
		EReplicDebugChannel::State,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic committed persistent state '%s' on '%s'."), *PropertyName.ToString(), *GetPathNameSafe(GetOwner())),
		FColor::Yellow,
		FString::Printf(TEXT("State Stored: %s"), *PropertyName.ToString()));
}

int32 UReplicTransportComponent::FindStateEntryIndex(const FReplicTargetDescriptor& TargetDescriptor, FName PropertyName) const
{
	return ReplicatedStates.Items.IndexOfByPredicate(
		[&TargetDescriptor, PropertyName](const FReplicStateEntry& Entry)
		{
			return Entry.PropertyName == PropertyName && Entry.Target.ToKey() == TargetDescriptor.ToKey();
		});
}

void UReplicTransportComponent::BroadcastMarkedPropertyChanged(UObject* TargetObject, FName PropertyName)
{
	EmitReplicDebugMessage(
		EReplicDebugChannel::Observers,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic observer change broadcast: '%s' on '%s'."), *PropertyName.ToString(), *GetPathNameSafe(TargetObject)),
		FColor::Blue,
		FString::Printf(TEXT("Observer: %s"), *PropertyName.ToString()));
	OnMarkedPropertyChanged.Broadcast(TargetObject, PropertyName);
}

void UReplicTransportComponent::ReapplyAllPersistentState()
{
	AActor* HostActor = GetOwner();
	if (!HostActor)
	{
		return;
	}

	for (const FReplicStateEntry& Entry : ReplicatedStates.Items)
	{
		ApplySerializedPropertyToTargets(HostActor, Entry.Target, Entry.PropertyName, Entry.SerializedValue);
	}

	EmitReplicDebugMessage(
		EReplicDebugChannel::State,
		ELogVerbosity::Log,
		FString::Printf(TEXT("Replic re-applied all persistent state entries (%d) on '%s'."), ReplicatedStates.Items.Num(), *GetPathNameSafe(HostActor)),
		FColor::Yellow,
		TEXT("State Reapplied"));
}
