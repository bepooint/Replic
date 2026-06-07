#include "ReplicRuntimeUtils.h"

#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "ReplicMetadata.h"
#include "ReplicSettings.h"
#include "Replic.h"
#include "ReplicTransportComponent.h"
#include "Animation/AnimInstance.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FReplicObservedObject
	{
		TWeakObjectPtr<AActor> HostActor;
		FString TargetKey;
		TWeakObjectPtr<UObject> Object;
	};

	TArray<FReplicObservedObject> GObservedObjects;
	// Observed objects are non-authoritative runtime views such as widgets or anim instances that need persistent state
	// re-applied after they are created. They are keyed by the resolved Replic target, not just by raw UObject pointer.

	void CompactObservedObjects()
	{
		GObservedObjects.RemoveAll([](const FReplicObservedObject& Entry)
		{
			return !Entry.HostActor.IsValid() || !Entry.Object.IsValid();
		});
	}

	FString MakeObserverKey(const AActor* HostActor, const FReplicTargetDescriptor& Descriptor)
	{
		return FString::Printf(TEXT("%s|%s"), *GetPathNameSafe(HostActor), *Descriptor.ToKey());
	}

	FReplicVariableSettings ParseVariableSettings(const FProperty* Property)
	{
		FReplicVariableSettings Settings;
		if (!Property)
		{
			return Settings;
		}

#if WITH_METADATA
		Settings.bReplicateAll = Property->HasMetaData(ReplicMetadata::VariableEnabled);
		Settings.bPersistentState = !Property->HasMetaData(ReplicMetadata::VariablePersistent)
			|| Property->GetBoolMetaData(ReplicMetadata::VariablePersistent);
		Settings.bUseBatching = Property->HasMetaData(ReplicMetadata::VariableBatching)
			&& Property->GetBoolMetaData(ReplicMetadata::VariableBatching);
		Settings.BatchIntervalSeconds = Property->HasMetaData(ReplicMetadata::VariableBatchInterval)
			? Property->GetFloatMetaData(ReplicMetadata::VariableBatchInterval)
			: GetDefault<UReplicSettings>()->DefaultBatchIntervalSeconds;
		if (Property->HasMetaData(ReplicMetadata::VariablePermissionMode))
		{
			const FString PermissionValue = Property->GetMetaData(ReplicMetadata::VariablePermissionMode);
			if (PermissionValue == TEXT("OwnerOnly"))
			{
				Settings.PermissionMode = EReplicPermissionMode::OwnerOnly;
			}
			else if (PermissionValue == TEXT("ServerOnly"))
			{
				Settings.PermissionMode = EReplicPermissionMode::ServerOnly;
			}
			else if (PermissionValue == TEXT("Custom"))
			{
				Settings.PermissionMode = EReplicPermissionMode::Custom;
			}
		}
#endif
		return Settings;
	}

	FReplicEventSettings ParseEventSettings(const UFunction* Function)
	{
		FReplicEventSettings Settings;
		if (!Function)
		{
			return Settings;
		}

#if WITH_METADATA
		Settings.bReplicateAll = Function->HasMetaData(ReplicMetadata::EventEnabled);
		if (Function->HasMetaData(ReplicMetadata::EventPermissionMode))
		{
			const FString PermissionValue = Function->GetMetaData(ReplicMetadata::EventPermissionMode);
			if (PermissionValue == TEXT("OwnerOnly"))
			{
				Settings.PermissionMode = EReplicPermissionMode::OwnerOnly;
			}
			else if (PermissionValue == TEXT("ServerOnly"))
			{
				Settings.PermissionMode = EReplicPermissionMode::ServerOnly;
			}
			else if (PermissionValue == TEXT("Custom"))
			{
				Settings.PermissionMode = EReplicPermissionMode::Custom;
			}
		}
		if (Function->HasMetaData(ReplicMetadata::EventMode))
		{
			const FString ModeValue = Function->GetMetaData(ReplicMetadata::EventMode);
			if (ModeValue == TEXT("LocalOnly"))
			{
				Settings.Mode = EReplicEventMode::LocalOnly;
			}
			else if (ModeValue == TEXT("ServerOnly"))
			{
				Settings.Mode = EReplicEventMode::ServerOnly;
			}
			else if (ModeValue == TEXT("OwnerOnly"))
			{
				Settings.Mode = EReplicEventMode::OwnerOnly;
			}
			else
			{
				Settings.Mode = EReplicEventMode::ReplicateAll;
			}
		}
#endif

		return Settings;
	}
}

