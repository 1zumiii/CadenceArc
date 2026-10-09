#include "Component/CadenceArcChargeMontageComponent.h"

#include "AlphaBlend.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineGlobals.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Resolver/CadenceArcResolver.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCadenceArcChargeMontage, Log, All);

UCadenceArcChargeMontageComponent::UCadenceArcChargeMontageComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UCadenceArcChargeMontageComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		if (!SkeletalMesh)
		{
			if (const ACharacter* Character = Cast<ACharacter>(Owner)) SkeletalMesh = Character->GetMesh();
			if (!SkeletalMesh) SkeletalMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
		}
		if (!CadenceArcComponent) CadenceArcComponent = Owner->FindComponentByClass<UCadenceArcComponent>();
	}
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FString Error;
		if (!ValidateEntry(Entries[Index], Error))
		{
			UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: entry %d: %s"), *GetNameSafe(this), Index, *Error);
		}
		for (int32 Earlier = 0; Earlier < Index; ++Earlier)
		{
			if (Entries[Earlier].InputTag == Entries[Index].InputTag && Entries[Earlier].SourceActionTag == Entries[Index].SourceActionTag)
			{
				UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: duplicate entry %d; first matching pair wins (%d)."),
					*GetNameSafe(this), Index, Earlier);
				break;
			}
		}
	}
	if (!SkeletalMesh || !CadenceArcComponent)
	{
		UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: charge montage presentation needs a mesh and CadenceArc component."), *GetNameSafe(this));
		return;
	}
	BoundComponent = CadenceArcComponent;
	StageChangedHandle = CadenceArcComponent->OnHoldStageChangedNative.AddUObject(this, &ThisClass::HandleStageChanged);
	HoldEndedHandle = CadenceArcComponent->OnHoldEndedNative.AddUObject(this, &ThisClass::HandleHoldEnded);
}

void UCadenceArcChargeMontageComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	++LifecycleGeneration;
	if (UCadenceArcComponent* Bound = BoundComponent.Get())
	{
		Bound->OnHoldStageChangedNative.Remove(StageChangedHandle);
		Bound->OnHoldEndedNative.Remove(HoldEndedHandle);
	}
	BoundComponent.Reset();
	// 停止前先清掉本地资格；停止回调重入时不会重复操作同一个实例。
	const FPlayback Previous = ActivePlayback;
	ActivePlayback = {};
	StopPlayback(Previous);
	FlushPendingStops(GFrameCounter, true);
	SetComponentTickEnabled(false);
	Super::EndPlay(EndPlayReason);
}

const FCadenceArcChargeMontageEntry* UCadenceArcChargeMontageComponent::FindEntry(FGameplayTag InputTag, FGameplayTag SourceActionTag) const
{
	const FCadenceArcChargeMontageEntry* Wildcard = nullptr;
	for (const FCadenceArcChargeMontageEntry& Entry : Entries)
	{
		if (Entry.InputTag != InputTag) continue;
		if (Entry.SourceActionTag.IsValid() && Entry.SourceActionTag == SourceActionTag) return &Entry;
		if (!Entry.SourceActionTag.IsValid() && !Wildcard) Wildcard = &Entry;
	}
	return Wildcard;
}

