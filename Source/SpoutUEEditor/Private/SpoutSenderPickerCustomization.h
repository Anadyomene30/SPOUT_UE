// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IPropertyHandle;
class SWidget;

/**
 * Replaces the receiver's SenderName text field with a picker.
 *
 * Typing a sender name by hand is how most Spout integrations get this wrong:
 * the names are chosen by other applications, they carry _1 / _2 suffixes when
 * several instances run, and a typo looks exactly like a disconnected feed.
 * The dropdown lists what is actually publishing right now, plus an explicit
 * entry for "follow the active sender".
 */
class FSpoutReceiverComponentDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	void RefreshOptions();

	TSharedRef<SWidget> OnGenerateOption(TSharedPtr<FString> Option) const;
	void OnOptionSelected(TSharedPtr<FString> Option, ESelectInfo::Type SelectInfo);
	FText GetSelectedOptionText() const;

	TSharedPtr<IPropertyHandle> SenderNameHandle;

	/** Rebuilt each time the dropdown is opened, since senders come and go. */
	TArray<TSharedPtr<FString>> Options;
};

/** Adds a live status block to sender components: published name, size and rate. */
class FSpoutSenderComponentDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FText GetStatusText() const;

	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
};
