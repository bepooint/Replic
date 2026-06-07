#pragma once

#include "EdGraphUtilities.h"

class FReplicGraphPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* InPin) const override;
};
