#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AIController.h"
#include "AI/MSBotSpawnManagerComponent.h"
#include "AbilitySystemComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSCombatStatics.h"
#include "Combat/MSVisualProjectile.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/MSGameMode.h"
#include "Gameplay/MSGameState.h"
#include "Gameplay/MSRadarPod.h"
#include "Player/MSPlayerCharacter.h"
#include "Weapons/GA_Fire_Hitscan.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponComponent.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRadarDamagePathsTest,
    "NeoMSGO.Gameplay.Radar.DamagePaths",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSRadarRespawnLimitTest,
    "NeoMSGO.Gameplay.Radar.RespawnLimitAndLifetime",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace MSRadarDamageTest
{
    struct FWorldScope
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSRadarDamageWorld"));
        UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
        FWorldScope()
        {
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.OwningGameInstance = GameInstance;
            Context.SetCurrentWorld(World);
            World->SetGameInstance(GameInstance);
            GameInstance->Init();
            FURL URL;
            URL.AddOption(*FString::Printf(TEXT("game=%s"), *AMSGameMode::StaticClass()->GetPathName()));
            World->SetGameMode(URL);
            if (AMSGameMode* Mode = World->GetAuthGameMode<AMSGameMode>())
                Mode->GetBotSpawnManager()->SetSpawnBotsEnabled(false);
            World->InitializeActorsForPlay(URL);
            World->BeginPlay();
            if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
        }
        ~FWorldScope()
        {
            GameInstance->Shutdown();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        void Tick(float Seconds)
        {
            while (Seconds > 0.0f)
            {
                const float Step = FMath::Min(Seconds, 0.01f);
                ++GFrameCounter;
                World->Tick(LEVELTICK_All, Step);
                Seconds -= Step;
            }
        }
    };

    AMSPlayerCharacter* SpawnShooter(UWorld* World, const FVector& Position, FGenericTeamId Team)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
            AMSPlayerCharacter::StaticClass(), Position, FRotator::ZeroRotator, Params);
        if (Unit)
        {
            Unit->SetGenericTeamId(Team);
            Unit->GetCharacterMovement()->DisableMovement();
        }
        return Unit;
    }
}

