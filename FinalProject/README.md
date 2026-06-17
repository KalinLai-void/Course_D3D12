# Final Project - MMD Toon Renderer
根據[作業需求](Requirement.md)進行實作。

>[!Tip]
> 以 [Assignment 1](./Assignment1%20-%20Deferred%20Renderer/) 專案為基礎做修改與擴充。
> 皆為自己撰寫 (部分有詢問 AI 來學習，但主要由我自己撰寫)，並無套用任何既成之 D3D12 渲染框架。

## Results

本專案以 Deferred Rendering 為基礎，將 Sponza 場景、PMX 人型角色、VMD 骨架動畫、Cel Shading、Directional Shadow Mapping、PCF、SSAO 與 ACES Tone Mapping 整合在同一個 DirectX 12 專案中。

目前主要的 Rendering Pipeline 如下：

```text
Shadow Pass
    ↓
Geometry Pass
    ├─ Albedo G-Buffer
    ├─ Normal G-Buffer
    └─ World Position G-Buffer
    ↓
SSAO Pass
    ↓
Lighting Pass
    ├─ Ambient / Directional / Point Light
    ├─ Cel Shading
    ├─ Shadow Mapping / PCF
    ├─ SSAO Composition
    └─ ACES Tone Mapping
    ↓
HUD / Text Overlay
    ↓
Back Buffer
```

已完成的主要功能：

- PMX 模型、材質、貼圖、骨架與 Alpha-tested 區域讀取。
- VMD 骨架動畫讀取、骨架階層解析與 Linear Blend Skinning。
- 動畫角色的梯度漫反射、輪廓線與 Rim Light。
- Directional Light Shadow Mapping，可動態調整水平及垂直光線方向。
- Shadow Depth Bias、Slope-scaled Bias、Normal Offset Bias 與 Comparison Bias。
- 可切換 Hard Shadow 與 3×3 Percentage-Closer Filtering (PCF)。
- 16 Samples Hemisphere Kernel SSAO、4×4 Procedural Random Rotation 與 5×5 Blur。
- 可調整 SSAO Radius、Bias 與 Intensity。
- ACES Approximation Tone Mapping 與 Exposure 調整。
- Debug Render Modes：Depth、Normal、Albedo、Final Color、SSAO、Shadow。
- 即時 FPS 與操作資訊 HUD，可使用空白鍵開關。

> [!Note]
> 專案預設解析度為 `1280 × 720`。實際 FPS 會依照 GPU、攝影機位置與畫面中可見幾何量有所差異；Demo 時應在 PMX 動畫、Shadow、PCF、SSAO 與 Tone Mapping 全部開啟的情況下確認穩定維持 45 FPS 以上。

### Final Color
- ![]()

### PMX + VMD Skeleton Animation
- ![]()

### Shadow Mapping / PCF
- ![]()

### SSAO
- ![]()

## How to control?

>[!Note] 
> 互動的部分，有盡可能解耦獨立程式碼
> - `Camera.h` 及 `Camera.cpp` 用來處理攝影機相關
> - `InputManager.h` 用來處理輸入相關
> - 這些檔案目前在架構上皆為 Singleton 存在，因為目前不會有複數的情況
> - 也有整合進 `Win32Application.cpp` 中
> - 最後在 `D3D12HelloTexture.cpp` 實作 `HandleInput()` 處理輸入

### 系統開關
- **`Z`**：切換 Depth / Normal / Albedo / Final Color / SSAO / Shadow 狀態
  - ![]()
- **`X`**：開/關顯示 PMX 模型
  - ![]()
- **`空白鍵`**：開/關顯示畫面資訊
  - ![]()
### 鏡頭移動
- **`W` / `A` / `S` / `D`**：第一人稱移動
  - ![]()
- **`Q` / `E`**：Q下移；E上移
  - ![]()
- **`滑鼠`**：第一人稱轉視角
  - 程式一執行後，滑鼠會隱藏進入遊戲狀態，直接轉即可
  - 為了方便實作，目前程式中僅使用 **Euler Angles** 實作旋轉
    - 未來有機會時，應該實作更精準且不會卡死的 **Quaternions** 方法
  - 攝影機的 Front Vector 計算如下，其中 $\psi$ 為 Yaw、$\theta$ 為 Pitch：

