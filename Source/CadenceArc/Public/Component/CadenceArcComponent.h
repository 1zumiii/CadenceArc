#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Input/CadenceArcInputTracker.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include "CadenceArcComponent.generated.h"

class UCadenceArcGraph;
class UCadenceArcResolver;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCadenceArcActionRequestedSignature, const FCadenceArcActionRequest&, Request);
DECLARE_MULTICAST_DELEGATE_OneParam(FCadenceArcActionRequestedNative, const FCadenceArcActionRequest&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FCadenceArcHoldStageChangedNative, FGameplayTag, const FCadenceArcInputStageChange&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCadenceArcHoldStageChangedSignature, FGameplayTag, InputTag, const FCadenceArcInputStageChange&, Change);

/**
 * UE 项目接入 CadenceArc 的标准入口。组件持有一个解析器，并负责每个项目都会写成一样的部分：
 * - 时间：所有调用自动使用 World 游戏时间，需要其他时间来源时用 SetTimeSource 替换；
 * - 逐帧推进：在 Tick 中推进按住资格的时间，处理蓄力阶段和自动释放；
 * - 按键配对：按下和松开由内部的 Tracker 配对，自动生成 Token 和按住时长；
 * - 请求出口：直接提交、松手、自动释放和完成时消费缓冲产生的请求，统一从 OnActionRequested 发出。
 *
 * 需要由项目决定的部分仍由调用方提供：事件上下文、持久上下文，以及执行器的开始、拒绝、完成、打断回调。
 * 解析器本身不读取 World 和时钟；需要直接控制时间的场景（测试、回放）可以通过 GetResolver 使用底层 API。
 */
UCLASS(ClassGroup=(CadenceArc), meta=(BlueprintSpawnableComponent))
class CADENCEARC_API UCadenceArcComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCadenceArcComponent();

	// BeginPlay 时用这张图初始化解析器。运行中换图请调用 InitializeResolver。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc")
	TObjectPtr<UCadenceArcGraph> Graph;

	// 解析器产生的每一个动作请求都从这里发出。处理函数应调用 NotifyActionStarted 或 NotifyActionRejected；
	// 在确认之前，解析器停留在 AwaitingStart，新的输入返回 RequestPending。
	UPROPERTY(BlueprintAssignable, Category="CadenceArc")
	FCadenceArcActionRequestedSignature OnActionRequested;

	// 按住资格进入蓄力或蓄满时发出，用于播放蓄力表现。Change.EffectiveTimestampSeconds 是阈值本身的时间。
	UPROPERTY(BlueprintAssignable, Category="CadenceArc")
	FCadenceArcHoldStageChangedSignature OnHoldStageChanged;

	// 与上面两个委托同时发出，供 C++ 执行器绑定 lambda 或普通成员函数
	FCadenceArcActionRequestedNative OnActionRequestedNative;
	FCadenceArcHoldStageChangedNative OnHoldStageChangedNative;

	// 用指定的图初始化（或重新初始化）解析器。成功后清空当前的按键配对。
	UFUNCTION(BlueprintCallable, Category="CadenceArc")
	ECadenceArcResolverInitResult InitializeResolver(UCadenceArcGraph* InGraph);

	// ---- 输入 ----

	// 按键按下。PressOnly 立即提交；HoldRelease 申请按住资格，松开或自动释放时解析。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input", meta=(AutoCreateRefTerm="ContextTags"))
	void PressInput(const FGameplayTag& InputTag, ECadenceArcInputMode Mode, const FGameplayTagContainer& ContextTags);

	// 按键松开。ContextTags 是松开那一刻的事件上下文，只在按住资格仍有效时使用。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input", meta=(AutoCreateRefTerm="ContextTags"))
	void ReleaseInput(const FGameplayTag& InputTag, const FGameplayTagContainer& ContextTags);

	// 取消一个按键的按住资格和配对，不产生松手。用于闪避、格挡等打断蓄力的操作。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void CancelInput(const FGameplayTag& InputTag);

	// 取消所有按键。失去控制、窗口失焦或 EndPlay 时调用；EndPlay 会自动调用。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void CancelAllInputs();

	UFUNCTION(BlueprintPure, Category="CadenceArc|Input")
	bool IsInputPressed(const FGameplayTag& InputTag) const;

	// 替换持久上下文，例如空中、持剑姿态。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void SetContextTags(const FGameplayTagContainer& InContextTags);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Input")
	FGameplayTagContainer GetContextTags() const;

	// ---- 执行器回调 ----

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	ECadenceArcHandshakeResult NotifyActionStarted(int64 RequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	ECadenceArcHandshakeResult NotifyActionRejected(int64 RequestId);

	// 动作完成。先推进按住时间，再通知解析器；消费缓冲产生的下一个请求从 OnActionRequested 发出。
	// 这个时刻也是停顿的起点，请在执行器中明确完成回调的发送时机。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	FCadenceArcActionCompletionOutcome NotifyActionCompleted(int64 RequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	ECadenceArcHandshakeResult NotifyActionCancelled(int64 RequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	ECadenceArcHandshakeResult NotifyActionInterrupted(int64 RequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	ECadenceArcHandshakeResult OpenBufferWindow(int64 RequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	ECadenceArcHandshakeResult CloseBufferWindow(int64 RequestId);

	// 回到入口动作。只在 Ready 时成功，例如连招超时。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Execution")
	ECadenceArcResolverResetResult ResetCombo();

	// ---- 时间与底层访问 ----

	// 组件使用的当前时间。默认是 World 游戏时间（随暂停停止，跟随全局时间膨胀）。
	UFUNCTION(BlueprintPure, Category="CadenceArc")
	double GetTimeSeconds() const;

	// 替换时间来源，例如使用角色的 CustomTimeDilation、联网同步时间或回放时间。传入空函数恢复默认。
	// 新的时间来源必须与已经提交的时间戳处于同一时间域，并且不递减。
	void SetTimeSource(TFunction<double()> InTimeSource);

	UFUNCTION(BlueprintPure, Category="CadenceArc")
	UCadenceArcResolver* GetResolver() const { return Resolver; }

	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FTrackedPress
	{
		FCadenceArcInputToken Token;
		ECadenceArcInputMode Mode = ECadenceArcInputMode::PressOnly;
	};

	UPROPERTY(Transient)
	TObjectPtr<UCadenceArcResolver> Resolver;

	TUniquePtr<FCadenceArcInputTracker> Tracker; // 不可复制；重建即换一个输入会话
	TMap<FGameplayTag, FTrackedPress> PressedByTag;
	TFunction<double()> TimeSource;

	// 推进按住时间并发出阶段变化和到期释放产生的请求。每个带时间的调用都先做这一步，
	// 否则到期的自动释放会让新的输入和完成回调返回 InputTimeAdvanceRequired。
	bool AdvanceAndDispatch(double Now);
	void DispatchRequest(const FCadenceArcActionRequest& Request);
	bool HasInitializedResolver() const;
};
