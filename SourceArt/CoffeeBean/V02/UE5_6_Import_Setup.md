# Coffee Bean Jar UE5.6 / PC VR 导入说明

## 资产结构

| 文件 | 用途 | LOD0 三角面 |
|---|---|---:|
| `SM_CoffeeJar.fbx` | 中空透明罐身，底部 Pivot，内置 13 个 UCX 碰撞代理 | 1,664 |
| `SM_CoffeeLid.fbx` | 木盖、金属包边、密封圈，底部中心 Pivot，内置碰撞 | 3,968 |
| `SM_CoffeeScoop.fbx` | 黑化金属勺与木柄，抓握区附近 Pivot，内置 2 个碰撞代理 | 2,112 |
| `SM_CoffeeBean.fbx` | 单颗可交互咖啡豆，内置简化碰撞 | 504 |
| `SM_CoffeeBeans_Display_Full/Half/Low.fbx` | 三档罐内豆量视觉状态，不启用碰撞 | 47,012 / 26,012 / 10,262 |
| `*_LOD1.fbx` / `*_LOD2.fbx` | 可在 Static Mesh Editor 中导入的低模层级 | 见 Manifest |

模型按米制作，导入 UE 时 `Import Uniform Scale = 1.0`。罐身约 17 cm 宽、18.8 cm 高；盖子安装后总高约 25 cm。

## 推荐导入设置

1. 分别导入 `SM_CoffeeJar`、`SM_CoffeeLid`、`SM_CoffeeScoop`、`SM_CoffeeBean` 和三档豆堆。
2. `Combine Meshes` 关闭；`Generate Missing Collision` 关闭，使用 FBX 中的 `UCX_` 碰撞。
3. `Import Normals and Tangents`，开启 `Convert Scene`；缩放保持 1.0。
4. 在 Static Mesh Editor 中给罐身、盖子、量勺、单豆分别导入对应 `LOD1`、`LOD2`。
5. 豆堆三个状态不启用碰撞、不启用物理，用 Mesh 可见性切换表示余量。

## 玻璃材质 `M_Glass_ThinTranslucent`

FBX 已预留同名材质槽；导入后在 UE 中新建或替换为：

- Material Domain：Surface
- Blend Mode：Translucent
- Shading Model：Thin Translucent
- Translucency Lighting Mode：Surface ForwardShading
- Two Sided：开启
- Base Color：`(0.92, 0.97, 1.0)`
- Roughness：`0.08`
- Specular：`0.5`
- Opacity：`0.12–0.18`
- Refraction / IOR：`1.45`
- Thin Translucent Transmittance：轻微冷白，避免把玻璃染成蓝色

PC VR 中透明材质成本较高，建议只让罐身使用 Translucent；盖子、豆堆和量勺保持 Opaque。

## 交互组合建议

- `BP_CoffeeJar`：罐身为 Root；豆堆状态 Mesh 共用罐身原点。
- `BP_CoffeeLid`：独立抓取对象。关闭时放在罐口局部 Z 约 `18.8 cm`；打开后解除附着。
- `BP_CoffeeScoop`：抓取点靠近木柄；勺头增加一个 Scoop Trigger，用于进入豆堆后切换“装满”状态。
- 倾倒表现：使用 `SM_CoffeeBean` 的对象池或 Niagara Mesh Renderer，只激活约 12–24 颗动态豆；落入漏斗后及时回收。
- 豆量：默认 `Full`，舀取若干次后切换 `Half`，最后切换 `Low`，不要让整罐豆子全部参与刚体模拟。

## 验证结果

- FBX 均已回读验证，模型名称、材质槽与 UCX 碰撞名称可识别。
- 罐身是中空双层结构，可让勺子进入；碰撞采用分段环形壁，内部空间不会被整块凸包封死。
- LOD1 / LOD2 文件不为空，尺寸与 LOD0 一致。
- `.blend` 主文件与 PBR 贴图一并保留，便于继续修改。
