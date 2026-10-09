#include "Component/CadenceArcChargeMontageComponent.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/CadenceArcAutomationTags.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

struct FCadenceArcChargeMontageTestAccess
{
	static const FCadenceArcChargeMontageEntry* Find(const UCadenceArcChargeMontageComponent& Component, FGameplayTag Input, FGameplayTag Source)
	{
		return Component.FindEntry(Input, Source);
	}
	static bool Plan(const FCadenceArcChargeMontageEntry& Entry, const FCadenceArcHoldSnapshot& Snapshot,
		ECadenceArcHoldStage Stage, double Delay, float& Position, float& Rate)
	{
		UCadenceArcChargeMontageComponent::FPlaybackPlan Result;
		const bool bValid = UCadenceArcChargeMontageComponent::MakePlaybackPlan(Entry, Snapshot, Stage, Delay, Result);
		Position = Result.Position;
		Rate = Result.PlayRate;
		return bValid;
	}
	static bool Validate(const FCadenceArcChargeMontageEntry& Entry)
	{
		FString Error;
		return UCadenceArcChargeMontageComponent::ValidateEntry(Entry, Error);
	}
	static void Own(UCadenceArcChargeMontageComponent& Component, UAnimInstance* Anim, FAnimMontageInstance* Instance,
		const FCadenceArcChargeMontageEntry& Entry)
	{
		Component.ActivePlayback.AnimInstance = Anim;
		Component.ActivePlayback.Montage = Entry.Montage;
		Component.ActivePlayback.InstanceId = Instance->GetInstanceID();
		Component.ActivePlayback.Entry = Entry;
		Component.ActivePlayback.InputTag = Entry.InputTag;
	}
	static bool Configure(FAnimMontageInstance& Instance, const FCadenceArcChargeMontageEntry& Entry, float Position, float Rate)
	{
		UCadenceArcChargeMontageComponent::FPlaybackPlan Plan;
		Plan.Position = Position;
		Plan.PlayRate = Rate;
		return UCadenceArcChargeMontageComponent::ConfigureInstance(Instance, Entry, Plan);
	}
	static void EndHold(UCadenceArcChargeMontageComponent& Component, FGameplayTag Input)
	{
		Component.HandleHoldEnded(Input, ECadenceArcHoldEndReason::Released);
	}
	static uint64 PendingFrame(const UCadenceArcChargeMontageComponent& Component) { return Component.PendingStops[0].EndedFrame; }
	static int32 PendingCount(const UCadenceArcChargeMontageComponent& Component) { return Component.PendingStops.Num(); }
	static void Flush(UCadenceArcChargeMontageComponent& Component, uint64 Frame, bool bForce = false) { Component.FlushPendingStops(Frame, bForce); }
};

namespace
{
	const CadenceArc::Tests::FAutomationTag InputTag{TEXT("CadenceArc.Automation.Animation.Input")};
	const CadenceArc::Tests::FAutomationTag SourceTag{TEXT("CadenceArc.Automation.Animation.Source")};
	const CadenceArc::Tests::FAutomationTag OtherTag{TEXT("CadenceArc.Automation.Animation.Other")};

	struct FAnimationFixture
	{
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
		UAnimInstance* Anim = NewObject<UAnimInstance>(Mesh);
		UAnimMontage* Montage = NewObject<UAnimMontage>();
		UCadenceArcChargeMontageComponent* Component = NewObject<UCadenceArcChargeMontageComponent>();
		FCadenceArcChargeMontageEntry Entry;

		FAnimationFixture()
		{
			USkeleton* Skeleton = NewObject<USkeleton>();
			Montage->SetSkeleton(Skeleton);
			Anim->CurrentSkeleton = Skeleton;
			// 通过基类虚调用设置长度，不依赖编辑器动画数据控制器或真实内容资产。
			UAnimCompositeBase* Composite = Montage;
			Composite->SetCompositeLength(4.0f);
			Montage->SlotAnimTracks.SetNum(1);
			Montage->SlotAnimTracks[0].SlotName = TEXT("DefaultSlot");
			Montage->AddAnimCompositeSection(TEXT("Windup"), 0.0f);
			Montage->AddAnimCompositeSection(TEXT("Hold"), 2.0f);
			Entry.InputTag = InputTag;
			Entry.Montage = Montage;
		}

