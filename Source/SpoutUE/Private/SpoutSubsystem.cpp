#include "SpoutSubsystem.h"

#include "D3D12/SpoutFormatUtils.h"
#include "Engine/Engine.h"
#include "SpoutSettings.h"
#include "SpoutUE.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include "SpoutSenderNames.h"
#include "SpoutUtils.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

#include <set>
#include <string>

namespace
{
	/**
	 * The Spout directory lives in shared memory and needs no graphics device,
	 * so one process-wide instance is enough to answer every query.
	 */
	spoutSenderNames& GetSenderDirectory()
	{
		static spoutSenderNames Directory;
		return Directory;
	}

	FString DXGIFormatName(uint32 Format)
	{
		return SpoutUE::DXGIFormatToString(static_cast<DXGI_FORMAT>(Format));
	}
}

#endif // PLATFORM_WINDOWS

USpoutSubsystem* USpoutSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<USpoutSubsystem>() : nullptr;
}

void USpoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if PLATFORM_WINDOWS
	const USpoutSettings* Settings = USpoutSettings::Get();

	GetSenderDirectory().SetMaxSenders(FMath::Clamp(Settings->MaxSenders, 1, 255));

	if (Settings->bVerboseSpoutLogging)
	{
		// A log file rather than EnableSpoutLog(): the console variant spawns a
		// window, which is not something an editor plugin should do on startup.
		spoututils::EnableSpoutLogFile("SpoutUE.log");
		spoututils::SetSpoutLogLevel(spoututils::SPOUT_LOG_VERBOSE);
	}
	else
	{
		spoututils::SetSpoutLogLevel(spoututils::SPOUT_LOG_ERROR);
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &USpoutSubsystem::Tick));

	RefreshSenders();

	UE_LOG(LogSpout, Log, TEXT("Spout subsystem ready (SDK %s)."), UTF8_TO_TCHAR(spoututils::GetSDKversion().c_str()));
#else
	UE_LOG(LogSpout, Warning, TEXT("Spout is only available on Windows."));
#endif
}

void USpoutSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	Senders.Reset();

	Super::Deinitialize();
}

bool USpoutSubsystem::Tick(float DeltaSeconds)
{
	const USpoutSettings* Settings = USpoutSettings::Get();

	TimeSinceRefresh += DeltaSeconds;
	if (TimeSinceRefresh >= Settings->SenderDiscoveryInterval)
	{
		TimeSinceRefresh = 0.f;
		RefreshSenders();
	}

	return true;
}

void USpoutSubsystem::RefreshSenders()
{
#if PLATFORM_WINDOWS
	std::set<std::string> Names;
	if (!GetSenderDirectory().GetSenderNames(&Names))
	{
		Names.clear();
	}

	const FString ActiveName = GetActiveSenderName();

	TArray<FSpoutSenderInfo> NewSenders;
	NewSenders.Reserve(static_cast<int32>(Names.size()));

	for (const std::string& Name : Names)
	{
		FSpoutSenderInfo Info;
		Info.Name = UTF8_TO_TCHAR(Name.c_str());

		unsigned int Width = 0;
		unsigned int Height = 0;
		HANDLE ShareHandle = nullptr;
		DWORD Format = 0;

		if (GetSenderDirectory().GetSenderInfo(Name.c_str(), Width, Height, ShareHandle, Format))
		{
			Info.Width = static_cast<int32>(Width);
			Info.Height = static_cast<int32>(Height);
			Info.Format = DXGIFormatName(static_cast<uint32>(Format));
		}

		Info.bIsActiveSender = (Info.Name == ActiveName);
		NewSenders.Add(MoveTemp(Info));
	}

	NewSenders.Sort([](const FSpoutSenderInfo& A, const FSpoutSenderInfo& B)
	{
		return A.Name < B.Name;
	});

	// Diff before swapping so subscribers get one event per actual change rather
	// than a full rebuild every poll.
	TArray<FSpoutSenderInfo> Added;
	TArray<FSpoutSenderInfo> Removed;

	for (const FSpoutSenderInfo& Candidate : NewSenders)
	{
		const bool bWasKnown = Senders.ContainsByPredicate([&Candidate](const FSpoutSenderInfo& Existing)
		{
			return Existing.Name == Candidate.Name;
		});

		if (!bWasKnown)
		{
			Added.Add(Candidate);
		}
	}

	for (const FSpoutSenderInfo& Existing : Senders)
	{
		const bool bStillThere = NewSenders.ContainsByPredicate([&Existing](const FSpoutSenderInfo& Candidate)
		{
			return Candidate.Name == Existing.Name;
		});

		if (!bStillThere)
		{
			Removed.Add(Existing);
		}
	}

	Senders = MoveTemp(NewSenders);

	for (const FSpoutSenderInfo& Info : Added)
	{
		OnSenderAdded.Broadcast(Info);
	}

	for (const FSpoutSenderInfo& Info : Removed)
	{
		OnSenderRemoved.Broadcast(Info);
	}

	if (Added.Num() > 0 || Removed.Num() > 0)
	{
		OnSenderListChanged.Broadcast();
	}
#endif
}

TArray<FString> USpoutSubsystem::GetSenderNames() const
{
	TArray<FString> Names;
	Names.Reserve(Senders.Num());

	for (const FSpoutSenderInfo& Info : Senders)
	{
		Names.Add(Info.Name);
	}

	return Names;
}

bool USpoutSubsystem::GetSenderInfo(const FString& SenderName, FSpoutSenderInfo& OutInfo) const
{
	OutInfo = FSpoutSenderInfo();

	if (SenderName.IsEmpty())
	{
		return false;
	}

#if PLATFORM_WINDOWS
	unsigned int Width = 0;
	unsigned int Height = 0;
	HANDLE ShareHandle = nullptr;
	DWORD Format = 0;

	const FTCHARToUTF8 NameUtf8(*SenderName);
	if (!GetSenderDirectory().GetSenderInfo(NameUtf8.Get(), Width, Height, ShareHandle, Format))
	{
		return false;
	}

	OutInfo.Name = SenderName;
	OutInfo.Width = static_cast<int32>(Width);
	OutInfo.Height = static_cast<int32>(Height);
	OutInfo.Format = DXGIFormatName(static_cast<uint32>(Format));
	OutInfo.bIsActiveSender = (SenderName == GetActiveSenderName());
	return true;
#else
	return false;
#endif
}

bool USpoutSubsystem::IsSenderAvailable(const FString& SenderName) const
{
#if PLATFORM_WINDOWS
	if (SenderName.IsEmpty())
	{
		return false;
	}

	const FTCHARToUTF8 NameUtf8(*SenderName);
	return GetSenderDirectory().FindSenderName(NameUtf8.Get());
#else
	return false;
#endif
}

FString USpoutSubsystem::GetActiveSenderName() const
{
#if PLATFORM_WINDOWS
	char Name[256] = {};
	if (GetSenderDirectory().GetActiveSender(Name, UE_ARRAY_COUNT(Name)))
	{
		return UTF8_TO_TCHAR(Name);
	}
#endif
	return FString();
}

bool USpoutSubsystem::SetActiveSenderName(const FString& SenderName)
{
#if PLATFORM_WINDOWS
	if (SenderName.IsEmpty())
	{
		return false;
	}

	const FTCHARToUTF8 NameUtf8(*SenderName);
	return GetSenderDirectory().SetActiveSender(NameUtf8.Get());
#else
	return false;
#endif
}
