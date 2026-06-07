#include "ReplicEventDetailsCustomization.h"

#include "BlueprintEditor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditorModule.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_ReplicCallEvent.h"
#include "K2Node_CustomEvent.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ReplicMetadata.h"
#include "SMyBlueprint.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ReplicEventDetails"

namespace
{
	const FText ReplicateAllEventTooltip = LOCTEXT("ReplicateAllEventTooltip", "Enables Replic handling for this custom event.\n\nCalls made through Call Marked Event are sent to the server and then dispatched using the selected Replic mode.");
	const FText EventPermissionTooltip = LOCTEXT("EventPermissionTooltip", "Controls who is allowed to request this custom event.\n\nNone:\nAccept requests without an extra permission check.\n\nOwnerOnly:\nOnly the owning client may request the event.\n\nServerOnly:\nOnly calls initiated on the server are accepted.\n\nCustom:\nThe target object must approve the request with a validation function.\nUse CanReplicCall_<EventName>() or CanReplicCall_<EventName>(RequestingActor).");
	const FText EventModeTooltip = LOCTEXT("EventModeTooltip", "Controls how Replic dispatches this custom event after the server accepts it.\n\nLocalOnly:\nExecute only on the local resolved target.\n\nServerOnly:\nExecute only on the server target.\n\nOwnerOnly:\nExecute on the server and send the event to the owning client.\n\nReplicateAll:\nExecute on the server and broadcast the event to all relevant clients.");

	FName MakeCustomEventValidationFunctionName(FName InEventName)
	{
		return FName(*FString::Printf(TEXT("CanReplicCall_%s"), *InEventName.ToString()));
	}

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

	TSharedPtr<FBlueprintEditor> ResolveBlueprintEditor(const TWeakPtr<FBlueprintEditor>& WeakEditor, UBlueprint* Blueprint)
	{
		if (TSharedPtr<FBlueprintEditor> ExistingEditor = WeakEditor.Pin())
		{
			return ExistingEditor;
		}

		if (!Blueprint || !FModuleManager::Get().IsModuleLoaded("Kismet"))
		{
			return nullptr;
		}

		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
		for (const TSharedRef<IBlueprintEditor>& EditorRef : BlueprintEditorModule.GetBlueprintEditors())
		{
			TSharedRef<FBlueprintEditor> ConcreteEditor = StaticCastSharedRef<FBlueprintEditor>(EditorRef);
			if (ConcreteEditor->GetBlueprintObj() == Blueprint)
			{
				return ConcreteEditor;
			}
		}

		return nullptr;
	}

	void SyncReplicEventMetadata(UBlueprint* Blueprint, const UK2Node_CustomEvent* EventNode)
	{
		if (!Blueprint || !EventNode)
		{
			return;
		}

		const auto SyncClass = [EventNode](UClass* TargetClass)
		{
			if (!TargetClass)
			{
				return;
			}

			if (UFunction* TargetFunction = TargetClass->FindFunctionByName(EventNode->GetFunctionName()))
			{
				UK2Node_CustomEvent* MutableEventNode = const_cast<UK2Node_CustomEvent*>(EventNode);
				const auto& Metadata = MutableEventNode->GetUserDefinedMetaData();

				if (Metadata.HasMetaData(ReplicMetadata::EventEnabled))
				{
					TargetFunction->SetMetaData(ReplicMetadata::EventEnabled, *Metadata.GetMetaData(ReplicMetadata::EventEnabled));
				}
				else
				{
					TargetFunction->RemoveMetaData(ReplicMetadata::EventEnabled);
				}

				if (Metadata.HasMetaData(ReplicMetadata::EventMode))
				{
					TargetFunction->SetMetaData(ReplicMetadata::EventMode, *Metadata.GetMetaData(ReplicMetadata::EventMode));
				}
				else
				{
					TargetFunction->RemoveMetaData(ReplicMetadata::EventMode);
				}

				if (Metadata.HasMetaData(ReplicMetadata::EventPermissionMode))
				{
					TargetFunction->SetMetaData(ReplicMetadata::EventPermissionMode, *Metadata.GetMetaData(ReplicMetadata::EventPermissionMode));
				}
				else
				{
					TargetFunction->RemoveMetaData(ReplicMetadata::EventPermissionMode);
				}
			}
		};

		SyncClass(Blueprint->SkeletonGeneratedClass);
		SyncClass(Blueprint->GeneratedClass);
	}
}