bool ReplicRuntimeUtils::ResolveTarget(UObject* Object, FReplicResolvedTarget& OutTarget)
{
	if (!Object)
	{
		return false;
	}

	if (AActor* Actor = Cast<AActor>(Object))
	{
		OutTarget.HostActor = Actor;
		OutTarget.Descriptor.Kind = EReplicTargetKind::Actor;
		return true;
	}

	if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		OutTarget.HostActor = Component->GetOwner();
		OutTarget.Descriptor.Kind = EReplicTargetKind::ActorComponent;
		OutTarget.Descriptor.ObjectName = Component->GetFName();
		return OutTarget.HostActor != nullptr;
	}

	if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(Object))
	{
		OutTarget.HostActor = AnimInstance->GetOwningActor();
		OutTarget.Descriptor.Kind = EReplicTargetKind::AnimInstance;
		OutTarget.Descriptor.ClassPath = AnimInstance->GetClass();
		if (UActorComponent* OwningComponent = AnimInstance->GetOwningComponent())
		{
			OutTarget.Descriptor.ObjectName = OwningComponent->GetFName();
		}
		return OutTarget.HostActor != nullptr;
	}

	if (UUserWidget* Widget = Cast<UUserWidget>(Object))
	{
		// Widgets do not replicate by themselves. Replic maps them onto a replicated gameplay owner so they can observe
		// already replicated state through PlayerState, Pawn, or as a fallback the owning PlayerController.
		AActor* HostActor = nullptr;
		if (APlayerController* OwningPlayer = Widget->GetOwningPlayer())
		{
			if (APlayerState* PlayerState = OwningPlayer->PlayerState)
			{
				HostActor = PlayerState;
			}
			else if (APawn* Pawn = OwningPlayer->GetPawn())
			{
				HostActor = Pawn;
			}
			else
			{
				HostActor = OwningPlayer;
			}
		}

		OutTarget.HostActor = HostActor;
		OutTarget.Descriptor.Kind = EReplicTargetKind::Widget;
		OutTarget.Descriptor.ClassPath = Widget->GetClass();
		return HostActor != nullptr;
	}

	return false;
}

void ReplicRuntimeUtils::RegisterObservedObject(UObject* Object)
{
	FReplicResolvedTarget ResolvedTarget;
	if (!ResolveTarget(Object, ResolvedTarget) || !ResolvedTarget.HostActor)
	{
		return;
	}

	CompactObservedObjects();
	const FString TargetKey = MakeObserverKey(ResolvedTarget.HostActor, ResolvedTarget.Descriptor);
	const bool bAlreadyRegistered = GObservedObjects.ContainsByPredicate(
		[&Object, &TargetKey](const FReplicObservedObject& Entry)
		{
			return Entry.Object.Get() == Object && Entry.TargetKey == TargetKey;
		});

	if (!bAlreadyRegistered)
	{
		GObservedObjects.Add({ResolvedTarget.HostActor, TargetKey, Object});
	}
}

void ReplicRuntimeUtils::UnregisterObservedObject(UObject* Object)
{
	GObservedObjects.RemoveAll([Object](const FReplicObservedObject& Entry)
	{
		return Entry.Object.Get() == Object;
	});
}

