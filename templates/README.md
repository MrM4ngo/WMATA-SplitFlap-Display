# Local / Generated File Templates

These templates correspond to files listed in `.gitignore` and `firmware/.gitignore`. Copy or generate the real files locally — they are not committed because they contain machine-specific paths or build output.

## Copy from template

| Template | Copy to | Notes |
|----------|---------|-------|
| `firmware/.vscode/c_cpp_properties.json.example` | `firmware/.vscode/c_cpp_properties.json` | IntelliSense include paths. PlatformIO also auto-generates this after the first build. |
| `firmware/.vscode/launch.json.example` | `firmware/.vscode/launch.json` | PlatformIO debug launch config. |
| `.vscode/c_cpp_properties.json.example` | `.vscode/c_cpp_properties.json` | Use when opening the **repo root** instead of `firmware/`. |
| `firmware/compile_commands.json.example` | `firmware/compile_commands.json` | Stub only — regenerate a real file (see below). |
| `firmware/platformio.ini.example` | `firmware/platformio.ini` | Optional. Set `upload_port` / `monitor_port` to your COM port. |

## Do not copy

| Ignored path | How to obtain |
|--------------|---------------|
| `firmware/.pio/` | Run `pio run` in `firmware/` |
| `firmware/.cache/` | Created automatically by clangd |
| `firmware/.vscode/ipch/` | Created by the C/C++ extension |
| `firmware/.vscode/.browse.c_cpp.db*` | Created by the C/C++ extension |
| `firmware/src/Config.cpp` | **Not used.** Settings live in flash via the web portal. See `firmware/src/Config.cpp.example`. |
| `firmware/Config.cpp.bak` | Backup file — ignore |

## Regenerate `compile_commands.json`

From the `firmware/` directory after at least one successful build:

```bash
pio run -t compiledb
```

If `pio` is not on your PATH (Windows):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t compiledb
```

Then use `firmware/.clangd` (already committed) with the generated `compile_commands.json` for Cursor/VS Code IntelliSense.

## Already committed (no copy needed)

These IDE files are safe to share and already in the repo:

- `firmware/.vscode/settings.json`
- `firmware/.vscode/tasks.json`
- `firmware/.vscode/extensions.json`
- `firmware/.clangd`
