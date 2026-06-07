#include "SReplicOptionPin.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ReplicGetArray.h"
#include "K2Node_ReplicCallEvent.h"
#include "K2Node_ReplicSetArray.h"
#include "K2Node_ReplicGetEnum.h"
#include "K2Node_ReplicSetEnum.h"
#include "ReplicK2NodeUtils.h"
#include "ReplicLibrary.h"
#include "ReplicPinOptionResolver.h"
#include "ScopedTransaction.h"
#include "Containers/Ticker.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	bool TryBuildNeutralPinTypeForFunctionAndPin(const UFunction* TargetFunction, const FName PinName, FEdGraphPinType& OutPinType)
	{
		if (!TargetFunction || TargetFunction->GetOwnerClass() != UReplicLibrary::StaticClass())
		{
			return false;
		}

		const FName FunctionName = TargetFunction->GetFName();
		if (PinName == TEXT("Value")
			&& (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedStruct)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedStruct)))
		{
			OutPinType = FEdGraphPinType();
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			return true;
		}

		if (PinName == TEXT("Value")
			&& (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedArray)))
		{
			OutPinType = FEdGraphPinType();
			OutPinType.ContainerType = EPinContainerType::Array;
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			return true;
		}

		if (PinName == TEXT("Value")
			&& (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedSet)))
		{
			OutPinType = FEdGraphPinType();
			OutPinType.ContainerType = EPinContainerType::Set;
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			return true;
		}

		if (PinName == TEXT("Value")
			&& (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap)
				|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedMap)))
		{
			OutPinType = FEdGraphPinType();
			OutPinType.ContainerType = EPinContainerType::Map;
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			OutPinType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Wildcard;
			return true;
		}

		if ((PinName == TEXT("Item")
				&& (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)
					|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray)
					|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)
					|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet)))
			|| (PinName == TEXT("Key")
				&& (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap)
					|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap)))
			|| (PinName == TEXT("Value")
				&& FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap)))
		{
			OutPinType = FEdGraphPinType();
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			return true;
		}

		return false;
	}

	UEnum* ResolveEnumForSelectedPropertyPin(const UEdGraphPin* PropertyNamePin)
	{
		if (!PropertyNamePin)
		{
			return nullptr;
		}

		const FName SelectedPropertyName(*PropertyNamePin->GetDefaultAsString());
		const FProperty* Property = nullptr;
		if (!ReplicPinOptionResolver::ResolveMarkedProperty(PropertyNamePin, SelectedPropertyName, Property) || !Property)
		{
			return nullptr;
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum();
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			return ByteProperty->Enum;
		}

		return nullptr;
	}

	bool ResolveSelectedPropertyPinTypeForFunctionPin(const UEdGraphPin* PropertyNamePin, const UFunction* TargetFunction, const FName PinName, FEdGraphPinType& OutPinType)
	{
		if (!PropertyNamePin || !TargetFunction)
		{
			return false;
		}

		const FName SelectedPropertyName(*(PropertyNamePin ? PropertyNamePin->GetDefaultAsString() : FString()));
		const FProperty* Property = nullptr;
		if (!ReplicPinOptionResolver::ResolveMarkedProperty(PropertyNamePin, SelectedPropertyName, Property) || !Property)
		{
			return false;
		}

		const FName FunctionName = TargetFunction->GetFName();
		const FProperty* PinProperty = Property;
		if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray))
		{
			const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
			PinProperty = (PinName == TEXT("Item") && ArrayProperty) ? ArrayProperty->Inner : nullptr;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet))
		{
			const FSetProperty* SetProperty = CastField<FSetProperty>(Property);
			PinProperty = (PinName == TEXT("Item") && SetProperty) ? SetProperty->GetElementProperty() : nullptr;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap))
		{
			const FMapProperty* MapProperty = CastField<FMapProperty>(Property);
			if (!MapProperty)
			{
				PinProperty = nullptr;
			}
			else if (PinName == TEXT("Key"))
			{
				PinProperty = MapProperty->GetKeyProperty();
			}
			else if (PinName == TEXT("Value"))
			{
				PinProperty = MapProperty->GetValueProperty();
			}
			else
			{
				PinProperty = nullptr;
			}
		}

		return PinProperty && ReplicK2NodeUtils::ConvertPropertyToPinType(PinProperty, OutPinType);
	}

	bool ResolveDefaultPinTypeFromFunctionAndPin(const UFunction* TargetFunction, const FName PinName, FEdGraphPinType& OutPinType)
	{
		if (!TargetFunction)
		{
			return false;
		}

		if (const FProperty* ValueProperty = FindFProperty<FProperty>(TargetFunction, PinName))
		{
			return ReplicK2NodeUtils::ConvertPropertyToPinType(ValueProperty, OutPinType);
		}

		return false;
	}

	void RefreshDynamicPinsOnCallFunction(UK2Node_CallFunction* CallFunctionNode)
	{
		if (!CallFunctionNode)
		{
			return;
		}

		const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction();
		if (!TargetFunction || TargetFunction->GetOwnerClass() != UReplicLibrary::StaticClass())
		{
			return;
		}

		const FName FunctionName = TargetFunction->GetFName();
		const bool bIsEnumFunction =
			FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedEnum)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedEnum);
		const bool bIsGenericPropertyFunction =
			FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedStruct)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedStruct)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedMap)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedObject)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedObject)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedClass)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedClass)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap);
		if (!bIsEnumFunction && !bIsGenericPropertyFunction)
		{
			return;
		}

		UEdGraphPin* PropertyNamePin = CallFunctionNode->FindPin(TEXT("PropertyName"));
		if (!PropertyNamePin)
		{
			return;
		}

		TArray<FName> PinsToRefresh;
		if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)
			|| FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet))
		{
			PinsToRefresh.Add(TEXT("Item"));
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap))
		{
			PinsToRefresh.Add(TEXT("Key"));
			PinsToRefresh.Add(TEXT("Value"));
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap))
		{
			PinsToRefresh.Add(TEXT("Key"));
		}
		else
		{
			PinsToRefresh.Add(TEXT("Value"));
		}

		const bool bHasSelectedProperty = PropertyNamePin->GetDefaultAsString().Len() > 0;
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		bool bAnyPinChanged = false;

		for (const FName PinName : PinsToRefresh)
		{
			UEdGraphPin* TargetPin = CallFunctionNode->FindPin(PinName);
			if (!TargetPin)
			{
				continue;
			}

			FEdGraphPinType NewPinType;
			if (bIsEnumFunction && PinName == TEXT("Value"))
			{
				NewPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				NewPinType.PinSubCategoryObject = ResolveEnumForSelectedPropertyPin(PropertyNamePin);
			}
			else if (!ResolveSelectedPropertyPinTypeForFunctionPin(PropertyNamePin, TargetFunction, PinName, NewPinType))
			{
				if (!bHasSelectedProperty && TryBuildNeutralPinTypeForFunctionAndPin(TargetFunction, PinName, NewPinType))
				{
				}
				else if (!ResolveDefaultPinTypeFromFunctionAndPin(TargetFunction, PinName, NewPinType))
				{
					continue;
				}
			}

			const bool bTypeChanged = !(TargetPin->PinType == NewPinType);
			if (bTypeChanged && K2Schema)
			{
				const FEdGraphPinType OldPinType = TargetPin->PinType;
				if (!K2Schema->ArePinTypesCompatible(OldPinType, NewPinType))
				{
					TargetPin->BreakAllPinLinks();
				}
			}

			TargetPin->PinType = NewPinType;

			if (bTypeChanged)
			{
				bAnyPinChanged = true;
				for (UEdGraphPin* LinkedPin : TargetPin->LinkedTo)
				{
					if (LinkedPin)
					{
						if (UK2Node* LinkedK2Node = Cast<UK2Node>(LinkedPin->GetOwningNodeUnchecked()))
						{
							LinkedK2Node->NotifyPinConnectionListChanged(LinkedPin);
						}
					}
				}
			}

			if (!bIsEnumFunction && !bHasSelectedProperty && TargetPin->LinkedTo.Num() == 0 && TargetPin->Direction == EGPD_Input)
			{
				TargetPin->DefaultValue.Reset();
				TargetPin->AutogeneratedDefaultValue.Reset();
				TargetPin->DefaultObject = nullptr;
				TargetPin->DefaultTextValue = FText::GetEmpty();
			}
			else if (bIsEnumFunction && PinName == TEXT("Value") && !NewPinType.PinSubCategoryObject.IsValid() && TargetPin->LinkedTo.Num() == 0 && TargetPin->Direction == EGPD_Input)
			{
				TargetPin->DefaultValue.Reset();
				TargetPin->AutogeneratedDefaultValue.Reset();
			}
		}

		if (bAnyPinChanged)
		{
			if (UEdGraph* Graph = CallFunctionNode->GetGraph())
			{
				Graph->NotifyNodeChanged(CallFunctionNode);
			}
		}
	}
}

void SReplicOptionPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
	RefreshOptions();
}

TSharedRef<SWidget> SReplicOptionPin::GetDefaultValueWidget()
{
	RefreshOptions();

	return SAssignNew(ComboBox, SComboBox<TSharedPtr<FReplicPinOptionItem>>)
		.OptionsSource(&Options)
		.InitiallySelectedItem(GetSelectedItem())
		.OnComboBoxOpening(this, &SReplicOptionPin::HandleComboBoxOpening)
		.OnGenerateWidget(this, &SReplicOptionPin::GenerateOptionWidget)
		.OnSelectionChanged(this, &SReplicOptionPin::HandleSelectionChanged)
		.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
		[
			SNew(STextBlock)
			.Text(this, &SReplicOptionPin::GetSelectedItemText)
			.ToolTipText(this, &SReplicOptionPin::GetSelectedItemTooltip)
		];
}

TSharedPtr<FReplicPinOptionItem> SReplicOptionPin::MakeNoneItem() const
{
	TSharedPtr<FReplicPinOptionItem> NoneItem = MakeShared<FReplicPinOptionItem>();
	NoneItem->Value = NAME_None;
	NoneItem->DisplayText = NSLOCTEXT("ReplicEditor", "ReplicOptionNone", "None");
	NoneItem->TooltipText = NSLOCTEXT("ReplicEditor", "ReplicOptionNoneTooltip", "Clears the current Replic selection.");
	NoneItem->SortKey = TEXT("!None");
	return NoneItem;
}

