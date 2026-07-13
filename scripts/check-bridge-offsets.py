from pathlib import Path
import re

sdk = Path(r"C:\Users\lance\Desktop\chameleonEsp-upstream\peterhack\SDK")


def find_in_class(text: str, class_marker: str, field_name: str):
    idx = text.find(class_marker)
    if idx < 0:
        return None
    chunk = text[idx : idx + 200000]
    m = re.search(rf"\b{re.escape(field_name)}\b.*?//\s*(0x[0-9A-Fa-f]+)", chunk)
    return m.group(1) if m else None


engine = (sdk / "Engine_classes.hpp").read_text(encoding="utf-8", errors="replace")
leon = (sdk / "BP_FirstPersonCharacter_cLeon_Character_classes.hpp").read_text(
    encoding="utf-8", errors="replace"
)
ph = (sdk / "PenguinHotel_classes.hpp").read_text(encoding="utf-8", errors="replace")

bridge = {
    "UWorld_OwningGameInstance": 0x0228,
    "UGameInstance_LocalPlayers": 0x0038,
    "UPlayer_PlayerController": 0x0030,
    "Controller_ControlRotation": 0x0320,
    "PlayerController_PlayerCameraManager": 0x0360,
    "BP_FirstPersonCharacter_RuntimePaintable": 0x0B68,
    "RuntimePaintable_CurrentBrushSettings": 0x0170,
    "SceneCapture2D_CaptureComponent2D": 0x02B8,
    "SceneCaptureComponent_CaptureSource": 0x0241,
    "SceneCaptureComponent_CaptureFlags": 0x0242,
    "SceneCaptureComponent_bAlwaysPersistRenderingState": 0x0243,
    "SceneCaptureComponent2D_ProjectionType": 0x0328,
    "SceneCaptureComponent2D_FOVAngle": 0x032C,
    "SceneCaptureComponent2D_TextureTarget": 0x0350,
}

lookups = [
    ("UWorld_OwningGameInstance", engine, "class UWorld ", "OwningGameInstance"),
    ("UGameInstance_LocalPlayers", engine, "class UGameInstance ", "LocalPlayers"),
    ("UPlayer_PlayerController", engine, "class UPlayer ", "PlayerController"),
    ("Controller_ControlRotation", engine, "class AController ", "ControlRotation"),
    (
        "PlayerController_PlayerCameraManager",
        engine,
        "class APlayerController ",
        "PlayerCameraManager",
    ),
    (
        "BP_FirstPersonCharacter_RuntimePaintable",
        leon,
        "ABP_FirstPersonCharacter_cLeon_Character_C",
        "RuntimePaintable",
    ),
    (
        "RuntimePaintable_CurrentBrushSettings",
        ph,
        "class URuntimePaintableComponent",
        "CurrentBrushSettings",
    ),
    (
        "SceneCapture2D_CaptureComponent2D",
        engine,
        "class ASceneCapture2D ",
        "CaptureComponent2D",
    ),
    (
        "SceneCaptureComponent_CaptureSource",
        engine,
        "class SDK_ALIGN(0x10) USceneCaptureComponent ",
        "CaptureSource",
    ),
    # Bridge writes the bitfield byte at 0x0242 (named CaptureFlags in sdk.hpp).
    (
        "SceneCaptureComponent_CaptureFlags",
        engine,
        "class SDK_ALIGN(0x10) USceneCaptureComponent ",
        "bCaptureEveryFrame",
    ),
    (
        "SceneCaptureComponent_bAlwaysPersistRenderingState",
        engine,
        "class SDK_ALIGN(0x10) USceneCaptureComponent ",
        "bAlwaysPersistRenderingState",
    ),
    (
        "SceneCaptureComponent2D_ProjectionType",
        engine,
        "class USceneCaptureComponent2D ",
        "ProjectionType",
    ),
    (
        "SceneCaptureComponent2D_FOVAngle",
        engine,
        "class USceneCaptureComponent2D ",
        "FOVAngle",
    ),
    (
        "SceneCaptureComponent2D_TextureTarget",
        engine,
        "class USceneCaptureComponent2D ",
        "TextureTarget",
    ),
]

print(f"{'field':55} {'bridge':>10} {'dump':>10} status")
print("-" * 90)
mismatches = []
for name, text, marker, field in lookups:
    dump_off = find_in_class(text, marker, field)
    bridge_off = bridge[name]
    if dump_off is None:
        print(f"{name:55} {bridge_off:#010x} {'MISSING':>10} FAIL")
        mismatches.append(name)
        continue
    dval = int(dump_off, 16)
    ok = bridge_off == dval
    status = "OK" if ok else "MISMATCH"
    if not ok:
        mismatches.append(name)
    print(f"{name:55} {bridge_off:#010x} {dval:#010x} {status}")

print()
print("mismatches:", mismatches or "none")
print("ProcessEventVtableIndex bridge=0x4C dump=0x4C")

# Also compare key param struct sizes used by bridge if present in dump
print("\nKey dump fields for MaxDecoySpawnCount / SpawnedDecoyActors:")
for field in ("MaxDecoySpawnCount", "SpawnedDecoyActors", "CurrentBrushSettings"):
    off = find_in_class(ph, "class URuntimePaintableComponent", field)
    print(f"  {field}: {off}")
