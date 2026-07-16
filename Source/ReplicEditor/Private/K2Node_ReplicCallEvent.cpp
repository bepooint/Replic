#include "K2Node_ReplicCallEvent.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "ReplicK2NodeUtils.h"
#include "ReplicLibrary.h"
#include "ReplicMetadata.h"
#include "ReplicPinOptionResolver.h"

#define LOCTEXT_NAMESPACE "ReplicCallEventNode"

namespace
{
	const FName ExecPinName(TEXT("Execute"));
	const FName ThenPinName(TEXT("Then"));
	const FName ContextObjectPinName(TEXT("ContextObject"));
	const FName TargetObjectPinName(TEXT("TargetObject"));
	const FName EventNamePinName(TEXT("EventName"));
	const FName SuccessPinName(TEXT("Success"));
bool UsesSelfAsTarget(const UEdGraphPin* TargetPin)
	{
		return TargetPin && TargetPin->LinkedTo.Num() == 0 && TargetPin->DefaultObject == nullptr && TargetPin->DefaultValue.IsEmpty();
	}

	bool IsReplicEnabledCustomEventNode(const UK2Node_CustomEvent* EventNode)
	{
		if (!EventNode)
		{
			return false;
		}

		const auto& Metadata = const_cast<UK2Node_CustomEvent*>(EventNode)->GetUserDefinedMetaData();
		return Metadata.HasMetaData(ReplicMetadata::EventEnabled) && Metadata.GetMetaData(ReplicMetadata::EventEnabled).ToBool();
	}

	bool TargetsCurrentBlueprint(const UK2Node_ReplicCallEvent* CallEventNode)
	{
		if (!CallEventNode)
		{
			return false;
		}

		const UEdGraphPin* TargetPin = CallEventNode->GetTargetObjectPin();
		if (UsesSelfAsTarget(TargetPin))
		{
			return true;
		}

		UClass* ResolvedTargetClass = nullptr;
		if (!ReplicPinOptionResolver::ResolveTargetClass(CallEventNode->GetEventNamePin(), ResolvedTargetClass) || !ResolvedTargetClass)
		{
			return false;
		}

		const UBlueprint* Blueprint = CallEventNode->GetBlueprint();
		return Blueprint
			&& (ResolvedTargetClass == Blueprint->SkeletonGeneratedClass.Get()
				|| ResolvedTargetClass == Blueprint->GeneratedClass.Get()
				|| ResolvedTargetClass->ClassGeneratedBy == Blueprint);
	}

	bool FindMarkedEventNodeOnCurrentBlueprint(const UK2Node_ReplicCallEvent* CallEventNode, const FName EventName, UK2Node_CustomEvent*& OutEventNode)
	{
		OutEventNode = nullptr;

		if (!CallEventNode || EventName.IsNone() || !TargetsCurrentBlueprint(CallEventNode))
		{
			return false;
		}

		UBlueprint* Blueprint = CallEventNode->GetBlueprint();
		if (!Blueprint)
		{
			return false;
		}

		TArray<UK2Node_CustomEvent*> EventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, EventNodes);
		for (UK2Node_CustomEvent* EventNode : EventNodes)
		{
			if (!EventNode || !IsReplicEnabledCustomEventNode(EventNode))
			{
				continue;
			}

			if (EventNode->CustomFunctionName == EventName || EventNode->GetFunctionName() == EventName)
			{
				OutEventNode = EventNode;
				return true;
			}
		}

