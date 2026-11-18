# 常客云端台词：MiniMax 国内订阅 Key

当前默认供应商为MiniMax国内平台，模型ID为 `MiniMax-M3.1-Flash`，接口为 `https://api.minimax.cn/v1/responses`。按用户现有Coding Plan订阅Key接入；本机读取 `MINIMAX_API_KEY`，不读取或复用OpenAI密钥。当前版本让模型受约束地改写已有剧情台词，保留固定选项；不接麦克风、TTS或自由聊天。没有密钥时立即使用原台词。

**当前状态：用户已完成MiniMax对话的头显验收。** 自动化与密钥存储测试只使用本地模拟或假密钥，不主动调用真实供应商。官方当前订阅接入指引使用M3.1 Flash，具体模型权限以账户为准。

## 怎么试玩

先关闭已有编辑器。在项目根目录的PowerShell中运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "F:\FirstVR\CyberCafe\Tools\Play-RegularCustomer.ps1"
```

第一次没有已存密钥或环境变量时，脚本在本机终端遮蔽输入，并用Windows DPAPI加密保存到 `Saved/LocalSecrets/MiniMax.key`。之后使用同一Windows用户运行同一命令会自动解密读取，不再询问。然后在 `L_RegularCustomer` 中启动VR Preview。

读取优先级：显式 `-PromptForApiKey` 输入并替换保存 > 当前供应商的加密文件 > 进程／Windows用户／机器环境变量 > 首次遮蔽输入。环境变量中的旧值不会自动复制到文件；需要迁移时加 `-PromptForApiKey` 重新输入一次。文件损坏或换用户无法解密会明确报错，不会悄悄换用其他Key。

启动脚本仅通过环境变量把解密后的Key传给本次编辑器，不放入命令行、配置资产、日志或永久环境变量；结束后恢复终端原环境。C++不会直接读取密钥文件，因此从桌面快捷方式打开编辑器仍只会使用其继承到的环境变量。请使用上述启动脚本以自动读取Saved密钥。

### 更换与删除

更换当前供应商Key（只保存，不启动编辑器或联网）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "F:\FirstVR\CyberCafe\Tools\Set-DialogueApiKey.ps1"
```

删除已存Key：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "F:\FirstVR\CyberCafe\Tools\Set-DialogueApiKey.ps1" -Remove
```

可用 `-Provider MiniMax` 或 `-Provider OpenAI` 指定供应商。删除只清理对应文件，不更改已有环境变量或已启动编辑器；要停用云端可用 `-AuthoredOnly`。更换后重新启动编辑器才使用新Key。

DPAPI文件绑定当前Windows用户与本机，不能靠复制文件跨电脑迁移。`Saved/`已被Git忽略，`DefaultPakFileRules.ini`额外排除LocalSecrets；现有诊断输出只写入各自报告目录，不包含密钥目录。分享项目压缩包或清理Saved时也应排除／保留此目录，避免把整个Saved直接打包。当前没有新增诊断打包工具。

假密钥验证：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools/Tests/Test-DialogueKeyStorage.ps1`。测试在独立Saved目录验证DPAPI加解密、密文不含明文、更换与空值保护、损坏文件报错、供应商隔离、启动环境注入和恢复、删除；不读取真实密钥，不联网。

需要对照原台词时：

```powershell
./Tools/Play-RegularCustomer.ps1 -AuthoredOnly
```

需要先检查账号、模型和接口是否可用时，可单独运行：

```powershell
./Tools/Test-CloudDialogueConnection.ps1 -PromptForApiKey
```

这个命令会发起**一次真实API请求，消耗订阅额度或API余额，取决于所输入的Key类型**。只发送固定连接探测文本，不发送玩家存档。结果与耗时写到 `Saved/RegularCustomer/CloudDialogue/live-connectivity.json`。它验证连通性，不代表头显里的台词效果已验收。订阅Key与按量Key不是同一种凭据；当前不会自动切换供应商或改用按量Key。

## 游戏中怎样使用AI

### 头显内查看当前台词来源

对话框右上角新增“神经链路”入口，空手用任一手射线指向它并扣扳机，可展开／收起下方详情。正文与选项的世界位置保持不变，展开不会重新播放台词、打断打字或选择剧情选项。

