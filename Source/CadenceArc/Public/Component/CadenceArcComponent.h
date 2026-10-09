#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Input/CadenceArcInputTracker.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include "CadenceArcComponent.generated.h"

class UCadenceArcGraph;
class UCadenceArcResolver;

// 一次输入调用在组件中的处理结果。动作请求不在这里返回，统一从 OnActionRequested 发出。
UENUM(BlueprintType)
enum class ECadenceArcInputStatus : uint8
{
	NotInitialized, // 解析器尚未初始化，输入被忽略
	AlreadyPressed, // 同一个键还没有松开，重复的按下被忽略
	NotPressed, // 松开了一个没有记录按下的键，被忽略
	RequestProduced, // 产生了动作请求，已通过 OnActionRequested 发出
	Buffered, // 动作执行中，输入已存入缓冲
	HoldGranted, // 按住资格已授予，松开或自动释放时解析
	KeyReleased, // 按键配对已结束，没有需要提交的松手（PressOnly 的键，或按住资格已经结束）
	NoAction, // 输入已处理，但没有产生动作，原因见 Reason
	Rejected, // 输入被拒绝，原因见 Reason
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcInputResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	ECadenceArcInputStatus Status = ECadenceArcInputStatus::NotInitialized;

	// NoAction 和 Rejected 时的解析原因，其余情况为 None
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::None;

	// 输入是否起了作用：产生请求、进入缓冲或取得按住资格
	bool IsAccepted() const
	{
		return Status == ECadenceArcInputStatus::RequestProduced || Status == ECadenceArcInputStatus::Buffered
			|| Status == ECadenceArcInputStatus::HoldGranted;
	}
};

// 按住资格结束的原因。每份资格只结束一次。
UENUM(BlueprintType)
enum class ECadenceArcHoldEndReason : uint8
{
	Released, // 手动松开
	AutoReleased, // 达到保持上限后自动释放
	Cancelled, // CancelInput、CancelAllInputs 或 EndPlay
	Replaced, // 被另一个被接受的输入替换
	Cleared, // 连招重置、重新初始化，或动作开始、取消、打断时清空
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCadenceArcActionRequestedSignature, const FCadenceArcActionRequest&, Request);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCadenceArcHoldStageChangedSignature, FGameplayTag, InputTag, const FCadenceArcInputStageChange&, Change);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCadenceArcHoldEndedSignature, FGameplayTag, InputTag, ECadenceArcHoldEndReason, Reason);
DECLARE_MULTICAST_DELEGATE_OneParam(FCadenceArcActionRequestedNative, const FCadenceArcActionRequest&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FCadenceArcHoldStageChangedNative, FGameplayTag, const FCadenceArcInputStageChange&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FCadenceArcHoldEndedNative, FGameplayTag, ECadenceArcHoldEndReason);