		return false;
	}

	bool ResolveSelectedReplicEventDefinition(const UK2Node_ReplicCallEvent* CallEventNode, UFunction*& OutFunction, UK2Node_CustomEvent*& OutEventNode)
	{
		OutFunction = nullptr;
		OutEventNode = nullptr;

		const UEdGraphPin* EventPin = CallEventNode ? CallEventNode->GetEventNamePin() : nullptr;
		if (!EventPin)
		{
			return false;
		}

		const FName SelectedEventName(*EventPin->GetDefaultAsString());
		if (SelectedEventName.IsNone())
		{
			return false;
		}

		// The editor-side node is authoritative for events declared on the Blueprint currently being compiled. Generated
		// classes can still expose the old UFunction for one compile after a rename, which must not validate a stale call.
		if (FindMarkedEventNodeOnCurrentBlueprint(CallEventNode, SelectedEventName, OutEventNode))
		{
			return true;
		}

		if (ReplicPinOptionResolver::ResolveMarkedEvent(EventPin, SelectedEventName, OutFunction))
		{
			if (TargetsCurrentBlueprint(CallEventNode))
			{
				const UBlueprint* Blueprint = CallEventNode->GetBlueprint();
				const UClass* FunctionOwner = OutFunction ? OutFunction->GetOwnerClass() : nullptr;
				if (Blueprint && FunctionOwner
					&& (FunctionOwner == Blueprint->SkeletonGeneratedClass.Get()
						|| FunctionOwner == Blueprint->GeneratedClass.Get()
						|| FunctionOwner->ClassGeneratedBy == Blueprint))
				{
					OutFunction = nullptr;
					return false;
				}
			}

			ReplicPinOptionResolver::ResolveMarkedEventNode(EventPin, SelectedEventName, OutEventNode);
			if (OutEventNode && !IsReplicEnabledCustomEventNode(OutEventNode))
			{
				OutEventNode = nullptr;
			}

			return true;
		}

		if (ReplicPinOptionResolver::ResolveMarkedEventNode(EventPin, SelectedEventName, OutEventNode) && IsReplicEnabledCustomEventNode(OutEventNode))
		{
			return true;
		}

		return false;
	}

	void GatherExpectedEventPins(const UFunction* SelectedEvent, const UK2Node_CustomEvent* SelectedEventNode, TArray<FUserPinInfo>& OutPins)
	{
		OutPins.Reset();

		if (SelectedEventNode)
		{
			// Prefer the editor-side custom event definition because it tracks uncompiled pin edits more accurately than the
			// runtime UFunction while the user is still modifying the event signature.
			ReplicK2NodeUtils::GatherCustomEventParameterDefinitions(SelectedEventNode, OutPins);
			if (OutPins.Num() > 0)
			{
				return;
			}
		}

		TArray<const FProperty*> EventInputs;
		ReplicK2NodeUtils::GatherEventInputProperties(SelectedEvent, EventInputs);
		for (const FProperty* InputProperty : EventInputs)
		{
			FEdGraphPinType PinType;
			if (!ReplicK2NodeUtils::ConvertPropertyToPinType(InputProperty, PinType))
			{
				continue;
			}

			FUserPinInfo& ExpectedPin = OutPins.AddDefaulted_GetRef();
			ExpectedPin.PinName = InputProperty->GetFName();
			ExpectedPin.PinType = PinType;
			ExpectedPin.DesiredPinDirection = EGPD_Output;
		}
	}

	void MovePinLinksOrCopyDefaults(FKismetCompilerContext& CompilerContext, UEdGraphPin* SourcePin, UEdGraphPin* TargetPin, bool bPreserveTargetDefaultsWhenSourceUnset = false)
	{
		if (!SourcePin || !TargetPin)
		{
			return;
		}

		if (SourcePin->LinkedTo.Num() > 0)
		{
			CompilerContext.MovePinLinksToIntermediate(*SourcePin, *TargetPin);
			return;
		}

		const bool bSourceHasExplicitDefault =
			!SourcePin->DefaultValue.IsEmpty()
			|| !SourcePin->AutogeneratedDefaultValue.IsEmpty()
			|| SourcePin->DefaultObject != nullptr
			|| !SourcePin->DefaultTextValue.IsEmpty();
		if (!bSourceHasExplicitDefault && bPreserveTargetDefaultsWhenSourceUnset)
		{
			return;
		}

		TargetPin->DefaultValue = SourcePin->DefaultValue;
		TargetPin->AutogeneratedDefaultValue = SourcePin->AutogeneratedDefaultValue;
		TargetPin->DefaultObject = SourcePin->DefaultObject;
		TargetPin->DefaultTextValue = SourcePin->DefaultTextValue;
	}

	void MoveTargetPinLinksOrConnectSelf(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node* SourceNode, UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
	{
		if (!SourcePin || !TargetPin)
		{
			return;
		}

		if (!UsesSelfAsTarget(SourcePin))
		{
			MovePinLinksOrCopyDefaults(CompilerContext, SourcePin, TargetPin);
			return;
		}

		UK2Node_Self* SelfNode = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(SourceNode, SourceGraph);
		SelfNode->AllocateDefaultPins();
		CompilerContext.GetSchema()->TryCreateConnection(SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self), TargetPin);
	}

	void GatherArgumentPinsFromList(const TArray<UEdGraphPin*>& SourcePins, TArray<UEdGraphPin*>& OutPins)
	{
		OutPins.Reset();

		for (UEdGraphPin* Pin : SourcePins)
		{
			if (!Pin || Pin->Direction != EGPD_Input)
			{
				continue;
			}

			if (Pin->ParentPin != nullptr)
			{
				continue;
			}

			if (Pin->PinName == ExecPinName
				|| Pin->PinName == ContextObjectPinName
				|| Pin->PinName == TargetObjectPinName
				|| Pin->PinName == EventNamePinName)
			{
				continue;
			}

			OutPins.Add(Pin);
		}
	}

	bool IsStaticPinName(const FName PinName)
	{
		return PinName == ExecPinName
			|| PinName == ThenPinName
			|| PinName == ContextObjectPinName
			|| PinName == TargetObjectPinName
			|| PinName == EventNamePinName
			|| PinName == SuccessPinName;
	}

	bool IsTopLevelArgumentPin(const UEdGraphPin* Pin)
	{
		return Pin
			&& Pin->ParentPin == nullptr
			&& !IsStaticPinName(Pin->PinName);
	}

	bool IsLegacyArgumentDirectionMismatch(const UEdGraphPin* NewPin, const UEdGraphPin* OldPin, const UEdGraphSchema_K2* Schema)
	{
		if (!NewPin || !OldPin || !Schema)
		{
			return false;
		}

		if (!IsTopLevelArgumentPin(NewPin) || !IsTopLevelArgumentPin(OldPin))
		{
			return false;
		}

		if (NewPin->PinName != OldPin->PinName || NewPin->Direction != EGPD_Input || OldPin->Direction != EGPD_Output)
		{
			return false;
		}

		return Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType)
			|| Schema->ArePinTypesCompatible(NewPin->PinType, OldPin->PinType);
	}

	FName ResolveNamedValueBuilderFunctionName(const UEdGraphPin* SourceArgumentPin)
	{
		if (!SourceArgumentPin)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedGenericValue);
		}

		switch (SourceArgumentPin->PinType.ContainerType)
		{
		case EPinContainerType::Array:
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedArrayValue);
		case EPinContainerType::Set:
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedSetValue);
		case EPinContainerType::Map:
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedMapValue);
		default:
			break;
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedStructValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedBoolValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedIntValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedFloatValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedNameValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedStringValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedTextValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
			|| SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedObjectValue);
		}

		if (SourceArgumentPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
		{
			return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedClassValue);
		}

		return GET_FUNCTION_NAME_CHECKED(UReplicLibrary, MakeNamedGenericValue);
	}

}

