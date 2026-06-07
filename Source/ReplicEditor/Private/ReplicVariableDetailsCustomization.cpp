#include "ReplicVariableDetailsCustomization.h"

#include "BlueprintEditor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_CallFunction.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_ReplicGetArray.h"
#include "K2Node_ReplicGetEnum.h"
#include "K2Node_ReplicSetArray.h"
#include "K2Node_ReplicSetEnum.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ReplicK2NodeUtils.h"
#include "ReplicLibrary.h"
#include "ReplicMetadata.h"
#include "ReplicSettings.h"
#include "SMyBlueprint.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ReplicVariableDetails"

namespace
{
	bool IsEnumLikeProperty(const FProperty* Property)
	{
		if (CastField<FEnumProperty>(Property))
		{
			return true;
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			return ByteProperty->Enum != nullptr;
		}

		return false;
	}

	const FText ReplicateAllTooltip = LOCTEXT("ReplicateAllTooltip", "Enables Replic handling for this variable.\n\nWrites made through Replic setter nodes are sent to the server and then distributed using this variable's Replic settings.");
	const FText PersistentStateTooltip = LOCTEXT("PersistentStateTooltip", "Stores this variable as persistent replicated state.\n\nLate joiners receive the latest stored value when they connect.");
	const FText UseBatchingTooltip = LOCTEXT("UseBatchingTooltip", "Queues repeated writes for this variable.\n\nReplic sends the latest value after a short delay instead of sending every single change immediately.");
	const FText BatchIntervalTooltip = LOCTEXT("BatchIntervalTooltip", "Defines how long Replic waits before flushing a batched write.\n\nLower values update sooner.\nHigher values reduce network spam.");
	const FText PermissionModeTooltip = LOCTEXT("PermissionModeTooltip", "Controls who is allowed to request writes for this variable.\n\nNone:\nAccept requests without an extra permission check.\n\nOwnerOnly:\nOnly the owning client may request writes.\n\nServerOnly:\nOnly writes initiated on the server are accepted.\n\nCustom:\nThe target object must approve the request with a validation function.\nUse CanReplicWrite_<VariableName>() or CanReplicWrite_<VariableName>(RequestingActor).");

	FName MakeCustomVariableValidationFunctionName(FName InVariableName)
	{
		return FName(*FString::Printf(TEXT("CanReplicWrite_%s"), *InVariableName.ToString()));
	}

	enum class EReplicContainerDeltaButtonKind : uint8
	{
		Add,
		Remove,
		SetEntry
	};

