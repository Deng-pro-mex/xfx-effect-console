# 效果控制台 1.0.28 源码

这是 Windows x64 原生插件、安装器、卸载器和测试的可审查源码。技术文件名 XFXNativeConsole 与个人配置目录保持兼容，用户看到的名称为“效果控制台”。本地维护版与GitHub已公开的1.0.19区分，不自动提交或发布。

在1.0.22＋输入修复的基础上，1.0.24仅将快捷槽子菜单变为各行右侧的绑定/备注/清空图标，删除执行菜单，并清理82条重复英文译名及恢复原生中文显示。**首页布局、筛选下拉和执行快捷键保持原样**；被撤回的1.0.23首页改版不回归。见 [本轮范围](MENU-LABELS.md) 与 [历史输入修复](INPUT-FIX.md)。

1.0.25额外仅修正筛选文字＋箭头整体居中，以及筛选改变列表高度时父窗口未完整重绘导致的底部残影/漏背景；不改首页布局或快捷槽行为。见 [绘制修复](PAINT-FIX.md)。

1.0.26针对用户从AE“窗口”菜单打开面板时报告的第226行“应为:”错误，将图标颜色的嵌套条件表达式改为等价显式if/else，不改原生AEX或UI布局。28项ScriptUI模拟回归通过；模拟运行于Node/V8，不等于Adobe解析器测试。更新安装后用户已重开AE并确认“窗口”菜单能正常打开、没有再报错。只读编译probe未运行，未声称已自动捕获旧表达式在Adobe解析器中的根因。

1.0.27保留上述1.0.26补丁，修复相机/灯光图层抓取错误项目选项、无用ICC profile门禁；保存后检查用户目标PNG可解码，搜索控制台相机明确弹出成功路径或失败原因。已复现分支缺陷不等于已确认用户当次宿主根因。另加入正式Release更新：仅新版出现双入口更新按钮，下载校验后显式交互安装，AE需自行保存退出；不自动发布。见 [更新协议](UPDATES.md)。

1.0.28依据用户真实截图“已打开工程仍报请先打开工程”继续修正：原生相机按钮只排队，先在AE Idle选保存路径、返回宿主后下一次Idle才渲染；深度改为读取NewFromItem创建选项的GetWorldType，不再依赖独立ProjectH非空。设置区常驻“检查更新”，发现新版显示“更新至 x.x.x”。保留1.0.26/27全部补丁，真实宿主效果仍待用户重开验证；不以FakeHost通过宣称宿主根因完全证实。

## 已本机安装的安全版参数中文对照

用户已选择先交付只读中文对照＋离线参数词库管理。当前工作树已在设置区加入独立入口，**不修改AE效果控件原文/数值/关键帧/表达式**，原位私有汉化保持关闭。便携词库只存UTF-8四列身份/原文/译文，无网络或必选AI。R9获批后已仅安装核验AEX，保留旧备份，个人文件不变；在新隔离QA的AE25.4x86真实读取20行、保存本地译文且实测语义与dirty=false不变。此为有限fixture验收，不是原位汉化或全插件支持；版本仍1.0.28，不提交/推送/发布。详见[范围、使用及迁移说明](PARAMETER-REFERENCE.md)。

## 构建依赖

- Windows x64、Windows PowerShell 5.1 或 PowerShell 7；本次使用 5.1 验证。
- Visual Studio 2022 Build Tools：MSVC v143（本轮 14.44.35207）及 Windows SDK 10.0.26100.0。脚本固定该已验证版本的工具路径；若使用其他版本需调整脚本中的路径。
- 自行从 Adobe 获取 After Effects SDK（本轮使用 25.6），解压到 vendor/ae-sdk，使 Examples/Headers/AEConfig.h 存在。SDK 和参考软件安装器不随源码分发。

源码 ZIP 和 Git 源文件统一使用 LF 换行，保留 UTF-8 BOM（适用的 PowerShell 脚本）；`SOURCE-SHA256SUMS.txt` 不依赖 Windows 的 CRLF 检出设置。打包脚本从 `VERSION` 读取版本号，并拒绝覆盖既有导出目录或压缩包。

## 编译