FName UK2Node_ReplicCallEvent::GetExecPinName()
{
	return ExecPinName;
}

UK2Node_ReplicCallEvent::UK2Node_ReplicCallEvent()
{
	OrphanedPinSaveMode = ESaveOrphanPinMode::SaveNone;
	bDisableOrphanPinSaving = true;
}

FName UK2Node_ReplicCallEvent::GetThenPinName()
{
	return ThenPinName;
}

FName UK2Node_ReplicCallEvent::GetContextObjectPinName()
{
	return ContextObjectPinName;
}

FName UK2Node_ReplicCallEvent::GetTargetObjectPinName()
{
	return TargetObjectPinName;
}

FName UK2Node_ReplicCallEvent::GetEventNamePinName()
{
	return EventNamePinName;
}

FName UK2Node_ReplicCallEvent::GetSuccessPinName()
{
	return SuccessPinName;
}

UEdGraphPin* UK2Node_ReplicCallEvent::GetExecPin() const
{
	return FindPinChecked(ExecPinName);
}

UEdGraphPin* UK2Node_ReplicCallEvent::GetThenPin() const
{
	return FindPinChecked(ThenPinName);
}

UEdGraphPin* UK2Node_ReplicCallEvent::GetContextObjectPin() const
{
	return FindPinChecked(ContextObjectPinName);
}

