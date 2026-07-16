#include "ReplicK2NodeUtils.h"

#include "Animation/AnimInstance.h"
#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "ReplicLibrary.h"
#include "ReplicTransportComponent.h"

bool ReplicK2NodeUtils::ConvertPropertyToPinType(const FProperty* Property, FEdGraphPinType& OutPinType)
{
	if (!Property)
	{
		return false;
	}

	return GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, OutPinType);
}

bool ReplicK2NodeUtils::ArePinAndPropertyCompatible(const UEdGraphPin* Pin, const FProperty* Property)
{
	if (!Pin || !Property)
	{
		return false;
	}

	FEdGraphPinType PropertyPinType;
	if (!ConvertPropertyToPinType(Property, PropertyPinType))
	{
		return false;
	}

	// Replic setter/getter pins often carry function-level qualifiers like const/reference that do not
	// change the actual value shape the user is allowed to connect. Validation should compare the reflected
	// value/container type, not reject an otherwise matching pin because the function signature passes it by ref.
	PropertyPinType.bIsReference = Pin->PinType.bIsReference;
	PropertyPinType.bIsConst = Pin->PinType.bIsConst;
	PropertyPinType.bIsWeakPointer = Pin->PinType.bIsWeakPointer;
	PropertyPinType.bIsUObjectWrapper = Pin->PinType.bIsUObjectWrapper;
	PropertyPinType.bSerializeAsSinglePrecisionFloat = Pin->PinType.bSerializeAsSinglePrecisionFloat;

	return GetDefault<UEdGraphSchema_K2>()->ArePinTypesCompatible(Pin->PinType, PropertyPinType);
}

bool ReplicK2NodeUtils::HasReplicTransportComponent(const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return true;
	}

	const UClass* ActorClass = nullptr;
	if (Blueprint->SkeletonGeneratedClass && Blueprint->SkeletonGeneratedClass->IsChildOf<AActor>())
	{
		ActorClass = Blueprint->SkeletonGeneratedClass;
	}
	else if (Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf<AActor>())
	{
		ActorClass = Blueprint->ParentClass;
	}
	else if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf<AActor>())
	{
		ActorClass = Blueprint->GeneratedClass;
	}

	if (!ActorClass)
	{
		return true;
	}

	if (const USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		for (const USCS_Node* SCSNode : SCS->GetAllNodes())
		{
			if (SCSNode && SCSNode->ComponentClass && SCSNode->ComponentClass->IsChildOf<UReplicTransportComponent>())
			{
				return true;
			}
		}
	}

	// Only inspect the stable parent/native CDO here. Accessing the generated Blueprint CDO while the Blueprint
	// is recompiling can hit transient or trash instances and cause editor ensures.
	if (const UClass* StableActorClass = (Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf<AActor>()) ? Blueprint->ParentClass : nullptr)
	{
		if (const AActor* ActorCDO = Cast<AActor>(StableActorClass->GetDefaultObject(false)))
		{
			if (ActorCDO->FindComponentByClass<UReplicTransportComponent>() != nullptr)
			{
				return true;
			}
		}
	}

	return false;
}

namespace
{
	bool IsSelfContextPin(const UEdGraphPin* ContextPin)
	{
		return ContextPin && ContextPin->LinkedTo.Num() == 0 && ContextPin->DefaultObject == nullptr && ContextPin->DefaultValue.IsEmpty();
	}

	UClass* GetPinObjectClass(const FEdGraphPinType& PinType)
	{
		if (UClass* PinClass = Cast<UClass>(PinType.PinSubCategoryObject.Get()))
		{
			return PinClass;
		}

		return nullptr;
	}

	bool IsSupportedReplicContextClass(const UClass* ContextClass)
	{
		return ContextClass
			&& (ContextClass->IsChildOf<AActor>()
				|| ContextClass->IsChildOf<UActorComponent>()
				|| ContextClass->IsChildOf<UAnimInstance>()
				|| ContextClass->IsChildOf<UUserWidget>());
	}
}