	UEdGraph* FindFunctionGraph(UBlueprint* Blueprint, FName FunctionName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == FunctionName)
			{
				return Graph;
			}
		}

		return nullptr;
	}

	UEdGraph* EnsureValidationFunctionGraph(UBlueprint* Blueprint, FName FunctionName)
	{
		if (!Blueprint || FunctionName.IsNone())
		{
			return nullptr;
		}

		if (UEdGraph* ExistingGraph = FindFunctionGraph(Blueprint, FunctionName))
		{
			return ExistingGraph;
		}

		Blueprint->Modify();
		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, true, nullptr);

		if (UK2Node_EditablePinBase* EntryNode = FBlueprintEditorUtils::GetEntryNode(NewGraph))
		{
			FEdGraphPinType ActorPinType;
			ActorPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			ActorPinType.PinSubCategoryObject = AActor::StaticClass();
			if (!EntryNode->FindPin(TEXT("RequestingActor")))
			{
				EntryNode->CreateUserDefinedPin(TEXT("RequestingActor"), ActorPinType, EGPD_Output, false);
			}

			if (UK2Node_FunctionResult* ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode))
			{
				FEdGraphPinType BoolPinType;
				BoolPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
				if (!ResultNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue))
				{
					ResultNode->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, BoolPinType, EGPD_Input, false);
				}
			}

			EntryNode->ReconstructNode();
		}

		return NewGraph;
	}

	const FProperty* ResolveBlueprintVariableProperty(const UBlueprint* Blueprint, FName VariableName)
	{
		if (!Blueprint || VariableName == NAME_None)
		{
			return nullptr;
		}

		if (const UClass* SkeletonClass = Blueprint->SkeletonGeneratedClass)
		{
			if (const FProperty* Property = FindFProperty<FProperty>(SkeletonClass, VariableName))
			{
				return Property;
			}
		}

		if (const UClass* GeneratedClass = Blueprint->GeneratedClass)
		{
			if (const FProperty* Property = FindFProperty<FProperty>(GeneratedClass, VariableName))
			{
				return Property;
			}
		}

		return nullptr;
	}

	UFunction* ResolveSetterFunctionForProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return nullptr;
		}

		UClass* LibraryClass = UReplicLibrary::StaticClass();
		if (CastField<FArrayProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray));
		}

		if (CastField<FSetProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet));
		}

		if (CastField<FMapProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap));
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedVector));
			}

			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedRotator));
			}

			if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedTransform));
			}

			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedStruct));
		}

		if (CastField<FBoolProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedBool));
		}

		if (CastField<FIntProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedInt));
		}

		if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedFloat));
		}

		if (IsEnumLikeProperty(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedEnum));
		}

		if (CastField<FByteProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedByte));
		}

		if (CastField<FNameProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedName));
		}

		if (CastField<FStrProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedString));
		}

		if (CastField<FTextProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedText));
		}

		if (CastField<FClassProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedClass));
		}

		if (CastField<FObjectPropertyBase>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedObject));
		}

		return nullptr;
	}

	UFunction* ResolveGetterFunctionForProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return nullptr;
		}

		UClass* LibraryClass = UReplicLibrary::StaticClass();
		if (CastField<FArrayProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedArray));
		}

		if (CastField<FSetProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedSet));
		}

		if (CastField<FMapProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedMap));
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedVector));
			}

			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedRotator));
			}

			if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedTransform));
			}

			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedStruct));
		}

		if (CastField<FBoolProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedBool));
		}

		if (CastField<FIntProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedInt));
		}

		if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedFloat));
		}

		if (IsEnumLikeProperty(Property) || CastField<FByteProperty>(Property))
		{
			return IsEnumLikeProperty(Property)
				? LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedEnum))
				: LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedByte));
		}

		if (CastField<FNameProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedName));
		}

		if (CastField<FStrProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedString));
		}

		if (CastField<FTextProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedText));
		}

		if (CastField<FClassProperty>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedClass));
		}

		if (CastField<FObjectPropertyBase>(Property))
		{
			return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedObject));
		}

		return nullptr;
	}

	UFunction* ResolveDeltaFunctionForProperty(const FProperty* Property, const EReplicContainerDeltaButtonKind Kind)
	{
		if (!Property)
		{
			return nullptr;
		}

		UClass* LibraryClass = UReplicLibrary::StaticClass();
		if (CastField<FArrayProperty>(Property))
		{
			if (Kind == EReplicContainerDeltaButtonKind::Add)
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray));
			}

			if (Kind == EReplicContainerDeltaButtonKind::Remove)
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray));
			}
		}

		if (CastField<FSetProperty>(Property))
		{
			if (Kind == EReplicContainerDeltaButtonKind::Add)
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet));
			}

			if (Kind == EReplicContainerDeltaButtonKind::Remove)
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet));
			}
		}

		if (CastField<FMapProperty>(Property))
		{
			if (Kind == EReplicContainerDeltaButtonKind::SetEntry)
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap));
			}

			if (Kind == EReplicContainerDeltaButtonKind::Remove)
			{
				return LibraryClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap));
			}
		}

		return nullptr;
	}
}

FReplicVariableDetailsCustomization::FReplicVariableDetailsCustomization(TWeakPtr<FBlueprintEditor> InBlueprintEditor)
	: BlueprintEditor(InBlueprintEditor)
{
	PermissionOptions.Add(MakeShared<FString>(TEXT("None")));
	PermissionOptions.Add(MakeShared<FString>(TEXT("OwnerOnly")));
	PermissionOptions.Add(MakeShared<FString>(TEXT("ServerOnly")));
	PermissionOptions.Add(MakeShared<FString>(TEXT("Custom")));

	if (TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin())
	{
		Blueprint = Editor->GetBlueprintObj();
	}
}

void FReplicVariableDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	FEdGraphSchemaAction_BlueprintVariableBase* VariableAction = nullptr;
	if (!ResolveSelection(VariableAction) || !Blueprint.IsValid())
	{
		return;
	}

	VariableName = VariableAction->GetVariableName();
	const FProperty* VariableProperty = ResolveBlueprintVariableProperty(Blueprint.Get(), VariableName);
	const FString CurrentPermissionMode = GetStringMetadata(ReplicMetadata::VariablePermissionMode, TEXT("None"));
	TSharedPtr<FString>* FoundPermissionMode = PermissionOptions.FindByPredicate([&CurrentPermissionMode](const TSharedPtr<FString>& Item)
	{
		return Item.IsValid() && *Item == CurrentPermissionMode;
	});
	TSharedPtr<FString> InitialPermissionMode = FoundPermissionMode ? *FoundPermissionMode : PermissionOptions[0];

	IDetailCategoryBuilder& Category = DetailLayout.EditCategory(TEXT("Replic"), LOCTEXT("ReplicCategory", "Replic"));
	Category.AddCustomRow(LOCTEXT("ReplicateAll", "Replicate All"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ReplicateAllLabel", "Replicate All"))
		.ToolTipText(ReplicateAllTooltip)
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.ToolTipText(ReplicateAllTooltip)
		.IsChecked_Lambda([this]()
		{
			return GetBoolMetadata(ReplicMetadata::VariableEnabled, false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
		{
			SetBoolMetadata(ReplicMetadata::VariableEnabled, NewState == ECheckBoxState::Checked);
		})
	];

	Category.AddCustomRow(LOCTEXT("PersistentState", "Persistent State"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PersistentStateLabel", "Persistent State"))
		.ToolTipText(PersistentStateTooltip)
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.ToolTipText(PersistentStateTooltip)
		.IsEnabled_Lambda([this]()
		{
			return GetBoolMetadata(ReplicMetadata::VariableEnabled, false);
		})
		.IsChecked_Lambda([this]()
		{
			return GetBoolMetadata(ReplicMetadata::VariablePersistent, true) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
		{
			SetBoolMetadata(ReplicMetadata::VariablePersistent, NewState == ECheckBoxState::Checked);
		})
	];

	Category.AddCustomRow(LOCTEXT("UseBatching", "Use Batching"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("UseBatchingLabel", "Use Batching"))
		.ToolTipText(UseBatchingTooltip)
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.ToolTipText(UseBatchingTooltip)
		.IsEnabled_Lambda([this]()
		{
			return GetBoolMetadata(ReplicMetadata::VariableEnabled, false);
		})
		.IsChecked_Lambda([this]()
		{
			return GetBoolMetadata(ReplicMetadata::VariableBatching, false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
		{
			SetBoolMetadata(ReplicMetadata::VariableBatching, NewState == ECheckBoxState::Checked);
		})
	];

	Category.AddCustomRow(LOCTEXT("BatchInterval", "Batch Interval"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BatchIntervalLabel", "Batch Interval"))
		.ToolTipText(BatchIntervalTooltip)
	]
	.ValueContent()
	[
		SNew(SNumericEntryBox<float>)
		.ToolTipText(BatchIntervalTooltip)
		.IsEnabled_Lambda([this]()
		{
			return GetBoolMetadata(ReplicMetadata::VariableEnabled, false) && GetBoolMetadata(ReplicMetadata::VariableBatching, false);
		})
		.MinValue(0.0f)
		.Value_Lambda([this]()
		{
			return GetFloatMetadata(ReplicMetadata::VariableBatchInterval, GetDefault<UReplicSettings>()->DefaultBatchIntervalSeconds);
		})
		.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
		{
			SetFloatMetadata(ReplicMetadata::VariableBatchInterval, NewValue);
		})
	];

	Category.AddCustomRow(LOCTEXT("PermissionMode", "Permission"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PermissionModeLabel", "Permission"))
		.ToolTipText(PermissionModeTooltip)
	]
	.ValueContent()
	[
		SNew(STextComboBox)
		.ToolTipText(PermissionModeTooltip)
		.OptionsSource(&PermissionOptions)
		.InitiallySelectedItem(InitialPermissionMode)
		.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Selection, ESelectInfo::Type)
		{
			if (Selection.IsValid())
			{
				SetStringMetadata(ReplicMetadata::VariablePermissionMode, *Selection);
				if (*Selection == TEXT("Custom"))
				{
					CreateCustomValidationFunction();
				}
			}
		})
	];

	Category.AddCustomRow(LOCTEXT("CreateCustomValidation", "Create Custom Validation"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("CreateCustomValidationLabel", "Create Custom Validation Function"))
		.ToolTipText(LOCTEXT("CreateCustomValidationTooltip", "Creates or opens the matching custom validation function for this variable.\n\nReplic will call CanReplicWrite_<VariableName>(RequestingActor) when Permission is set to Custom."))
		.OnClicked_Lambda([this]()
		{
			return CreateCustomValidationFunction();
		})
	];

	Category.AddCustomRow(LOCTEXT("CreateReplicSetter", "Create Replic Setter"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("CreateReplicSetterLabel", "Create Replic Setter Node"))
		.ToolTipText(LOCTEXT("CreateReplicSetterTooltip", "Adds the matching typed Replic setter node for this variable to the currently focused graph."))
		.OnClicked_Lambda([this]()
		{
			return CreateReplicSetterNode();
		})
	];

	Category.AddCustomRow(LOCTEXT("CreateReplicGetter", "Create Replic Getter"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("CreateReplicGetterLabel", "Create Replic Getter Node"))
		.ToolTipText(LOCTEXT("CreateReplicGetterTooltip", "Adds the matching typed Replic getter node for this variable to the currently focused graph."))
		.OnClicked_Lambda([this]()
		{
			return CreateReplicGetterNode();
		})
	];

	if (CastField<FArrayProperty>(VariableProperty) || CastField<FSetProperty>(VariableProperty))
	{
		const bool bIsArray = CastField<FArrayProperty>(VariableProperty) != nullptr;
		Category.AddCustomRow(LOCTEXT("CreateReplicAddDelta", "Create Replic Add Delta"))
		.WholeRowContent()
		[
			SNew(SButton)
			.Text(bIsArray ? LOCTEXT("CreateReplicArrayAddLabel", "Create Replic Add Node") : LOCTEXT("CreateReplicSetAddLabel", "Create Replic Add Node"))
			.ToolTipText(bIsArray
				? LOCTEXT("CreateReplicArrayAddTooltip", "Adds an Add To Marked Array node for this variable to the currently focused graph.")
				: LOCTEXT("CreateReplicSetAddTooltip", "Adds an Add To Marked Set node for this variable to the currently focused graph."))
			.OnClicked_Lambda([this]()
			{
				return CreateReplicAddDeltaNode();
			})
		];

		Category.AddCustomRow(LOCTEXT("CreateReplicRemoveDelta", "Create Replic Remove Delta"))
		.WholeRowContent()
		[
			SNew(SButton)
			.Text(bIsArray ? LOCTEXT("CreateReplicArrayRemoveLabel", "Create Replic Remove Node") : LOCTEXT("CreateReplicSetRemoveLabel", "Create Replic Remove Node"))
			.ToolTipText(bIsArray
				? LOCTEXT("CreateReplicArrayRemoveTooltip", "Adds a Remove From Marked Array node for this variable to the currently focused graph.")
				: LOCTEXT("CreateReplicSetRemoveTooltip", "Adds a Remove From Marked Set node for this variable to the currently focused graph."))
			.OnClicked_Lambda([this]()
			{
				return CreateReplicRemoveDeltaNode();
			})
		];
	}
	else if (CastField<FMapProperty>(VariableProperty))
	{
		Category.AddCustomRow(LOCTEXT("CreateReplicSetEntryDelta", "Create Replic Set Entry Delta"))
		.WholeRowContent()
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateReplicMapSetEntryLabel", "Create Replic Set Entry Node"))
			.ToolTipText(LOCTEXT("CreateReplicMapSetEntryTooltip", "Adds a Set In Marked Map node for this variable to the currently focused graph."))
			.OnClicked_Lambda([this]()
			{
				return CreateReplicSetEntryDeltaNode();
			})
		];

		Category.AddCustomRow(LOCTEXT("CreateReplicRemoveMapDelta", "Create Replic Remove Map Delta"))
		.WholeRowContent()
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateReplicMapRemoveLabel", "Create Replic Remove Entry Node"))
			.ToolTipText(LOCTEXT("CreateReplicMapRemoveTooltip", "Adds a Remove From Marked Map node for this variable to the currently focused graph."))
			.OnClicked_Lambda([this]()
			{
				return CreateReplicRemoveDeltaNode();
			})
		];
	}
}

bool FReplicVariableDetailsCustomization::ResolveSelection(FEdGraphSchemaAction_BlueprintVariableBase*& OutVariableAction) const
{
	if (TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin())
	{
		if (TSharedPtr<SMyBlueprint> MyBlueprint = Editor->GetMyBlueprintWidget())
		{
			OutVariableAction = MyBlueprint->SelectionAsBlueprintVariable();
			if (!OutVariableAction)
			{
				OutVariableAction = MyBlueprint->SelectionAsVar();
			}
		}
	}

	return OutVariableAction != nullptr;
}

bool FReplicVariableDetailsCustomization::GetBoolMetadata(const FName& Key, bool bDefaultValue) const
{
	if (!Blueprint.IsValid() || VariableName == NAME_None)
	{
		return bDefaultValue;
	}

	FString MetaDataValue;
	if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, Key, MetaDataValue))
	{
		return MetaDataValue.ToBool();
	}

	return bDefaultValue;
}

float FReplicVariableDetailsCustomization::GetFloatMetadata(const FName& Key, float DefaultValue) const
{
	if (!Blueprint.IsValid() || VariableName == NAME_None)
	{
		return DefaultValue;
	}

	FString MetaDataValue;
	if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, Key, MetaDataValue))
	{
		return FCString::Atof(*MetaDataValue);
	}

	return DefaultValue;
}