bool UCadenceArcChargeMontageComponent::ValidateEntry(const FCadenceArcChargeMontageEntry& Entry, FString& Error)
{
	if (!Entry.InputTag.IsValid()) Error = TEXT("InputTag must be valid.");
	else if (!IsValid(Entry.Montage)) Error = TEXT("Montage is required.");
	else if (Entry.WindupSection.IsNone() || Entry.Montage->GetSectionIndex(Entry.WindupSection) == INDEX_NONE)
		Error = TEXT("Windup section does not exist.");
	else if (Entry.HoldSection.IsNone() || Entry.Montage->GetSectionIndex(Entry.HoldSection) == INDEX_NONE)
		Error = TEXT("Hold section does not exist.");
	else if (Entry.WindupSection == Entry.HoldSection) Error = TEXT("Windup and Hold sections must differ.");
	else if (!FMath::IsFinite(Entry.Montage->RateScale) || Entry.Montage->RateScale <= 0.0f)
		Error = TEXT("Montage RateScale must be finite and positive.");
	else if (!FMath::IsFinite(Entry.StopBlendOutSeconds) || Entry.StopBlendOutSeconds < 0.0f)
		Error = TEXT("Stop blend-out must be finite and nonnegative.");
	else
	{
		const float WindupLength = Entry.Montage->GetSectionLength(Entry.Montage->GetSectionIndex(Entry.WindupSection));
		const float HoldLength = Entry.Montage->GetSectionLength(Entry.Montage->GetSectionIndex(Entry.HoldSection));
		if (!FMath::IsFinite(WindupLength) || WindupLength <= 0.0f || !FMath::IsFinite(HoldLength) || HoldLength <= 0.0f)
			Error = TEXT("Windup and Hold sections must have positive finite lengths.");
		else return true;
	}
	return false;
}

bool UCadenceArcChargeMontageComponent::MakePlaybackPlan(const FCadenceArcChargeMontageEntry& Entry,
	const FCadenceArcHoldSnapshot& Snapshot, ECadenceArcHoldStage Stage, double DelaySeconds, FPlaybackPlan& Plan)
{
	const double ChargeDuration = Snapshot.ChargeFullSeconds - Snapshot.ChargeStartSeconds;
	if (!FMath::IsFinite(ChargeDuration) || ChargeDuration <= 0.0 || !FMath::IsFinite(DelaySeconds) || DelaySeconds < 0.0
		|| !FMath::IsFinite(Snapshot.MaxChargedHoldSeconds) || Snapshot.MaxChargedHoldSeconds < 0.0)
		return false;
	float Start = 0.0f, End = 0.0f;
	const bool bCharged = Stage == ECadenceArcHoldStage::Charged;
	Entry.Montage->GetSectionStartAndEndTime(Entry.Montage->GetSectionIndex(bCharged ? Entry.HoldSection : Entry.WindupSection), Start, End);
	const double Length = End - Start;
	double EffectiveRate = Length / ChargeDuration;
	if (bCharged)
	{
		if (Snapshot.MaxChargedHoldSeconds > 0.0) EffectiveRate = Length / Snapshot.MaxChargedHoldSeconds * 0.97;
		else EffectiveRate = Entry.Montage->GetSectionLength(Entry.Montage->GetSectionIndex(Entry.WindupSection)) / ChargeDuration;
	}
	const double Position = Start + (bCharged ? FMath::Fmod(DelaySeconds * EffectiveRate, Length) : DelaySeconds * EffectiveRate);
	const double Rate = EffectiveRate / Entry.Montage->RateScale;
	if (!FMath::IsFinite(Position) || !FMath::IsFinite(Rate) || Rate <= 0.0 || Rate > MAX_flt || Position > MAX_flt) return false;
	Plan.Position = static_cast<float>(Position);
	Plan.PlayRate = static_cast<float>(Rate);
	return Plan.PlayRate > 0.0f;
}

FAnimMontageInstance* UCadenceArcChargeMontageComponent::FindOwnedInstance(const FPlayback& Playback)
{
	UAnimInstance* Anim = Playback.AnimInstance.Get();
	if (!Anim || !Playback.Montage.IsValid() || Playback.InstanceId == INDEX_NONE) return nullptr;
	FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(Playback.InstanceId);
	return Instance && Instance->Montage == Playback.Montage.Get() ? Instance : nullptr;
}

bool UCadenceArcChargeMontageComponent::ConfigureInstance(FAnimMontageInstance& Instance,
	const FCadenceArcChargeMontageEntry& Entry, const FPlaybackPlan& Plan)
{
	if (!Instance.SetNextSectionName(Entry.WindupSection, Entry.HoldSection)
		|| !Instance.SetNextSectionName(Entry.HoldSection, Entry.HoldSection)) return false;
	Instance.SetPlayRate(Plan.PlayRate);
	Instance.SetPosition(Plan.Position);
	return true;
}