UEdGraphPin* UK2Node_ReplicCallEvent::GetTargetObjectPin() const
{
	return FindPinChecked(TargetObjectPinName);
}

UEdGraphPin* UK2Node_ReplicCallEvent::GetEventNamePin() const
{
	return FindPinChecked(EventNamePinName);
}

UEdGraphPin* UK2Node_ReplicCallEvent::GetSuccessPin() const
{
	return FindPinChecked(SuccessPinName);
}

void UK2Node_ReplicCallEvent::PostLoad()
{
	Super::PostLoad();
	RemoveInvalidLegacyLinks();
	RemoveInvalidLegacyPins();
	RemoveOrphanedPins();
}

void UK2Node_ReplicCallEvent::HandleReplicSelectionChanged(FName ChangedPinName)
{
	if (ChangedPinName != EventNamePinName || bIsRefreshingPins)
	{
		return;
	}

	ReconstructNode();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
}

void UK2Node_ReplicCallEvent::AllocateDefaultPins()
{
	AllocateStaticPins();
	ConfigureDynamicPins();

	Super::AllocateDefaultPins();
}

void UK2Node_ReplicCallEvent::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	AllocateStaticPins();
	ApplyStaticPinToolTips();

	UEdGraphPin* SourceEventPin = nullptr;
	for (UEdGraphPin* OldPin : OldPins)
	{
		if (OldPin && OldPin->PinName == EventNamePinName && OldPin->Direction == EGPD_Input)
		{
			SourceEventPin = OldPin;
			break;
		}
	}

	if (SourceEventPin)
	{
		if (UEdGraphPin* NewEventPin = FindPin(EventNamePinName))
		{
			NewEventPin->DefaultValue = SourceEventPin->DefaultValue;
			NewEventPin->AutogeneratedDefaultValue = SourceEventPin->AutogeneratedDefaultValue;
			NewEventPin->DefaultObject = SourceEventPin->DefaultObject;
			NewEventPin->DefaultTextValue = SourceEventPin->DefaultTextValue;
		}
	}

	RefreshArgumentPins(&OldPins, SourceEventPin);
	RestoreSplitPins(OldPins);
}

