# CyberCafe 架构与资源整理（第一轮）

更新：五项内部实现重构已完成，当前职责、验证结果与头显验收顺序见 [RefactorAcceptance.md](RefactorAcceptance.md)。[CustomerOrderFlow.md](CustomerOrderFlow.md) 保留第一阶段订单解耦设计。下文“本轮验证结果”保留第一轮整理时的历史记录。

## 当前入口与事实

- UE 5.6，PCVR；用户指定的主关卡是 `/Game/VRTemplate/Maps/L_Cafe`。
- 运行时代码目前只有 `CyberCafe` 一个模块，不急于拆成多个 DLL。
- 首次磁盘盘点：2,989 个 `.uasset` / `.umap`，17 个地图；源码有 201 处 `/Game/` 路径匹配（包含测试和注释，不代表 201 个独立依赖）。
- `Content/Cafe/Maps/L_Cafe.umap` 也存在，不能仅凭同名判断为副本，更不能直接删除。
- 检查时默认启动地图仍为 `VRTemplateMap`。配置有用户未提交修改，本轮保留现状。
- 尚未通过头显实测，也尚未审阅全部蓝图图表；磁盘清单不能说明蓝图内部耦合。

## 已落地的职责边界

| 目录 | 职责 |
| --- | --- |
| `Core/Events` | 跨玩法交互事件；不归教学所有 |
| `Character`、根目录 `GrabComponent` | 玩家、手柄输入、抓取及指针；后续统一交互接口 |
| `Liquid` | 容器、成分、转移、冰块、玻璃破碎 |
| `Coffee` | 咖啡豆、研磨与冲煮设备 |
| `Serving` | 配方、接单、出品点、订单展示 |
| `Customers` | 顾客饮用动作和表现配置；从 Tutorial 迁出 |
| `Tutorial` | 教学导演、步骤和教学专用 NPC |
| `UI/Narrative` | 叙事展示；后续再把流程与展示解耦 |
| `Media` | 电视与场景音乐；音乐从模块根目录迁入 |
| `Tests` | 既有 UE 自动化验收 |

本轮仅迁移源码位置、修正 include；模块名、UCLASS 名、属性及函数签名不变，所以不需要改变 `/Script/CyberCafe.*` 或添加类型重定向。后续若重命名反射类型或拆模块，必须另做 CoreRedirects 与保存资源验证。

事件总线应只发布已发生的交互事实。新系统通过订阅使用它，不把 NPC 决策、教学分支、UI 排版放入总线。当前 `LiquidContainerActor` 仍直接引用 `CustomerDrinkComponent`，属于下一轮需要以抓取/占用契约解耦的反向依赖；本轮未改行为。

## 美术与蓝图资源

先用 Content Browser 的共享 Collections 提供稳定入口，避免首次整理就改变数千个包路径。已增加 `CC_01_MainLevel`、`CC_02_Coffee`、`CC_03_Drinks`、`CC_04_Characters`、`CC_05_Environment`、`CC_06_Audio`。它们按已知目录分类，是导航分组，不是主关卡的完整依赖集合。素材包内可能仍有演示内容。

新增自研资源建议统一进入以下目录；现有资源通过引用审计后分批迁入：

```text
Content/CyberCafe/
  Core/                 # GameMode、通用配置
  Player/               # Pawn、Input、手部表现
  Gameplay/
    Coffee/             # Blueprints、Data、Meshes、Materials、VFX
    Drinks/
    Serving/
    Customers/
    Tutorial/
  UI/
  Art/Environment/
  Audio/
  Maps/
  Tests/
```

第三方素材保持原包结构，暂不搬入自研目录。`Movies` 保留 UE 标准位置。`SourceArt` 存 DCC 源文件，`Content` 存运行时导入资源；当前 Content 中的 FBX、TGA、tbscene 和嵌套 uproject 要核对导入来源后再处理。不得根据扩展名自动删除。