/**
 * UE 项目接入 CadenceArc 的标准入口。组件持有一个解析器，并负责每个项目都会写成一样的部分：
 * - 时间：所有调用自动使用 World 游戏时间，需要其他时间来源时用 SetTimeSource 替换；
 * - 逐帧推进：在 Tick 中推进按住资格的时间，处理蓄力阶段和自动释放；
 * - 按键配对：按下和松开由内部的 Tracker 配对，自动生成 Token 和按住时长；
 * - 输入方式：按 InputModes 配置决定按下立即提交还是等待松开；
 * - 事件上下文：从 ICadenceArcInputContextProvider 采集，通常由角色实现；
 * - 请求出口：所有动作请求统一从 OnActionRequested 发出。
 *
 * 游戏需要决定的部分仍由调用方提供：执行器的开始、拒绝、缓冲窗口、完成、取消和打断回调。
 * 解析器本身不读取 World 和时钟；测试、回放等场景可以通过 GetResolver 直接使用底层 API，
 * 但绕过组件调用解析器时，组件不会发出 OnHoldEnded。
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

	// 每个输入 Tag 的输入方式；没有配置的 Tag 按 PressOnly 处理。
	// HoldRelease 始终申请按住资格；HoldIfAvailable 只在当前节点有该 Tag 的 Released 转移时申请，否则按下立即提交。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc", meta=(ForceInlineRow))
	TMap<FGameplayTag, ECadenceArcInputMode> InputModes;

	// 解析器产生的每一个动作请求都从这里发出。一个组件只能有一个执行器处理请求：
	// 处理函数调用 NotifyActionStarted 或 NotifyActionRejected，确认之前新输入返回 RequestPending。
	// 其他系统（UI、音效）不应订阅这个事件来执行动作，否则同一个请求会被执行两次。
	UPROPERTY(BlueprintAssignable, Category="CadenceArc")
	FCadenceArcActionRequestedSignature OnActionRequested;

	// 按住资格进入蓄力或蓄满时发出，用于播放蓄力表现。Change.EffectiveTimestampSeconds 是阈值本身的时间。
	UPROPERTY(BlueprintAssignable, Category="CadenceArc")
	FCadenceArcHoldStageChangedSignature OnHoldStageChanged;

	// 按住资格结束时发出，每份资格只发一次，用于清理蓄力特效、音效和 UI。
	UPROPERTY(BlueprintAssignable, Category="CadenceArc")
	FCadenceArcHoldEndedSignature OnHoldEnded;

	// 与上面三个委托同时发出，供 C++ 绑定 lambda 或普通成员函数
	FCadenceArcActionRequestedNative OnActionRequestedNative;
	FCadenceArcHoldStageChangedNative OnHoldStageChangedNative;
	FCadenceArcHoldEndedNative OnHoldEndedNative;

	// 用指定的图初始化（或重新初始化）解析器。成功后清空当前的按键配对，持久上下文保留。
	UFUNCTION(BlueprintCallable, Category="CadenceArc")
	ECadenceArcResolverInitResult InitializeResolver(UCadenceArcGraph* InGraph);

	// ---- 输入 ----

	// 设置一个输入 Tag 的输入方式，与 InputModes 中的配置相同。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void SetInputMode(const FGameplayTag& InputTag, ECadenceArcInputMode Mode);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Input")
	ECadenceArcInputMode GetInputMode(const FGameplayTag& InputTag) const;

	// 按键按下。事件上下文从上下文提供者采集。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	FCadenceArcInputResult PressInput(const FGameplayTag& InputTag);

	// 按键按下，并使用调用方提供的完整上下文快照，不调用上下文提供者。传入空容器表示没有上下文。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	FCadenceArcInputResult PressInputWithContext(const FGameplayTag& InputTag, const FGameplayTagContainer& ContextTags);

	// 按键松开。事件上下文从上下文提供者采集，只在按住资格仍有效时使用。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	FCadenceArcInputResult ReleaseInput(const FGameplayTag& InputTag);

	// 按键松开，并使用调用方提供的完整上下文快照，不调用上下文提供者。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	FCadenceArcInputResult ReleaseInputWithContext(const FGameplayTag& InputTag, const FGameplayTagContainer& ContextTags);

	// 取消一个按键的按住资格和配对，不产生松开。用于闪避、格挡等打断蓄力的操作。
	// 只清理输入，不会停止正在执行的动作；需要停止动作时由执行器调用 NotifyActionInterrupted。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void CancelInput(const FGameplayTag& InputTag);

	// 取消所有按键，例如失去控制或窗口失焦；EndPlay 会自动调用。同样不会停止正在执行的动作。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void CancelAllInputs();

	UFUNCTION(BlueprintPure, Category="CadenceArc|Input")
	bool IsInputPressed(const FGameplayTag& InputTag) const;

	// 替换持久上下文，例如空中、持剑姿态。可以在解析器初始化之前调用，初始化时生效。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void SetContextTags(const FGameplayTagContainer& InContextTags);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Input")
	FGameplayTagContainer GetContextTags() const;

	// 指定事件上下文的提供者。默认使用所属 Actor（如果它实现了 ICadenceArcInputContextProvider）。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void SetContextProvider(UObject* InProvider);

	// C++ 中以函数提供事件上下文，优先于接口提供者。传入空函数恢复使用接口提供者。
	void SetContextProviderFunction(TFunction<FGameplayTagContainer(FGameplayTag, ECadenceArcInputPhase)> InFunction);

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

	// ---- 状态查询 ----

	UFUNCTION(BlueprintPure, Category="CadenceArc")
	bool IsInitialized() const;

	// 解析器状态：Ready、AwaitingStart（等待执行器确认）、Executing；未初始化时为 Uninitialized
	UFUNCTION(BlueprintPure, Category="CadenceArc")
	ECadenceArcResolverState GetState() const;

	// 已提交的当前动作；未初始化时为空
	UFUNCTION(BlueprintPure, Category="CadenceArc")
	FGameplayTag GetCurrentActionTag() const;

	// 使用组件的时间来源查询，不推进解析器或提交节点。
	UFUNCTION(BlueprintPure, Category="CadenceArc|State")
	FGameplayTag GetEffectiveActionTag() const;

	// -1 不计时，0 已到期，正数为下一次输入从入口解析前的剩余秒数。
	UFUNCTION(BlueprintPure, Category="CadenceArc|State")
	double GetComboResetRemainingSeconds() const;

	// 底层解析器，用于调试或高级查询。通过它直接修改解析器会绕过组件，组件不会发出 OnHoldEnded。
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
		bool bHold = false; // 按下时实际申请了按住资格
	};

	UPROPERTY(Transient)
	TObjectPtr<UCadenceArcResolver> Resolver;

	// 持久上下文由组件保存，解析器初始化前设置也不会丢失
	UPROPERTY(Transient)
	FGameplayTagContainer PersistentContext;

	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> ContextProvider;

	TUniquePtr<FCadenceArcInputTracker> Tracker; // 不可复制；重建即换一个输入会话
	TMap<FGameplayTag, FTrackedPress> PressedByTag;
	TFunction<double()> TimeSource;
	TFunction<FGameplayTagContainer(FGameplayTag, ECadenceArcInputPhase)> ContextProviderFunction;

	// 推进按住时间并发出阶段变化、资格结束和到期释放产生的请求。每个带时间的调用都先做这一步，
	// 否则到期的自动释放会让新的输入和完成回调返回 InputTimeAdvanceRequired。
	bool AdvanceAndDispatch(double Now);
	void DispatchRequest(const FCadenceArcActionRequest& Request);
	bool HasInitializedResolver() const;

	FCadenceArcInputResult PressInputInternal(const FGameplayTag& InputTag, const FGameplayTagContainer* ExplicitContext);
	FCadenceArcInputResult ReleaseInputInternal(const FGameplayTag& InputTag, const FGameplayTagContainer* ExplicitContext);
	FGameplayTagContainer CollectContext(const FGameplayTag& InputTag, ECadenceArcInputPhase Phase) const;
	// 当前节点是否有这个 Tag 的 Released 转移；HoldRelease 的键据此决定是否申请按住资格
	bool CurrentNodeHasReleasedEdge(const FGameplayTag& InputTag, double NowSeconds) const;

	// 调用解析器前记下当前的按住资格；调用后资格消失或换了 Token，就发出一次 OnHoldEnded
	FCadenceArcHoldSnapshot CaptureHold() const;
	void NotifyHoldEndedIfChanged(const FCadenceArcHoldSnapshot& Before, ECadenceArcHoldEndReason Reason);
};
