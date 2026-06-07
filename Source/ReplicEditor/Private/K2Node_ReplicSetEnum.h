#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"

#include "K2Node_ReplicSetEnum.generated.h"

UCLASS()
class REPLICEDITOR_API UK2Node_ReplicSetEnum : public UK2Node
{
	GENERATED_BODY()

public:
	static FName GetExecPinName();
	static FName GetThenPinName();
	static FName GetContextObjectPinName();
	static FName GetTargetObjectPinName();
	static FName GetPropertyNamePinName();
	static FName GetValuePinName();
	static FName GetSuccessPinName();

	UEdGraphPin* GetExecPin() const;
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetContextObjectPin() const;
	UEdGraphPin* GetTargetObjectPin() const;
	UEdGraphPin* GetPropertyNamePin() const;
	UEdGraphPin* GetValuePin() const;
	UEdGraphPin* GetSuccessPin() const;
	bool HasPersistedEnumSelection() const { return bHasPersistedEnumSelection; }
	FName GetPersistedEnumValue() const { return PersistedEnumValue; }

	void HandleReplicSelectionChanged(FName ChangedPinName);
	void HandleEnumValueSelectionChanged(FName NewEnumValue);

	virtual void AllocateDefaultPins() override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(class FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual void PostReconstructNode() override;
	virtual void PostLoad() override;
	virtual bool IsNodeSafeToIgnore() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return true; }

	void RefreshValuePinType();

private:
	const FProperty* ResolveSelectedEnumProperty() const;
	UEnum* ResolveSelectedEnum() const;
	FText GetResolvedDisplayName() const;

	UPROPERTY()
	FName PersistedEnumValue = NAME_None;

	UPROPERTY()
	bool bHasPersistedEnumSelection = false;

	mutable bool bIsRefreshingPins = false;
};
