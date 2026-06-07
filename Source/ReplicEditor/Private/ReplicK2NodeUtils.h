#pragma once

#include "CoreMinimal.h"
#include "K2Node_EditablePinBase.h"

class FProperty;
class UBlueprint;
class UEdGraphPin;
class UFunction;
class USimpleConstructionScript;
class UK2Node_CallFunction;
class UK2Node_CustomEvent;

namespace ReplicK2NodeUtils
{
	bool ConvertPropertyToPinType(const FProperty* Property, FEdGraphPinType& OutPinType);
	bool ArePinAndPropertyCompatible(const UEdGraphPin* Pin, const FProperty* Property);
	bool HasReplicTransportComponent(const UBlueprint* Blueprint);
	void ApplyReplicLibraryPinToolTips(UK2Node_CallFunction* CallNode);
	void SetPinToolTip(UEdGraphPin* Pin, const FString& ToolTip);
	void GatherEventInputProperties(const UFunction* Function, TArray<const FProperty*>& OutProperties);
	void GatherCustomEventParameterDefinitions(const UK2Node_CustomEvent* EventNode, TArray<FUserPinInfo>& OutPins);
}
