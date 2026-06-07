#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FBlueprintEditor;
struct FGraphPanelNodeFactory;
struct FGraphPanelPinFactory;
class IBlueprintEditor;
class IDetailCustomization;
class UBlueprint;
class UReplicBlueprintCompilerExtension;

class FReplicEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandlePostEngineInit();
	void RegisterBlueprintCustomizations();
	void UnregisterBlueprintCustomizations();
	void RegisterPropertyCustomizations();
	void UnregisterPropertyCustomizations();
	void RegisterGraphNodeFactory();
	void UnregisterGraphNodeFactory();
	void RegisterGraphPinFactory();
	void UnregisterGraphPinFactory();
	void HandleBlueprintPreCompile(UBlueprint* Blueprint);
	void HandleBlueprintCompiled();
	void SyncAllLoadedBlueprintEventMetadata() const;
	TSharedPtr<IDetailCustomization> CreateVariableCustomization(TSharedPtr<IBlueprintEditor> BlueprintEditor);
	TSharedPtr<IDetailCustomization> CreateFunctionCustomization(TSharedPtr<IBlueprintEditor> BlueprintEditor);

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle BlueprintPreCompileHandle;
	FDelegateHandle BlueprintCompiledHandle;
	FDelegateHandle VariableCustomizationHandle;
	FDelegateHandle FunctionCustomizationHandle;
	TSharedPtr<FGraphPanelNodeFactory> GraphNodeFactory;
	TSharedPtr<FGraphPanelPinFactory> GraphPinFactory;
	UReplicBlueprintCompilerExtension* CompilerExtension = nullptr;
	bool bCustomizationsRegistered = false;
	bool bPropertyCustomizationsRegistered = false;
};
