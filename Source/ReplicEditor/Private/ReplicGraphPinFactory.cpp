#include "ReplicGraphPinFactory.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ReplicSetEnum.h"
#include "ReplicLibrary.h"
#include "ReplicPinOptionResolver.h"
#include "SReplicArrayValuePin.h"
#include "SReplicEnumValuePin.h"
#include "SReplicMapValuePin.h"
#include "SReplicOptionPin.h"
#include "SReplicSetValuePin.h"

TSharedPtr<SGraphPin> FReplicGraphPinFactory::CreatePin(UEdGraphPin* InPin) const
{
	EReplicPinOptionKind Kind = EReplicPinOptionKind::None;
	if (ReplicPinOptionResolver::IsSupportedReplicNamePin(InPin, Kind))
	{
		return SNew(SReplicOptionPin, InPin);
	}

	if (InPin
		&& InPin->Direction == EGPD_Input
		&& InPin->PinName == TEXT("Value")
		&& InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte
		&& InPin->PinType.PinSubCategoryObject.IsValid())
	{
		if (Cast<UK2Node_ReplicSetEnum>(InPin->GetOwningNode()))
		{
			return SNew(SReplicEnumValuePin, InPin);
		}

		if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(InPin->GetOwningNode()))
		{
			if (const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction())
			{
				if (TargetFunction->GetOwnerClass() == UReplicLibrary::StaticClass()
					&& TargetFunction->GetFName() == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedEnum))
				{
					return SNew(SReplicEnumValuePin, InPin);
				}
			}
		}
	}

	if (InPin
		&& InPin->PinName == TEXT("Value"))
	{
		if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(InPin->GetOwningNode()))
		{
			if (const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction())
			{
				if (TargetFunction->GetOwnerClass() == UReplicLibrary::StaticClass())
				{
					const FName FunctionName = TargetFunction->GetFName();
					if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray)
						|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedArray))
					{
						return SNew(SReplicArrayValuePin, InPin);
					}

					if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet)
						|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedSet))
					{
						return SNew(SReplicSetValuePin, InPin);
					}

					if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap)
						|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedMap))
					{
						return SNew(SReplicMapValuePin, InPin);
					}
				}
			}
		}
	}

	return nullptr;
}
