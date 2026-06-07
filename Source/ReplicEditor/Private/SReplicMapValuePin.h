#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"

class SReplicMapValuePin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SReplicMapValuePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void SynchronizeMapPinType() const;
};
