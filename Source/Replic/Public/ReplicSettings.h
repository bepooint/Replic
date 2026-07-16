#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ReplicSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Replic", ToolTip = "Global settings for the Replic plugin."))
class REPLIC_API UReplicSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ToolTip = "If enabled, Replic writes runtime debug information to the log."))
	bool bEnableRuntimeDebugLogs = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (AdvancedDisplay, ToolTip = "If enabled, Replic includes routine request, apply, dispatch, state, and observer messages. Leave this disabled to log only warnings and errors for the enabled debug channels."))
	bool bEnableVerboseRuntimeLogs = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ToolTip = "If enabled, Replic also prints short runtime debug messages on screen during play."))
	bool bEnableScreenDebugMessages = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0.1", ToolTip = "How long Replic screen debug messages stay visible."))
	float ScreenDebugMessageDuration = 4.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (AdvancedDisplay, ToolTip = "If enabled, Replic logs property write requests, approvals, and failures."))
	bool bEnableWriteDebugLogs = true;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (AdvancedDisplay, ToolTip = "If enabled, Replic logs event requests, approvals, and failures."))
	bool bEnableEventDebugLogs = true;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (AdvancedDisplay, ToolTip = "If enabled, Replic logs persistent state storage, batching, and late-join state reapplication."))
	bool bEnableStateDebugLogs = true;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (AdvancedDisplay, ToolTip = "If enabled, Replic logs property observer binds, unbinds, and change notifications."))
	bool bEnableObserverDebugLogs = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (AdvancedDisplay, ToolTip = "If enabled, Replic logs detailed permission decisions such as OwnerOnly, ServerOnly, and Custom validation results."))
	bool bEnableDetailedPermissionLogs = true;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ToolTip = "If enabled, Replic can surface editor-side warnings for unsupported or suspicious setup."))
	bool bEnableEditorWarnings = true;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0.0", ToolTip = "Default batching delay used when a Replic variable enables batching but does not override the interval."))
	float DefaultBatchIntervalSeconds = 0.05f;

	virtual FName GetCategoryName() const override
	{
		return TEXT("Plugins");
	}
};
