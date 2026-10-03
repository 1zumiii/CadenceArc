#include "SCadenceArcRuntimeDetails.h"

#include "SCadenceArcChargeTimeline.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* ResolverStateName(const ECadenceArcResolverState State)
	{
		switch (State)
		{
		case ECadenceArcResolverState::Ready: return TEXT("Ready");
		case ECadenceArcResolverState::AwaitingStart: return TEXT("AwaitingStart");
		case ECadenceArcResolverState::Executing: return TEXT("Executing");
		default: return TEXT("Uninitialized");
		}
	}

	const TCHAR* HoldStageName(const ECadenceArcHoldStage Stage)
	{
		switch (Stage)
		{
		case ECadenceArcHoldStage::Holding: return TEXT("Holding");
		case ECadenceArcHoldStage::Charging: return TEXT("Charging");
		case ECadenceArcHoldStage::Charged: return TEXT("Charged");
		default: return TEXT("None");
		}
	}

	FString TagNameOrNone(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return TEXT("None");
		}
		const FString Name = Tag.ToString();
		int32 DotIndex = INDEX_NONE;
		return Name.FindLastChar(TEXT('.'), DotIndex) ? Name.RightChop(DotIndex + 1) : Name;
	}
}

void SCadenceArcRuntimeDetails::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f, 4.f, 4.f, 12.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Runtime")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(STextBlock).Text(this, &SCadenceArcRuntimeDetails::GetStateText)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(STextBlock)
				.Text(this, &SCadenceArcRuntimeDetails::GetRequestText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(STextBlock).Text(this, &SCadenceArcRuntimeDetails::GetWindowText)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(STextBlock)
				.Text(this, &SCadenceArcRuntimeDetails::GetBufferedInputText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f, 16.f, 4.f, 8.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Hold")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(STextBlock)
				.Text(this, &SCadenceArcRuntimeDetails::GetHoldText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SAssignNew(ChargeTimeline, SCadenceArcChargeTimeline)
				.Visibility(this, &SCadenceArcRuntimeDetails::GetChargeVisibility)
			]
		]
	];
}

void SCadenceArcRuntimeDetails::Update(
	const bool bInHasResolver, const FCadenceArcDebugView& View, const FCadenceArcHoldSnapshot& DisplayedHold)
{
	bHasResolver = bInHasResolver;
	LatestView = View;
	DisplayedHoldSnapshot = DisplayedHold;
	ChargeTimeline->SetSnapshot(DisplayedHoldSnapshot);
}

FText SCadenceArcRuntimeDetails::GetStateText() const
{
	return bHasResolver
		? FText::FromString(FString::Printf(TEXT("State: %s"), ResolverStateName(LatestView.ResolverState)))
		: FText::FromString(TEXT("State: no PIE Resolver selected"));
}

FText SCadenceArcRuntimeDetails::GetRequestText() const
{
	const FCadenceArcActionRequest& Request = LatestView.OutstandingRequest;
	if (Request.RequestId == 0)
	{
		return FText::FromString(TEXT("Request: None"));
	}
	return FText::FromString(FString::Printf(
		TEXT("Request Id: %lld\nSource: %s\nTarget: %s\nInput: %s"),
		static_cast<long long>(Request.RequestId),
		*TagNameOrNone(Request.SourceActionTag),
		*TagNameOrNone(Request.TargetActionTag),
		*TagNameOrNone(Request.InputTag)));
}

FText SCadenceArcRuntimeDetails::GetWindowText() const
{
	return FText::FromString(LatestView.bBufferWindowOpen
		? TEXT("Buffer window: Open") : TEXT("Buffer window: Closed"));
}

FText SCadenceArcRuntimeDetails::GetBufferedInputText() const
{
	return FText::FromString(FString::Printf(
		TEXT("Buffered Tag: %s"), *TagNameOrNone(LatestView.BufferedInputTag)));
}

FText SCadenceArcRuntimeDetails::GetHoldText() const
{
	const FCadenceArcHoldSnapshot& Hold = DisplayedHoldSnapshot;
	if (!Hold.bHasHold)
	{
		return FText::FromString(TEXT("Stage: None\nPressId: None\nCharge config: No"));
	}
	return FText::FromString(FString::Printf(
		TEXT("%s\nStage: %s\nPressId: %lld\nCharge config: %s\nInput: %s"),
		LatestView.HoldSnapshot.bHasHold ? TEXT("Active") : TEXT("Last observed (no active hold)"),
		HoldStageName(Hold.Stage), static_cast<long long>(Hold.Token.PressId),
		Hold.bHasChargeConfig ? TEXT("Yes") : TEXT("No"),
		*TagNameOrNone(Hold.InputTag)));
}

EVisibility SCadenceArcRuntimeDetails::GetChargeVisibility() const
{
	return DisplayedHoldSnapshot.bHasHold && DisplayedHoldSnapshot.bHasChargeConfig
		? EVisibility::Visible : EVisibility::Collapsed;
}