bool ReplicK2NodeUtils::BuildContextObjectWarning(const UBlueprint* Blueprint, const UEdGraphPin* ContextPin, FString& OutWarning)
{
	OutWarning.Reset();

	if (!ContextPin)
	{
		return false;
	}

	if (IsSelfContextPin(ContextPin))
	{
		if (!HasReplicTransportComponent(Blueprint))
		{
			OutWarning = TEXT("ContextObject uses Self, but this Blueprint has no ReplicTransportComponent. Add one to the actor that owns this graph, or connect a ContextObject that can access one. Client-side Replic requests from Self may fail.");
			return true;
		}

		return false;
	}

	if (ContextPin->LinkedTo.Num() == 0)
	{
		return false;
	}

	for (const UEdGraphPin* LinkedPin : ContextPin->LinkedTo)
	{
		if (!LinkedPin)
		{
			continue;
		}

		UClass* LinkedClass = GetPinObjectClass(LinkedPin->PinType);
		if (!LinkedClass || LinkedClass == UObject::StaticClass())
		{
			OutWarning = TEXT("ContextObject is connected, but the editor can only see a generic UObject type. Replic can only resolve Actors, ActorComponents, AnimInstances, and UserWidgets at runtime. If this request fails, connect Self from a Character/Pawn/Controller/Actor/Component or a widget with an owning player.");
			return true;
		}

		if (!IsSupportedReplicContextClass(LinkedClass))
		{
			OutWarning = FString::Printf(
				TEXT("ContextObject type '%s' is not a supported Replic runtime context. Connect an Actor, ActorComponent, AnimInstance, or UserWidget that can resolve to a replicated actor."),
				*LinkedClass->GetName());
			return true;
		}
	}

	return false;
}
void ReplicK2NodeUtils::SetPinToolTip(UEdGraphPin* Pin, const FString& ToolTip)
{
	if (Pin)
	{
		Pin->PinToolTip = ToolTip;
	}
}

