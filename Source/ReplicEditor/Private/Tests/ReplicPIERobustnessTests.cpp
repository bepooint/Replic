#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "ReplicLibrary.h"
#include "ReplicMetadata.h"
#include "ReplicPropertyObserver.h"
#include "ReplicRuntimeUtils.h"
#include "ReplicTransportComponent.h"
#include "Tests/ReplicPIENetworkTestActors.h"

namespace
{
	constexpr int32 ReplicatedActorCount = 32;
	constexpr int32 EventBurstCount = 64;
	constexpr int32 LargeContainerEntryCount = 512;

	void EnableVariable(const FName PropertyName, const bool bPersistent = true)
	{
		if (FProperty* Property = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), PropertyName))
		{
			Property->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			Property->SetMetaData(ReplicMetadata::VariablePersistent, bPersistent ? TEXT("true") : TEXT("false"));
			Property->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}
	}

	void EnableEvent(const FName EventName)
	{
		if (UFunction* Function = AReplicPIENetworkActor::StaticClass()->FindFunctionByName(EventName))
		{
			Function->SetMetaData(ReplicMetadata::EventEnabled, TEXT("true"));
			Function->SetMetaData(ReplicMetadata::EventPermissionMode, TEXT("None"));
			Function->SetMetaData(ReplicMetadata::EventMode, TEXT("ReplicateAll"));
		}
	}

	void ConfigureRobustnessMetadata()
	{
		EnableVariable(GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue));
		EnableVariable(GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray));
		EnableVariable(GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState));
		EnableVariable(GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState));
		EnableEvent(GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse));
		EnableEvent(GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedReferencePulse));
	}

	APlayerController* GetFirstController(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	template<typename ActorType>
	ActorType* FindFirstActor(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	AReplicPIENetworkActor* FindActorBySharedValue(UWorld* World, const int32 SharedValue)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AReplicPIENetworkActor> It(World); It; ++It)
		{
			if (It->SharedValue == SharedValue)
			{
				return *It;
			}
		}
		return nullptr;
	}

	int32 CountNetworkActors(UWorld* World)
	{
		int32 Count = 0;
		if (World)
		{
			for (TActorIterator<AReplicPIENetworkActor> It(World); It; ++It)
			{
				++Count;
			}
		}
		return Count;
	}

	bool HasCompleteActorLoad(UWorld* World)
	{
		if (CountNetworkActors(World) != ReplicatedActorCount)
		{
			return false;
		}

		for (int32 Index = 0; Index < ReplicatedActorCount; ++Index)
		{
			if (!FindActorBySharedValue(World, 1000 + Index))
			{
				return false;
			}
		}
		return true;
	}

	bool HasLargeContainers(UWorld* World)
	{
		const AReplicPIENetworkActor* Actor = FindActorBySharedValue(World, 1000);
		return Actor
			&& Actor->SharedArray.Num() == LargeContainerEntryCount
			&& Actor->IntSetState.Num() == LargeContainerEntryCount
			&& Actor->IntIntMapState.Num() == LargeContainerEntryCount
			&& Actor->SharedArray[LargeContainerEntryCount - 1] == LargeContainerEntryCount - 1
			&& Actor->IntSetState.Contains(LargeContainerEntryCount - 1)
			&& Actor->IntIntMapState.FindRef(LargeContainerEntryCount - 1) == (LargeContainerEntryCount - 1) * 10;
	}

	bool RequestFullPropertyWrite(APlayerController* Controller, AReplicPIENetworkActor* Actor, const FName PropertyName)
	{
		if (!Controller || !Actor)
		{
			return false;
		}

		FProperty* Property = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), PropertyName);
		if (!Property)
		{
			return false;
		}

		FString SerializedValue;
		if (!ReplicRuntimeUtils::ExportPropertyValueToText(Property, Property->ContainerPtrToValuePtr<void>(Actor), SerializedValue))
		{
			return false;
		}

		UReplicLibrary::RegisterObservedObject(Actor);
		UReplicTransportComponent* Transport = UReplicTransportComponent::FindOrCreate(Controller);
		return Transport && Transport->RequestMarkedPropertyWrite(Controller, Actor, PropertyName, SerializedValue);
	}

	bool HasReplicatedComponentLocations(UWorld* World)
	{
		const AReplicPIEComponentLoadActor* Actor = FindFirstActor<AReplicPIEComponentLoadActor>(World);
		if (!Actor || Actor->TrackedComponents.Num() != 24)
		{
			return false;
		}

		for (int32 Index = 0; Index < Actor->TrackedComponents.Num(); ++Index)
		{
			const USceneComponent* Component = Actor->TrackedComponents[Index];
			if (!Component || !Component->GetRelativeLocation().Equals(FVector(Index * 10.0, Index * 2.0, Index * 3.0), 0.01))
			{
				return false;
			}
		}
		return true;
	}

	struct FReplicRobustnessState : public FBasePIENetworkComponentState
	{
		AReplicPIENetworkActor* SharedActor = nullptr;
		AReplicPIENetworkActor* ReferenceActor = nullptr;
		AReplicPIEComponentLoadActor* ComponentActor = nullptr;
		UReplicPropertyObserver* FirstObserver = nullptr;
		UReplicPropertyObserver* SecondObserver = nullptr;
		UReplicPIEObserverSink* FirstSink = nullptr;
		UReplicPIEObserverSink* SecondSink = nullptr;
	};

	template<typename StateType>
	void BuildListenServerNetwork(FPIENetworkComponent<StateType>& Network, const int32 ClientCount)
	{
		FNetworkComponentBuilder<StateType>()
			.WithClients(ClientCount)
			.AsListenServer()
			.WithGameMode(AReplicPIENetworkGameMode::StaticClass())
			.Build(Network);
	}

	template<typename StateType>
	class FReplicTravelNetworkComponent : public FPIENetworkComponent<StateType>
	{
		using Super = FPIENetworkComponent<StateType>;

	public:
		FReplicTravelNetworkComponent(FAutomationTestBase* InTestRunner, FTestCommandBuilder& InCommandBuilder, const bool bInInitializing)
			: Super(InTestRunner, InCommandBuilder, bInInitializing)
		{
		}

		FReplicTravelNetworkComponent& RefreshWorldsAfterTravel(const FString& DestinationMapName)
		{
			this->Until(TEXT("Server and clients finish map travel"), [this, DestinationMapName]()
			{
				int32 ReadyServers = 0;
				int32 ReadyClients = 0;
				for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
				{
					UWorld* World = WorldContext.World();
					if (WorldContext.WorldType != EWorldType::PIE || !IsValid(World) || !IsValid(World->GetNetDriver()) || !World->GetMapName().Contains(DestinationMapName))
					{
						continue;
					}
					if (World->GetNetDriver()->IsServer())
					{
						++ReadyServers;
					}
					else
					{
						++ReadyClients;
					}
				}
				return ReadyServers == 1 && ReadyClients == this->ServerState->ClientCount;
			});
			this->Then(TEXT("Clear stale CQTest world pointers after travel"), [this]()
			{
				this->ServerState->World = nullptr;
				for (TUniquePtr<FBasePIENetworkComponentState>& ClientState : this->ClientStates)
				{
					ClientState->World = nullptr;
				}
				this->SpawnedActors.Reset();
			});
			this->Until(TEXT("PIE worlds are refreshed after map travel"), [this]()
			{
				return this->SetWorlds();
			});
			return *this;
		}

		FReplicTravelNetworkComponent& ReconnectSingleDisconnectedClient()
		{
			const FTimespan Timeout = this->MakeTimeout(CQTest::DefaultTimeout);
			this->Do(TEXT("Replace disconnected CQTest client state"), [this]()
			{
				this->ClientStates.Reset();
				this->ServerState->ClientCount = 1;
				this->ServerState->ClientConnections.SetNum(1);
				this->ServerState->ClientConnections[0] = nullptr;

				TUniquePtr<StateType> NewClientState = MakeUnique<StateType>(StateType{});
				NewClientState->ClientIndex = 0;
				this->ClientStates.Add(MoveTemp(NewClientState));
				GEditor->RequestLateJoin();
			});
			this->Until(TEXT("Set reconnected client world"), [this]() { return this->SetWorlds(); }, Timeout);
			this->Then(TEXT("Apply packet settings after reconnect"), [this]() { this->SetPacketSettings(); });
			this->Then(TEXT("Connect replacement client"), [this]() { this->ConnectClientsToServer(); });
			this->Until(TEXT("Replacement client is ready"), [this]() { return this->AwaitClientsReady(); }, Timeout);
			return *this;
		}
	};
}