$$
\mathbf{F} =
\operatorname{normalize}
\begin{bmatrix}
\cos\psi\cos\theta \\
\sin\theta \\
\sin\psi\cos\theta
\end{bmatrix}
$$

  - ![]()
- **`ESC`**：顯示滑鼠並暫時離開遊戲（此時視角不會轉）
  - 離開遊戲狀態時，可以按 **滑鼠右鍵** 拖曳視角
  - **滑鼠點畫面左鍵** 能夠回到遊戲狀態
  - ![]()
### 光線及陰影設定
> 本專案的陰影主要使用直射光（Directional Light）實作。
- **`J`**：開關直接光源影響場景渲染（關閉時僅有環境光 (Ambient)）
  - ![]()
- **`F`** ：切換 Hard Shadow 1×1 與陰影邊緣模糊效果 3×3 PCF
  - ![]()
- **`H` / `K`**：調整 Directional Light 的水平方向
- **`U` / `M`**：調整 Directional Light 的垂直方向
  - ![]()
### 後處理設定
- **`↑` / `↓`**：調整（+/-） Tone Mapping (使用 ACES 演算法) 的曝光度
  - ![]()
- **`C`**：開關 Screen Space Ambient Occlusion (SSAO) 之效果
  - ![]()
- **`[` / `]`**：調整（+/-）SSAO 的半徑
  - ![]()
- **`;` / `'`**：調整（+/-）SSAO 的 Bias 值
  - ![]()
- **`,` / `.`**：調整（+/-）SSAO 的強度
  - ![]()

