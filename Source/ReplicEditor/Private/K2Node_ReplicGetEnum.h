#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"

#include "K2Node_ReplicGetEnum.generated.h"

UCLASS()
class REPLICEDITOR_API UK2Node_ReplicGetEnum : public UK2Node
{
	GENERATED_BODY()

public:
	static FName GetTargetObjectPinName();
	static FName GetPropertyNamePinName();
	static FName GetValuePinName();
	static FName GetSuccessPinName();

	UEdGraphPin* GetTargetObjectPin() const;
	UEdGraphPin* GetPropertyNamePin() const;
	UEdGraphPin* GetValuePin() const;
	UEdGraphPin* GetSuccessPin() const;

	void HandleReplicSelectionChanged(FName ChangedPinName);

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
	virtual bool IsNodePure() const override { return true; }
	virtual bool IsNodeSafeToIgnore() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return true; }

	void RefreshValuePinType();

private:
	const FProperty* ResolveSelectedEnumProperty() const;
	UEnum* ResolveSelectedEnum() const;
	FText GetResolvedDisplayName() const;

	mutable bool bIsRefreshingPins = false;
};
