#include "ReplicCallNodeValidation.h"

#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "ReplicK2NodeUtils.h"
#include "ReplicLibrary.h"
#include "ReplicPinOptionResolver.h"

namespace
{
	UEdGraphPin* FindReplicNamePin(const UK2Node_CallFunction* CallNode, bool& bOutIsEventCall)
	{
		bOutIsEventCall = false;
		if (!CallNode)
		{
			return nullptr;
		}

		if (UEdGraphPin* PropertyPin = CallNode->FindPin(TEXT("PropertyName")))
		{
			return PropertyPin;
		}

		if (UEdGraphPin* EventPin = CallNode->FindPin(TEXT("EventName")))
		{
			bOutIsEventCall = true;
			return EventPin;
		}

		return nullptr;
	}

	UClass* GetReferenceClassForProperty(const FProperty* Property)
	{
		if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			return ClassProperty->MetaClass;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			return ObjectProperty->PropertyClass;
		}

		return nullptr;
	}

	bool IsReferenceSetterFunction(const UFunction* Function)
	{
		if (!Function || Function->GetOwnerClass() != UReplicLibrary::StaticClass())
		{
			return false;
		}

		const FName FunctionName = Function->GetFName();
		return FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedObject)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedClass);
	}

	bool IsPropertyValueFunction(const UFunction* Function)
	{
		if (!Function || Function->GetOwnerClass() != UReplicLibrary::StaticClass())
		{
			return false;
		}

		const bool bHasPropertyName = Function->FindPropertyByName(TEXT("PropertyName")) != nullptr;
		const bool bHasDynamicValuePin = Function->FindPropertyByName(TEXT("Value")) != nullptr
			|| Function->FindPropertyByName(TEXT("Item")) != nullptr
			|| Function->FindPropertyByName(TEXT("Key")) != nullptr;
		return bHasPropertyName && bHasDynamicValuePin;
	}

	const FProperty* ResolveExpectedPropertyForPin(const UFunction* Function, const FProperty* SelectedProperty, const FName PinName)
	{
		if (!Function || !SelectedProperty)
		{
			return nullptr;
		}

		const FName FunctionName = Function->GetFName();
		if (PinName == TEXT("Value"))
		{
			if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap))
			{
				if (const FMapProperty* MapProperty = CastField<FMapProperty>(SelectedProperty))
				{
					return MapProperty->GetValueProperty();
				}
			}

			// Raw Replic setters/getters all expose their selected property through the Value pin except for the map
			// delta node above, which binds Value to the map's value sub-property instead of the full reflected property.
			return SelectedProperty;
		}

		if (PinName == TEXT("Item"))
		{
			if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray))
			{
				if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(SelectedProperty))
				{
					return ArrayProperty->Inner;
				}
			}

			if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet))
			{
				if (const FSetProperty* SetProperty = CastField<FSetProperty>(SelectedProperty))
				{
					return SetProperty->GetElementProperty();
				}
			}
		}

		if (PinName == TEXT("Key"))
		{
			if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap))
			{
				if (const FMapProperty* MapProperty = CastField<FMapProperty>(SelectedProperty))
				{
					return MapProperty->GetKeyProperty();
				}
			}
		}

		return nullptr;
	}

	FString DescribeExpectedPinRole(const UFunction* Function, const FName PinName)
	{
		if (!Function)
		{
			return TEXT("property value");
		}

		const FName FunctionName = Function->GetFName();
		if (PinName == TEXT("Item"))
		{
			if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray))
			{
				return TEXT("array element");
			}

			if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet))
			{
				return TEXT("set element");
			}
		}

		if (PinName == TEXT("Key"))
		{
			return TEXT("map key");
		}

		if (PinName == TEXT("Value") && FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap))
		{
			return TEXT("map value");
		}

		return TEXT("property value");
	}
}

bool ReplicCallNodeValidation::IsReplicCallNode(const UK2Node_CallFunction* CallNode)
{
	if (!CallNode)
	{
		return false;
	}

	const UFunction* TargetFunction = CallNode->GetTargetFunction();
	return TargetFunction && TargetFunction->GetOwnerClass() == UReplicLibrary::StaticClass();
}