FString FReplicVariableDetailsCustomization::GetStringMetadata(const FName& Key, const FString& DefaultValue) const
{
	if (!Blueprint.IsValid() || VariableName == NAME_None)
	{
		return DefaultValue;
	}

	FString MetaDataValue;
	if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, Key, MetaDataValue))
	{
		return MetaDataValue;
	}

	return DefaultValue;
}

FReply FReplicVariableDetailsCustomization::CreateReplicCallFunctionNode(UFunction* Function, FVector2D SpawnPosition) const
{
	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	if (!Editor.IsValid() || !Blueprint.IsValid() || VariableName == NAME_None || !Function)
	{
		return FReply::Handled();
	}

	UEdGraph* TargetGraph = Editor->GetFocusedGraph();
	if (!TargetGraph && Blueprint->UbergraphPages.Num() > 0)
	{
		TargetGraph = Blueprint->UbergraphPages[0];
		Editor->OpenGraphAndBringToFront(TargetGraph);
	}

	if (!TargetGraph)
	{
		return FReply::Handled();
	}
	UK2Node_CallFunction* NewNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
		TargetGraph,
		SpawnPosition,
		EK2NewNodeFlags::SelectNewNode,
		[Function](UK2Node_CallFunction* NewInstance)
		{
			NewInstance->SetFromFunction(Function);
		});

	if (NewNode)
	{
		if (UEdGraphPin* PropertyPin = NewNode->FindPin(TEXT("PropertyName")))
		{
			PropertyPin->DefaultValue = VariableName.ToString();
			PropertyPin->AutogeneratedDefaultValue = PropertyPin->DefaultValue;
			NewNode->PinDefaultValueChanged(PropertyPin);
		}

		ReplicK2NodeUtils::ApplyReplicLibraryPinToolTips(NewNode);
		Editor->AddToSelection(NewNode);
		Editor->JumpToNode(NewNode);
	}

	return FReply::Handled();
}

