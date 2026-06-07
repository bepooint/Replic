#pragma once

#include "CoreMinimal.h"
#include "ReplicTypes.h"

class UFunction;
class UActorComponent;
class UUserWidget;

struct FReplicResolvedTarget
{
	AActor* HostActor = nullptr;
	FReplicTargetDescriptor Descriptor;
};

namespace ReplicRuntimeUtils
{
	REPLIC_API bool ResolveTarget(UObject* Object, FReplicResolvedTarget& OutTarget);
	REPLIC_API void RegisterObservedObject(UObject* Object);
	REPLIC_API void UnregisterObservedObject(UObject* Object);
	REPLIC_API void ResolveLocalObjects(AActor* HostActor, const FReplicTargetDescriptor& Descriptor, TArray<TWeakObjectPtr<UObject>>& OutObjects);
	REPLIC_API FProperty* FindPropertyByName(UObject* Object, FName PropertyName);
	REPLIC_API UFunction* FindFunctionByName(UObject* Object, FName FunctionName);
	REPLIC_API bool IsMarkedVariable(const FProperty* Property);
	REPLIC_API bool IsMarkedEvent(const UFunction* Function);
	REPLIC_API bool IsMarkedVariable(UObject* TargetObject, FName PropertyName);
	REPLIC_API bool IsMarkedEvent(UObject* TargetObject, FName EventName);
	REPLIC_API FReplicVariableSettings GetVariableSettings(const FProperty* Property);
	REPLIC_API FReplicEventSettings GetEventSettings(const UFunction* Function);
	REPLIC_API bool TryGetVariableSettings(UObject* TargetObject, FName PropertyName, FReplicVariableSettings& OutSettings);
	REPLIC_API bool TryGetEventSettings(UObject* TargetObject, FName EventName, FReplicEventSettings& OutSettings);
	REPLIC_API bool ExportPropertyValueToText(const FProperty* Property, const void* ValuePtr, FString& OutText);
	REPLIC_API bool ImportPropertyValueFromText(const FProperty* Property, void* ValuePtr, const FString& InText);
	REPLIC_API bool ExportObjectPropertyToText(UObject* Object, const FProperty* Property, FString& OutText);
	REPLIC_API bool ImportObjectPropertyFromText(UObject* Object, FProperty* Property, const FString& InText);
	REPLIC_API bool CopyValueBetweenPropertiesByText(const FProperty* SourceProperty, const void* SourceValue, const FProperty* DestinationProperty, void* DestinationValue);
	REPLIC_API bool InvokeFunctionBySerializedArguments(UObject* TargetObject, UFunction* Function, const TArray<FReplicNamedValue>& Arguments);
	REPLIC_API bool IsRequesterOwnedByCallingConnection(const UActorComponent* RequestComponent);
}