| 灯色与标签 | 含义 |
| --- | --- |
| 灰色／本地台词 | 当前为确定性台词、云端关闭或缺少密钥 |
| 蓝色呼吸／正在生成 | 新台词正在等待请求完成，不代表已得到AI台词 |
| 蓝色常亮／回复就绪 | 新台词在等待读完菜单，当前可见台词来源仍单独记录 |
| 青色／AI台词 | 这句实际采用了通过本地检查的API回复 |
| 橙色／已回退 | 这句因超时、限流、额度保护或不合规输出而使用本地台词 |

详情显示配置模型、当前句来源、新台词状态、最近已结束请求的结果／耗时、本句说明、会话请求数与滚动60秒使用量。计数包含失败和被取消的尝试，不等于服务方计费次数。没有请求时不显示虚构的延迟；API等待中单独显示实时等待时长。

来源由展示端维护，不能直接拿客户端最近一次成功状态代替。回复被取消、延后或丢弃，不会把旧的本地字幕标成AI；后续确定性提示也不会沿用上一句的AI标记。详情只输出模型名、固定状态标签和数字，不输出密钥、接口地址、请求正文或提供方错误正文。此功能不会额外发起“连通性探测”。

原生HTTP和展示回归共26项通过：`Saved/ArchitectureAudit/CustomerFlow-20261118-222513/Tests/index.json`。新增检查覆盖耗时与滚动计数、取消后的迟到结果、排队／过期回复不改当前来源、双手射线开合详情、展开不改变NPC摆放与打字进度。编译日志：`Saved/RegularCustomer/build-ai-indicator.log`。本轮没有调用真实API。

`Tools/Editor/preview_regular_customer.py` 会导出 `Saved/RegularCustomer/ai-link-*-preview.png`，预览使用明确的合成诊断数据，不能作为真实连接证明。

- 适用：招呼、话题选择前的提问、已有故事段落、拿杯／倒液／实际磨豆的闲聊、品尝反馈、持续聊天和下一杯的点单提议。订单只有玩家答应后才由本地状态机创建。
- 保留本地确定性：选项文本、订单和配方判定、拒收原因、接杯提示、动作启动、取消／失败提示、饮品数值、剧情进度与记忆写入。
- 发送不可变快照：提示类型、已编写的原句、上次饮品及话题、话题完成次数、是否实际磨豆、已验收饮品数据，以及当前到访完成饮品数、本轮选项、实际／提议配方名和最多450字近期台词。没有发送源码、美术资源、文件路径、麦克风内容或完整存档。
- 要求API返回 `{"line":"..."}`。没有工具调用、执行命令或更改游戏状态的接口。MiniMax请求不附加未明确支持的严格JSON Schema参数，使用提示词要求JSON并执行同样的本地解析检查；不是供应商强制结构化输出。请求发送 `store=false`；MiniMax文档说明该参数被忽略且响应固定为false，不表示提供方没有其他日志或保留政策。
- 剧情锚点必须保留；实际饮品反馈的首句原样保留。输出有长度、格式、关键字、数值串和部分未确认动作／属性检查。拒绝、截断、格式错误或不符合检查都回退本地台词。
- 这些检查不能证明所有自然语言语义都正确。因此当前只做受约束改写，真实台词仍需人工验收；不宣称模型永远不会产生不准确表达。

## 时序与回退

- 每个Encounter同时只有一个请求。玩家换页、接受饮品、取消、重开或销毁Actor时，旧请求被取消并失去展示资格。
- 会话台词等待时短暂显示“……”，选项和制作逻辑不等待网络。没配置密钥、禁用云端或不允许联网的运行环境会在同一次调用中立即回到原台词。
- 闲聊等待网络时保留原屏幕文字。返回时再次检查菜单阅读状态，需要时继续等待；超过8秒的闲聊展示期限直接丢弃，不在玩家忙完后突然补说。
- 默认6秒超时，回到原台词。超时用单调实时时钟，不依赖游戏时间。HTTP回调明确在Game Thread执行。
- 同一个Encounter滚动60秒最多8个HTTP请求，包括发出后被取消的请求。窗口用尽时立即使用本地台词，旧请求超过60秒后恢复额度，不会因长期聊天永久停用AI。没有自动重试。401／403／429后冷却60秒，其他网络错误冷却10秒。
- 日志只输出固定状态码，不输出密钥、请求正文或提供方错误正文。开发日志中的 `[RegularDialogue] result=validated generated=1` 表示该句通过了云端输出检查；其他状态说明使用了回退。

## 配置与代码位置

公共设置在 `Config/DefaultGame.ini` 的 `[/Script/CyberCafe.RegularDialogueSettings]`：

