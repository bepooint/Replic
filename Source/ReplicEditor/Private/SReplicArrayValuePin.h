#pragma once

#include "SGraphPin.h"

class SReplicArrayValuePin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SReplicArrayValuePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void SynchronizeArrayPinType() const;
};
