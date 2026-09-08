#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include <type_traits>

class UCadenceArcGraph;
class UCadenceArcResolver;

struct FCadenceArcNode;
struct FCadenceArcActionRequest;
struct FCadenceArcInputEvent;
struct FCadenceArcSubmitOutcome;
struct FCadenceArcActionCompletionOutcome;

enum class ECadenceArcResolverInitResult : uint8;
enum class ECadenceArcResolutionCategory : uint8;
enum class ECadenceArcResolutionReason : uint8;
enum class ECadenceArcHandshakeResult : uint8;
enum class ECadenceArcResolverState : uint8;

namespace CadenceArc::Tests
{
	inline constexpr double RegressionTimestampSeconds = 0.125;

	// Test tags
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Root);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Light01);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Light02);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Heavy01);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Heavy02);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Finisher01);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Finisher02);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Finisher03);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Light);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Heavy);

	struct FExpectedResolution
	{
		ECadenceArcResolutionCategory Category;
		ECadenceArcResolutionReason Reason;
	};

	struct FResolverSnapshot
	{
		ECadenceArcResolverState State;
		FGameplayTag CurrentAction;
		FCadenceArcActionRequest Request;
		bool bWindowOpen;
		FGameplayTag BufferedTag;

		explicit FResolverSnapshot(const UCadenceArcResolver* Resolver);
		void ExpectUnchanged(
			FAutomationTestBase& Test,
			const UCadenceArcResolver* Resolver) const;
	};

	FExpectedResolution ExpectedResolution(
		ECadenceArcResolutionCategory Category,
		ECadenceArcResolutionReason Reason);
	FCadenceArcInputEvent MakeInput(
		const FGameplayTag& InputTag,
		double TimestampSeconds = RegressionTimestampSeconds);
	FCadenceArcNode& AddNode(
		UCadenceArcGraph* Graph,
		const FGameplayTag& ActionTag);
	void AddTransition(
		FCadenceArcNode& Node,
		const FGameplayTag& InputTag,
		const FGameplayTag& TargetActionTag);

	UCadenceArcGraph* MakeValidGraph();

	TArray<double> InvalidTimes();


	bool TestInit(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcResolverInitResult Actual,
		const ECadenceArcResolverInitResult Expected);
	bool TestSubmit(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FCadenceArcSubmitOutcome& Actual,
		const ECadenceArcResolutionCategory ExpectedCategory,
		const ECadenceArcResolutionReason ExpectedReason);
	bool TestHandshake(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcHandshakeResult Actual,
		const ECadenceArcHandshakeResult Expected);
	bool TestBufferConsumption(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FCadenceArcActionCompletionOutcome& Actual,
		const ECadenceArcResolutionCategory ExpectedCategory,
		const ECadenceArcResolutionReason ExpectedReason);
	bool TestTransition(
		FAutomationTestBase& Test, const TCHAR* What,
		const FCadenceArcSubmitOutcome& Actual,
		const FExpectedResolution Expected);
	bool TestBufferConsume(
		FAutomationTestBase& Test, const TCHAR* What,
		const FCadenceArcActionCompletionOutcome& Actual, const FExpectedResolution Expected);
	bool TestState(
		FAutomationTestBase& Test, const TCHAR* What,
		const ECadenceArcResolverState Actual,
		const ECadenceArcResolverState Expected);
	bool TestTag(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FGameplayTag& Actual,
		const FGameplayTag& Expected);
	bool TestEmptyRequest(FAutomationTestBase& Test, const FCadenceArcActionRequest& Request);
	FCadenceArcSubmitOutcome SubmitForTest(
		UCadenceArcResolver* Resolver,
		const FCadenceArcInputEvent& Event,
		FCadenceArcActionRequest& OutRequest);
	bool ResolveAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const FGameplayTag& SourceTag,
		const FGameplayTag& TargetTag,
		FCadenceArcActionRequest& OutRequest,
		const TCHAR* Step);
	bool StartAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& Request,
		const TCHAR* Step);
	bool CompleteAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& Request,
		const TCHAR* Step);
	bool ExecuteAndComplete(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const FGameplayTag& SourceTag,
		const FGameplayTag& TargetTag,
		const TCHAR* Step);
	bool ResolveFailureAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const FExpectedResolution Expected,
		const FGameplayTag& ExpectedCurrentTag,
		const ECadenceArcResolverState ExpectedState,
		const int64 ExpectedOutstandingId,
		const TCHAR* Step);
	bool BeginTimedAction(
		FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
		FCadenceArcActionRequest& Request, const double MaxAge);
	bool BufferAt(
		FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& Request, const FGameplayTag& InputTag, const double Timestamp);
	void ExpectTimedCompletion(
		FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& CompletedRequest,
		const FCadenceArcActionCompletionOutcome& Outcome,
		const FExpectedResolution Expected,
		const FGameplayTag& ExpectedInput = Input_Heavy,
		const FGameplayTag& ExpectedTarget = Action_Finisher01);

	// During the Phase 5 API migration, fail explicitly instead of invoking the old
	// one-argument completion method, which dereferences a nonexistent test World.
	// No legacy-clock fallback is allowed: these tests must inject completion time.
	template <typename TResolver = UCadenceArcResolver>
	bool RequireExplicitCompletionTime(FAutomationTestBase& Test)
	{
		constexpr bool bAcceptsTime = std::is_invocable_r_v<FCadenceArcActionCompletionOutcome,
															decltype(&TResolver::NotifyActionCompleted), TResolver*,
															int64, double>;
		if constexpr (!bAcceptsTime)
		{
			Test.AddError(TEXT(
				"Phase 5 API missing: NotifyActionCompleted must accept (int64 RequestId, double CompletionTimestampSeconds). Completion behavior was not executed; no World-clock fallback is permitted."));
		}
		return bAcceptsTime;
	}

	template <typename TResolver>
	FCadenceArcActionCompletionOutcome CompleteAt(
		FAutomationTestBase& Test, TResolver* Resolver, const int64 RequestId, const double TimestampSeconds)
	{
		if constexpr (std::is_invocable_r_v<FCadenceArcActionCompletionOutcome,
											decltype(&TResolver::NotifyActionCompleted), TResolver*, int64, double>)
		{
			return Resolver->NotifyActionCompleted(RequestId, TimestampSeconds);
		}
		else
		{
			RequireExplicitCompletionTime<TResolver>(Test);
			return {};
		}
	}
}

#endif