bool ReplicCallNodeValidation::ValidateReplicCallNode(const UK2Node_CallFunction* CallNode, FString& OutMessage, EMessageSeverity::Type& OutSeverity)
{
	OutMessage.Reset();
	OutSeverity = EMessageSeverity::Info;

	if (!IsReplicCallNode(CallNode))
	{
		return false;
	}

	bool bIsEventCall = false;
	UEdGraphPin* NamePin = FindReplicNamePin(CallNode, bIsEventCall);
	if (!NamePin)
	{
		return false;
	}

	const UFunction* TargetFunction = CallNode->GetTargetFunction();
	const bool bIsReferenceSetter = !bIsEventCall && IsReferenceSetterFunction(TargetFunction);
	if (bIsReferenceSetter)
	{
		// If the connected value pin already narrows the reference type, fail early when no property on the target can ever
		// accept that type. This catches bad combinations before the user even picks a property.
		UClass* TargetClass = nullptr;
		UClass* ValueClass = nullptr;
		if (ReplicPinOptionResolver::ResolveTargetClass(NamePin, TargetClass)
			&& ReplicPinOptionResolver::ResolveReferenceValueClass(NamePin, ValueClass)
			&& ValueClass
			&& ValueClass != UObject::StaticClass()
			&& ValueClass != UClass::StaticClass())
		{
			TArray<TSharedPtr<FReplicPinOptionItem>> FilteredOptions;
			ReplicPinOptionResolver::BuildOptions(NamePin, FilteredOptions);
			if (FilteredOptions.Num() == 0)
			{
				OutSeverity = EMessageSeverity::Error;
				OutMessage = FString::Printf(
					TEXT("No Replic-marked %s property on target class '%s' accepts value type '%s'."),
					TargetFunction && TargetFunction->GetFName() == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedClass) ? TEXT("class reference") : TEXT("object reference"),
					TargetClass ? *TargetClass->GetName() : TEXT("Unknown"),
					*ValueClass->GetName());
				return true;
			}
		}
	}

	const FName SelectedName(*NamePin->GetDefaultAsString());
	if (SelectedName.IsNone())
	{
		return false;
	}

	UClass* TargetClass = nullptr;
	if (!ReplicPinOptionResolver::ResolveTargetClass(NamePin, TargetClass))
	{
		OutSeverity = EMessageSeverity::Warning;
		OutMessage = TEXT("TargetObject could not be resolved to a concrete class in the editor. Replic cannot validate this selection yet.");
		return true;
	}

	TArray<TSharedPtr<FReplicPinOptionItem>> Options;
	ReplicPinOptionResolver::BuildOptions(NamePin, Options);

	const bool bExistsOnTarget = Options.ContainsByPredicate([SelectedName](const TSharedPtr<FReplicPinOptionItem>& Item)
	{
		return Item.IsValid() && Item->Value == SelectedName;
	});

	if (!bExistsOnTarget)
	{
		OutSeverity = EMessageSeverity::Error;
		OutMessage = FString::Printf(
			TEXT("'%s' is not a Replic-marked %s on target class '%s'."),
			*SelectedName.ToString(),
			bIsEventCall ? TEXT("event") : TEXT("property"),
			TargetClass ? *TargetClass->GetName() : TEXT("Unknown"));
		return true;
	}

	if (bIsReferenceSetter)
	{
		const FProperty* SelectedProperty = nullptr;
		UClass* ValueClass = nullptr;
		if (ReplicPinOptionResolver::ResolveMarkedProperty(NamePin, SelectedName, SelectedProperty)
			&& ReplicPinOptionResolver::ResolveReferenceValueClass(NamePin, ValueClass)
			&& ValueClass
			&& ValueClass != UObject::StaticClass()
			&& ValueClass != UClass::StaticClass())
		{
			if (UClass* PropertyClass = GetReferenceClassForProperty(SelectedProperty))
			{
				if (!ValueClass->IsChildOf(PropertyClass))
				{
					OutSeverity = EMessageSeverity::Error;
					OutMessage = FString::Printf(
						TEXT("Replic value type '%s' is not compatible with property '%s' of type '%s'."),
						*ValueClass->GetName(),
						*SelectedName.ToString(),
						*PropertyClass->GetName());
					return true;
				}
			}
		}
	}

	if (!bIsEventCall && IsPropertyValueFunction(TargetFunction))
	{
		// Raw property nodes are generic callfunction wrappers. Once PropertyName is fixed, the value pin must still be
		// validated against the actual reflected property type or stale wildcard connections can slip through compile.
		const FProperty* SelectedProperty = nullptr;
		if (ReplicPinOptionResolver::ResolveMarkedProperty(NamePin, SelectedName, SelectedProperty))
		{
			for (const FName PinName : { FName(TEXT("Item")), FName(TEXT("Key")), FName(TEXT("Value")) })
			{
				UEdGraphPin* ValuePin = CallNode->FindPin(PinName);
				const FProperty* ExpectedProperty = ResolveExpectedPropertyForPin(TargetFunction, SelectedProperty, PinName);
				if (!ValuePin || !ExpectedProperty)
				{
					continue;
				}

				if (!ReplicK2NodeUtils::ArePinAndPropertyCompatible(ValuePin, ExpectedProperty))
				{
					OutSeverity = EMessageSeverity::Error;
					const FString ExpectedRole = DescribeExpectedPinRole(TargetFunction, PinName);
					const FString ExpectedType = ExpectedProperty->GetCPPType();
					OutMessage = FString::Printf(
						TEXT("Replic pin '%s' is not compatible with property '%s'. Expected the %s type '%s'. Re-select the property or reconnect that pin."),
						*PinName.ToString(),
						*SelectedName.ToString(),
						*ExpectedRole,
						*ExpectedType);
					return true;
				}
			}
		}
	}

	return false;
}