NETWORK_TEST_CLASS(FReplicPIEFourPlayerScaleTest, "Replic.Network.Robustness.PlayerScale")
{
	FPIENetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 3);
	}

	TEST_METHOD(FourPlayersReceivePersistentState)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.ThenServer(TEXT("Server writes value for four-player test"), [this](FReplicRobustnessState& State)
			{
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(GetFirstController(State.World), State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 404)));
			})
			.UntilClients(TEXT("All three clients receive four-player value"), [](FReplicRobustnessState& State)
			{
				return IsValid(State.SharedActor) && State.SharedActor->SharedValue == 404;
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIESixPlayerScaleTest, "Replic.Network.Robustness.PlayerScale")
{
	FPIENetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 5);
	}

	TEST_METHOD(SixPlayersReceivePersistentState)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.ThenServer(TEXT("Server writes value for six-player test"), [this](FReplicRobustnessState& State)
			{
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(GetFirstController(State.World), State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 606)));
			})
			.UntilClients(TEXT("All five clients receive six-player value"), [](FReplicRobustnessState& State)
			{
				return IsValid(State.SharedActor) && State.SharedActor->SharedValue == 606;
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIELoadTest, "Replic.Network.Robustness.Load")
{
	FPIENetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 1);
	}

	TEST_METHOD(ActorsEventsAndLargeContainersRemainConsistent)
	{
		Network
			.ThenServer(TEXT("Server spawns and writes 32 Replic actors"), [this](FReplicRobustnessState& State)
			{
				APlayerController* Controller = GetFirstController(State.World);
				ASSERT_THAT(IsNotNull(Controller));
				for (int32 Index = 0; Index < ReplicatedActorCount; ++Index)
				{
					AReplicPIENetworkActor* Actor = State.World->SpawnActor<AReplicPIENetworkActor>();
					ASSERT_THAT(IsNotNull(Actor));
					ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(Controller, Actor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 1000 + Index)));
				}
			})
			.UntilServer(TEXT("Server has all actor values"), [](FReplicRobustnessState& State) { return HasCompleteActorLoad(State.World); })
			.UntilClients(TEXT("Client has all actor values"), [](FReplicRobustnessState& State) { return HasCompleteActorLoad(State.World); })
			.ThenServer(TEXT("Server dispatches 64 events in one burst"), [this](FReplicRobustnessState& State)
			{
				AReplicPIENetworkActor* Actor = FindActorBySharedValue(State.World, 1000);
				APlayerController* Controller = GetFirstController(State.World);
				ASSERT_THAT(IsNotNull(Actor));
				for (int32 Index = 0; Index < EventBurstCount; ++Index)
				{
					TArray<FReplicNamedValue> Arguments;
					Arguments.Add(UReplicLibrary::MakeNamedIntValue(TEXT("Delta"), 1));
					ASSERT_THAT(IsTrue(UReplicLibrary::CallMarkedEvent(Controller, Actor, GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse), Arguments)));
				}
			})
			.UntilServer(TEXT("Server receives full event burst"), [](FReplicRobustnessState& State)
			{
				const AReplicPIENetworkActor* Actor = FindActorBySharedValue(State.World, 1000);
				return Actor && Actor->EventValue == EventBurstCount;
			})
			.UntilClients(TEXT("Client receives full event burst"), [](FReplicRobustnessState& State)
			{
				const AReplicPIENetworkActor* Actor = FindActorBySharedValue(State.World, 1000);
				return Actor && Actor->EventValue == EventBurstCount;
			})
			.ThenServer(TEXT("Server writes 512-entry array, set, and map"), [this](FReplicRobustnessState& State)
			{
				AReplicPIENetworkActor* Actor = FindActorBySharedValue(State.World, 1000);
				APlayerController* Controller = GetFirstController(State.World);
				ASSERT_THAT(IsNotNull(Actor));
				for (int32 Index = 0; Index < LargeContainerEntryCount; ++Index)
				{
					Actor->SharedArray.Add(Index);
					Actor->IntSetState.Add(Index);
					Actor->IntIntMapState.Add(Index, Index * 10);
				}
				ASSERT_THAT(IsTrue(RequestFullPropertyWrite(Controller, Actor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray))));
				ASSERT_THAT(IsTrue(RequestFullPropertyWrite(Controller, Actor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState))));
				ASSERT_THAT(IsTrue(RequestFullPropertyWrite(Controller, Actor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState))));
			})
			.UntilServer(TEXT("Server keeps all large container entries"), [](FReplicRobustnessState& State) { return HasLargeContainers(State.World); })
			.UntilClients(TEXT("Connected client receives all large container entries"), [](FReplicRobustnessState& State) { return HasLargeContainers(State.World); })
			.ThenClientJoins()
			.UntilClient(TEXT("Late client receives all large persistent containers"), 1, [](FReplicRobustnessState& State) { return HasLargeContainers(State.World); });
	}
};