bool FMSRadarDamagePathsTest::RunTest(const FString&)
{
    using namespace MSRadarDamageTest;
    FWorldScope Scope;
    AMSPlayerCharacter* Ally = SpawnShooter(Scope.World, FVector(-20000, 20000, 0), MSTeam::PlayerTeamId());
    AMSPlayerCharacter* Enemy = SpawnShooter(Scope.World, FVector(-20000, -20000, 0), MSTeam::EnemyTeamId());
    AMSGameState* State = Scope.World->GetGameState<AMSGameState>();
    if (!TestNotNull(TEXT("ally"), Ally) || !TestNotNull(TEXT("enemy"), Enemy)
        || !TestNotNull(TEXT("real game state"), State)) return false;
    const float PlayerGauge = State->PlayerGauge;
    const float EnemyGauge = State->EnemyGauge;
    const FVector RayStart(-1500, 0, 0);

    const TCHAR* Paths[] = {TEXT("hitscan"), TEXT("direct projectile"), TEXT("explosive projectile"),
        TEXT("radial explosion"), TEXT("normal melee"), TEXT("standard saber blade")};
    for (int32 Path = 0; Path < UE_ARRAY_COUNT(Paths); ++Path)
    {
        AMSRadarPod* Pod = AMSRadarPod::Spawn(Scope.World, FVector::ZeroVector, Ally,
            MSTeam::PlayerTeamId(), AMSRadarPod::DefaultRangeStat);
        if (!TestNotNull(Paths[Path], Pod)) return false;
        TestTrue(TEXT("new radar is active"), Pod->IsActive());
        TestEqual(TEXT("GAS armor initialized"), Pod->GetVitalSet()->GetArmor(), 300.0f);
        // A secondary target proves the projectile's actual explosion overlap,
        // not merely a direct-hit compatibility callback, carries damage.
        AMSRadarPod* Nearby = Path == 2 ? AMSRadarPod::Spawn(Scope.World, FVector(0, 500, 0),
            Ally, MSTeam::PlayerTeamId(), AMSRadarPod::DefaultRangeStat) : nullptr;
        if (Path == 2 && !TestNotNull(TEXT("off-axis explosion target"), Nearby)) return false;

        for (int32 Hostile = 0; Hostile < 2; ++Hostile)
        {
            AMSPlayerCharacter* Shooter = Hostile ? Enemy : Ally;
            const FString Label = FString::Printf(TEXT("%s %s"), Paths[Path], Hostile ? TEXT("hostile") : TEXT("friendly"));
            const float Before = Pod->GetVitalSet()->GetArmor();
            if (Path <= 2)
            {
                UMSRangedWeaponData* Gun = NewObject<UMSRangedWeaponData>(Shooter);
                Gun->MagazineSize = 1; Gun->FireInterval = 0; Gun->Power = 100; Gun->DownPoint = 0;
                Gun->Range = 20; Gun->RangeWorldScale = 1;
                Shooter->GetWeaponComponent()->InitializeSlots({Gun});
                if (Path == 0)
                {
                    TStrongObjectPtr<UGA_Fire_Hitscan> Fire(NewObject<UGA_Fire_Hitscan>(Shooter));
                    TestTrue(Label + TEXT(" fire executes"), Fire->Fire(Shooter, RayStart, FVector::ForwardVector));
                }
                else
                {
                    Gun->ProjectileSpeed = 20;
                    Gun->SplashRadius = Path == 2 ? 2.0f : 0.0f;
                    Gun->SplashDamage = Path == 2 ? 100 : 0;
                    AMSVisualProjectile* Shot = AMSVisualProjectile::LaunchVisualProjectile(
                        Shooter, RayStart, FVector::ForwardVector, Gun);
                    if (!TestNotNull(Label + TEXT(" projectile"), Shot)) return false;
                    TestTrue(Label + TEXT(" confirmed direct hit"), Shot->GetConfirmedHit().Hit.GetActor() == Pod);
                    TestEqual(Label + TEXT(" no early damage"), Pod->GetVitalSet()->GetArmor(), Before);
                    Scope.Tick(Shot->GetFlightDuration() + 0.1f);
                }
            }
            else if (Path == 3)
                UMSCombatStatics::ApplySplashDamage(Scope.World, FVector(100, 0, 0), 1000, 100, {}, Shooter);
            else
            {
                UMSMeleeWeaponData* Sword = NewObject<UMSMeleeWeaponData>(Shooter);
                Sword->Power = 100; Sword->DownPoint = 0; Sword->Range = 3;
                Sword->MagazineSize = 0; Sword->FireInterval = 0;
                Sword->bUseStandardBeamSaberMotion = Path == 5;
                Shooter->GetWeaponComponent()->InitializeSlots({Sword});
                TStrongObjectPtr<UGA_Melee> Melee(NewObject<UGA_Melee>(Shooter));
                TestTrue(Label + TEXT(" swing executes"), Melee->PerformMelee(Shooter, RayStart, FVector::ForwardVector, false));
                // Sample a blade crossing the target through the production
                // capsule-overlap method; animation asset timing is tested separately.
                if (Path == 5) Melee->ApplyStandardSaberBladeSegment(FVector(-200, 0, 0), FVector(200, 0, 0));
                Scope.Tick(1.5f);
            }
            if (Hostile)
                TestTrue(Label + TEXT(" reduces Armor through GAS"), Pod->GetVitalSet()->GetArmor() < Before);
            else
                TestEqual(Label + TEXT(" FF rejected"), Pod->GetVitalSet()->GetArmor(), Before);
            if (Nearby)
            {
                if (Hostile) TestTrue(TEXT("off-axis target damaged by projectile explosion"), Nearby->GetVitalSet()->GetArmor() < 300);
                else TestEqual(TEXT("off-axis friendly blast rejected"), Nearby->GetVitalSet()->GetArmor(), 300.0f);
            }
        }
        UMSCombatStatics::ApplySplashDamage(Scope.World, FVector(100, 0, 0), 1000, 5000, {}, Enemy);
        TestFalse(TEXT("lethal explosion disables sensing immediately"), Pod->IsActive());
        TestTrue(TEXT("lethal explosion destroys replicated radar"), Pod->IsActorBeingDestroyed());
        TestFalse(TEXT("dead equipment rejects repeated damage"), Pod->ApplyWeaponDamage(Enemy, 100));
        if (Nearby && !Nearby->IsActorBeingDestroyed()) Nearby->Destroy();
        Scope.Tick(0.1f);
    }
    TestEqual(TEXT("radar destruction costs no friendly gauge"), State->PlayerGauge, PlayerGauge);
    TestEqual(TEXT("radar destruction costs no enemy gauge"), State->EnemyGauge, EnemyGauge);
    return true;
}

