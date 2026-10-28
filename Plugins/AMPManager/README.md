# AMPManager 1.11

在插件设置的 **API proxy** 中填写代理 URL，保存后会重新登录并刷新。

- HTTP：`http://127.0.0.1:7890`
- SOCKS5（本地解析 DNS）：`socks5://127.0.0.1:1080`
- SOCKS5（代理解析 DNS）：`socks5h://127.0.0.1:1080`
- 代理认证：`http://user:password@127.0.0.1:7890` 或 `socks5h://user:password@127.0.0.1:1080`。用户名和密码中的 URL 特殊字符需要百分号编码。

留空使用原有的 WinHTTP 默认连接。代理仅用于 AMP API（登录和数据查询）；OneBot WebSocket 连接继续使用原有设置。

自定义代理需要系统目录中的 `curl.exe`，通常随 Windows 10 1803 及更新系统提供。请求失败时不会回退直连；提示中的 curl 错误码可参见 [curl 文档](https://curl.se/docs/manpage.html)。代理地址通过 Windows DPAPI 加密保存，请在设置界面中编辑，不要直接修改 INI 中的密文。

本版源代码版本为 1.11；构建需要 Visual Studio 2022 的 v143 C++ 工具集、Windows SDK 和 MFC，使用 `AMPManager.vcxproj` 的 Release/x64 或 Release/Win32 配置。