void UK2Node_ReplicCallEvent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UFunction* SelectedEvent = nullptr;
	UK2Node_CustomEvent* SelectedEventNode = nullptr;
	if (!ResolveSelectedReplicEventDefinition(this, SelectedEvent, SelectedEventNode))
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("MissingReplicEvent", "Replic: No valid Replic event is selected for @@").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, CallMarkedEvent)));
	CallNode->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *CallNode->GetThenPin());
	MovePinLinksOrCopyDefaults(CompilerContext, GetContextObjectPin(), CallNode->FindPinChecked(ContextObjectPinName), true);
	MoveTargetPinLinksOrConnectSelf(CompilerContext, SourceGraph, this, GetTargetObjectPin(), CallNode->FindPinChecked(TargetObjectPinName));
	MovePinLinksOrCopyDefaults(CompilerContext, GetEventNamePin(), CallNode->FindPinChecked(EventNamePinName));
	CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *CallNode->GetReturnValuePin());

	const TArray<UEdGraphPin*> ArgumentPins = GetArgumentPins();
	TArray<UK2Node_CallFunction*> NamedValueNodes;
	TArray<UK2Node_CallFunction*> ExecutableNamedValueNodes;
	if (ArgumentPins.Num() > 0)
	{
		// The runtime call still takes a serialized named-value array. This node keeps the Blueprint UX ergonomic by
		// expanding the selected event signature into direct pins and packing them back into named values during compile.
		UK2Node_MakeArray* MakeArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
		MakeArrayNode->AllocateDefaultPins();

		TArray<UEdGraphPin*> MakeArrayInputPins;
		for (UEdGraphPin* Pin : MakeArrayNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				MakeArrayInputPins.Add(Pin);
			}
		}

		while (MakeArrayInputPins.Num() < ArgumentPins.Num())
		{
			MakeArrayNode->AddInputPin();
			MakeArrayInputPins.Reset();
			for (UEdGraphPin* Pin : MakeArrayNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
				{
					MakeArrayInputPins.Add(Pin);
				}
			}
		}

		FEdGraphPinType NamedValuePinType;
		NamedValuePinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		NamedValuePinType.PinSubCategoryObject = FReplicNamedValue::StaticStruct();

		for (UEdGraphPin* Pin : MakeArrayInputPins)
		{
			Pin->PinType = NamedValuePinType;
		}

		UEdGraphPin* MakeArrayOutputPin = nullptr;
		for (UEdGraphPin* Pin : MakeArrayNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				MakeArrayOutputPin = Pin;
				break;
			}
		}

		if (MakeArrayOutputPin)
		{
			MakeArrayOutputPin->PinType = NamedValuePinType;
			MakeArrayOutputPin->PinType.ContainerType = EPinContainerType::Array;
		}

		for (int32 ArgumentIndex = 0; ArgumentIndex < ArgumentPins.Num(); ++ArgumentIndex)
		{
			UEdGraphPin* SourceArgumentPin = ArgumentPins[ArgumentIndex];
			if (!SourceArgumentPin || !MakeArrayInputPins.IsValidIndex(ArgumentIndex))
			{
				continue;
			}

			UK2Node_CallFunction* NamedValueNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NamedValueNode->SetFromFunction(UReplicLibrary::StaticClass()->FindFunctionByName(ResolveNamedValueBuilderFunctionName(SourceArgumentPin)));
			NamedValueNode->AllocateDefaultPins();

			UEdGraphPin* NamePin = NamedValueNode->FindPinChecked(TEXT("Name"));
			NamePin->DefaultValue = SourceArgumentPin->PinName.ToString();

			UEdGraphPin* ValuePin = NamedValueNode->FindPinChecked(TEXT("Value"));
			ValuePin->PinType = SourceArgumentPin->PinType;

			MovePinLinksOrCopyDefaults(CompilerContext, SourceArgumentPin, ValuePin);
			CompilerContext.GetSchema()->TryCreateConnection(NamedValueNode->GetReturnValuePin(), MakeArrayInputPins[ArgumentIndex]);
			NamedValueNodes.Add(NamedValueNode);
			if (NamedValueNode->GetExecPin() && NamedValueNode->GetThenPin())
			{
				ExecutableNamedValueNodes.Add(NamedValueNode);
			}
		}

		if (MakeArrayOutputPin)
		{
			CompilerContext.GetSchema()->TryCreateConnection(MakeArrayOutputPin, CallNode->FindPinChecked(TEXT("Arguments")));
		}
	}

	if (ExecutableNamedValueNodes.Num() > 0)
	{
		CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *ExecutableNamedValueNodes[0]->GetExecPin());
		for (int32 NodeIndex = 0; NodeIndex < ExecutableNamedValueNodes.Num(); ++NodeIndex)
		{
			UEdGraphPin* SourceThenPin = ExecutableNamedValueNodes[NodeIndex]->GetThenPin();
			UEdGraphPin* TargetExecPin = ExecutableNamedValueNodes.IsValidIndex(NodeIndex + 1)
				? ExecutableNamedValueNodes[NodeIndex + 1]->GetExecPin()
				: CallNode->GetExecPin();
			CompilerContext.GetSchema()->TryCreateConnection(SourceThenPin, TargetExecPin);
		}
	}
	else
	{
		CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *CallNode->GetExecPin());
	}

	BreakAllNodeLinks();
}

void UK2Node_ReplicCallEvent::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	RebuildPinsIfNeeded(Pin);
}

void UK2Node_ReplicCallEvent::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	RebuildPinsIfNeeded(Pin);
}

FText UK2Node_ReplicCallEvent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FText ResolvedName = GetResolvedDisplayName();
	return ResolvedName.IsEmpty()
		? LOCTEXT("ReplicCallEventTitle", "Replic Call Event")
		: FText::Format(LOCTEXT("ReplicCallEventSelectedTitle", "Replic Call {0}"), ResolvedName);
}

FText UK2Node_ReplicCallEvent::GetTooltipText() const
{
	return LOCTEXT("ReplicCallEventTooltip", "Calls a Replic-marked custom event with auto-generated argument pins.\n\nSelect the event first, then this node exposes the event parameters directly.");
}

FText UK2Node_ReplicCallEvent::GetMenuCategory() const
{
	return LOCTEXT("ReplicCategory", "Replic");
}

