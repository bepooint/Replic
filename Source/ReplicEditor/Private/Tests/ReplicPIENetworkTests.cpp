#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Kismet/GameplayStatics.h"
#include "ReplicLibrary.h"
#include "ReplicMetadata.h"
#include "ReplicPropertyObserver.h"
#include "ReplicTransportComponent.h"
#include "Tests/ReplicPIENetworkTestActors.h"

namespace
{
	const TCHAR* ToMetadataString(const EReplicPermissionMode PermissionMode)
	{
		switch (PermissionMode)
		{
		case EReplicPermissionMode::None:
			return TEXT("None");
		case EReplicPermissionMode::OwnerOnly:
			return TEXT("OwnerOnly");
		case EReplicPermissionMode::ServerOnly:
			return TEXT("ServerOnly");
		case EReplicPermissionMode::Custom:
			return TEXT("Custom");
		default:
			return TEXT("None");
		}
	}

	const TCHAR* ToMetadataString(const EReplicEventMode EventMode)
	{
		switch (EventMode)
		{
		case EReplicEventMode::LocalOnly:
			return TEXT("LocalOnly");
		case EReplicEventMode::ServerOnly:
			return TEXT("ServerOnly");
		case EReplicEventMode::OwnerOnly:
			return TEXT("OwnerOnly");
		case EReplicEventMode::ReplicateAll:
			return TEXT("ReplicateAll");
		default:
			return TEXT("ReplicateAll");
		}
	}

	void ConfigureReplicPIENetworkMetadata(
		const EReplicPermissionMode VariablePermissionMode,
		const bool bPersistentState,
		const EReplicPermissionMode EventPermissionMode,
		const EReplicEventMode EventMode)
	{
		if (FProperty* SharedValueProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue)))
		{
			SharedValueProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			SharedValueProperty->SetMetaData(ReplicMetadata::VariablePersistent, bPersistentState ? TEXT("true") : TEXT("false"));
			SharedValueProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, ToMetadataString(VariablePermissionMode));
		}

		if (FProperty* BatchedValueProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue)))
		{
			BatchedValueProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			BatchedValueProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			BatchedValueProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
			BatchedValueProperty->SetMetaData(ReplicMetadata::VariableBatching, TEXT("true"));
			BatchedValueProperty->SetMetaData(ReplicMetadata::VariableBatchInterval, TEXT("0.05"));
		}

		if (FProperty* SharedArrayProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)))
		{
			SharedArrayProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			SharedArrayProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			SharedArrayProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, ToMetadataString(VariablePermissionMode));
		}

		if (FProperty* IntSetStateProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState)))
		{
			IntSetStateProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			IntSetStateProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			IntSetStateProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* IntIntMapStateProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState)))
		{
			IntIntMapStateProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			IntIntMapStateProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			IntIntMapStateProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (UFunction* MarkedPulseFunction = AReplicPIENetworkActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse)))
		{
			MarkedPulseFunction->SetMetaData(ReplicMetadata::EventEnabled, TEXT("true"));
			MarkedPulseFunction->SetMetaData(ReplicMetadata::EventPermissionMode, ToMetadataString(EventPermissionMode));
			MarkedPulseFunction->SetMetaData(ReplicMetadata::EventMode, ToMetadataString(EventMode));
		}
	}

	APlayerController* GetFirstController(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	APlayerController* GetControllerByIndex(UWorld* World, const int32 PlayerIndex)
	{
		return World ? UGameplayStatics::GetPlayerController(World, PlayerIndex) : nullptr;
	}

	TArray<FReplicNamedValue> MakePulseArguments(const int32 Delta)
	{
		TArray<FReplicNamedValue> Arguments;
		Arguments.Add(UReplicLibrary::MakeNamedIntValue(TEXT("Delta"), Delta));
		return Arguments;
	}

	bool RequestIntContainerDelta(
		UObject* ContextObject,
		UObject* TargetObject,
		const FName PropertyName,
		const EReplicContainerDeltaOperation Operation,
		const int32 PrimaryValue,
		const TOptional<int32> SecondaryValue = TOptional<int32>())
	{
		if (!ContextObject || !TargetObject)
		{
			return false;
		}

		AActor* RequestActor = Cast<AActor>(ContextObject);
		if (!RequestActor)
		{
			return false;
		}

		UReplicLibrary::RegisterObservedObject(TargetObject);

		if (UReplicTransportComponent* Transport = UReplicTransportComponent::FindOrCreate(RequestActor))
		{
			return Transport->RequestMarkedContainerDelta(
				ContextObject,
				TargetObject,
				PropertyName,
				Operation,
				LexToString(PrimaryValue),
				SecondaryValue.IsSet() ? LexToString(SecondaryValue.GetValue()) : FString());
		}

		return false;
	}

	struct FReplicPIENetworkState : public FBasePIENetworkComponentState
	{
		AReplicPIENetworkActor* SharedActor = nullptr;
		AReplicPIENetworkActor* ClientOwnedActor = nullptr;
		UReplicPropertyObserver* PropertyObserver = nullptr;
		UReplicPIEObserverSink* ObserverSink = nullptr;
	};

	template<typename NetworkState>
	void BuildListenServerNetwork(FPIENetworkComponent<NetworkState>& Network)
	{
		FNetworkComponentBuilder<NetworkState>()
			.WithClients(1)
			.AsListenServer()
			.WithGameMode(AReplicPIENetworkGameMode::StaticClass())
			.Build(Network);
	}
}