FReply FReplicVariableDetailsCustomization::CreateReplicSetterNode() const
{
	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	if (!Editor.IsValid() || !Blueprint.IsValid() || VariableName == NAME_None)
	{
		return FReply::Handled();
	}

	const FProperty* VariableProperty = ResolveBlueprintVariableProperty(Blueprint.Get(), VariableName);
	if (IsEnumLikeProperty(VariableProperty))
	{
		UEdGraph* TargetGraph = Editor->GetFocusedGraph();
		if (!TargetGraph && Blueprint->UbergraphPages.Num() > 0)
		{
			TargetGraph = Blueprint->UbergraphPages[0];
			Editor->OpenGraphAndBringToFront(TargetGraph);
		}

		if (!TargetGraph)
		{
			return FReply::Handled();
		}

		UK2Node_ReplicSetEnum* NewNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ReplicSetEnum>(
			TargetGraph,
			FVector2D(200.0, 200.0),
			EK2NewNodeFlags::SelectNewNode);

		if (NewNode)
		{
			if (UEdGraphPin* PropertyPin = NewNode->FindPin(TEXT("PropertyName")))
			{
				PropertyPin->DefaultValue = VariableName.ToString();
			}

			NewNode->ReconstructNode();
			Editor->AddToSelection(NewNode);
			Editor->JumpToNode(NewNode);
		}

		return FReply::Handled();
	}

	if (CastField<FArrayProperty>(VariableProperty))
	{
		UEdGraph* TargetGraph = Editor->GetFocusedGraph();
		if (!TargetGraph && Blueprint->UbergraphPages.Num() > 0)
		{
			TargetGraph = Blueprint->UbergraphPages[0];
			Editor->OpenGraphAndBringToFront(TargetGraph);
		}

		if (!TargetGraph)
		{
			return FReply::Handled();
		}

		UK2Node_ReplicSetArray* NewNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ReplicSetArray>(
			TargetGraph,
			FVector2D(200.0, 200.0),
			EK2NewNodeFlags::SelectNewNode);

		if (NewNode)
		{
			if (UEdGraphPin* PropertyPin = NewNode->FindPin(TEXT("PropertyName")))
			{
				PropertyPin->DefaultValue = VariableName.ToString();
			}

			NewNode->ReconstructNode();
			Editor->AddToSelection(NewNode);
			Editor->JumpToNode(NewNode);
		}

		return FReply::Handled();
	}

	return CreateReplicCallFunctionNode(ResolveSetterFunctionForProperty(VariableProperty), FVector2D(200.0, 200.0));
}

