# Risks — Tubelight

## R1 — Antivirus detecta inyección DLL como malware

**Probabilidad**: high
**Impacto**: med
**Detección temprana**: pruebas en VM Win11 con Defender activado durante F1; pruebas en máquinas con AV terceros (Kaspersky, Norton) durante F5.
**Mitigación**:
- Inyección sólo bajo demanda explícita del usuario (no daemon residente).
- Whitelisting documentado en USER_GUIDE.md.
- Firma de código con certificado EV (compra ~$300/año) si el proyecto encuentra adopción.
**Circuit breaker**: si la inyección dispara cuarentena en >50% de los AV populares en VirusTotal scan, abandonar inyección y vivir con fallback DXGI permanentemente. Reposicionar el proyecto como "overlay con +1 frame conocido" sin pretensión de cero lag.

## R2 — Latencia M1 (<2 ms) no alcanzable

**Probabilidad**: med
**Impacto**: high
**Detección temprana**: prototipo de hook Present() en F5.1 medido inmediatamente con FLM antes de implementar el pipeline completo.
**Mitigación**:
- Reducir passes (compute shader fusionado).
- Renderizar passes intermedios a 50% resolución y upscale al final.
- Async compute para bloom y persistence.
**Circuit breaker**: si tras optimización el lag se queda en 3-4 ms con pipeline completo, relajar M1 a `<5 ms` y documentar honestamente. Si >5 ms, repensar arquitectura (Vulkan layer único en lugar de hook DX directo).

## R3 — Calidad shader no supera CRT-Royale

**Probabilidad**: med
**Impacto**: high
**Detección temprana**: comparativa visual side-by-side en gate de F2 con golden frames y revisión humana.
**Mitigación**:
- Inversión en Pass −1 (señal) y Pass 5 (temporal) — donde otros shaders están débiles.
- Validación contra captura real de PVM cuando esté disponible.
**Circuit breaker**: si tras F4 el shader no es claramente superior, no liberar como "highest fidelity"; reposicionar como "primer overlay standalone con buen CRT" — diferenciar por arquitectura, no por shader puro.

## R4 — Datos físicos no disponibles para perfiles clave

**Probabilidad**: med
**Impacto**: med
**Detección temprana**: SOURCES.md ya tiene 8 huecos identificados.
**Mitigación**:
- Compra de service manuals físicos en eBay (~$30-50 por monitor).
- Contacto con comunidad shmups.system11.org y eevblog para mediciones.
- Petición a usuarios con PVM/BVM de medir dot pitch con regla milimétrica + foto.
**Circuit breaker**: si tras 4 semanas de búsqueda no se consigue número para un perfil, marcar el campo `[NEEDS-MEASUREMENT]` en JSON y publicar el perfil con valor aproximado documentado como tal. No bloquear F3.

## R5 — Vulkan layer rota anti-cheat de juegos

**Probabilidad**: low (en emuladores), high (en juegos AAA)
**Impacto**: med (no es el caso de uso principal)
**Detección temprana**: pruebas en F6 sólo con emuladores; documentar explícitamente que Tubelight no debe usarse sobre juegos competitivos online.
**Mitigación**:
- Vulkan layer marcada como `enable_environment` (no implicit) → usuario debe optar in.
- README enfatiza uso retro / contenido offline.
**Circuit breaker**: si Riot Vanguard / EAC bannean cuentas por presencia del layer, añadir warning prominente en UI cuando target sea un juego con anti-cheat conocido. No es responsabilidad de Tubelight evitar uso indebido.

## R6 — PipeWire screencast portal pide permisos en cada attach

**Probabilidad**: high
**Impacto**: low
**Detección temprana**: prueba en F6.4 con GNOME/KDE.
**Mitigación**:
- Solicitar token persistente via `xdg-desktop-portal` (soporte desde GNOME 41+).
- Documentar limitación en USER_GUIDE.
**Circuit breaker**: si el portal no permite persistencia, aceptar prompt cada vez que el usuario inicia Tubelight en modo fallback. UX subóptima pero funcional.

## R7 — Inyección DX12 más compleja de lo esperado

**Probabilidad**: med
**Impacto**: med
**Detección temprana**: F6.2.
**Mitigación**:
- Estudio del código de Special K y ReShade (ambos open source) para patrones probados.
- Comenzar con `IDXGISwapChain3::Present` antes de `Present1`.
**Circuit breaker**: si DX12 hook se atasca >2 semanas, recomendar a usuarios DX12 que activen el Vulkan layer (DXVK convierte automáticamente). Diferir DX12 nativo a v2.

## R8 — Comunidad RetroArch ve Tubelight como competencia y rechaza export .slangp

**Probabilidad**: low
**Impacto**: low
**Detección temprana**: PR exploratorio o thread en r/RetroArch tras F7.5.
**Mitigación**:
- Posicionar Tubelight como complementario (overlay para apps fuera de libretro).
- Export `.slangp` enmarcado como "para quien prefiera RetroArch".
**Circuit breaker**: si el formato evoluciona en libretro y rompe export, mantener compatibilidad con versión fijada y documentar.

## R9 — Falta de demo persuasivo

**Probabilidad**: med
**Impacto**: high (adopción)
**Detección temprana**: en F4 generar la demo de la cascada de Sonic, que es el caso visualmente más impactante.
**Mitigación**:
- Vídeo lado a lado: RGB → composite NTSC → emulador con Tubelight composite NTSC → captura de PVM real.
- Comparativa con CRT-Royale (mismo input, mismo perfil) mostrando dónde Tubelight ganan (Pass −1, persistencia).
**Circuit breaker**: si en F7 no hay capturas demostrativas convincentes, retrasar release v1.0 y dedicar 1 semana extra a contenido visual.

## R10 — Sesgo del autor (no es competencia real con shaders existentes)

**Probabilidad**: low pero costoso si ocurre
**Impacto**: critical
**Detección temprana**: revisión externa por un tercero familiarizado con CRT-Royale/Guest-Advanced antes del release.
**Mitigación**:
- Mantener apertura a contribuciones desde F3.
- Pedir review de la comunidad shmups/r/crtgaming en alpha cerrada.
**Circuit breaker**: si reviewer cualificado dice "no se nota diferencia", reposicionar el proyecto antes del release público — no quemar el lanzamiento con sobrepromesas.
