// Copyright NeoMSGO Project. All Rights Reserved.
//
// 標準ジム型ビーム・サーベルの時系列受け入れテスト。
// モーション値そのものは MSStandardSaberMotion の仮データを参照し、
// ここでは「Active区間だけ判定する」「同一段の重複を抑える」「段遷移と中断」を検証する。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <initializer_list>

#include "AbilitySystemComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSStandardSaberMotion.h"
#include "Components/SceneComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Player/MSCharacterMovementComponent.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponAbilityRouting.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberAbilityTest,
	"NeoMSGO.Weapons.StandardSaberAbility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSStandardSaberInputCancelTest,
	"NeoMSGO.Weapons.StandardSaberInputCancel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** Keep input-route coverage independent from the editor's normal Online default. */
	struct FScopedStandardSaberOfflineAuth
	{
		FString Previous = FCommandLine::Get();

		FScopedStandardSaberOfflineAuth()
		{
			TArray<FString> Tokens;
			Previous.ParseIntoArrayWS(Tokens);
			FString Effective;
			for (const FString& Token : Tokens)
			{
				if (Token.StartsWith(TEXT("-MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthMode="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("-MSAuthBaseUrl="), ESearchCase::IgnoreCase)
					|| Token.StartsWith(TEXT("/MSAuthBaseUrl="), ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
				Effective += Token;
			}
			if (!Effective.IsEmpty()) Effective.AppendChar(TEXT(' '));
			Effective += TEXT("-MSAuthMode=OfflineLocal");
			FCommandLine::Set(*Effective);
		}

		~FScopedStandardSaberOfflineAuth()
		{
			FCommandLine::Set(*Previous);
		}
	};

	struct FStandardSaberTestWorld
	{
		UWorld* World = nullptr;
		FStandardSaberTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSStandardSaberTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FStandardSaberTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	void TickStandardSaberAbilityWorld(UWorld* World, float Seconds)
	{
		while (Seconds > 0.0f)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
			++GFrameCounter;
		}
	}

	void TickWorldWithSteps(UWorld* World, std::initializer_list<float> Steps)
	{
		for (const float Step : Steps)
		{
			World->Tick(LEVELTICK_All, Step);
			++GFrameCounter;
		}
	}

	AMSUnitBase* SpawnUnit(UWorld* World, const FVector& Location, const FGenericTeamId Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMSUnitBase* Unit = World->SpawnActor<AMSPlayerCharacter>(
			AMSPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (!Unit) return nullptr;
		Unit->SetGenericTeamId(Team);
		Unit->GetCharacterMovement()->DisableMovement();
		return Unit;
	}

	UMSMeleeWeaponData* MakeStandardWeapon(UObject* Outer)
	{
		UMSMeleeWeaponData* Weapon = NewObject<UMSMeleeWeaponData>(Outer);
		Weapon->Range = 3.0f;
		Weapon->Power = 100;
		Weapon->DownPoint = 10;
		Weapon->FireInterval = 0.40f;
		Weapon->MagazineSize = 0;
		Weapon->bUseStandardBeamSaberMotion = true;
		return Weapon;
	}

	FVector BladeMidpointWorld(AMSUnitBase* Attacker, const int32 AttackIndex, const float TimeSeconds)
	{
		FVector LocalStart = FVector::ZeroVector;
		FVector LocalEnd = FVector::ZeroVector;
		FMSStandardSaberMotion::SampleBlade(AttackIndex, TimeSeconds, 100.0f, LocalStart, LocalEnd);
		if (UMSMechRigComponent* Rig = Attacker->GetMechRigComponent())
		{
			if (USceneComponent* Root = Rig->GetRigRoot())
			{
				FVector Midpoint = Root->GetComponentTransform().TransformPosition((LocalStart + LocalEnd) * 0.5f);
				const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(AttackIndex);
				const float LungeAlpha = FMath::Clamp((TimeSeconds - Timing.LungeStart)
					/ FMath::Max(Timing.LungeEnd - Timing.LungeStart, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
				Midpoint += Attacker->GetActorForwardVector()
					* (LungeAlpha * Timing.LungeDistanceCm * MSGO::MSScale);
				return Midpoint;
			}
		}
		FTransform ActorTransform = Attacker->GetActorTransform();
		ActorTransform.SetScale3D(ActorTransform.GetScale3D() * MSGO::MSScale);
		FVector Midpoint = ActorTransform.TransformPosition((LocalStart + LocalEnd) * 0.5f);
		const FMSStandardSaberTiming Timing = FMSStandardSaberMotion::GetTiming(AttackIndex);
		const float LungeAlpha = FMath::Clamp((TimeSeconds - Timing.LungeStart)
			/ FMath::Max(Timing.LungeEnd - Timing.LungeStart, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		Midpoint += Attacker->GetActorForwardVector()
			* (LungeAlpha * Timing.LungeDistanceCm * MSGO::MSScale);
		return Midpoint;
	}

	void PlaceTargetOnBlade(AMSUnitBase* Target, AMSUnitBase* Attacker,
		const int32 AttackIndex, const float TimeSeconds)
	{
		Target->SetActorLocation(BladeMidpointWorld(Attacker, AttackIndex, TimeSeconds));
	}
}

bool FMSStandardSaberAbilityTest::RunTest(const FString& Parameters)
{
	FStandardSaberTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	const FGenericTeamId PlayerTeam(1);
	const FGenericTeamId EnemyTeam(2);

	AMSUnitBase* Attacker = SpawnUnit(World, FVector::ZeroVector, PlayerTeam);
	AMSUnitBase* Target = SpawnUnit(World, FVector(5000.0f, 0.0f, 0.0f), EnemyTeam);
	if (!TestNotNull(TEXT("standard saber attacker exists"), Attacker)
		|| !TestNotNull(TEXT("standard saber target exists"), Target))
	{
		return false;
	}

	UMSMeleeWeaponData* StandardWeapon = MakeStandardWeapon(Attacker);
	Attacker->GetWeaponComponent()->InitializeSlots({StandardWeapon});
	TStrongObjectPtr<UGA_Melee> Ability(NewObject<UGA_Melee>(Attacker));
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);

	UAbilitySystemComponent* AttackerASC = Attacker->GetAbilitySystemComponent();
	const float ArmorBefore = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("startup begins without an immediate hit"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	const float CooldownAfterStart = Attacker->GetWeaponComponent()->GetFireCooldownRemaining();
	TestFalse(TEXT("an early repeated input is rejected during startup"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	TestTrue(TEXT("early repeated input does not consume another cooldown"),
		FMath::IsNearlyEqual(Attacker->GetWeaponComponent()->GetFireCooldownRemaining(),
			CooldownAfterStart, 0.001f));
	TickStandardSaberAbilityWorld(World, 0.12f);
	TestTrue(TEXT("startup before ActiveStart causes no damage"),
		FMath::IsNearlyEqual(Target->GetVitalSet()->GetArmor(), ArmorBefore, 0.01f));
	TickStandardSaberAbilityWorld(World, 0.12f);
	const float ArmorAfterActive = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("ActiveStart..ActiveEnd applies damage"), ArmorAfterActive < ArmorBefore - 0.5f);
	TestEqual(TEXT("one actor is hit once during one swing"), Ability->GetLastHitCount(), 1);
	TickStandardSaberAbilityWorld(World, 0.20f);
	TestTrue(TEXT("the same actor is not damaged twice by one stage"),
		FMath::IsNearlyEqual(Target->GetVitalSet()->GetArmor(), ArmorAfterActive, 0.01f));
	TestEqual(TEXT("first standard stage is right diagonal"),
		Ability->GetLastStandardSaberAttackIndex(), 0);

	// 無入力なら初段だけで終了し、硬直後に次の入力を受け付ける。
	TickStandardSaberAbilityWorld(World, 0.35f);
	TestEqual(TEXT("no input ends after the first standard stage"),
		Ability->GetLastStandardSaberAttackIndex(), 0);
	TickStandardSaberAbilityWorld(World, 0.50f);

	// 初段の後半入力をバッファし、初段終了時に二段目へ接続する。
	TestTrue(TEXT("a second standard swing starts after recovery"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	PlaceTargetOnBlade(Target, Attacker, 1, 0.20f);
	TickStandardSaberAbilityWorld(World, 0.35f);
	TestTrue(TEXT("a latter-half input is accepted as a buffered follow-up"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	TickStandardSaberAbilityWorld(World, 0.52f);
	TestEqual(TEXT("buffered input starts the second standard stage"),
		Ability->GetLastStandardSaberAttackIndex(), 1);
	TestTrue(TEXT("second stage can hit again"),
		Target->GetVitalSet()->GetArmor() < ArmorAfterActive - 0.5f);

	// 期限切れ後は新しい初段として扱い、D格は追加入力を持たない。
	TickStandardSaberAbilityWorld(World, 1.00f);
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	TestTrue(TEXT("a new swing starts after recovery and expired combo window"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	TestEqual(TEXT("expired follow-up does not reopen stage two"),
		Ability->GetLastStandardSaberAttackIndex(), 0);
	TickStandardSaberAbilityWorld(World, 0.75f);
	TickStandardSaberAbilityWorld(World, 0.60f);
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	TestTrue(TEXT("input after the combo deadline starts a fresh first stage"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	TestEqual(TEXT("expired combo input does not start stage two"),
		Ability->GetLastStandardSaberAttackIndex(), 0);
	TickStandardSaberAbilityWorld(World, 0.75f);
	TickStandardSaberAbilityWorld(World, 0.60f);

	PlaceTargetOnBlade(Target, Attacker, 2, 0.20f);
	const float DashArmorBefore = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("D格 standard saber is a single stage"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, true));
	TickStandardSaberAbilityWorld(World, 0.24f);
	const float DashDamage = DashArmorBefore - Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("D格 only applies damage during its active window"), DashDamage > 0.5f);
	TestEqual(TEXT("D格 is not followed by a second standard stage"),
		Ability->GetLastStandardSaberAttackIndex(), 2);
	TestTrue(TEXT("D格 damage is three times the normal power"),
		FMath::IsNearlyEqual(DashDamage, (ArmorBefore - ArmorAfterActive) * 3.0f, 1.0f));

	// 死亡・よろけは判定／突進をキャンセルする。
	TickStandardSaberAbilityWorld(World, 1.20f);
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	const float CancelArmor = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("a swing can be started before an interruption"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	AttackerASC->SetLooseGameplayTagCount(MSTags::State_Staggered, 1);
	TickStandardSaberAbilityWorld(World, 0.50f);
	TestTrue(TEXT("stagger cancels pending standard damage"),
		FMath::IsNearlyEqual(Target->GetVitalSet()->GetArmor(), CancelArmor, 0.01f));
	AttackerASC->SetLooseGameplayTagCount(MSTags::State_Staggered, 0);

	TickStandardSaberAbilityWorld(World, 0.90f);
	for (const FGameplayTag& ReactionTag : {MSTags::State_Flinched.GetTag(), MSTags::State_GettingUp.GetTag()})
	{
		PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
		AttackerASC->SetLooseGameplayTagCount(ReactionTag, 1);
		TestFalse(TEXT("flinch/get-up prevents a new standard saber stage"),
			Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
		AttackerASC->SetLooseGameplayTagCount(ReactionTag, 0);
		const float ReactionArmor = Target->GetVitalSet()->GetArmor();
		TestTrue(TEXT("standard saber starts after the reaction clears"),
			Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
		AttackerASC->SetLooseGameplayTagCount(ReactionTag, 1);
		TickStandardSaberAbilityWorld(World, 0.50f);
		TestTrue(TEXT("flinch/get-up interrupts an active standard saber before damage"),
			FMath::IsNearlyEqual(Target->GetVitalSet()->GetArmor(), ReactionArmor, 0.01f));
		AttackerASC->SetLooseGameplayTagCount(ReactionTag, 0);
		TickStandardSaberAbilityWorld(World, 0.90f);
	}
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	const float DeathCancelArmor = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("a swing can be cancelled by death"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	AttackerASC->SetLooseGameplayTagCount(MSTags::State_Dead, 1);
	TickStandardSaberAbilityWorld(World, 0.50f);
	TestTrue(TEXT("death cancels pending standard damage"),
		FMath::IsNearlyEqual(Target->GetVitalSet()->GetArmor(), DeathCancelArmor, 0.01f));
	AttackerASC->SetLooseGameplayTagCount(MSTags::State_Dead, 0);

	// 別dt列でもActive区間を跨いで判定できることを確認する。
	TickStandardSaberAbilityWorld(World, 0.90f);
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	const float VariableDtArmor = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("variable-dt swing starts"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	TickWorldWithSteps(World, {0.13f, 0.02f, 0.17f});
	TestTrue(TEXT("variable dt does not skip the active window"),
		Target->GetVitalSet()->GetArmor() < VariableDtArmor - 0.5f);

	// 武器切替は残りの判定とコンボ窓をキャンセルする。
	TickStandardSaberAbilityWorld(World, 0.90f);
	UMSMeleeWeaponData* OtherWeapon = NewObject<UMSMeleeWeaponData>(Attacker);
	OtherWeapon->FireInterval = 0.0f;
	OtherWeapon->MagazineSize = 0;
	Attacker->GetWeaponComponent()->InitializeSlots({StandardWeapon, OtherWeapon});
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	const float SwitchCancelArmor = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("a swing can be cancelled by weapon switch"),
		Ability->PerformMelee(Attacker, Attacker->GetActorLocation(), FVector::ForwardVector, false));
	TestTrue(TEXT("weapon switch request succeeds"), Attacker->GetWeaponComponent()->SwitchToSlot(1));
	TickStandardSaberAbilityWorld(World, 0.50f);
	TestTrue(TEXT("weapon switch cancels pending standard damage"),
		FMath::IsNearlyEqual(Target->GetVitalSet()->GetArmor(), SwitchCancelArmor, 0.01f));

	return true;
}

bool FMSStandardSaberInputCancelTest::RunTest(const FString& Parameters)
{
	FScopedStandardSaberOfflineAuth OfflineAuth;
	FStandardSaberTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AMSPlayerCharacter* Attacker = Cast<AMSPlayerCharacter>(
		SpawnUnit(World, FVector::ZeroVector, FGenericTeamId(1)));
	AMSUnitBase* Target = SpawnUnit(World, FVector(5000.0f, 0.0f, 0.0f), FGenericTeamId(2));
	if (!TestNotNull(TEXT("input-path saber attacker exists"), Attacker)
		|| !TestNotNull(TEXT("input-path saber target exists"), Target))
	{
		return false;
	}

	UMSMeleeWeaponData* StandardWeapon = MakeStandardWeapon(Attacker);
	Attacker->GetWeaponComponent()->InitializeSlots({StandardWeapon});
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	const float ArmorBeforeRelease = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("StartCurrentWeaponFire enters the melee route"),
		Attacker->StartCurrentWeaponFire());
	TestEqual(TEXT("StartCurrentWeaponFire resolves the melee route"),
		Attacker->GetLastWeaponAbilityRoute(), EMSWeaponAbilityRoute::Melee);
	Attacker->StopCurrentWeaponFire();
	TickStandardSaberAbilityWorld(World, 0.24f);
	TestTrue(TEXT("StopCurrentWeaponFire leaves an active standard swing running"),
		Target->GetVitalSet()->GetArmor() < ArmorBeforeRelease - 0.5f);

	// The latter-half press is buffered through the real StartCurrentWeaponFire
	// path.  A second target position is authored before the second stage starts.
	TickStandardSaberAbilityWorld(World, 1.10f);
	PlaceTargetOnBlade(Target, Attacker, 0, 0.22f);
	const float ArmorBeforeCombo = Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("first standard stage starts through the player input path"),
		Attacker->StartCurrentWeaponFire());
	TickStandardSaberAbilityWorld(World, 0.35f);
	PlaceTargetOnBlade(Target, Attacker, 1, 0.20f);
	TestTrue(TEXT("late second input is accepted by StartCurrentWeaponFire"),
		Attacker->StartCurrentWeaponFire());
	TickStandardSaberAbilityWorld(World, 0.56f);
	TestTrue(TEXT("buffered StartCurrentWeaponFire reaches the second standard stage"),
		Target->GetVitalSet()->GetArmor() < ArmorBeforeCombo - 1.0f);

	// Standard D-stage lunge is integrated by the standard saber timer, not by
	// the legacy LungeTimer.  Cancellation must stop both its future sweep and
	// its recovery tag.
	TickStandardSaberAbilityWorld(World, 1.10f);
	UMSCharacterMovementComponent* Movement = Attacker->GetMSCharacterMovement();
	TestNotNull(TEXT("standard saber input fixture has MS movement"), Movement);
	if (Movement)
	{
		// Damage/contact was covered above.  Keep the lunge cancellation lane
		// clear: the earlier combo target otherwise blocks the standard lunge,
		// leaving the following legacy lunge against the same capsule.
		Target->SetActorLocation(Attacker->GetActorLocation() + FVector(0.0f, 50000.0f, 0.0f));
		Movement->SetBoostInputHeld(true);
		TestTrue(TEXT("standard cancellation fixture can start D-melee"), Movement->IsBoostDashing());
		const FVector DashPositionBefore = Attacker->GetActorLocation();
		TestTrue(TEXT("standard D-stage starts through StartCurrentWeaponFire"),
			Attacker->StartCurrentWeaponFire());
		TestEqual(TEXT("standard D-stage blocks boost regen during its sampled motion"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_DashMeleeRegenBlock), 1);
		TickStandardSaberAbilityWorld(World, 0.20f);
		const FVector DashPositionAtCancel = Attacker->GetActorLocation();
		TestTrue(TEXT("standard D-stage has begun its sampled lunge"),
			!DashPositionAtCancel.Equals(DashPositionBefore, 0.1f));
		Attacker->CancelCurrentWeaponFire();
		TestEqual(TEXT("standard cancel clears State.MeleeRecovery"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 0);
		TestEqual(TEXT("standard D cancellation releases boost regen block"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);
		TickStandardSaberAbilityWorld(World, 0.40f);
		TestTrue(TEXT("standard cancel stops the remaining sampled lunge"),
			Attacker->GetActorLocation().Equals(DashPositionAtCancel, 0.1f));
		Movement->SetBoostInputHeld(false);
	}

	// A non-standard weapon exercises the old timer-backed D-lunge cleanup.
	UMSMeleeWeaponData* LegacyWeapon = NewObject<UMSMeleeWeaponData>(Attacker);
	LegacyWeapon->Range = 3.0f;
	LegacyWeapon->Power = 100;
	LegacyWeapon->DownPoint = 10;
	LegacyWeapon->FireInterval = 0.0f;
	LegacyWeapon->MagazineSize = 0;
	LegacyWeapon->bUseStandardBeamSaberMotion = false;
	Attacker->GetWeaponComponent()->InitializeSlots({LegacyWeapon});
	if (Movement)
	{
		Movement->SetBoostInputHeld(true);
		TestTrue(TEXT("legacy cancellation fixture can start D-melee"), Movement->IsBoostDashing());
	}
	const FVector LegacyPositionBefore = Attacker->GetActorLocation();
	TestTrue(TEXT("legacy D-stage starts through StartCurrentWeaponFire"),
		Attacker->StartCurrentWeaponFire());
	TestEqual(TEXT("legacy D-stage blocks boost regen during lunge and recovery"),
		Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_DashMeleeRegenBlock), 1);
	TickStandardSaberAbilityWorld(World, 0.10f);
	const FVector LegacyPositionAtCancel = Attacker->GetActorLocation();
	TestTrue(TEXT("legacy D-stage has moved before cancellation"),
		!LegacyPositionAtCancel.Equals(LegacyPositionBefore, 0.1f));
	Attacker->CancelCurrentWeaponFire();
	TestEqual(TEXT("legacy cancel clears State.MeleeRecovery"),
		Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 0);
	TestEqual(TEXT("legacy D cancellation releases boost regen block"),
		Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);
	TickStandardSaberAbilityWorld(World, 0.40f);
	TestTrue(TEXT("legacy cancel clears the remaining LungeTimer movement"),
		Attacker->GetActorLocation().Equals(LegacyPositionAtCancel, 0.1f));

	// Let a second legacy D-stage finish naturally.  The fixture has already
	// spent boost on the standard D and the cancelled legacy D above, so the
	// explicit IsBoostDashing assertion documents the remaining-boost precondition
	// instead of silently demoting this case to an ordinary swing.
	if (Movement)
	{
		Movement->SetBoostInputHeld(false);
		Movement->SetBoostInputHeld(true);
		TestTrue(TEXT("legacy natural-completion fixture retains enough boost for D-melee"),
			Movement->IsBoostDashing());
		const FVector NaturalDPositionBefore = Attacker->GetActorLocation();
		TestTrue(TEXT("legacy D-stage starts for natural-completion recovery regression"),
			Attacker->StartCurrentWeaponFire());
		TickStandardSaberAbilityWorld(World, UGA_Melee::DashMeleeLungeDurationSeconds + 0.05f);
		TestTrue(TEXT("legacy natural D-stage completes its lunge"),
			!Attacker->GetActorLocation().Equals(NaturalDPositionBefore, 0.1f));
		TestEqual(TEXT("natural legacy D-stage still owns recovery after lunge ends"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 1);
		TestEqual(TEXT("natural legacy D-stage still blocks boost regen after lunge"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_DashMeleeRegenBlock), 1);

		// Release boost and wait out the D recovery before starting a new ordinary
		// legacy swing.  Cancel must not mistake the completed D's old handle for
		// this new swing's live recovery.
		Movement->SetBoostInputHeld(false);
		TickStandardSaberAbilityWorld(World, UGA_Melee::MeleeRecoverySeconds + 0.05f);
		TestEqual(TEXT("natural legacy D recovery expires before the ordinary swing"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 0);
		TestEqual(TEXT("natural legacy D recovery releases boost regen block"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);
		TestFalse(TEXT("ordinary legacy regression starts with dash input released"),
			Movement->IsBoostDashing());
		TestTrue(TEXT("ordinary legacy swing starts after natural D completion"),
			Attacker->StartCurrentWeaponFire());
		TestEqual(TEXT("ordinary legacy swing enters its own recovery"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 1);
		TestEqual(TEXT("ordinary legacy swing does not inherit the D boost gate"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_DashMeleeRegenBlock), 0);

		Attacker->CancelCurrentWeaponFire();
		TestEqual(TEXT("cancel does not clear ordinary recovery after a completed legacy D"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 1);
		TickStandardSaberAbilityWorld(World, UGA_Melee::MeleeRecoverySeconds - 0.10f);
		TestEqual(TEXT("ordinary recovery remains until its own timer expires"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 1);
		TickStandardSaberAbilityWorld(World, 0.20f);
		TestEqual(TEXT("ordinary recovery eventually clears normally"),
			Attacker->GetAbilitySystemComponent()->GetTagCount(MSTags::State_MeleeRecovery), 0);
	}

	return true;
}

#endif
