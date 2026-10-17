# Godot 4.7.2 para Nintendo Switch (homebrew)

Port da plataforma `switch` do fork Godot 3.5 para a Godot 4.7.2. Gera templates de
exportação `.nro` para homebrew (Atmosphère / hbmenu) e um exportador no editor.

## Diferenças 3.x → 4.x e como foram tratadas

| Área | Godot 3.5 (fork) | Godot 4.7.2 (este port) |
|---|---|---|
| Janela/entrada | `OS` cuidava de janela, vídeo e input | `OS_Switch` (subclasse de `OS_Unix`) + `DisplayServerSwitch` registrado com `register_create_function` |
| Renderização | GLES2/GLES3 via `RasterizerGLES*` | Renderer **Compatibility** (`opengl3_es`) sobre o Mesa/nouveau do devkitPro, contexto EGL em `gl_manager_switch_egl.*` |
| Dock/portátil | resolução fixa | ao trocar de modo, a surface EGL é recriada (`nwindowSetDimensions` 1920x1080 / 1280x720) |
| Áudio | audren | `AudioDriverSwitch` (audren, 2 wavebufs, thread própria) com a API de áudio da 4.x |
| Controles | joypad do `OS` | `JoypadSwitch` com 8 pads; ZL/ZR viram eixos de gatilho; layout igual ao do SDL (A da Godot = B da Nintendo) |
| Teclado | swkbd | `SwkbdInline` gerando eventos de tecla (`virtual_keyboard_show`) |
| Build | `detect.py` com `get_flags` em lista | `get_flags` em dict, `PROGSUFFIX .elf`, NRO montado no `SCsub` com `nacptool` + `elf2nro` |
| Exportação | `EditorExportPlatform` 3.x | `EditorExportPlatformSwitch` 4.x: opções de preset, ícone convertido em JPEG 256x256, NACP reescrito, PCK embutido no NRO e deploy em um clique via `nxlink` |
| PCK | `romfs:/game.pck` | continua aceito (`core/config/project_settings.cpp`), e o padrão agora é PCK embutido no final do NRO |

Também foi preciso mexer em alguns arquivos fora de `platform/switch`, sempre protegidos por `HORIZON_ENABLED`/`__SWITCH__`:
`drivers/unix/{file_access_unix,net_socket_unix,os_unix}.cpp`, `modules/websocket/SCsub`, e os thirdparty
Jolt, manifold e mbedtls (com arquivos de patch em `thirdparty/*/patches/`). Os headers POSIX que a newlib não tem
(`sys/un.h`, `ifaddrs.h`, `syslog.h`, `dlfcn.h`) ficam em `platform/switch/compat/`, e os stubs em `switch_compat.cpp`.

## Como compilar

Pré-requisitos: devkitPro com `switch-dev` e o Mesa do devkitPro (`switch-mesa`, `switch-libdrm_nouveau`), e `DEVKITPRO` definido.

```sh
scons platform=switch target=template_release   # bin/godot.switch.template_release.arm64.nro
scons platform=switch target=template_debug     # bin/godot.switch.template_debug.arm64.nro
```

Copie os arquivos para a pasta de templates como `switch_release.nro` e `switch_debug.nro`, ou aponte
`custom_template/*` no preset. O projeto precisa usar o renderer **Compatibility** (`gl_compatibility`).

Deploy em um clique: com o hbmenu aberto no modo netloader (botão Y), o console aparece como dispositivo
no botão de rodar do editor. O caminho do `nxlink` fica em *Editor Settings → export/switch/nxlink*.

## Limitações conhecidas

- **Nunca testado em hardware real.** O template compila e linka, mas a execução no console ainda não foi validada.
- Somente o renderer Compatibility. Forward+/Mobile (Vulkan) não estão disponíveis.
- Sem GDExtension/bibliotecas dinâmicas (Horizon não tem carregador dinâmico) e sem C#/Mono.
- Vibração (rumble) ainda não implementada.
- Formatos de textura: use ETC2/ASTC. Não foi confirmado se o Mesa expõe S3TC/BPTC.
- `OS.execute` não existe. `OS.create_process` usa `envSetNextLoad` (troca de NRO ao sair).
- Pode ser necessário ajustar o tamanho de pilha das threads e o comportamento do teclado virtual após testar no console.