void UK2Node_ReplicCallEvent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}

	UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
	check(Spawner);
	ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
}

void UK2Node_ReplicCallEvent::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	const UEdGraphPin* EventPin = GetEventNamePin();
	const FName SelectedEventName(*EventPin->GetDefaultAsString());
	if (SelectedEventName.IsNone())
	{
		MessageLog.Error(TEXT("Replic: No Replic event is selected for @@. Choose a Replic-enabled custom event in the EventName dropdown."), this);
		return;
	}

	UFunction* SelectedEvent = nullptr;
	UK2Node_CustomEvent* SelectedEventNode = nullptr;
	if (!ResolveSelectedReplicEventDefinition(this, SelectedEvent, SelectedEventNode))
	{
		UClass* TargetClass = nullptr;
		if (!ReplicPinOptionResolver::ResolveTargetClass(EventPin, TargetClass))
		{
			MessageLog.Warning(TEXT("Replic: TargetObject could not be resolved to a concrete Blueprint class for @@. Connect a concrete actor/component reference or compile after the TargetObject type is known."), this);
			return;
		}

		MessageLog.Error(
			*FString::Printf(TEXT("Replic: '%s' is not a Replic-marked event on target class '%s' for @@"), *SelectedEventName.ToString(), *TargetClass->GetName()),
			this);
		return;
	}

	TArray<FUserPinInfo> ExpectedPins;
	GatherExpectedEventPins(SelectedEvent, SelectedEventNode, ExpectedPins);
	for (const FUserPinInfo& ExpectedPin : ExpectedPins)
	{
		if (ExpectedPin.PinName == NAME_None)
		{
			continue;
		}

		const UEdGraphPin* ArgumentPin = FindPin(ExpectedPin.PinName);
		if (!ArgumentPin)
		{
			MessageLog.Error(
				*FString::Printf(TEXT("Replic: Missing argument pin '%s' for @@"), *ExpectedPin.PinName.ToString()),
				this);
			return;
		}

		if (!GetDefault<UEdGraphSchema_K2>()->ArePinTypesCompatible(ArgumentPin->PinType, ExpectedPin.PinType))
		{
			MessageLog.Error(
				*FString::Printf(TEXT("Replic: Argument pin '%s' does not match the selected event signature for @@"), *ExpectedPin.PinName.ToString()),
				this);
		}
	}

	if (const UBlueprint* Blueprint = GetBlueprint())
	{
		FString ContextWarning;
		if (ReplicK2NodeUtils::BuildContextObjectWarning(Blueprint, GetContextObjectPin(), ContextWarning))
		{
			MessageLog.Warning(*FString::Printf(TEXT("Replic: %s for @@"), *ContextWarning), this);
		}
	}
}

void UK2Node_ReplicCallEvent::PostReconstructNode()
{
	RemoveInvalidLegacyPins();
	RemoveOrphanedPins();
	Super::PostReconstructNode();
}

UK2Node::ERedirectType UK2Node_ReplicCallEvent::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	if (const ERedirectType BaseRedirect = Super::DoPinsMatchForReconstruction(NewPin, NewPinIndex, OldPin, OldPinIndex);
		BaseRedirect != ERedirectType_None)
	{
		return BaseRedirect;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	return IsLegacyArgumentDirectionMismatch(NewPin, OldPin, Schema)
		? ERedirectType_Name
		: ERedirectType_None;
}

TArray<UEdGraphPin*> UK2Node_ReplicCallEvent::GetArgumentPins() const
{
	TArray<UEdGraphPin*> Result;
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input)
		{
			continue;
		}

		if (Pin->ParentPin != nullptr)
		{
			continue;
		}

		if (Pin->PinName == ExecPinName
			|| Pin->PinName == ContextObjectPinName
			|| Pin->PinName == TargetObjectPinName
			|| Pin->PinName == EventNamePinName)
		{
			continue;
		}

		Result.Add(Pin);
	}

	return Result;
}

