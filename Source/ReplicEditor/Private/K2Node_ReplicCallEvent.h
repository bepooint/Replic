#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"

#include "K2Node_ReplicCallEvent.generated.h"

UCLASS()
class REPLICEDITOR_API UK2Node_ReplicCallEvent : public UK2Node
{
	GENERATED_BODY()

public:
	UK2Node_ReplicCallEvent();

	static FName GetExecPinName();
	static FName GetThenPinName();
	static FName GetContextObjectPinName();
	static FName GetTargetObjectPinName();
	static FName GetEventNamePinName();
	static FName GetSuccessPinName();

	UEdGraphPin* GetExecPin() const;
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetContextObjectPin() const;
	UEdGraphPin* GetTargetObjectPin() const;
	UEdGraphPin* GetEventNamePin() const;
	UEdGraphPin* GetSuccessPin() const;
	void HandleReplicSelectionChanged(FName ChangedPinName);

	virtual void PostLoad() override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(class FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual void PostReconstructNode() override;
	virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const override;
	virtual bool IsNodeSafeToIgnore() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return true; }

	TArray<UEdGraphPin*> GetArgumentPins() const;
	void RefreshArgumentPins(const TArray<UEdGraphPin*>* PinsToPreserve = nullptr, const UEdGraphPin* SourceEventPin = nullptr);

private:
	void AllocateStaticPins();
	void ApplyStaticPinToolTips();
	void RebuildPinsIfNeeded(UEdGraphPin* ChangedPin);
	void ConfigureDynamicPins();
	void RemoveInvalidLegacyLinks();
	void RemoveInvalidLegacyPins();
	void RemoveOrphanedPins();
	UFunction* ResolveSelectedEvent() const;
	FText GetResolvedDisplayName() const;

	mutable bool bIsRefreshingPins = false;
};
