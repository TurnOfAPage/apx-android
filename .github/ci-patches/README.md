# ai-cpatches

临时编辑通道的输入目录（**用完即删**，含 workflow、脚本与补丁）。

- `edits.json`：{"edits": [{path, find, replace, expect}]} —— 精确匹配→替换，命中数不符即失败
- `apply.py`：执行上述替换（两遍校验，成功后自毁 edits.json 防重入）
- 触发：向本目录推入文件即运行 AI Edit Channel workflow