NETWORK_TEST_CLASS(FReplicPIEComponentLoadTest, "Replic.Network.Robustness.ComponentLoad")
{
	FPIENetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 1);
	}

	TEST_METHOD(ComponentTransformsReachConnectedAndLateClients)
	{
		Network
			.SpawnAndReplicate<AReplicPIEComponentLoadActor, &FReplicRobustnessState::ComponentActor>()
			.ThenServer(TEXT("Server moves 24 tracked components"), [this](FReplicRobustnessState& State)
			{
				ASSERT_THAT(IsNotNull(State.ComponentActor));
				for (int32 Index = 0; Index < State.ComponentActor->TrackedComponents.Num(); ++Index)
				{
					State.ComponentActor->TrackedComponents[Index]->SetRelativeLocation(FVector(Index * 10.0, Index * 2.0, Index * 3.0));
				}
				State.ComponentActor->ReplicTransportComponent->RefreshComponentTransformTracking(true, true);
			})
			.UntilClients(TEXT("Connected client receives 24 component transforms"), [](FReplicRobustnessState& State) { return HasReplicatedComponentLocations(State.World); })
			.ThenClientJoins()
			.UntilClient(TEXT("Late client receives persistent component transforms"), 1, [](FReplicRobustnessState& State) { return HasReplicatedComponentLocations(State.World); });
	}
};

