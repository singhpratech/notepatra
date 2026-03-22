# Changelog

All notable changes to Notepatra will be documented in this file.

## [0.1.0] — 2026-03-22

### Initial Release

**Core**
- Native C++ (Qt5 + QScintilla) editor with Rust core library
- 60+ file types with syntax highlighting
- Memory-mapped file I/O handles files up to 2.5 GB
- Aho-Corasick search engine for fast literal matching
- Myers diff algorithm for file comparison
- 4.9 MB standalone binary, no runtime dependencies

**Editor**
- Tabbed editing with drag, reorder, middle-click close
- Tab right-click: Close, Close Others, Save, Rename, Copy Path, Color Tag
- Session persistence — reopens all files on restart
- Crash recovery — unsaved work restored after unexpected exit
- File change detection — notifies when external programs modify open files
- Double-click word highlight (all occurrences)
- Ctrl+B brace matching with selection highlight
- Code folding, bookmarks, auto-complete, indent guides
- Custom scrollbars, pastel green current line

**Plugins (inbuilt)**
- JSON Tools — Format, Minify, Fix+Format (Rust), AI Fix (Ollama)
- HTML Tools — Format, Minify, Fix+Format, AI Fix (Ollama)
- Bracket Tools — Check, Auto-Fix (Rust), AI Fix (Ollama)
- SQL Formatter — UPPERCASE/lowercase keywords, configurable indent
- Compare — Side-by-side Scintilla diff, navigation, ignore options
- Git Integration — Changed files, branch, push/pull, git gutter

**AI Integration (Ollama)**
- AI Assistant panel with 8 actions: Explain, Find Bugs, Refactor, Write Tests, Add Comments, Generate Docs, Optimize, Translate
- AI Fix button in JSON, HTML, and Bracket tools
- Ollama status indicator (green/red dot) with model selector
- Setup instructions shown when Ollama not available

**Search**
- 5-tab Find/Replace: Find, Replace, Find in Files, Mark, Go to
- 3 search modes: Normal, Extended, Regular expression
- Find/Replace in all opened documents
- Search results panel with double-click to jump to line

**Features**
- Built-in Terminal (opens as tab)
- REST Client (.http files)
- Hex Editor (binary file viewer)
- Markdown Converter (text to table/list/code/bold/link)
- File Explorer sidebar
- Function List panel
- Preferences dialog (6 tabs)

**Plugin System**
- User plugins via .so shared libraries
- Simple C API: export 2 functions to create a plugin
