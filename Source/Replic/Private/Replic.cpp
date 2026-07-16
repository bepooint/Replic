#include "Replic.h"

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, Replic)

DEFINE_LOG_CATEGORY(LogReplic);
DEFINE_LOG_CATEGORY(LogReplicWrites);
DEFINE_LOG_CATEGORY(LogReplicEvents);
DEFINE_LOG_CATEGORY(LogReplicState);
DEFINE_LOG_CATEGORY(LogReplicObservers);
DEFINE_LOG_CATEGORY(LogReplicPermissions);
