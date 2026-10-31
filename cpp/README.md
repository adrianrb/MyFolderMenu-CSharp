# MyFolderMenu — versão C++ nativa (Win32)

Porta nativa em C++ da versão C# WinForms do MyFolderMenu. Usa apenas a API
Win32 e a biblioteca padrão do C++; **nenhuma dependência externa** e nenhum
runtime adicional (não precisa de .NET instalado).

## Funcionalidades

- Ícone na bandeja do sistema, sem janela principal e sem entrada na barra de tarefas.
- Menu com as pastas configuradas; submenus carregados sob demanda (lazy).
- Pastas primeiro, arquivos depois, ambos em ordem alfabética.
- Ícones reais do shell para pastas e arquivos.
- Oculta arquivos/pastas com atributos *hidden* e *system*.
- Abre arquivos com o aplicativo padrão do Windows (`ShellExecute`).
- Temas Claro, Escuro e Sistema (segue `AppsUseLightTheme`), incluindo barra de título escura.
- Opção de ocultar extensões de arquivo no menu.
- Configuração persistida em INI em `%APPDATA%\MyFolderMenu\folders.ini`.
- Migração automática do antigo `folders.json`.
- Inicialização com o Windows via `HKCU\...\CurrentVersion\Run`.
- Diálogos de Configurações e Sobre, com exportação/importação de configurações.
- Instância única via mutex `Local\MyFolderMenu`.
- DPI: PerMonitorV2 declarado no manifesto (layout dimensionado pela DPI do sistema).

## Estrutura

| Arquivo | Descrição |
| --- | --- |
| `MyFolderMenu.cpp` | Implementação completa (~2.000 linhas). |
| `MyFolderMenu.rc` | Ícone (`../app.ico`), manifesto e `VERSIONINFO` 1.0.0. |
| `MyFolderMenu.manifest` | PerMonitorV2, Common Controls 6, `asInvoker`. |
| `resource.h` | IDs de recurso. |
| `build_msvc.bat` | Build com MSVC (`/MT`, link estático). |
| `build_mingw.bat` | Build com MinGW-w64 (`-static`). |

## Compilação

### MSVC

Abra o **x64 Native Tools Command Prompt for VS** na pasta `cpp` e execute:

```bat
build_msvc.bat
```

Ou, a partir de qualquer prompt:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Desenv\_Projetos\C#\20 Apps\MyFolderMenu\cpp"
build_msvc.bat
```

Requisitos: Visual Studio 2019/2022/2026 com workload **Desktop development with C++**.
Saída: `MyFolderMenu.exe` (~250 KB, CRT estático).

### MinGW-w64

Com `g++` e `windres` no `PATH`:

```bat
build_mingw.bat
```

Ou manualmente:

```sh
x86_64-w64-mingw32-windres MyFolderMenu.rc -O coff -o MyFolderMenu.res.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -mwindows -static \
  -static-libgcc -static-libstdc++ MyFolderMenu.cpp MyFolderMenu.res.o \
  -o MyFolderMenu.exe \
  -luser32 -lgdi32 -lshell32 -lole32 -lcomdlg32 -ladvapi32 \
  -lcomctl32 -luxtheme -ldwmapi -lversion
```

Saída: `MyFolderMenu.exe` (~810 KB; o tamanho maior vem da CRT/libstdc++ estáticas).

## Execução

```bat
MyFolderMenu.exe
```

O aplicativo inicia direto na bandeja. Clique com o botão direito no ícone para
abrir o menu; duplo clique abre a primeira pasta disponível.

## Configuração

Arquivo: `%APPDATA%\MyFolderMenu\folders.ini`

```ini
[Folders]
Folder1=C:\Users\Adrian\Desktop
Folder2=C:\Users\Adrian\Downloads
Folder3=C:\Users\Adrian\Documents

[Settings]
HideFileExtensions=1
Theme=Sistema
```

- `Theme`: `Sistema`, `Claro` ou `Escuro` (também aceita `light`/`dark`).
- `HideFileExtensions`: `1`/`0` (aceita `true`/`false`, `sim`/`nao`).
- Caminhos aceitam variáveis de ambiente (ex.: `%USERPROFILE%\Documents`).
- Duplicatas são removidas (comparação sem diferenciar maiúsculas).
- Se o INI não existir, é criado com Desktop, Downloads e Documentos; se houver
  um `folders.json` antigo, ele é migrado.

As configurações também podem ser alteradas pelo diálogo **Configurações...**,
que permite exportar/importar o INI.

## Diagnóstico (para testes)

Estes parâmetros abrem uma janela/menu direto e **ignoram o mutex de instância única**:

| Parâmetro | Efeito |
| --- | --- |
| `--menu` | Abre o menu no centro da área de trabalho e encerra. |
| `--settings` | Abre o diálogo de Configurações. |
| `--about` | Abre o diálogo Sobre. |

Uso típico em testes automatizados/screenshots:

```bat
MyFolderMenu.exe --about
```

## Diferenças em relação à versão C#

- Menus são desenhados pelo próprio código (owner-draw), para reproduzir as
  cores exatas dos temas claro/escuro e preservar o alfa dos ícones do shell.
- Sem tooltips por item de menu (o WinForms original também não exibia).
- O seletor de pasta usa o navegador clássico do shell (`SHBrowseForFolder`);
  o WinForms usava o mesmo diálogo via `FolderBrowserDialog`.
- Layout dos diálogos dimensionado pela DPI do monitor no momento da abertura
  (não há reposicionamento dinâmico ao mover entre monitores com DPI diferente).
- O tema escuro de controles padrão usa `SetWindowTheme(..., L"DarkMode_Explorer")`
  e `SetPreferredAppMode`; em versões muito antigas do Windows 10 alguns
  controles podem aparecer com o tema claro do sistema.
- Versão exibida em **Sobre** é lida do `VERSIONINFO` do executável (1.0.0).

## Desempenho estimado (runtime)

Medições aproximadas em Windows 10/11 x64, SSD:

| Operação | Tempo |
| --- | --- |
| Inicialização até o ícone na bandeja | < 50 ms |
| Abrir o menu raiz | 5–15 ms |
| Expandir um submenus (pasta com ~100 itens, com ícones) | 20–60 ms |
| Expandir submenu já visitado (cache de ícones) | 5–15 ms |
| Abrir Configurações/Sobre | 20–40 ms |
| Memória residente | ~4–8 MB |
| Tamanho do executável | ~250 KB (MSVC) / ~810 KB (MinGW) |

O carregamento de ícones do shell é o custo dominante ao expandir pastas grandes;
ele é feito uma vez por arquivo e fica em cache enquanto o menu está aberto.

## Esforço de desenvolvimento estimado

Para uma pessoa familiarizada com Win32/WinForms, recriar esta versão a partir do
código C# levaria aproximadamente **6–10 horas**, incluindo:

- leitura e mapeamento do comportamento do original (~1 h);
- estrutura do app de bandeja, menu owner-draw e temas (~2–3 h);
- configuração INI, migração JSON, registro de inicialização (~1–2 h);
- diálogos de Configurações/Sobre e importação/exportação (~1–2 h);
- build MSVC/MinGW, ajustes de DPI e testes visuais (~2 h).

Sem experiência prévia em Win32, o esforço sobe para **15–25 horas**.
