#include "RadarSystemWidget.h"
#include "../Orchestrator.h"
#include "../Movement/MyCharacterBase.h"
#include "../Conversion/CubeToSphere.h"
#include "../AI/MazeRunner.h"

#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Rendering/DrawElements.h"

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void URadarSystemWidget::NativeConstruct()
{
	Super::NativeConstruct();
	TryAcquireReferences();
}

void URadarSystemWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	PulseTime += InDeltaTime;
	if (PulseTime > 2.f * PI) PulseTime -= 2.f * PI;

	if (!HasValidRefs())
		TryAcquireReferences();

}

// ─────────────────────────────────────────────────────────────────────────────
// Reference Acquisition
// ─────────────────────────────────────────────────────────────────────────────

void URadarSystemWidget::TryAcquireReferences()
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (!CachedOrchestrator.IsValid())
		CachedOrchestrator = Cast<AOrchestrator>(
			UGameplayStatics::GetActorOfClass(World, AOrchestrator::StaticClass()));

	if (!CachedPlayer.IsValid())
		CachedPlayer = Cast<AMyCharacterBase>(UGameplayStatics::GetPlayerPawn(World, 0));

	if (!CachedSphereActor.IsValid() && CachedOrchestrator.IsValid())
		CachedSphereActor = CachedOrchestrator.Get()->SphereActor;
}

bool URadarSystemWidget::HasValidRefs() const
{
	return CachedOrchestrator.IsValid()
		&& CachedPlayer.IsValid()
		&& CachedSphereActor.IsValid();
}

// ─────────────────────────────────────────────────────────────────────────────
// View Axes
// ─────────────────────────────────────────────────────────────────────────────

bool URadarSystemWidget::BuildViewAxes(FRadarAxes& OutAxes) const
{
	if (!HasValidRefs()) return false;

	AMyCharacterBase* Player = CachedPlayer.Get();
	ACubeToSphere*    Sphere = CachedSphereActor.Get();

	OutAxes.SphereCenter = Sphere->GetActorLocation();

	// Depth = sphere-center → player so the player always projects to radar center (0,0).
	FVector PlayerUnitDir = (Player->GetActorLocation() - OutAxes.SphereCenter).GetSafeNormal();
	if (PlayerUnitDir.IsNearlyZero()) return false;
	OutAxes.Depth = PlayerUnitDir;

	// Derive Right/Up from the camera so the radar orientation matches the screen,
	// then re-orthogonalize against the new Depth axis.
	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!CamMgr) return false;

	const FRotationMatrix CamMatrix(CamMgr->GetCameraRotation());
	FVector CamRight = CamMatrix.GetScaledAxis(EAxis::Y);
	FVector CamUp    = CamMatrix.GetScaledAxis(EAxis::Z);

	// Project CamRight onto the plane perpendicular to PlayerUnitDir
	FVector Right = (CamRight - PlayerUnitDir * FVector::DotProduct(CamRight, PlayerUnitDir)).GetSafeNormal();
	if (Right.IsNearlyZero()) // degenerate: camera looking straight down at player
		Right = (CamUp - PlayerUnitDir * FVector::DotProduct(CamUp, PlayerUnitDir)).GetSafeNormal();
	if (Right.IsNearlyZero()) return false;

	OutAxes.Right = Right;
	OutAxes.Up    = FVector::CrossProduct(OutAxes.Depth, OutAxes.Right);

	// Ensure Up points in the same general direction as camera up
	if (FVector::DotProduct(OutAxes.Up, CamUp) < 0.f)
		OutAxes.Up = -OutAxes.Up;

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Projection
// ─────────────────────────────────────────────────────────────────────────────

FVector2D URadarSystemWidget::ProjectPoint(
	const FVector& UnitPt,
	const FRadarAxes& Axes,
	const FVector2D& Center,
	float& OutDepth) const
{
	float x =  FVector::DotProduct(UnitPt, Axes.Right);
	float y = -FVector::DotProduct(UnitPt, Axes.Up);   // negate: Slate Y grows downward
	OutDepth = FVector::DotProduct(UnitPt, Axes.Depth);
	return Center + FVector2D(x * RadarRadius, y * RadarRadius);
}

// ─────────────────────────────────────────────────────────────────────────────
// NativePaint
// ─────────────────────────────────────────────────────────────────────────────

