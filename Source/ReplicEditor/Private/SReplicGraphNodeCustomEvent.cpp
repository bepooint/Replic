#include "SReplicGraphNodeCustomEvent.h"

#include "K2Node_CustomEvent.h"
#include "ReplicMetadata.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SReplicGraphNodeCustomEvent"

void SReplicGraphNodeCustomEvent::Construct(const FArguments& InArgs, UK2Node_CustomEvent* InNode)
{
	SGraphNodeK2Default::Construct(SGraphNodeK2Default::FArguments(), InNode);
}

TSharedRef<SWidget> SReplicGraphNodeCustomEvent::CreateTitleRightWidget()
{
	return SNew(SBorder)
		.Visibility(this, &SReplicGraphNodeCustomEvent::GetReplicBadgeVisibility)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(this, &SReplicGraphNodeCustomEvent::GetReplicBadgeColor)
		.Padding(FMargin(6.0f, 2.0f))
		.ToolTipText(this, &SReplicGraphNodeCustomEvent::GetReplicBadgeToolTip)
		[
			SNew(STextBlock)
			.Text(this, &SReplicGraphNodeCustomEvent::GetReplicBadgeText)
			.TextStyle(FAppStyle::Get(), "Graph.Node.NodeTitleExtraLines")
			.ColorAndOpacity(FLinearColor::White)
		];
}

EVisibility SReplicGraphNodeCustomEvent::GetReplicBadgeVisibility() const
{
	return IsReplicEnabled() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SReplicGraphNodeCustomEvent::GetReplicBadgeText() const
{
	if (!IsReplicEnabled())
	{
		return FText::GetEmpty();
	}

	return FText::Format(LOCTEXT("ReplicBadgeText", "Replic {0}"), FText::FromString(GetReplicMode()));
}

FText SReplicGraphNodeCustomEvent::GetReplicBadgeToolTip() const
{
	if (!IsReplicEnabled())
	{
		return FText::GetEmpty();
	}

	return FText::Format(
		LOCTEXT("ReplicBadgeToolTip", "Replic is enabled for this custom event.\nDispatch Mode: {0}"),
		FText::FromString(GetReplicMode()));
}

FSlateColor SReplicGraphNodeCustomEvent::GetReplicBadgeColor() const
{
	const FString Mode = GetReplicMode();
	if (Mode == TEXT("ServerOnly"))
	{
		return FLinearColor(0.80f, 0.38f, 0.10f, 0.95f);
	}

	if (Mode == TEXT("OwnerOnly"))
	{
		return FLinearColor(0.12f, 0.45f, 0.82f, 0.95f);
	}

	if (Mode == TEXT("LocalOnly"))
	{
		return FLinearColor(0.40f, 0.40f, 0.40f, 0.95f);
	}

	return FLinearColor(0.12f, 0.62f, 0.26f, 0.95f);
}

const UK2Node_CustomEvent* SReplicGraphNodeCustomEvent::GetCustomEventNode() const
{
	return Cast<UK2Node_CustomEvent>(GraphNode);
}

bool SReplicGraphNodeCustomEvent::IsReplicEnabled() const
{
	const UK2Node_CustomEvent* EventNode = GetCustomEventNode();
	if (!EventNode)
	{
		return false;
	}

	UK2Node_CustomEvent* MutableEventNode = const_cast<UK2Node_CustomEvent*>(EventNode);
	const auto& Metadata = MutableEventNode->GetUserDefinedMetaData();
	return Metadata.HasMetaData(ReplicMetadata::EventEnabled) && Metadata.GetMetaData(ReplicMetadata::EventEnabled).ToBool();
}

FString SReplicGraphNodeCustomEvent::GetReplicMode() const
{
	const UK2Node_CustomEvent* EventNode = GetCustomEventNode();
	if (!EventNode)
	{
		return TEXT("ReplicateAll");
	}

	UK2Node_CustomEvent* MutableEventNode = const_cast<UK2Node_CustomEvent*>(EventNode);
	const auto& Metadata = MutableEventNode->GetUserDefinedMetaData();
	return Metadata.HasMetaData(ReplicMetadata::EventMode)
		? Metadata.GetMetaData(ReplicMetadata::EventMode)
		: TEXT("ReplicateAll");
}

#undef LOCTEXT_NAMESPACE
