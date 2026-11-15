# 五点重构：统一验收

本轮在已有订单组件基础上完成五项实现重构。原反射类名、主关卡路径、蓝图配置属性、抓取/倒液入口保留；没有移动或另存 `.uasset` / `.umap`。以下“保留入口”指兼容外观，实际业务实现已归入独立组件。

## 已完成的职责拆分

| 问题 | 本轮实现 |
| --- | --- |
| 液体容器承担库存、表现、音频和顾客行为 | `LiquidInventoryComponent` 持有毫升、成分与版本并执行收支；`LiquidPresentationComponent` 执行材质、Niagara、动态波动并持有运动历史；`LiquidAudioComponent` 持有循环音源和播放门禁；`LiquidConsumptionComponent` 提供通用一次性占用、限额消费与释放。容器不再引用顾客类。 |
| 教学兼任顾客流程 | `CustomerOrderComponent` 独立管理出品到品尝的生命周期；教学通过适配层发起临时订单、等待终态。未出品订单的旧配置恢复已移入订单组件；导演不知道出品步骤和品尝步骤的具体类型组合。 |
| 出品点兼任订单与验收 | `ServingOrderComponent` 持有订单序号、占用者、验收结果和提交事务，负责配方验收与冻结前复核；`ServingSpotActor` 保留空间检测、候选稳定判断和实体放置，旧 API 转发到组件。 |
| 顾客动作针对某个动画硬编码 | `CustomerDrinkMotion` 增加可编辑眨眼 Cue、Morph 名称、声音插槽/音量、抬杯混合结束和回杯混合起点，并验证配置。执行组件读取配置；已有资产缺少新字段时使用复现旧行为的默认值。NPC 声音锚点也读取配置插槽。 |
| 配方判断混合反馈文案 | `EvaluateSnapshot` 只读准备快照并输出数值、原因、原料和失败区间；`DrinkFeedbackFormatter` 单独决定中文/配置文案。原 `Evaluate(Cup)` 是采集快照与格式化的兼容入口。纯规则实现中没有 Coffee/Milk 特判或文本生成。 |

主要文件位于 `Source/CyberCafe/Liquid`、`Serving`、`Customers`。`ALiquidContainerActor` 的 `CurrentVolumeML`、`Composition` 等旧蓝图只读入口作为同步视图保留，权威库存仅在库存组件。原作者配置仍位于 Actor，避免重定向既有蓝图属性；这不意味着需要继续在 Actor 中添加新业务分支。

## 运行约定

- 液体转移的在途包与世界账本继续由 `LiquidTransferSubsystem` 管理，视觉粒子不决定毫升数量。
- 冻结杯只允许持有通用消费权限的对象按预算扣量；错误持有者不能扣量或释放他人的权限。预算耗尽、释放后均不能重新占用同一已交付杯。
- 消费先提交库存和行为计数，再通知外部，保留取消/销毁回调重入保护。
- 品尝可以没有教学导演；教学跳过/重启仍会取消其自己创建的待启动或进行中的动作。旧地图直接进入 Consume 的兼容入口保留。
- 一个顾客组件同时处理一个订单；连续营业仍需玩法明确移走已交付的杯子、复位出品点。没有新增排队、AI 服务或网络请求。

## 自动验证

- UE5.6 `CyberCafeEditor Win64 Development` 编译通过。
- 主关卡依赖的 **52 个蓝图全部编译通过**，UE 原生 CompileAllBlueprints 报告 0 错误、0 警告、0 加载失败，未保存资源。
- 最终受影响回归共 **89 项：68 通过、21 失败、0 未运行**。新增 4 项架构测试均通过：通用消耗权限与预算、自定义动作配置、临时订单配置恢复、非咖啡结构化配方与文案独立性。
- 顾客 11/11、配方 8/8、液体 9/9、咖啡 8/8、场景音乐 7/7；玻璃 8/9、倒液音频 9/10、同事对话 4/7。旧工作台恢复 0/16，具体原因如下。
- 这 21 项失败已在隔离的 **Git HEAD 原始源码** 中复现，错误文本逐条相同，没有新增失败：16 项依赖另一台机器 `C:/Users/cloudcheng/WorkBuddy/.../current_before.json`；另 5 项断言旧恢复目标数量、旧液体容器数量或旧台词，与当前保存资产不符。本轮没有通过删除、跳过或放宽这些断言伪造全绿。

报告：

- 最终回归：`Saved/ArchitectureAudit/CustomerFlow-20261116-010427/Tests/index.json`。
- 旧恢复/玻璃/音频基线：`Saved/ArchitectureAudit/OriginalBaselineResults/Tests/index.json`。
- 对话基线：`Saved/ArchitectureAudit/OriginalBaselineDialogue/Tests/index.json`。
- 隔离原始构建位于 `Saved/ArchitectureAudit/OriginalBaseline`，只在此目录重建原始源码；Content 是指向项目原资源的目录联接，没有复制或修改资源。没有回退当前工作区。

快速回归：`./Tools/Test-CustomerFlow.ps1`，默认覆盖顾客、配方、场景音乐和新增架构测试。扩大验证可通过 `-Filter` 使用 UE 的 `+` 合并过滤条件。包含上述旧基线测试时，脚本仍正确报失败。

蓝图验证：`./Tools/Verify-MainMapBlueprints.ps1`。它先生成主关卡传递依赖中的蓝图白名单，再用 UE 原生 CompileAllBlueprints 在内存中编译，不保存包；结果见 `Saved/ArchitectureAudit/blueprint-compile.log`。

## 头显统一验收顺序

打开 `/Game/VRTemplate/Maps/L_Cafe`：

1. 抓取杯子、瓶子和咖啡器具，观察液面、材质、倒液声音与原有手感。
2. 按现有教学做冰拿铁；分别提交空杯、缺奶、比例错误、缺冰和正确饮品，检查拒收原因、角色反馈与全息菜单。
3. 正确出品后观察 NPC 使用原杯完成品尝、喝掉对应份量并把杯子放回；检查眨眼、声音位置、杯子轨迹及原有冰块。
4. 在出品前、出品后待启动、中途品尝时分别跳过/重新开始教学，确认没有迟到动作、重复扣液体或杯子消失。
5. 检查工作台恢复、破碎和补给；对已交付冻结杯保持保护。自动化旧恢复套件的缺失外部基线尚未修复，因此这项尤其需要实际确认。

要检查可配置性，可在测试用 Motion 副本中改变 BlinkCues、BlinkMorph、VoiceSocket、LiftBlendEnd、ReturnBlendStart；不要直接覆盖正式动作资产做实验。新调酒配方可以通过原料规则配置，并用 EvaluateSnapshot 获取结构化结果。正式资产本轮没有修改。

本轮尚未做头显、画面与打包验证；上述步骤是用户统一验收入口。