int32 URadarSystemWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FVector2D Center = AllottedGeometry.GetLocalSize() * 0.5f;
	int32 Layer = LayerId;

	DrawBackground    (OutDrawElements, Layer++, AllottedGeometry, Center);
	DrawDistanceRings (OutDrawElements, Layer++, AllottedGeometry, Center);

	FRadarAxes Axes;
	const bool bHasAxes = BuildViewAxes(Axes);

	if (bHasAxes)
	{
		DrawWireframe     (OutDrawElements, Layer++, AllottedGeometry, Center, Axes);
		DrawFaceIndicators(OutDrawElements, Layer++, AllottedGeometry, Center, Axes);
	}

	DrawOuterRing  (OutDrawElements, Layer++, AllottedGeometry, Center);
	DrawNorthTick  (OutDrawElements, Layer++, AllottedGeometry, Center);

	if (!bHasAxes)
		return Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		                          OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// ── Player dot + directional arrows ────────────────────────────────────
	DrawDot(OutDrawElements, Layer, AllottedGeometry, Center, PlayerDotRadius, PlayerDotColor);
	DrawPlayerArrows(OutDrawElements, Layer + 1, AllottedGeometry, Center, Axes);

	// ── Enemy dots ──────────────────────────────────────────────────────────
	AOrchestrator* Orch = CachedOrchestrator.Get();
	for (AMazeRunner* Runner : Orch->ActiveRunners)
	{
		if (!IsValid(Runner)) continue;

		FVector UnitDir = (Runner->GetActorLocation() - Axes.SphereCenter).GetSafeNormal();
		float Depth;
		FVector2D DotPos = ProjectPoint(UnitDir, Axes, Center, Depth);

		// Choose color by AI state
		FLinearColor DotColor;
		switch (Runner->CurrentState)
		{
			case EAIState::Escaping:
			{
				float T = (FMath::Sin(PulseTime * 4.f) + 1.f) * 0.5f;
				DotColor = FLinearColor::LerpUsingHSV(EnemyNormalColor, EnemyEscapingColor, T);
				break;
			}
			case EAIState::Casting:
				DotColor = EnemyCastingColor;
				break;
			default:
				DotColor = EnemyNormalColor;
				break;
		}

		// Dim dots on the far hemisphere
		if (Depth < 0.f) DotColor.A *= 0.25f;

		DrawDot(OutDrawElements, Layer + 1, AllottedGeometry, DotPos, EnemyDotRadius, DotColor);
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
	                          OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw Helpers
// ─────────────────────────────────────────────────────────────────────────────

void URadarSystemWidget::DrawBackground(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center) const
{
	const int32 N = 48;
	const float SpokeThickness = FMath::Max(2.f, 2.f * PI * RadarRadius / N + 1.f);

	for (int32 i = 0; i < N; i++)
	{
		float Angle = (2.f * PI * i) / N;
		FVector2D Edge = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RadarRadius;
		TArray<FVector2D> Pts = { Center, Edge };
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Pts, ESlateDrawEffect::None, BackgroundColor, false, SpokeThickness);
	}
}

void URadarSystemWidget::DrawWireframe(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const FRadarAxes& Axes) const
{
	// Draws a line strip split into near (full alpha) and far (25% alpha) runs.
	// This gives the "globe wrapping around" look where back-side lines recede.
	auto DrawSegmentedStrip = [&](const TArray<FVector>& UnitPts,
	                               const FLinearColor& BaseColor, float Thickness)
	{
		FLinearColor FarColor = BaseColor;
		FarColor.A *= 0.25f;

		TArray<FVector2D> Run;
		bool bRunIsNear = true;

		auto FlushRun = [&]()
		{
			if (Run.Num() > 1)
				FSlateDrawElement::MakeLines(
					OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(), Run,
					ESlateDrawEffect::None,
					bRunIsNear ? BaseColor : FarColor, true, Thickness);
			Run.Empty();
		};

		bool bFirst = true;
		for (const FVector& UnitPt : UnitPts)
		{
			float Depth;
			FVector2D ScreenPt = ProjectPoint(UnitPt, Axes, Center, Depth);
			bool bNear = Depth >= 0.f;

			if (bFirst)
			{
				bRunIsNear = bNear;
				bFirst = false;
			}
			else if (bNear != bRunIsNear)
			{
				// Hemisphere crossed — include this crossing point in both runs
				Run.Add(ScreenPt);
				FlushRun();
				bRunIsNear = bNear;
				Run.Add(ScreenPt); // start next run from same crossing point
				continue;
			}
			Run.Add(ScreenPt);
		}
		FlushRun();
	};

	// ── Latitude rings ───────────────────────────────────────────────────────
	const float LatStep = 160.f / (NumLatitudeBands + 1); // spans -80° to +80°

	auto MakeLatRing = [&](float LatDeg) -> TArray<FVector>
	{
		float LatRad = FMath::DegreesToRadians(LatDeg);
		float r = FMath::Cos(LatRad);
		float z = FMath::Sin(LatRad);
		TArray<FVector> Pts;
		Pts.Reserve(WireSegments + 1);
		for (int32 s = 0; s <= WireSegments; s++)
		{
			float Angle = (2.f * PI * s) / WireSegments;
			Pts.Add(FVector(r * FMath::Cos(Angle), r * FMath::Sin(Angle), z));
		}
		return Pts;
	};

	// Equator — brighter and thicker
	DrawSegmentedStrip(MakeLatRing(0.f), EquatorColor, EquatorLineThickness);

	// Non-equator latitude bands
	for (int32 i = 1; i <= NumLatitudeBands; i++)
	{
		float Lat = -80.f + LatStep * i;
		if (FMath::Abs(Lat) < 1.f) continue;
		DrawSegmentedStrip(MakeLatRing(Lat), GridLineColor, GridLineThickness);
	}

	// ── Longitude arcs ───────────────────────────────────────────────────────
	for (int32 i = 0; i < NumLongitudeLines; i++)
	{
		float LonRad = (2.f * PI * i) / NumLongitudeLines;
		TArray<FVector> Pts;
		Pts.Reserve(WireSegments + 1);
		for (int32 t = 0; t <= WireSegments; t++)
		{
			float Phi = PI * t / WireSegments; // 0 = north pole, PI = south pole
			Pts.Add(FVector(
				FMath::Sin(Phi) * FMath::Cos(LonRad),
				FMath::Sin(Phi) * FMath::Sin(LonRad),
				FMath::Cos(Phi)
			));
		}
		DrawSegmentedStrip(Pts, GridLineColor, GridLineThickness);
	}
}

