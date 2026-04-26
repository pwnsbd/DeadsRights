#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RadarSystemWidget.generated.h"

class AOrchestrator;
class AMyCharacterBase;
class ACubeToSphere;

UCLASS()
class GAME_API URadarSystemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	// ── Layout ────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Layout")
	float RadarRadius = 70.f;

	// ── Wireframe ─────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Wireframe")
	int32 NumLatitudeBands = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Wireframe")
	int32 NumLongitudeLines = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Wireframe")
	int32 WireSegments = 32;

	// ── Style ─────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Style")
	float GridLineThickness = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Style")
	float EquatorLineThickness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Style")
	float PlayerDotRadius = 4.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Style")
	float EnemyDotRadius = 3.5f;

	// Length of the forward/right arrows extending from the player dot
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Style")
	float ArrowLength = 14.f;

	// Size of the arrowhead lines
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Style")
	float ArrowHeadSize = 4.f;

	// ── Colors ────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor GridLineColor = FLinearColor(0.9f, 0.65f, 0.15f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor EquatorColor = FLinearColor(1.0f, 0.8f, 0.2f, 0.85f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor BackgroundColor = FLinearColor(0.04f, 0.02f, 0.0f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor PlayerDotColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	// Forward arrow — red
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor ForwardArrowColor = FLinearColor(1.f, 0.1f, 0.1f, 1.f);

	// Right arrow — green
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor RightArrowColor = FLinearColor(0.1f, 0.9f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor EnemyNormalColor = FLinearColor(1.f, 0.95f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor EnemyEscapingColor = FLinearColor(1.f, 0.05f, 0.0f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor EnemyCastingColor = FLinearColor(1.f, 0.5f, 0.0f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Style")
	float ArtifactDotRadius = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor BeamRadarColor = FLinearColor(1.f, 0.1f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor PhaseWalkRadarColor = FLinearColor(0.1f, 0.9f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor PathFinderRadarColor = FLinearColor(1.f, 0.9f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor AoEBombRadarColor = FLinearColor(1.f, 0.5f, 0.0f, 1.f);

	// Faint rings at 33% and 66% RadarRadius showing distance zones
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor DistanceRingColor = FLinearColor(0.9f, 0.65f, 0.15f, 0.18f);

	// Cube face boundary arcs — cyan-blue to distinguish from amber lat/lon lines
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor FaceEdgeColor = FLinearColor(0.5f, 0.85f, 1.0f, 0.55f);

	// North tick triangle on the outer ring
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radar|Colors")
	FLinearColor NorthTickColor = FLinearColor(1.0f, 0.8f, 0.2f, 1.0f);

private:
	// Three orthonormal axes that define the radar "camera" each frame.
	// Built once per NativePaint from player position/facing, passed to all draw helpers.
	struct FRadarAxes
	{
		FVector Right;
		FVector Up;
		FVector Depth;       // points from sphere center toward player
		FVector SphereCenter;
	};

	UPROPERTY() TWeakObjectPtr<AOrchestrator>    CachedOrchestrator;
	UPROPERTY() TWeakObjectPtr<AMyCharacterBase> CachedPlayer;
	UPROPERTY() TWeakObjectPtr<ACubeToSphere>    CachedSphereActor;

	// Pulse accumulator for the Escaping enemy blink effect (updated in NativeTick)
	float PulseTime = 0.f;

	void TryAcquireReferences();
	bool HasValidRefs() const;

	// Builds the view axes from the player's current world position and facing.
	// Returns false if references are invalid or degenerate.
	bool BuildViewAxes(FRadarAxes& OutAxes) const;

	// Projects a unit-sphere point to radar 2D space.
	// OutDepth >= 0 means near hemisphere (fully visible).
	FVector2D ProjectPoint(const FVector& UnitPt, const FRadarAxes& Axes,
	                       const FVector2D& Center, float& OutDepth) const;

	// Draw helpers — each layer added on top of the previous
	void DrawBackground      (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D&) const;
	void DrawDistanceRings   (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D&) const;
	void DrawWireframe       (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D&, const FRadarAxes&) const;
	void DrawFaceIndicators  (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D&, const FRadarAxes&) const;
	void DrawOuterRing       (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D&) const;
	void DrawNorthTick       (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D&) const;
	void DrawDot             (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D&, float Radius, const FLinearColor&) const;
	void DrawPlayerArrows    (FSlateWindowElementList&, int32, const FGeometry&, const FVector2D& Center, const FRadarAxes&) const;

	// Draws a single arrow: shaft from Origin in Dir, with a small arrowhead at the tip
	void DrawArrow(FSlateWindowElementList&, int32, const FGeometry&,
	               const FVector2D& Origin, const FVector2D& Dir,
	               float Length, float HeadSize, const FLinearColor&) const;
};
