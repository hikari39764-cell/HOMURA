$ErrorActionPreference = 'Stop'

# 根の回転と子の移動を確認するための、外部Textureを使うglTFを生成する
$binaryPath = Join-Path $PSScriptRoot 'plane.bin'
$stream = [System.IO.File]::Open($binaryPath, [System.IO.FileMode]::Create)
$writer = New-Object System.IO.BinaryWriter($stream)
try {
    # XZ平面。根ノードでX軸に90度回転すると、カメラに向いたXY平面になる
    $positions = @(-1, 0, 1, 1, 0, 1, -1, 0, -1, 1, 0, -1)
    $normals = @(0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0)
    $texcoords = @(0, 1, 1, 1, 0, 0, 1, 0)
    foreach ($value in ($positions + $normals + $texcoords)) { $writer.Write([single]$value) }
    foreach ($index in @(1, 2, 0, 1, 3, 2)) { $writer.Write([uint16]$index) }
}
finally {
    $writer.Dispose()
}

$gltf = @'
{
  "asset": { "version": "2.0", "generator": "HOMURA CG3 plane fixture" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [
    { "name": "PlaneRoot", "rotation": [0.7071067811865476, 0, 0, 0.7071067811865476], "children": [1] },
    { "name": "PlaneMesh", "translation": [0.25, 0, 0], "mesh": 0 }
  ],
  "meshes": [{
    "name": "Plane",
    "primitives": [{ "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "indices": 3, "material": 0 }]
  }],
  "materials": [{
    "name": "UvChecker",
    "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 }, "metallicFactor": 0, "roughnessFactor": 1 }
  }],
  "textures": [{ "source": 0 }],
  "images": [{ "uri": "../uvChecker.png" }],
  "buffers": [{ "uri": "plane.bin", "byteLength": 140 }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962 },
    { "buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34962 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 32, "target": 34962 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 12, "target": 34963 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, 0, -1], "max": [1, 0, 1] },
    { "bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
}
'@
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'plane.gltf'), $gltf + "`n", [System.Text.UTF8Encoding]::new($false))