NETWORK_TEST_CLASS(FReplicPIEVariableReplicationTest, "Replic.Network.VariableReplication")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::None, true, EReplicPermissionMode::None, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(ServerAndClientWritesReplicateAcrossPIE)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenServer(TEXT("Server writes Replic value"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					11);
				ASSERT_THAT(IsTrue(bWriteAccepted));
			})
			.UntilServer(TEXT("Server observes replicated value 11"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor) && ServerState.SharedActor->SharedValue == 11;
			})
			.UntilClients(TEXT("Clients observe replicated value 11"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->SharedValue == 11;
			})
			.ThenClients(TEXT("Client state is 11 after server write"), [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ASSERT_THAT(AreEqual(11, ClientState.SharedActor->SharedValue));
			})
			.ThenClient(TEXT("Client writes Replic value"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				APlayerController* ClientController = GetFirstController(ClientState.World);
				ASSERT_THAT(IsNotNull(ClientController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					21);
				ASSERT_THAT(IsTrue(bWriteAccepted));
			})
			.UntilServer(TEXT("Server observes replicated value 21"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor) && ServerState.SharedActor->SharedValue == 21;
			})
			.UntilClients(TEXT("Clients observe replicated value 21"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->SharedValue == 21;
			})
			.ThenServer(TEXT("Server state is 21 after client write"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				ASSERT_THAT(AreEqual(21, ServerState.SharedActor->SharedValue));
			})
			.ThenClients(TEXT("Client state is 21 after client write"), [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ASSERT_THAT(AreEqual(21, ClientState.SharedActor->SharedValue));
			});
	}

	TEST_METHOD(ServerAndClientEventsReplicateAcrossPIE)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenServer(TEXT("Server calls Replic event"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));

				const bool bCallAccepted = UReplicLibrary::CallMarkedEvent(
					ServerController,
					ServerState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(5));
				ASSERT_THAT(IsTrue(bCallAccepted));
			})
			.UntilServer(TEXT("Server observes event value 5"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor) && ServerState.SharedActor->EventValue == 5;
			})
			.UntilClients(TEXT("Clients observe event value 5"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->EventValue == 5;
			})
			.ThenClient(TEXT("Client calls Replic event"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				APlayerController* ClientController = GetFirstController(ClientState.World);
				ASSERT_THAT(IsNotNull(ClientController));

				const bool bCallAccepted = UReplicLibrary::CallMarkedEvent(
					ClientController,
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(7));
				ASSERT_THAT(IsTrue(bCallAccepted));
			})
			.UntilServer(TEXT("Server observes event value 12"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor) && ServerState.SharedActor->EventValue == 12;
			})
			.UntilClients(TEXT("Clients observe event value 12"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->EventValue == 12;
			})
			.ThenServer(TEXT("Server state is 12 after client event"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				ASSERT_THAT(AreEqual(12, ServerState.SharedActor->EventValue));
			})
			.ThenClients(TEXT("Client state is 12 after client event"), [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ASSERT_THAT(AreEqual(12, ClientState.SharedActor->EventValue));
			});
	}

	TEST_METHOD(LateJoinReceivesPersistentReplicState)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenServer(TEXT("Server writes persistent Replic value"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					33);
				ASSERT_THAT(IsTrue(bWriteAccepted));
			})
			.UntilServer(TEXT("Server observes persistent value 33"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor) && ServerState.SharedActor->SharedValue == 33;
			})
			.UntilClients(TEXT("Initial client observes persistent value 33"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->SharedValue == 33;
			})
			.ThenClientJoins()
			.UntilClient(TEXT("Late client receives actor and persistent value"), 1, [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->SharedValue == 33;
			})
			.ThenClient(TEXT("Late client state is 33"), 1, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ASSERT_THAT(AreEqual(33, ClientState.SharedActor->SharedValue));
			})
			.ThenServer(TEXT("Server state remains 33 after late join"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				ASSERT_THAT(AreEqual(33, ServerState.SharedActor->SharedValue));
			})
			.ThenClient(TEXT("Original client state remains 33 after late join"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ASSERT_THAT(AreEqual(33, ClientState.SharedActor->SharedValue));
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIEContainerDeltaOperationsTest, "Replic.Network.ContainerDeltaOperations")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::None, true, EReplicPermissionMode::None, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(ServerAndClientContainerDeltasReplicateAcrossPIE)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenServer(TEXT("Server applies initial container deltas"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					1)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					2)));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState),
					EReplicContainerDeltaOperation::AddSetItem,
					10)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState),
					EReplicContainerDeltaOperation::AddSetItem,
					20)));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState),
					EReplicContainerDeltaOperation::SetMapEntry,
					7,
					70)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState),
					EReplicContainerDeltaOperation::SetMapEntry,
					8,
					80)));
			})
			.UntilServer(TEXT("Server observes initial delta results"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& ServerState.SharedActor->SharedArray.Num() == 2
					&& ServerState.SharedActor->SharedArray[0] == 1
					&& ServerState.SharedActor->SharedArray[1] == 2
					&& ServerState.SharedActor->IntSetState.Num() == 2
					&& ServerState.SharedActor->IntSetState.Contains(10)
					&& ServerState.SharedActor->IntSetState.Contains(20)
					&& ServerState.SharedActor->IntIntMapState.Num() == 2
					&& ServerState.SharedActor->IntIntMapState.FindRef(7) == 70
					&& ServerState.SharedActor->IntIntMapState.FindRef(8) == 80;
			})
			.UntilClients(TEXT("Clients observe initial delta results"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& ClientState.SharedActor->SharedArray.Num() == 2
					&& ClientState.SharedActor->SharedArray[0] == 1
					&& ClientState.SharedActor->SharedArray[1] == 2
					&& ClientState.SharedActor->IntSetState.Num() == 2
					&& ClientState.SharedActor->IntSetState.Contains(10)
					&& ClientState.SharedActor->IntSetState.Contains(20)
					&& ClientState.SharedActor->IntIntMapState.Num() == 2
					&& ClientState.SharedActor->IntIntMapState.FindRef(7) == 70
					&& ClientState.SharedActor->IntIntMapState.FindRef(8) == 80;
			})
			.ThenClient(TEXT("Client applies follow-up container deltas"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				APlayerController* ClientController = GetFirstController(ClientState.World);
				ASSERT_THAT(IsNotNull(ClientController));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::RemoveArrayItem,
					1)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					3)));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState),
					EReplicContainerDeltaOperation::RemoveSetItem,
					10)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState),
					EReplicContainerDeltaOperation::AddSetItem,
					30)));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState),
					EReplicContainerDeltaOperation::SetMapEntry,
					7,
					700)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState),
					EReplicContainerDeltaOperation::RemoveMapEntry,
					8)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState),
					EReplicContainerDeltaOperation::SetMapEntry,
					9,
					90)));
			})
			.UntilServer(TEXT("Server observes final delta results"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& ServerState.SharedActor->SharedArray.Num() == 2
					&& ServerState.SharedActor->SharedArray[0] == 2
					&& ServerState.SharedActor->SharedArray[1] == 3
					&& ServerState.SharedActor->IntSetState.Num() == 2
					&& ServerState.SharedActor->IntSetState.Contains(20)
					&& ServerState.SharedActor->IntSetState.Contains(30)
					&& ServerState.SharedActor->IntIntMapState.Num() == 2
					&& ServerState.SharedActor->IntIntMapState.FindRef(7) == 700
					&& !ServerState.SharedActor->IntIntMapState.Contains(8)
					&& ServerState.SharedActor->IntIntMapState.FindRef(9) == 90;
			})
			.UntilClients(TEXT("Clients observe final delta results"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& ClientState.SharedActor->SharedArray.Num() == 2
					&& ClientState.SharedActor->SharedArray[0] == 2
					&& ClientState.SharedActor->SharedArray[1] == 3
					&& ClientState.SharedActor->IntSetState.Num() == 2
					&& ClientState.SharedActor->IntSetState.Contains(20)
					&& ClientState.SharedActor->IntSetState.Contains(30)
					&& ClientState.SharedActor->IntIntMapState.Num() == 2
					&& ClientState.SharedActor->IntIntMapState.FindRef(7) == 700
					&& !ClientState.SharedActor->IntIntMapState.Contains(8)
					&& ClientState.SharedActor->IntIntMapState.FindRef(9) == 90;
			})
			.ThenClientJoins()
			.UntilClient(TEXT("Late client receives final delta state"), 1, [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& ClientState.SharedActor->SharedArray.Num() == 2
					&& ClientState.SharedActor->SharedArray[0] == 2
					&& ClientState.SharedActor->SharedArray[1] == 3
					&& ClientState.SharedActor->IntSetState.Num() == 2
					&& ClientState.SharedActor->IntSetState.Contains(20)
					&& ClientState.SharedActor->IntSetState.Contains(30)
					&& ClientState.SharedActor->IntIntMapState.Num() == 2
					&& ClientState.SharedActor->IntIntMapState.FindRef(7) == 700
					&& !ClientState.SharedActor->IntIntMapState.Contains(8)
					&& ClientState.SharedActor->IntIntMapState.FindRef(9) == 90;
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIENonePermissionTest, "Replic.Network.Permissions.None")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::None, false, EReplicPermissionMode::None, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(ClientPropertyContainerAndEventRequestsAreAccepted)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenClient(TEXT("Client sends requests without an additional permission gate"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				APlayerController* ClientController = GetFirstController(ClientState.World);
				ASSERT_THAT(IsNotNull(ClientController));

				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					12)));
				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					21)));
				ASSERT_THAT(IsTrue(UReplicLibrary::CallMarkedEvent(
					ClientController,
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(3))));
			})
			.UntilServer(TEXT("Server accepts None permission requests"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& ServerState.SharedActor->SharedValue == 12
					&& ServerState.SharedActor->SharedArray == TArray<int32>({ 21 })
					&& ServerState.SharedActor->EventValue == 3;
			})
			.UntilClients(TEXT("Client receives accepted None permission results"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& ClientState.SharedActor->SharedValue == 12
					&& ClientState.SharedActor->SharedArray == TArray<int32>({ 21 })
					&& ClientState.SharedActor->EventValue == 3;
			});
	}

};

