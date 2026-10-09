#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Component/CadenceArcComponent.h"
#include "CadenceArcChargeMontageComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class USkeletalMeshComponent;
struct FAnimMontageInstance;

USTRUCT(BlueprintType)
struct CADENCEARCANIMATION_API FCadenceArcChargeMontageEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Animation")
	FGameplayTag InputTag;

	// 留空时适用于该输入的所有源动作；精确源动作条目优先。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Animation")
	FGameplayTag SourceActionTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Animation")
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Animation")
	FName WindupSection = TEXT("Windup");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Animation")
	FName HoldSection = TEXT("Hold");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Animation", meta=(ClampMin="0"))
	float StopBlendOutSeconds = 0.2f;
};

/** 只订阅按住生命周期并播放表现；不提交输入、不接管执行器、不修改解析器。 */
UCLASS(ClassGroup=(CadenceArc), meta=(BlueprintSpawnableComponent))
class CADENCEARCANIMATION_API UCadenceArcChargeMontageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCadenceArcChargeMontageComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Animation")
	TArray<FCadenceArcChargeMontageEntry> Entries;

	// 未指定时在所属 Actor 上查找。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Animation")
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Animation")
	TObjectPtr<UCadenceArcComponent> CadenceArcComponent;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend struct FCadenceArcChargeMontageTestAccess;

	struct FPlayback
	{
		TWeakObjectPtr<UAnimInstance> AnimInstance;
		TWeakObjectPtr<UAnimMontage> Montage;
		int32 InstanceId = INDEX_NONE;
		FGameplayTag InputTag;
		FCadenceArcInputToken Token;
		FCadenceArcChargeMontageEntry Entry;
		uint64 EndedFrame = 0;
	};

	struct FPlaybackPlan
	{
		float Position = 0.0f;
		float PlayRate = 1.0f;
	};

	FPlayback ActivePlayback;
	TArray<FPlayback> PendingStops;
	TWeakObjectPtr<UCadenceArcComponent> BoundComponent;
	FDelegateHandle StageChangedHandle;
	FDelegateHandle HoldEndedHandle;
	uint64 LifecycleGeneration = 0;
	bool bEndingPlay = false;

	void HandleStageChanged(FGameplayTag InputTag, const FCadenceArcInputStageChange& Change);
	void HandleHoldEnded(FGameplayTag InputTag, ECadenceArcHoldEndReason Reason);
	const FCadenceArcChargeMontageEntry* FindEntry(FGameplayTag InputTag, FGameplayTag SourceActionTag) const;
	static bool ValidateEntry(const FCadenceArcChargeMontageEntry& Entry, FString& Error);
	static bool MakePlaybackPlan(const FCadenceArcChargeMontageEntry& Entry, const FCadenceArcHoldSnapshot& Snapshot,
		ECadenceArcHoldStage Stage, double DelaySeconds, FPlaybackPlan& Plan);
	static FAnimMontageInstance* FindOwnedInstance(const FPlayback& Playback);
	static void StopPlayback(const FPlayback& Playback);
	static bool ConfigureInstance(FAnimMontageInstance& Instance, const FCadenceArcChargeMontageEntry& Entry,
		const FPlaybackPlan& Plan);
	void QueueActiveStop();
	void FlushPendingStops(uint64 CurrentFrame, bool bForce);
};