void ReplicRuntimeUtils::ResolveLocalObjects(AActor* HostActor, const FReplicTargetDescriptor& Descriptor, TArray<TWeakObjectPtr<UObject>>& OutObjects)
{
	if (!HostActor)
	{
		return;
	}

	if (Descriptor.Kind == EReplicTargetKind::Actor)
	{
		OutObjects.Add(HostActor);
		return;
	}

	if (Descriptor.Kind == EReplicTargetKind::ActorComponent)
	{
		TInlineComponentArray<UActorComponent*> Components(HostActor);
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetFName() == Descriptor.ObjectName)
			{
				OutObjects.Add(Component);
			}
		}
		return;
	}

	if (Descriptor.Kind == EReplicTargetKind::AnimInstance && Descriptor.ObjectName != NAME_None)
	{
		TInlineComponentArray<UActorComponent*> Components(HostActor);
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetFName() == Descriptor.ObjectName)
			{
				if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Component))
				{
					if (UAnimInstance* AnimInstance = SkelMesh->GetAnimInstance())
					{
						OutObjects.Add(AnimInstance);
					}
				}
			}
		}
	}

	CompactObservedObjects();
	const FString TargetKey = MakeObserverKey(HostActor, Descriptor);
	for (const FReplicObservedObject& Entry : GObservedObjects)
	{
		if (Entry.TargetKey == TargetKey)
		{
			OutObjects.Add(Entry.Object);
		}
	}
}

FProperty* ReplicRuntimeUtils::FindPropertyByName(UObject* Object, FName PropertyName)
{
	return Object ? FindFProperty<FProperty>(Object->GetClass(), PropertyName) : nullptr;
}

UFunction* ReplicRuntimeUtils::FindFunctionByName(UObject* Object, FName FunctionName)
{
	return Object ? Object->FindFunction(FunctionName) : nullptr;
}

bool ReplicRuntimeUtils::IsMarkedVariable(const FProperty* Property)
{
	return ParseVariableSettings(Property).bReplicateAll;
}

bool ReplicRuntimeUtils::IsMarkedEvent(const UFunction* Function)
{
	return ParseEventSettings(Function).bReplicateAll;
}

bool ReplicRuntimeUtils::IsMarkedVariable(UObject* TargetObject, FName PropertyName)
{
	FReplicVariableSettings Settings;
	return TryGetVariableSettings(TargetObject, PropertyName, Settings);
}

bool ReplicRuntimeUtils::IsMarkedEvent(UObject* TargetObject, FName EventName)
{
	FReplicEventSettings Settings;
	return TryGetEventSettings(TargetObject, EventName, Settings);
}

FReplicVariableSettings ReplicRuntimeUtils::GetVariableSettings(const FProperty* Property)
{
	return ParseVariableSettings(Property);
}

FReplicEventSettings ReplicRuntimeUtils::GetEventSettings(const UFunction* Function)
{
	return ParseEventSettings(Function);
}

bool ReplicRuntimeUtils::TryGetVariableSettings(UObject* TargetObject, FName PropertyName, FReplicVariableSettings& OutSettings)
{
	OutSettings = FReplicVariableSettings();
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	const FProperty* Property = FindPropertyByName(TargetObject, PropertyName);
	const FReplicVariableSettings MetadataSettings = GetVariableSettings(Property);
	if (MetadataSettings.bReplicateAll)
	{
		OutSettings = MetadataSettings;
		return true;
	}

	FReplicResolvedTarget ResolvedTarget;
	if (!ResolveTarget(TargetObject, ResolvedTarget) || !ResolvedTarget.HostActor)
	{
		return false;
	}

	if (const UReplicTransportComponent* Transport = UReplicTransportComponent::FindOnActor(ResolvedTarget.HostActor))
	{
		return Transport->FindVariableDefinition(ResolvedTarget.Descriptor, PropertyName, OutSettings);
	}

	return false;
}

