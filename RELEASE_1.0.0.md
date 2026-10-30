# MyFolderMenu 1.0.0

**Tag:** `v1.0.0`  
**Data:** 30/10/2026  
**Plataforma:** Windows x64  
**Runtime:** .NET 8 Desktop Runtime

Aplicativo de bandeja para acessar rapidamente suas pastas e arquivos favoritos, sem janela principal e sem poluir a barra de tarefas.

## Destaques

- Menu de pastas configuráveis diretamente no ícone da bandeja.
- Submenus carregados sob demanda, com pastas primeiro e arquivos depois.
- Arquivos abertos com o aplicativo padrão do Windows e ícones reais do shell.
- Configuração de inicialização com o Windows, exibição de extensões e tema.
- Temas Claro, Escuro e Sistema, incluindo barra de título escura.
- Exportação e importação completa das configurações.
- Instância única e configuração persistida em INI.

## Mudanças nesta versão

- Ícone do aplicativo alinhado ao ícone exibido na bandeja.
- Versão promovida para `1.0.0`.
- Configurações de tema, extensões e inicialização consolidadas.
- Exportação e importação de pastas e preferências pela tela de configurações.

## Instalação

1. Baixe o arquivo `MyFolderMenu-1.0.0-win-x64.zip`.
2. Extraia o conteúdo em uma pasta de sua preferência.
3. Execute `MyFolderMenu.exe`.
4. Opcionalmente, abra **Configurações...** e marque **Iniciar com o Windows**.

O aplicativo é portátil e não possui instalador.

## Requisitos

- Windows 10 ou Windows 11, 64 bits.
- .NET 8 Desktop Runtime (x64).

## Configuração

- Na primeira execução, são adicionadas as pastas Desktop, Downloads e Documentos.
- Use **Pastas** no menu da bandeja para adicionar ou remover diretórios.
- O arquivo de configuração fica em `%APPDATA%\MyFolderMenu\folders.ini`.
- A opção de ocultar extensões vem marcada por padrão.
- Exportar/importar inclui pastas, tema e preferência de extensões. **Iniciar com o Windows** é específico de cada usuário do Windows e não é incluído no arquivo.

## Arquivo

- `MyFolderMenu-1.0.0-win-x64.zip`
- SHA-256: `c8f8b0d56c25f433340e915b3d022f63dcc55020c42cdd8abc23e39adec9c961`

## Observações

- O executável não é assinado digitalmente; o Windows pode exibir um aviso do SmartScreen na primeira execução.
- Para usar o aplicativo, é necessário ter o .NET 8 Desktop Runtime instalado.