		FAnimMontageInstance* Play()
		{
			if (Anim->Montage_Play(Montage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false) <= 0.0f) return nullptr;
			return Anim->GetActiveInstanceForMontage(Montage);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcChargeMontagePlanTest, "CadenceArc.Animation.ChargeMontage.RatesAndDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcChargeMontagePlanTest::RunTest(const FString& Parameters)
{
	FAnimationFixture Fixture;
	FCadenceArcHoldSnapshot Snapshot;
	Snapshot.ChargeStartSeconds = 0.5;
	Snapshot.ChargeFullSeconds = 1.5;
	Snapshot.MaxChargedHoldSeconds = 2.0;
	Fixture.Montage->RateScale = 2.0f;
	float Position = 0.0f, Rate = 0.0f;
	TestTrue(TEXT("Charging plan"), FCadenceArcChargeMontageTestAccess::Plan(Fixture.Entry, Snapshot, ECadenceArcHoldStage::Charging, 0.25, Position, Rate));
	TestEqual(TEXT("Windup compensates late threshold notification"), Position, 0.5f);
	TestEqual(TEXT("Windup accounts for asset RateScale"), Rate, 1.0f);
	TestTrue(TEXT("Already charged plan"), FCadenceArcChargeMontageTestAccess::Plan(Fixture.Entry, Snapshot, ECadenceArcHoldStage::Charged, 0.5, Position, Rate));
	TestEqual(TEXT("Hold position compensates delay from full threshold"), Position, 2.485f);
	TestEqual(TEXT("Hold consumes 97 percent during maximum hold duration"), Rate, 0.485f);
	Snapshot.MaxChargedHoldSeconds = 0.0;
	TestTrue(TEXT("Zero maximum has no division by zero"), FCadenceArcChargeMontageTestAccess::Plan(Fixture.Entry, Snapshot, ECadenceArcHoldStage::Charged, 0.0, Position, Rate));
	Snapshot.ChargeFullSeconds = Snapshot.ChargeStartSeconds;
	TestFalse(TEXT("Zero charging interval rejected"), FCadenceArcChargeMontageTestAccess::Plan(Fixture.Entry, Snapshot, ECadenceArcHoldStage::Charging, 0.0, Position, Rate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcChargeMontageOwnershipTest, "CadenceArc.Animation.ChargeMontage.OwnedInstanceCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcChargeMontageOwnershipTest::RunTest(const FString& Parameters)
{
	FAnimationFixture Fixture;
	FAnimMontageInstance* Original = Fixture.Play();
	if (!TestNotNull(TEXT("Real engine montage playback"), Original)) return false;
	TestTrue(TEXT("Configure runtime links"), FCadenceArcChargeMontageTestAccess::Configure(*Original, Fixture.Entry, 0.5f, 0.75f));
	TestEqual(TEXT("Windup points to Hold"), Original->GetNextSectionID(0), 1);
	TestEqual(TEXT("Hold loops"), Original->GetNextSectionID(1), 1);
	TestEqual(TEXT("Asset Hold link remains unchanged"), Fixture.Montage->CompositeSections[1].NextSectionName, NAME_None);
	TestEqual(TEXT("Instance seeks to delayed position"), Original->GetPosition(), 0.5f);
	TestEqual(TEXT("Instance uses planned rate"), Original->GetPlayRate(), 0.75f);
	FCadenceArcChargeMontageTestAccess::Own(*Fixture.Component, Fixture.Anim, Original, Fixture.Entry);
	FCadenceArcChargeMontageTestAccess::EndHold(*Fixture.Component, OtherTag);
	TestEqual(TEXT("Unrelated input leaves presentation owned"), FCadenceArcChargeMontageTestAccess::PendingCount(*Fixture.Component), 0);
	FCadenceArcChargeMontageTestAccess::EndHold(*Fixture.Component, InputTag);
	TestEqual(TEXT("Release schedules one pending stop"), FCadenceArcChargeMontageTestAccess::PendingCount(*Fixture.Component), 1);
	const uint64 EndFrame = FCadenceArcChargeMontageTestAccess::PendingFrame(*Fixture.Component);
	FCadenceArcChargeMontageTestAccess::Flush(*Fixture.Component, EndFrame);
	TestTrue(TEXT("Same-frame tick does not stop presentation"), Original->IsActive());
	FAnimMontageInstance* Replacement = Fixture.Play();
	if (!TestNotNull(TEXT("Executor replays same asset before deferred cleanup"), Replacement)) return false;
	TestNotEqual(TEXT("Replay has another ID"), Replacement->GetInstanceID(), Original->GetInstanceID());
	FCadenceArcChargeMontageTestAccess::Flush(*Fixture.Component, EndFrame + 1);
	TestTrue(TEXT("Only original presentation stops"), Original->IsStopped());
	TestTrue(TEXT("Newer same-asset execution survives"), Replacement->IsActive());
	TestEqual(TEXT("Deferred stop consumed"), FCadenceArcChargeMontageTestAccess::PendingCount(*Fixture.Component), 0);
	FCadenceArcChargeMontageTestAccess::Own(*Fixture.Component, Fixture.Anim, Replacement, Fixture.Entry);
	FCadenceArcChargeMontageTestAccess::EndHold(*Fixture.Component, InputTag);
	FCadenceArcChargeMontageTestAccess::Flush(*Fixture.Component, EndFrame, true);
	TestTrue(TEXT("Forced teardown cleans pending instance without waiting"), Replacement->IsStopped());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcChargeMontageValidationTest, "CadenceArc.Animation.ChargeMontage.SelectionAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcChargeMontageValidationTest::RunTest(const FString& Parameters)
{
	FAnimationFixture Fixture;
	Fixture.Component->Entries.Add(Fixture.Entry);
	FCadenceArcChargeMontageEntry Specific = Fixture.Entry;
	Specific.SourceActionTag = SourceTag;
	Fixture.Component->Entries.Add(Specific);
	Fixture.Component->Entries.Add(Specific);
	TestTrue(TEXT("Specific source wins over earlier wildcard"), FCadenceArcChargeMontageTestAccess::Find(*Fixture.Component, InputTag, SourceTag) == &Fixture.Component->Entries[1]);
	TestTrue(TEXT("Wildcard matches other source"), FCadenceArcChargeMontageTestAccess::Find(*Fixture.Component, InputTag, OtherTag) == &Fixture.Component->Entries[0]);
	TestNull(TEXT("Other input has no match"), FCadenceArcChargeMontageTestAccess::Find(*Fixture.Component, OtherTag, SourceTag));
	TestTrue(TEXT("Valid entry accepted"), FCadenceArcChargeMontageTestAccess::Validate(Fixture.Entry));
	FCadenceArcChargeMontageEntry Invalid = Fixture.Entry;
	Invalid.InputTag = {};
	TestFalse(TEXT("Invalid input rejected"), FCadenceArcChargeMontageTestAccess::Validate(Invalid));
	Invalid = Fixture.Entry;
	Invalid.Montage = nullptr;
	TestFalse(TEXT("Null montage rejected"), FCadenceArcChargeMontageTestAccess::Validate(Invalid));
	Invalid = Fixture.Entry;
	Invalid.WindupSection = TEXT("Missing");
	TestFalse(TEXT("Missing section rejected"), FCadenceArcChargeMontageTestAccess::Validate(Invalid));
	Fixture.Montage->RateScale = -1.0f;
	TestFalse(TEXT("Reverse asset rate rejected"), FCadenceArcChargeMontageTestAccess::Validate(Fixture.Entry));
	Fixture.Montage->RateScale = 1.0f;
#if WITH_EDITOR
	FDataValidationContext Context;
	TestEqual(TEXT("Duplicate configuration remains valid"), Fixture.Component->IsDataValid(Context), EDataValidationResult::Valid);
	TestEqual(TEXT("One duplicate pair warning"), Context.GetNumWarnings(), 1u);
	TestEqual(TEXT("No validation errors"), Context.GetNumErrors(), 0u);
	Fixture.Component->Entries.Add(Invalid);
	FDataValidationContext InvalidContext;
	TestEqual(TEXT("Malformed configuration is invalid"), Fixture.Component->IsDataValid(InvalidContext), EDataValidationResult::Invalid);
	TestEqual(TEXT("Malformed entry produces error"), InvalidContext.GetNumErrors(), 1u);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcChargeMontageIntegrationTest, "CadenceArc.Animation.ChargeMontage.ComponentLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcChargeMontageIntegrationTest::RunTest(const FString& Parameters)
{
	FAnimationFixture Animation;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CadenceArcAnimationTestWorld"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	AActor* Actor = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Actor);
	Mesh->RegisterComponent();
	UAnimInstance* Anim = NewObject<UAnimInstance>(Mesh);
	Anim->CurrentSkeleton = Animation.Montage->GetSkeleton();
	UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
	Graph->EntryActionTag = SourceTag;
	FCadenceArcNode& Root = Graph->Nodes.AddDefaulted_GetRef();
	Root.ActionTag = SourceTag;
	FCadenceArcTransition Tap;
	Tap.InputTag = InputTag;
	Tap.TargetActionTag = OtherTag;
	Tap.InputPhase = ECadenceArcInputPhase::Released;
	Tap.bUseDurationRange = true;
	Tap.DurationRange.bHasMaxHeldDuration = true;
	Tap.DurationRange.MaxHeldDurationSecondsExclusive = 1.5;
	Root.Transitions.Add(Tap);
	FCadenceArcTransition Charged = Tap;
	Charged.DurationRange.MinHeldDurationSeconds = 1.5;
	Charged.DurationRange.bHasMaxHeldDuration = false;
	Root.Transitions.Add(Charged);
	FCadenceArcHoldChargeConfig& Config = Root.HoldChargeConfigs.AddDefaulted_GetRef();
	Config.InputTag = InputTag;
	Config.ChargeStartSeconds = 0.5;
	Config.MaxChargedHoldSeconds = 2.0;
	Graph->Nodes.AddDefaulted_GetRef().ActionTag = OtherTag;
	double Now = 1.0;
	UCadenceArcComponent* Arc = NewObject<UCadenceArcComponent>(Actor);
	Arc->Graph = Graph;
	Arc->SetTimeSource([&Now]() { return Now; });
	Arc->SetInputMode(InputTag, ECadenceArcInputMode::HoldRelease);
	Arc->RegisterComponent();
	Arc->OnActionRequestedNative.AddLambda([Arc](const FCadenceArcActionRequest& Request)
	{
		Arc->NotifyActionRejected(Request.RequestId);
	});
	UCadenceArcChargeMontageComponent* Presentation = NewObject<UCadenceArcChargeMontageComponent>(Actor);
	Presentation->Entries.Add(Animation.Entry);
	Presentation->RegisterComponent();
	// 独立测试 World 未走关卡初始化，补齐 Actor 生命周期，保证 Destroy 会分发 EndPlay。
	Actor->PreInitializeComponents();
	Actor->InitializeComponents();
	Actor->PostInitializeComponents();
	Actor->DispatchBeginPlay();
	Mesh->AnimScriptInstance = Anim;
	TestTrue(TEXT("Owner component discovery binds lifecycle events"), Presentation->CadenceArcComponent == Arc && Presentation->SkeletalMesh == Mesh);
	TestEqual(TEXT("Real semantic input grants hold"), Arc->PressInput(InputTag).Status, ECadenceArcInputStatus::HoldGranted);
	Now = 1.75;
	Arc->TickComponent(0.0f, LEVELTICK_All, nullptr);
	FAnimMontageInstance* First = Anim->GetActiveInstanceForMontage(Animation.Montage);
	if (!TestNotNull(TEXT("Charging event starts presentation"), First)) return false;
	TestEqual(TEXT("Charging compensates 0.25 second delay"), First->GetPosition(), 0.5f);
	Now = 2.75;
	Arc->TickComponent(0.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Charged event retains owned instance"), Anim->GetActiveInstanceForMontage(Animation.Montage) == First);
	TestEqual(TEXT("Charged event seeks from actual full threshold"), First->GetPosition(), 2.2425f);
	TestEqual(TEXT("Charged event scales hold playback"), First->GetPlayRate(), 0.97f);
	Arc->CancelInput(InputTag);
	TestEqual(TEXT("Real cancellation schedules cleanup"), FCadenceArcChargeMontageTestAccess::PendingCount(*Presentation), 1);
	FCadenceArcChargeMontageTestAccess::Flush(*Presentation, FCadenceArcChargeMontageTestAccess::PendingFrame(*Presentation) + 1);
	TestTrue(TEXT("Cancelled presentation stops"), First->IsStopped());
	Now = 3.0;
	Arc->PressInput(InputTag);
	Now = 4.75;
	Arc->TickComponent(0.0f, LEVELTICK_All, nullptr);
	FAnimMontageInstance* Late = Anim->GetActiveInstanceForMontage(Animation.Montage);
	if (!TestNotNull(TEXT("One late update crosses both thresholds"), Late)) return false;
	TestEqual(TEXT("Late charging starts directly in Hold"), Late->GetPosition(), 2.2425f);
	TestEqual(TEXT("Two stage notifications create only one new instance"), Anim->MontageInstances.Num(), 2);
	Now = 6.5;
	Arc->TickComponent(0.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Automatic release schedules cleanup"), FCadenceArcChargeMontageTestAccess::PendingCount(*Presentation), 1);
	Arc->ReleaseInput(InputTag);
	Now = 7.0;
	Arc->PressInput(InputTag);
	Now = 10.5;
	Arc->TickComponent(0.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Advance past automatic release skips historical stage playback"), Anim->MontageInstances.Num(), 2);
	const int32 LateId = Late->GetInstanceID();
	Actor->Destroy();
	FAnimMontageInstance* Remaining = Anim->GetMontageInstanceForID(LateId);
	TestTrue(TEXT("EndPlay stops or tears down pending presentation immediately"), !Remaining || Remaining->IsStopped());
	TestEqual(TEXT("EndPlay drains pending cleanup"), FCadenceArcChargeMontageTestAccess::PendingCount(*Presentation), 0);
	return true;
}

#endif
