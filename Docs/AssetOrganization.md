# 美术与蓝图资源目录：当前结构

已实际迁移 **706 个非地图资源和主关卡**，通过 UE 编辑器包接口完成，未在文件管理器中移动 UE 包。第一阶段迁移 688 项；第二阶段把主关卡和此前仍在 VRTemplate 的 18 项材质、材质函数及 Niagara 资源一起迁入新目录。主关卡现在是 `/Game/CyberCafe/Maps/L_Cafe`，编辑器启动地图和游戏默认地图均已同步。其他地图与原有资源名、类名保留。

## 从哪里开始找

自研内容统一从 Content Browser 的 `/Game/CyberCafe` 进入：

```text
CyberCafe/
  Maps/                                  当前主关卡 L_Cafe
  Core/Blueprints/                       GameMode
  Player/
    Blueprints/                          Pawn、抓取与 VR 通用蓝图
    Input/                               输入动作与映射
    Haptics/                             手柄反馈
    Pointer/                             当前指针资源
  Gameplay/
    Coffee/
      Beans/                             豆罐、勺子与相关资源
      Grinder/                           研磨机蓝图与反馈声音
      Brewing/                           手冲壶、滤杯、滤纸、咖啡粉与冲煮反馈
    Drinks/
      Containers/                        杯瓶、网格、材质与破碎配置
      Ice/                               冰桶、冰铲、冰块及表现
      Audio/                             倒液声音
    Serving/
      Blueprints/                        出品点、全息订单屏及其专用 UI
      Recipes/                           饮品配方
      Menu/                              配方菜单数据与图标
    Tutorial/Blueprints/                  教学蓝图和叙事 HUD
  Characters/                            BarGirl、Manny 及动作配置
  Environment/Cafe/                      自研咖啡馆模型、材质、贴图、电视
  Shared/Audio/                          场景音乐与混音
  UI/                                    通用 Widget 与叙事声音
  Tests/                                 迁出的测试资源
```

Content Browser 的共享集合 **`CC_00_CyberCafe`** 汇总这 707 个资源（含主关卡）。原有六个分类集合已同步新路径，可以继续按咖啡、饮品、人物、环境和音频查找。

常用入口：

| 内容 | 当前包路径 |
| --- | --- |
| 主关卡 | `/Game/CyberCafe/Maps/L_Cafe` |
| 玩家 Pawn | `/Game/CyberCafe/Player/Blueprints/VRPawn` |
| 研磨机 | `/Game/CyberCafe/Gameplay/Coffee/Grinder/Blueprints/BP_CoffeeGrinder` |
| 豆罐 | `/Game/CyberCafe/Gameplay/Coffee/Beans/BeanJar/Blueprints/BP_DirectBeanJar` |
| 手冲壶 | `/Game/CyberCafe/Gameplay/Coffee/Brewing/Blueprints/BP_HandPourKettle` |
| Highball 杯 | `/Game/CyberCafe/Gameplay/Drinks/Containers/Cups/Menu/BP_MenuCup_Highball` |
| 冰拿铁配方 | `/Game/CyberCafe/Gameplay/Serving/Recipes/DA_Latte` |
| 配方菜单 | `/Game/CyberCafe/Gameplay/Serving/Menu/DA_RecipeMenu` |
| 顾客品尝动作 | `/Game/CyberCafe/Characters/BarGirl/FirstCustomer/Highball/DA_BarGirlHighballDrinkMotion` |

## 为什么旧目录还存在

原路径保留了 UE 的兼容 Redirector，蓝图生成类路径也有对应处理。这些小文件不是重复复制的整套美术，保证旧软路径、尚未改动的外部工具及已有资源引用仍能解析。当前不要通过文件管理器删除它们。

第三方原包继续保留在 `Hong_Kong_Street`、`HongKong`、`Flying_cantina`、`Coffee_Equipment`、`LiquidMaterials_VFXPack` 等原目录。`coffee_grinder_scene` 只迁出了明确属于项目定制的 FilterSet、GroundsV02；其余原始内容保留。第三方示例地图和序列可能因引用自研资源而重新保存引用，但目录未迁移。

当前主关卡的 644 个可达包已经没有 VRTemplate 真实资源或重定向依赖，运行时代码与启动配置也不再指向该目录。包依赖扫描不涵盖任意字符串动态加载；编辑器迁移工具仍有意保留旧路径作为迁移来源校验。