命名沿用 `BP_`、`DA_`、`DT_`、`WBP_`、`SM_`、`SK_`、`M_`、`MI_`、`T_`、`NS_`、`S_`、`L_`；同一玩法的资源集中在玩法目录内。不要单纯为了统一前缀重命名第三方资源。

每批迁移步骤：生成依赖报告 → 列出旧/新包路径 → 搜索 C++、配置、测试及软路径 → 在 UE AssetTools 中移动 → 更新路径 → 编译受影响蓝图并保存引用包 → 加载主关卡 → 跑相关验收与 VR 实测。确认外部引用前保留 Redirector；不使用文件管理器搬移 `.uasset` / `.umap`，不批量清理所谓“未引用资源”。注册表看不到所有动态字符串加载与外部媒体路径。

## 可重复审计

项目根目录执行 `./Tools/Audit-Project.ps1`，输出 `Saved/ArchitectureAudit/` 下的资源清单、目录统计、同名清单和源码资源引用。报告是临时产物，不提交 Saved。

启用 Python Editor Script Plugin 后，在 UE Python 控制台执行：

```python
exec(open(r'F:/FirstVR/CyberCafe/Tools/Editor/audit_asset_dependencies.py', encoding='utf-8').read())
```

该脚本只读 Asset Registry，将主关卡的传递包依赖写到 `Saved/ArchitectureAudit/main-map-dependencies.json`，不移动、不保存资源。文件系统同名不等于内容重复，注册表无引用也不等于可以删除。

## 后续重构顺序

1. 用注册表依赖和关卡加载检查确定主关卡实际资源边界，先迁一小组自研资源并验证整条流程。
2. 把 EditorLibrary 和编辑器验收从运行时代码中隔离。现有 BlueprintFunctionLibrary 可能被工具蓝图引用，迁模块前需检查 `/Script/` 引用，不能直接剪切。
3. 收紧抓取、液体容器、顾客之间的直接依赖；针对液体守恒、占用与取消保留既有回归测试。
4. 将长函数按抓取/倒液/反馈拆出可组合组件，先固定现有行为；避免只为减少行数拆文件。
5. 加 NPC 对话状态与决策接口，再接 AI 服务。AI 输出高层意图，由游戏侧校验并执行订单、动作与对话；不要让模型直接控制每帧物理或绕过出品规则。网络请求异步、可取消，断网有本地退路，密钥不进入蓝图默认值或资源包。

每轮交付说明修改、编译与测试结果、仍未验证的项目。不要将目录整理等同于架构已经全部解决。

## 本轮验证结果

- UE 5.6 `CyberCafeEditor Win64 Development` 编译成功，UHT 与链接通过。仍有 Niagara 弃用 API、工具链推荐版本和 XRBase 插件依赖声明等警告，本轮未处理。
- 对比 Git HEAD 验证 36 个现存变更源码文件：变更仅为路径迁移与 include 替换，没有改动玩法逻辑。
- Python 依赖命令行审计成功，主关卡传递依赖包含 644 个 `/Game/` 包（包括入口）；结果见 `Saved/ArchitectureAudit/main-map-dependencies.json`。
- `CyberCafe.SceneMusic.*`：7/7 通过，报告见 `Saved/ArchitectureAudit/SceneMusicTests/index.json`。
- `CyberCafe.Customer.*`：0/10 通过。报错涉及 `TrySubmitCup` 配方验收、保存 NPC 动作绑定等，报告见 `Saved/ArchitectureAudit/Tests/index.json`。没有运行重构前基线对照，不能将失败判为已存在或此次引入；后续先定位并建立可靠基线，再继续迁资源。
- 未做头显、视觉与打包验证。未批量移动二进制资源，也未清理 Redirector。
- `git diff --check -- Source` 通过；全仓检查发现用户已有 `Config/DefaultEditor.ini` 的末尾空行提示，未修改该文件。
