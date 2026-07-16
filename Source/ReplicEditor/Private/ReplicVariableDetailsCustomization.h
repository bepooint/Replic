#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FBlueprintEditor;
struct FEdGraphSchemaAction_BlueprintVariableBase;
class UFunction;

class FReplicVariableDetailsCustomization : public IDetailCustomization
{
public:
	explicit FReplicVariableDetailsCustomization(TWeakPtr<FBlueprintEditor> InBlueprintEditor);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	bool ResolveSelection(FEdGraphSchemaAction_BlueprintVariableBase*& OutVariableAction) const;
	bool GetBoolMetadata(const FName& Key, bool bDefaultValue) const;
	float GetFloatMetadata(const FName& Key, float DefaultValue) const;
	FString GetStringMetadata(const FName& Key, const FString& DefaultValue) const;
	FReply CreateReplicCallFunctionNode(UFunction* Function, FVector2D SpawnPosition) const;
	FReply CreateReplicSetterNode() const;
	FReply CreateReplicGetterNode() const;
	FReply CreateReplicAddDeltaNode() const;
	FReply CreateReplicRemoveDeltaNode() const;
	FReply CreateReplicSetEntryDeltaNode() const;
	FReply CreateCustomValidationFunction() const;
	void SetBoolMetadata(const FName& Key, bool bEnabled);
	void SetFloatMetadata(const FName& Key, float Value);
	void SetStringMetadata(const FName& Key, const FString& Value);
	void RefreshBlueprint() const;

	TWeakPtr<FBlueprintEditor> BlueprintEditor;
	TWeakObjectPtr<UBlueprint> Blueprint;
	FName VariableName = NAME_None;
	TOptional<float> PendingBatchInterval;
	TArray<TSharedPtr<FString>> PermissionOptions;
};