`VRTemplate` 仍保留兼容重定向、原模板地图及旧示例；这不等于整个目录可以删除。`Cafe/Maps/L_Cafe` 与主关卡同名，尚未确认用途，未删除或合并。已知旧 `WBP_TutorialHint` 也未强行搬迁或修复。

## 后续放资源的规则

- 新自研资源直接进入 `CyberCafe` 对应功能目录。BP 按职责放，不再建立一个覆盖整个项目的 Blueprints 总目录。
- 一个道具的专用网格、材质、声音、数据放在该道具或玩法附近；内容较多时再使用 Blueprints、Meshes、Materials、Audio、Data 子目录。迁入的旧内容保留了一部分内部子目录，便于追溯。
- 真正被多个功能使用的资源才进入 Shared；专用资源留在所属功能。
- 原始 FBX、DCC 文件和自制贴图源文件放 SourceArt；本轮保留模型、贴图的重导入源路径与原始导入时间/哈希，没有重新导入或改变几何数据。原本位于外部机器上的源文件未被重建。
- 第三方包保留原结构。项目定制优先创建自己的派生蓝图或材质实例，引用原包的基础资源。
- 命名沿用 BP、WBP、DA、DT、SM、SK、M、MI、T、NS、S、L 等现有前缀。本轮没有为了统一前缀批量改资源名。

## 已完成的验证

- 第二阶段 19 项目标资源与旧路径重定向有效；主关卡仍为 312 个 Actor，身份、标签、类型及变换一致。
- 新主关卡的 644 个可达包没有 VRTemplate 真实资源或重定向依赖；151 个第二阶段备份文件的 SHA256 全部匹配。
- 688 个目标资源存在且类型正确；688 个旧路径的兼容重定向有效。
- 主关卡 312 个 Actor 的身份、标签、类映射与变换保持一致。
- 将原始二进制备份作为只读元数据扫描来源，对比 708 个受影响资源包的 Game 包依赖：没有丢失原有依赖。
- 第一阶段 43 个文本文件、第二阶段 24 个文本文件同步新路径（两阶段文件可能重叠），启动地图配置另行更新；再次扫描没有待更新路径。
- UE5.6 编辑器目标编译通过；主关卡依赖蓝图编译通过，0 错误、0 警告。
- 主关卡迁移后，同一组 89 项回归：68 项通过，21 项仍是此前确认的旧基线失败，没有新增失败。完整报告位于 `Saved/ArchitectureAudit/CustomerFlow-20261117-000808/Tests/index.json`，与迁移前的状态及错误逐项比较见 `Saved/MapOrganization/regression-comparison.json`。
- 回归脚本从引擎 Entry 空地图启动，再由原测试加载并复制保存主关卡，避免默认启动主关卡时生成的临时导航 Actor 进入测试源；没有放宽完整场景复制断言，实际游戏仍默认启动新主关卡。
- 另修正了验证所需的非玩法问题：Unity 编译的内部辅助函数重名、三个源文件的首 include 顺序，以及旧破碎测试跨 World Tick 使用临时配置时缺少 GC 强引用。没有放宽测试断言。

头显手感、场景观感和打包仍需要实际验收。建议按 [统一验收顺序](RefactorAcceptance.md) 检查抓取、倒液、咖啡制作、全息菜单、出品、顾客品尝和恢复。

## 清单、备份与复查

- `Tools/AssetMigrationManifest.json`：逐项旧/新路径清单。
- `Tools/MainMapMigrationManifest.json`：第二阶段主关卡及 18 项依赖的逐项清单。
- `Saved/MapOrganization/Backup`：第二阶段迁移前受影响包的备份与 SHA256 清单。
- `Saved/MapOrganization/verification.json`：19 项目标与重定向、312 个 Actor 的一致性检查。
- `Saved/MapOrganization/template-dependencies.json`：新主关卡的 VRTemplate 依赖扫描结果。
- `Tools/AssetOrganizationRules.json`：本轮目录分类规则与保留项。
- `Saved/AssetOrganization/Backup`：708 个原始包的备份及 SHA256 清单，验收前保留。
- `Saved/AssetOrganization/journal.json`：29 批迁移记录。
- `Saved/AssetOrganization/verification.json`：目标、兼容路径与场景检查结果。
- `Saved/AssetOrganization/dependency-comparison.json`：与原备份逐包对比的依赖结果。

可执行 `./Tools/Verify-AssetOrganization.ps1` 复查资源与场景，执行 `./Tools/Verify-MainMapBlueprints.ps1` 复查蓝图。迁移已完成，不应再次执行 apply；将来新资源直接按目录规则创建即可。
