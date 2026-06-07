#pragma once

#include "CoreMinimal.h"
#include "Logging/LogVerbosity.h"

class UK2Node_CallFunction;

namespace ReplicCallNodeValidation
{
	bool IsReplicCallNode(const UK2Node_CallFunction* CallNode);
	bool ValidateReplicCallNode(const UK2Node_CallFunction* CallNode, FString& OutMessage, EMessageSeverity::Type& OutSeverity);
}
