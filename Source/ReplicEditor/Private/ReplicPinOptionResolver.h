#pragma once

#include "CoreMinimal.h"

class UEdGraphPin;
class UFunction;
class UClass;
class FProperty;
class UK2Node_CustomEvent;

enum class EReplicPinOptionKind : uint8
{
	None = 0,
	AnyProperty,
	BoolProperty,
	IntProperty,
	FloatProperty,
	ByteProperty,
	EnumProperty,
	NameProperty,
	StringProperty,
	TextProperty,
	VectorProperty,
	RotatorProperty,
	TransformProperty,
	ObjectProperty,
	ClassProperty,
	StructProperty,
	ArrayProperty,
	SetProperty,
	MapProperty,
	Event
};

struct FReplicPinOptionItem
{
	FName Value = NAME_None;
	FText DisplayText;
	FText TooltipText;
	FString SortKey;
};

namespace ReplicPinOptionResolver
{
	bool IsSupportedReplicNamePin(const UEdGraphPin* Pin, EReplicPinOptionKind& OutKind);
	bool ResolveTargetClass(const UEdGraphPin* Pin, UClass*& OutTargetClass);
	bool ResolveReferenceValueClass(const UEdGraphPin* Pin, UClass*& OutValueClass);
	bool ResolveMarkedProperty(const UEdGraphPin* Pin, FName PropertyName, const FProperty*& OutProperty);
	bool ResolveMarkedEvent(const UEdGraphPin* Pin, FName EventName, UFunction*& OutFunction);
	bool ResolveMarkedEventNode(const UEdGraphPin* Pin, FName EventName, UK2Node_CustomEvent*& OutEventNode);
	void BuildOptions(const UEdGraphPin* Pin, TArray<TSharedPtr<FReplicPinOptionItem>>& OutOptions);
}
