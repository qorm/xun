# XUN 语法高亮与编辑器支持使用指南 (Editor Syntax Guide)

本文档介绍如何在各大主流编辑器与 IDE 中快速配置并启用 **XUN (X Unquoted Notation)** 的语法高亮与语言支持。

---

## 1. Visual Studio Code / Cursor / Windsurf

### 方式 A：一键软链接安装（推荐，最快捷）

在终端中执行以下命令即可（将当前扩展目录直接链接至编辑器插件目录）：

#### macOS / Linux
```bash
# VS Code
mkdir -p ~/.vscode/extensions
ln -s "$(pwd)/editors/vscode" ~/.vscode/extensions/vscode-xun

# Cursor (如使用 Cursor 编辑器)
mkdir -p ~/.cursor/extensions
ln -s "$(pwd)/editors/vscode" ~/.cursor/extensions/vscode-xun
```

#### Windows (PowerShell)
```powershell
New-Item -ItemType SymbolicLink -Path "$HOME\.vscode\extensions\vscode-xun" -Target "$PWD\editors\vscode"
```

重启或重新加载编辑器窗口（`Cmd+Shift+P` -> `Reload Window`），打开任何 `.xun` 文件即可享受精准高亮与 2 格自动缩进！

---

### 方式 B：打包为 `.vsix` 插件安装

```bash
cd editors/vscode
# 使用 vsce 打包
npx -y @vscode/vsce package
# 安装到 VS Code
code --install-extension vscode-xun-0.1.5.vsix
```

---

## 2. JetBrains 系列 IDE (IntelliJ IDEA / WebStorm / PyCharm / GoLand / CLion)

JetBrains IDE 原生支持导入 TextMate 语法高亮包：

1. 打开 IDE 菜单：`Settings`（Windows/Linux）或 `Preferences`（macOS）；
2. 导航至：**`Editor`** -> **`TextMate Bundles`**；
3. 点击右上角的 **`+` (Add)** 按钮，选择本仓库的 **`syntaxes/`** 文件夹；
4. IDE 将自动识别 `XUN` 语法定义并将 `*.xun` 文件后缀关联到该语法；
5. 点击 `Apply` 保存，打开任意 `.xun` 文件即可看到着色效果。

---

## 3. Vim / NeoVim

执行以下命令，将语法高亮与文件类型探测脚本安装至本地：

### Vim
```bash
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp editors/vim/syntax/xun.vim ~/.vim/syntax/
echo 'autocmd BufNewFile,BufRead *.xun setfiletype xun' > ~/.vim/ftdetect/xun.vim
```

### NeoVim
```bash
mkdir -p ~/.config/nvim/syntax ~/.config/nvim/ftdetect
cp editors/vim/syntax/xun.vim ~/.config/nvim/syntax/
echo 'autocmd BufNewFile,BufRead *.xun setfiletype xun' > ~/.config/nvim/ftdetect/xun.vim
```

---

## 4. Sublime Text

将语法文件拷贝至 Sublime Text 的用户配置包目录：

### macOS
```bash
cp editors/sublime/XUN.sublime-syntax ~/Library/Application\ Support/Sublime\ Text/Packages/User/
```

### Linux
```bash
cp editors/sublime/XUN.sublime-syntax ~/.config/sublime-text/Packages/User/
```

### Windows
```cmd
copy editors\sublime\XUN.sublime-syntax "%APPDATA%\Sublime Text\Packages\User\"
```

打开 `.xun` 文件后，右下角选择语法为 **`XUN`** 即可。

---

## 5. Web 网页代码高亮 (Shiki / Prism)

`syntaxes/xun.tmLanguage.json` 完全兼容标准 TextMate 语法，可直接加载到 **Shiki** 或任何基于 TextMate 的网页代码渲染器中：

```js
import { getHighlighter } from "shiki";
import xunGrammar from "./syntaxes/xun.tmLanguage.json" assert { type: "json" };

const highlighter = await getHighlighter({
  langs: [
    {
      id: "xun",
      scopeName: "source.xun",
      grammar: xunGrammar,
    },
  ],
  themes: ["nord"],
});

const html = highlighter.codeToHtml(code, { lang: "xun", theme: "nord" });
```