FReply FReplicVariableDetailsCustomization::CreateReplicGetterNode() const
{
	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	if (!Editor.IsValid() || !Blueprint.IsValid() || VariableName == NAME_None)
	{
		return FReply::Handled();
	}

	UEdGraph* TargetGraph = Editor->GetFocusedGraph();
	if (!TargetGraph && Blueprint->UbergraphPages.Num() > 0)
	{
		TargetGraph = Blueprint->UbergraphPages[0];
		Editor->OpenGraphAndBringToFront(TargetGraph);
	}

	if (!TargetGraph)
	{
		return FReply::Handled();
	}

	const FProperty* VariableProperty = ResolveBlueprintVariableProperty(Blueprint.Get(), VariableName);
	if (IsEnumLikeProperty(VariableProperty))
	{
		// Enum variables need dedicated K2 nodes so the value pin can expose the concrete enum instead of a raw byte pin.
		const FVector2D SpawnPosition(200.0, 260.0);
		UK2Node_ReplicGetEnum* NewNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ReplicGetEnum>(
			TargetGraph,
			SpawnPosition,
			EK2NewNodeFlags::SelectNewNode);

		if (NewNode)
		{
			if (UEdGraphPin* PropertyPin = NewNode->FindPin(TEXT("PropertyName")))
			{
				PropertyPin->DefaultValue = VariableName.ToString();
			}

			NewNode->ReconstructNode();
			Editor->AddToSelection(NewNode);
			Editor->JumpToNode(NewNode);
		}

		return FReply::Handled();
	}

	if (CastField<FArrayProperty>(VariableProperty))
	{
		const FVector2D SpawnPosition(200.0, 260.0);
		UK2Node_ReplicGetArray* NewNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ReplicGetArray>(
			TargetGraph,
			SpawnPosition,
			EK2NewNodeFlags::SelectNewNode);

		if (NewNode)
		{
			if (UEdGraphPin* PropertyPin = NewNode->FindPin(TEXT("PropertyName")))
			{
				PropertyPin->DefaultValue = VariableName.ToString();
			}

			NewNode->ReconstructNode();
			Editor->AddToSelection(NewNode);
			Editor->JumpToNode(NewNode);
		}

		return FReply::Handled();
	}

	return CreateReplicCallFunctionNode(ResolveGetterFunctionForProperty(VariableProperty), FVector2D(200.0, 260.0));
}