void ReplicK2NodeUtils::ApplyReplicLibraryPinToolTips(UK2Node_CallFunction* CallNode)
{
	if (!CallNode)
	{
		return;
	}

	const UFunction* TargetFunction = CallNode->GetTargetFunction();
	if (!TargetFunction || TargetFunction->GetOwnerClass() != UReplicLibrary::StaticClass())
	{
		return;
	}

	const FName FunctionName = TargetFunction->GetFName();

	if (UEdGraphPin* ContextPin = CallNode->FindPin(TEXT("ContextObject")))
	{
		SetPinToolTip(ContextPin, TEXT("Object used to identify who is making the network request.\n\nUsually this should be Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent."));
	}

	if (UEdGraphPin* TargetPin = CallNode->FindPin(TEXT("TargetObject")))
	{
		const bool bIsEventCall = FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, CallMarkedEvent);
		SetPinToolTip(TargetPin, bIsEventCall
			? TEXT("Object that owns the Replic-marked custom event.\n\nLeave this unconnected only when the event is on Self or in the current Blueprint.\n\nIf the event belongs to another actor or component, connect that object here first.")
			: TEXT("Object that owns the Replic-marked property.\n\nLeave this unconnected only when the property is on Self or in the current Blueprint.\n\nIf the property belongs to another actor or component, connect that object here first."));
	}

	if (UEdGraphPin* PropertyPin = CallNode->FindPin(TEXT("PropertyName")))
	{
		SetPinToolTip(PropertyPin, TEXT("Choose the Replic-marked property to use.\n\nThe list is filtered by the connected TargetObject when the editor can resolve it.\n\nChoose None to clear the current selection and reset the dynamic Replic pins."));
	}

	if (UEdGraphPin* EventPin = CallNode->FindPin(TEXT("EventName")))
	{
		SetPinToolTip(EventPin, TEXT("Choose the Replic-enabled custom event to call.\n\nThe list is filtered by the connected TargetObject when the editor can resolve it.\n\nChoose None to clear the current selection and reset generated argument pins."));
	}

	if (UEdGraphPin* ValuePin = CallNode->FindPin(TEXT("Value")))
	{
		FString ValueToolTip = TEXT("Value used by the selected Replic property.");
		if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedStruct)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedObject)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedClass)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedBool)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedInt)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedFloat)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedByte)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedEnum)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedName)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedString)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedText)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedVector)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedRotator)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedTransform))
		{
			ValueToolTip = TEXT("New value written to the selected Replic property.\n\nThis pin must match the selected property's value type.");
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedStruct)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedMap)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedObject)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedClass))
		{
			ValueToolTip = TEXT("Local value read from the selected Replic property.\n\nThis pin follows the selected property's type.");
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap))
		{
			ValueToolTip = TEXT("New map value stored for the selected Key.\n\nThis pin must match the map value type of the selected Replic property.");
		}

		SetPinToolTip(ValuePin, ValueToolTip);
	}

	if (UEdGraphPin* NamePin = CallNode->FindPin(TEXT("Name")))
	{
		SetPinToolTip(NamePin, TEXT("Event argument name.\n\nThis must exactly match the parameter name on the Replic-enabled custom event."));
	}

	if (UEdGraphPin* ArgumentsPin = CallNode->FindPin(TEXT("Arguments")))
	{
		SetPinToolTip(ArgumentsPin, TEXT("Named event arguments passed to the selected Replic event.\n\nThe typed Replic Call Event node builds this list automatically. Use this pin manually only for advanced Call Marked Event workflows."));
	}

	if (UEdGraphPin* ItemPin = CallNode->FindPin(TEXT("Item")))
	{
		FString ItemToolTip = TEXT("Container element used by the selected Replic property.");
		if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray))
		{
			ItemToolTip = TEXT("Array element to add.\n\nThis pin must match the element type of the selected Replic array property.");
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray))
		{
			ItemToolTip = TEXT("Array element to remove.\n\nReplic removes matching entries from the selected array property.");
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet))
		{
			ItemToolTip = TEXT("Set element to add.\n\nThis pin must match the element type of the selected Replic set property.");
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet))
		{
			ItemToolTip = TEXT("Set element to remove.\n\nReplic removes the matching entry from the selected set property.");
		}

		SetPinToolTip(ItemPin, ItemToolTip);
	}

	if (UEdGraphPin* KeyPin = CallNode->FindPin(TEXT("Key")))
	{
		FString KeyToolTip = TEXT("Map key used by the selected Replic property.");
		if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap))
		{
			KeyToolTip = TEXT("Map key to add or update.\n\nThis pin must match the key type of the selected Replic map property.");
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap))
		{
			KeyToolTip = TEXT("Map key to remove.\n\nReplic removes the matching entry from the selected map property.");
		}

		SetPinToolTip(KeyPin, KeyToolTip);
	}
}

void ReplicK2NodeUtils::GatherEventInputProperties(const UFunction* Function, TArray<const FProperty*>& OutProperties)
{
	OutProperties.Reset();

	if (!Function)
	{
		return;
	}

	for (TFieldIterator<FProperty> PropertyIt(Function); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		if (!Property || Property->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
		{
			continue;
		}

		OutProperties.Add(Property);
	}
}

void ReplicK2NodeUtils::GatherCustomEventParameterDefinitions(const UK2Node_CustomEvent* EventNode, TArray<FUserPinInfo>& OutPins)
{
	OutPins.Reset();

	if (!EventNode)
	{
		return;
	}

	for (const TSharedPtr<FUserPinInfo>& UserPinInfo : EventNode->UserDefinedPins)
	{
		if (!UserPinInfo.IsValid() || UserPinInfo->DesiredPinDirection != EGPD_Output)
		{
			continue;
		}

		if (UserPinInfo->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			continue;
		}

		if (UserPinInfo->PinName == NAME_None)
		{
			continue;
		}

		OutPins.Add(*UserPinInfo);
	}
}
