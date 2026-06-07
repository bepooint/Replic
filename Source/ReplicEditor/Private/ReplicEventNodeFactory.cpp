#include "ReplicEventNodeFactory.h"

#include "K2Node_CustomEvent.h"
#include "SReplicGraphNodeCustomEvent.h"

TSharedPtr<SGraphNode> FReplicEventNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(InNode))
	{
		return SNew(SReplicGraphNodeCustomEvent, CustomEventNode);
	}

	return nullptr;
}
