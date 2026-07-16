#pragma once

#include "CoreMinimal.h"
#include "KismetNodes/SGraphNodeK2Event.h"

class UK2Node_CustomEvent;

class SReplicGraphNodeCustomEvent : public SGraphNodeK2Event
{
public:
	SLATE_BEGIN_ARGS(SReplicGraphNodeCustomEvent) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node_CustomEvent* InNode);

protected:
	virtual TSharedRef<SWidget> CreateTitleRightWidget() override;

private:
	EVisibility GetReplicBadgeVisibility() const;
	FText GetReplicBadgeText() const;
	FText GetReplicPermissionBadgeText() const;
	FText GetReplicBadgeToolTip() const;
	FSlateColor GetReplicBadgeColor() const;
	const UK2Node_CustomEvent* GetCustomEventNode() const;
	bool IsReplicEnabled() const;
	FString GetReplicMode() const;
	FString GetReplicPermission() const;
};