FReply FReplicVariableDetailsCustomization::CreateReplicAddDeltaNode() const
{
	const FProperty* VariableProperty = ResolveBlueprintVariableProperty(Blueprint.Get(), VariableName);
	return CreateReplicCallFunctionNode(ResolveDeltaFunctionForProperty(VariableProperty, EReplicContainerDeltaButtonKind::Add), FVector2D(200.0, 320.0));
}

FReply FReplicVariableDetailsCustomization::CreateReplicRemoveDeltaNode() const
{
	const FProperty* VariableProperty = ResolveBlueprintVariableProperty(Blueprint.Get(), VariableName);
	return CreateReplicCallFunctionNode(ResolveDeltaFunctionForProperty(VariableProperty, EReplicContainerDeltaButtonKind::Remove), FVector2D(200.0, 380.0));
}

FReply FReplicVariableDetailsCustomization::CreateReplicSetEntryDeltaNode() const
{
	const FProperty* VariableProperty = ResolveBlueprintVariableProperty(Blueprint.Get(), VariableName);
	return CreateReplicCallFunctionNode(ResolveDeltaFunctionForProperty(VariableProperty, EReplicContainerDeltaButtonKind::SetEntry), FVector2D(200.0, 320.0));
}

FReply FReplicVariableDetailsCustomization::CreateCustomValidationFunction() const
{
	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	if (!Editor.IsValid() || !Blueprint.IsValid() || VariableName == NAME_None)
	{
		return FReply::Handled();
	}

	if (UEdGraph* ValidationGraph = EnsureValidationFunctionGraph(Blueprint.Get(), MakeCustomVariableValidationFunctionName(VariableName)))
	{
		Editor->OpenGraphAndBringToFront(ValidationGraph);
	}

	return FReply::Handled();
}

void FReplicVariableDetailsCustomization::SetBoolMetadata(const FName& Key, bool bEnabled)
{
	if (!Blueprint.IsValid() || VariableName == NAME_None)
	{
		return;
	}

	if (bEnabled)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, Key, TEXT("true"));
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, Key);
	}

	RefreshBlueprint();
}

void FReplicVariableDetailsCustomization::SetFloatMetadata(const FName& Key, float Value)
{
	if (!Blueprint.IsValid() || VariableName == NAME_None)
	{
		return;
	}

	FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, Key, LexToString(Value));
	RefreshBlueprint();
}

void FReplicVariableDetailsCustomization::SetStringMetadata(const FName& Key, const FString& Value)
{
	if (!Blueprint.IsValid() || VariableName == NAME_None)
	{
		return;
	}

	FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, Key, Value);
	RefreshBlueprint();
}

void FReplicVariableDetailsCustomization::RefreshBlueprint() const
{
	if (Blueprint.IsValid())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint.Get());
	}

	if (TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin())
	{
		Editor->RefreshInspector();
	}
}

#undef LOCTEXT_NAMESPACE
