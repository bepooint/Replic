#include "ReplicEditorModule.h"

#include "BlueprintEditor.h"
#include "BlueprintCompilationManager.h"
#include "BlueprintEditorModule.h"
#include "Editor.h"
#include "EdGraphToken.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ReplicCallEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/CoreDelegates.h"
#include "PropertyEditorModule.h"
#include "ReplicBlueprintCompilerExtension.h"
#include "ReplicCallNodeValidation.h"
#include "ReplicEventNodeFactory.h"
#include "ReplicGraphPinFactory.h"
#include "ReplicMetadata.h"
#include "ReplicEventDetailsCustomization.h"
#include "ReplicSceneComponentDetailsCustomization.h"
#include "ReplicVariableDetailsCustomization.h"
#include "ReplicK2NodeUtils.h"
#include "UObject/UObjectIterator.h"
#include "EdGraphUtilities.h"
#include "Logging/TokenizedMessage.h"

IMPLEMENT_MODULE(FReplicEditorModule, ReplicEditor)

namespace
{
	void SyncReplicEventMetadataForBlueprint(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return;
		}

		TArray<UK2Node_CustomEvent*> CustomEventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, CustomEventNodes);

		const auto SyncClass = [&CustomEventNodes](UClass* TargetClass)
		{
			if (!TargetClass)
			{
				return;
			}

			for (const UK2Node_CustomEvent* EventNode : CustomEventNodes)
			{
				if (!EventNode)
				{
					continue;
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

					// Keep event permissions in sync with the compiled runtime function as well.
					// Without this, Permission falls back to None after a Blueprint compile even though
					// the editor-side custom event node still shows the selected mode in Details.
					if (Metadata.HasMetaData(ReplicMetadata::EventPermissionMode))
					{
						TargetFunction->SetMetaData(ReplicMetadata::EventPermissionMode, *Metadata.GetMetaData(ReplicMetadata::EventPermissionMode));
					}
					else
					{
						TargetFunction->RemoveMetaData(ReplicMetadata::EventPermissionMode);
					}
				}
			}
		};

		SyncClass(Blueprint->SkeletonGeneratedClass);
		SyncClass(Blueprint->GeneratedClass);
	}

	const TCHAR* ReplicCompilerPrefix = TEXT("Replic: ");

	void ClearReplicCompilerMessage(UEdGraphNode* Node)
	{
		if (Node && Node->bHasCompilerMessage && Node->ErrorMsg.StartsWith(ReplicCompilerPrefix))
		{
			Node->bHasCompilerMessage = false;
			Node->ErrorMsg.Reset();
			Node->ErrorType = EMessageSeverity::Info;
		}
	}

	void SetReplicCompilerMessage(UEdGraphNode* Node, EMessageSeverity::Type Severity, const FString& Message)
	{
		if (!Node)
		{
			return;
		}

		Node->bHasCompilerMessage = true;
		Node->ErrorType = Severity;
		Node->ErrorMsg = FString::Printf(TEXT("%s%s"), ReplicCompilerPrefix, *Message);
	}

	void ValidateReplicCallNode(UK2Node_CallFunction* CallNode)
	{
		if (!CallNode || !ReplicCallNodeValidation::IsReplicCallNode(CallNode))
		{
			return;
		}

		ReplicK2NodeUtils::ApplyReplicLibraryPinToolTips(CallNode);
		ClearReplicCompilerMessage(CallNode);

		FString ValidationMessage;
		EMessageSeverity::Type Severity = EMessageSeverity::Info;
		if (ReplicCallNodeValidation::ValidateReplicCallNode(CallNode, ValidationMessage, Severity))
		{
			SetReplicCompilerMessage(CallNode, Severity, ValidationMessage);
		}
	}

	void RefreshReplicEventNodes(UBlueprint* Blueprint, const bool bNotifyGraphs)
	{
		if (!Blueprint)
		{
			return;
		}

		TArray<UK2Node_ReplicCallEvent*> ReplicEventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, ReplicEventNodes);
		for (UK2Node_ReplicCallEvent* ReplicEventNode : ReplicEventNodes)
		{
			if (!ReplicEventNode)
			{
				continue;
			}

			ReplicEventNode->ReconstructNode();
			ReplicEventNode->ClearCompilerMessage();
			if (bNotifyGraphs)
			{
				if (UEdGraph* Graph = ReplicEventNode->GetGraph())
				{
					Graph->NotifyNodeChanged(ReplicEventNode);
				}
			}
		}
	}

	bool HasNonExecOutputNeighbors(const UK2Node_ReplicCallEvent* CallEventNode)
	{
		if (!CallEventNode)
		{
			return false;
		}

		bool bHasNeighbor = false;
		const_cast<UK2Node_ReplicCallEvent*>(CallEventNode)->ForEachNodeDirectlyConnectedIf(
			[](const UEdGraphPin* Pin)
			{
				return Pin
					&& Pin->Direction == EGPD_Output
					&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec;
			},
			[&bHasNeighbor](UEdGraphNode*)
			{
				bHasNeighbor = true;
			});
		return bHasNeighbor;
	}

	bool IsFalsePositiveReplicPrunedExecWarning(const TSharedRef<FTokenizedMessage>& Message)
	{
		if (Message->GetSeverity() != EMessageSeverity::Warning)
		{
			return false;
		}

		const FString MessageText = Message->ToText().ToString();
		if (!MessageText.Contains(TEXT("was pruned because its Exec pin is not connected")))
		{
			return false;
		}

		for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
		{
			if (Token->GetType() != EMessageToken::EdGraph)
			{
				continue;
			}

			const FEdGraphToken& GraphToken = static_cast<const FEdGraphToken&>(Token.Get());
			const UK2Node_ReplicCallEvent* CallEventNode = Cast<UK2Node_ReplicCallEvent>(GraphToken.GetGraphObject());
			if (!CallEventNode)
			{
				continue;
			}

			const UEdGraphPin* ExecPin = CallEventNode->GetExecPin();
			if (!ExecPin || ExecPin->LinkedTo.Num() == 0)
			{
				continue;
			}

			if (!HasNonExecOutputNeighbors(CallEventNode))
			{
				return true;
			}
		}

		return false;
	}

	void StripFalsePositiveReplicPrunedExecWarnings(UBlueprint* Blueprint)
	{
		if (!Blueprint || !Blueprint->PreCompileLog.IsValid())
		{
			return;
		}

		FCompilerResultsLog& CompileLog = *Blueprint->PreCompileLog;
		int32 RemovedWarnings = 0;
		for (int32 MessageIndex = CompileLog.Messages.Num() - 1; MessageIndex >= 0; --MessageIndex)
		{
			if (!IsFalsePositiveReplicPrunedExecWarning(CompileLog.Messages[MessageIndex]))
			{
				continue;
			}

			CompileLog.Messages.RemoveAt(MessageIndex);
			++RemovedWarnings;
		}

		if (RemovedWarnings == 0)
		{
			return;
		}

		CompileLog.NumWarnings = FMath::Max(0, CompileLog.NumWarnings - RemovedWarnings);
		if (Blueprint->Status == BS_UpToDateWithWarnings && CompileLog.NumWarnings == 0 && CompileLog.NumErrors == 0)
		{
			Blueprint->Status = BS_UpToDate;
		}
	}
}