FReplicEventDetailsCustomization::FReplicEventDetailsCustomization(TWeakPtr<FBlueprintEditor> InBlueprintEditor, bool bInFunctionCustomization)
	: BlueprintEditor(InBlueprintEditor)
	, bFunctionCustomization(bInFunctionCustomization)
{
	ModeOptions.Add(MakeShared<FString>(TEXT("LocalOnly")));
	ModeOptions.Add(MakeShared<FString>(TEXT("ServerOnly")));
	ModeOptions.Add(MakeShared<FString>(TEXT("OwnerOnly")));
	ModeOptions.Add(MakeShared<FString>(TEXT("ReplicateAll")));
	PermissionOptions.Add(MakeShared<FString>(TEXT("None")));
	PermissionOptions.Add(MakeShared<FString>(TEXT("OwnerOnly")));
	PermissionOptions.Add(MakeShared<FString>(TEXT("ServerOnly")));
	PermissionOptions.Add(MakeShared<FString>(TEXT("Custom")));
}

TSharedRef<IDetailCustomization> FReplicEventDetailsCustomization::MakeInstance()
{
	return MakeShared<FReplicEventDetailsCustomization>();
}

void FReplicEventDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	const bool bHasDirectNodeSelection = DetailLayout.GetSelectedObjects().ContainsByPredicate([](const TWeakObjectPtr<UObject>& SelectedObject)
	{
		return Cast<UK2Node_CustomEvent>(SelectedObject.Get()) != nullptr;
	});

	if (bFunctionCustomization && bHasDirectNodeSelection)
	{
		return;
	}

	if (!bFunctionCustomization && !bHasDirectNodeSelection)
	{
		return;
	}

	UK2Node_CustomEvent* EventNode = ResolveSelectedEventNode(DetailLayout);
	if (!EventNode)
	{
		return;
	}

	const FString CurrentMode = GetStringMetadata(EventNode, ReplicMetadata::EventMode, TEXT("ReplicateAll"));
	TSharedPtr<FString>* FoundMode = ModeOptions.FindByPredicate([&CurrentMode](const TSharedPtr<FString>& Item)
	{
		return Item.IsValid() && *Item == CurrentMode;
	});
	TSharedPtr<FString> InitialMode = FoundMode ? *FoundMode : ModeOptions.Last();
	const FString CurrentPermissionMode = GetStringMetadata(EventNode, ReplicMetadata::EventPermissionMode, TEXT("None"));
	TSharedPtr<FString>* FoundPermissionMode = PermissionOptions.FindByPredicate([&CurrentPermissionMode](const TSharedPtr<FString>& Item)
	{
		return Item.IsValid() && *Item == CurrentPermissionMode;
	});
	TSharedPtr<FString> InitialPermissionMode = FoundPermissionMode ? *FoundPermissionMode : PermissionOptions[0];

	IDetailCategoryBuilder& Category = DetailLayout.EditCategory(TEXT("Replic"), LOCTEXT("ReplicCategory", "Replic"));
	Category.AddCustomRow(LOCTEXT("ReplicateAllEvent", "Replicate All"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ReplicateAllEventLabel", "Replicate All"))
		.ToolTipText(ReplicateAllEventTooltip)
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.ToolTipText(ReplicateAllEventTooltip)
		.IsChecked_Lambda([this, EventNode]()
		{
			return GetBoolMetadata(EventNode, ReplicMetadata::EventEnabled, false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, EventNode](ECheckBoxState NewState)
		{
			SetBoolMetadata(EventNode, ReplicMetadata::EventEnabled, NewState == ECheckBoxState::Checked);
		})
	];

	Category.AddCustomRow(LOCTEXT("EventMode", "Event Mode"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("EventModeLabel", "Mode"))
		.ToolTipText(EventModeTooltip)
	]
	.ValueContent()
	[
		SNew(STextComboBox)
		.ToolTipText(EventModeTooltip)
		.OptionsSource(&ModeOptions)
		.InitiallySelectedItem(InitialMode)
		.OnSelectionChanged_Lambda([this, EventNode](TSharedPtr<FString> Selection, ESelectInfo::Type)
		{
			if (Selection.IsValid())
			{
				SetStringMetadata(EventNode, ReplicMetadata::EventMode, *Selection);
			}
		})
	];

	Category.AddCustomRow(LOCTEXT("EventPermission", "Permission"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("EventPermissionLabel", "Permission"))
		.ToolTipText(EventPermissionTooltip)
	]
	.ValueContent()
	[
		SNew(STextComboBox)
		.ToolTipText(EventPermissionTooltip)
		.OptionsSource(&PermissionOptions)
		.InitiallySelectedItem(InitialPermissionMode)
		.OnSelectionChanged_Lambda([this, EventNode](TSharedPtr<FString> Selection, ESelectInfo::Type)
		{
			if (Selection.IsValid())
			{
				SetStringMetadata(EventNode, ReplicMetadata::EventPermissionMode, *Selection);
				if (*Selection == TEXT("Custom"))
				{
					CreateCustomValidationFunction(EventNode);
				}
			}
		})
	];

	Category.AddCustomRow(LOCTEXT("CreateEventCustomValidation", "Create Custom Validation"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("CreateEventCustomValidationLabel", "Create Custom Validation Function"))
		.ToolTipText(LOCTEXT("CreateEventCustomValidationTooltip", "Creates or opens the matching custom validation function for this event.\n\nReplic will call CanReplicCall_<EventName>(RequestingActor) when Permission is set to Custom."))
		.OnClicked_Lambda([this, EventNode]()
		{
			return CreateCustomValidationFunction(EventNode);
		})
	];

	Category.AddCustomRow(LOCTEXT("CreateReplicCaller", "Create Replic Caller"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("CreateReplicCallerLabel", "Create Replic Caller Node"))
		.ToolTipText(LOCTEXT("CreateReplicCallerTooltip", "Adds a Replic Call Event node for this custom event to the currently focused graph."))
		.OnClicked_Lambda([this, EventNode]()
		{
			return CreateReplicCallerNode(EventNode);
		})
	];
}

UK2Node_CustomEvent* FReplicEventDetailsCustomization::ResolveSelectedEventNode(IDetailLayoutBuilder& DetailLayout) const
{
	for (const TWeakObjectPtr<UObject>& SelectedObject : DetailLayout.GetSelectedObjects())
	{
		if (UK2Node_CustomEvent* EventNode = Cast<UK2Node_CustomEvent>(SelectedObject.Get()))
		{
			return EventNode;
		}
	}

	if (TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin())
	{
		if (TSharedPtr<SMyBlueprint> MyBlueprint = Editor->GetMyBlueprintWidget())
		{
			if (FEdGraphSchemaAction_K2Event* EventAction = MyBlueprint->SelectionAsEvent())
			{
				return Cast<UK2Node_CustomEvent>(EventAction->NodeTemplate);
			}
		}
	}

	return nullptr;
}

bool FReplicEventDetailsCustomization::GetBoolMetadata(const UK2Node_CustomEvent* EventNode, const FName& Key, bool bDefaultValue) const
{
	if (!EventNode)
	{
		return bDefaultValue;
	}

	UK2Node_CustomEvent* MutableEventNode = const_cast<UK2Node_CustomEvent*>(EventNode);
	return MutableEventNode->GetUserDefinedMetaData().HasMetaData(Key)
		? MutableEventNode->GetUserDefinedMetaData().GetMetaData(Key).ToBool()
		: bDefaultValue;
}

FString FReplicEventDetailsCustomization::GetStringMetadata(const UK2Node_CustomEvent* EventNode, const FName& Key, const FString& DefaultValue) const
{
	if (!EventNode)
	{
		return DefaultValue;
	}

	UK2Node_CustomEvent* MutableEventNode = const_cast<UK2Node_CustomEvent*>(EventNode);
	return MutableEventNode->GetUserDefinedMetaData().HasMetaData(Key)
		? MutableEventNode->GetUserDefinedMetaData().GetMetaData(Key)
		: DefaultValue;
}

FReply FReplicEventDetailsCustomization::CreateReplicCallerNode(UK2Node_CustomEvent* EventNode) const
{
	TSharedPtr<FBlueprintEditor> Editor = ResolveBlueprintEditor(BlueprintEditor, EventNode ? EventNode->GetBlueprint() : nullptr);
	if (!Editor.IsValid() || !EventNode || !EventNode->GetBlueprint())
	{
		return FReply::Handled();
	}

	UEdGraph* TargetGraph = Editor->GetFocusedGraph();
	if (!TargetGraph && EventNode->GetBlueprint()->UbergraphPages.Num() > 0)
	{
		TargetGraph = EventNode->GetBlueprint()->UbergraphPages[0];
		Editor->OpenGraphAndBringToFront(TargetGraph);
	}

	if (!TargetGraph)
	{
		return FReply::Handled();
	}

	const FVector2D SpawnPosition(200.0, 260.0);
	UK2Node_ReplicCallEvent* NewNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ReplicCallEvent>(
		TargetGraph,
		SpawnPosition,
		EK2NewNodeFlags::SelectNewNode);

	if (NewNode)
	{
		if (UEdGraphPin* EventPin = NewNode->FindPin(TEXT("EventName")))
		{
			EventPin->DefaultValue = EventNode->GetFunctionName().ToString();
		}

		NewNode->ReconstructNode();
		Editor->AddToSelection(NewNode);
		Editor->JumpToNode(NewNode);
	}

	return FReply::Handled();
}

FReply FReplicEventDetailsCustomization::CreateCustomValidationFunction(UK2Node_CustomEvent* EventNode) const
{
	TSharedPtr<FBlueprintEditor> Editor = ResolveBlueprintEditor(BlueprintEditor, EventNode ? EventNode->GetBlueprint() : nullptr);
	if (!Editor.IsValid() || !EventNode || !EventNode->GetBlueprint())
	{
		return FReply::Handled();
	}

	if (UEdGraph* ValidationGraph = EnsureValidationFunctionGraph(EventNode->GetBlueprint(), MakeCustomEventValidationFunctionName(EventNode->GetFunctionName())))
	{
		Editor->OpenGraphAndBringToFront(ValidationGraph);
	}

	return FReply::Handled();
}

void FReplicEventDetailsCustomization::SetBoolMetadata(UK2Node_CustomEvent* EventNode, const FName& Key, bool bEnabled) const
{
	if (!EventNode)
	{
		return;
	}

	EventNode->Modify();
	if (bEnabled)
	{
		EventNode->GetUserDefinedMetaData().SetMetaData(Key, FString(TEXT("true")));
	}
	else
	{
		EventNode->GetUserDefinedMetaData().RemoveMetaData(Key);
	}

	SyncReplicEventMetadata(EventNode->GetBlueprint(), EventNode);
	RefreshBlueprint(EventNode);
}

void FReplicEventDetailsCustomization::SetStringMetadata(UK2Node_CustomEvent* EventNode, const FName& Key, const FString& Value) const
{
	if (!EventNode)
	{
		return;
	}

	EventNode->Modify();
	EventNode->GetUserDefinedMetaData().SetMetaData(Key, Value);
	SyncReplicEventMetadata(EventNode->GetBlueprint(), EventNode);
	RefreshBlueprint(EventNode);
}

void FReplicEventDetailsCustomization::RefreshBlueprint(UK2Node_CustomEvent* EventNode) const
{
	if (EventNode && EventNode->GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(EventNode->GetBlueprint());

		if (UEdGraph* Graph = EventNode->GetGraph())
		{
			Graph->NotifyNodeChanged(EventNode);
		}
	}

	if (TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin())
	{
		Editor->RefreshInspector();
	}
}

#undef LOCTEXT_NAMESPACE