在本目录打开 PowerShell：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-effect-console-release.ps1
```

输出 artifacts/native-plugin/AEGP/XFXNativeConsole.aex、installer/XFXNativeConsoleInstaller.exe 和 installer/EffectConsoleUninstaller.exe。构建过程只编译、打包和校验，不安装到真实 AE。

## 测试

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test-native-installer-cpp.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test-native-installer.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test-effect-console-lifecycle.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-core-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-console-support-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-ui-tests.ps1
node scripts/scriptui-toolbar-tests.cjs
node scripts/effect-inventory-tests.cjs
node scripts/audit-effect-translations-tests.cjs
node scripts/generate-effect-translations-tests.cjs
node scripts/generate-effect-translations.cjs --check
```

Node 用于工具栏、诊断脚本和词典生成器测试，最终用户不需要。生命周期测试使用临时目录和复制的 PING 伪 AE，不结束真实 AE；测试入口 EXE 仅将 UAC 清单改为 asInvoker，并写入 artifacts/installer-entry-tests。发行 EXE 始终要求管理员权限。

## 译名覆盖核对

在 AE 中通过“文件 → 脚本 → 运行脚本文件”运行 `scripts/export-effect-inventory.jsx`，只读导出已注册效果到 `artifacts/effect-inventory/ae-effects.json`，不修改工程。脚本写入权限未开启时会报告错误，不自动更改首选项。

```powershell
node scripts/audit-effect-translations.cjs artifacts/effect-inventory/ae-effects.json installer/XFXNativeConsole.aliases.tsv
```

1.0.20 已按实际导出的1,401项清单补齐原先906项缺失中文标签；其中3项功能/身份待确认，明确标注，不编造译名功能。统计仅限这份本机清单，非所有厂商/版本全覆盖，也不翻译效果参数面板。详细范围、未决项与回归见 [中文标签覆盖说明](LOCALIZATION.md)。

编辑TSV后运行 `node scripts/generate-effect-translations.cjs`，用 `--check` 检查编译内置表一致性。用户词库优先级保持不变；审计不读取个人词库，系列分类仍为启发式。

## 图层样式快捷按钮

1.0.22 在原快捷栏/结果区域下新增独立一排9种AE原生图层样式按钮，**不进入搜索，不占用原1–9快捷槽**。点击对所选图层添加/启用；已有样式保留参数，锁定/不兼容层报告错误，父样式组总开关关闭时提示用户处理，不擅自打开其他样式。九种已在AE25.4x86实际测试：两层初次添加、重复与关闭后重启均通过，测试自有资源已清理，项目项14→14。操作、Undo/选择恢复与跨版本限制见 [图层样式说明](LAYER-STYLES.md)。

## 默认效果目录校验

1.0.21 修复已注册条目全部进入搜索的问题：按宿主精确 `_Obsolete` 与空分类/无效元数据排除隐藏旧项和内部辅助注册，不按显示名合并效果，不误删 `BCC Obsolete` 或中文“过时”分类。真实1,401项快照的默认目录为1,203项，原68组同名冲突消失；这不是逐项添加/授权/渲染验收。应用时重新检查当前注册目录和每个图层的可添加性，缓存/快捷槽旧项不会静默替换成同名效果。详见 [目录策略与验证边界](CATALOG.md)。

```powershell
node scripts/audit-effect-catalog.cjs artifacts/effect-inventory/ae-effects.json
node scripts/effect-catalog-tests.cjs
node scripts/effect-availability-tests.cjs
```

真实可添加性探针仅在现有图层上调用 `canAddProperty`，不新增或应用效果。解除AE模式对话框阻塞后，已取得普通AV图层的1,401项报告，全部返回允许；当前默认目录1,204项且同名冲突为0（新增Bokeh/临时伪效果退出导致与历史夹具相差1项）。`canAddProperty`不等于实际应用、授权或渲染验证，不能声称所有插件可用；详见CATALOG.md的上下文与限制。

## 目录

- native/plugin 与 native/src：AE 入口、Win32 界面、搜索、设置、图片和桥接实现。
- native/tests、installer/tests：测试源码。
- installer：安装/卸载共用 C++ 源码、资源、清单、词库和 JSX 工具栏。内部 JSX 源文件保留旧名，安装目标为 效果控制台.jsx。
- scripts：构建、测试、源码打包与安全安装脚本。
- vendor/zlib-inflate、native/src/data：随附第三方来源与许可。vendor/ae-sdk 仅包含获取说明。

本仓库未为自有代码声明开源许可证；上传源码不表示额外授予许可。第三方内容遵循其随附许可。测试版宿主与 UI 验收限制见发布目录的 VERIFICATION.md。
