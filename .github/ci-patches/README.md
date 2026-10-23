# ci-patches

临时编辑通道的输入目录（**用完即删**，含 workflow、脚本与补丁）。

- `edits.json`：`{"edits": [{path, find, replace, expect}]}` —— 精确匹配→替换，命中数不符即失败
- `apply.py`：执行上述替换（两遍校验，JSON 解析失败会显式报错，成功后自毁 `edits.json` 防重入）
- 触发：向本目录推入文件即运行 AI Edit Channel workflow
- 注意：应用方（GITHUB_TOKEN）推送的提交触发的 Build 会要求人工批准，
  因此每次应用后需再用正常身份推一次以获得自动构建

已应用批次：
1. ③ 分组整理（外观拆键盘外观、TV 卡连接/工具、键映射 5→3、硬编码迁资源）
2. ③ 续（排序开关两行合一、环境摘要挪到遥控 tab 诊断卡）