bool ReplicRuntimeUtils::TryGetEventSettings(UObject* TargetObject, FName EventName, FReplicEventSettings& OutSettings)
{
	OutSettings = FReplicEventSettings();
	if (!TargetObject || EventName.IsNone())
	{
		return false;
	}

	const UFunction* Function = FindFunctionByName(TargetObject, EventName);
	const FReplicEventSettings MetadataSettings = GetEventSettings(Function);
	if (MetadataSettings.bReplicateAll)
	{
		OutSettings = MetadataSettings;
		return true;
	}

	FReplicResolvedTarget ResolvedTarget;
	if (!ResolveTarget(TargetObject, ResolvedTarget) || !ResolvedTarget.HostActor)
	{
		return false;
	}

	if (const UReplicTransportComponent* Transport = UReplicTransportComponent::FindOnActor(ResolvedTarget.HostActor))
	{
		return Transport->FindEventDefinition(ResolvedTarget.Descriptor, EventName, OutSettings);
	}

	return false;
}

bool ReplicRuntimeUtils::ExportPropertyValueToText(const FProperty* Property, const void* ValuePtr, FString& OutText)
{
	if (!Property || !ValuePtr)
	{
		return false;
	}

	Property->ExportText_Direct(OutText, ValuePtr, ValuePtr, nullptr, PPF_None);
	return true;
}

bool ReplicRuntimeUtils::ImportPropertyValueFromText(const FProperty* Property, void* ValuePtr, const FString& InText)
{
	return Property && ValuePtr && Property->ImportText_Direct(*InText, ValuePtr, nullptr, PPF_None, GLog) != nullptr;
}

bool ReplicRuntimeUtils::ExportObjectPropertyToText(UObject* Object, const FProperty* Property, FString& OutText)
{
	if (!Object || !Property)
	{
		return false;
	}

	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	return ExportPropertyValueToText(Property, ValuePtr, OutText);
}

bool ReplicRuntimeUtils::ImportObjectPropertyFromText(UObject* Object, FProperty* Property, const FString& InText)
{
	if (!Object || !Property)
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	return ImportPropertyValueFromText(Property, ValuePtr, InText);
}

bool ReplicRuntimeUtils::CopyValueBetweenPropertiesByText(const FProperty* SourceProperty, const void* SourceValue, const FProperty* DestinationProperty, void* DestinationValue)
{
	FString SerializedText;
	if (!ExportPropertyValueToText(SourceProperty, SourceValue, SerializedText))
	{
		return false;
	}

	return ImportPropertyValueFromText(DestinationProperty, DestinationValue, SerializedText);
}

bool ReplicRuntimeUtils::InvokeFunctionBySerializedArguments(UObject* TargetObject, UFunction* Function, const TArray<FReplicNamedValue>& Arguments)
{
	if (!TargetObject || !Function)
	{
		return false;
	}

	TArray<uint8> ParameterBuffer;
	ParameterBuffer.SetNumZeroed(Function->ParmsSize);

	for (TFieldIterator<FProperty> PropertyIt(Function); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
	{
		FProperty* FunctionProperty = *PropertyIt;
		FunctionProperty->InitializeValue_InContainer(ParameterBuffer.GetData());
	}

	for (const FReplicNamedValue& Argument : Arguments)
	{
		if (FProperty* FunctionProperty = FindFProperty<FProperty>(Function, Argument.Name))
		{
			void* ValuePtr = FunctionProperty->ContainerPtrToValuePtr<void>(ParameterBuffer.GetData());
			ImportPropertyValueFromText(FunctionProperty, ValuePtr, Argument.SerializedValue);
		}
	}

	TargetObject->ProcessEvent(Function, ParameterBuffer.GetData());

	for (TFieldIterator<FProperty> PropertyIt(Function); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
	{
		FProperty* FunctionProperty = *PropertyIt;
		FunctionProperty->DestroyValue_InContainer(ParameterBuffer.GetData());
	}

	return true;
}

bool ReplicRuntimeUtils::IsRequesterOwnedByCallingConnection(const UActorComponent* RequestComponent)
{
	if (!RequestComponent)
	{
		return false;
	}

	const AActor* OwnerActor = RequestComponent->GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	return OwnerActor->GetNetConnection() != nullptr || OwnerActor->HasAuthority();
}
