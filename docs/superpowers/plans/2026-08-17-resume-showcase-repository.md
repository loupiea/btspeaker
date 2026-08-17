# 简历展示仓库发布实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 btspeaker 整理、验证并推送为适合放入简历的公开 GitHub 项目。

**Architecture:** 保持现有硬件、软件和方案三层目录结构；根 README 作为面试官入口，链接到详细资料。通过 `.gitignore` 隔离本地产物，通过自动化测试、ESP-IDF 编译和敏感信息扫描建立发布门槛。

**Tech Stack:** ESP32、ESP-IDF 5.4、C、FreeRTOS、Python unittest、Git、GitHub。

## Global Constraints

- 不提交密码、Token、私钥、Wi-Fi 凭据或个人隐私。
- 不提交编译目录、编辑器配置、硬件自动备份和调试日志。
- 保留硬件源工程、BOM、Gerber、方案文档及本次软件修改。
- 推送当前分支 `codex/ch376s-spi-self-test`，不直接覆盖默认分支。

---

### Task 1: 发布内容清理

**Files:**
- Create: `.gitignore`
- Inspect: repository-wide tracked and untracked files

- [ ] 检查当前状态、历史提交和远程仓库。
- [ ] 扫描常见凭据、私钥和本地产物。
- [ ] 编写 `.gitignore`，排除 `build/`、`.vscode/`、`.devcontainer/`、`.clangd`、备份、日志和 Office 临时文件。
- [ ] 确认 BOM、Gerber、硬件源工程与方案文档仍在发布范围内。

### Task 2: README 展示入口

**Files:**
- Modify: `README.md`

- [ ] 根据当前源码核对蓝牙、TF、AUX 功能和硬件型号。
- [ ] 增加项目亮点、功能、本人工作、硬件清单和系统架构。
- [ ] 增加目录、构建烧录步骤、TF 音频要求和测试命令。
- [ ] 增加演示素材区域；无素材时明确给出待添加的文件路径，而不伪造演示结果。

### Task 3: 发布验证

**Files:**
- Test: `software/tests/*.py`
- Build: `software/`

- [ ] 运行 `python -m unittest discover -s software/tests -v`，要求零失败。
- [ ] 使用 ESP-IDF 5.4 运行完整 Ninja 构建，要求退出码为 0。
- [ ] 再次扫描敏感信息和不应发布的文件。
- [ ] 检查 Git diff，确保没有误删用户资料。

### Task 4: 提交与推送

**Files:**
- Stage: only files covered by the approved publication scope

- [ ] 显式暂存功能代码、测试、硬件资料、方案文档、README、`.gitignore` 和本计划。
- [ ] 检查暂存区文件清单与 diff。
- [ ] 创建简洁的发布提交。
- [ ] 推送当前分支到 `origin` 并确认远端分支状态。