```ini
bEnabled=True
Provider=MiniMax
Model=MiniMax-M3.1-Flash
Endpoint="https://api.minimax.cn/v1/responses"
DeadlineSeconds=6.0
MaxOutputTokens=1024
MaxRequestsPerMinute=8
```

INI中的HTTPS地址需要保留引号。接口必须兼容Responses；不能直接填Chat Completions地址。M3.1 Flash始终推理，使用 `reasoning.effort=low`，不发送不支持的 `none` 或温度参数。只展示assistant的output_text，不展示reasoning内容。

保留OpenAI适配：如以后切回，需要同时设置 `Provider=OpenAI`、`Model=gpt-6.1-sol` 和 `Endpoint="https://api.openai.com/v1/responses"`；对应读取 `OPENAI_API_KEY` 并发送严格JSON Schema。启动与连通探测脚本读取上述项目配置。UE的Saved配置若覆盖公共设置，需要同步清除或修改对应覆盖项。

如果探测成功但游戏经常记录 `timeout` 或 `incomplete`，需要根据实测调整期限或输出上限。上限包括推理与文本输出；增加额度和等待时间会影响成本与体验。尚未测量本账户真实延迟。

- `Dialogue/RegularDialogueProtocol`：请求结构、角色指令、响应解析和输出检查。
- `Dialogue/RegularDialogueClient`：HTTP、请求归属、超时、冷却、预算和取消。
- `Customers/RegularCustomerEncounter`：生成事实快照及应用当前有效的台词，继续控制原有玩法。
- `Tools/Dialogue-Key.ps1`：按供应商选择DPAPI本地存储、本机遮蔽输入与环境变量读取。
- `Tools/Set-DialogueApiKey.ps1`：更换／删除本机密钥，不启动游戏或发起API请求。

本轮是本机编辑器／Development原型。**Shipping构建禁用直接云端请求**；公开分发前需要后端持有供应商密钥并向游戏提供受限接口，目前没有部署该后端。密钥没有放进设置、蓝图、存档或仓库。

## 验证

`./Tools/Test-CloudDialogue.ps1` 启动一次性回环模拟服务器，测试真实UE HTTP传输、Authorization和Content-Type格式、MiniMax请求参数、带reasoning项的中文往返，以及解析、输出约束、超时、取消和预算。只使用固定测试密钥，不转发请求、不调用真实供应商。OpenAI请求结构仍由单元测试覆盖。

自动测试、Commandlet和无人值守运行默认不调用云端；测试传输显式注入模拟结果。常客场景回归额外覆盖切换话题后旧回复丢弃、读菜单时延后展示、取消后的迟到结果不覆盖UI，以及AI不修改订单或记忆。

切换MiniMax之前，完整132项：111通过，21项既有失败，原126项的状态及错误逐项一致。报告：`Saved/ArchitectureAudit/CustomerFlow-20261118-034350/Tests/index.json`；对比：`Saved/RegularCustomer/CloudDialogue/comparison.json`。

切换前云端5项通过（`CustomerFlow-20261118-034940`），常客11项通过（`CustomerFlow-20261118-034956`），报告均在 `Saved/ArchitectureAudit`。MiniMax新增协议与密钥选择测试。不能把引擎退出码0当成全部测试通过，也不能把本地模拟成功称为真实API联调成功。

MiniMax切换后编译通过（`Saved/RegularCustomer/CloudDialogue/build-minimax.log`），云端6项及常客11项共17项全部通过：`Saved/ArchitectureAudit/CustomerFlow-20261118-203708/Tests/index.json`。包括原生HTTP回环请求与中文响应、推理项不进入台词、保留OpenAI协议，以及既有取消、超时、菜单阅读和记忆保护。此次未发起真实供应商请求。

## 官方接口依据

- [MiniMax国内订阅配置](https://platform.minimax.cn/docs/m-plan/other-tools)
- [MiniMax Responses接口](https://platform.minimax.cn/docs/api-reference/responses-create)
- [MiniMax订阅Key和按量Key区别](https://platform.minimax.cn/docs/m-plan/faq)
- [GPT-6.1 Sol模型](https://developers.openai.com/api/docs/models/gpt-6.1-sol)
- [Responses创建接口](https://developers.openai.com/api/reference/resources/responses/methods/create)
- [结构化输出](https://developers.openai.com/api/docs/guides/structured-outputs?api-mode=responses)