void FReplicEditorModule::StartupModule()
{
	if (GEditor)
	{
		RegisterBlueprintCustomizations();
		RegisterPropertyCustomizations();
		RegisterGraphNodeFactory();
		RegisterGraphPinFactory();
		if (!CompilerExtension)
		{
			CompilerExtension = NewObject<UReplicBlueprintCompilerExtension>(GetTransientPackage());
			CompilerExtension->AddToRoot();
			FBlueprintCompilationManager::RegisterCompilerExtension(UBlueprint::StaticClass(), CompilerExtension);
		}
		BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(this, &FReplicEditorModule::HandleBlueprintPreCompile);
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FReplicEditorModule::HandleBlueprintCompiled);
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FReplicEditorModule::HandlePostEngineInit);
	}
}

void FReplicEditorModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	if (BlueprintCompiledHandle.IsValid() && GEditor)
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
		BlueprintCompiledHandle.Reset();
	}

	if (BlueprintPreCompileHandle.IsValid() && GEditor)
	{
		GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
		BlueprintPreCompileHandle.Reset();
	}

	UnregisterBlueprintCustomizations();
	UnregisterPropertyCustomizations();
	UnregisterGraphNodeFactory();
	UnregisterGraphPinFactory();
	if (CompilerExtension)
	{
		if (!IsEngineExitRequested())
		{
			CompilerExtension->RemoveFromRoot();
		}

		CompilerExtension = nullptr;
	}
}

void FReplicEditorModule::HandlePostEngineInit()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	RegisterBlueprintCustomizations();
	RegisterPropertyCustomizations();
	RegisterGraphNodeFactory();
	RegisterGraphPinFactory();
	if (!CompilerExtension)
	{
		CompilerExtension = NewObject<UReplicBlueprintCompilerExtension>(GetTransientPackage());
		CompilerExtension->AddToRoot();
		FBlueprintCompilationManager::RegisterCompilerExtension(UBlueprint::StaticClass(), CompilerExtension);
	}
	if (GEditor && !BlueprintCompiledHandle.IsValid())
	{
		if (!BlueprintPreCompileHandle.IsValid())
		{
			BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(this, &FReplicEditorModule::HandleBlueprintPreCompile);
		}

		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FReplicEditorModule::HandleBlueprintCompiled);
	}
}

