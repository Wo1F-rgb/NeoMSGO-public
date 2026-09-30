// Copyright NeoMSGO Project. All Rights Reserved.

#include "Gameplay/MSPlayerState.h"

#include "Core/MSTeamTypes.h"
#include "Online/MSOnlineServerSubsystem.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogMSPlayerState, Log, All);

AMSPlayerState::AMSPlayerState()
	: SelectedFaction(MSTeam::PlayerTeamId())
{
	bReplicates = true;
}

namespace
{
	constexpr int32 MSMaxLoginUsernameLength = 32;
	constexpr int32 MSMaxUsernameSearchAttempts = 1000;

	bool IsLoginUsernameTaken(const FString& Name, const TArray<FString>& TakenUsernames)
	{
		return TakenUsernames.ContainsByPredicate([&Name](const FString& Other)
		{
			return Other.Equals(Name, ESearchCase::IgnoreCase);
		});
	}

	/** True when Name is exactly the guest prefix followed by one or more digits. */
	bool IsGuestLoginUsername(const FString& Name, const FString& Prefix)
	{
		if (!Name.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return false;
		}
		const FString Digits = Name.RightChop(Prefix.Len());
		if (Digits.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Character : Digits)
		{
			if (!FChar::IsDigit(Character))
			{
				return false;
			}
		}
		return true;
	}
}

const TCHAR* AMSPlayerState::GetGuestNamePrefix()
{
	return TEXT("ゲスト");
}

FString AMSPlayerState::SanitizeLoginUsername(const FString& InUsername)
{
	FString Sanitized = InUsername;
	Sanitized.TrimStartAndEndInline();
	Sanitized.LeftInline(MSMaxLoginUsernameLength, EAllowShrinking::No);
	Sanitized.TrimStartAndEndInline();
	return Sanitized;
}

FString AMSPlayerState::ResolveUniqueLoginUsername(const FString& DesiredUsername,
	const TArray<FString>& TakenUsernames)
{
	const FString Desired = SanitizeLoginUsername(DesiredUsername);
	if (Desired.IsEmpty() || !IsLoginUsernameTaken(Desired, TakenUsernames))
	{
		return Desired;
	}

	const FString GuestPrefix(GetGuestNamePrefix());
	if (IsGuestLoginUsername(Desired, GuestPrefix))
	{
		for (int32 Index = 1; Index <= MSMaxUsernameSearchAttempts; ++Index)
		{
			FString Candidate = FString::Printf(TEXT("%s%d"), *GuestPrefix, Index);
			if (!IsLoginUsernameTaken(Candidate, TakenUsernames))
			{
				return Candidate;
			}
		}
		return Desired;
	}

	for (int32 Suffix = 2; Suffix <= MSMaxUsernameSearchAttempts; ++Suffix)
	{
		FString Candidate = SanitizeLoginUsername(FString::Printf(TEXT("%s(%d)"), *Desired, Suffix));
		if (!IsLoginUsernameTaken(Candidate, TakenUsernames))
		{
			return Candidate;
		}
	}
	return Desired;
}

void AMSPlayerState::SetLoginUsername(const FString& InUsername)
{
	if (!HasAuthority() || MSOnline::IsEnabled()) return;
	FString Sanitized = SanitizeLoginUsername(InUsername);
	if (Sanitized.IsEmpty())
	{
		return;
	}

	LoginUsername = MoveTemp(Sanitized);
	SetPlayerName(LoginUsername);
	ForceNetUpdate();
}

void AMSPlayerState::SetSelectedFaction(const FGenericTeamId InFaction)
{
	if (!HasAuthority())
	{
		return;
	}
	SelectedFaction = InFaction;
	ForceNetUpdate();
}

void AMSPlayerState::OnRep_LoginUsername()
{
	// APlayerState::PlayerName is replicated too, but keeping it synchronized here makes the
	// presence name immediately available to existing engine/UI consumers.
	SetPlayerName(LoginUsername);
	UE_LOG(LogMSPlayerState, Display, TEXT("Replicated presence received: %s"), *LoginUsername);
}

void AMSPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (AMSPlayerState* NewPlayerState = Cast<AMSPlayerState>(PlayerState))
	{
		NewPlayerState->LoginUsername = LoginUsername;
		NewPlayerState->SetPlayerName(LoginUsername);
		NewPlayerState->SelectedFaction = SelectedFaction;
	}
}

void AMSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMSPlayerState, LoginUsername);
	DOREPLIFETIME(AMSPlayerState, SelectedFaction);
}

void AMSPlayerState::SetVerifiedOnlineIdentity(const FMSOnlineIdentity& Identity)
{
 if (!HasAuthority() || !Identity.IsValid()) return;
 LoginUsername = SanitizeLoginUsername(Identity.DisplayName);
 SetPlayerName(LoginUsername); ForceNetUpdate();
}
