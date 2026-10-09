#if WITH_DEV_AUTOMATION_TESTS

#include "Component/CadenceArcComponent.h"
#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "Tests/CadenceArcTestSupport.h"

namespace CadenceArc::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcComponentRecoveryModeTest,
		"CadenceArc.Component.Recovery.HoldIfAvailableUsesEffectiveSource",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentRecoveryModeTest::RunTest(const FString& Parameters)
	{
		// 0 默认按下，1 到期从入口申请按住，2 无 Released 边时从入口申请按住。
		for (int32 Mode = 0; Mode < 3; ++Mode)
		{
			UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
			Graph->EntryActionTag = Action_Root;
			Graph->ComboResetSeconds = Mode == 1 ? 1.0 : 0.0;
			Graph->bFallbackToEntryOnNoMatch = Mode == 2;
			Graph->Nodes.Reserve(3);
			FCadenceArcNode& Root = AddNode(Graph, Action_Root);
			AddTransition(Root, Input_Light, Action_Light01);
			AddTransition(Root, Input_Heavy, Action_Heavy01);
			Root.Transitions.Last().InputPhase = ECadenceArcInputPhase::Released;
			FCadenceArcNode& Light = AddNode(Graph, Action_Light01);
			AddTransition(Light, Input_Heavy, Action_Heavy01);
			AddNode(Graph, Action_Heavy01);

			double Now = 1.0;
			UCadenceArcComponent* Component = NewObject<UCadenceArcComponent>();
			Component->SetTimeSource([&Now]() { return Now; });
			Component->SetInputMode(Input_Heavy, ECadenceArcInputMode::HoldIfAvailable);
			TArray<FCadenceArcActionRequest> Requests;
			Component->OnActionRequestedNative.AddLambda([&Requests, Component](const FCadenceArcActionRequest& Request)
			{
				Requests.Add(Request);
				Component->NotifyActionStarted(Request.RequestId);
			});
			TestInit(*this, TEXT("Initialize"), Component->InitializeResolver(Graph), ECadenceArcResolverInitResult::Success);
			Component->PressInput(Input_Light);
			Component->ReleaseInput(Input_Light);
			if (Requests.IsEmpty())
			{
				AddError(TEXT("Initial request missing"));
				continue;
			}
			Now = 2.0;
			Component->NotifyActionCompleted(Requests.Last().RequestId);
			Now = 3.0;
			TestTag(*this, TEXT("Committed node remains"), Component->GetCurrentActionTag(), Action_Light01);
			TestTag(*this, TEXT("Effective query"), Component->GetEffectiveActionTag(), Mode == 1 ? Action_Root : Action_Light01);
			TestEqual(TEXT("Countdown query"), Component->GetComboResetRemainingSeconds(), Mode == 1 ? 0.0 : -1.0);
			const FCadenceArcInputResult Result = Component->PressInput(Input_Heavy);
			TestEqual(TEXT("Mode follows actual source"), Result.Status,
				Mode == 0 ? ECadenceArcInputStatus::RequestProduced : ECadenceArcInputStatus::HoldGranted);
			if (Mode != 0)
			{
				TestEqual(TEXT("No request until release"), Requests.Num(), 1);
				TestTag(*this, TEXT("Qualification uses entry"), Component->GetResolver()->GetInputHoldSnapshot().SourceActionTag, Action_Root);
				TestEqual(TEXT("No countdown while holding"), Component->GetComboResetRemainingSeconds(), -1.0);
			}
			Now = 5.0;
			Component->ReleaseInput(Input_Heavy);
			TestEqual(TEXT("Exactly one heavy request"), Requests.Num(), 2);
			if (Requests.Num() == 2)
			{
				TestTag(*this, TEXT("Request source"), Requests.Last().SourceActionTag, Mode == 0 ? Action_Light01 : Action_Root);
				TestTag(*this, TEXT("Heavy starts"), Component->GetCurrentActionTag(), Action_Heavy01);
			}
		}
		return !HasAnyErrors();
	}
}

#endif