NETWORK_TEST_CLASS(FReplicPIEReferenceArgumentTest, "Replic.Network.Robustness.ReferenceArguments")
{
	FPIENetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 1);
	}

	TEST_METHOD(ObjectAndClassReferencesResolveOnEveryMachine)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::ReferenceActor>()
			.ThenServer(TEXT("Server calls event with actor and class references"), [this](FReplicRobustnessState& State)
			{
				TArray<FReplicNamedValue> Arguments;
				Arguments.Add(UReplicLibrary::MakeNamedObjectValue(TEXT("ActorValue"), State.ReferenceActor));
				Arguments.Add(UReplicLibrary::MakeNamedClassValue(TEXT("ClassValue"), AReplicPIENetworkActor::StaticClass()));
				ASSERT_THAT(IsTrue(UReplicLibrary::CallMarkedEvent(GetFirstController(State.World), State.SharedActor, GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedReferencePulse), Arguments)));
			})
			.UntilServer(TEXT("Server resolves both reference arguments"), [](FReplicRobustnessState& State)
			{
				return State.SharedActor && State.SharedActor->LastEventActor == State.ReferenceActor && State.SharedActor->LastEventClass == AReplicPIENetworkActor::StaticClass();
			})
			.UntilClients(TEXT("Clients resolve local actor and class references"), [](FReplicRobustnessState& State)
			{
				return State.SharedActor && State.SharedActor->LastEventActor == State.ReferenceActor && State.SharedActor->LastEventClass == AReplicPIENetworkActor::StaticClass();
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIEObserverLifetimeTest, "Replic.Network.Robustness.ObserverLifetime")
{
	FPIENetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 1);
	}

	TEST_METHOD(RepeatedBindingsUnbindIndependentlyAndCleanUpOnDestroy)
	{
		auto BindTwoObservers = [this](FReplicRobustnessState& State)
		{
			State.FirstObserver = UReplicLibrary::BindMarkedPropertyChanged(State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue));
			State.SecondObserver = UReplicLibrary::BindMarkedPropertyChanged(State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue));
			ASSERT_THAT(IsNotNull(State.FirstObserver));
			ASSERT_THAT(IsNotNull(State.SecondObserver));
			State.FirstSink = NewObject<UReplicPIEObserverSink>(State.FirstObserver);
			State.SecondSink = NewObject<UReplicPIEObserverSink>(State.SecondObserver);
			State.FirstObserver->OnChanged.AddDynamic(State.FirstSink, &UReplicPIEObserverSink::HandleObservedChange);
			State.SecondObserver->OnChanged.AddDynamic(State.SecondSink, &UReplicPIEObserverSink::HandleObservedChange);
		};

		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.ThenServer(TEXT("Server creates two observers for one property"), BindTwoObservers)
			.ThenClients(TEXT("Client creates two observers for one property"), BindTwoObservers)
			.ThenServer(TEXT("Server writes first observed value"), [this](FReplicRobustnessState& State)
			{
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(GetFirstController(State.World), State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 1)));
			})
			.UntilServer(TEXT("Both server observers fire exactly once"), [](FReplicRobustnessState& State) { return State.FirstSink && State.SecondSink && State.FirstSink->ChangeCount == 1 && State.SecondSink->ChangeCount == 1; })
			.UntilClients(TEXT("Both client observers fire exactly once"), [](FReplicRobustnessState& State) { return State.FirstSink && State.SecondSink && State.FirstSink->ChangeCount == 1 && State.SecondSink->ChangeCount == 1; })
			.ThenServer(TEXT("Server unbinds only first observer"), [](FReplicRobustnessState& State) { State.FirstObserver->Unbind(); })
			.ThenClients(TEXT("Clients unbind only first observer"), [](FReplicRobustnessState& State) { State.FirstObserver->Unbind(); })
			.ThenServer(TEXT("Server writes second observed value"), [this](FReplicRobustnessState& State)
			{
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(GetFirstController(State.World), State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 2)));
			})
			.UntilServer(TEXT("Only second server observer receives second change"), [](FReplicRobustnessState& State) { return State.FirstSink->ChangeCount == 1 && State.SecondSink->ChangeCount == 2; })
			.UntilClients(TEXT("Only second client observer receives second change"), [](FReplicRobustnessState& State) { return State.FirstSink->ChangeCount == 1 && State.SecondSink->ChangeCount == 2; })
			.ThenServer(TEXT("Server destroys observed actor"), [](FReplicRobustnessState& State) { State.SharedActor->Destroy(); })
			.UntilServer(TEXT("Server observer automatically unbinds on actor destruction"), [](FReplicRobustnessState& State) { return State.SecondObserver && !State.SecondObserver->IsBound(); })
			.UntilClients(TEXT("Client observer automatically unbinds on replicated actor destruction"), [](FReplicRobustnessState& State) { return State.SecondObserver && !State.SecondObserver->IsBound(); });
	}
};