bool FMSRadarRespawnLimitTest::RunTest(const FString&)
{
    using namespace MSRadarDamageTest;
    FWorldScope Scope;
    AAIController* Controller = Scope.World->SpawnActor<AAIController>();
    AMSPlayerCharacter* OldPawn = SpawnShooter(Scope.World, FVector(-20000, 0, 0), MSTeam::PlayerTeamId());
    AMSPlayerCharacter* NewPawn = SpawnShooter(Scope.World, FVector(-25000, 0, 0), MSTeam::PlayerTeamId());
    if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("old pawn"), OldPawn)
        || !TestNotNull(TEXT("replacement pawn"), NewPawn)) return false;
    Controller->Possess(OldPawn);
    AMSRadarPod* First = nullptr;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        AMSRadarPod* Pod = AMSRadarPod::Spawn(Scope.World, FVector(Index * 500, 0, 0), OldPawn,
            MSTeam::PlayerTeamId(), AMSRadarPod::DefaultRangeStat);
        if (!TestNotNull(TEXT("initial installation"), Pod)) return false;
        if (Index == 0) First = Pod;
        TestTrue(TEXT("configured radar lifetime is 150s"), FMath::IsNearlyEqual(Pod->GetLifeSpan(), 150.0f));
        Scope.Tick(0.02f);
    }
    Controller->UnPossess();
    Controller->Possess(NewPawn);
    OldPawn->Destroy();
    AMSRadarPod* Fourth = AMSRadarPod::Spawn(Scope.World, FVector(1500, 0, 0), NewPawn,
        MSTeam::PlayerTeamId(), AMSRadarPod::DefaultRangeStat);
    if (!TestNotNull(TEXT("replacement pawn installation"), Fourth)) return false;
    TestFalse(TEXT("fourth across respawn retires oldest"), First->IsActive());
    TestTrue(TEXT("weapon ownership remains current pawn"), Fourth->GetOwner() == NewPawn);
    int32 Active = 0;
    for (TActorIterator<AMSRadarPod> It(Scope.World); It; ++It) if (It->IsActive()) ++Active;
    TestEqual(TEXT("same controller retains three-installation limit across respawn"), Active, 3);
    Controller->Destroy();
    NewPawn->Destroy();
    TestTrue(TEXT("owner loss does not prematurely remove equipment"), Fourth->IsActive());
    // Accelerate the already-asserted 150s lifetime through the real timer callback.
    Fourth->SetLifeSpan(0.05f);
    Scope.Tick(0.1f);
    TestTrue(TEXT("orphaned equipment expires"), Fourth->IsActorBeingDestroyed());
    TestFalse(TEXT("expired equipment cannot sense"), Fourth->IsActive());
    return true;
}
#endif