void SReplicOptionPin::RefreshOptions()
{
	ReplicPinOptionResolver::BuildOptions(GraphPinObj, Options);
	Options.Insert(MakeNoneItem(), 0);
}

void SReplicOptionPin::HandleComboBoxOpening()
{
	RefreshOptions();
	if (ComboBox.IsValid())
	{
		ComboBox->RefreshOptions();
		ComboBox->SetSelectedItem(GetSelectedItem());
	}
}

void SReplicOptionPin::HandleSelectionChanged(TSharedPtr<FReplicPinOptionItem> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (!SelectedItem.IsValid())
	{
		return;
	}

	if (const UEdGraphSchema* Schema = GraphPinObj ? GraphPinObj->GetSchema() : nullptr)
	{
		const FString NewValue = SelectedItem->Value.ToString();
		const FString EffectiveValue = SelectedItem->Value.IsNone() ? FString() : NewValue;
		if (GraphPinObj->GetDefaultAsString() != EffectiveValue)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("ReplicEditor", "ChangeReplicPinValue", "Change Replic Pin Value"));
			TWeakObjectPtr<UEdGraphNode> WeakOwningNode = GraphPinObj->GetOwningNodeUnchecked();
			const FName ChangedPinName = GraphPinObj->PinName;
			GraphPinObj->Modify();
			Schema->TrySetDefaultValue(*GraphPinObj, EffectiveValue);

			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakOwningNode, ChangedPinName](float)
			{
				if (UK2Node_ReplicCallEvent* ReplicCallEventNode = Cast<UK2Node_ReplicCallEvent>(WeakOwningNode.Get()))
				{
					ReplicCallEventNode->HandleReplicSelectionChanged(ChangedPinName);
				}
				else if (UK2Node_ReplicSetArray* ReplicSetArrayNode = Cast<UK2Node_ReplicSetArray>(WeakOwningNode.Get()))
				{
					ReplicSetArrayNode->HandleReplicSelectionChanged(ChangedPinName);
				}
				else if (UK2Node_ReplicGetArray* ReplicGetArrayNode = Cast<UK2Node_ReplicGetArray>(WeakOwningNode.Get()))
				{
					ReplicGetArrayNode->HandleReplicSelectionChanged(ChangedPinName);
				}
				else if (UK2Node_ReplicSetEnum* ReplicSetEnumNode = Cast<UK2Node_ReplicSetEnum>(WeakOwningNode.Get()))
				{
					ReplicSetEnumNode->HandleReplicSelectionChanged(ChangedPinName);
				}
				else if (UK2Node_ReplicGetEnum* ReplicGetEnumNode = Cast<UK2Node_ReplicGetEnum>(WeakOwningNode.Get()))
				{
					ReplicGetEnumNode->HandleReplicSelectionChanged(ChangedPinName);
				}
				else if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(WeakOwningNode.Get()))
				{
					RefreshDynamicPinsOnCallFunction(CallFunctionNode);
				}

				return false;
			}), 0.0f);
		}
	}
}