NETWORK_TEST_CLASS(FReplicPIEDisconnectTest, "Replic.Network.Robustness.Disconnect")
{
	FReplicTravelNetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 1);
	}

	TEST_METHOD(ClientDisconnectAndFreshReconnectReceivePersistentState)
	{
		TestRunner->AddExpectedError(TEXT("UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost"));
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.ThenServer(TEXT("Server writes state before client disconnect"), [this](FReplicRobustnessState& State)
			{
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(GetFirstController(State.World), State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 77)));
			})
			.UntilClients(TEXT("Initial client receives state before disconnect"), [](FReplicRobustnessState& State) { return State.SharedActor && State.SharedActor->SharedValue == 77; })
			.ThenClient(TEXT("Initial client closes its server connection"), 0, [this](FReplicRobustnessState& State)
			{
				UNetDriver* NetDriver = State.World->GetNetDriver();
				ASSERT_THAT(IsNotNull(NetDriver));
				ASSERT_THAT(IsNotNull(NetDriver->ServerConnection));
				NetDriver->ServerConnection->Close();
			})
			.UntilServer(TEXT("Server observes initial client disconnect"), [](FReplicRobustnessState& State)
			{
				const UNetDriver* NetDriver = State.World ? State.World->GetNetDriver() : nullptr;
				return NetDriver && (NetDriver->ClientConnections.IsEmpty() || NetDriver->ClientConnections[0]->GetConnectionState() == USOCK_Closed);
			})
			;
		Network.ReconnectSingleDisconnectedClient();
		Network
			.UntilClient(TEXT("Freshly connected client receives persistent state"), 0, [](FReplicRobustnessState& State)
			{
				return FindActorBySharedValue(State.World, 77) != nullptr;
			});
	}

	TEST_METHOD(HostDisconnectClosesClientServerConnection)
	{
		TestRunner->AddExpectedError(TEXT("UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost"));
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.ThenServer(TEXT("Host closes all client connections"), [this](FReplicRobustnessState& State)
			{
				UNetDriver* NetDriver = State.World->GetNetDriver();
				ASSERT_THAT(IsNotNull(NetDriver));
				for (UNetConnection* Connection : NetDriver->ClientConnections)
				{
					if (Connection)
					{
						Connection->Close();
					}
				}
			})
			.UntilClient(TEXT("Client detects host disconnect"), 0, [](FReplicRobustnessState& State)
			{
				const UNetDriver* NetDriver = State.World ? State.World->GetNetDriver() : nullptr;
				return !NetDriver || !NetDriver->ServerConnection || NetDriver->ServerConnection->GetConnectionState() == USOCK_Closed;
			});
	}
};