void URadarSystemWidget::DrawOuterRing(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center) const
{
	const int32 N = 64;
	TArray<FVector2D> Pts;
	Pts.Reserve(N + 1);
	for (int32 i = 0; i <= N; i++)
	{
		float Angle = (2.f * PI * i) / N;
		Pts.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RadarRadius);
	}
	FSlateDrawElement::MakeLines(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Pts, ESlateDrawEffect::None, EquatorColor, true, EquatorLineThickness);
}

void URadarSystemWidget::DrawDot(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& DotCenter,
	float Radius,
	const FLinearColor& Color) const
{
	const int32 N = 12;
	const float SpokeThickness = FMath::Max(1.f, 2.f * PI * Radius / N + 1.f);
	for (int32 i = 0; i < N; i++)
	{
		float Angle = (2.f * PI * i) / N;
		FVector2D Edge = DotCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		TArray<FVector2D> Pts = { DotCenter, Edge };
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Pts, ESlateDrawEffect::None, Color, true, SpokeThickness);
	}
}

void URadarSystemWidget::DrawPlayerArrows(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const FRadarAxes& Axes) const
{
	AMyCharacterBase* Player = CachedPlayer.Get();
	if (!Player) return;

	// Project player's world forward vector onto the radar 2D plane
	FVector WorldFwd   = Player->GetActorForwardVector();
	FVector WorldRight = Player->GetActorRightVector();

	auto ProjectDir = [&](const FVector& WorldDir) -> FVector2D
	{
		float x =  FVector::DotProduct(WorldDir, Axes.Right);
		float y = -FVector::DotProduct(WorldDir, Axes.Up); // negate: Slate Y down
		return FVector2D(x, y).GetSafeNormal();
	};

	FVector2D FwdDir   = ProjectDir(WorldFwd);
	FVector2D RightDir = ProjectDir(WorldRight);

	// Offset arrows so they start at the edge of the player dot, not the center
	const FVector2D FwdOrigin   = Center + FwdDir   * PlayerDotRadius;
	const FVector2D RightOrigin = Center + RightDir  * PlayerDotRadius;

	DrawArrow(OutDrawElements, LayerId, AllottedGeometry, FwdOrigin,   FwdDir,   ArrowLength, ArrowHeadSize, ForwardArrowColor);
	DrawArrow(OutDrawElements, LayerId, AllottedGeometry, RightOrigin, RightDir, ArrowLength, ArrowHeadSize, RightArrowColor);
}

