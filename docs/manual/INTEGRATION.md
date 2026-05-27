# INTEGRATION.md — Tubelight + Manual de Usuario

El manual de usuario se entrega como `docs/manual/manual.html` (single-file
interactivo bilingüe, ~300 KB) acompañado de `manual.pdf`, `manual.es.txt`,
`manual.en.txt`. La integración en la propia app expone un botón que abre
el HTML local en el navegador por defecto.

## Cambio aplicado a `src/overlay/menu.cpp`

En el tab **Help** del menú ImGui se ha añadido un botón "Open user manual
(manual.html)" con estilo de acento mint. La lógica:

1. Detecta el directorio del binario con `GetModuleFileNameW`.
2. Busca `manual.html` en estas rutas (en orden):
   - `<exe_dir>/docs/manual/manual.html`
   - `<exe_dir>/../docs/manual/manual.html` (build/Release subdir)
   - `<exe_dir>/../../docs/manual/manual.html` (build/windows-vcpkg/Release)
   - `<exe_dir>/manual/manual.html`
   - `<exe_dir>/manual.html`
3. Si la encuentra, abre con `ShellExecuteW(nullptr, L"open", path, ...)`.
4. Si no, fallback a `https://github.com/GS-RUN/tubelight`.

Headers añadidos al top del fichero:

```cpp
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <shellapi.h>
#endif
```

## Distribución

Para que el botón funcione en releases distribuidos, el zip de release
debe incluir la carpeta `docs/manual/` junto al ejecutable:

```
tubelight-0.1.0-win64/
  tubelight.exe
  epoxy-0.dll
  glfw3.dll
  profiles/
    crts/...
    signals/...
  docs/
    manual/
      manual.html
      manual.pdf
      manual.es.txt
      manual.en.txt
      assets/
        profiles/...
        signals/...
        ui/...
        fine/...
```

## Build del manual

```powershell
cd docs\manual
node build_manual.mjs      # HTML + TXT
node build_pdf.mjs         # PDF (requiere Playwright instalado)
node validate-manual.mjs   # CI validator
```

## Re-capturar pantallazos

```powershell
powershell -ExecutionPolicy Bypass -File docs\manual\scripts\capture_all.ps1
# o sólo una serie:
powershell -ExecutionPolicy Bypass -File docs\manual\scripts\capture_all.ps1 -Only profiles
```

Requisitos: `tubelight.exe` accesible en `D:\AgentWorkspace\Tubelight\`
(ajustable con `-ExePath`).

## Linux

Pendiente para v1.1. La integración debe sustituir `ShellExecuteW` por
`xdg-open` vía `system()` o `execvp()`.