## 技術細節
### PMD/PMX + VMD Skeleton Animation + Cel Shading
- `AssimpLoader`：開啟靜態場景（[Sponza 模型](./D3D12DeferredRenderer/D3D12DeferredRenderer/sponza/)）資源之方式，整合 [Open Asset Import Library (Assimp)](https://github.com/assimp/assimp) 來讀取模型與貼圖。
- `PMXLoader`：開啟 MMD 模型資源（二進位檔案(.PMX)）之方式。包括讀取：模型 (Model) 檔案、貼圖 (Texture) 檔案、材質 (Material) 資訊、骨架 (Skeleton) 資訊。
- `VMDLoader`：開啟 MMD 之動作檔案 (.VMD) 之方式。
- `MMDAnimator`：將 VMD 動作檔綁到 PMX 模型檔上並播放。
- `GeometryPass.hlsl` 與 `LightingPass.hlsl`：處理 Cel / Toon Shading 之相關算法（包含梯度漫反射、輪廓線描邊與 Rim light），並成功套在動作上貼圖無破圖。
  - 會記錄模型是否為 PMX 角色，若是才套渲染。
  - 使用 Normal G-Buffer 的 Alpha 通道記錄是否為 PMX 角色。

#### PMX 模型與座標系處理

PMX 原始資料與本專案使用的 DirectX 左手座標系不同，因此讀取 Position、Normal 與 Bone Position 時會反轉 Z 軸：

$$
(x, y, z)_{\mathrm{LH}} = (x, y, -z)_{\mathrm{PMX}}
$$

為了維持三角形正確的正反面方向，Index 順序由：

$$
(i_0, i_1, i_2)
$$

改為：

$$
(i_0, i_2, i_1)
$$

目前 Loader 會處理 BDEF1、BDEF2、BDEF4、SDEF 與 QDEF 對應的 Bone Index / Bone Weight 資料；其中 SDEF 目前以 BDEF2 權重方式近似，尚未使用 SDEF 的額外曲面參數。

模型貼圖使用相對於 PMX 檔案所在資料夾的路徑。Geometry Pass 會依材質的 `opacityMode` 決定是否執行 Alpha Test：

$$
\operatorname{clip}(\alpha - 0.5)
$$

當貼圖 Alpha 小於 `0.5` 時捨棄該 Pixel，用於角色頭髮、睫毛與其他 Cutout 區域。

#### VMD 動畫讀取

VMD 骨架名稱使用 Shift-JIS，因此讀取後使用 Windows Code Page 932 轉換為 `std::wstring`。位置資料轉換為左手座標系時反轉 Z 軸；旋轉四元數則反轉 X 與 Y：

$$
\mathbf{p}_{\mathrm{LH}} =
(p_x, p_y, -p_z)
$$

$$
\mathbf{q}_{\mathrm{LH}} =
(-q_x, -q_y, q_z, q_w)
$$

目前會讀取 Bone Motion，並依骨架名稱分組、依 Frame 編號排序。VMD 原始的 Bezier Interpolation 目前未實作，而是使用線性位置插值與 Quaternion Slerp。

兩個 Keyframe 之間的時間參數為：

$$
t =
\frac{f - f_0}
{f_1 - f_0}
$$

位置使用 Linear Interpolation：

$$
\mathbf{p}(t) =
(1-t)\mathbf{p}_0 +
t\mathbf{p}_1
$$

旋轉使用 Spherical Linear Interpolation：

$$
\mathbf{q}(t) =
\operatorname{Slerp}
(\mathbf{q}_0,\mathbf{q}_1,t)
$$

#### Bone Hierarchy 與 Skinning Matrix

每根 Bone 的 Local Bind Position 為 Bone Position 與 Parent Bone Position 的差：

$$
\mathbf{b}^{local}_i =
\mathbf{b}_i -
\mathbf{b}_{parent(i)}
$$

Bone 的 Local Transform 由 Bind Translation、VMD Position Offset 與 VMD Rotation 組成：

$$
L_i =
T\left(
\mathbf{b}^{local}_i +
\mathbf{p}^{anim}_i
\right)
R\left(
\mathbf{q}^{anim}_i
\right)
$$

本專案使用 Row Vector 的矩陣乘法方式，Global Transform 依骨架階層遞迴計算：

$$
G_i =
L_i G_{parent(i)}
$$

Root Bone 則為：

$$
G_{root} = L_{root}
$$

最終 Skinning Matrix 為：

$$
S_i =
B_i^{-1} G_i
$$

其中 $B_i^{-1}$ 為 Inverse Bind Transform。上傳 GPU 前會進行 Matrix Transpose，以符合 HLSL 中 `mul(rowVector, matrix)` 的使用方式。

#### Linear Blend Skinning

為避免模型中 Bone Weight 未正規化，Vertex Shader 會先計算：

$$
W =
\sum_{i=0}^{3} w_i
$$

$$
\hat{w}_i =
\frac{w_i}{W}
$$

頂點位置與法線分別使用最多四根骨架進行 Linear Blend Skinning：

$$
\mathbf{p}_{skin} =
\sum_{i=0}^{3}
\hat{w}_i
\left(
\mathbf{p}S_i
\right)
$$

$$
\mathbf{n}_{skin} =
\operatorname{normalize}
\left(
\sum_{i=0}^{3}
\hat{w}_i
\left(
\mathbf{n}S_i
\right)
\right)
$$

完成 Skinning 後，再套用 PMX 模型的 Scale、Y 軸 Rotation 與 Translation，使角色能正確放入 Sponza 場景。

#### Gradient Diffuse / Toon Shading

角色的梯度漫反射先以 Half-Lambert 計算：

$$
h =
\operatorname{saturate}
\left(
\frac{\mathbf{N}\cdot\mathbf{L}}{2}
+
\frac{1}{2}
\right)
$$

為避免硬切 Threshold 在模型表面造成明顯的三角面色塊，使用 `smoothstep` 建立較平滑的 Toon Ramp：

$$
r =
\operatorname{smoothstep}
(0.36, 0.58, h)
$$

$$
D_{toon} =
\operatorname{lerp}
(0.18, 0.66, r)
$$

角色仍會投射 Shadow 至 Sponza；為降低薄片衣物、頭髮與裙擺的 Self-shadow Artifact，Shadow Map 對角色本身的接收強度設定為 `0.15`：

$$
V_{character} =
\operatorname{lerp}
\left(
1,
\operatorname{smoothstep}
(0.10, 0.90, V_{shadow}),
0.15
\right)
$$

$$
D_{final} =
D_{toon} V_{character}
$$

#### 輪廓線描邊

輪廓線使用 Screen-space 的 Normal / Position 差異判斷。對目前 Pixel 的上下左右四個鄰居取樣：

$$
\Delta N =
1 -
\mathbf{N}\cdot\mathbf{N}_{neighbor}
$$

$$
\Delta P =
\left\|
\mathbf{P} -
\mathbf{P}_{neighbor}
\right\|
$$

當：

$$
\Delta N > 0.25
\quad \text{或} \quad
\Delta P > 0.4
$$

則將該 Pixel 判定為 Outline，輸出黑色。此方式不需要額外放大模型或繪製 Back-face Outline Pass，但輪廓寬度會受到解析度影響。

#### Rim Light

Rim Light 先使用 View Direction 與 Normal 計算輪廓強度：

$$
R =
1 -
\operatorname{saturate}
(\mathbf{N}\cdot\mathbf{V})
$$

再以四次方集中到模型邊緣，並限制在朝向光源的表面：

$$
R_{intensity} =
R^4
\operatorname{saturate}
(\mathbf{N}\cdot\mathbf{L})
$$

最後使用 `smoothstep` 形成柔和的 Rim Band：

$$
R_{toon} =
0.20
\operatorname{smoothstep}
(0.25, 0.45, R_{intensity})
$$

> 沒有處理 Facial Animation、IK、裙擺/頭髮物理及 Camera Motion。

### Shadow Mapping (using Directional light)

本專案建立一張 `2048 × 2048` 的 Directional Shadow Map。Shadow Pass 僅輸出 Depth，不輸出 Color；Sponza 與 PMX 角色皆會使用與 Geometry Pass 相同的 Model / Skinning Transform 寫入 Shadow Map。

為了避免透明頭髮、葉片或鏈條的透明區域寫入錯誤深度，Shadow Pass 亦使用 Diffuse Texture Alpha Test：

$$
\operatorname{clip}(\alpha - 0.5)
$$

#### Directional Light 方向

Directional Light 使用水平方位角 $\phi$ 與垂直仰角 $\theta$ 建立 Surface-to-light Direction：

$$
\mathbf{L} =
\operatorname{normalize}
\begin{bmatrix}
\cos\phi\cos\theta \\
\sin\theta \\
\sin\phi\cos\theta
\end{bmatrix}
$$

`H / K` 調整 $\phi$，`U / M` 調整 $\theta$。每一幀會重新建立 Light View Matrix 與 Orthographic Projection Matrix：

$$
M_{lightVP} =
V_{light}
P_{light}
$$

因此光線方向改變時，Shadow Map 會同步重新繪製。

#### Shadow Comparison

目前 Pixel 的 World Position 轉換至 Light Clip Space：

$$
\mathbf{p}_{light} =
\mathbf{p}_{world}
M_{lightVP}
$$

Perspective Divide 後轉換至 Shadow Map UV：

$$
\mathbf{u}_{shadow} =
\begin{bmatrix}
0.5x + 0.5 \\
-0.5y + 0.5
\end{bmatrix}
$$

若 Shadow Map 中記錄的深度比目前 Pixel 的 Light-space Depth 更靠近光源，表示目前 Pixel 被遮擋。

#### Shadow Acne 與 Peter Panning

Caster 端的 Rasterizer Bias 設定為：

```cpp
DepthBias = 300;
DepthBiasClamp = 0.0006f;
SlopeScaledDepthBias = 0.75f;
```

Receiver 端則依 Normal 與 Light Direction 的夾角使用 Normal Offset：

$$
g =
1 -
\operatorname{saturate}
(\mathbf{N}\cdot\mathbf{L})
$$

$$
\mathbf{p}' =
\mathbf{p} +
\mathbf{N}
\operatorname{lerp}
(b_{min}, b_{max}, g)
$$

角色使用的 Normal Offset 範圍為：

$$
b_n^{character} \in [0.015, 0.055]
$$

Sponza 使用：

$$
b_n^{scene} \in [0.005, 0.030]
$$

除此之外，Depth Comparison 仍保留較小的 Comparison Bias：

$$
z_{reference} =
z_{current} -
b_c
$$

角色的 $b_c$ 約為 `0.00004 ~ 0.00014`，場景則約為 `0.00006 ~ 0.00025`。Caster Bias、Normal Offset 與 Comparison Bias 共同降低 Shadow Acne，同時避免使用過大的單一 Bias 造成 Peter Panning。

#### Percentage-Closer Filtering (PCF)

可使用 `F` 鍵切換 Hard 1×1 Shadow 與 3×3 PCF。

Hard Shadow 僅進行一次 Depth Comparison：

$$
V =
\operatorname{Compare}
\left(
D(\mathbf{u}),
z_{reference}
\right)
$$

PCF 則在 Shadow Map 周圍取 3×3 共九個 Sample，將每次比較結果平均：

$$
V_{PCF}(\mathbf{u}) =
\frac{1}{9}
\sum_{x=-1}^{1}
\sum_{y=-1}^{1}
\operatorname{Compare}
\left(
D(\mathbf{u} + (x,y)\Delta\mathbf{u}),
z_{reference}
\right)
$$

其中：

$$
\Delta\mathbf{u} =
\left(
\frac{1}{W_{shadow}},
\frac{1}{H_{shadow}}
\right)
$$

3×3 Kernel 能降低 Shadow Edge 的鋸齒，同時避免過大的 Filter Radius 造成明顯的 Light Leaking。

> [!Note]
> Point Light 目前僅作為額外場景照明，未實作 Cubemap Point Shadow；本專案的 Shadow Mapping 評分項目以 Directional Light 為主。

### Screen Space Ambient Occlusion, SSAO

SSAO 使用 Geometry Pass 輸出的 World Position G-Buffer 與 Normal G-Buffer。Shader 中先將 Position 與 Normal 轉換至 View Space，再在表面法線為中心的 Hemisphere 中取樣。

為兼顧效能，SSAO 使用半解析度的 `R8_UNORM` Render Target，Sampling Kernel 數量為 `16`。

#### Hemisphere Sampling Kernel

每個 Sample 初始為：

$$
\mathbf{s}_i =
(x_i, y_i, z_i)
$$

其中：

$$
x_i,y_i \in [-1,1],
\qquad
z_i \in [0,1]
$$

因為 $z_i$ 僅為正值，所以 Sample 位於 Hemisphere，而不是完整 Sphere。正規化並加入隨機長度後，再以非線性 Scale 讓 Sample 靠近中心處較密集：

$$
t_i =
\frac{i}{N-1}
$$

$$
scale_i =
0.1 +
0.9t_i^2
$$

$$
\mathbf{s}'_i =
scale_i
\cdot
r_i
\cdot
\operatorname{normalize}
(\mathbf{s}_i)
$$

其中 $N=16$，$r_i\in[0,1]$。

#### Random Rotation 與 TBN

為避免每個 Pixel 使用相同 Kernel 方向而產生 Banding，Shader 使用重複的 4×4 Procedural Hash Pattern 產生隨機角度：

$$
\theta =
2\pi
\operatorname{Hash}
(\mathbf{p}_{pixel}\bmod 4)
$$

$$
\mathbf{r} =
(\cos\theta,\sin\theta,0)
$$

將 Random Vector 投影到表面 Tangent Plane：

$$
\mathbf{T} =
\operatorname{normalize}
\left(
\mathbf{r} -
\mathbf{N}
(\mathbf{r}\cdot\mathbf{N})
\right)
$$

$$
\mathbf{B} =
\operatorname{normalize}
(\mathbf{N}\times\mathbf{T})
$$

Sample Direction 由 Tangent Space 轉換至 View Space：

$$
\mathbf{d}_i =
T s_{ix} +
B s_{iy} +
N s_{iz}
$$

#### Sample Position 與遮蔽判定

每個 Sample Position 為：

$$
\mathbf{p}_i =
\mathbf{p}_{view} +
radius \cdot \mathbf{d}_i
$$

再以 Projection Matrix 投影回 Screen UV，讀取該位置的 View-space Depth。DirectX 左手 View Space 中，較小的正 Z 值表示更接近 Camera，因此遮蔽判斷為：

$$
blocked_i =
\begin{cases}
1, &
z_{sampled} <
z_i - bias \\
0, &
\text{otherwise}
\end{cases}
$$

為降低不相關表面與深度不連續造成的 Halo，若：

$$
|z-z_{sampled}| > 2 \cdot radius
$$

則捨棄該 Sample。其 Range Weight 為：

$$
w_i =
\operatorname{smoothstep}
\left(
0,
1,
\frac{radius}
{\max(|z-z_{sampled}|,\epsilon)}
\right)
$$

Raw AO 為：

$$
AO_{raw} =
1 -
\frac{
\sum_i blocked_i w_i
}{
N_{valid}
}
$$

Intensity 最後以冪次調整：

$$
AO =
\operatorname{saturate}
(AO_{raw})^{intensity}
$$

#### Blur

Lighting Pass 中對 SSAO Texture 使用 5×5 Box Blur：

$$
AO_{blur}(\mathbf{u}) =
\frac{1}{25}
\sum_{x=-2}^{2}
\sum_{y=-2}^{2}
AO_{raw}
\left(
\mathbf{u} +
(x,y)\Delta\mathbf{u}
\right)
$$

Blur 能降低 Random Rotation 造成的 Noise。由於目前使用一般 Box Blur，而非 Bilateral Blur，在 Depth Discontinuity 或物件邊界附近仍可能出現輕微 Halo。

#### 可調整參數

- `Radius`：決定搜尋遮擋物的距離；數值越大，AO 影響範圍越廣。
- `Bias`：避免表面將自身誤判為遮擋物；數值過小容易產生 Self-occlusion，過大則會失去接觸陰影。
- `Intensity`：控制 AO 最終明暗強度，不改變搜尋範圍。
- `C`：Enable / Disable SSAO，方便比較前後效果。

SSAO 最終只影響 Ambient / Indirect Light，不直接將 Directional Direct Light 全部乘黑，以避免場景過度暗沉。

#### SSAO 限制

SSAO 為 Screen-space Technique，因此具有以下限制：

- 無法取得畫面外或被遮擋幾何的資訊。
- Depth Discontinuity 附近可能出現 Halo。
- 大 Radius 容易跨越不相關表面。
- Camera 移動時可能看到 Noise Pattern 變化。
- 一般 Blur 可能使 AO 跨越物件輪廓。

### Tone Mapping (using ACES)

Lighting Pass 先計算 Linear-space Light Contribution，再乘上 Exposure：

$$
\mathbf{x} =
exposure
\cdot
\mathbf{C}_{linear}
$$

本專案使用 ACES Approximation：

$$
\operatorname{ACES}(x) =
\frac{
x(2.51x+0.03)
}{
x(2.43x+0.59)+0.14
}
$$

此公式分別套用於 RGB 三個 Channel。與直接 `saturate` / Clamp 相比，ACES 能將高亮區域平滑壓縮至顯示範圍，保留更多高亮層次。

Tone Mapping 後，再進行 Gamma Correction：

$$
\mathbf{C}_{display} =
\max(
\operatorname{ACES}(\mathbf{x}),
0
)^{\frac{1}{2.2}}
$$

最後才將結果限制在 $[0,1]$：

$$
\mathbf{C}_{LDR} =
\operatorname{saturate}
(\mathbf{C}_{display})
$$

`↑ / ↓` 會即時調整 Exposure，最低限制為 `0.1`。Exposure 越大，進入 ACES 前的線性亮度越高；但高亮區會由 ACES 曲線壓縮，而不是直接被截斷。

> [!Note]
> 目前 ACES 計算直接寫在 `LightingPass.hlsl` 的後段，Lighting Pass 直接輸出至 `R8G8B8A8_UNORM` Back Buffer。若要完全拆成標準 HDR Pipeline，可再新增 `R16G16B16A16_FLOAT` HDR Intermediate Render Target，將 Lighting 與 Tone Mapping 分成兩個獨立 Pass。

## 未實作與目前限制

- Facial Animation。
- IK Solver。
- 裙擺 / 頭髮物理。
- VMD Camera Motion。
- Point Light Cubemap Shadow。
- Spot Light Shadow。
- Bloom。
- Bilateral SSAO Blur。
- 獨立 HDR Intermediate Render Target / Tone Mapping Pass。
- VMD Bezier Interpolation，目前使用 Lerp / Slerp。
- PMX SDEF 目前以 BDEF2 權重方式近似。
