// Copyright NeoMSGO Project. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSMobBodyRigidSkinTest,
    "NeoMSGO.Animation.MobBodyRigidSkin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMSMobBodyRigidSkinTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
        TEXT("/Game/Meshes/MobileSuits/MobLightHands/SK_MobLightHands.SK_MobLightHands"));
    if (!TestNotNull(TEXT("actual MobLight body"), Mesh)) return false;
    FSkeletalMeshModel* Model = Mesh->GetImportedModel();
    if (!TestTrue(TEXT("imported LOD is available"), Model && Model->LODModels.Num() > 0)) return false;
    const FSkeletalMeshLODModel& LOD = Model->LODModels[0];
    int32 NonRigidVertices = 0, MixedTriangles = 0, Vertices = 0, Triangles = 0;
    FString Details;
    for (const FSkelMeshSection& Section : LOD.Sections)
    {
        TArray<int32> Bones;
        for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
        {
            int32 Count = 0, Bone = INDEX_NONE;
            for (int32 Slot = 0; Slot < MAX_TOTAL_INFLUENCES; ++Slot)
            {
                if (Vertex.InfluenceWeights[Slot] > 0)
                {
                    ++Count;
                    Bone = Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Slot])
                        ? Section.BoneMap[Vertex.InfluenceBones[Slot]] : INDEX_NONE;
                }
            }
            if (Count != 1 || Bone == INDEX_NONE) ++NonRigidVertices;
            Bones.Add(Bone);
            ++Vertices;
        }
        for (uint32 Triangle = 0; Triangle < Section.NumTriangles; ++Triangle)
        {
            const uint32 Start = Section.BaseIndex + Triangle * 3;
            const int32 A = LOD.IndexBuffer[Start] - Section.BaseVertexIndex;
            const int32 B = LOD.IndexBuffer[Start + 1] - Section.BaseVertexIndex;
            const int32 C = LOD.IndexBuffer[Start + 2] - Section.BaseVertexIndex;
            if (!Bones.IsValidIndex(A) || !Bones.IsValidIndex(B) || !Bones.IsValidIndex(C))
            {
                AddError(TEXT("triangle index outside its section"));
                continue;
            }
            if (Bones[A] != Bones[B] || Bones[A] != Bones[C])
            {
                ++MixedTriangles;
                if (MixedTriangles <= 30)
                    Details += FString::Printf(TEXT("section %d tri %u vertices %d %d %d bones %d %d %d\n"),
                        Section.MaterialIndex, Triangle, A, B, C, Bones[A], Bones[B], Bones[C]);
            }
            ++Triangles;
        }
    }
    const FString Summary = FString::Printf(TEXT("vertices=%d triangles=%d nonRigidVertices=%d mixedBoneTriangles=%d\n"),
        Vertices, Triangles, NonRigidVertices, MixedTriangles);
    AddInfo(Summary);
    FFileHelper::SaveStringToFile(Summary + Details,
        *(FPaths::ProjectDir() / TEXT("Saved/CodexWeaponFit20260924/BMG/SkinAudit/UERigidSkin.txt")));
    TestEqual(TEXT("every body vertex has one rigid influence"), NonRigidVertices, 0);
    TestEqual(TEXT("separate rigid armour never spans bone frames"), MixedTriangles, 0);
    return true;
}
#endif