void FReplicEditorModule::RegisterBlueprintCustomizations()
{
	if (bCustomizationsRegistered)
	{
		return;
	}

	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	VariableCustomizationHandle = BlueprintEditorModule.RegisterVariableCustomization(
		FProperty::StaticClass(),
		FOnGetVariableCustomizationInstance::CreateRaw(this, &FReplicEditorModule::CreateVariableCustomization));
	FunctionCustomizationHandle = BlueprintEditorModule.RegisterFunctionCustomization(
		UK2Node_CustomEvent::StaticClass(),
		FOnGetFunctionCustomizationInstance::CreateRaw(this, &FReplicEditorModule::CreateFunctionCustomization));
	bCustomizationsRegistered = true;
}

void FReplicEditorModule::UnregisterBlueprintCustomizations()
{
	if (!bCustomizationsRegistered || !FModuleManager::Get().IsModuleLoaded("Kismet"))
	{
		return;
	}

	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
	if (VariableCustomizationHandle.IsValid())
	{
		BlueprintEditorModule.UnregisterVariableCustomization(FProperty::StaticClass(), VariableCustomizationHandle);
		VariableCustomizationHandle.Reset();
	}

	if (FunctionCustomizationHandle.IsValid())
	{
		BlueprintEditorModule.UnregisterFunctionCustomization(UK2Node_CustomEvent::StaticClass(), FunctionCustomizationHandle);
		FunctionCustomizationHandle.Reset();
	}

	bCustomizationsRegistered = false;
}

void FReplicEditorModule::RegisterPropertyCustomizations()
{
	if (bPropertyCustomizationsRegistered)
	{
		return;
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(
		TEXT("K2Node_CustomEvent"),
		FOnGetDetailCustomizationInstance::CreateStatic(&FReplicEventDetailsCustomization::MakeInstance));
	PropertyEditorModule.RegisterCustomClassLayout(
		TEXT("SceneComponent"),
		FOnGetDetailCustomizationInstance::CreateStatic(&FReplicSceneComponentDetailsCustomization::MakeInstance));
	PropertyEditorModule.NotifyCustomizationModuleChanged();
	bPropertyCustomizationsRegistered = true;
}

void FReplicEditorModule::UnregisterPropertyCustomizations()
{
	if (!bPropertyCustomizationsRegistered || !FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		return;
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.UnregisterCustomClassLayout(TEXT("K2Node_CustomEvent"));
	PropertyEditorModule.UnregisterCustomClassLayout(TEXT("SceneComponent"));
	PropertyEditorModule.NotifyCustomizationModuleChanged();
	bPropertyCustomizationsRegistered = false;
}

void FReplicEditorModule::RegisterGraphPinFactory()
{
	if (!GraphPinFactory.IsValid())
	{
		GraphPinFactory = MakeShared<FReplicGraphPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(GraphPinFactory);
	}
}

void FReplicEditorModule::RegisterGraphNodeFactory()
{
	if (!GraphNodeFactory.IsValid())
	{
		GraphNodeFactory = MakeShared<FReplicEventNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(GraphNodeFactory);
	}
}

void FReplicEditorModule::UnregisterGraphNodeFactory()
{
	if (GraphNodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(GraphNodeFactory);
		GraphNodeFactory.Reset();
	}
}

void FReplicEditorModule::UnregisterGraphPinFactory()
{
	if (GraphPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(GraphPinFactory);
		GraphPinFactory.Reset();
	}
}

void FReplicEditorModule::HandleBlueprintCompiled()
{
	SyncAllLoadedBlueprintEventMetadata();
	for (TObjectIterator<UBlueprint> It; It; ++It)
	{
		if (It->IsTemplate())
		{
			continue;
		}

		TArray<UK2Node_CallFunction*> CallNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(*It, CallNodes);
		for (UK2Node_CallFunction* CallNode : CallNodes)
		{
			ValidateReplicCallNode(CallNode);
		}

		RefreshReplicEventNodes(*It, true);
		StripFalsePositiveReplicPrunedExecWarnings(*It);
	}
}

void FReplicEditorModule::HandleBlueprintPreCompile(UBlueprint* Blueprint)
{
	RefreshReplicEventNodes(Blueprint, false);
}

void FReplicEditorModule::SyncAllLoadedBlueprintEventMetadata() const
{
	for (TObjectIterator<UBlueprint> It; It; ++It)
	{
		if (!It->IsTemplate())
		{
			SyncReplicEventMetadataForBlueprint(*It);
		}
	}
}

TSharedPtr<IDetailCustomization> FReplicEditorModule::CreateVariableCustomization(TSharedPtr<IBlueprintEditor> BlueprintEditor)
{
	return MakeShared<FReplicVariableDetailsCustomization>(StaticCastSharedPtr<FBlueprintEditor>(BlueprintEditor));
}

TSharedPtr<IDetailCustomization> FReplicEditorModule::CreateFunctionCustomization(TSharedPtr<IBlueprintEditor> BlueprintEditor)
{
	return MakeShared<FReplicEventDetailsCustomization>(StaticCastSharedPtr<FBlueprintEditor>(BlueprintEditor), true);
}
