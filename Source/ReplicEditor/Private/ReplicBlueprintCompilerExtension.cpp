#include "ReplicBlueprintCompilerExtension.h"

#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ReplicCallNodeValidation.h"
#include "ReplicK2NodeUtils.h"
#include "ReplicMetadata.h"
#include "ReplicTransportComponent.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"

namespace
{
	bool IsEnabledReplicVariable(const UBlueprint* Blueprint, FName VariableName)
	{
		if (!Blueprint || VariableName.IsNone())
		{
			return false;
		}

		FString MetadataValue;
		return FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, ReplicMetadata::VariableEnabled, MetadataValue)
			&& MetadataValue.ToBool();
	}

	bool GetBoolVariableMetadata(const UBlueprint* Blueprint, FName VariableName, const TCHAR* Key, bool bDefaultValue)
	{
		FString MetadataValue;
		return FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, Key, MetadataValue)
			? MetadataValue.ToBool()
			: bDefaultValue;
	}

	FString GetStringVariableMetadata(const UBlueprint* Blueprint, FName VariableName, const TCHAR* Key, const FString& DefaultValue)
	{
		FString MetadataValue;
		return FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, Key, MetadataValue)
			? MetadataValue
			: DefaultValue;
	}

	EReplicPermissionMode ParsePermissionMode(const FString& Value)
	{
		if (Value == TEXT("OwnerOnly"))
		{
			return EReplicPermissionMode::OwnerOnly;
		}
		if (Value == TEXT("ServerOnly"))
		{
			return EReplicPermissionMode::ServerOnly;
		}
		if (Value == TEXT("Custom"))
		{
			return EReplicPermissionMode::Custom;
		}
		return EReplicPermissionMode::None;
	}

	EReplicEventMode ParseEventMode(const FString& Value)
	{
		if (Value == TEXT("LocalOnly"))
		{
			return EReplicEventMode::LocalOnly;
		}
		if (Value == TEXT("ServerOnly"))
		{
			return EReplicEventMode::ServerOnly;
		}
		if (Value == TEXT("OwnerOnly"))
		{
			return EReplicEventMode::OwnerOnly;
		}
		return EReplicEventMode::ReplicateAll;
	}

	FReplicVariableSettings MakeVariableSettings(const UBlueprint* Blueprint, FName VariableName)
	{
		FReplicVariableSettings Settings;
		Settings.bReplicateAll = GetBoolVariableMetadata(Blueprint, VariableName, ReplicMetadata::VariableEnabled, false);
		Settings.bPersistentState = GetBoolVariableMetadata(Blueprint, VariableName, ReplicMetadata::VariablePersistent, true);
		Settings.bUseBatching = GetBoolVariableMetadata(Blueprint, VariableName, ReplicMetadata::VariableBatching, false);
		Settings.BatchIntervalSeconds = FCString::Atof(*GetStringVariableMetadata(Blueprint, VariableName, ReplicMetadata::VariableBatchInterval, TEXT("0.0")));
		Settings.PermissionMode = ParsePermissionMode(GetStringVariableMetadata(Blueprint, VariableName, ReplicMetadata::VariablePermissionMode, TEXT("None")));
		return Settings;
	}

	FReplicEventSettings MakeEventSettings(UK2Node_CustomEvent* EventNode)
	{
		FReplicEventSettings Settings;
		if (!EventNode)
		{
			return Settings;
		}

		const auto& Metadata = EventNode->GetUserDefinedMetaData();
		Settings.bReplicateAll = Metadata.HasMetaData(ReplicMetadata::EventEnabled)
			&& Metadata.GetMetaData(ReplicMetadata::EventEnabled).ToBool();
		Settings.PermissionMode = Metadata.HasMetaData(ReplicMetadata::EventPermissionMode)
			? ParsePermissionMode(Metadata.GetMetaData(ReplicMetadata::EventPermissionMode))
			: EReplicPermissionMode::None;
		Settings.Mode = Metadata.HasMetaData(ReplicMetadata::EventMode)
			? ParseEventMode(Metadata.GetMetaData(ReplicMetadata::EventMode))
			: EReplicEventMode::ReplicateAll;
		return Settings;
	}

	UReplicTransportComponent* FindBlueprintReplicTransportTemplate(const UBlueprint* Blueprint)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			return nullptr;
		}

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf<UReplicTransportComponent>())
			{
				return Cast<UReplicTransportComponent>(Node->ComponentTemplate);
			}
		}

		return nullptr;
	}

	void WriteRuntimeReplicDefinitions(UBlueprint* Blueprint)
	{
		UReplicTransportComponent* Transport = FindBlueprintReplicTransportTemplate(Blueprint);
		if (!Transport)
		{
			return;
		}

		FReplicTargetDescriptor ActorTarget;
		ActorTarget.Kind = EReplicTargetKind::Actor;

		Transport->MarkedVariableDefinitions.Reset();
		for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
		{
			const FReplicVariableSettings Settings = MakeVariableSettings(Blueprint, VariableDescription.VarName);
			if (!Settings.bReplicateAll)
			{
				continue;
			}

			FReplicMarkedVariableDefinition Definition;
			Definition.Target = ActorTarget;
			Definition.PropertyName = VariableDescription.VarName;
			Definition.Settings = Settings;
			Transport->MarkedVariableDefinitions.Add(Definition);
		}

		Transport->MarkedEventDefinitions.Reset();
		TArray<UK2Node_CustomEvent*> EventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, EventNodes);
		for (UK2Node_CustomEvent* EventNode : EventNodes)
		{
			const FReplicEventSettings Settings = MakeEventSettings(EventNode);
			if (!Settings.bReplicateAll)
			{
				continue;
			}

			FReplicMarkedEventDefinition Definition;
			Definition.Target = ActorTarget;
			Definition.EventName = EventNode->GetFunctionName();
			Definition.Settings = Settings;
			Transport->MarkedEventDefinitions.Add(Definition);
		}
	}

	void GatherEnabledReplicVariables(const UBlueprint* Blueprint, TArray<FName>& OutVariableNames)
	{
		OutVariableNames.Reset();
		if (!Blueprint)
		{
			return;
		}

		for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
		{
			if (IsEnabledReplicVariable(Blueprint, VariableDescription.VarName))
			{
				OutVariableNames.Add(VariableDescription.VarName);
			}
		}
	}

	bool IsEnabledReplicEventNode(UK2Node_CustomEvent* EventNode)
	{
		if (!EventNode)
		{
			return false;
		}

		const auto& Metadata = EventNode->GetUserDefinedMetaData();
		return Metadata.HasMetaData(ReplicMetadata::EventEnabled) && Metadata.GetMetaData(ReplicMetadata::EventEnabled).ToBool();
	}

	void GatherEnabledReplicEvents(const UBlueprint* Blueprint, TArray<FName>& OutEventNames)
	{
		OutEventNames.Reset();
		if (!Blueprint)
		{
			return;
		}

		TArray<UK2Node_CustomEvent*> EventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, EventNodes);
		for (UK2Node_CustomEvent* EventNode : EventNodes)
		{
			if (IsEnabledReplicEventNode(EventNode))
			{
				OutEventNames.AddUnique(EventNode->GetFunctionName());
			}
		}
	}

	FString JoinNames(const TArray<FName>& Names)
	{
		TArray<FString> NameStrings;
		NameStrings.Reserve(Names.Num());
		for (const FName Name : Names)
		{
			NameStrings.Add(Name.ToString());
		}

		return FString::Join(NameStrings, TEXT(", "));
	}
}

void UReplicBlueprintCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	if (!CompilationContext.Blueprint)
	{
		return;
	}

	if (!ReplicK2NodeUtils::HasReplicTransportComponent(CompilationContext.Blueprint))
	{
		TArray<FName> EnabledVariables;
		TArray<FName> EnabledEvents;
		GatherEnabledReplicVariables(CompilationContext.Blueprint, EnabledVariables);
		GatherEnabledReplicEvents(CompilationContext.Blueprint, EnabledEvents);

		if (EnabledVariables.Num() > 0 || EnabledEvents.Num() > 0)
		{
			FString DetailMessage;
			if (EnabledVariables.Num() > 0)
			{
				DetailMessage += FString::Printf(TEXT("Variables: %s"), *JoinNames(EnabledVariables));
			}

			if (EnabledEvents.Num() > 0)
			{
				if (!DetailMessage.IsEmpty())
				{
					DetailMessage += TEXT(" | ");
				}

				DetailMessage += FString::Printf(TEXT("Events: %s"), *JoinNames(EnabledEvents));
			}

			CompilationContext.MessageLog.Error(
				*FString::Printf(
					TEXT("Replic: This Blueprint has Replic-marked members but no ReplicTransportComponent. Add a ReplicTransportComponent before compiling. %s @@"),
					*DetailMessage),
				CompilationContext.Blueprint);
		}
	}

	WriteRuntimeReplicDefinitions(CompilationContext.Blueprint);

	TArray<UK2Node_CallFunction*> CallNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(CompilationContext.Blueprint, CallNodes);

	for (UK2Node_CallFunction* CallNode : CallNodes)
	{
		FString ValidationMessage;
		EMessageSeverity::Type Severity = EMessageSeverity::Info;
		if (!ReplicCallNodeValidation::ValidateReplicCallNode(CallNode, ValidationMessage, Severity))
		{
			continue;
		}

		if (Severity == EMessageSeverity::Error)
		{
			CompilationContext.MessageLog.Error(*FString::Printf(TEXT("Replic: %s @@"), *ValidationMessage), CallNode);
		}
		else if (Severity == EMessageSeverity::Warning)
		{
			CompilationContext.MessageLog.Warning(*FString::Printf(TEXT("Replic: %s @@"), *ValidationMessage), CallNode);
		}
	}
}