TSharedRef<SWidget> SReplicOptionPin::GenerateOptionWidget(TSharedPtr<FReplicPinOptionItem> Item) const
{
	return SNew(STextBlock)
		.Text(Item.IsValid() ? Item->DisplayText : FText::GetEmpty())
		.ToolTipText(Item.IsValid() ? Item->TooltipText : FText::GetEmpty());
}

TSharedPtr<FReplicPinOptionItem> SReplicOptionPin::GetSelectedItem() const
{
	if (!GraphPinObj)
	{
		return nullptr;
	}

	const FName CurrentValue(*GraphPinObj->GetDefaultAsString());
	for (const TSharedPtr<FReplicPinOptionItem>& Item : Options)
	{
		if (Item.IsValid() && Item->Value == CurrentValue)
		{
			return Item;
		}
	}

	if (CurrentValue.IsNone())
	{
		return MakeNoneItem();
	}

	if (!CurrentValue.IsNone())
	{
		TSharedPtr<FReplicPinOptionItem> FallbackItem = MakeShared<FReplicPinOptionItem>();
		FallbackItem->Value = CurrentValue;
		FallbackItem->DisplayText = FText::FromName(CurrentValue);
		FallbackItem->TooltipText = FText::FromString(TEXT("Current value is not available in the filtered Replic option list."));
		return FallbackItem;
	}

	return nullptr;
}

FText SReplicOptionPin::GetSelectedItemText() const
{
	if (const TSharedPtr<FReplicPinOptionItem> SelectedItem = GetSelectedItem())
	{
		return SelectedItem->DisplayText;
	}

	return NSLOCTEXT("ReplicEditor", "SelectReplicOption", "Select...");
}

FText SReplicOptionPin::GetSelectedItemTooltip() const
{
	if (const TSharedPtr<FReplicPinOptionItem> SelectedItem = GetSelectedItem())
	{
		return SelectedItem->TooltipText;
	}

	return NSLOCTEXT("ReplicEditor", "SelectReplicOptionTooltip", "Choose a Replic-marked property or event.");
}