void UK2Node_ReplicCallEvent::RebuildPinsIfNeeded(UEdGraphPin* ChangedPin)
{
	if (bIsRefreshingPins || !ChangedPin)
	{
		return;
	}

	if (ChangedPin->PinName == EventNamePinName)
	{
		ReconstructNode();
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyNodeChanged(this);
		}
	}
	else if (ChangedPin->PinName == TargetObjectPinName)
	{
		// Do not reconstruct from TargetObject link changes. Users commonly connect Blueprint Self here, and rebuilding
		// while the editor is still settling that object-pin connection can destabilize the graph. EventName changes still
		// rebuild the dynamic argument pins; if the target class changes, validation catches stale event selections.
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyNodeChanged(this);
		}
	}
}

void UK2Node_ReplicCallEvent::ConfigureDynamicPins()
{
	ApplyStaticPinToolTips();
	RefreshArgumentPins();
	RemoveInvalidLegacyLinks();
	RemoveInvalidLegacyPins();
	RemoveOrphanedPins();
}

void UK2Node_ReplicCallEvent::RemoveInvalidLegacyLinks()
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin)
		{
			continue;
		}

		TArray<UEdGraphPin*> LinkedPins = Pin->LinkedTo;
		for (UEdGraphPin* LinkedPin : LinkedPins)
		{
			if (LinkedPin && LinkedPin->GetOwningNodeUnchecked() && LinkedPin->GetOwningNode()->Pins.Contains(LinkedPin))
			{
				continue;
			}

			Pin->LinkedTo.Remove(LinkedPin);
		}
	}
}

void UK2Node_ReplicCallEvent::RemoveInvalidLegacyPins()
{
	const UEdGraphSchema* Schema = GetSchema();
	TSet<FName> SeenArgumentPins;

	for (int32 PinIndex = Pins.Num() - 1; PinIndex >= 0; --PinIndex)
	{
		UEdGraphPin* Pin = Pins[PinIndex];
		if (!Pin || Pin->ParentPin != nullptr || Pin->bOrphanedPin)
		{
			continue;
		}

		bool bRemovePin = false;
		if (Pin->PinName == ExecPinName)
		{
			bRemovePin = Pin->Direction != EGPD_Input || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec;
		}
		else if (Pin->PinName == ThenPinName)
		{
			bRemovePin = Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec;
		}
		else if (Pin->PinName == SuccessPinName)
		{
			bRemovePin = Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Boolean;
		}
		else if (IsStaticPinName(Pin->PinName))
		{
			bRemovePin = Pin->Direction != EGPD_Input;
		}
		else
		{
			if (Pin->Direction != EGPD_Input)
			{
				bRemovePin = true;
			}
			else if (SeenArgumentPins.Contains(Pin->PinName))
			{
				bRemovePin = true;
			}
			else
			{
				SeenArgumentPins.Add(Pin->PinName);
			}
		}

		if (!bRemovePin)
		{
			continue;
		}

		if (Pin->SubPins.Num() > 0 && Schema)
		{
			Schema->RecombinePin(Pin);
		}

		Pin->BreakAllPinLinks();
		RemovePin(Pin);
	}
}

void UK2Node_ReplicCallEvent::RemoveOrphanedPins()
{
	bool bRemovedPin = false;
	do
	{
		bRemovedPin = false;

		for (int32 PinIndex = Pins.Num() - 1; PinIndex >= 0; --PinIndex)
		{
			UEdGraphPin* Pin = Pins[PinIndex];
			if (!Pin || !Pin->bOrphanedPin)
			{
				continue;
			}

			Pin->BreakAllPinLinks();
			RemovePin(Pin);
			bRemovedPin = true;
		}
	}
	while (bRemovedPin);
}

void UK2Node_ReplicCallEvent::AllocateStaticPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, ExecPinName);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, ThenPinName);

	UEdGraphPin* ContextPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), ContextObjectPinName);
	ContextPin->bAdvancedView = true;

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), TargetObjectPinName);
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, EventNamePinName);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, SuccessPinName);
}