NETWORK_TEST_CLASS(FReplicPIETravelTest, "Replic.Network.Robustness.Travel")
{
	FReplicTravelNetworkComponent<FReplicRobustnessState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	BEFORE_EACH()
	{
		ConfigureRobustnessMetadata();
		BuildListenServerNetwork(Network, 1);
	}

	TEST_METHOD(NonSeamlessTravelStartsCleanAndReplicStillWorks)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.ThenServer(TEXT("Server starts non-seamless map travel"), [this](FReplicRobustnessState& State)
			{
				AGameModeBase* GameMode = State.World->GetAuthGameMode<AGameModeBase>();
				ASSERT_THAT(IsNotNull(GameMode));
				GameMode->bUseSeamlessTravel = false;
				ASSERT_THAT(IsTrue(State.World->ServerTravel(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson?listen?game=/Script/ReplicEditor.ReplicPIENetworkGameMode"), false)));
			});
		Network.RefreshWorldsAfterTravel(TEXT("Lvl_ThirdPerson"));
		Network
			.ThenServer(TEXT("Server spawns and writes state after non-seamless travel"), [this](FReplicRobustnessState& State)
			{
				State.SharedActor = State.World->SpawnActor<AReplicPIENetworkActor>();
				ASSERT_THAT(IsNotNull(State.SharedActor));
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(GetFirstController(State.World), State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 88)));
			})
			.UntilClients(TEXT("Client receives Replic state after non-seamless travel"), [](FReplicRobustnessState& State)
			{
				State.SharedActor = FindActorBySharedValue(State.World, 88);
				return IsValid(State.SharedActor);
			});
	}

	TEST_METHOD(SeamlessTravelKeepsReplicActorState)
	{
		Network
			.SpawnAndReplicate<AReplicPIENetworkActor, &FReplicRobustnessState::SharedActor>()
			.ThenServer(TEXT("Server writes persistent state before seamless travel"), [this](FReplicRobustnessState& State)
			{
				ASSERT_THAT(IsTrue(UReplicLibrary::SetMarkedInt(GetFirstController(State.World), State.SharedActor, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue), 99)));
			})
			.UntilClients(TEXT("Client receives state before seamless travel"), [](FReplicRobustnessState& State) { return State.SharedActor && State.SharedActor->SharedValue == 99; })
			.ThenServer(TEXT("Server starts seamless map travel"), [this](FReplicRobustnessState& State)
			{
				IConsoleVariable* AllowPIESeamlessTravel = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel"));
				ASSERT_THAT(IsNotNull(AllowPIESeamlessTravel));
				AllowPIESeamlessTravel->Set(1, ECVF_SetByCode);

				AGameModeBase* GameMode = State.World->GetAuthGameMode<AGameModeBase>();
				ASSERT_THAT(IsNotNull(GameMode));
				GameMode->bUseSeamlessTravel = true;
				ASSERT_THAT(IsTrue(State.World->ServerTravel(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson?listen?game=/Script/ReplicEditor.ReplicPIENetworkGameMode"), false)));
			});
		Network.RefreshWorldsAfterTravel(TEXT("Lvl_ThirdPerson"));
		Network
			.UntilServer(TEXT("Server keeps actor state through seamless travel"), [](FReplicRobustnessState& State)
			{
				State.SharedActor = FindActorBySharedValue(State.World, 99);
				return IsValid(State.SharedActor);
			})
			.UntilClients(TEXT("Client keeps actor state through seamless travel"), [](FReplicRobustnessState& State)
			{
				State.SharedActor = FindActorBySharedValue(State.World, 99);
				return IsValid(State.SharedActor);
			});
	}
};

#endif
