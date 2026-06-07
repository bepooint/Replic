#pragma once

#include "CoreMinimal.h"

namespace ReplicMetadata
{
	inline constexpr TCHAR VariableEnabled[] = TEXT("Replic.Enabled");
	inline constexpr TCHAR VariablePersistent[] = TEXT("Replic.PersistentState");
	inline constexpr TCHAR VariableBatching[] = TEXT("Replic.UseBatching");
	inline constexpr TCHAR VariableBatchInterval[] = TEXT("Replic.BatchIntervalSeconds");
	inline constexpr TCHAR VariablePermissionMode[] = TEXT("Replic.PermissionMode");
	inline constexpr TCHAR EventEnabled[] = TEXT("Replic.EventEnabled");
	inline constexpr TCHAR EventPermissionMode[] = TEXT("Replic.EventPermissionMode");
	inline constexpr TCHAR EventMode[] = TEXT("Replic.EventMode");
}