NETWORK_TEST_CLASS(FReplicPIEDiagnosticsTest, "Replic.Network.Diagnostics")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::None, true, EReplicPermissionMode::None, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(MarkedPropertySnapshotIncludesPersistentState)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenServer(TEXT("Server writes value for diagnostics"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				ASSERT_THAT(IsTrue(UReplicLibrary::HasReplicTransportComponent(ServerState.SharedActor)));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					73)));
			})
			.UntilServer(TEXT("Server persistent diagnostics value is committed"), [](FReplicPIENetworkState& ServerState)
			{
				FReplicPropertyDebugInfo DebugInfo;
				return IsValid(ServerState.SharedActor)
					&& UReplicLibrary::GetMarkedPropertyDebugInfo(ServerState.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), DebugInfo)
					&& DebugInfo.bHasPersistentState
					&& DebugInfo.PersistentValue == TEXT("73");
			})
			.UntilClients(TEXT("Clients receive diagnostics value"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->SharedValue == 73;
			})
			.ThenServer(TEXT("Server diagnostic snapshot is complete"), [this](FReplicPIENetworkState& ServerState)
			{
				FReplicPropertyDebugInfo DebugInfo;
				ASSERT_THAT(IsTrue(UReplicLibrary::GetMarkedPropertyDebugInfo(ServerState.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), DebugInfo)));
				ASSERT_THAT(IsTrue(DebugInfo.bTargetResolved));
				ASSERT_THAT(IsTrue(DebugInfo.bHasReplicTransportComponent));
				ASSERT_THAT(IsTrue(DebugInfo.bPropertyFound));
				ASSERT_THAT(IsTrue(DebugInfo.bReplicEnabled));
				ASSERT_THAT(IsTrue(DebugInfo.bPersistentStateConfigured));
				ASSERT_THAT(AreEqual(FString(TEXT("73")), DebugInfo.LocalValue));
				ASSERT_THAT(AreEqual(FString(TEXT("73")), DebugInfo.PersistentValue));
			})
			.ThenClients(TEXT("Client diagnostic snapshot resolves locally"), [this](FReplicPIENetworkState& ClientState)
			{
				FReplicPropertyDebugInfo DebugInfo;
				ASSERT_THAT(IsTrue(UReplicLibrary::HasReplicTransportComponent(ClientState.SharedActor)));
				ASSERT_THAT(IsTrue(UReplicLibrary::GetMarkedPropertyDebugInfo(ClientState.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), DebugInfo)));
				ASSERT_THAT(IsTrue(DebugInfo.bReplicEnabled));
				ASSERT_THAT(AreEqual(FString(TEXT("73")), DebugInfo.LocalValue));
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIEOwnerOnlyPermissionTest, "Replic.Network.Permissions.OwnerOnly")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::OwnerOnly, false, EReplicPermissionMode::OwnerOnly, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(ClientCanAffectOwnedTargetButNotUnownedTarget)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::ClientOwnedActor>([](AReplicPIENetworkActor& Actor)
			{
				if (APlayerController* ClientController = GetControllerByIndex(Actor.GetWorld(), 1))
				{
					Actor.SetOwner(ClientController);
				}
			})
			.ThenClient(TEXT("Client writes/calls on owned and unowned targets"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ASSERT_THAT(IsNotNull(ClientState.ClientOwnedActor));
				AReplicPIENetworkPlayerController* ClientController = Cast<AReplicPIENetworkPlayerController>(GetFirstController(ClientState.World));
				ASSERT_THAT(IsNotNull(ClientController));

				const bool bOwnedWriteAccepted = UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.ClientOwnedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					10);
				ASSERT_THAT(IsTrue(bOwnedWriteAccepted));

				const bool bSharedWriteAccepted = UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					99);
				ASSERT_THAT(IsFalse(bSharedWriteAccepted));

				const bool bOwnedEventAccepted = UReplicLibrary::CallMarkedEvent(
					ClientController,
					ClientState.ClientOwnedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(4));
				ASSERT_THAT(IsTrue(bOwnedEventAccepted));

				const bool bSharedEventAccepted = UReplicLibrary::CallMarkedEvent(
					ClientController,
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(9));
				ASSERT_THAT(IsFalse(bSharedEventAccepted));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.ClientOwnedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					40)));
				ASSERT_THAT(IsFalse(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					99)));

				ASSERT_THAT(IsNotNull(ClientController->ReplicTransportComponent));
				ClientController->ReplicTransportComponent->SendUncheckedPropertyRequest(
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					TEXT("98"));
				ClientController->ReplicTransportComponent->SendUncheckedContainerRequest(
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					TEXT("98"));
				ClientController->ReplicTransportComponent->SendUncheckedEventRequest(
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(98));

				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue),
					101)));
			})
			.UntilServer(TEXT("Server observes owner-only results"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& IsValid(ServerState.ClientOwnedActor)
					&& ServerState.SharedActor->BatchedValue == 101
					&& ServerState.SharedActor->SharedValue == 0
					&& ServerState.SharedActor->EventValue == 0
					&& ServerState.SharedActor->SharedArray.IsEmpty()
					&& ServerState.ClientOwnedActor->SharedValue == 10
					&& ServerState.ClientOwnedActor->EventValue == 4
					&& ServerState.ClientOwnedActor->SharedArray == TArray<int32>({ 40 });
			})
			.UntilClients(TEXT("Client observes owner-only results"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& IsValid(ClientState.ClientOwnedActor)
					&& ClientState.SharedActor->BatchedValue == 101
					&& ClientState.SharedActor->SharedValue == 0
					&& ClientState.SharedActor->EventValue == 0
					&& ClientState.SharedActor->SharedArray.IsEmpty()
					&& ClientState.ClientOwnedActor->SharedValue == 10
					&& ClientState.ClientOwnedActor->EventValue == 4
					&& ClientState.ClientOwnedActor->SharedArray == TArray<int32>({ 40 });
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIEServerOnlyPermissionTest, "Replic.Network.Permissions.ServerOnly")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::ServerOnly, false, EReplicPermissionMode::ServerOnly, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(ClientRequestsAreDeniedButServerRequestsStillApply)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenClient(TEXT("Client requests are denied in ServerOnly mode"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				AReplicPIENetworkPlayerController* ClientController = Cast<AReplicPIENetworkPlayerController>(GetFirstController(ClientState.World));
				ASSERT_THAT(IsNotNull(ClientController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					15);
				ASSERT_THAT(IsFalse(bWriteAccepted));

				const bool bEventAccepted = UReplicLibrary::CallMarkedEvent(
					ClientController,
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(6));
				ASSERT_THAT(IsFalse(bEventAccepted));

				ASSERT_THAT(IsFalse(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					15)));

				ASSERT_THAT(IsNotNull(ClientController->ReplicTransportComponent));
				ClientController->ReplicTransportComponent->SendUncheckedPropertyRequest(
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					TEXT("16"));
				ClientController->ReplicTransportComponent->SendUncheckedContainerRequest(
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					TEXT("16"));
				ClientController->ReplicTransportComponent->SendUncheckedEventRequest(
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(16));

				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue),
					201)));
			})
			.UntilServer(TEXT("Denied client requests leave server state unchanged"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& ServerState.SharedActor->BatchedValue == 201
					&& ServerState.SharedActor->SharedValue == 0
					&& ServerState.SharedActor->EventValue == 0
					&& ServerState.SharedActor->SharedArray.IsEmpty();
			})
			.UntilClients(TEXT("Denied client requests leave client state unchanged"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& ClientState.SharedActor->BatchedValue == 201
					&& ClientState.SharedActor->SharedValue == 0
					&& ClientState.SharedActor->EventValue == 0
					&& ClientState.SharedActor->SharedArray.IsEmpty();
			})
			.ThenServer(TEXT("Server requests still apply in ServerOnly mode"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					25);
				ASSERT_THAT(IsTrue(bWriteAccepted));

				const bool bEventAccepted = UReplicLibrary::CallMarkedEvent(
					ServerController,
					ServerState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(7));
				ASSERT_THAT(IsTrue(bEventAccepted));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					50)));
			})
			.UntilServer(TEXT("Server observes allowed ServerOnly results"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& ServerState.SharedActor->SharedValue == 25
					&& ServerState.SharedActor->EventValue == 7
					&& ServerState.SharedActor->SharedArray == TArray<int32>({ 50 });
			})
			.UntilClients(TEXT("Client observes allowed ServerOnly results"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& ClientState.SharedActor->SharedValue == 25
					&& ClientState.SharedActor->EventValue == 7
					&& ClientState.SharedActor->SharedArray == TArray<int32>({ 50 });
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIECustomPermissionTest, "Replic.Network.Permissions.Custom")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::Custom, false, EReplicPermissionMode::Custom, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(CustomValidationBlocksUntilServerStateAllowsRequest)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenClient(TEXT("Client queues requests for authoritative custom validation"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				APlayerController* ClientController = GetFirstController(ClientState.World);
				ASSERT_THAT(IsNotNull(ClientController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					31);
				ASSERT_THAT(IsTrue(bWriteAccepted));

				const bool bEventAccepted = UReplicLibrary::CallMarkedEvent(
					ClientController,
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(9));
				ASSERT_THAT(IsTrue(bEventAccepted));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					61)));

				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue),
					301)));
			})
			.UntilServer(TEXT("Denied custom requests leave server state unchanged"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& ServerState.SharedActor->BatchedValue == 301
					&& ServerState.SharedActor->SharedValue == 0
					&& ServerState.SharedActor->EventValue == 0
					&& ServerState.SharedActor->SharedArray.IsEmpty();
			})
			.ThenServer(TEXT("Server enables custom validation"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				ServerState.SharedActor->bAllowCustomWrite = true;
				ServerState.SharedActor->bAllowCustomEvent = true;
			})
			.ThenClient(TEXT("Client requests succeed using server-only validation state"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				APlayerController* ClientController = GetFirstController(ClientState.World);
				ASSERT_THAT(IsNotNull(ClientController));
				ASSERT_THAT(IsFalse(ClientState.SharedActor->bAllowCustomWrite));
				ASSERT_THAT(IsFalse(ClientState.SharedActor->bAllowCustomEvent));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					30);
				ASSERT_THAT(IsTrue(bWriteAccepted));

				const bool bEventAccepted = UReplicLibrary::CallMarkedEvent(
					ClientController,
					ClientState.SharedActor,
					GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse),
					MakePulseArguments(8));
				ASSERT_THAT(IsTrue(bEventAccepted));

				ASSERT_THAT(IsTrue(RequestIntContainerDelta(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
					EReplicContainerDeltaOperation::AddArrayItem,
					60)));

				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue),
					302)));
			})
			.UntilServer(TEXT("Server observes allowed custom results"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& ServerState.SharedActor->BatchedValue == 302
					&& ServerState.SharedActor->SharedValue == 30
					&& ServerState.SharedActor->EventValue == 8
					&& ServerState.SharedActor->SharedArray == TArray<int32>({ 60 });
			})
			.UntilClients(TEXT("Client observes allowed custom results"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& ClientState.SharedActor->BatchedValue == 302
					&& ClientState.SharedActor->SharedValue == 30
					&& ClientState.SharedActor->EventValue == 8
					&& ClientState.SharedActor->SharedArray == TArray<int32>({ 60 });
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIEObserverTest, "Replic.Network.Observers.PropertyChanged")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::None, true, EReplicPermissionMode::None, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(BindAndUnbindTrackReplicChangesAcrossPIE)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenServer(TEXT("Server binds observer"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				ServerState.PropertyObserver = UReplicLibrary::BindMarkedPropertyChanged(
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue));
				ASSERT_THAT(IsNotNull(ServerState.PropertyObserver));

				ServerState.ObserverSink = NewObject<UReplicPIEObserverSink>(ServerState.PropertyObserver);
				ASSERT_THAT(IsNotNull(ServerState.ObserverSink));
				ServerState.PropertyObserver->OnChanged.AddDynamic(ServerState.ObserverSink, &UReplicPIEObserverSink::HandleObservedChange);
			})
			.ThenClient(TEXT("Client binds observer"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ClientState.PropertyObserver = UReplicLibrary::BindMarkedPropertyChanged(
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue));
				ASSERT_THAT(IsNotNull(ClientState.PropertyObserver));

				ClientState.ObserverSink = NewObject<UReplicPIEObserverSink>(ClientState.PropertyObserver);
				ASSERT_THAT(IsNotNull(ClientState.ObserverSink));
				ClientState.PropertyObserver->OnChanged.AddDynamic(ClientState.ObserverSink, &UReplicPIEObserverSink::HandleObservedChange);
			})
			.ThenServer(TEXT("Server writes first observed value"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					41);
				ASSERT_THAT(IsTrue(bWriteAccepted));
			})
			.UntilServer(TEXT("Server observer receives first change"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& IsValid(ServerState.ObserverSink)
					&& ServerState.SharedActor->SharedValue == 41
					&& ServerState.ObserverSink->ChangeCount == 1
					&& ServerState.ObserverSink->LastTargetObject == ServerState.SharedActor
					&& ServerState.ObserverSink->LastPropertyName == GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue);
			})
			.UntilClient(TEXT("Client observer receives first change"), 0, [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& IsValid(ClientState.ObserverSink)
					&& ClientState.SharedActor->SharedValue == 41
					&& ClientState.ObserverSink->ChangeCount == 1
					&& ClientState.ObserverSink->LastTargetObject == ClientState.SharedActor
					&& ClientState.ObserverSink->LastPropertyName == GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue);
			})
			.ThenClient(TEXT("Client unbinds observer"), 0, [](FReplicPIENetworkState& ClientState)
			{
				if (ClientState.PropertyObserver)
				{
					UReplicLibrary::UnbindMarkedPropertyChanged(ClientState.PropertyObserver);
				}
			})
			.ThenServer(TEXT("Server writes second observed value"), [this](FReplicPIENetworkState& ServerState)
			{
				ASSERT_THAT(IsNotNull(ServerState.SharedActor));
				APlayerController* ServerController = GetFirstController(ServerState.World);
				ASSERT_THAT(IsNotNull(ServerController));

				const bool bWriteAccepted = UReplicLibrary::SetMarkedInt(
					ServerController,
					ServerState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
					52);
				ASSERT_THAT(IsTrue(bWriteAccepted));
			})
			.UntilServer(TEXT("Server observer receives second change"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor)
					&& IsValid(ServerState.ObserverSink)
					&& ServerState.SharedActor->SharedValue == 52
					&& ServerState.ObserverSink->ChangeCount == 2;
			})
			.UntilClient(TEXT("Client value changes without second observer callback"), 0, [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor)
					&& IsValid(ClientState.ObserverSink)
					&& ClientState.SharedActor->SharedValue == 52
					&& ClientState.ObserverSink->ChangeCount == 1;
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIEBatchingTest, "Replic.Network.Batching.PersistentState")
{
	FPIENetworkComponent<FReplicPIENetworkState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureReplicPIENetworkMetadata(EReplicPermissionMode::None, true, EReplicPermissionMode::None, EReplicEventMode::ReplicateAll);
		BuildListenServerNetwork(Network);
	}

	TEST_METHOD(ClientRapidWritesCollapseToLatestPersistentValue)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicPIENetworkState::SharedActor>()
			.ThenClient(TEXT("Client performs rapid batched writes"), 0, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				APlayerController* ClientController = GetFirstController(ClientState.World);
				ASSERT_THAT(IsNotNull(ClientController));

				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue),
					101)));
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue),
					202)));
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(
					ClientController,
					ClientState.SharedActor,
					GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, BatchedValue),
					303)));
			})
			.UntilServer(TEXT("Server observes final batched value"), [](FReplicPIENetworkState& ServerState)
			{
				return IsValid(ServerState.SharedActor) && ServerState.SharedActor->BatchedValue == 303;
			})
			.UntilClients(TEXT("Connected client observes final batched value"), [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->BatchedValue == 303;
			})
			.ThenClientJoins()
			.UntilClient(TEXT("Late client receives final batched persistent value"), 1, [](FReplicPIENetworkState& ClientState)
			{
				return IsValid(ClientState.SharedActor) && ClientState.SharedActor->BatchedValue == 303;
			})
			.ThenClient(TEXT("Late client sees final batched value"), 1, [this](FReplicPIENetworkState& ClientState)
			{
				ASSERT_THAT(IsNotNull(ClientState.SharedActor));
				ASSERT_THAT(AreEqual(303, ClientState.SharedActor->BatchedValue));
			});
	}
};

#endif
