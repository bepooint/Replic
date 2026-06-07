#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"

class SReplicSetValuePin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SReplicSetValuePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void SynchronizeSetPinType() const;
};