void UCadenceArcChargeMontageComponent::HandleStageChanged(FGameplayTag InputTag, const FCadenceArcInputStageChange& Change)
{
	UCadenceArcComponent* Source = BoundComponent.Get();
	if (bEndingPlay || !IsValid(Source) || !Source->GetResolver() || !IsValid(SkeletalMesh)
		|| (Change.ToStage != ECadenceArcHoldStage::Charging && Change.ToStage != ECadenceArcHoldStage::Charged)) return;
	const FCadenceArcHoldSnapshot Snapshot = Source->GetResolver()->GetInputHoldSnapshot();
	// 同一次推进可能已跨过自动释放点；此时只会收到历史阶段事件，不能短暂重播已结束的蓄力。
	if (!Snapshot.bHasHold || !Snapshot.bHasChargeConfig || Snapshot.InputTag != InputTag) return;
	const FCadenceArcChargeMontageEntry* Matched = FindEntry(InputTag, Snapshot.SourceActionTag);
	if (!Matched) return;
	const bool bSameHold = ActivePlayback.InstanceId != INDEX_NONE && ActivePlayback.Token == Snapshot.Token;
	const FCadenceArcChargeMontageEntry Entry = bSameHold ? ActivePlayback.Entry : *Matched;
	FString Error;
	if (!ValidateEntry(Entry, Error))
	{
		UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: skipping charge presentation: %s"), *GetNameSafe(this), *Error);
		return;
	}
	const double Now = Source->GetTimeSeconds();
	// Charging 通知也可能晚到蓄满之后：直接定位 Hold，避免把完整 Windup 再播一次。
	const ECadenceArcHoldStage Stage = Snapshot.Stage == ECadenceArcHoldStage::Charged ? ECadenceArcHoldStage::Charged : Change.ToStage;
	if (bSameHold && Stage == ECadenceArcHoldStage::Charged && Snapshot.MaxChargedHoldSeconds == 0.0) return;
	const double Threshold = Stage == ECadenceArcHoldStage::Charged
		? Snapshot.ChargeFullTimestampSeconds : Change.EffectiveTimestampSeconds;
	FPlaybackPlan Plan;
	if (!FMath::IsFinite(Now) || !FMath::IsFinite(Threshold)
		|| !MakePlaybackPlan(Entry, Snapshot, Stage, FMath::Max(0.0, Now - Threshold), Plan))
	{
		UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: skipping charge presentation: invalid timing or play rate."), *GetNameSafe(this));
		return;
	}
	if (bSameHold)
	{
		if (FAnimMontageInstance* Instance = FindOwnedInstance(ActivePlayback); Instance && Instance->IsActive())
			ConfigureInstance(*Instance, Entry, Plan);
		return;
	}
	QueueActiveStop();
	UAnimInstance* Anim = SkeletalMesh->GetAnimInstance();
	if (!Anim)
	{
		UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: mesh has no animation instance."), *GetNameSafe(this));
		return;
	}
	const uint64 PlayGeneration = ++LifecycleGeneration;
	TSet<int32> PreviousIds;
	for (const FAnimMontageInstance* Existing : Anim->MontageInstances)
	{
		if (Existing) PreviousIds.Add(Existing->GetInstanceID());
	}
	if (Anim->Montage_Play(Entry.Montage, Plan.PlayRate, EMontagePlayReturnType::MontageLength, Plan.Position, false) <= 0.0f)
	{
		UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: could not play charge montage %s."), *GetNameSafe(this), *GetNameSafe(Entry.Montage));
		return;
	}
	// Play 内部的同步回调可能再次播放同一资产。不能把此时 active map 中的新执行器实例认领成自己的。
	FAnimMontageInstance* Instance = nullptr;
	for (FAnimMontageInstance* Candidate : Anim->MontageInstances)
	{
		if (!Candidate || Candidate->Montage != Entry.Montage || PreviousIds.Contains(Candidate->GetInstanceID())) continue;
		if (Instance)
		{
			UE_LOG(LogCadenceArcChargeMontage, Warning, TEXT("%s: reentrant same-asset playback is ambiguous; skipping instance ownership."), *GetNameSafe(this));
			return;
		}
		Instance = Candidate;
	}
	if (!Instance) return;
	FPlayback Started;
	Started.AnimInstance = Anim;
	Started.Montage = Entry.Montage;
	Started.InstanceId = Instance->GetInstanceID();
	Started.InputTag = InputTag;
	Started.Token = Snapshot.Token;
	Started.Entry = Entry;
	const FCadenceArcHoldSnapshot After = IsValid(Source) && Source->GetResolver()
		? Source->GetResolver()->GetInputHoldSnapshot() : FCadenceArcHoldSnapshot{};
	if (bEndingPlay || PlayGeneration != LifecycleGeneration || !After.bHasHold || !(After.Token == Snapshot.Token))
	{
		if (bEndingPlay) StopPlayback(Started);
		else
		{
			Started.EndedFrame = GFrameCounter;
			PendingStops.Add(Started);
			SetComponentTickEnabled(true);
		}
		return;
	}
	if (!Instance->IsActive()) return;
	ActivePlayback = Started;
	if (!ConfigureInstance(*Instance, Entry, Plan)) QueueActiveStop();
}

