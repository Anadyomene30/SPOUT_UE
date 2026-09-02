// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSenderPickerCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "SpoutReceiverComponent.h"
#include "SpoutSenderComponent.h"
#include "SpoutSubsystem.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SpoutEditor"

namespace
{
	/** Shown for the empty string, which means "whatever Spout says is active". */
	FText GetActiveSenderLabel()
	{
		return LOCTEXT("ActiveSender", "<Active sender>");
	}
}

TSharedRef<IDetailCustomization> FSpoutReceiverComponentDetails::MakeInstance()
{
	return MakeShared<FSpoutReceiverComponentDetails>();
}

void FSpoutReceiverComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	SenderNameHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, SenderName));
	if (!SenderNameHandle.IsValid() || !SenderNameHandle->IsValidHandle())
	{
		return;
	}

	RefreshOptions();

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Spout|Receiver"));

	DetailBuilder.HideProperty(SenderNameHandle);

	Category.AddCustomRow(LOCTEXT("SenderFilter", "Sender"))
		.NameContent()
		[
			SenderNameHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&Options)
			.OnGenerateWidget(this, &FSpoutReceiverComponentDetails::OnGenerateOption)
			.OnSelectionChanged(this, &FSpoutReceiverComponentDetails::OnOptionSelected)
			.OnComboBoxOpening(FOnComboBoxOpening::CreateLambda([this]()
			{
				// The list is only meaningful at the moment it is shown.
				RefreshOptions();
			}))
			[
				SNew(STextBlock)
				.Text(this, &FSpoutReceiverComponentDetails::GetSelectedOptionText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

void FSpoutReceiverComponentDetails::RefreshOptions()
{
	Options.Reset();

	// The empty entry has to come first: it is the default, and it is the one
	// that cannot be discovered by looking at the list of live senders.
	Options.Add(MakeShared<FString>());

	if (const USpoutSubsystem* Subsystem = USpoutSubsystem::Get())
	{
		for (const FString& Name : Subsystem->GetSenderNames())
		{
			Options.Add(MakeShared<FString>(Name));
		}
	}

	// A name that was set while its sender was running should not vanish from the
	// list just because that application has since been closed.
	FString CurrentValue;
	if (SenderNameHandle.IsValid() && SenderNameHandle->GetValue(CurrentValue) == FPropertyAccess::Success && !CurrentValue.IsEmpty())
	{
		const bool bAlreadyListed = Options.ContainsByPredicate([&CurrentValue](const TSharedPtr<FString>& Option)
		{
			return Option.IsValid() && *Option == CurrentValue;
		});

		if (!bAlreadyListed)
		{
			Options.Add(MakeShared<FString>(CurrentValue));
		}
	}
}

TSharedRef<SWidget> FSpoutReceiverComponentDetails::OnGenerateOption(TSharedPtr<FString> Option) const
{
	const FText Label = (Option.IsValid() && !Option->IsEmpty())
		? FText::FromString(*Option)
		: GetActiveSenderLabel();

	return SNew(STextBlock)
		.Text(Label)
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

void FSpoutReceiverComponentDetails::OnOptionSelected(TSharedPtr<FString> Option, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct || !Option.IsValid() || !SenderNameHandle.IsValid())
	{
		return;
	}

	SenderNameHandle->SetValue(*Option);
}

FText FSpoutReceiverComponentDetails::GetSelectedOptionText() const
{
	FString CurrentValue;
	if (!SenderNameHandle.IsValid() || SenderNameHandle->GetValue(CurrentValue) != FPropertyAccess::Success)
	{
		return FText::GetEmpty();
	}

	return CurrentValue.IsEmpty() ? GetActiveSenderLabel() : FText::FromString(CurrentValue);
}

TSharedRef<IDetailCustomization> FSpoutSenderComponentDetails::MakeInstance()
{
	return MakeShared<FSpoutSenderComponentDetails>();
}

void FSpoutSenderComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Spout|Sender"));

	Category.AddCustomRow(LOCTEXT("SenderStatusFilter", "Status"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SenderStatus", "Status"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(STextBlock)
			.Text(this, &FSpoutSenderComponentDetails::GetStatusText)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

FText FSpoutSenderComponentDetails::GetStatusText() const
{
	if (CustomizedObjects.Num() != 1)
	{
		return LOCTEXT("MultipleSenders", "Multiple senders selected");
	}

	const USpoutSenderComponent* Component = Cast<USpoutSenderComponent>(CustomizedObjects[0].Get());
	if (!Component)
	{
		return FText::GetEmpty();
	}

	if (!Component->IsSending())
	{
		return LOCTEXT("SenderIdle", "Idle");
	}

	const FIntPoint Size = Component->GetPublishedSize();
	const FSpoutStreamStats Stats = Component->GetStats();

	if (!Stats.LastError.IsEmpty())
	{
		return FText::FromString(Stats.LastError);
	}

	return FText::FromString(FString::Printf(
		TEXT("Sending \"%s\"  %d x %d  %.1f fps"),
		*Component->GetPublishedSenderName(),
		Size.X,
		Size.Y,
		Stats.FrameRate));
}

#undef LOCTEXT_NAMESPACE
