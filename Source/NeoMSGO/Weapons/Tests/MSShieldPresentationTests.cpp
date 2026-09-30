#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "Core/MSGameplayTags.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMobHandVisuals.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Components/Image.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/MSShieldComponent.h"
#include "Core/MSTeamTypes.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionDesaturation.h"
#endif
#include "Player/MSPlayerCharacter.h"
#include "UI/MSMobileSuitPreviewRenderer.h"
#include "Weapons/MSWeaponVisualComponent.h"
#include "Weapons/MSShieldPresentation.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSShieldShaderValidityTest,
    "NeoMSGO.Weapons.ShieldPresentation.ShaderValidity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMSShieldShaderValidityTest::RunTest(const FString& Parameters)
{
    TArray<UMaterial*> Paints;
    for (const TCHAR* Key : {TEXT("DS1_DefenderShield"), TEXT("LS2_LightShield"), TEXT("FS3_FortressShield")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Meshes/Weapons/WeaponLibraryOpus/Materials/%s/M_MI_%s_Paint.M_MI_%s_Paint"), Key, Key, Key);
        UMaterial* Paint = LoadObject<UMaterial>(nullptr, *Path);
        if (!TestNotNull(*Path, Paint)) continue;
        Paints.Add(Paint);
#if WITH_EDITOR
        int32 RequiredInputNodes = 0;
        for (UMaterialExpression* Node : Paint->GetExpressionCollection().Expressions)
        {
            const FExpressionInput* Required = nullptr;
            if (const UMaterialExpressionClamp* Clamp = Cast<UMaterialExpressionClamp>(Node)) Required = &Clamp->Input;
            else if (const UMaterialExpressionAbs* Abs = Cast<UMaterialExpressionAbs>(Node)) Required = &Abs->Input;
            else if (const UMaterialExpressionDesaturation* Desaturation = Cast<UMaterialExpressionDesaturation>(Node)) Required = &Desaturation->Input;
            if (Required)
            {
                ++RequiredInputNodes;
                TestNotNull(*(Path + TEXT(" required input on ") + Node->GetName()), Required->Expression);
            }
        }
        TestEqual(*(Path + TEXT(" has two clamp, one abs, one desaturation")), RequiredInputNodes, 4);
#endif
    }
    if (!FApp::CanEverRender() || GUsingNullRHI)
    {
        AddInfo(TEXT("NullRHI: checked graph connectivity only; shader rendering validity remains unverified."));
        return true;
    }
    // Parameters can exist even when shader compilation failed and the default material
    // is displayed. Finish real shader jobs and inspect each resource, never the fallback.
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    for (UMaterial* Paint : Paints)
    {
        const FMaterialResource* Resource = Paint->GetMaterialResource(GMaxRHIShaderPlatform);
        if (!TestNotNull(*(Paint->GetName() + TEXT(" shader resource")), Resource)) continue;
#if WITH_EDITOR
        // UE 5.7 declares compilation status and error diagnostics only for editor builds.
        // This EditorContext test still checks both when it can actually run.
        TestTrue(*(Paint->GetName() + TEXT(" shader compilation completed")), Resource->IsCompilationFinished());
        TestTrue(*(Paint->GetName() + TEXT(" has no shader errors: ") + FString::Join(Resource->GetCompileErrors(), TEXT("; "))),
            Resource->GetCompileErrors().IsEmpty());
#endif
        const FMaterialShaderMap* ShaderMap = Resource->GetGameThreadShaderMap();
        TestTrue(*(Paint->GetName() + TEXT(" own shader map is valid for rendering")),
            ShaderMap && ShaderMap->IsValidForRendering());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSShieldPresentationParityTest,
    "NeoMSGO.Weapons.ShieldPresentation.HangarAndGuard",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSShieldPresentationParityTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShieldPresentationWorld"));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
    UMSMobileSuitPreviewRenderer* Preview = NewObject<UMSMobileSuitPreviewRenderer>();
    Preview->Initialize(World, NewObject<UImage>());
    for (const TCHAR* Name : {TEXT("StrikerMk1"), TEXT("GuardianDefender"), TEXT("BreakerAssault"),
        TEXT("VanguardRX"), TEXT("DreadnoughtHeavy")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Data/MobileSuits/DA_MS_%s.DA_MS_%s"), Name, Name);
        UMSMobileSuitData* Suit = LoadObject<UMSMobileSuitData>(nullptr, *Path);
        if (!TestNotNull(Name, Suit)) continue;
        Preview->SetMobileSuit(Suit, true);
        UStaticMeshComponent* HangarShield = nullptr;
        TArray<UStaticMeshComponent*> Components;
        Preview->GetPreviewActor()->GetComponents(Components);
        for (UStaticMeshComponent* Component : Components)
            if (Component->GetFName() == TEXT("PreviewNativeShield")) HangarShield = Component;
        if (!TestNotNull(TEXT("authored shield component exists"), HangarShield)) continue;
        TestTrue(TEXT("hangar uses equipped data mesh, never the old cubes"),
            HangarShield->GetStaticMesh() == Suit->GetNativeShield()->WeaponMesh.LoadSynchronous());
        TestTrue(TEXT("hangar follows the visible forearm bone"),
            HangarShield->GetAttachSocketName() == TEXT("forearm_l") && Preview->IsNativeShieldVisible());
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),
            FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        Unit->InitializeUnit(Suit);
        UMSMechRigComponent* Rig = Unit->GetMechRigComponent();
        UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent();
        UMSWeaponVisualComponent* Visual = Unit->GetWeaponVisualComponent();
        UStaticMeshComponent* Shield = Visual->GetImportedShieldMeshComponent();
        TestTrue(TEXT("hangar and combat share authored mesh and size"), Shield->GetStaticMesh() == HangarShield->GetStaticMesh()
            && Shield->GetComponentScale().Equals(HangarShield->GetComponentScale(), 0.001f));
        FMSProceduralPoseInputs Input;
        Input.bIsGuarding = true;
        for (int32 Frame = 0; Frame < 40; ++Frame)
        {
            Animator->EvaluatePose(Input, 0.05f);
            Unit->GetMeshVisualComponent()->TickComponent(0.05f, LEVELTICK_All, nullptr);
            Visual->TickComponent(0.05f, LEVELTICK_All, nullptr);
        }
        const USceneComponent* Upper = Rig->GetJoint(TEXT("UpperArm_L"));
        const USceneComponent* Fore = Rig->GetJoint(TEXT("Forearm_L"));
        TestTrue(TEXT("guard elbow bends rather than extending straight forward"),
            FVector::DotProduct(Upper->GetUpVector(), Fore->GetUpVector()) < 0.5f);
        TestTrue(TEXT("guard plate is upright"), FVector::DotProduct(Shield->GetForwardVector(), FVector::UpVector) > 0.98f);
        TestTrue(TEXT("guard broad face covers the forward direction"),
            FVector::DotProduct(Shield->GetUpVector(), Unit->GetActorForwardVector()) > 0.98f);
        TestTrue(TEXT("guard shield remains attached to the arm"),
            Shield->GetAttachParent() == Unit->GetMeshVisualComponent()->GetPoseableMesh()
            && Shield->GetAttachSocketName() == TEXT("forearm_l"));
        auto FindShieldChild = [&](const FName ChildName) -> UStaticMeshComponent*
        {
            for (USceneComponent* Child : Shield->GetAttachChildren())
                if (Child->GetFName() == ChildName) return Cast<UStaticMeshComponent>(Child);
            return nullptr;
        };
        auto CheckConnectedBrace = [&]()
        {
            UStaticMeshComponent* Cuff = FindShieldChild(TEXT("NativeShieldGuardCuff"));
            if (!TestNotNull(TEXT("shield has a physical forearm cuff"), Cuff)) return;
            FLinearColor CuffColor;
            TestTrue(TEXT("support uses a working Color material and the authored dark metal tint"),
                Cuff->GetMaterial(0) && Cuff->GetMaterial(0)->GetVectorParameterValue(
                    FHashedMaterialParameterInfo(TEXT("Color")), CuffColor)
                && CuffColor.Equals(FLinearColor(.045f,.055f,.060f), .001f));
            const FVector ArmCenter = Unit->GetMeshVisualComponent()->GetPoseableMesh()->GetBoneLocation(
                TEXT("forearm_l"), EBoneSpaces::WorldSpace) + Fore->GetComponentTransform().TransformVector(FVector(0,0,-16));
            TestTrue(TEXT("cuff remains on the actual forearm instead of floating with the shield"),
                Cuff->GetComponentLocation().Equals(ArmCenter, 0.5f) && Cuff->IsVisible());
            for (const TCHAR* RailName : {TEXT("NativeShieldGuardRailLower"), TEXT("NativeShieldGuardRailUpper")})
            {
                UStaticMeshComponent* Rail = FindShieldChild(RailName);
                if (!TestNotNull(RailName, Rail)) continue;
                TestTrue(TEXT("guard connecting rail remains visible"), Rail->IsVisible());
                const FVector A = Rail->GetComponentTransform().TransformPosition(FVector(-50,0,0));
                const FVector B = Rail->GetComponentTransform().TransformPosition(FVector(50,0,0));
                TestTrue(TEXT("guard rail rear endpoint enters the forearm cuff"),
                    Cuff->GetStaticMesh()->GetBoundingBox().ExpandBy(0.5f).IsInsideOrOn(
                        Cuff->GetComponentTransform().InverseTransformPosition(A)));
                TestTrue(TEXT("guard rail forward endpoint reaches the shield mount"),
                    FVector::Dist(B, Shield->GetComponentLocation()) < Fore->GetComponentScale().GetAbsMax() * 4.1f);
            }
        };
        auto CheckHandBehindShield = [&]()
        {
            const UMSMobHandVisuals* Hands = Unit->GetMeshVisualComponent()->GetMobHandVisuals();
            if (!TestNotNull(TEXT("guard still has visible physical hands"), Hands)) return;
            const UStaticMeshComponent* Hand = Hands->GetHandComponentForAutomationTests(true, Hands->GetCurrentPose().Left);
            if (!TestNotNull(TEXT("guard left hand exists"), Hand)) return;
            TestTrue(TEXT("shield clearance does not hide the left hand"), Hand->IsVisible());
            if (!TestNotNull(TEXT("left hand has authored geometry"), Hand->GetStaticMesh().Get())) return;
            const FBox HandBounds = Hand->GetStaticMesh()->GetBoundingBox();
            double MostForward = -TNumericLimits<double>::Max();
            for (int32 Corner = 0; Corner < 8; ++Corner)
            {
                const FVector Local((Corner & 1) ? HandBounds.Max.X : HandBounds.Min.X,
                    (Corner & 2) ? HandBounds.Max.Y : HandBounds.Min.Y,
                    (Corner & 4) ? HandBounds.Max.Z : HandBounds.Min.Z);
                const FVector WorldPoint = Hand->GetComponentTransform().TransformPosition(Local);
                MostForward = FMath::Max(MostForward,
                    Shield->GetComponentTransform().InverseTransformPosition(WorldPoint).Z);
            }
            TestTrue(FString::Printf(TEXT("all left fingers stay behind shield mount/back plane (%.2f cm)"), MostForward),
                MostForward < 0.0);
        };
        CheckHandBehindShield();
        CheckConnectedBrace();
        // Nonzero actor and upper-body headings catch yaw sign/double-composition bugs
        // that an actor-yaw-zero / aim-yaw-zero guard cannot expose.
        Input.bHasDirectionalFacing = true;
        for (const float ActorYaw : {0.0f, 35.0f, -125.0f})
        {
            Unit->SetActorRotation(FRotator(0, ActorYaw, 0));
            for (const float AimYaw : {-60.0f, 60.0f})
            {
                Input.AimYaw = AimYaw;
                for (int32 Frame = 0; Frame < 40; ++Frame)
                {
                    Animator->EvaluatePose(Input, 0.05f);
                    Unit->GetMeshVisualComponent()->TickComponent(0.05f, LEVELTICK_All, nullptr);
                    Visual->TickComponent(0.05f, LEVELTICK_All, nullptr);
                }
                const FVector ChestForward = Rig->GetJoint(TEXT("Torso"))->GetForwardVector().GetSafeNormal2D();
                TestTrue(TEXT("turning guard faces the actual torso, including actor yaw once"),
                    FVector::DotProduct(Shield->GetUpVector(), ChestForward) > 0.98f);
                TestTrue(TEXT("turning guard stays upright"),
                    FVector::DotProduct(Shield->GetForwardVector(), FVector::UpVector) > 0.98f);
                CheckHandBehindShield();
                CheckConnectedBrace();
                // Real PIE exposed solver/displayed-yaw disagreement. The attachment
                // must prefer the rendered torso even if its optional scalar is stale.
                MSShieldPresentation::Attach(Shield, Rig->GetJoint(TEXT("Forearm_L")),
                    Unit->GetMeshVisualComponent()->GetPoseableMesh(), Rig->GetRigRoot()->GetComponentTransform(),
                    Suit->GetNativeShield()->WeaponMeshTransform, 1.0f, -AimYaw);
                TestTrue(TEXT("stale opposite solver yaw cannot turn guard away from the visible torso"),
                    FVector::DotProduct(Shield->GetUpVector(), ChestForward) > 0.98f);
                CheckHandBehindShield();
                CheckConnectedBrace();
            }
        }
        Preview->SetMobileSuit(Suit, false);
        TestFalse(TEXT("hangar deck-off hides shield"), Preview->IsNativeShieldVisible());
        Preview->SetMobileSuit(Suit, true);
        TestTrue(TEXT("hangar deck-on restores authored shield"), Preview->IsNativeShieldVisible());
        Unit->SetGenericTeamId(MSTeam::EnemyTeamId());
        for (int32 Tier = 0; Tier < 3; ++Tier)
        {
            Rig->ApplyAppearance(MSTeam::TeamEnemy, true, Tier);
            Unit->GetMeshVisualComponent()->TickComponent(0.05f, LEVELTICK_All, nullptr);
            Visual->TickComponent(0.05f, LEVELTICK_All, nullptr);
            const FLinearColor Color = Rig->GetPartColor(TEXT("Torso"));
            // Compare the requested painted sRGB palette, not an arbitrary linear-space
            // cutoff that incorrectly rejects the muted red tier's red channel.
            const FColor ExpectedPaint[] = {FColor(110,139,100),FColor(63,110,168),FColor(180,104,98)};
            TestTrue(TEXT("enemy paint uses muted Zaku / Gouf / Char-inspired tier colors"),
                Color.Equals(FLinearColor(ExpectedPaint[Tier]),0.001f));
            TestTrue(TEXT("enemy tier keeps green / blue / red identity"),
                Tier == 0 ? Color.G > Color.R && Color.G > Color.B :
                Tier == 1 ? Color.B > Color.R && Color.B > Color.G : Color.R > Color.G && Color.R > Color.B);
            bool bFoundTint = false;
            for (int32 Slot = 0; Slot < Shield->GetNumMaterials(); ++Slot)
            {
                UMaterialInterface* Material = Shield->GetMaterial(Slot);
                float Tint = 0;
                FLinearColor ShieldColor;
                if (Material && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("MSShieldTeamTint")), Tint))
                {
                    bFoundTint = true;
                    Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("MSTeamPrimary")), ShieldColor);
                    TestTrue(TEXT("enemy shield paint matches body, preserving its texture"), Tint == 1.0f && ShieldColor.Equals(Color, 0.001f));
                }
            }
            TestTrue(TEXT("production shield paint supports team tint"), bFoundTint);
        }
        Unit->SetGenericTeamId(MSTeam::PlayerTeamId());
        Visual->TickComponent(0.05f, LEVELTICK_All, nullptr);
        for (int32 Slot = 0; Slot < Shield->GetNumMaterials(); ++Slot)
        {
            float Tint = 0;
            if (UMaterialInterface* Material = Shield->GetMaterial(Slot); Material &&
                Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("MSShieldTeamTint")), Tint))
                {
                FLinearColor Color;
                Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("MSTeamPrimary")), Color);
                TestTrue(TEXT("allied shield paint matches the suit role color"), Tint == 1.0f &&
                    Color.Equals(UMSMeshVisualComponent::GetRoleAccentColor(Suit), 0.001f));
            }
        }
        UMSShieldComponent* Native = Unit->GetShieldComponent();
        float ArmorDamage = 0, ShieldDamage = 0;
        bool bGuarded = false;
        const float MaxDurability = Native->GetMaxDurability();
        TestTrue(TEXT("real frontal damage breaks the equipped shield"), Native->TryAbsorbDamage(
            MaxDurability + 100.0f, false, Unit->GetActorLocation() + Unit->GetActorForwardVector() * 1000,
            ArmorDamage, ShieldDamage, bGuarded) && Native->IsShieldBroken());
        TestTrue(TEXT("broken shield keeps damaged geometry, not the intact plate"),
            Visual->IsBrokenShieldVisible() && Visual->IsShieldVisible() && !Shield->IsVisible());
        TestFalse(TEXT("broken remnant cannot provide guard"), Native->CanProvideGuard());
        // Simulate the observer receiving broken state before its Guarding-tag update.
        // GatherInputs must still release the left arm using the authoritative broken flag.
        Unit->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Guarding, 1);
        for (int32 Frame = 0; Frame < 40; ++Frame)
        {
            Animator->TickComponent(0.05f, LEVELTICK_All, nullptr);
            Unit->GetMeshVisualComponent()->TickComponent(0.05f, LEVELTICK_All, nullptr);
            Visual->TickComponent(0.05f, LEVELTICK_All, nullptr);
        }
        TestFalse(TEXT("stale replicated guard tag cannot hold a broken shield up"), Animator->GetPoseInputs().bIsGuarding);
        TestTrue(TEXT("broken shield releases guard blend and lowers the upper arm"),
            Animator->GetPoseState().GuardPoseAlpha < 0.01f &&
            FVector::DotProduct(Upper->GetUpVector(), Unit->GetActorUpVector()) > 0.98f);
        int32 RemnantCount = 0;
        for (USceneComponent* Child : Shield->GetAttachChildren())
        {
            if (Child->GetName().StartsWith(TEXT("NativeShieldBroken")) && Child->IsVisible())
            {
                ++RemnantCount;
                const UStaticMeshComponent* Piece = Cast<UStaticMeshComponent>(Child);
                FLinearColor PieceColor;
                TestTrue(TEXT("broken pieces use Color-capable material rather than white default cubes"),
                    Piece && Piece->GetMaterial(0) && Piece->GetMaterial(0)->GetVectorParameterValue(
                        FHashedMaterialParameterInfo(TEXT("Color")), PieceColor)
                    && FMath::Max3(PieceColor.R, PieceColor.G, PieceColor.B) < .65f);
            }
            if (Child->GetName().StartsWith(TEXT("NativeShieldGuard")))
                TestFalse(TEXT("raised guard support does not remain after break"), Child->IsVisible());
        }
        TestEqual(TEXT("damage leaves fractured plates and exposed ribs"), RemnantCount, 6);
        Unit->GetAbilitySystemComponent()->SetLooseGameplayTagCount(MSTags::State_Guarding, 0);
        Native->Repair(MaxDurability * 0.25f);
        TestTrue(TEXT("partial repair preserves the damaged remnant and no defense"),
            Native->IsShieldBroken() && Visual->IsBrokenShieldVisible() && !Native->CanProvideGuard());
        Native->RepairToFull();
        TestTrue(TEXT("full repair restores original shield mesh and durability"),
            !Native->IsShieldBroken() && Shield->IsVisible() && !Visual->IsBrokenShieldVisible() &&
            Native->GetCurrentDurability() == MaxDurability && Native->CanProvideGuard());
        for (USceneComponent* Child : Shield->GetAttachChildren())
            if (Child->GetName().StartsWith(TEXT("NativeShieldBroken")))
                TestFalse(TEXT("repair leaves no visible wreckage behind the intact shield"), Child->IsVisible());
        Native->TryAbsorbDamage(MaxDurability + 100.0f, false,
            Unit->GetActorLocation() + Unit->GetActorForwardVector() * 1000, ArmorDamage, ShieldDamage, bGuarded);
        Native->SetDeckShieldEnabled(false);
        Visual->RefreshFromWeaponComponent();
        TestFalse(TEXT("removing broken shield hides intact and broken graphics"), Visual->IsShieldVisible());
        for (USceneComponent* Child : Shield->GetAttachChildren())
            TestFalse(TEXT("removing shield leaves no floating support or wreckage"), Child->IsVisible());
        Unit->Destroy();
    }
    Preview->Shutdown();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