void URadarSystemWidget::DrawArrow(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Origin,
	const FVector2D& Dir,
	float Length,
	float HeadSize,
	const FLinearColor& Color) const
{
	const FVector2D Tip = Origin + Dir * Length;

	// Shaft
	TArray<FVector2D> Shaft = { Origin, Tip };
	FSlateDrawElement::MakeLines(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Shaft, ESlateDrawEffect::None, Color, true, 1.2f);

	// Arrowhead — two lines diverging back from the tip at ±35°
	const float HeadAngle = FMath::DegreesToRadians(35.f);
	FVector2D Back = -Dir; // direction back along the shaft
	auto Rotate2D = [](const FVector2D& V, float AngleRad) -> FVector2D
	{
		float C = FMath::Cos(AngleRad), S = FMath::Sin(AngleRad);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	};

	FVector2D LeftWing  = Tip + Rotate2D(Back,  HeadAngle) * HeadSize;
	FVector2D RightWing = Tip + Rotate2D(Back, -HeadAngle) * HeadSize;

	TArray<FVector2D> HeadL = { Tip, LeftWing };
	TArray<FVector2D> HeadR = { Tip, RightWing };
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
		HeadL, ESlateDrawEffect::None, Color, true, 1.2f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
		HeadR, ESlateDrawEffect::None, Color, true, 1.2f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Distance Rings
// ─────────────────────────────────────────────────────────────────────────────

void URadarSystemWidget::DrawDistanceRings(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center) const
{
	const int32 N = 48;
	for (float Fraction : { 0.33f, 0.66f })
	{
		const float R = RadarRadius * Fraction;
		TArray<FVector2D> Pts;
		Pts.Reserve(N + 1);
		for (int32 i = 0; i <= N; i++)
		{
			float Angle = (2.f * PI * i) / N;
			Pts.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R);
		}
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Pts, ESlateDrawEffect::None, DistanceRingColor, true, 0.7f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Face Indicators
// ─────────────────────────────────────────────────────────────────────────────

void URadarSystemWidget::DrawFaceIndicators(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center,
	const FRadarAxes& Axes) const
{
	ACubeToSphere* Sphere = CachedSphereActor.Get();
	if (!Sphere) return;

	const FQuat SphereRot = Sphere->GetActorQuat();

	// 12 edges of the unit cube (vertices at ±1). Each edge becomes a great-circle
	// arc on the sphere when its points are normalized — revealing the face boundaries.
	static const FVector EdgePairs[12][2] =
	{
		// Top face ring
		{{ -1,-1, 1 }, {  1,-1, 1 }},
		{{  1,-1, 1 }, {  1, 1, 1 }},
		{{  1, 1, 1 }, { -1, 1, 1 }},
		{{ -1, 1, 1 }, { -1,-1, 1 }},
		// Bottom face ring
		{{ -1,-1,-1 }, {  1,-1,-1 }},
		{{  1,-1,-1 }, {  1, 1,-1 }},
		{{  1, 1,-1 }, { -1, 1,-1 }},
		{{ -1, 1,-1 }, { -1,-1,-1 }},
		// Vertical edges
		{{ -1,-1,-1 }, { -1,-1, 1 }},
		{{  1,-1,-1 }, {  1,-1, 1 }},
		{{  1, 1,-1 }, {  1, 1, 1 }},
		{{ -1, 1,-1 }, { -1, 1, 1 }},
	};

	const FLinearColor FarColor = FLinearColor(FaceEdgeColor.R, FaceEdgeColor.G, FaceEdgeColor.B, FaceEdgeColor.A * 0.25f);
	const int32 EdgeSegments = 16;

	for (const auto& Edge : EdgePairs)
	{
		TArray<FVector2D> NearPts, FarPts;

		for (int32 s = 0; s <= EdgeSegments; s++)
		{
			float t = (float)s / EdgeSegments;
			// Lerp along cube edge then normalize → projects edge onto sphere surface
			FVector CubePt = FMath::Lerp(Edge[0], Edge[1], t);
			FVector UnitPt = SphereRot.RotateVector(CubePt.GetSafeNormal());

			float Depth;
			FVector2D ScreenPt = ProjectPoint(UnitPt, Axes, Center, Depth);

			if (Depth >= 0.f) NearPts.Add(ScreenPt);
			else              FarPts.Add(ScreenPt);
		}

		if (NearPts.Num() > 1)
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(), NearPts,
				ESlateDrawEffect::None, FaceEdgeColor, true, 1.0f);

		if (FarPts.Num() > 1)
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(), FarPts,
				ESlateDrawEffect::None, FarColor, true, 1.0f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// North Tick
// ─────────────────────────────────────────────────────────────────────────────

void URadarSystemWidget::DrawNorthTick(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	const FVector2D& Center) const
{
	// North is always at the top of the radar (sphere Up axis = stable reference)
	const float TickWidth = 6.f;
	const float TickDepth = 9.f;

	const FVector2D Top   = Center + FVector2D(0.f, -RadarRadius);          // on ring edge
	const FVector2D TipIn = Center + FVector2D(0.f, -(RadarRadius - TickDepth)); // inward tip
	const FVector2D BaseL = Top + FVector2D(-TickWidth * 0.5f, 0.f);
	const FVector2D BaseR = Top + FVector2D( TickWidth * 0.5f, 0.f);

	TArray<FVector2D> Triangle = { BaseL, TipIn, BaseR, BaseL };
	FSlateDrawElement::MakeLines(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Triangle, ESlateDrawEffect::None, NorthTickColor, true, 1.5f);
}