void UCadenceArcChargeMontageComponent::HandleHoldEnded(FGameplayTag InputTag, ECadenceArcHoldEndReason Reason)
{
	++LifecycleGeneration;
	// 更早的结束监听者可能已同步授予同 Tag 的新资格；旧通知不能清理新表现。
	if (UCadenceArcComponent* Source = BoundComponent.Get(); Source && Source->GetResolver())
	{
		const FCadenceArcHoldSnapshot Current = Source->GetResolver()->GetInputHoldSnapshot();
		if (Current.bHasHold && Current.Token == ActivePlayback.Token) return;
	}
	if (ActivePlayback.InputTag == InputTag) QueueActiveStop();
}

void UCadenceArcChargeMontageComponent::QueueActiveStop()
{
	if (ActivePlayback.InstanceId == INDEX_NONE) return;
	ActivePlayback.EndedFrame = GFrameCounter;
	PendingStops.Add(ActivePlayback);
	ActivePlayback = {};
	SetComponentTickEnabled(true);
}

void UCadenceArcChargeMontageComponent::StopPlayback(const FPlayback& Playback)
{
	// 必须按实例 ID 停止。执行器可能已经用同一资产播放新的动作实例。
	if (FAnimMontageInstance* Instance = FindOwnedInstance(Playback); Instance && Instance->IsActive())
		Instance->Stop(FAlphaBlend(Playback.Entry.StopBlendOutSeconds));
}

void UCadenceArcChargeMontageComponent::FlushPendingStops(uint64 CurrentFrame, bool bForce)
{
	TArray<FPlayback> Due;
	for (int32 Index = PendingStops.Num() - 1; Index >= 0; --Index)
	{
		if (bForce || CurrentFrame > PendingStops[Index].EndedFrame)
		{
			Due.Add(PendingStops[Index]);
			PendingStops.RemoveAtSwap(Index);
		}
	}
	for (const FPlayback& Playback : Due) StopPlayback(Playback);
	SetComponentTickEnabled(!PendingStops.IsEmpty());
}

void UCadenceArcChargeMontageComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FlushPendingStops(GFrameCounter, false);
}

#if WITH_EDITOR
EDataValidationResult UCadenceArcChargeMontageComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FString Error;
		if (!ValidateEntry(Entries[Index], Error))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Charge montage entry %d: %s"), Index, *Error)));
			Result = EDataValidationResult::Invalid;
		}
		for (int32 Earlier = 0; Earlier < Index; ++Earlier)
		{
			if (Entries[Earlier].InputTag == Entries[Index].InputTag && Entries[Earlier].SourceActionTag == Entries[Index].SourceActionTag)
			{
				Context.AddWarning(FText::FromString(FString::Printf(TEXT("Charge montage entry %d duplicates %d; first matching pair wins."), Index, Earlier)));
				break;
			}
		}
	}
	return Result == EDataValidationResult::Invalid ? Result : EDataValidationResult::Valid;
}
#endif