void UK2Node_ReplicCallEvent::ApplyStaticPinToolTips()
{
	if (UEdGraphPin* EventPin = FindPin(EventNamePinName))
	{
		ReplicK2NodeUtils::SetPinToolTip(EventPin, TEXT("Choose the Replic-marked custom event to call.\n\nThe list is filtered by the connected TargetObject when the editor can resolve it.\n\nIf the event belongs to another Blueprint, connect that object first."));
	}

	if (UEdGraphPin* TargetPin = FindPin(TargetObjectPinName))
	{
		ReplicK2NodeUtils::SetPinToolTip(TargetPin, TEXT("Object that owns the Replic-marked custom event.\n\nLeave this unconnected only when the event is on Self or in the current Blueprint.\n\nIf the event is on another actor or component, connect that object here."));
	}

	if (UEdGraphPin* ContextPin = FindPin(ContextObjectPinName))
	{
		ReplicK2NodeUtils::SetPinToolTip(ContextPin, TEXT("Object used to identify who is making the network request.\n\nUsually this should be Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent."));
	}
}

void UK2Node_ReplicCallEvent::RefreshArgumentPins(const TArray<UEdGraphPin*>* PinsToPreserve, const UEdGraphPin* SourceEventPin)
{
	if (bIsRefreshingPins)
	{
		return;
	}

	TGuardValue<bool> RefreshGuard(bIsRefreshingPins, true);

	TArray<UEdGraphPin*> OldArgumentPins;
	if (PinsToPreserve)
	{
		GatherArgumentPinsFromList(*PinsToPreserve, OldArgumentPins);
	}
	else
	{
		OldArgumentPins = GetArgumentPins();
	}

	for (UEdGraphPin* ExistingArgumentPin : GetArgumentPins())
	{
		if (!ExistingArgumentPin)
		{
			continue;
		}

		if (ExistingArgumentPin->SubPins.Num() > 0)
		{
			if (const UEdGraphSchema* Schema = GetSchema())
			{
				Schema->RecombinePin(ExistingArgumentPin);
			}
		}

		ExistingArgumentPin->BreakAllPinLinks();
		RemovePin(ExistingArgumentPin);
	}

	UFunction* SelectedEvent = nullptr;
	if (const UEdGraphPin* EventNamePin = SourceEventPin ? SourceEventPin : FindPin(EventNamePinName))
	{
		ReplicPinOptionResolver::ResolveMarkedEvent(EventNamePin, FName(*EventNamePin->GetDefaultAsString()), SelectedEvent);
	}

	UK2Node_CustomEvent* SelectedEventNode = nullptr;
	if (const UEdGraphPin* EventNamePin = SourceEventPin ? SourceEventPin : FindPin(EventNamePinName))
	{
		const FName SelectedEventName(*EventNamePin->GetDefaultAsString());
		if (!(ReplicPinOptionResolver::ResolveMarkedEventNode(EventNamePin, SelectedEventName, SelectedEventNode) && IsReplicEnabledCustomEventNode(SelectedEventNode)))
		{
			SelectedEventNode = nullptr;
			FindMarkedEventNodeOnCurrentBlueprint(this, SelectedEventName, SelectedEventNode);
		}
	}

	TArray<FUserPinInfo> EventParameterPins;
	GatherExpectedEventPins(SelectedEvent, SelectedEventNode, EventParameterPins);
	for (const FUserPinInfo& SourcePin : EventParameterPins)
	{
		if (SourcePin.PinName == NAME_None)
		{
			continue;
		}

		UEdGraphPin* ArgumentPin = CreatePin(EGPD_Input, SourcePin.PinType, SourcePin.PinName);
		ReplicK2NodeUtils::SetPinToolTip(ArgumentPin, FString::Printf(TEXT("Replic argument for event parameter '%s'."), *SourcePin.PinName.ToString()));
	}
}

UFunction* UK2Node_ReplicCallEvent::ResolveSelectedEvent() const
{
	if (const UEdGraphPin* EventPin = FindPin(EventNamePinName))
	{
		const FName SelectedEventName(*EventPin->GetDefaultAsString());
		UFunction* Function = nullptr;
		if (ReplicPinOptionResolver::ResolveMarkedEvent(EventPin, SelectedEventName, Function))
		{
			return Function;
		}
	}

	return nullptr;
}

FText UK2Node_ReplicCallEvent::GetResolvedDisplayName() const
{
	if (const UEdGraphPin* EventPin = FindPin(EventNamePinName))
	{
		const FName SelectedEventName(*EventPin->GetDefaultAsString());
		if (!SelectedEventName.IsNone())
		{
			return FText::FromName(SelectedEventName);
		}
	}

	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
